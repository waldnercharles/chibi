import test from "node:test";
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { chooseEditorAction, chooseEditorPath, decisionRequest, decisionResult } from "./laya.mjs";

const args = {
  goal: "Create a blue circle",
  observation: "The shape menu is open. Circle and rectangle are available.",
  actions: [{ id: "circle", label: "Create a circle" }, { id: "rectangle", label: "Create a rectangle" }],
};
const response = (confidence = 0.9) => ({ model: "laya", answers: {
  action: { choice: "0", probabilities: { 0: confidence, 1: 1 - confidence }, confidence },
} });

test("local HTTP inference returns an offered action without executing it", async (t) => {
  let payload;
  const server = createServer(async (req, res) => {
    assert.equal(req.url, "/v1/decide");
    assert.equal(req.headers.authorization, "Bearer local-test-key");
    let body = "";
    for await (const chunk of req) body += chunk;
    payload = JSON.parse(body);
    res.setHeader("content-type", "application/json");
    res.end(JSON.stringify(response()));
  });
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  t.after(() => server.close());
  const result = await chooseEditorAction(args, { env: {
    LAYA_URL: `http://127.0.0.1:${server.address().port}/v1/decide`, LAYA_API_KEY: "local-test-key",
  } });
  assert.equal(result.result.action_id, "circle");
  assert.equal(result.result.executed, false);
  assert.equal(result.result.status, "action");
  assert.deepEqual(payload.state, { goal: args.goal, observation: args.observation });
  assert.deepEqual(payload.questions.action.criteria, { 0: "Create a circle", 1: "Create a rectangle" });
});

test("text decisions hand off to the supervisor without generating text", () => {
  const input = { ...args, actions: [{ id: "name", label: "Enter the asset name", needs_text: true }, args.actions[1]] };
  const result = decisionResult(input, decisionRequest(input), response(), 20);
  assert.equal(result.status, "needs_text");
  assert.equal(result.action_id, "name");
  assert.equal(result.text, undefined);
});

test("low confidence and repeated decisions on unchanged state hand off", () => {
  const request = decisionRequest(args);
  assert.equal(decisionResult(args, request, response(0.6), 20).reason, "low_confidence");
  const previous = decisionResult(args, request, response(), 20);
  assert.equal(decisionResult({ ...args, previous }, request, response(), 20).reason, "unchanged_observation");
  const changed = { ...args, observation: "The new circle is selected.", previous };
  assert.equal(decisionResult(changed, decisionRequest(changed), response(), 20).status, "action");
});

test("invalid provider answers cannot become actions", () => {
  for (const edit of [
    (a) => { a.choice = "999"; },
    (a) => { a.confidence = "0.9"; },
    (a) => { a.confidence = NaN; },
    (a) => { a.probabilities[0] = -1; },
    (a) => { a.probabilities[1] = 0.9; },
    (a) => { delete a.probabilities[1]; },
    (a) => { a.probabilities = { 0: 0.9, other: 0.1 }; },
    (a) => { a.choice = "1"; },
  ]) {
    const value = response();
    edit(value.answers.action);
    assert.throws(() => decisionResult(args, decisionRequest(args), value, 1));
  }
});

test("oversized and ambiguous menus fail before inference", () => {
  assert.throws(() => decisionRequest({ ...args, observation: "x".repeat(701) }));
  assert.throws(() => decisionRequest({ ...args, actions: Array(7).fill(args.actions[0]) }));
  assert.throws(() => decisionRequest({ ...args, actions: [args.actions[0], args.actions[0]] }));
  assert.throws(() => decisionRequest({ ...args, actions: [args.actions[0], { id: "other", label: "Create a circle" }] }));
  assert.throws(() => decisionRequest({ ...args, min_confidence: NaN }));
});

test("authentication failure does not expose the response or secret", async () => {
  await assert.rejects(chooseEditorAction(args, {
    env: { LAYA_API_KEY: "private-key" },
    fetch: async () => ({ ok: false, status: 401, json: () => { throw new Error("private response"); } }),
  }), /^Error: Laya HTTP 401;/);
});

function mockedLaya(confidences, requests) {
  return async (_, init) => {
    requests.push(JSON.parse(init.body));
    const confidence = confidences.shift();
    assert.notEqual(confidence, undefined, "unexpected model call");
    return { ok: true, json: async () => response(confidence) };
  };
}

test("focused questions traverse a validated path with bounded model calls", async () => {
  const requests = [];
  const validated = [];
  const result = await chooseEditorPath({ ...args, tree: {
    question: "Which editor target should change?",
    children: [
      { id: "player", label: "Player prototype", question: "Which Player field should change?", children: [
        { id: "hitbox", label: "Hitbox shape" }, { id: "color", label: "Sprite color" },
      ] },
      { id: "scene", label: "Current scene" },
    ],
  } }, {
    fetch: mockedLaya([0.9, 0.9], requests),
    validateLeaf: async (leaf, path) => { validated.push([leaf.id, path]); return leaf.id === "hitbox"; },
  });
  assert.equal(result.result.status, "candidate");
  assert.equal(result.result.action_id, "hitbox");
  assert.deepEqual(result.result.path, ["player", "hitbox"]);
  assert.equal(result.result.model_calls, 2);
  assert.deepEqual(validated, [["hitbox", ["player", "hitbox"]]]);
  assert.deepEqual(requests.map((request) => request.questions.action.instructions), [
    "Which editor target should change?", "Which Player field should change?",
  ]);
  assert.deepEqual(result.result.decisions.map((decision) => decision.action_id), ["player", "hitbox"]);
  assert.equal(result.result.executed, false);
});

