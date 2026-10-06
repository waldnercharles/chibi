#!/usr/bin/env python3
"""Offline coverage for package invalidation, publication, and concurrent reuse."""
import concurrent.futures
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("dependency_package", TOOLS / "dependency_package.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class PackageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        (self.source / "cmake/patches").mkdir(parents=True)
        (self.source / "third_party/yyjson").mkdir(parents=True)
        (self.source / "CMakeLists.txt").write_text("project(example)\n")
        (self.source / "cmake/dependencies.cmake").write_text("set(DEP_PIN v1)\n")
        (self.source / "cmake/Findcrypto.cmake").write_text("# crypto discovery v1\n")
        (self.source / "cmake/patches/dep.patch").write_text("patch v1\n")
        (self.source / "third_party/yyjson/local.c").write_text("int dep;\n")
        sdk = self.root / "offline-sdk"
        toolchain = sdk / "cmake/Modules/Platform/Emscripten.cmake"
        toolchain.parent.mkdir(parents=True)
        toolchain.write_text("# offline toolchain fixture\n")
        for name in ("emcc", "em++", "emcmake"):
            (sdk / name).write_text("# offline tool fixture\n")
        patch = mock.patch.object(package, "web_toolchain", return_value=toolchain)
        patch.start()
        self.addCleanup(patch.stop)
        which = shutil.which
        patch = mock.patch.object(package.shutil, "which", side_effect=lambda name:
            str(sdk / name) if name == "emcmake" else which(name))
        patch.start()
        self.addCleanup(patch.stop)
        self.compiler = "compiler-v1"
        patch = mock.patch.object(package, "executable_identity", side_effect=self.tool)
        patch.start()
        self.addCleanup(patch.stop)

    def tool(self, command):
        return {"command": command, "version": self.compiler}

    def key(self, source=None, arguments=None):
        return package.package_key(package.identity(source or self.source, "native", arguments or []))

    def test_worktree_location_does_not_change_identity(self):
        other = self.root / "other"
        shutil.copytree(self.source, other)
        (other / "CMakeLists.txt").write_text("project(other_application)\n")
        self.assertEqual(self.key(), self.key(other))
        self.assertEqual(self.key(arguments=["-DCHIBI_REFLECT_EXECUTABLE=/one/reflection",
            "-DCHIBI_COLLAB_URL=https://one"]), self.key(other,
            ["-DCHIBI_REFLECT_EXECUTABLE=/two/reflection", "-DCHIBI_COLLAB_URL=https://two"]))

    def test_equivalent_defaults_and_boolean_spellings_share_packages(self):
        defaults = ["-DCHIBI_WEB_THREADS=OFF",
                    "-DCHIBI_ENABLE_TRACY=OFF", "-DCHIBI_ENABLE_SANITIZERS=ON",
                    "-DCHIBI_SANITIZERS=address,undefined", "-DCHIBI_ENABLE_CCACHE=ON",
                    "-DCHIBI_ENABLE_SJLIT=ON", "-DCHIBI_WEB_WORKERS=12"]
        self.assertEqual(self.key(), self.key(arguments=defaults))
        self.assertEqual(self.key(), self.key(arguments=["-DCHIBI_ENABLE_SANITIZERS:BOOL=TRUE",
            "-DCHIBI_ENABLE_TRACY:BOOL=0"]))
        self.assertEqual(self.key(arguments=["-DCHIBI_ENABLE_TRACY=ON"]),
                         self.key(arguments=["-DCHIBI_ENABLE_TRACY=YES"]))
        serial = package.package_arguments(["-DCHIBI_WEB_WORKERS=12", "-DCHIBI_ENABLE_SJLIT=ON"], "web")
        self.assertEqual(serial, [])
        self.assertEqual(package.package_arguments(["-DCHIBI_WEB_THREADS=ON", "-DCHIBI_WEB_WORKERS=4"], "web"),
                         ["-DCHIBI_WEB_THREADS=ON"])
        self.assertIn("-DCHIBI_WEB_WORKERS=12", package.package_arguments(
            ["-DCHIBI_WEB_THREADS=ON", "-DCHIBI_WEB_WORKERS=12"], "web"))

    def test_canonical_web_toolchain_and_cached_compilers_match_auto_configuration(self):
        sdk = self.root / "sdk"
        toolchain = sdk / "cmake/Modules/Platform/Emscripten.cmake"
        toolchain.parent.mkdir(parents=True)
        toolchain.write_text("include(other_module)\n")
        (sdk / "emcc").write_text("C driver")
        (sdk / "em++").write_text("CXX driver")
        with mock.patch.object(package, "web_toolchain", return_value=toolchain.resolve()):
            auto = package.identity(self.source, "web", [])
            cached = package.identity(self.source, "web", [f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
                f"-DCMAKE_C_COMPILER={sdk / 'emcc'}", f"-DCMAKE_CXX_COMPILER={sdk / 'em++'}"])
            self.assertEqual(package.package_key(auto), package.package_key(cached))
            toolchain.write_text("include(updated_module)\n")
            updated = package.identity(self.source, "web", [])
            self.assertNotEqual(package.package_key(auto), package.package_key(updated))
            custom = self.root / "custom.cmake"
            custom.write_text("include(custom_external)\n")
            with self.assertRaises(RuntimeError):
                package.identity(self.source, "web", [f"-DCMAKE_TOOLCHAIN_FILE={custom}"])

    def test_flags_compilers_pins_patches_and_local_sources_invalidate(self):
        (self.source / "cmake/dependency_package").mkdir()
        (self.source / "cmake/dependency_package/CMakeLists.txt").write_text("# export v1\n")
        initial = self.key()
        self.assertNotEqual(initial, self.key(arguments=["-DCMAKE_BUILD_TYPE=Release"]))
        self.compiler = "compiler-v2"
        self.assertNotEqual(initial, self.key())
        self.compiler = "compiler-v1"
        for name in ("cmake/dependencies.cmake", "cmake/Findcrypto.cmake", "cmake/dependency_package/CMakeLists.txt",
                     "cmake/patches/dep.patch", "third_party/yyjson/local.c"):
            path = self.source / name
            original = path.read_text()
            path.write_text(original + "changed\n")
            self.assertNotEqual(initial, self.key(), name)
            path.write_text(original)

    def test_external_source_and_preload_overrides_are_rejected(self):
        for flag in ("-DFETCHCONTENT_SOURCE_DIR_CUTE=/tmp/dependency", "-C", "-S/tmp/project",
                     "-DCHIBI_SDL3_PACKAGE_PREFIX=/tmp/package"):
            with self.assertRaises(RuntimeError):
                self.key(arguments=[flag])

    def test_multiconfiguration_generators_are_rejected(self):
        for generator in ("Ninja Multi-Config", "Xcode", "Visual Studio 17 2022"):
            with mock.patch.dict(package.os.environ, {"CMAKE_GENERATOR": generator}):
                with self.assertRaises(RuntimeError):
                    self.key()
            with self.assertRaises(RuntimeError):
                self.key(arguments=[f"-DCMAKE_GENERATOR={generator}"])
        with self.assertRaises(RuntimeError):
            self.key(arguments=["-DCMAKE_CONFIGURATION_TYPES=Debug;Release"])

    def test_compilation_environment_invalidates(self):
        initial = self.key()
        for name in ("OBJCFLAGS", "OBJCXXFLAGS", "EMCC_CFLAGS", "CPATH", "CMAKE_PREFIX_PATH"):
            with mock.patch.dict(package.os.environ, {name: "different"}):
                self.assertNotEqual(initial, self.key(), name)

    def test_objective_c_compiler_identity_and_environment(self):
        with mock.patch.object(package.sys, "platform", "darwin"), \
                mock.patch.object(package.subprocess, "run", return_value=
                    subprocess.CompletedProcess("xcrun", 0, stdout="/sdk")):
            initial = self.key()
            for name in ("OBJC", "OBJCXX"):
                with mock.patch.dict(package.os.environ, {name: "different-clang"}):
                    self.assertNotEqual(initial, self.key(), name)
                self.assertNotEqual(initial, self.key(arguments=[f"-DCMAKE_{name}_COMPILER=different-clang"]))

    def test_toolchain_content_invalidation(self):
        toolchain = self.root / "toolchain.cmake"
        toolchain.write_text("set(CMAKE_SYSTEM_NAME Generic)\n")
        arguments = [f"-DCMAKE_TOOLCHAIN_FILE={toolchain}"]
        initial = self.key(arguments=arguments)
        toolchain.write_text("set(CMAKE_SYSTEM_NAME Linux)\n")
        self.assertNotEqual(initial, self.key(arguments=arguments))

    def test_existing_configuration_is_preserved_and_explicit_flags_win(self):
        build = self.root / "existing"
        build.mkdir()
        (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\n"
            "CHIBI_ENABLE_SANITIZERS:BOOL=OFF\n"
            "CMAKE_C_FLAGS:STRING=-march=armv8.4-a\n")
        expected = ["-DCMAKE_BUILD_TYPE=Release", "-DCHIBI_ENABLE_SANITIZERS=OFF",
                    "-DCMAKE_C_FLAGS=-march=armv8.4-a"]
        effective = package.effective_arguments(build, [])
        self.assertEqual(self.key(arguments=effective), self.key(arguments=sorted(expected)))
        override = package.effective_arguments(build, ["-DCHIBI_ENABLE_SANITIZERS=ON"])
        self.assertIn("-DCHIBI_ENABLE_SANITIZERS=ON", override)
        self.assertNotIn("-DCHIBI_ENABLE_SANITIZERS=OFF", override)

    def test_cmake_populated_defaults_do_not_change_existing_package_key(self):
        build = self.root / "consumer"
        shared = self.root / "package/build"
        build.mkdir()
        shared.mkdir(parents=True)
        initial = ["-DCMAKE_BUILD_TYPE=Release"]
        (shared.parent / "complete.json").write_text(json.dumps({"arguments": initial}))
        defaults = "CMAKE_BUILD_TYPE:STRING=Release\nCMAKE_C_COMPILER:FILEPATH=/usr/bin/cc\nCMAKE_C_FLAGS_RELEASE:STRING=-O3 -DNDEBUG\n"
        (shared / "CMakeCache.txt").write_text(defaults)
        (build / "CMakeCache.txt").write_text(defaults +
            f"CHIBI_DEPENDENCY_PACKAGE:FILEPATH={shared / 'chibi-dependencies.cmake'}\n")
        self.assertEqual(package.effective_arguments(build, []), initial)

    def test_existing_reload_package_becomes_ordinary_with_same_toolchain_and_provider(self):
        build = self.root / "consumer"
        shared = self.root / "reload-package/build"
        build.mkdir()
        shared.mkdir(parents=True)
        retained = ["-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCMAKE_C_COMPILER=selected-clang",
                    "-DCMAKE_C_FLAGS=-march=armv8.4-a", "-DOPENSSL_ROOT_DIR=/selected/provider"]
        (shared.parent / "complete.json").write_text(json.dumps({
            "arguments": [*retained, "-DCHIBI_DEVELOPMENT_RELOAD=ON"]}))
        cached = ("CMAKE_BUILD_TYPE:STRING=RelWithDebInfo\n"
                  "CMAKE_C_COMPILER:FILEPATH=selected-clang\n"
                  "CMAKE_C_FLAGS:STRING=-march=armv8.4-a\n"
                  "OPENSSL_ROOT_DIR:PATH=/selected/provider\n"
                  "CHIBI_DEVELOPMENT_RELOAD:BOOL=ON\n")
        (shared / "CMakeCache.txt").write_text(cached)
        (build / "CMakeCache.txt").write_text(cached +
            f"CHIBI_DEPENDENCY_PACKAGE:FILEPATH={shared / 'chibi-dependencies.cmake'}\n")
        effective = package.effective_arguments(build, [])
        self.assertEqual(effective, sorted(retained))
        self.assertEqual(self.key(arguments=effective), self.key(arguments=retained))

    def test_custom_toolchain_external_file_and_environment_reads_are_rejected(self):
        custom = self.root / "custom.cmake"
        for command in ("file(READ /external value)", "set(value $ENV{EXTERNAL})"):
            custom.write_text(command)
            with self.assertRaises(RuntimeError):
                self.key(arguments=[f"-DCMAKE_TOOLCHAIN_FILE={custom}"])
            with mock.patch.dict(package.os.environ, {"CMAKE_TOOLCHAIN_FILE": str(custom)}):
                with self.assertRaises(RuntimeError):
                    self.key()

    @unittest.skipUnless(shutil.which("cmake") and shutil.which("make"), "CMake and Make required")
    def test_legacy_consumer_generator_does_not_constrain_package_generator(self):
        project = self.source / "cmake/dependency_package"
        project.mkdir()
        (project / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.20)\n"
            "project(package_fixture LANGUAGES NONE)\n"
            'file(WRITE "${CMAKE_BINARY_DIR}/chibi-dependencies.cmake" "# fixture\\n")\n'
            "add_custom_target(chibi_dependency_package)\n")
        build = self.root / "consumer"
        build.mkdir()
        (build / "CMakeCache.txt").write_text("CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n")
        self.assertNotIn("-DCMAKE_GENERATOR=Unix Makefiles", package.effective_arguments(build, []))
        with mock.patch.dict(package.os.environ, {"CMAKE_GENERATOR": ""}):
            entry = package.prepare(self.source, build, "native", [], self.root / "cache")
        generator = package.cache_values(entry / "build/CMakeCache.txt")["CMAKE_GENERATOR"]
        self.assertEqual(generator, "Ninja" if shutil.which("ninja") else "Unix Makefiles")
        with mock.patch.dict(package.os.environ, {"CMAKE_GENERATOR": ""}):
            explicit = package.prepare(self.source, self.root / "explicit", "native",
                ["-DCMAKE_GENERATOR=Unix Makefiles"], self.root / "cache")
        self.assertEqual(package.cache_values(explicit / "build/CMakeCache.txt")["CMAKE_GENERATOR"], "Unix Makefiles")

    def test_concurrent_worktrees_build_once_and_reuse_package(self):
        calls = []
        guard = threading.Lock()

        def cmake(command, **kwargs):
            with guard:
                calls.append(command)
            if "-B" in command:
                build = Path(command[command.index("-B") + 1])
                time.sleep(0.03)
                (build / "chibi-dependencies.cmake").write_text("# exported targets\n")
                (build / "CMakeFiles/dependency.dir").mkdir(parents=True)
                (build / "CMakeFiles/dependency.dir/dependency.o").write_text("object")
                (build / "CMakeFiles/dependency.dir/dependency.obj").write_text("object")
                (build / "CMakeFiles/dependency.dir/_deps/example-src").mkdir(parents=True)
                (build / "CMakeFiles/dependency.dir/_deps/example-src/example.o").write_text("object")
                (build / "_deps/example-src/models").mkdir(parents=True)
                (build / "_deps/example-src/models/example.obj").write_text("model asset")
                (build / "dependency.a").write_text("archive")
            return subprocess.CompletedProcess(command, 0)

        with mock.patch.object(package.subprocess, "run", side_effect=cmake), \
                mock.patch.object(package.sys, "platform", "linux"), \
                mock.patch.object(package, "publish_sources"):

            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                entries = list(pool.map(lambda index: package.prepare(self.source,
                    self.root / f"build-{index}", "native", [], self.root / "cache"), range(2)))
        self.assertEqual(entries[0], entries[1])
        self.assertEqual(len(calls), 2)
        self.assertFalse((entries[0] / "build/CMakeFiles/dependency.dir/dependency.o").exists())
        self.assertFalse((entries[0] / "build/CMakeFiles/dependency.dir/dependency.obj").exists())
        self.assertFalse((entries[0] / "build/CMakeFiles/dependency.dir/_deps/example-src/example.o").exists())
        self.assertTrue((entries[0] / "build/_deps/example-src/models/example.obj").is_file())
        self.assertTrue((entries[0] / "build/dependency.a").is_file())
        self.assertTrue((entries[0] / "complete.json").is_file())
        for index in range(2):
            self.assertIn(str(entries[0]), (self.root / f"build-{index}/.chibi-dependency-package.cmake").read_text())

    def test_failed_build_is_removed_and_can_be_retried(self):
        with mock.patch.object(package.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "cmake")):
            with self.assertRaises(subprocess.CalledProcessError):
                package.prepare(self.source, self.root / "build", "native", [], self.root / "cache")
        entry = self.root / "cache/native" / self.key()
        self.assertFalse(entry.exists())
        self.assertFalse((self.root / "build/.chibi-dependency-package.cmake").exists())


