import { readFileSync, writeFileSync } from "node:fs";

const fixture = JSON.parse(readFileSync(new URL("./laya-property-cases.json", import.meta.url)));
const keyFile = process.env.LAYA_API_KEY_FILE;
const key = process.env.LAYA_API_KEY || (keyFile ? readFileSync(keyFile, "utf8").trim() : "");
const endpoint = process.env.LAYA_URL || "http://127.0.0.1:8768/v1/decide";
const threshold = 0.8;
const orders = ["forward", "reverse"];
const modes = process.env.LAYA_EVAL_MODES?.split(",") || ["flat", "tree"];
const targetLabels = {
  player: "Player prototype; Ship instances inherit it",
  enemy: "Enemy prototype; Foe instances inherit it",
};
const fieldLabels = {
  color: "appearance color",
  shape: "collider hitbox shape",
  width: "visible sprite width",
};
const observedFields = {
  color: "Appearance Color, current #FFFFFFFF",
  shape: "Collider Shape, current Box",
  width: "Sprite Width, current 32",
};

function options(entries, order) {
  return order === "reverse" ? [...entries].reverse() : entries;
}

function namedTarget(goal) {
  const player = /\b(player|ship)\b/i.test(goal);
  const enemy = /\b(enemy|foe)\b/i.test(goal);
  return player === enemy ? null : player ? "player" : "enemy";
}

async function choose(testCase, instruction, entries, observation = fixture.observation) {
  const criteria = Object.fromEntries(entries.map((entry, index) => [String(index), entry.label]));
  const request = {
    state: { goal: testCase.goal, observation },
    questions: { action: { type: "choice", instructions: instruction, criteria } },
  };
  const start = performance.now();
  const response = await fetch(endpoint, {
    method: "POST",
    redirect: "error",
    signal: AbortSignal.timeout(15000),
    headers: { "content-type": "application/json", ...(key ? { authorization: `Bearer ${key}` } : {}) },
    body: JSON.stringify(request),
  });
  if (!response.ok) {
    throw new Error(`Laya HTTP ${response.status}: ${await response.text()}`);
  }
  const answer = (await response.json()).answers?.action;
  const index = Number(answer?.choice);
  if (!Number.isInteger(index) || !entries[index]) {
    throw new Error(`Invalid Laya choice: ${JSON.stringify(answer)}`);
  }
  const probability = answer.probabilities?.[String(index)];
  if (!Number.isFinite(probability) || !Number.isFinite(answer.confidence)) {
    throw new Error(`Invalid Laya confidence: ${JSON.stringify(answer)}`);
  }
  return {
    id: entries[index].id,
    confidence: Math.min(answer.confidence, probability),
    elapsed_ms: performance.now() - start,
  };
}

async function scoreFields(testCase, target, order) {
  const fields = options(Object.entries(fieldLabels), order);
  const questions = Object.fromEntries(fields.map(([id, label]) => [id, {
    type: "noul",
    instructions: `Would one edit to the ${target} prototype's ${label} fully satisfy the user request?`,
  }]));
  const start = performance.now();
  const response = await fetch(endpoint, {
    method: "POST",
    redirect: "error",
    signal: AbortSignal.timeout(15000),
    headers: { "content-type": "application/json", ...(key ? { authorization: `Bearer ${key}` } : {}) },
    body: JSON.stringify({ state: { goal: testCase.goal, observation: fixture.observation }, questions }),
  });
  if (!response.ok) throw new Error(`Laya HTTP ${response.status}: ${await response.text()}`);
  const answers = (await response.json()).answers;
  const scores = fields.map(([id]) => ({ id, score: answers?.[id]?.noul }));
  if (scores.some(({ score }) => !Number.isFinite(score))) {
    throw new Error(`Invalid Laya noul scores: ${JSON.stringify(answers)}`);
  }
  scores.sort((a, b) => b.score - a.score);
  return { id: scores[0].score >= 0.5 ? `${target}_${scores[0].id}` : null,
    confidence: scores[0].score, elapsed_ms: performance.now() - start };
}

