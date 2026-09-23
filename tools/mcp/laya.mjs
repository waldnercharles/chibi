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
      question: text(200),
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
  check(args.question === undefined ||
    (typeof args.question === "string" && args.question.trim() && args.question.length <= 200),
  "Question must contain 1-200 characters");
  // Ordinal keys and short labels leave more of Laya's head budget for the choices.
  // These character limits bound requests; they are not a tokenizer guarantee.
  const criteria = Object.fromEntries(args.actions.map((action, i) => [String(i), action.label]));
  return {
    state: { goal: args.goal, observation: args.observation },
    questions: {
      action: {
        type: "choice",
        instructions: args.question || "Choose the next action that advances the goal given the current observation.",
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

function validateTree(node, depth, ids) {
  check(node && typeof node === "object" && !Array.isArray(node) && depth <= 3,
    "Decision tree exceeds three levels");
  if (!node.children) {
    check(depth > 0, "Decision tree needs choices");
    check(node.needs_text === undefined || typeof node.needs_text === "boolean", "Invalid text choice");
    return;
  }
  check(Array.isArray(node.children) && node.children.length >= 1 && node.children.length <= 6,
    "Each decision must offer 1-6 choices");
  check(node.needs_text === undefined, "Only leaves can request text");
  check(node.children.length === 1 ||
    (typeof node.question === "string" && node.question.trim() && node.question.length <= 200),
  "Each decision needs a focused question");
  const labels = new Set();
  for (const child of node.children) {
    check(child && typeof child.id === "string" && /^[a-zA-Z0-9_-]{1,40}$/.test(child.id) &&
      !ids.has(child.id), "Decision IDs must be unique");
    check(typeof child.label === "string" && child.label.trim() && child.label.length <= 64 &&
      !labels.has(child.label.trim().toLowerCase()), "Sibling labels must be distinct");
    ids.add(child.id);
    labels.add(child.label.trim().toLowerCase());
    validateTree(child, depth + 1, ids);
  }
}

async function availableTree(node, path, availableLeaf) {
  if (!node.children) return await availableLeaf(node, path.map((part) => part.id)) ? node : null;
  const children = (await Promise.all(node.children.map((child) =>
    availableTree(child, [...path, child], availableLeaf)))).filter(Boolean);
  return children.length ? { ...node, children } : null;
}

// Search only proposals. The caller checks a leaf against a read-only typed
// catalog or dry run, then decides whether to apply the returned proposal.
export async function chooseEditorPath(args, options = {}) {
  check(typeof options.validateLeaf === "function", "A read-only leaf validator is required");
  check(options.availableLeaf === undefined || typeof options.availableLeaf === "function",
    "Available-leaf filter must be a function");
  check(args && typeof args === "object", "Decision request is required");
  for (const [key, max] of [["goal", 200], ["observation", 700]]) {
    check(typeof args[key] === "string" && args[key].trim() && args[key].length <= max,
      `${key} must contain 1-${max} characters`);
  }
  check(Number.isInteger(args.max_decisions ?? 3) && (args.max_decisions ?? 3) >= 1 &&
    (args.max_decisions ?? 3) <= 4, "Invalid decision budget");
  check(Number.isInteger(args.max_backtracks ?? 1) && (args.max_backtracks ?? 1) >= 0 &&
    (args.max_backtracks ?? 1) <= 2, "Invalid backtrack budget");
  validateTree(args.tree, 0, new Set());
  const started = performance.now();
  const state = { calls: 0, backtracks: 0, model_ms: 0, rejected: [], decisions: [], reason: null };
  const tree = options.availableLeaf ? await availableTree(args.tree, [], options.availableLeaf) : args.tree;

  async function visit(node, path) {
    if (!node.children) {
      if (await options.validateLeaf(node, path.map((part) => part.id))) return { leaf: node, path };
      state.rejected.push(path.map((part) => part.id));
      return null;
    }
    if (node.children.length === 1) return visit(node.children[0], [...path, node.children[0]]);
    if (state.calls >= (args.max_decisions ?? 3)) {
      state.reason = "search_budget";
      return null;
    }
    state.calls++;
    const result = (await chooseEditorAction({
      goal: args.goal, observation: args.observation, question: node.question,
      actions: node.children.map(({ id, label, needs_text }) => ({ id, label, needs_text })),
      min_confidence: args.min_confidence,
    }, options)).result;
    state.model_ms += result.elapsed_ms;
    state.decisions.push({
      path: path.map((part) => part.id), question: node.question,
      action_id: result.action_id, confidence: result.confidence,
      probabilities: result.probabilities, reason: result.reason, elapsed_ms: result.elapsed_ms,
    });
    if (result.status === "needs_supervisor") {
      state.reason = result.reason;
      return null;
    }
    const ranked = [...node.children].sort((a, b) => result.probabilities[b.id] - result.probabilities[a.id]);
    for (let i = 0; i < ranked.length; i++) {
      if (i > 0) {
        if (state.backtracks >= (args.max_backtracks ?? 1)) {
          state.reason = "search_budget";
          return null;
        }
        state.backtracks++;
      }
      const candidate = await visit(ranked[i], [...path, ranked[i]]);
      if (candidate || state.reason) return candidate;
    }
    return null;
  }

  const candidate = tree ? await visit(tree, []) : null;
  const leaf = candidate?.leaf;
  const selected = leaf && state.rejected.length ? "needs_supervisor" : leaf?.needs_text ? "needs_text" : leaf ? "candidate" : "needs_supervisor";
  return { contract: 1, operation: "choose_editor_path", ok: true, result: {
    status: selected, reason: state.reason || (leaf && state.rejected.length ? "backtracked" : leaf ? null :
      tree ? "no_valid_path" : "no_available_path"),
    action_id: leaf?.id || null, path: candidate?.path.map((part) => part.id) || null,
    rejected_paths: state.rejected, decisions: state.decisions,
    model_calls: state.calls, backtracks: state.backtracks,
    model_ms: state.model_ms, elapsed_ms: Math.round(performance.now() - started), executed: false,
  } };
}
