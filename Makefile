# Convenience wrapper around the build. `make` builds: with the Chibi build
# graph on Linux and macOS, otherwise by configuring CMake.
BUILD_DIR ?= build
SDK_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF
RUNTIME_SDK_BUILD_DIR ?= build-runtime-sdk
RUNTIME_SDK_PREFIX ?= dist/runtime-sdk
DEBUG_BUILD_DIR ?= build-debug
RELEASE_BUILD_DIR ?= build-release
SDK_REFLECT_BUILD_DIR ?= build-sdk-reflect
# Make's abspath splits paths at spaces. Preserve this shared SDK handoff.
SDK_REFLECT_PATH = $(if $(or $(filter /%,$(firstword $(SDK_REFLECT_BUILD_DIR))),$(findstring :/,$(firstword $(SDK_REFLECT_BUILD_DIR)))),$(SDK_REFLECT_BUILD_DIR),$(CURDIR)/$(SDK_REFLECT_BUILD_DIR))
SDK_REFLECT_LLVM_ROOT = $(if $(or $(filter /%,$(firstword $(LIBCLANG_ROOT))),$(findstring :/,$(firstword $(LIBCLANG_ROOT)))),$(LIBCLANG_ROOT),$(CURDIR)/$(LIBCLANG_ROOT))
UPDATE_RUNTIME ?= 1
CHIBI_COLLAB_URL ?=
FAST_BUILD_DIR ?= $(BUILD_DIR)
JOBS ?= 8
BUILD_TARGET ?= chibi chibi-compile
BUILD_HOST ?= $(BUILD_DIR)/sdk/chibi-build-host
CHIBI_BUILD_HOST ?= $(BUILD_DIR)/sdk/chibi-build-host
# Another build's sdk directory, such as build-release/sdk, whose
# chibi-build-host and chibi-compiler-host generate the asset compiler instead
# of this build's. The resource is the same; Release hosts generate it faster.
ASSET_GENERATOR_HOSTS ?=
BUILD_PROGRAM ?= tools/toolchain/Build.chibi
BUILD_TIMEOUT_MS ?= 1800000
# On Linux and macOS `make build` compiles with the Chibi build graph (PLAN.md M6), by
# profile name: sanitizer, or debug and release for build-debug and
# build-release. CMake builds only the first build host, in a tree beside the
# CCACHE_BASEDIR, as CMake sets it, lets worktrees share ccache objects.
BUILD_PROFILE ?= sanitizer
# Opt-in Graph experiment; pair with matching CMAKE_<LANG>_FLAGS_DEBUG.
BUILD_DEBUG_OPTIMIZATION ?=
BUILD_DEBUG_FRAME_POINTERS ?=
GRAPH_PROGRAM ?= tools/toolchain/Graph.chibi
GRAPH_TIMEOUT_MS ?= 3600000
# Optional existing native reflection tool, including cross-build wrappers.
GRAPH_REFLECT ?=
VERBOSE ?= 0
export VERBOSE TEST_ARGS AGENT_SELECTOR
MAKEFLAGS += --no-print-directory
QUIET = python3 tools/quiet.py
AGENT_SELECTOR ?=
TEST_ARGS ?=
TEST_TIMINGS ?= 0
# Group scheduling belongs to the runner, not recursive make parallelism.
TEST_JOBS ?= $(CHIBI_TEST_JOBS)
ifneq ($(strip $(TEST_JOBS)),)
export CHIBI_TEST_JOBS := $(TEST_JOBS)
endif
ifneq ($(strip $(TEST_ARGS)),)
ifneq ($(MAKECMDGOALS),test-fast)
$(error TEST_ARGS is only supported with test-fast as the sole make goal)
endif
endif
# Use installed upstream LLVM for native sanitizer checks when available.
NATIVE_TOOLCHAIN_FLAGS ?= $(if $(wildcard /opt/homebrew/opt/llvm/bin/clang),-DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++)
SANITIZERS ?= address,undefined
# AddressSanitizer reserves a large shadow-memory range on macOS. Disable
# Apple's nano allocator before starting instrumented native binaries so the
# allocator does not report a failed nano-zone reservation.
NATIVE_RUNTIME_ENV ?= MallocNanoZone=0
# The directory of opt, llc, ld.lld and llvm-nm for native scalar execution
# in test-language-all. It runs only when this is set.
NATIVE_LLVM_BIN ?= $(CHIBI_LLVM_BIN)

