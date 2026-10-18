# Native startup baseline result

The one admitted two-frame run exited 0 in 14.065 seconds under the unchanged
60-second supervisor. It produced a decoded 1920x1080 RGBA PNG with SHA-256
`378a65dbe62e7e6c28601ba905f6f4c00defed0fdc0bab3f650ca1ee50135792`.

Manual image inspection confirms the Studio menu and toolbars, empty Start
viewport, authored Outliner Start row with count 0 and empty-scene notice,
Properties UI/Physics controls, and empty behavior panel. Text and icons render
legibly. This is empty-scene startup evidence, not interaction or scale coverage.
No remaining chibi-dev or studio-acceptance process was found after completion.

Overall validation FAILED because UBSan reported null pointer arguments at
Cute Framework cute_draw.cpp:5875 and null-member accesses at lines 5602, 5605,
5608 and 5611. The errors remain in invocation.log and startup.png.log. Exit 0
and a valid image do not override these diagnostics. No retry, sanitizer
suppression or renderer fallback was performed. All scratch is retained for
diagnosis. This uses installed SDK65ed and predates the scalar append candidate.
