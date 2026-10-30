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
PLAYER_OUTPUT ?= dist/chibi-player-$(shell uname -s | tr '[:upper:]' '[:lower:]')-$(shell uname -m)
UPDATE_RUNTIME ?= 1
CHIBI_COLLAB_URL ?=
FAST_BUILD_DIR ?= $(BUILD_DIR)
JOBS ?= 8
BUILD_TARGET ?= chibi chibi-compile chibi-player
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
DEBUG_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
RELEASE_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Release -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
SANITIZERS ?= address,undefined
SANITIZER_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=ON -DCHIBI_SANITIZERS=$(SANITIZERS) -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=ON
LOCAL_CMAKE_FLAGS ?= $(SANITIZER_CMAKE_FLAGS)
FAST_CMAKE_FLAGS ?= $(LOCAL_CMAKE_FLAGS)
# AddressSanitizer reserves a large shadow-memory range on macOS. Disable
# Apple's nano allocator before starting instrumented native binaries so the
# allocator does not report a failed nano-zone reservation.
NATIVE_RUNTIME_ENV ?= MallocNanoZone=0
# The directory of opt, llc, ld.lld and llvm-nm for native scalar execution
# in test-language-all. It runs only when this is set.
NATIVE_LLVM_BIN ?= $(CHIBI_LLVM_BIN)

.PHONY: all help build build-debug build-release prepare-project player-run  install package-player format check-format test test-full test-tools test-slow test-fast test-list test-compiler-parity test-render benchmark clean

all: build

help:
	@printf '%s\n' \
		'make build          Build the compiler, player and launcher' \
		'make build-debug    Build products in plain Debug' \
		'make build-release  Build products in Release' \
		'make package-player Package the separate deployed player' \
		'make prepare-project PROJECT_ROOT=/game ARTIFACT=/output/game.chibip' \
		'make player-run ARTIFACT=/output/game.chibip [RESOURCE_ROOT=/resources]' \
		'make install [PREFIX=~/.local]  Install the Release SDK, including chibi' \
		'make agent-list     List runnable CPU and explicit GPU selectors' \
		'make agent-test AGENT_SELECTOR=--source-systems' \
		'make agent-gpu-test AGENT_SELECTOR=--scene3d' \
		'make test / test-short  Six C execution-policy checks' \
		'make test-tools     Standalone C tooling integrations' \
		'make test-build     Build the full test drivers in BUILD_DIR' \
		'make test-native    CPU test_tools selectors' \
		'make test-materials Material simulation selectors' \
		'make test-projects  Project, asset import and launcher selectors' \
		'make test-language-all  Native language aggregate' \
		'make language-build Build the public language C consumer' \
		'make test-language-{semantic,execution,program,headless,prepared,format}' \
		'make test-full      CPU native, material, project, language and tooling groups' \
		'make test-render    Explicit GPU render assertions' \
		'make test-fast TEST_ARGS=--selector [TEST_BINARY=/path/to/driver]' \
		'make test-list      List command selectors and explicit groups' \
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
# Windows retains native construction until its build host can run processes.
# Keep its restricted C-only SDK profile until that replacement is verified.
sdk-reflect-build:
	@$(QUIET) configure-sdk-reflect -- cmake -S tools/reflect -B "$(SDK_REFLECT_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) -DCMAKE_BUILD_TYPE=Release -DCHIBI_SDK_BUILD_NATIVE=OFF -DCHIBI_SDK_BUILD_DESKTOP=OFF $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	@$(QUIET) build-sdk-reflect -- cmake --build "$(SDK_REFLECT_BUILD_DIR)" --target reflect --parallel $(JOBS)
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

player-run:
	@test -n "$(ARTIFACT)" && test -f "$(ARTIFACT)" || { printf '%s\n' 'Set ARTIFACT to an existing prepared project' >&2; exit 2; }
	@$(MAKE) package-player
	env $(NATIVE_RUNTIME_ENV) "$(PLAYER_OUTPUT)/bin/chibi-player" "$(abspath $(ARTIFACT))" --resource-root "$(abspath $(RESOURCE_ROOT))" $(ARGS)

# Installs the Release graph products into PREFIX (tools/toolchain/SDK.chibi):
# bin/chibi, bin/chibi-compile, bin/chibi-player, the private hosts in
# libexec/chibi and share/chibi.
install:
	@$(MAKE) build-release BUILD_TARGET='chibi chibi-compile chibi-player chibi-build-host chibi-compiler-host'
	@$(QUIET) install -- "$(RELEASE_BUILD_DIR)/sdk/chibi-build-host" --compiler tools/compiler --compiler-core packages/core --root "$(CURDIR)" --source tools/toolchain/SDK.chibi --core packages/core --collection graph=tools/graph --entry Install --timeout-ms "$(GRAPH_TIMEOUT_MS)" -- --build "$(abspath $(RELEASE_BUILD_DIR))" --prefix "$(abspath $(PREFIX))"

