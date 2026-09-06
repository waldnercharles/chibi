"""Exercise component identity across insertion, shared layouts, and renames.

Run with: python3 reflect/test_identity.py build/reflect
"""
import pathlib
import subprocess
import sys
import tempfile


reflect = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="reflect-identity-") as directory:
    directory = pathlib.Path(directory)
    source = directory / "components.c"
    manifest = directory / "components.tsv"
    output = directory / "registry.c"

    def generate(declarations):
        source.write_text(
            '#define COMPONENT(T) struct __attribute__((annotate("node:type"), annotate("node:component:index:dense"))) T\n'
            + declarations
        )
        subprocess.run(
            [str(reflect), str(source), str(output), "TEST", "--manifest", str(manifest)],
            check=True,
        )
        rows = [line.split() for line in manifest.read_text().splitlines() if not line.startswith("#")]
        return {row[4]: (row[2], row[3]) for row in rows}

    original = generate(
        "typedef COMPONENT(Health) { int current; } Health;\n"
        "typedef COMPONENT(Speed) { float value; } Speed;\n"
    )
    assert len(original) == 2, original
    inserted = generate(
        "typedef COMPONENT(Collider) { float value; } Collider;\n"
        "typedef COMPONENT(Health) { int current; } Health;\n"
        "typedef COMPONENT(Speed) { float value; } Speed;\n"
    )
    assert inserted["Health"] == original["Health"]
    assert inserted["Speed"] == original["Speed"]
    assert len({identity[0] for identity in inserted.values()}) == 3
    renamed = generate(
        "typedef COMPONENT(Collider) { float value; } Collider;\n"
        "\n\n"
        "typedef COMPONENT(Vitality) { int current; } Vitality;\n"
        "typedef COMPONENT(Speed) { float value; } Speed;\n"
    )
    assert renamed["Vitality"] == original["Health"]
    reverted = generate(
        "typedef COMPONENT(Collider) { float value; } Collider;\n"
        "typedef COMPONENT(Speed) { float value; } Speed;\n"
        "typedef COMPONENT(Health) { int current; int maximum; } Health;\n"
    )
    assert reverted["Health"] == original["Health"]
    assert reverted["Speed"] == original["Speed"]
    assert reverted["Collider"] == inserted["Collider"]
print("PASS: component identities survive insertion, shared layouts, renames, and alias restoration")
