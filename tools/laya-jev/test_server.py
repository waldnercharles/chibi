from fastapi.testclient import TestClient

import server


class FakeRouter:
    def predict(self, state, questions, model):
        assert state["message"] == "charged twice"
        assert questions["department"]["instructions"]["goal"] == "route"
        assert questions["department"]["criteria"]["billing"]["topics"] == ["refunds"]
        return {
            "model": "laya-test",
            "routing": {"model": "english"},
            "answers": {
                "department": {
                    "type": "choice",
                    "choice": "billing",
                    "probabilities": {"billing": 0.9, "technical": 0.1},
                    "confidence": 0.8,
                    "action": {"act_probability": 1.0},
                }
            },
            "usage": {"input_tokens": 3, "output_tokens": 0},
        }

    def unload(self):
        pass


def test_systemone_accepts_structured_jev_questions(monkeypatch):
    monkeypatch.setattr(server, "_build_router", lambda: FakeRouter())
    with TestClient(server.app) as client:
        response = client.post(
            "/v1/systemone",
            json={
                "state": {"message": "charged twice"},
                "questions": {
                    "department": {
                        "type": "choice",
                        "instructions": {"goal": "route"},
                        "criteria": {"billing": {"topics": ["refunds"]}, "technical": "bugs"},
                    }
                },
            },
        )
    assert response.status_code == 200, response.text
    assert response.json()["answers"]["department"]["choice"] == "billing"
    assert "routing" not in response.json()