# The Release build generates its asset compiler with its own hosts, even when
# a sanitized build in the same make borrows them.
build-release:
	@$(MAKE) build BUILD_DIR="$(RELEASE_BUILD_DIR)" BUILD_PROFILE=release BUILD_DEBUG_OPTIMIZATION= BUILD_DEBUG_FRAME_POINTERS= BUILD_TARGET="$(BUILD_TARGET)" ASSET_GENERATOR_HOSTS=

package-player:
	@$(MAKE) package-runtime-sdk SDK_CMAKE_FLAGS="$(RELEASE_CMAKE_FLAGS)"
	@$(QUIET) package_player.py -- python3 tools/package_player.py --sdk "$(RUNTIME_SDK_PREFIX)" --output "$(PLAYER_OUTPUT)"

benchmark: test-build
	@$(MAKE) build-release BUILD_TARGET='chibi chibi-build-host chibi-compiler-host'
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" CHIBI_LANGUAGE_BUILD_DIR="$(BUILD_DIR)/test/language" CHIBI_CLI="$(RELEASE_BUILD_DIR)/sdk/chibi" CHIBI_LLVM_BIN="$(NATIVE_LLVM_BIN)" python3 tools/test_policy.py group benchmarks

format:
	@$(QUIET) clang-format.sh -- sh tools/clang-format.sh

check-format:
	@$(QUIET) clang-format.sh -- sh tools/clang-format.sh --check

# Agent selectors run existing drivers; use test-build to build them.
.PHONY: agent-list agent-test agent-gpu-test test-build test-native test-materials test-projects test-bootstrap bootstrap-artifact bootstrap-recover test-recovery test-short test-policy-inventory tools-build native-census
agent-list:
	@python3 tools/agent.py list

agent-test:
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" python3 tools/test_policy.py agent-env "$(BUILD_DIR)" test

agent-gpu-test:
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" python3 tools/test_policy.py agent-env "$(BUILD_DIR)" gpu-test

# The full test drivers build with the engine, its native packages and reflection.
test-build:
	@$(MAKE) build BUILD_TARGET='test_tools test_language'

test-native: test-build
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" CHIBI_LANGUAGE_BUILD_DIR="$(BUILD_DIR)/test/language" CHIBI_BUILD_HOST="$(CHIBI_BUILD_HOST)" python3 tools/test_policy.py group native

test-materials: test-build
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" python3 tools/test_policy.py group materials

# Project commands repeatedly load the authoring catalog. Use Release products
# for these integrations; the native driver retains its sanitizer coverage.
test-projects: test-build
	@$(MAKE) build-release BUILD_TARGET='chibi chibi-build-host chibi-compiler-host'
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" CHIBI_CLI="$(RELEASE_BUILD_DIR)/sdk/chibi" CHIBI_COMPILE="$(RELEASE_BUILD_DIR)/sdk/chibi-compile" CHIBI_LLVM_BIN="$(NATIVE_LLVM_BIN)" python3 tools/test_policy.py group projects

# Stage one compiling itself, and stage two compiling the same sources to the
# same bytes, take about two minutes each under sanitizers, so the bootstrap
# check runs against an unsanitized Release test_language.
test-bootstrap:
	@$(MAKE) build-release BUILD_TARGET=test_language
	@CHIBI_RELEASE_LANGUAGE_BUILD_DIR="$(RELEASE_BUILD_DIR)/test/language" python3 tools/test_policy.py tool language-package-compiler-chibi-bootstrap

# The bootstrap artifact is checked in at tools/compiler/bootstrap, as Zig
# checks in zig1.wasm. bootstrap-artifact rewrites it: the bootstrap check
# leaves stage two and its arguments in a seed folder, and the Release compiler
# host writes the artifact once stage two compiles the sources to itself.
# bootstrap-recover recovers the compiler from it with the sources on disk, and
# test-recovery with sources exported from HEAD. See docs/self-hosting-build.md.
BOOTSTRAP_ARTIFACT ?= tools/compiler/bootstrap
BOOTSTRAP_SEED = $(RELEASE_BUILD_DIR)/bootstrap.seed
BOOTSTRAP_DRIVER_BUILD = $(RELEASE_BUILD_DIR)/test/language
bootstrap-artifact:
	@$(MAKE) build-release BUILD_TARGET='test_language chibi-compiler-host'
	@rm -rf "$(BOOTSTRAP_SEED)" && mkdir -p "$(BOOTSTRAP_SEED)"
	@CHIBI_BOOTSTRAP_SEED="$(abspath $(BOOTSTRAP_SEED))" CHIBI_RELEASE_LANGUAGE_BUILD_DIR="$(BOOTSTRAP_DRIVER_BUILD)" python3 tools/test_policy.py tool language-package-compiler-chibi-bootstrap
	@"$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" --bootstrap "$(BOOTSTRAP_ARTIFACT)" --compiler tools/compiler --core packages/core --seed "$(BOOTSTRAP_SEED)" --revision "$$(git describe --always --dirty --abbrev=40)"
	@rm -rf "$(BOOTSTRAP_SEED)"

