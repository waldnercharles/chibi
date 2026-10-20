# Environment authoring specimen

A minimal recovery of historical Neon Swarm MaterialStudy: a camera, bounded
material world, and two identical authored terrain samples. The lower sample
runs the historical CutMaterial behavior on Tick; the upper sample is the
unchanged comparison. This is 2D material terrain, not a 3D heightfield.

The historical project is dirty and remains untouched. fixtures.json records
actual source hashes and the current migrated fixture sources used for the
executable closure. The recovered world drops five retired HRC lab fields using the existing
project migration policy; fixtures.json names the removed fields.
The current project uses version 24 directly; it does not
claim the entire version-5 game has been migrated. FieldShapes and CutMaterial
keep their original descriptions as source comments. No game movement, weapons, enemies,
fluid simulation or project-owned systems are required.

Sandbox's CaveField already has a current engine fixture in material-contracts;
its full gameplay scene is reference material, not a dependency of this specimen.

The CPU dispatcher prepares a portable player, generates equal nonempty terrain
masks, runs one authored Tick, then applies queued damage at the next material
preparation boundary. It compares original terrain masks separately from retained
fragments: the solid specimen remains unchanged, the cut point becomes air and
remaining source terrain decreases, while retained chips may conserve total mass.
A separate controller case validates the checked-in project. These checks provide
CPU generation/destruction evidence; they do not render the scene or prove GPU
appearance.

SeamStudy is a separate E1 acceptance scene: two 256 by 16 slabs at cell size
1, whose two storage chunks meet at x=0. Continuous kinematic sensor probes
exercise blocking and passage through the physics adapter. The cuttable slab
disables retained chips so the test isolates terrain collision from moving
debris. The historical MaterialStudy remains unchanged.