@unittest.skipUnless(shutil.which("cmake") and shutil.which("make"), "CMake and Make required")
class ExportTest(unittest.TestCase):
    def run_cmake(self, *args, expected=0):
        result = subprocess.run(["cmake", *map(str, args)], text=True, capture_output=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result.stdout + result.stderr

    @unittest.skipUnless(shutil.which("pkg-config"), "pkg-config required")
    def test_real_openssl_rejects_mixed_provider_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("a", "b"):
                provider = root / name
                (provider / "lib").mkdir(parents=True)
                (provider / "include/openssl").mkdir(parents=True)
                # Discovery-only fixtures; the export test below exercises linking.
                (provider / "lib/libcrypto.a").touch()
                (provider / "include/openssl/ssl.h").touch()
                (provider / "include/openssl/opensslv.h").write_text(
                    "#define OPENSSL_VERSION_NUMBER 0x30000000L\n")
            selected = root / "a"
            alias = root / "alias"
            alias.symlink_to(selected, target_is_directory=True)
            upstream = root / "cmake/modules"
            upstream.mkdir(parents=True)
            (upstream / "Findcrypto.cmake").write_text(
                "add_library(AWS::crypto UNKNOWN IMPORTED)\n"
                "set_target_properties(AWS::crypto PROPERTIES\n"
                ' IMPORTED_LOCATION "${crypto_LIBRARY}"\n'
                ' INTERFACE_INCLUDE_DIRECTORIES "${crypto_INCLUDE_DIR}")\n')
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 4.2)\nproject(provenance C)\n"
                f'list(PREPEND CMAKE_MODULE_PATH "{TOOLS.parent}/cmake")\n'
                f'set(s2n_SOURCE_DIR "{root}")\n'
                f'set(crypto_LIBRARY "{selected}/lib/libcrypto.a")\n'
                f'set(crypto_INCLUDE_DIR "{selected}/include")\n'
                'set(OPENSSL_CRYPTO_LIBRARY "${crypto_LIBRARY}" CACHE FILEPATH "")\n'
                'set(OPENSSL_INCLUDE_DIR "${crypto_INCLUDE_DIR}" CACHE PATH "")\n'
                "find_package(crypto REQUIRED)\n"
                'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/links.txt" CONTENT '
                '"$<TARGET_PROPERTY:AWS::crypto,INTERFACE_LINK_LIBRARIES>")\n')
            for name, lib, include, expected in (
                ("aligned", selected, selected, 0),
                ("canonical-alias", alias, alias, 0),
                ("mixed", root / "b", root / "b", 1),
                ("split-crypto", root / "b", root / "b", 1),
                ("library-only", root / "b", selected, 1),
                ("headers-only", selected, root / "b", 1),
            ):
                with self.subTest(name=name):
                    metadata = root / name / "pkgconfig"
                    metadata.mkdir(parents=True)
                    fields = f"libdir={lib}/lib\nincludedir={include}/include\n"
                    (metadata / "libcrypto.pc").write_text(fields +
                        "Name: fixture crypto\nDescription: provider fixture\nVersion: 3.0.0\n"
                        "Libs: -L${libdir} -lcrypto\nLibs.private: -lfixture_closure\n"
                        "Cflags: -I${includedir}\n")
                    openssl_fields = (f"libdir={selected}/lib\nincludedir={selected}/include\n"
                        if name == "split-crypto" else fields)
                    (metadata / "openssl.pc").write_text(openssl_fields +
                        "Name: fixture OpenSSL\nDescription: provider fixture\nVersion: 3.0.0\n"
                        "Requires: libcrypto\n")
                    build = root / f"build-{name}"
                    with mock.patch.dict(package.os.environ, {"PKG_CONFIG_PATH": str(metadata),
                            "PKG_CONFIG_LIBDIR": str(metadata), "PKG_CONFIG_SYSROOT_DIR": ""}):
                        output = self.run_cmake("-S", root, "-B", build, expected=expected)
                    if expected:
                        self.assertIn("pkg-config OpenSSL metadata does not match", output)
                    else:
                        self.assertIn("fixture_closure", (build / "links.txt").read_text())

    @unittest.skipUnless(shutil.which("pkg-config") and
        Path("/usr/lib/x86_64-linux-gnu/libcrypto.a").is_file(), "Debian host crypto fixture required")
    def test_cloud_crypto_scope_restores_environment_and_recovers_stale_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cloud = root / "cloud"
            for name in ("root/usr/bin/clang-19", "python/bin/cmake"):
                path = cloud / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            upstream = root / "cmake/modules"
            upstream.mkdir(parents=True)
            (upstream / "Findcrypto.cmake").write_text(
                "add_library(AWS::crypto UNKNOWN IMPORTED)\n"
                "set_target_properties(AWS::crypto PROPERTIES\n"
                ' IMPORTED_LOCATION "${crypto_LIBRARY}"\n'
                ' INTERFACE_INCLUDE_DIRECTORIES "${crypto_INCLUDE_DIR}")\n')
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 4.2)\nproject(cloud_crypto C)\n"
                f'list(PREPEND CMAKE_MODULE_PATH "{TOOLS.parent}/cmake")\n'
                f'set(s2n_SOURCE_DIR "{root}")\n'
                'set(crypto_LIBRARY "/usr/lib/x86_64-linux-gnu/libcrypto.a")\n'
                'set(crypto_INCLUDE_DIR "/usr/include")\n'
                'set(OPENSSL_CRYPTO_LIBRARY "${crypto_LIBRARY}")\n'
                'set(OPENSSL_INCLUDE_DIR "${crypto_INCLUDE_DIR}")\n'
                'if(BLOCK_VAR)\nset(${BLOCK_VAR} "${BLOCK_VALUE}")\nendif()\n'
                'set(__pkg_config_checked_UNRELATED "sentinel" CACHE INTERNAL "")\n'
                'set(UNRELATED_STATIC_LIBRARIES "sentinel" CACHE INTERNAL "")\n'
                'foreach(name PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_PATH PKG_CONFIG_LIBDIR CMAKE_PREFIX_PATH)\n'
                ' set(saved_${name} "$ENV{${name}}")\n'
                ' set(defined_${name} FALSE)\n'
                ' if(DEFINED ENV{${name}})\nset(defined_${name} TRUE)\nendif()\n'
                'endforeach()\n'
                'find_package(crypto REQUIRED)\n'
                'foreach(name PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_PATH PKG_CONFIG_LIBDIR CMAKE_PREFIX_PATH)\n'
                ' if(NOT "$ENV{${name}}" STREQUAL "${saved_${name}}" OR\n'
                '    (defined_${name} AND NOT DEFINED ENV{${name}}) OR\n'
                '    (NOT defined_${name} AND DEFINED ENV{${name}}))\n'
                '  message(FATAL_ERROR "Environment leaked: ${name}")\nendif()\n'
                'endforeach()\n'
                'if(NOT __pkg_config_checked_UNRELATED STREQUAL "sentinel" OR\n'
                '   NOT UNRELATED_STATIC_LIBRARIES STREQUAL "sentinel")\n'
                ' message(FATAL_ERROR "Unrelated package cache changed")\nendif()\n'
                'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/library.txt" CONTENT '
                '"$<TARGET_PROPERTY:AWS::crypto,IMPORTED_LOCATION>")\n')
            env = dict(package.os.environ)
            for name in ("PKG_CONFIG_LIBDIR", "PKG_CONFIG", "CMAKE_TOOLCHAIN_FILE", "OPENSSL_ROOT_DIR"):
                env.pop(name, None)
            env.update(CHIBI_CLOUD_ACTIVE=str(cloud), CMAKE_PREFIX_PATH=f"{cloud}/root/usr",
                PKG_CONFIG_PATH=f"{cloud}/root/usr/lib/x86_64-linux-gnu/pkgconfig:{cloud}/root/usr/share/pkgconfig",
                PKG_CONFIG_SYSROOT_DIR=f"{cloud}/root")
            aligned = "Using verified host OpenSSL metadata"
            with mock.patch.dict(package.os.environ, env, clear=True):
                stale = root / "stale"
                self.run_cmake("-S", root, "-B", stale, "-DBLOCK_VAR=CMAKE_SYSROOT", "-DBLOCK_VALUE=", expected=1)
                self.assertIn(f"_OPENSSL_LIBDIR:INTERNAL={cloud}/root/usr/lib", (stale / "CMakeCache.txt").read_text())
                for _ in range(2):
                    output = self.run_cmake("-S", root, "-B", stale, "-DBLOCK_VAR=")
                    self.assertIn(aligned, output)
                    self.assertEqual((stale / "library.txt").read_text(), "/usr/lib/x86_64-linux-gnu/libcrypto.a")
                for variable, value in (("CMAKE_TOOLCHAIN_FILE", "/explicit/toolchain"),
                        ("CMAKE_SYSROOT", "/explicit/sysroot"), ("CMAKE_SYSROOT_COMPILE", "/explicit/sysroot"),
                        ("CMAKE_SYSROOT_LINK", "/explicit/sysroot"), ("CMAKE_FIND_ROOT_PATH", "/explicit/root"),
                        ("CMAKE_CROSSCOMPILING", "TRUE"), ("OPENSSL_ROOT_DIR", "/explicit/provider")):
                    with self.subTest(variable=variable):
                        output = self.run_cmake("-S", root, "-B", root / variable,
                            f"-DBLOCK_VAR={variable}", f"-DBLOCK_VALUE={value}", expected=1)
                        self.assertNotIn(aligned, output)
            for name, value, expected in (("PKG_CONFIG_SYSROOT_DIR", None, 0),
                    ("PKG_CONFIG_SYSROOT_DIR", "", 0), ("PKG_CONFIG_SYSROOT_DIR", "/explicit/sysroot", 1),
                    ("PKG_CONFIG_PATH", "/usr/lib/x86_64-linux-gnu/pkgconfig", 1),
                    ("PKG_CONFIG_LIBDIR", "/usr/lib/x86_64-linux-gnu/pkgconfig", 1)):
                with self.subTest(name=name, value=value), mock.patch.dict(package.os.environ, env, clear=True):
                    if value is None:
                        package.os.environ.pop(name, None)
                    else:
                        package.os.environ[name] = value
                    output = self.run_cmake("-S", root, "-B", root / f"env-{name}-{expected}-{value is None}",
                        expected=expected)
                    self.assertNotIn(aligned, output)

    def test_directory_scoped_crypto_survives_clean_and_reused_imports(self):
        for kind in ("STATIC", "SHARED", "BUILT", "CUSTOM"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                source = root / "source"
                cmake = source / "cmake"
                project = cmake / "dependency_package"
                project.mkdir(parents=True)
                shutil.copy2(TOOLS.parent / "cmake/dependency_package/CMakeLists.txt", project)
                for name in package.PACKAGE_CMAKE:
                    if name != "dependency_package/CMakeLists.txt":
                        (cmake / name).write_text("")
                shutil.copy2(TOOLS.parent / "cmake/Findcrypto.cmake", cmake)
                provider = root / "provider"
                provider.mkdir()
                (provider / "helper.c").write_text("int helper_value(void) { return 42; }\n")
                (provider / "crypto.c").write_text(
                    "int helper_value(void); int crypto_value(void) { return helper_value(); }\n")
                (provider / "CMakeLists.txt").write_text(
                    "cmake_minimum_required(VERSION 3.20)\nproject(crypto C)\n"
                    "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n"
                    "add_library(helper STATIC helper.c)\n"
                    f"add_library(crypto {'SHARED' if kind == 'SHARED' else 'STATIC'} crypto.c)\n"
                    "target_link_libraries(crypto PRIVATE helper)\n")
                library_build = root / "library"
                self.run_cmake("-S", provider, "-B", library_build)
                self.run_cmake("--build", library_build, "--parallel", "2")
                library = next(library_build.glob("libcrypto.*"))
                helper = next(library_build.glob("libhelper.*"))
                (provider / "FindZLIB.cmake").write_text(
                    "if(NOT TARGET ZLIB::ZLIB)\nadd_library(ZLIB::ZLIB UNKNOWN IMPORTED)\n"
                    f'set_target_properties(ZLIB::ZLIB PROPERTIES IMPORTED_LOCATION "{helper}")\n'
                    "endif()\nset(ZLIB_FOUND TRUE)\n")
                # A metadata provider with a real, named transitive link dependency.
                # Shared and pre-existing built targets must not use static discovery.
                openssl = 'message(FATAL_ERROR "Unexpected OpenSSL discovery")\n'
                if kind == "STATIC":
                    openssl = ("if(NOT OPENSSL_USE_STATIC_LIBS)\n"
                        'message(FATAL_ERROR "Static crypto metadata required")\nendif()\n'
                        "find_package(ZLIB REQUIRED)\n"
                        "add_library(OpenSSL::Crypto UNKNOWN IMPORTED)\n"
                        f'set(OPENSSL_FIXTURE_LIBRARY "{library}" CACHE FILEPATH "")\n'
                        f'set(OPENSSL_FIXTURE_INCLUDE "{provider}" CACHE PATH "")\n'
                        "set_target_properties(OpenSSL::Crypto PROPERTIES\n"
                        ' IMPORTED_LOCATION "${OPENSSL_FIXTURE_LIBRARY}"\n'
                        ' INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_FIXTURE_INCLUDE}"\n'
                        ' INTERFACE_LINK_LIBRARIES "ZLIB::ZLIB;Threads::Threads")\n'
                        'set(OpenSSL_FOUND TRUE)\n')
                (provider / "FindOpenSSL.cmake").write_text(openssl)
                fixture = source / "fixture"
                fixture.mkdir()
                (fixture / "s2n.c").write_text(
                    "int crypto_value(void); int s2n_value(void) { return crypto_value(); }\n")
                if kind == "BUILT":
                    crypto = (f'find_package(Threads REQUIRED)\nadd_library(crypto STATIC "{provider}/crypto.c")\n'
                        f'add_library(helper STATIC "{provider}/helper.c")\n'
                        "target_link_libraries(crypto PUBLIC helper)\n")
                    link = "crypto Threads::Threads"
                else:
                    crypto = ("find_package(Threads REQUIRED)\n"
                        "add_library(AWS::crypto UNKNOWN IMPORTED)\n"
                        "set_target_properties(AWS::crypto PROPERTIES\n"
                        f' IMPORTED_LOCATION "{library}"\n'
                        f' INTERFACE_INCLUDE_DIRECTORIES "{provider}"\n'
                        ' IMPORTED_LINK_INTERFACE_LANGUAGES "C")\n'
                        'add_dependencies(AWS::crypto Threads::Threads)\n')
                    if kind == "CUSTOM":
                        crypto += ("find_package(ZLIB REQUIRED)\n"
                            "set_property(TARGET AWS::crypto PROPERTY INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)\n")
                    link = "AWS::crypto Threads::Threads"
                upstream = fixture / "cmake/modules"
                upstream.mkdir(parents=True)
                (upstream / "Findcrypto.cmake").write_text(
                    "if(TARGET crypto OR TARGET AWS::crypto)\nreturn()\nendif()\n" +
                    (crypto if kind in ("STATIC", "SHARED") else ""))
                (fixture / "CMakeLists.txt").write_text(
                    (crypto if kind in ("BUILT", "CUSTOM") else "") +
                    f'set(s2n_SOURCE_DIR "{fixture}")\n'
                    f'set(crypto_LIBRARY "{library}")\n'
                    f'set(crypto_INCLUDE_DIR "{provider}")\n'
                    'find_package(crypto REQUIRED)\n' +
                    "add_library(s2n STATIC s2n.c)\n"
                    f"target_link_libraries(s2n PUBLIC {link})\n")
                (cmake / "dependencies.cmake").write_text(
                    f'list(PREPEND CMAKE_MODULE_PATH "{cmake}" "{provider}")\n'
                    'add_subdirectory("${CHIBI_SOURCE_ROOT}/fixture" fixture)\n'
                    'add_executable(direct "${CHIBI_SOURCE_ROOT}/fixture/main.c")\n'
                    'target_link_libraries(direct PRIVATE s2n)\n')
                (fixture / "main.c").write_text(
                    "int s2n_value(void); int main(void) { return s2n_value() != 42; }\n")
                build = root / "package"
                self.run_cmake("-S", project, "-B", build, f"-DCHIBI_SOURCE_ROOT={source}",
                    "-DCHIBI_ENABLE_CCACHE=OFF", "-DCHIBI_ENABLE_SANITIZERS=OFF")
                self.run_cmake("--build", build, "--target", "chibi_dependency_package", "--parallel", "2")
                self.run_cmake("--build", build, "--target", "direct", "--parallel", "2")
                subprocess.run([str(build / "direct")], check=True)
                exported = build / "chibi-crypto.cmake"
                self.assertEqual(exported.exists(), kind != "BUILT")
                if exported.exists():
                    self.assertIn(str(library), exported.read_text())
                if kind == "STATIC":
                    for name, value in (("LIBRARY", helper), ("INCLUDE", library_build)):
                        output = self.run_cmake("-S", project, "-B", root / f"mismatch-{name}",
                            f"-DCHIBI_SOURCE_ROOT={source}", f"-DOPENSSL_FIXTURE_{name}={value}", expected=1)
                        self.assertIn("OpenSSL metadata does not match", output)
                consumer = root / "consumer"
                consumer.mkdir()
                (consumer / "main.c").write_text(
                    "int s2n_value(void); int main(void) { return s2n_value() != 42; }\n")
                identity = ""
                if kind != "BUILT":
                    identity = ('get_target_property(selected AWS::crypto IMPORTED_LOCATION)\n'
                        f'if(NOT selected STREQUAL "{library}")\n'
                        'message(FATAL_ERROR "Crypto provider changed")\nendif()\n')
                (consumer / "CMakeLists.txt").write_text(
                    "cmake_minimum_required(VERSION 3.20)\nproject(consumer C)\n"
                    f'list(PREPEND CMAKE_MODULE_PATH "{provider}")\n'
                    f'include("{build}/chibi-dependencies.cmake")\n' + identity +
                    "add_executable(probe main.c)\ntarget_link_libraries(probe PRIVATE s2n)\n")
                for name in ("clean", "reused", "reused"):
                    destination = root / name
                    self.run_cmake("-S", consumer, "-B", destination)
                    self.run_cmake("--build", destination, "--parallel", "2")
                    subprocess.run([str(destination / "probe")], check=True)


if __name__ == "__main__":
    unittest.main()