.PHONY: all help build build-debug build-release prepare-project install format check-format clean

all: build

help:
	@printf '%s\n' \
		'make build          Build the compiler, player and launcher' \
		'make build-debug    Build products in plain Debug' \
		'make build-release  Build products in Release' \
		'make prepare-project PROJECT_ROOT=/game ARTIFACT=/output/game.chibip' \
		'make install [PREFIX=~/.local]  Install the Release SDK, including chibi' \
		'make format / check-format  Format or check project C sources' \
		'make clean          Remove default engine build outputs'

# The engine build is `chibi build`, tools/toolchain/Driver.chibi (tools/build.sh
# runs it before a chibi exists). These variables become its options.
BUILD_PROFILE_FLAG = $(if $(filter release,$(BUILD_PROFILE)),--release,$(if $(filter debug,$(BUILD_PROFILE)),--debug,))
BUILD_FLAGS = $(BUILD_PROFILE_FLAG) --build "$(BUILD_DIR)" --release-build "$(RELEASE_BUILD_DIR)" --jobs "$(JOBS)" --host "$(BUILD_HOST)" $(if $(strip $(BUILD_TARGET)),$(foreach target,$(BUILD_TARGET),--target $(target)),--all) $(if $(strip $(LIBCLANG_ROOT)),--libclang-root "$(LIBCLANG_ROOT)",) $(if $(strip $(GRAPH_REFLECT)),--reflect "$(GRAPH_REFLECT)",) $(if $(strip $(BUILD_DEBUG_OPTIMIZATION)),--debug-optimization "$(BUILD_DEBUG_OPTIMIZATION)",) $(if $(strip $(BUILD_DEBUG_FRAME_POINTERS)),--debug-frame-pointers "$(BUILD_DEBUG_FRAME_POINTERS)",) $(if $(strip $(ASSET_GENERATOR_HOSTS)),--asset-generator-hosts "$(ASSET_GENERATOR_HOSTS)",)
build:
	@sh tools/build.sh build $(BUILD_FLAGS)

build-debug:
	@$(MAKE) build BUILD_DIR="$(DEBUG_BUILD_DIR)" BUILD_PROFILE=debug BUILD_TARGET="$(BUILD_TARGET)"

# The private build host that runs the authored SDK programs: the build's own, or
# the one cold recovery constructs beside the reflection build.
define SDK_GRAPH_HOST
sh tools/bootstrap/seed.sh || exit $$?; \
	host="$(BUILD_HOST)"; \
	if [ ! -x "$$host" ] || [ "$$("$$host" --protocol-version 2>/dev/null)" != 14 ]; then \
		host="$(SDK_REFLECT_BUILD_DIR)/host/bin/chibi-build-host"; \
		if [ ! -x "$$host" ] || [ "$$("$$host" --protocol-version 2>/dev/null)" != 14 ]; then \
			$(MAKE) sdk-reflect-bootstrap || exit $$?; \
		fi; \
	fi;
endef

.PHONY: sdk-reflect-build runtime-sdk-build package-runtime-sdk
ifneq ($(filter Linux Darwin,$(shell uname -s)),)
sdk-reflect-build:
	@$(SDK_GRAPH_HOST) \
	$(QUIET) build-sdk-reflect -- env CCACHE_BASEDIR="$(CURDIR)" "$$host" --compiler tools/compiler --compiler-core packages/core --root "$(CURDIR)" --source "$(GRAPH_PROGRAM)" --core packages/core --collection graph=tools/graph --entry SDKReflect --timeout-ms "$(GRAPH_TIMEOUT_MS)" -- --build "$(SDK_REFLECT_PATH)" --jobs "$(JOBS)" $(if $(strip $(LIBCLANG_ROOT)),--libclang-root "$(SDK_REFLECT_LLVM_ROOT)",) -- $(NATIVE_TOOLCHAIN_FLAGS)

# Cold recovery fetches the host that runs the authored graph.
.PHONY: sdk-reflect-bootstrap
sdk-reflect-bootstrap:
	@$(QUIET) bootstrap -- sh tools/bootstrap.sh "$(SDK_REFLECT_BUILD_DIR)/host/bin/chibi-build-host"
else
# Windows has no graph build host that runs processes yet.
sdk-reflect-build:
	@printf '%s\n' 'The graph does not run on Windows yet.' >&2; exit 2
endif

