# Sprite fixtures

The Lantern Yard scene draws `GameSprite` frames from `content/images/props.png`
without any platformer mechanics. It covers frame cells, `flip_x`, a turned
sprite, a translated and rotated 2×2 decorative `GameTilemap` with one empty
cell, a Material that tints one crate, emissive sprites that light the yard
through HRC, and a sprite with zero frame sizes that shows the whole sheet.
Props and the tilemap carry `props_normal.png` and `props_height.png`, normal
and height maps bevelled from the sheet's alpha, standing 4 units high. The
yard is an opaque floor, which leaves HRC's surface light no open space to
bring into the props, so the maps reach the lighting targets but do not yet
change the picture.
Preview it with `chibi_ctl --project-root test/fixtures/sprites --render`.