bootstrap-recover:
	@$(MAKE) build-release BUILD_TARGET=chibi-compiler-host
	@"$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" --bootstrap "$(BOOTSTRAP_ARTIFACT)" --compiler tools/compiler --core packages/core

test-recovery: test-build
	@$(MAKE) build-release BUILD_TARGET=chibi-compiler-host
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" CHIBI_RELEASE_COMPILER_HOST="$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" CHIBI_BOOTSTRAP_ARTIFACT="$(BOOTSTRAP_ARTIFACT)" python3 tools/test_policy.py tool bootstrap-recovery

# W21's census rewrites docs/native-compiler-census.md: the first native
# blocker of each self-hosted compiler function. See docs/native-llvm-ledger.md.
native-census: test-build
	@python3 tools/test_policy.py native "$(BUILD_DIR)/test/language/test_language" --native-census docs/native-compiler-census.md

test: test-short

TOOLS_BUILD_DIR ?= $(BUILD_DIR)/test/tools
TOOLS_C_COMPILER ?= $(if $(wildcard /opt/homebrew/opt/llvm/bin/clang),/opt/homebrew/opt/llvm/bin/clang,$(CC))

# Public SDK language checks configure independently of the engine and Studio.
LANGUAGE_BUILD_DIR ?= $(BUILD_DIR)/test/language
.PHONY: language-build test-language test-language-all test-language-semantic test-language-execution test-language-program test-language-headless test-language-prepared test-language-format
language-build:
	@$(MAKE) build BUILD_TARGET=test_language

test-language:
	@CHIBI_LANGUAGE_BUILD_DIR="$(LANGUAGE_BUILD_DIR)" python3 tools/test_policy.py tool language

test-language-all: test-build
	@CHIBI_LANGUAGE_BUILD_DIR="$(BUILD_DIR)/test/language" CHIBI_LLVM_BIN="$(NATIVE_LLVM_BIN)" python3 tools/test_policy.py group language

test-language-semantic:
	@CHIBI_LANGUAGE_BUILD_DIR="$(LANGUAGE_BUILD_DIR)" python3 tools/test_policy.py tool language-semantic

test-language-execution:
	@CHIBI_LANGUAGE_BUILD_DIR="$(LANGUAGE_BUILD_DIR)" python3 tools/test_policy.py tool language-execution

test-language-program:
	@CHIBI_LANGUAGE_BUILD_DIR="$(LANGUAGE_BUILD_DIR)" python3 tools/test_policy.py tool language-program

test-language-headless:
	@CHIBI_LANGUAGE_BUILD_DIR="$(LANGUAGE_BUILD_DIR)" python3 tools/test_policy.py tool language-headless

test-language-format:
	@CHIBI_LANGUAGE_BUILD_DIR="$(LANGUAGE_BUILD_DIR)" python3 tools/test_policy.py tool language-format

test-language-prepared:
	@CHIBI_LANGUAGE_BUILD_DIR="$(LANGUAGE_BUILD_DIR)" CHIBI_TOOLS_BUILD_DIR="$(TOOLS_BUILD_DIR)" python3 tools/test_policy.py tool language-prepared

tools-build:
	@$(MAKE) build BUILD_TARGET=test_tools

test-short: tools-build
	@python3 tools/test_policy.py unit "$(abspath $(TOOLS_BUILD_DIR))/test_tools"

test-tools: tools-build
	@python3 tools/test_policy.py short "$(abspath $(TOOLS_BUILD_DIR))/test_tools"

test-full test-slow: test-tools test-native test-materials test-projects test-language-all

test-render: test-build
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" python3 tools/test_policy.py group render

test-compiler-parity:
	@$(MAKE) build-release BUILD_TARGET=test_language
	@CHIBI_LANGUAGE_BUILD_DIR="$(RELEASE_BUILD_DIR)/test/language" CHIBI_RELEASE_LANGUAGE_BUILD_DIR="$(RELEASE_BUILD_DIR)/test/language" python3 tools/test_policy.py group compiler

TEST_BINARY ?= $(abspath $(TOOLS_BUILD_DIR))/test_tools
test-fast: tools-build
	@python3 tools/test_policy.py native-env TEST_ARGS "$(TEST_BINARY)"

test-list test-policy-inventory:
	@python3 tools/test_policy.py inventory

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