ifeq ($(shell uname -s),Darwin)
# The macOS runtime SDK is constructed by the Chibi graph and installed by
# tools/toolchain/RuntimeSDK.chibi. Linux keeps the CMake route below.
runtime-sdk-build: sdk-reflect-build
	@$(SDK_GRAPH_HOST) \
	$(QUIET) third-party -- "$$host" --compiler tools/compiler --compiler-core packages/core --root "$(CURDIR)" --source "$(BUILD_PROGRAM)" --core packages/core --entry Build --timeout-ms "$(BUILD_TIMEOUT_MS)" -- third-party "$(RUNTIME_SDK_BUILD_DIR)" && \
	$(QUIET) build-runtime-sdk -- env CCACHE_BASEDIR="$(CURDIR)" "$$host" --compiler tools/compiler --compiler-core packages/core --root "$(CURDIR)" --source "$(GRAPH_PROGRAM)" --core packages/core --collection graph=tools/graph --entry RuntimeSDK --timeout-ms "$(GRAPH_TIMEOUT_MS)" -- --build "$(abspath $(RUNTIME_SDK_BUILD_DIR))" --reflect "$(SDK_REFLECT_PATH)/reflect" --jobs "$(JOBS)" $(if $(strip $(LIBCLANG_ROOT)),--libclang-root "$(SDK_REFLECT_LLVM_ROOT)",) -- $(NATIVE_TOOLCHAIN_FLAGS) $(SDK_CMAKE_FLAGS)

package-runtime-sdk: runtime-sdk-build
	@$(SDK_GRAPH_HOST) \
	$(QUIET) package-runtime-sdk -- "$$host" --compiler tools/compiler --compiler-core packages/core --root "$(CURDIR)" --source tools/toolchain/RuntimeSDK.chibi --core packages/core --collection graph=tools/graph --entry RuntimePackage --timeout-ms "$(GRAPH_TIMEOUT_MS)" -- --build "$(abspath $(RUNTIME_SDK_BUILD_DIR))" --output "$(abspath $(RUNTIME_SDK_PREFIX))"
else
runtime-sdk-build package-runtime-sdk:
	@printf '%s\n' 'The runtime SDK is constructed on macOS only; Linux returns with the graph.' >&2; exit 2
endif

PROJECT_ROOT ?=
ARTIFACT ?=
RESOURCE_ROOT ?= $(dir $(abspath $(ARTIFACT)))
SCENE ?=
SCENE_ARG = $(if $(SCENE),--scene "$(SCENE)")
PREFIX ?= $(HOME)/.local

prepare-project:
	@test -n "$(PROJECT_ROOT)" -a -n "$(ARTIFACT)" || { printf '%s\n' 'Set PROJECT_ROOT and ARTIFACT' >&2; exit 2; }
	@$(MAKE) build-release BUILD_TARGET=chibi-compile
	"$(RELEASE_BUILD_DIR)/sdk/chibi-compile" --project-root "$(abspath $(PROJECT_ROOT))" --output "$(abspath $(ARTIFACT))"

# Installs the Release graph products into PREFIX (tools/toolchain/SDK.chibi):
# bin/chibi, bin/chibi-compile, the private hosts in
# libexec/chibi and share/chibi.
install:
	@$(MAKE) build-release BUILD_TARGET='chibi chibi-compile chibi-build-host chibi-compiler-host'
	@$(QUIET) install -- "$(RELEASE_BUILD_DIR)/sdk/chibi-build-host" --compiler tools/compiler --compiler-core packages/core --root "$(CURDIR)" --source tools/toolchain/SDK.chibi --core packages/core --collection graph=tools/graph --entry Install --timeout-ms "$(GRAPH_TIMEOUT_MS)" -- --build "$(abspath $(RELEASE_BUILD_DIR))" --prefix "$(abspath $(PREFIX))"

# The Release build generates its asset compiler with its own hosts, even when
# a sanitized build in the same make borrows them.
build-release:
	@$(MAKE) build BUILD_DIR="$(RELEASE_BUILD_DIR)" BUILD_PROFILE=release BUILD_DEBUG_OPTIMIZATION= BUILD_DEBUG_FRAME_POINTERS= BUILD_TARGET="$(BUILD_TARGET)" ASSET_GENERATOR_HOSTS=

format:
	@$(QUIET) clang-format.sh -- sh tools/clang-format.sh

check-format:
	@$(QUIET) clang-format.sh -- sh tools/clang-format.sh --check

