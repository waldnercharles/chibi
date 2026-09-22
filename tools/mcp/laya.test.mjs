import test from "node:test";
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { once } from "node:events";
import { chooseEditorAction, decisionRequest, decisionResult } from "./laya.mjs";

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
