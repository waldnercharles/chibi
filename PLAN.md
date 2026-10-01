Summary: Switch authored assets to KDL 2 and make their internal documents native C values. Text, Studio controls and MCP share asset semantics and preserve authored source.

Status: Complete. All 245 authored assets use KDL 2, all project manifests use version 4, and asset models and snapshots use owned native C values.

Context: YAML previously converted into yyjson documents. Graph snapshots and scene component overrides also used serialized JSON. The built-in code editor provides KDL completion, highlighting, diagnostics and hover through the shared asset source service.

System impact: KDL is the authored source. Native graphs, prototypes and component overrides form the validated model and retain override intent. Protocol adapters convert MCP/collaboration JSON at the boundary. Source edits retain the last valid preview; visual edits patch the authored text. The project version changes from 3 to 4, and all asset paths/manifests migrate directly.

Changes:
- `src/support/asset_value.*`, `asset_text.*`, `asset_json.*`: native values, KDL parsing/emission and explicit protocol conversion.
- `src/graph/document/`, `src/assets/`: native document loading, validation and semantic mutations.
- `src/editor/documents/`, `src/editor/workspace/`: native undo snapshots and component overrides.
- `src/assets/asset_source.*`, Studio code editor: source ranges, minimal edits, reflected completion, highlighting and diagnostics.
- `content/`, `projects/`, fixtures, tools and packaging: KDL assets and manifest/path migration.
- Tests and authoring docs: new contracts, source preservation and format validation.

Verification:
- Passed native value and KDL round trips, scalar type/ID boundaries, syntax errors, source reconciliation and checkpoint restoration tests.
- Passed graph, prototype, scene, undo, refactoring, collaboration and MCP authoring checks, including ordered signature changes and native duplication.
- Passed `make build`, the full `make test` target and whole-function audit, `make check-format`, and canonical asset checks across every project. Native validation used ASan/UBSan and the installed macOS 26.5 SDK.
- Passed serial and threaded `make web` builds and backend checks. Rendered Studio validation confirmed highlighting, component/field completion, diagnostic recovery and undo to pristine source; source/visual reconciliation and last-valid preview are covered by the native tests.

The migration includes no new CSS theme system; RML/RCSS continues to own UI structure and styling.