async function run(testCase, mode, order) {
  const start = performance.now();
  const deferral = { id: null, label: "Defer; no single supported edit satisfies this request" };
  let raw = null;
  let confidence = 1;
  let calls = 0;
  if (mode === "flat") {
    const entries = options([
      ...fixture.actions.map((action) => ({ id: action.id, label: action.description })),
      deferral,
    ], order);
    const result = await choose(testCase,
      "Which one edit fully satisfies the request? Defer if none, ambiguous, conditional, or multiple edits are needed.",
      entries);
    raw = result.id;
    confidence = result.confidence;
    calls = 1;
  } else if (["scoped", "scoped_compact", "scoped_no_defer", "scoped_observed", "scoped_noul"].includes(mode)) {
    const target = namedTarget(testCase.goal);
    if (target) {
      const compact = mode === "scoped_compact" || mode === "scoped_no_defer";
      const observed = mode === "scoped_observed";
      const fields = options([
        ...Object.entries(fieldLabels).map(([id, label]) => ({
          id: `${target}_${id}`,
          label: observed ? observedFields[id] : compact ? `Change ${label}` : `Change ${targetLabels[target]}: ${label}`,
        })),
        ...(mode === "scoped_no_defer" || observed ? [] : [
          { ...deferral, label: compact ? "None of these edits" : deferral.label },
        ]),
      ], order);
      const result = mode === "scoped_noul"
        ? await scoreFields(testCase, target, order)
        : await choose(testCase,
          observed ? "Which property best fulfills the user's request?" :
            compact ? `Which single change to ${target} fulfills the request?` :
              `Which one property edit on the ${target} prototype fully satisfies the request? Defer if none does.`,
          fields,
          observed ? `Current target: ${target === "player" ? "Player prototype, used by Ship" : "Enemy prototype, used by Foe"}. Choices reflect current editor values.` : fixture.observation);
      raw = result.id;
      confidence = result.confidence;
      calls = 1;
    }
  } else if (mode === "tree") {
    const targets = options([
      ...Object.entries(targetLabels).map(([id, label]) => ({ id, label })),
      deferral,
    ], order);
    const target = await choose(testCase,
      "Which prototype should receive the one requested edit? Defer if unsupported, ambiguous, conditional, or multiple edits are needed.",
      targets);
    confidence = target.confidence;
    calls = 1;
    if (target.id) {
      const fields = options([
        ...Object.entries(fieldLabels).map(([id, label]) => ({ id, label: `Change ${targetLabels[target.id]}: ${label}` })),
        deferral,
      ], order);
      const field = await choose(testCase,
        `Which one property edit on the ${target.id} prototype fully satisfies the request? Defer if none does.`,
        fields);
      raw = field.id ? `${target.id}_${field.id}` : null;
      confidence = Math.min(confidence, field.confidence);
      calls++;
    }
  } else {
    throw new Error(`Unknown mode ${mode}`);
  }
  const accepted = confidence >= threshold ? raw : null;
  return {
    id: testCase.id,
    goal: testCase.goal,
    expected: testCase.expected,
    mode,
    order,
    raw,
    confidence,
    accepted,
    calls,
    elapsed_ms: performance.now() - start,
  };
}

function percentile(numbers, p) {
  const sorted = [...numbers].sort((a, b) => a - b);
  return Math.round(sorted[Math.ceil(p * sorted.length) - 1] || 0);
}

function summarize(rows) {
  const eligible = rows.filter((row) => row.expected !== null);
  const deferrals = rows.filter((row) => row.expected === null);
  return {
    cases: rows.length,
    eligible: eligible.length,
    unsupported: deferrals.length,
    raw_correct: eligible.filter((row) => row.raw === row.expected).length,
    accepted_correct: eligible.filter((row) => row.accepted === row.expected).length,
    accepted_wrong: rows.filter((row) => row.accepted !== null && row.accepted !== row.expected).length,
    correct_deferrals: deferrals.filter((row) => row.accepted === null).length,
    calls: rows.reduce((sum, row) => sum + row.calls, 0),
    latency_p50_ms: percentile(rows.map((row) => row.elapsed_ms), 0.5),
    latency_p95_ms: percentile(rows.map((row) => row.elapsed_ms), 0.95),
  };
}

await choose(fixture.cases[0], "Which prototype?", [
  { id: "player", label: targetLabels.player },
  { id: "enemy", label: targetLabels.enemy },
]);

const rows = [];
for (const testCase of fixture.cases) {
  for (const order of orders) {
    for (const mode of modes) {
      const row = await run(testCase, mode, order);
      rows.push(row);
    }
  }
  if (rows.length % 24 === 0) {
    process.stderr.write(`Completed ${rows.length}/${fixture.cases.length * orders.length * modes.length} decisions\n`);
  }
}
const summary = Object.fromEntries(modes.map((mode) => [mode,
  Object.fromEntries(orders.map((order) => [order,
    summarize(rows.filter((row) => row.mode === mode && row.order === order))]))]));
const output = { fixture: "laya-property-cases.json", threshold, endpoint, summary, rows };
const outputPath = process.env.LAYA_EVAL_OUTPUT;
if (outputPath) writeFileSync(outputPath, JSON.stringify(output, null, 2) + "\n");
process.stdout.write(JSON.stringify(summary, null, 2) + "\n");
