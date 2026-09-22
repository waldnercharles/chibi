"""Small Jev-compatible HTTP service for the pinned Laya PR checkout."""

from __future__ import annotations

import asyncio
import os
from contextlib import asynccontextmanager
from typing import Annotated, Any, Dict, List, Literal, Optional, Union

import laya
import token_budget
from fastapi import FastAPI, HTTPException, Request, status
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, ConfigDict, Field


class ChoiceQuestion(BaseModel):
    model_config = ConfigDict(extra="forbid")

    type: Literal["choice"]
    instructions: Any
    criteria: Dict[str, Any]


class ScoreQuestion(BaseModel):
    model_config = ConfigDict(extra="forbid")

    type: Literal["score"]
    instructions: Any
    criteria: List[Any] = Field(min_length=2)


class NoulQuestion(BaseModel):
    model_config = ConfigDict(extra="forbid")

    type: Literal["noul"]
    instructions: Any
    criteria: Optional[Dict[str, Any]] = None


Question = Annotated[
    Union[ChoiceQuestion, ScoreQuestion, NoulQuestion],
    Field(discriminator="type"),
]


class DecideRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    model: str = "laya"
    state: Union[str, Dict[str, Any], List[Any]]
    questions: Dict[str, Question] = Field(min_length=1)


class DecisionResponse(BaseModel):
    model: str
    answers: Dict[str, Any]
    usage: Dict[str, Any]


class BatchRequest(BaseModel):
    model: str = "laya"
    states: List[Union[str, Dict[str, Any], List[Any]]] = Field(min_length=1, max_length=256)
    questions: Dict[str, Question] = Field(min_length=1)


class BatchResponse(BaseModel):
    model: str
    results: List[DecisionResponse]
    total_usage: Dict[str, int]


MODEL_ALIASES = {
    "convaiinnovations/laya": ("convaiinnovations/laya", None),
    "laya": ("convaiinnovations/laya", None),
    "english": ("convaiinnovations/laya", None),
    "convaiinnovations/laya-multilingual": ("convaiinnovations/laya", "multilingual"),
    "multilingual": ("convaiinnovations/laya", "multilingual"),
    "convaiinnovations/laya-typed-decisions": ("convaiinnovations/laya", "typed-decisions"),
    "typed-decisions": ("convaiinnovations/laya", "typed-decisions"),
}


def _settings() -> Dict[str, Optional[str]]:
    return {
        "model": os.environ.get("LAYA_MODEL", "laya"),
        "model_path": os.environ.get("LAYA_MODEL_PATH"),
        "device": os.environ.get("LAYA_DEVICE") or None,
        "token": os.environ.get("LAYA_HF_TOKEN") or os.environ.get("HF_TOKEN") or None,
        "api_key": os.environ.get("LAYA_API_KEY") or os.environ.get("TYPESAFE_API_KEY"),
    }


def _model_spec(name: str) -> tuple[str, Optional[str]]:
    return MODEL_ALIASES.get(name.strip().lower(), (name, None))


def _build_router() -> laya.Router:
    settings = _settings()
    repo, subfolder = (settings["model_path"], None) if settings["model_path"] else _model_spec(settings["model"] or "laya")
    router = laya.Router(
        models={"laya": (repo, subfolder)},
        device=settings["device"],
        token=settings["token"],
        max_loaded=1,
        default="laya",
    )
    router.preload(["laya"])
    return router


_router: Optional[laya.Router] = None
_capabilities = None


@asynccontextmanager
async def lifespan(_app: FastAPI):
    global _router, _capabilities
    _router = await asyncio.to_thread(_build_router)
    agent = _router.load('laya') if hasattr(_router, 'load') else None
    _capabilities = token_budget.metadata(agent) if agent else None
    yield
    if _router is not None:
        _router.unload()
    _router = None


app = FastAPI(
    title="Laya Jev Compatibility API",
    version=getattr(laya, "__version__", "unknown"),
    lifespan=lifespan,
)
app.add_middleware(
    CORSMiddleware,
    allow_origins=os.environ.get("LAYA_CORS_ORIGINS", "*").split(","),
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


def _check_auth(request: Request) -> None:
    expected = _settings()["api_key"]
    if expected and request.headers.get("authorization") != f"Bearer {expected}":
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail={"error": {"code": "unauthorized", "message": "Invalid or missing API key."}},
            headers={"WWW-Authenticate": "Bearer"},
        )


def _router_or_raise() -> laya.Router:
    if _router is None:
        raise HTTPException(status_code=503, detail="Laya model is still loading.")
    return _router


def _raw_questions(request: DecideRequest | BatchRequest) -> Dict[str, Dict[str, Any]]:
    return {name: question.model_dump(exclude_none=True) for name, question in request.questions.items()}


def _public_response(raw: Dict[str, Any]) -> DecisionResponse:
    response = dict(raw)
    response.pop("routing", None)
    response.setdefault("usage", {"input_tokens": 0, "output_tokens": 0})
    return DecisionResponse.model_validate(response)


async def _infer(state: Any, questions: Dict[str, Dict[str, Any]]) -> DecisionResponse:
    router = _router_or_raise()
    configured_model = _settings()["model"] or "laya"
    try:
        agent = router.load('laya') if hasattr(router, 'load') else None
        budget = token_budget.measure(agent, state, questions) if agent else None
        raw = await asyncio.to_thread(router.predict, state, questions, model="laya")
        if budget:
            raw.setdefault('usage', {})['budget'] = budget
            raw['usage']['model_metadata'] = _capabilities
    except ValueError as exc:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail={"error": {"code": "invalid_question", "message": str(exc)}},
        ) from exc
    response = _public_response(raw)
    if not response.model:
        response.model = configured_model
    return response


@app.get("/health")
async def health() -> Dict[str, str]:
    _router_or_raise()
    return {"status": "ok", "model": _settings()["model"] or "laya"}


@app.get("/v1/capabilities")
async def capabilities(request: Request):
    _check_auth(request)
    _router_or_raise()
    return _capabilities or {"available": False}


@app.get("/v1/models")
async def models() -> Dict[str, Any]:
    _router_or_raise()
    model = _settings()["model"] or "laya"
    return {
        "object": "list",
        "data": [{"id": model, "object": "model", "owned_by": "convai-innovations"}],
    }


@app.post("/v1/decide", response_model=DecisionResponse)
@app.post("/v1/systemone", response_model=DecisionResponse)
async def decide(request: DecideRequest, raw_request: Request) -> DecisionResponse:
    _check_auth(raw_request)
    return await _infer(request.state, _raw_questions(request))


@app.post("/v1/decide/batch", response_model=BatchResponse)
async def decide_batch(request: BatchRequest, raw_request: Request) -> BatchResponse:
    _check_auth(raw_request)
    questions = _raw_questions(request)
    results = await asyncio.gather(*[_infer(state, questions) for state in request.states])
    total_usage = {
        "input_tokens": sum(int(result.usage.get("input_tokens", 0)) for result in results),
        "output_tokens": sum(int(result.usage.get("output_tokens", 0)) for result in results),
    }
    return BatchResponse(model=_settings()["model"] or "laya", results=results, total_usage=total_usage)
