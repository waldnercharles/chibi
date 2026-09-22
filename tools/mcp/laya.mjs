import { existsSync, readFileSync } from "node:fs";
import { createHash } from "node:crypto";

const text = (maxLength) => ({ type: "string", minLength: 1, maxLength });
const actionSchema = {
  type: "object", additionalProperties: false, required: ["id", "label"],
  properties: { id: text(40), label: text(64), needs_text: { type: "boolean" } },
};

export const layaToolDefinition = {
  name: "chooseEditorAction",
  outputSchema: { type: "object", required: ["ok", "result"], properties: {
    ok: { type: "boolean" }, result: { type: "object" },
  } },
  inputSchema: {
    type: "object", additionalProperties: false,
    required: ["goal", "observation", "actions"],
    properties: {
      goal: text(200),
      observation: text(700),
      actions: { type: "array", minItems: 2, maxItems: 6, items: actionSchema },
      min_confidence: { type: "number", minimum: 0, maximum: 1 },
      previous: {
        type: "object", additionalProperties: false,
        required: ["observation_hash", "action_id"],
        properties: { observation_hash: text(64), action_id: text(40) },
      },
    },
  },
};

function check(condition, message) {
  if (!condition) throw new Error(message);
}

export function decisionRequest(args) {
  for (const [key, max] of [["goal", 200], ["observation", 700]]) {
    check(typeof args[key] === "string" && args[key].trim() && args[key].length <= max,
      `${key} must contain 1-${max} characters`);
  }
  check(Array.isArray(args.actions) && args.actions.length >= 2 && args.actions.length <= 6,
    "Offer 2-6 distinct actions, grouping a larger menu before calling Laya");
  const ids = new Set();
  const labels = new Set();
  for (const action of args.actions) {
    check(typeof action.id === "string" && /^[a-zA-Z0-9_-]{1,40}$/.test(action.id), "Invalid action id");
    check(typeof action.label === "string" && action.label.trim() && action.label.length <= 64,
      "Action labels must contain 1-64 characters");
    check(!ids.has(action.id) && !labels.has(action.label.trim().toLowerCase()), "Actions must be distinct");
    check(action.needs_text === undefined || typeof action.needs_text === "boolean", "needs_text must be boolean");
    ids.add(action.id);
    labels.add(action.label.trim().toLowerCase());
  }
  const threshold = args.min_confidence ?? 0.65;
  check(Number.isFinite(threshold) && threshold >= 0 && threshold <= 1, "Invalid confidence threshold");
  // Ordinal keys and short labels leave more of Laya's head budget for the choices.
  // These character limits bound requests; they are not a tokenizer guarantee.
  const criteria = Object.fromEntries(args.actions.map((action, i) => [String(i), action.label]));
  return {
    state: { goal: args.goal, observation: args.observation },
    questions: {
      action: {
        type: "choice",
        instructions: "Choose the next action that advances the goal given the current observation.",
        criteria,
      },
    },
  };
}

export function decisionResult(args, request, response, elapsedMs) {
  const answer = response?.answers?.action;
  check(answer && Object.hasOwn(request.questions.action.criteria, answer.choice), "Laya returned an unknown choice");
  const probabilities = answer.probabilities;
  const keys = Object.keys(request.questions.action.criteria);
  check(probabilities && !Array.isArray(probabilities) && Object.keys(probabilities).length === keys.length &&
    keys.every((key) => Number.isFinite(probabilities[key]) && probabilities[key] >= 0 && probabilities[key] <= 1),
  "Laya returned an invalid probability distribution");
  const total = keys.reduce((sum, key) => sum + probabilities[key], 0);
  check(Math.abs(total - 1) < 0.02, "Laya probabilities do not sum to one");
  check(Number.isFinite(answer.confidence) && answer.confidence >= 0 && answer.confidence <= 1,
    "Laya returned invalid confidence");
  check(probabilities[answer.choice] + 0.000001 >= Math.max(...Object.values(probabilities)),
    "Laya choice disagrees with its probabilities");
  const action = args.actions[Number(answer.choice)];
  const observationHash = createHash("sha256").update(args.observation).digest("hex");
  const confidence = Math.min(answer.confidence, probabilities[answer.choice]);
  const repeated = args.previous?.observation_hash === observationHash && args.previous?.action_id === action.id;
  const reason = repeated ? "unchanged_observation" : confidence < (args.min_confidence ?? 0.65) ? "low_confidence" : null;
  return {
    status: reason ? "needs_supervisor" : action.needs_text ? "needs_text" : "action",
    reason,
    action_id: action.id,
    label: action.label,
    confidence,
    probabilities: Object.fromEntries(args.actions.map((candidate, i) => [candidate.id, probabilities[String(i)]])),
    observation_hash: observationHash,
    model: response.model,
    elapsed_ms: Math.round(elapsedMs),
    executed: false,
  };
}

export async function chooseEditorAction(args, options = {}) {
  const request = decisionRequest(args);
  const env = options.env || process.env;
  const endpoint = new URL(env.LAYA_URL || "http://127.0.0.1:8768/v1/decide");
  check(["http:", "https:"].includes(endpoint.protocol) && !endpoint.username && !endpoint.password,
    "LAYA_URL must be an HTTP endpoint without embedded credentials");
  const localKey = new URL("../../.chibi/laya-api-key", import.meta.url);
  const keyFile = env.LAYA_API_KEY_FILE || (existsSync(localKey) ? localKey : null);
  const key = env.LAYA_API_KEY || (keyFile ? readFileSync(keyFile, "utf8").trim() : "");
  const started = performance.now();
  const result = await (options.fetch || fetch)(endpoint, {
    method: "POST", redirect: "error", signal: AbortSignal.timeout(15000),
    headers: { "content-type": "application/json", ...(key ? { authorization: `Bearer ${key}` } : {}) },
    body: JSON.stringify(request),
  });
  check(result.ok, `Laya HTTP ${result.status}; check LAYA_URL and LAYA_API_KEY or LAYA_API_KEY_FILE`);
  const response = await result.json();
  return { contract: 1, operation: "choose_editor_action", ok: true,
    result: decisionResult(args, request, response, performance.now() - started) };
}