# Tests run with `chibi build test` (tools/build.sh test before a chibi exists):
# the selectors and groups are listed in tools/tests.tsv, and --list names them.
# The bootstrap artifact lives at tools/compiler/bootstrap: bootstrap.json is
# checked in and pins the seed, compiler.chibip, which tools/bootstrap/seed.sh
# fetches from a release, as Zig ships zig1.wasm. bootstrap-artifact rewrites
# both, and publish-seed releases the new seed and pins it in
# tools/bootstrap/seed.tsv. The bootstrap check
# leaves stage two and its arguments in a seed folder, and the Release compiler
# host writes the artifact once stage two compiles the sources to itself.
# bootstrap-recover recovers the compiler from it with the sources on disk, and
# the bootstrap-recovery selector with sources exported from HEAD. See
# docs/self-hosting-build.md.
BOOTSTRAP_ARTIFACT ?= tools/compiler/bootstrap
BOOTSTRAP_SEED = $(RELEASE_BUILD_DIR)/bootstrap.seed
.PHONY: bootstrap-artifact bootstrap-refresh publish-seed bootstrap-recover native-census
bootstrap-artifact:
	@$(MAKE) build-release BUILD_TARGET='test_language chibi-compiler-host'
	@rm -rf "$(BOOTSTRAP_SEED)" && mkdir -p "$(BOOTSTRAP_SEED)"
	@CHIBI_BOOTSTRAP_SEED="$(abspath $(BOOTSTRAP_SEED))" sh tools/build.sh test --release-build "$(RELEASE_BUILD_DIR)" language-package-compiler-chibi-bootstrap
	@"$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" --bootstrap "$(BOOTSTRAP_ARTIFACT)" --compiler tools/compiler --core packages/core --seed "$(BOOTSTRAP_SEED)" --revision "$$(git describe --always --dirty --abbrev=40)"
	@rm -rf "$(BOOTSTRAP_SEED)"

# bootstrap-refresh writes a seed for the sources on disk without the
# bootstrap check's sample runs: recovery's fixed point and the arguments it
# compiled the sources with make the seed folder, and writing it proves again
# that the seed compiles the sources to itself.
BOOTSTRAP_REFRESH = $(RELEASE_BUILD_DIR)/bootstrap.refresh
bootstrap-refresh:
	@$(MAKE) build-release BUILD_TARGET=chibi-compiler-host
	@rm -rf "$(BOOTSTRAP_REFRESH)" && mkdir -p "$(BOOTSTRAP_REFRESH)"
	@"$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" --bootstrap "$(BOOTSTRAP_ARTIFACT)" --compiler tools/compiler --core packages/core --recover "$(BOOTSTRAP_REFRESH)/compiler.chibip" --arguments "$(BOOTSTRAP_REFRESH)/compile.json"
	@"$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" --bootstrap "$(BOOTSTRAP_ARTIFACT)" --compiler tools/compiler --core packages/core --seed "$(BOOTSTRAP_REFRESH)" --revision "$$(git describe --always --dirty --abbrev=40)"
	@rm -rf "$(BOOTSTRAP_REFRESH)"

publish-seed:
	@sh tools/bootstrap/publish-seed.sh

bootstrap-recover:
	@$(MAKE) build-release BUILD_TARGET=chibi-compiler-host
	@"$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" --bootstrap "$(BOOTSTRAP_ARTIFACT)" --compiler tools/compiler --core packages/core

# W21's census rewrites docs/native-compiler-census.md: the first native
# blocker of each self-hosted compiler function. See docs/native-llvm-ledger.md.
native-census:
	@$(MAKE) build BUILD_TARGET=test_language
	@"$(BUILD_DIR)/test/language/test_language" --native-census docs/native-compiler-census.md

clean:
	@for dir in "$(abspath $(BUILD_DIR))"; do \
		if git worktree list --porcelain | awk -v root="$$dir" \
			'$$1 == "worktree" && ($$2 == root || index($$2, root "/") == 1) { found = 1 } END { exit !found }'; then \
			printf '%s\n' "Skipping $$dir: it contains a registered worktree"; \
		else \
			rm -rf -- "$$dir"; \
		fi; \
	done
	rm -rf "$(DEBUG_BUILD_DIR)" "$(DEBUG_BUILD_DIR)-cmake" "$(RELEASE_BUILD_DIR)" "$(RELEASE_BUILD_DIR)-cmake"
