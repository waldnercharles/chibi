# Studio workspace contracts

This synthetic project owns two scene subjects, four UI documents and an idle
replay. It exercises designer panels, camera overlays, assets virtualization,
graph completion and graph tab lifetimes without a playable game composition.

Movement and Weapon and their callable dependencies are frozen regression
snapshots. Weapon must stay stable for completion and close/reopen checks. Components.chibi supplies the reflected labels those checks
use. `fixtures.json` records extraction sources, purposes and hashes.

The Firefight name belongs to the existing replay assertion; its sixty-frame idle
scenario only exercises authoring lifetimes. Game encounter/input acceptance and
expensive thumbnail/benchmark cases retain their original owning projects.
This fixture migration does not establish Studio or GPU execution coverage.