test("an invalid leaf backtracks using existing probabilities and requests review", async () => {
  const requests = [];
  const checked = [];
  const result = await chooseEditorPath({ ...args, tree: {
    question: "Which action should be prepared?",
    children: [
      { id: "missing", label: "Change a missing asset" },
      { id: "available", label: "Change the available asset" },
    ],
  } }, {
    fetch: mockedLaya([0.9], requests),
    validateLeaf: async (leaf) => { checked.push(leaf.id); return leaf.id === "available"; },
  });
  assert.deepEqual(checked, ["missing", "available"]);
  assert.equal(requests.length, 1);
  assert.equal(result.result.status, "needs_supervisor");
  assert.equal(result.result.reason, "backtracked");
  assert.equal(result.result.action_id, "available");
  assert.equal(result.result.backtracks, 1);
  assert.deepEqual(result.result.rejected_paths, [["missing"]]);
});

test("search budgets and low confidence stop before a leaf is applied", async () => {
  const tree = { question: "Which action?", children: [
    { id: "bad", label: "Invalid action" }, { id: "good", label: "Valid action" },
  ] };
  const requests = [];
  const limited = await chooseEditorPath({ ...args, tree, max_backtracks: 0 }, {
    fetch: mockedLaya([0.9], requests), validateLeaf: (leaf) => leaf.id === "good",
  });
  assert.equal(limited.result.status, "needs_supervisor");
  assert.equal(limited.result.reason, "search_budget");
  assert.equal(limited.result.backtracks, 0);
  assert.equal(limited.result.action_id, null);
  let validations = 0;
  const uncertain = await chooseEditorPath({ ...args, tree }, {
    fetch: mockedLaya([0.6], requests),
    validateLeaf: () => { validations++; return true; },
  });
  assert.equal(uncertain.result.reason, "low_confidence");
  assert.equal(validations, 0);
});

test("catalog filtering removes unavailable branches before inference", async () => {
  const requests = [];
  const result = await chooseEditorPath({ ...args, tree: {
    question: "Which asset?", children: [
      { id: "instance", label: "Ship instance", question: "Which field?", children: [
        { id: "instance_color", label: "Color" }, { id: "instance_shape", label: "Shape" },
      ] },
      { id: "prototype", label: "Player prototype", question: "Which field?", children: [
        { id: "prototype_color", label: "Color" }, { id: "prototype_shape", label: "Shape" },
      ] },
    ],
  } }, {
    availableLeaf: (leaf) => leaf.id.startsWith("prototype_"),
    validateLeaf: () => true,
    fetch: mockedLaya([0.9], requests),
  });
  assert.equal(result.result.status, "candidate");
  assert.deepEqual(result.result.path, ["prototype", "prototype_color"]);
  assert.equal(result.result.model_calls, 1);
  assert.deepEqual(Object.values(requests[0].questions.action.criteria), ["Color", "Shape"]);
});

test("a single available leaf needs no model call", async () => {
  const tree = { question: "Which asset?", children: [
    { id: "instance", label: "Ship instance" }, { id: "prototype", label: "Player prototype" },
  ] };
  const fetch = () => { throw new Error("must not call Laya"); };
  const options = { availableLeaf: (leaf) => leaf.id === "prototype", validateLeaf: () => true, fetch };
  const selected = await chooseEditorPath({ ...args, tree }, options);
  assert.deepEqual(selected.result.path, ["prototype"]);
  assert.equal(selected.result.model_calls, 0);
  const empty = await chooseEditorPath({ ...args, tree }, { ...options, availableLeaf: () => false });
  assert.equal(empty.result.reason, "no_available_path");
  assert.equal(empty.result.model_calls, 0);
});

test("invalid trees fail before inference", async () => {
  const fetch = () => { throw new Error("must not call Laya"); };
  const validateLeaf = () => true;
  for (const tree of [
    { id: "only" },
    { children: [{ id: "a", label: "A" }, { id: "b", label: "B" }] },
    { question: "Which?", children: [{ id: "a", label: "A" }, { id: "a", label: "B" }] },
  ]) {
    await assert.rejects(chooseEditorPath({ ...args, tree }, { fetch, validateLeaf }));
  }
});

test("CLI searches an observed catalog without applying the choice", async (t) => {
  const server = createServer(async (req, res) => {
    for await (const _ of req) { /* Consume the request. */ }
    res.setHeader("content-type", "application/json");
    res.end(JSON.stringify(response()));
  });
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  t.after(() => server.close());
  const child = spawn(process.execPath, [new URL("./laya-cli.mjs", import.meta.url).pathname], {
    env: { ...process.env, LAYA_URL: `http://127.0.0.1:${server.address().port}/v1/decide`, LAYA_API_KEY: "" },
  });
  child.stdin.end(JSON.stringify({ ...args,
    available_action_ids: ["available", "other"],
    tree: { question: "Which action?", children: [
      { id: "missing", label: "Missing" }, { id: "available", label: "Available" },
      { id: "other", label: "Other available action" },
    ] },
  }));
  let output = "";
  for await (const chunk of child.stdout) output += chunk;
  const [exitCode] = await once(child, "exit");
  assert.equal(exitCode, 0);
  const result = JSON.parse(output).result;
  assert.equal(result.status, "candidate");
  assert.deepEqual(result.path, ["available"]);
  assert.equal(result.model_calls, 1);
  assert.equal(result.executed, false);
});
