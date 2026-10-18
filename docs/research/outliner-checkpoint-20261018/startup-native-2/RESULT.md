# Native startup2 result

PASS: one reviewed two-frame invocation, exit0, 15.814s under the unchanged60s production supervisor.

Root decoded and manually inspected the fresh1920x1080 RGBA PNG. Visible: Start scene, empty Outliner header/count0 and no-objects notice, filter input, menus/toolbars, Properties UI/Physics sections, bottom behavior panel and status bar. No missing-font blocks or malformed panels observed. The image matches the previous baseline pixels, while the previous Cute UBSan diagnostics are absent. This is startup/readback only, not interaction or hardware-performance evidence.

Full18-line log reviewed:16font loads, screenshot saved, supervisor timing. No application failure marker, ASan/UBSan/LSan diagnostic. No matching chibi-dev process remains; production supervisor completed cleanup. Default sanitizer settings remained enabled.

- `run-startup.sh` SHA256 `2805f4913a9f65a0f8f9aa7b7fcd20ce358e896a7b347343802b896cc982d692`
- `README.md` SHA256 `614665eb88b4bf17eee98c29c5fe7efb5197897be8286dad826947ac090bb31e`
- `startup.png` SHA256 `378a65dbe62e7e6c28601ba905f6f4c00defed0fdc0bab3f650ca1ee50135792`
- `startup.png.log` SHA256 `45c066b37c9dc123bf38cd6013ac66a1697ecb144eec370d5a62589977802d62`
- `invocation.log` SHA256 `065f3d11e85212f30ae66d9700c773a81bc171ee3a027a51ad55f9960738bbf0`

After passing, the task-owned runtime and project copies were removed per repository scratch-cleanup policy. PNG, full logs, source/resource/project manifests and build provenance remain. The original SDK10 and Studio builds remain intact.
