---
name: prompt-refinement
description: Refine a prompt for Claude Opus 5.5 following Anthropic's guidance. Use when writing, reviewing, trimming or debugging a system prompt, CLAUDE.md/AGENTS.md, skill, subagent or task prompt, or an MCP tool description, or when a model misbehaves in a way the prompt should fix (stops early, over-verifies, widens scope, runs long, goes silent, follows pasted instructions).
---

Claude 5 models need less instruction than earlier models. Most refinement
removes rules the model already follows and keeps the facts it cannot infer.
Anthropic cut over 80% of Claude Code's system prompt for these models with no
measurable loss. Work through the passes in order; each ends on a checkable
state.

## Passes

1. **Frame it.** Establish what kind of prompt this is (API system prompt,
   chat product, unattended agent, human-in-the-loop agent, CLAUDE.md, skill,
   subagent brief, tool description), who reads its output, and the behavior
   the user has actually observed. Done when you can name the prompt kind and
   the observed symptom, or record that there is none yet. Without a symptom,
   only cut and clarify; add nothing.
2. **Cut.** Test every line with one question: would removing it cause a
   mistake? Remove:
   - verification and re-check instructions ("double-check", "add a final
     verification step", "use a subagent to verify"), which cause
     over-verification;
   - "think carefully", "reason step by step", and requests to write
     reasoning into the response. Effort controls thinking, and asking the
     model to reproduce its reasoning can trigger a `reasoning_extraction`
     refusal;
   - rules telling the model not to think;
   - anything the model can learn by reading the code or the data;
   - self-evident practice ("write clean code") and standard conventions;
   - repeated rules and stacked emphasis. Emphasis such as "IMPORTANT"
     belongs on at most one line the model keeps skipping;
   - conflicts between lines. Resolve each one explicitly.

   Done when every remaining line is a fact, a command, a project-specific
   convention, a gotcha, or a behavior fix tied to an observed symptom.
3. **Move.** Move content the model needs only sometimes out of the
   always-loaded prompt: occasional workflows into skills, detailed
   references into linked docs, and area-specific rules into a nested
   CLAUDE.md. Leave a one-line pointer only where nothing else would lead the
   model there (a skill's description already does). Controls that must be
   enforced go in permissions or hooks, not prose. Done when the always-loaded
   prompt holds only what applies to most sessions.
4. **Rewrite.** Use plain prose. Give the reason behind a rule so the model
   can use judgement, and describe what to do rather than what to avoid.
   When a general instruction keeps failing, name the specific patterns
   instead (see Frontend design in the reference). Prefer positive examples
   of the output style you want. Give rich references (code, tests, rubrics,
   a sample artifact) over thin specs. Done when each rule reads as a
   specific instruction a new colleague could follow.
5. **Add only what a symptom calls for.** Match each observed symptom to its
   pattern in [references/opus-5-5-patterns.md](references/opus-5-5-patterns.md)
   and adapt that pattern's wording; add nothing speculative. Harness and
   API settings come before prompt text: effort (default `medium`; lower it,
   rather than prompting, to get less thinking), `max_tokens`, thinking
   display, and time budgets. Done when every addition names the symptom it
   targets.
6. **Report.** Return the revised prompt in full, then a short change list,
   one line per change: what changed, and the guidance or symptom behind it.
   End with what to measure on the user's own tasks to confirm the change,
   such as completion, early stops, response length, tool calls, latency, or
   refusals.

## Scope notes

- Claude Code already supplies its own delegation, confirmation,
  pasted-content and progress-update instructions. Don't repeat them in a
  CLAUDE.md.
- A file shared with other agents (AGENTS.md) keeps model-neutral wording;
  Claude-specific tuning goes in a Claude-only file or a skill.
- Some sources these passes draw on:
  [Prompting Claude Opus 5.5](https://platform.claude.com/docs/en/build-with-claude/prompt-engineering/prompting-claude-opus-5-5),
  [Prompting Claude Opus 5](https://platform.claude.com/docs/en/build-with-claude/prompt-engineering/prompting-claude-opus-5),
  [Write an effective CLAUDE.md](https://code.claude.com/docs/en/best-practices#write-an-effective-claude-md),
  [The new rules of context engineering for Claude 5 generation models](https://claude.dev/blog/the-new-rules-of-context-engineering-for-claude-5-generation-models/).
