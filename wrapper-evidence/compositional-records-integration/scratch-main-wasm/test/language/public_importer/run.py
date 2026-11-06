"""Replay the public importer proof using a configured, bytecode-only SDK build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

BASE = "69cb3f268baaeecc1e34c3add22aa836f072f2b4"
PUBLIC_SHA256 = "3c20a8b4271f60b6b54f9f9dcad8d39ee39510e5f631d84f6daeb7469123b329"
PROPAGATION = "    c.wire.record_limit = budget ? budget->record_limit : 0;\n"
HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--artifact", required=True, type=Path)
    parser.add_argument("--plan", action="store_true", help="Print commands without building or running")
    args = parser.parse_args()
    build, work, artifact = (p.resolve() for p in (args.build_dir, args.work_dir, args.artifact))
    if artifact.stat().st_size != 15499688 or digest(artifact) != PUBLIC_SHA256:
        raise SystemExit("Public artifact size/SHA256 differs")
    delta = subprocess.check_output([
        "git", "-C", str(REPO), "diff", BASE, "--", "src", "sdk", "tools/compiler", "packages/core"
    ])
    if delta:
        raise SystemExit("Production sources differ from the reviewed candidate")
    cache = (build / "CMakeCache.txt").read_text()
    if "CHIBI_ENABLE_SJLIT:BOOL=OFF" not in cache or "CMAKE_BUILD_TYPE:STRING=Debug" not in cache:
        raise SystemExit("Use the documented Debug bytecode-only SDK configuration")
    entries = json.loads((build / "compile_commands.json").read_text())

    def compile_command(relative, source, output):
        entry = next(e for e in entries if Path(e["file"]).resolve() == REPO / relative)
        command = entry.get("arguments") or shlex.split(entry["command"])
        if Path(command[0]).name == "ccache":
            command = command[1:]
        command[command.index("-c") + 1] = str(source)
        command[command.index("-o") + 1] = str(output)
        return command

    adapter = work / "adapter.o"
    mutant_object = work / "prepared-no-import-bound.o"
    mutant_source = work / "prepared-no-import-bound.c"
    source = (REPO / "src/execution/prepared.c").read_text()
    if source.count(PROPAGATION) != 1:
        raise SystemExit("Expected exactly one importer propagation line")
    compile_adapter = compile_command("src/toolchain/compiler_main.c", HERE / "adapter.c", adapter)
    compile_adapter += ["-I" + str(REPO / "src"), "-I" + str(REPO)]
    compile_mutant = compile_command("src/execution/prepared.c", mutant_source, mutant_object)
    commands = subprocess.check_output([
        "ninja", "-C", str(build), "-t", "commands", "chibi-compiler-host"
    ], text=True).splitlines()
    # CMake's Ninja linker rule is `: && <link command> && :` on macOS.
    tokens = shlex.split(commands[-1])
    if tokens[:2] != [":", "&&"] or tokens[-2:] != ["&&", ":"]:
        raise SystemExit("Unexpected linker rule; inspect rather than guess")
    link = tokens[2:-2]
    original_object = next(i for i, token in enumerate(link) if token.endswith("/compiler_main.c.o"))
    link[original_object] = str(adapter)
    link[link.index("-o") + 1] = str(work / "restored")
    mutant_link = list(link)
    mutant_link.insert(mutant_link.index("-o"), str(mutant_object))
    mutant_link[mutant_link.index("-o") + 1] = str(work / "mutant")
    importer = work / "importer.chibip"
    stages = [
        ("libraries", ["cmake", "--build", str(build), "--target", "chibi_compiler_host", "--parallel", "1"], work),
        ("adapter-compile", compile_adapter, build),
        ("restored-link", link, build),
        ("mutant-compile", compile_mutant, build),
        ("mutant-link", mutant_link, build),
        ("emit", [str(work / "restored"), "--emit", str(artifact), str(importer)], work),
        ("restored-before", [str(work / "restored"), "--check", str(artifact), str(importer)], work),
        ("mutant", [str(work / "mutant"), "--check-mutant", str(artifact), str(importer)], work),
        ("restored-after", [str(work / "restored"), "--check", str(artifact), str(importer)], work),
    ]
    if args.plan:
        print(json.dumps([dict(name=n, command=c, cwd=str(d)) for n, c, d in stages], indent=2))
        return
    work.mkdir(parents=True, exist_ok=True)
    mutant_source.write_text(source.replace(PROPAGATION, ""))
    environment = dict(os.environ, CCACHE_READONLY="true", CCACHE_TEMPDIR=str(work / "ccache-tmp"))
    Path(environment["CCACHE_TEMPDIR"]).mkdir(exist_ok=True)
    for name, command, cwd in stages:
        # The wrapper owns logs and meters every descendant, even new process groups.
        subprocess.run([
            sys.executable, str(HERE / "bounded.py"), str(work / "evidence" / name),
            "120", "768", *command
        ], cwd=cwd, env=environment, check=True)
    identities = {name: digest(work / name) for name in ["restored", "mutant", "importer.chibip", "adapter.o", "prepared-no-import-bound.c"]}
    identities.update({"artifact": digest(artifact), "adapter.c": digest(HERE / "adapter.c"), "model.inc": digest(HERE / "model.inc")})
    (work / "identities.json").write_text(json.dumps(identities, indent=2) + "\n")
    print(json.dumps(identities, indent=2))


if __name__ == "__main__":
    main()
