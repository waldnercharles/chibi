#!/usr/bin/env python3
"""Offline integration coverage for independent FetchContent source snapshots."""

import concurrent.futures
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "fetchcontent_cache", Path(__file__).resolve().parents[1] / "tools/fetchcontent_cache.py"
)
cache = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cache)


def git(path, *args):
    result = subprocess.run(["git", "-C", str(path), *args], capture_output=True, text=True,
                            env={**os.environ, "GIT_ALLOW_PROTOCOL": "file", "GIT_OPTIONAL_LOCKS": "0"})
    if result.returncode:
        raise AssertionError(result.stderr)
    return result.stdout.strip()


def declaration(name, repository, revision):
    return f"FetchContent_Declare({name}\n GIT_REPOSITORY {repository}\n GIT_TAG {revision}\n)\n"


class CacheTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "project"
        self.source.mkdir()
        (self.source / "cmake" / "patches").mkdir(parents=True)
        (self.source / "CMakeLists.txt").write_text("")
        self.storage = self.root / "storage"
        self.donor = self.root / "donor"
        (self.donor / "_deps").mkdir(parents=True)
        (self.donor / "CMakeCache.txt").write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={self.source}\n")

    def repository(self, name, cmake=""):
        path = self.root / name
        path.mkdir()
        git(path, "init", "-q")
        git(path, "config", "user.email", "cache-test@example.com")
        git(path, "config", "user.name", "Cache Test")
        (path / "source.c").write_text("original\n")
        (path / "CMakeLists.txt").write_text(cmake)
        git(path, "add", ".")
        git(path, "commit", "-qm", "upstream")
        git(path, "tag", "v1")
        return path

    def clone_donor(self, name, repository):
        target = self.donor / "_deps" / f"{name}-src"
        git(self.root, "clone", "-q", str(repository), str(target))
        return target

    def declare(self, name, repository, revision="v1", module=None):
        path = self.source / (f"cmake/{module}.cmake" if module else "CMakeLists.txt")
        with path.open("a") as output:
            output.write(declaration(name, repository, revision))

    def test_modules_macro_nested_pins_and_root_precedence(self):
        sdl = self.repository("sdl")
        box = self.repository("box")
        cute = self.repository("cute", declaration("SDL3", self.root / "wrong", "wrong")
                               + declaration("box2d", box, "v1"))
        self.declare("cute_framework", cute)
        self.declare("SDL3", sdl)
        self.clone_donor("cute_framework", cute)
        self.clone_donor("sdl3", sdl)
        self.clone_donor("box2d", box)
        for name, module in [("rmlui", "rmlui"), ("freetype", "rmlui"), ("zep", "zep")]:
            repo = self.repository(name)
            self.declare(name, repo, module=module)
            self.clone_donor(name, repo)
        tree = self.repository("tree")
        (self.source / "cmake/tree_sitter.cmake").write_text(
            "macro(chibi_tree_sitter_source name repository tag)\n"
            " FetchContent_Declare(${name}\n GIT_REPOSITORY ${repository}\n GIT_TAG ${tag})\nendmacro()\n"
            f"chibi_tree_sitter_source(tree_sitter\n {tree} v1)\n")
        self.clone_donor("tree_sitter", tree)
        self.assertEqual(cache.publish_build(self.source, self.donor, self.storage), 7)
        native = self.root / "native"
        self.assertEqual(cache.prepare(self.source, native, self.storage), 7)
        self.assertIn("FETCHCONTENT_SOURCE_DIR_FREETYPE", (native / ".chibi-fetchcontent-cache.cmake").read_text())
        web = self.root / "web"
        self.assertEqual(cache.prepare(self.source, web, self.storage, web=True), 6)
        self.assertFalse((web / "_deps/sdl3-src").exists())

    def test_per_dependency_keys_and_configuration_fingerprint(self):
        first = self.repository("first")
        second = self.repository("second")
        self.declare("first", first)
        self.declare("second", second)
        pins = cache.source_pins(self.source)
        second_entry = cache.entry_path(self.storage, "second", pins["second"])
        before = cache.fingerprint(self.source)
        (self.source / "cmake/patches/first.patch").write_text("patch version 1")
        self.assertNotEqual(before, cache.fingerprint(self.source))
        self.assertEqual(second_entry, cache.entry_path(self.storage, "second", cache.source_pins(self.source)["second"]))
        (self.source / "CMakeLists.txt").write_text(declaration("first", first, "v2") + declaration("second", second, "v1"))
        newer = cache.source_pins(self.source)
        self.assertNotEqual(cache.dependency_key(pins["first"]), cache.dependency_key(newer["first"]))
        self.assertEqual(second_entry, cache.entry_path(self.storage, "second", newer["second"]))

    def test_dirty_donor_files_never_published_and_private_patch_isolation(self):
        repo = self.repository("rmlui")
        self.declare("rmlui", repo)
        donor = self.clone_donor("rmlui", repo)
        (donor / "source.c").write_text("donor patch\n")
        (donor / "untracked.c").write_text("must not be published")
        cache.publish_build(self.source, self.donor, self.storage)
        first = self.root / "first-build"
        second = self.root / "second-build"
        cache.prepare(self.source, first, self.storage)
        cache.prepare(self.source, second, self.storage)
        a = first / "_deps/rmlui-src"
        b = second / "_deps/rmlui-src"
        self.assertEqual((a / "source.c").read_text(), "original\n")
        self.assertFalse((a / "untracked.c").exists())
        (a / "source.c").write_text("branch patch\n")
        self.assertEqual((b / "source.c").read_text(), "original\n")
        shared = cache.entry_path(self.storage, "rmlui", cache.source_pins(self.source)["rmlui"]) / "rmlui-src"
        self.assertEqual((shared / "source.c").read_text(), "original\n")

    def test_submodule_snapshot_survives_donor_and_upstream_removal(self):
        sub = self.repository("dlg")
        parent = self.repository("freetype")
        git(parent, "submodule", "add", "-q", str(sub), "subprojects/dlg")
        git(parent, "commit", "-qam", "pin dlg")
        git(parent, "tag", "v2")
        self.declare("freetype", parent, "v2", module="rmlui")
        donor = self.clone_donor("freetype", parent)
        git(donor, "submodule", "update", "--init")
        (donor / "subprojects/dlg/source.c").write_text("dirty submodule\n")
        cache.publish_build(self.source, self.donor, self.storage)
        shutil.rmtree(self.donor)
        shutil.rmtree(parent)
        shutil.rmtree(sub)
        build = self.root / "independent"
        cache.prepare(self.source, build, self.storage)
        checkout = build / "_deps/freetype-src"
        child = checkout / "subprojects/dlg"
        self.assertEqual((child / "source.c").read_text(), "original\n")
        self.assertTrue((child / ".git").is_dir())
        self.assertEqual(git(child, "status", "--porcelain"), "")
        self.assertEqual(git(checkout, "status", "--porcelain"), "")
        self.assertEqual(git(checkout, "rev-list", "--count", "HEAD"), "1")
        self.assertEqual(git(checkout, "rev-parse", "v2^{commit}"), git(checkout, "rev-parse", "HEAD"))
        self.assertFalse((child / ".git/objects/info/alternates").exists())

    def test_parallel_publication_and_unknown_or_wrong_pin_rejection(self):
        repo = self.repository("zep")
        self.declare("zep", repo)
        donor = self.clone_donor("zep", repo)
        identity = cache.source_pins(self.source)["zep"]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as workers:
            results = list(workers.map(lambda _: cache.publish_one(self.storage, "zep", identity, donor), range(4)))
        self.assertEqual(sum(results), 1)
        self.assertTrue(cache.complete(cache.entry_path(self.storage, "zep", identity), "zep", identity))
        (donor / "source.c").write_text("a local commit\n")
        git(donor, "-c", "user.name=Cache Test", "-c", "user.email=test@example.com", "commit", "-qam", "local")
        with self.assertRaisesRegex(RuntimeError, "revision differs"):
            cache.publish_build(self.source, self.donor, self.storage)
        git(donor, "checkout", "--detach", "v1")
        self.clone_donor("unknown", repo)
        with self.assertRaisesRegex(RuntimeError, "unknown dependency"):
            cache.validate_donor(self.source, self.donor)

    def test_stale_owned_override_unset_without_mutating_private_or_user_sources(self):
        repo = self.repository("zep")
        self.declare("zep", repo)
        self.clone_donor("zep", repo)
        cache.publish_build(self.source, self.donor, self.storage)
        build = self.root / "build"
        cache.prepare(self.source, build, self.storage)
        target = build / "_deps/zep-src"
        (target / "source.c").write_text("keep local patch\n")
        (build / "CMakeCache.txt").write_text(f"FETCHCONTENT_SOURCE_DIR_ZEP:PATH={target}\n")
        (self.source / "CMakeLists.txt").write_text(declaration("zep", repo, "unavailable-new-pin"))
        cache.prepare(self.source, build, self.storage)
        self.assertIn("unset(FETCHCONTENT_SOURCE_DIR_ZEP CACHE)", (build / ".chibi-fetchcontent-cache.cmake").read_text())
        self.assertEqual((target / "source.c").read_text(), "keep local patch\n")
        # A user points the same cache variable elsewhere: preserve that intent.
        (build / "CMakeCache.txt").write_text(f"FETCHCONTENT_SOURCE_DIR_ZEP:PATH={self.root}/custom\n")
        cache.prepare(self.source, build, self.storage)
        self.assertNotIn("unset(FETCHCONTENT_SOURCE_DIR_ZEP", (build / ".chibi-fetchcontent-cache.cmake").read_text())


if __name__ == "__main__":
    unittest.main()
