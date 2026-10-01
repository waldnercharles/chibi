"""Exercise native/web configure wrappers without downloading dependencies."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ConfigureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="chibi configure ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source tree"
        tools = self.source / "tools"
        tools.mkdir(parents=True)
        for name in ("configure.sh", "configure_native.sh", "configure_web.sh"):
            shutil.copy2(ROOT / "tools" / name, tools / name)
        (self.source / "config-key").write_text("first")
        (tools / "sdl_package.sh").write_text("printf '%s\\n' '/shared SDL/install'\n")
        (tools / "fetchcontent_cache.py").write_text('''import json, os, sys
from pathlib import Path
action = sys.argv[1]
if action == "fingerprint":
    print(Path("config-key").read_text())
else:
    with open(os.environ["CHIBI_CONFIGURE_TEST_LOG"], "a") as log:
        log.write(json.dumps(["cache", *sys.argv[1:]]) + "\\n")
    if action == "prepare":
        build = Path(sys.argv[sys.argv.index("--build-dir") + 1])
        (build / ".chibi-fetchcontent-cache.cmake").write_text("# private sources\\n")
''')
        binary = self.root / "bin"
        binary.mkdir()
        self.executable(binary / "cmake", '''import json, os, sys
from pathlib import Path
with open(os.environ["CHIBI_CONFIGURE_TEST_LOG"], "a") as log:
    log.write(json.dumps(["cmake", *sys.argv[1:]]) + "\\n")
build = Path(sys.argv[sys.argv.index("-B") + 1])
(build / "CMakeCache.txt").write_text("CMAKE_GENERATOR:INTERNAL=Ninja\\n")
''')
        self.executable(binary / "emcmake", '''import json, os, sys
with open(os.environ["CHIBI_CONFIGURE_TEST_LOG"], "a") as log:
    log.write(json.dumps(["emcmake", *sys.argv[1:]]) + "\\n")
os.execvp(sys.argv[1], sys.argv[1:])
''')
        self.log = self.root / "calls.jsonl"
        self.environment = {
            **os.environ,
            "PATH": f"{binary}{os.pathsep}{os.environ['PATH']}",
            "CHIBI_FETCHCONTENT_CACHE_DIR": str(self.root / "shared cache"),
            "CHIBI_CONFIGURE_TEST_LOG": str(self.log),
        }
        self.environment.pop("CMAKE_GENERATOR", None)

    def executable(self, path, source):
        path.write_text("#!/usr/bin/env python3\n" + source)
        path.chmod(0o755)

    def configure(self, profile, build, *arguments):
        subprocess.run(
            ["sh", str(self.source / "tools" / f"configure_{profile}.sh"),
             str(build), *arguments],
            cwd=self.source, env=self.environment, check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_native_package_and_web_platform_use_same_cache(self):
        self.configure("native", self.source / "build", "-DCHIBI_ENABLE_TRACY=ON")
        self.configure("web", self.source / "build-web", "-DCHIBI_COLLAB_URL=wss://example.test/room")
        calls = self.calls()
        preparations = [call for call in calls if call[:2] == ["cache", "prepare"]]
        self.assertEqual(len(preparations), 2)
        self.assertIn("--no-sdl", preparations[0])
        self.assertIn("--tracy", preparations[0])
        self.assertIn("--web", preparations[1])
        native, web = [call for call in calls if call[0] == "cmake"]
        self.assertIn("-DCHIBI_SDL3_PACKAGE_PREFIX=/shared SDL/install", native)
        self.assertIn("-DCHIBI_SDL3_PACKAGE_PREFIX=", web)
        self.assertIn("-DCHIBI_COLLAB_URL=wss://example.test/room", web)
        self.assertEqual(len([call for call in calls if call[0] == "emcmake"]), 1)

    def test_unchanged_config_skips_but_pin_change_refreshes_existing_build(self):
        build = self.source / "build-web"
        self.configure("web", build, "-DCMAKE_BUILD_TYPE=Release")
        original = self.calls()
        self.configure("web", build, "-DCMAKE_BUILD_TYPE=Release")
        self.assertEqual(self.calls(), original)
        (self.source / "config-key").write_text("new dependency pin")
        self.configure("web", build, "-DCMAKE_BUILD_TYPE=Release")
        added = self.calls()[len(original):]
        self.assertEqual(added[0][:2], ["cache", "prepare"])
        cmake = next(call for call in added if call[0] == "cmake")
        self.assertIn("-C", cmake)
        self.assertIn(str(build / ".chibi-fetchcontent-cache.cmake"), cmake)
        self.assertNotIn("-G", cmake)
        self.assertEqual(added[-1][:2], ["cache", "publish"])


if __name__ == "__main__":
    unittest.main()
