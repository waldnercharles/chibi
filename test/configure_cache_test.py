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
        (tools / "dependency_package.py").write_text('''import json, os, sys
from pathlib import Path
with open(os.environ["CHIBI_CONFIGURE_TEST_LOG"], "a") as log:
    log.write(json.dumps(["package", *sys.argv[1:]]) + "\\n")
build = Path(sys.argv[sys.argv.index("--build-dir") + 1])
build.mkdir(parents=True, exist_ok=True)
profile = sys.argv[sys.argv.index("--profile") + 1]
entry = "/shared dependencies/" + profile + "/" + Path("config-key").read_text()
(build / ".chibi-dependency-package.cmake").write_text("# package " + entry + "\\n")
print(entry)
''')
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
        self.binary = binary
        self.executable(binary / "uname", 'print("Linux")\n')
        self.executable(binary / "cc", 'print("fixture compiler")\n')
        self.executable(binary / "cmake", '''import json, os, sys
from pathlib import Path
with open(os.environ["CHIBI_CONFIGURE_TEST_LOG"], "a") as log:
    log.write(json.dumps(["cmake", *sys.argv[1:]]) + "\\n")
build = Path(sys.argv[sys.argv.index("-B") + 1])
cache = build / "CMakeCache.txt"
lines = cache.read_text().splitlines() if cache.is_file() else []
lines = [line for line in lines if not line.startswith("CMAKE_GENERATOR:")]
for argument in sys.argv[1:]:
    if not argument.startswith("-D") or "=" not in argument:
        continue
    name, value = argument[2:].split("=", 1)
    name = name.split(":", 1)[0]
    if name.startswith("FETCHCONTENT_SOURCE_DIR_") or name in ("CHIBI_SJLIT_SOURCE_DIR", "CMAKE_OSX_SYSROOT"):
        lines = [line for line in lines if not line.startswith(name + ":")]
        lines.append(name + ":PATH=" + value)
cache.write_text("\\n".join([*lines, "CMAKE_GENERATOR:INTERNAL=Ninja"]) + "\\n")
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
        self.environment.pop("CHIBI_DEPENDENCY_PACKAGES", None)
        self.environment.pop("SDKROOT", None)
        self.environment.pop("CC", None)
        self.environment.pop("CXX", None)
        self.environment.pop("CMAKE_TOOLCHAIN_FILE", None)

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

    def test_linux_prefers_clang_only_without_a_compiler_selection(self):
        for name in ("clang", "clang++"):
            self.executable(self.binary / name, 'print("fixture clang")\n')
        cases = (
            ("fresh", {}, [], None, True),
            ("cached", {}, [], "CMAKE_C_COMPILER:FILEPATH=/custom/gcc\n", False),
            ("cc", {"CC": "custom-cc"}, [], None, False),
            ("cxx", {"CXX": "custom-cxx"}, [], None, False),
            ("toolchain-env", {"CMAKE_TOOLCHAIN_FILE": "/custom/toolchain"}, [], None, False),
            ("compiler", {}, ["-DCMAKE_C_COMPILER:FILEPATH=/custom/gcc"], None, False),
            ("cxx-compiler", {}, ["-DCMAKE_CXX_COMPILER=/custom/g++"], None, False),
            ("toolchain", {}, ["-DCMAKE_TOOLCHAIN_FILE=/custom/toolchain"], None, False),
        )
        for name, environment, arguments, cache, automatic in cases:
            with self.subTest(name=name):
                build = self.source / name
                if cache:
                    build.mkdir()
                    (build / "CMakeCache.txt").write_text(cache)
                self.environment.update(environment)
                before = len(self.calls()) if self.log.exists() else 0
                self.configure("native", build, *arguments)
                for key in environment:
                    self.environment.pop(key)
                calls = [call for call in self.calls()[before:] if call[0] in ("package", "cmake")]
                self.assertEqual(len(calls), 2)
                for call in calls:
                    self.assertEqual("-DCMAKE_C_COMPILER=clang" in call, automatic)
                    self.assertEqual("-DCMAKE_CXX_COMPILER=clang++" in call, automatic)
                    for argument in arguments:
                        self.assertIn(argument, call)

    def test_native_sdk_selection_uses_same_sdk_for_package_and_consumer(self):
        self.executable(self.binary / "uname", 'print("Darwin")\n')
        self.executable(self.binary / "xcrun", 'print("/shared SDK27")\n')
        for label, cached, arguments, expected, automatic in (
            ("default", None, [], "/shared SDK27", True),
            ("cached", "/shared SDK26.5", [], "/shared SDK26.5", False),
            ("explicit", None, ["-DCMAKE_OSX_SYSROOT=/shared SDK26.5"], "/shared SDK26.5", False),
            ("last-explicit", None, ["-DCMAKE_OSX_SYSROOT=/shared SDK26.5", "-DCMAKE_OSX_SYSROOT=/shared SDK27"],
             "/shared SDK27", False),
            ("explicit-empty", "/shared SDK26.5", ["-DCMAKE_OSX_SYSROOT="], "/shared SDK27", True),
        ):
            with self.subTest(label=label):
                build = self.source / label
                if cached:
                    build.mkdir()
                    (build / "CMakeCache.txt").write_text(f"CMAKE_OSX_SYSROOT:PATH={cached}\n")
                before = len(self.calls()) if self.log.exists() else 0
                self.configure("native", build, *arguments)
                added = self.calls()[before:]
                package = next(call for call in added if call[0] == "package")
                cmake = next(call for call in added if call[0] == "cmake")
                if automatic or arguments:
                    option = f"-DCMAKE_OSX_SYSROOT={expected}"
                    self.assertIn(option, package)
                    self.assertIn(option, cmake)
                    self.assertEqual([arg for arg in cmake if arg.startswith("-DCMAKE_OSX_SYSROOT=")][-1], option)
                else:
                    self.assertFalse(any(arg.startswith("-DCMAKE_OSX_SYSROOT=") for arg in cmake))
                self.assertIn(f"CMAKE_OSX_SYSROOT:PATH={expected}", (build / "CMakeCache.txt").read_text())

    def test_native_automatic_sdk_comes_from_selected_compiler_driver(self):
        self.executable(self.binary / "uname", 'print("Darwin")\n')
        self.executable(self.binary / "xcrun", 'print("/shared SDK27")\n')
        drivers = {}
        for name in ("cc", "cached-clang", "explicit-clang"):
            sdk = self.root / f"{name} compatible SDK26.5"
            sdk.mkdir()
            drivers[name] = str(sdk.resolve())
            self.executable(self.binary / name,
                "import sys\nprint(" + repr(f'"clang" "-isysroot" "{sdk}"') + ", file=sys.stderr)\n")
        cases = (
            ("default-driver", None, [], "cc"),
            ("cached-driver", str(self.binary / "cached-clang"), [], "cached-clang"),
            ("explicit-driver", str(self.binary / "cached-clang"),
             [f"-DCMAKE_C_COMPILER={self.binary / 'cached-clang'}", f"-DCMAKE_C_COMPILER={self.binary / 'explicit-clang'}"], "explicit-clang"),
            ("reset-sdk", None, ["-DCMAKE_OSX_SYSROOT="], "cc"),
        )
        for label, cached_compiler, arguments, driver in cases:
            with self.subTest(label=label):
                build = self.source / label
                if cached_compiler:
                    build.mkdir()
                    (build / "CMakeCache.txt").write_text(f"CMAKE_C_COMPILER:FILEPATH={cached_compiler}\n")
                before = len(self.calls()) if self.log.exists() else 0
                self.configure("native", build, *arguments)
                added = self.calls()[before:]
                expected = f"-DCMAKE_OSX_SYSROOT={drivers[driver]}"
                for kind in ("package", "cmake"):
                    call = next(call for call in added if call[0] == kind)
                    self.assertEqual([arg for arg in call if arg.startswith("-DCMAKE_OSX_SYSROOT=")][-1], expected)
                    self.assertNotIn("-DCMAKE_OSX_SYSROOT=/shared SDK27", call)

    def test_native_and_web_preload_packages_without_local_dependencies(self):
        self.configure("native", self.source / "build", "-DCHIBI_ENABLE_TRACY=ON")
        self.configure("web", self.source / "build-web", "-DCHIBI_COLLAB_URL=wss://example.test/room")
        calls = self.calls()
        self.assertFalse(any(call[0] == "cache" for call in calls))
        packages = [call for call in calls if call[0] == "package"]
        self.assertEqual(len(packages), 2)
        self.assertIn("native", packages[0])
        self.assertIn("web", packages[1])
        self.assertIn("-DCHIBI_ENABLE_TRACY=ON", packages[0])
        native, web = [call for call in calls if call[0] == "cmake"]
        self.assertIn(str(self.source / "build/.chibi-dependency-package.cmake"), native)
        self.assertIn(str(self.source / "build-web/.chibi-dependency-package.cmake"), web)
        self.assertNotIn("-G", web)
        self.assertIn("-DCHIBI_COLLAB_URL=wss://example.test/room", web)
        self.assertEqual(len([call for call in calls if call[0] == "emcmake"]), 1)
        self.assertEqual((self.source / "build/.chibi-fetchcontent-cache.cmake").read_text(), "")

    def test_unchanged_config_validates_package_but_skips_cmake(self):
        build = self.source / "build-web"
        self.configure("web", build, "-DCMAKE_BUILD_TYPE=Release")
        original = self.calls()
        self.configure("web", build, "-DCMAKE_BUILD_TYPE=Release")
        added = self.calls()[len(original):]
        self.assertEqual(len(added), 1)
        self.assertEqual(added[0][:2], ["package", "prepare"])
        (self.source / "config-key").write_text("new dependency pin")
        before = len(self.calls())
        self.configure("web", build, "-DCMAKE_BUILD_TYPE=Release")
        added = self.calls()[before:]
        self.assertEqual(added[0][:2], ["package", "prepare"])
        cmake = next(call for call in added if call[0] == "cmake")
        self.assertIn(str(build / ".chibi-dependency-package.cmake"), cmake)
        self.assertNotIn("-G", cmake)
        self.assertFalse(any(call[0] == "cache" for call in added))

    def test_cached_tracy_python_bindings_disable_packages_until_explicitly_disabled(self):
        build = self.source / "build"
        build.mkdir()
        cached = "CMAKE_GENERATOR:INTERNAL=Ninja\nTRACY_CLIENT_PYTHON:BOOL=ON\n"
        (build / "CMakeCache.txt").write_text(cached)
        self.configure("native", build, "-DCHIBI_ENABLE_TRACY=ON")
        calls = self.calls()
        self.assertFalse(any(call[0] == "package" for call in calls))
        preparation = next(call for call in calls if call[:2] == ["cache", "prepare"])
        self.assertIn("--tracy", preparation)
        self.assertEqual(calls[-1][:2], ["cache", "publish"])
        # Restore ON because the mock CMake only writes its generator entry.
        (build / "CMakeCache.txt").write_text(cached)
        before = len(calls)
        self.configure("native", build, "-DCHIBI_ENABLE_TRACY=ON", "-DTRACY_CLIENT_PYTHON:BOOL=OFF")
        added = self.calls()[before:]
        preparation = next(call for call in added if call[:2] == ["package", "prepare"])
        self.assertIn("-DTRACY_CLIENT_PYTHON:BOOL=OFF", preparation)
        self.assertFalse(any(call[0] == "cache" for call in added))
        cmake = next(call for call in added if call[0] == "cmake")
        self.assertIn(str(build / ".chibi-dependency-package.cmake"), cmake)

    def test_external_cached_sources_use_private_build_and_remain_selected(self):
        for name in ("FETCHCONTENT_SOURCE_DIR_CUTE_FRAMEWORK", "CHIBI_SJLIT_SOURCE_DIR"):
            with self.subTest(name=name):
                build = self.source / name
                build.mkdir()
                external = self.root / "external dependency"
                selection = f"{name}:PATH={external}"
                (build / "CMakeCache.txt").write_text(selection + "\n")
                before = len(self.calls()) if self.log.exists() else 0
                self.configure("native", build)
                added = self.calls()[before:]
                self.assertFalse(any(call[0] == "package" for call in added))
                self.assertTrue(any(call[:2] == ["cache", "prepare"] for call in added))
                self.assertIn(selection, (build / "CMakeCache.txt").read_text())

    def test_cached_private_fetchcontent_source_allows_packages(self):
        build = self.source / "build"
        build.mkdir()
        private = build / "_deps/cute_framework-src"
        (build / "CMakeCache.txt").write_text(f"FETCHCONTENT_SOURCE_DIR_CUTE_FRAMEWORK:PATH={private}\n")
        self.configure("native", build)
        calls = self.calls()
        self.assertTrue(any(call[:2] == ["package", "prepare"] for call in calls))
        self.assertFalse(any(call[0] == "cache" for call in calls))

    def test_explicit_fetchcontent_source_uses_private_build(self):
        build = self.source / "build"
        external = self.root / "explicit dependency"
        option = f"-DFETCHCONTENT_SOURCE_DIR_CUTE_FRAMEWORK:PATH={external}"
        self.configure("native", build, option)
        calls = self.calls()
        self.assertFalse(any(call[0] == "package" for call in calls))
        self.assertTrue(any(call[:2] == ["cache", "prepare"] for call in calls))
        cmake = next(call for call in calls if call[0] == "cmake")
        self.assertIn(option, cmake)
        self.assertIn(f"FETCHCONTENT_SOURCE_DIR_CUTE_FRAMEWORK:PATH={external}",
                      (build / "CMakeCache.txt").read_text())

    def test_disabled_packages_prepare_and_publish_private_sources(self):
        self.environment["CHIBI_DEPENDENCY_PACKAGES"] = "0"
        build = self.source / "build-web"
        self.configure("web", build, "-DCHIBI_ENABLE_TRACY=ON")
        calls = self.calls()
        self.assertFalse(any(call[0] == "package" for call in calls))
        preparation = next(call for call in calls if call[:2] == ["cache", "prepare"])
        self.assertIn("--web", preparation)
        self.assertIn("--tracy", preparation)
        self.assertEqual(calls[-1][:2], ["cache", "publish"])
        self.assertIn("unset(CHIBI_DEPENDENCY_PACKAGE CACHE)",
                      (build / ".chibi-dependency-package.cmake").read_text())
        self.assertIn("# private sources", (build / ".chibi-fetchcontent-cache.cmake").read_text())
        before = len(calls)
        self.configure("web", build, "-DCHIBI_ENABLE_TRACY=ON")
        self.assertEqual(len(self.calls()), before)


if __name__ == "__main__":
    unittest.main()
