# Material authoring in Studio

This describes the current native implementation in the asset-authoring
candidate. Its C-driven panel does not yet meet the
[authored editor target](studio-ui-authoring.md#authored-editor-target), which
requires RML, RCSS and `.chibi` ownership of the editor workflow.

Open a material's graph and its Node Properties. The asset name identifies the
preview. Parameters edits the authored defaults with numeric, vector, boolean
and color controls. Textures chooses project images. Save commits the source to
disk; each numeric scrub or color-picker session is one undo step. Numeric Escape
restores the value. The shared color picker commits when closed and supports Undo.

Drag the preview to orbit a material, and scroll to zoom. Preview settings offers
the same view controls through the keyboard. Time is fixed so successive edits
can be compared at the same shader time. Shapes have a flat preview with zoom.
Source or image revisions invalidate the retained picture; unchanged previews
spend no additional rendering work. A failed publication or rendering attempt
shows an error and disables evidence export.

Diagnostics starts collapsed. It contains compiler resource estimates and a CPU
sample at a chosen UV and the preview time. The sample uses local/world origin,
normal/view +Y and zero beat/influence. Scalar results show one value and colors
show RGB. These are shader outputs before lighting. Resource counts are static
estimates, not GPU timings or full raymarch costs. Model Properties similarly
keeps resource diagnostics and anchor-transform inventories collapsed, with
actual capacity warnings visible during authoring.

Export evidence freezes the successfully published shader, current resident image
inputs, sample and view before starting asynchronous readback. It rejects disk
images that disagree with the resident pixels. A new directory under
`.chibi/evidence/` contains a small preview project, linked material/shape snapshot,
exact image dependencies, `preview.png`, `report.json`, `manifest.json` and
`requests.ndjson`. Browser Studio downloads the bundle as a ZIP.

From the bundle directory, replay with:

```sh
chibi_ctl --project-root . --render < requests.ndjson
```

The report distinguishes graph validation, CPU sampling, GPU rendering and full
verification. Use the recorded SDK and backend for pixel comparison; this does
not promise identical pixels across graphics backends or platforms. FNV-1a
fingerprints detect drift, not authenticity. Failed file publication reports a
retained partial directory; a completed manifest is written last.

The interface follows Studio's kit and design tokens. Prior art informed the
preview interaction, typed parameters and deliberate diagnostics:
[Unity Main Preview](https://docs.unity3d.com/Packages/com.unity.shadergraph@17.0/manual/Main-Preview.html),
[Godot uniform hints and groups](https://docs.godotengine.org/en/stable/tutorials/shaders/shader_reference/shading_language.html#uniform-hints),
and [Unreal Material Editor](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-material-editor-ui).

The focused native acceptance route runs one disposable Studio host for exactly
60 hidden frames at a fixed 1/60-second timestep. It exercises creation, typed
roughness editing, a live scrub canceled with Escape, undo/redo, save, native
scrollbar-thumb navigation into Diagnostics, save-toast swipe dismissal,
sampling, evidence export and duplicate-name refusal.
Two tiny in-memory material documents additionally check pending-gesture history
across document switches, immediate undo/redo and cancellation with existing redo.
They also cover pending edits across local-function navigation. These history
checks do not publish assets or create another renderer. A further SDL scrub is
undone before pointer release; a subsequent typed edit must form a fresh undo
step, clear redo, and undo back to the saved and exported value.
One auxiliary material and shader library also check that publishing a library
edit refreshes the open material's CPU report without changing its edit revision.
Both definitions serialize to at most 16 KiB and are removed after the check;
they reuse the same renderer and add no frames or readbacks.

When `CHIBI_ACCEPTANCE_EVIDENCE_DIR` names a fresh review output directory, the
same run captures three existing UI frames and preserves the six completed bundle
files under `bundle/`: manifest, report, replay request, project, preview PNG and
linked material snapshot. Copying is bounded to 1 MiB per file through a 16 KiB
buffer. It uses exclusive creation and removes its own partial copies on failure.
The one asynchronous preview readback and three optional screenshot readbacks
remain inside the route's shared 60-second deadline; they add no host frames.
This route does not establish color-picker interaction, touch, every preset or
cross-backend pixel equivalence.
