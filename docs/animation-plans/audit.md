# Native editor animation audit

Commit 90fc6e6f. Working tree inspected 2026-09-06, including concurrent feature work. Read-only source review; no live motion capture or frame-time measurements were performed. This is a focused audit of shared controls, sidebars, Inspector disclosure, graph camera, graph feedback and Scene floating controls. No suspicious instruction text was found in the source excerpts inspected.

The editor uses C, Dear ImGui draw lists and Cute Framework. It already has persistent per-item ImGui storage, a Reduced Motion preference, smooth Navigator width, disclosure rotation, hover color, camera interpolation and execution markers. The intended motion is quiet feedback in a frequently used technical editor. There is no browser compositor: CSS transforms, media queries and DevTools instructions in the skill references are translated into native draw geometry, ImGui input source, stored animation state and frame stepping. Changing native width does not imply browser style recalculation. It can still resize a canvas and allocate backing surfaces, so that cost needs measurement before declaring a performance defect.

## Vetted findings

| # | Severity | Category | Location | Finding | Fix summary |
| --- | --- | --- | --- | --- | --- |
| 1 | MEDIUM | Purpose/frequency, accessibility | editor/ui_shell.c:1011–1045 | Left/Right keyboard changes update the same smoothed chevron state as mouse activation. | Snap state on keyboard activation; retain pointer retargeting. Plan 001. |
| 2 | LOW | Timing, cohesion | editor/ui_shell.c:875 | Hover's 75ms exponential time constant reaches 95% in about 225ms and 99% in 345ms. This repeats across dense toolbars. | Reduce color tail to 40ms time constant, about 120ms to 95%; retain immediate active contrast. Plan 002. |
| 3 | LOW | Physicality, feedback | editor/ui_shell.c:885–912; engine/viewport.c:300 | Press state has clear active color but no physical compression. | Optional pointer-only 0.97 visual compression over 100ms, immediate release; fixed hit target and focus outline. Plan 002. |
| 4 | LOW | Timing | editor/editor.c:55,980 | Error toast has400ms linear fade after 4s readable dwell. | Keep dwell; shorten fade to200ms. Plan 004. |
| 5 | LOW | Missed opportunity | editor/ui_shell.c:758; engine/viewport.c:343 | Occasional Manage/More popups appear at full opacity without transition. | Pointer-only 125ms opacity entry; instant keyboard and dismissal. Plan 003. |

## Review verdict

Block approval of the keyboard disclosure animation until finding 1 is fixed. Remaining findings are polish, not functional blockers. Approve existing retargetable hover/disclosure storage, reduced-motion branch, direct pan while dragging and semantic execution indications within this audit's scope. Native performance and visual feel remain unverified.

| Before | After | Why |
| --- | --- | --- |
| Keyboard disclosure uses 40ms exponential tail, editor/ui_shell.c:1041 | Set progress equal to target in the input frame | Keyboard actions must be immediate. |
| Toolbar hover takes~225ms to 95%, editor/ui_shell.c:875 | ~120ms to 95%, no position change | Frequent hover needs less motion. |
| Pointer press only changes background, editor/ui_shell.c:885 | Keep active color and optionally compress drawn icon/backplate to 0.97 | Confirms contact without moving the hit rectangle. |
| Error fade 400ms, editor/editor.c:55 | Fade 200ms after unchanged 4s dwell | Keeps UI transition below300ms without shortening reading time. |
| Occasional popup immediate, engine/viewport.c:343 | Pointer entry 125ms opacity, keyboard instant | Makes appearance less abrupt while preserving exact native hit geometry. |

The popup recommendation deliberately omits scale and translation. Dear ImGui popup hit testing, clip rectangles, text rasterization and placement are not automatically transformed together. A native fade is a smaller safe adaptation than changing only draw vertices and leaving interaction behind. Do not add a custom popup animation framework merely to copy a CSS example.

## Opportunity pass

| # | Location | Today | Purpose | Frequency | Suggested motion |
| --- | --- | --- | --- | --- | --- |
| 1 | Shared icon button; Scene floating controls | Active color only | Pointer press feedback | Tens/day; reduced amplitude | Draw-only scale 1→0.97 in100ms ease-out, immediate release; optional in plan 002. |
| 2 | Manage attachment popup | Instant appearance | Prevent jarring appearance from trigger | Occasional | Mouse-only 125ms opacity0→1, strong ease-out; no delayed interaction. |
| 3 | Compact Scene More popup | Instant appearance | State indication | Occasional | Same shared mouse-only entry; no toolbar/group stagger. |
| 4 | Navigator collapse | Existing smooth width | Spatial consistency | Occasional | Preserve existing interruptible interpolation and reduced-motion snap; measure resize cost before changing. |

## Rejected candidates

| Candidate | Gate | Decision |
| --- | --- | --- |
| Back/Forward, Undo/Redo and Files selection content transitions | Frequency, keyboard | Keep immediate. A crossfade would obscure whether the new document is editable. |
| Node picker and search result row entrances | Frequency, keyboard | No entrance, stagger or filtering motion. |
| Node dragging, pin dragging and direct canvas pan | Direct manipulation | Keep coordinates attached to pointer; no smoothing between pointer and authored state. |
| Inspector component body height animation | Frequency, layout, complexity | Rotate pointer chevron only; contents and property hit boxes update immediately. |
| Branch labels and execution bubbles as decorative pulses | Semantics | Only actual trace traversal may animate; do not manufacture activity. |

## Coverage of the eight audit categories

| Category | Assessment |
| --- | --- |
| Purpose/frequency | Keyboard disclosure defect; frequent list and command actions excluded from additions. |
| Easing/duration | Existing exponentials responsive and interruptible; hover tail and toast fade are longer than useful. No ease-in found in inspected native controls. |
| Physicality/origin | Icons have fixed targets and centered ink. Pointer compression is bounded; popup transforms deferred due native geometry costs. |
| Interruptibility | Existing lerps retarget current state correctly. Any new press state must do the same. Dismissal stays immediate. |
| Performance | No new render target or shader proposed. Sidebar resize cost and graph GPU pressure need live measurement; source review cannot claim dropped frames. |
| Accessibility | Existing preference is respected. Preserve active color and focus cursor in reduced motion, disable press geometry. Track keyboard versus mouse at activation. |
| Cohesion/tokens | Put new native timing constants in ui_shell.c. Avoid a general engine or web dependencies. |
| Missed opportunities | Only occasional popup entry and subtle pointer press selected. |

## Evidence and implementation order

Plans follow the local improve-animations AUDIT.md and PLAN-TEMPLATE.md and review-animations STANDARDS.md. Their required custom ease-out is cubic-bezier(0.23,1,0.32,1), which must be evaluated as y at solved x, not y(t) alone. Existing exponential responses are documented rather than falsely labeled cubic Béziers. No plans require a spring, bounce or blur.

Implement 001 first, then 002 and 004. Plan 003 is independent and lower priority. User authorization already covers execution, so these plans do not require a selection round. Root coordinates ownership and builds. See icon-audit.md for a separately verified full font inventory and sidebar mapping.
