# Convenience wrapper around the build. `make` builds: with the Chibi build
# graph on macOS, otherwise by configuring CMake.
BUILD_DIR ?= build
SDK_BUILD_DIR ?= build-sdk
SDK_PREFIX ?= dist/sdk
SDK_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF
RUNTIME_SDK_BUILD_DIR ?= build-runtime-sdk
RUNTIME_SDK_PREFIX ?= dist/runtime-sdk
DEBUG_BUILD_DIR ?= build-debug
RELEASE_BUILD_DIR ?= build-release
SDK_REFLECT_BUILD_DIR ?= build-sdk-reflect
PLAYER_OUTPUT ?= dist/chibi-player-$(shell uname -s | tr '[:upper:]' '[:lower:]')-$(shell uname -m)
UPDATE_RUNTIME ?= 1
WEB_THREADS ?= 0
WEB_WORKERS ?= 4
WEB_SUFFIX = $(if $(filter 1,$(WEB_THREADS)),-threads,)
WEB_SDK_BUILD_DIR ?= build-web-sdk$(WEB_SUFFIX)
WEB_SDK_PREFIX ?= dist/web-sdk$(WEB_SUFFIX)
WEB_BUILD_DIR ?= build-studio-web$(WEB_SUFFIX)
WEB_OUTPUT ?= dist/studio-web$(WEB_SUFFIX)
WEB_SDK_CMAKE_FLAGS ?=
WEB_CMAKE_FLAGS ?=
WEB_PROJECTS ?=
WEB_PORT ?= 8099
CHIBI_COLLAB_URL ?=
FAST_BUILD_DIR ?= $(BUILD_DIR)
JOBS ?= 8
BUILD_TARGET ?= chibi chibi-compile chibi-player chibi_ctl
BUILD_HOST ?= $(BUILD_DIR)/sdk/chibi-build-host
# Another build's sdk directory, such as build-release/sdk, whose
# chibi-build-host and chibi-compiler-host generate the asset compiler instead
# of this build's. The resource is the same; Release hosts generate it faster.
ASSET_GENERATOR_HOSTS ?=
ASSET_GENERATOR_HOSTS_PATH = $(if $(strip $(ASSET_GENERATOR_HOSTS)),$(abspath $(ASSET_GENERATOR_HOSTS)),)
BUILD_PROGRAM ?= tools/toolchain/Build.chibi
BUILD_TIMEOUT_MS ?= 1800000
# On macOS `make build` compiles with the Chibi build graph (PLAN.md M6), by
# profile name: sanitizer, or debug and release for build-debug and
# build-release. CMake builds only the first build host, in a tree beside the
# build directory, which the build-graph checks also compare against
# (graph-reference). Linux, the SDK, web and Studio builds still use CMake.
# CCACHE_BASEDIR, as CMake sets it, lets worktrees share ccache objects.
BUILD_PROFILE ?= sanitizer
# Opt-in Graph experiment; pair with matching CMAKE_<LANG>_FLAGS_DEBUG.
BUILD_DEBUG_OPTIMIZATION ?=
BUILD_DEBUG_FRAME_POINTERS ?=
GRAPH_BUILD = $(filter Darwin,$(shell uname -s))
GRAPH_PROGRAM ?= tools/toolchain/Graph.chibi
GRAPH_TIMEOUT_MS ?= 3600000
CMAKE_REFERENCE_DIR ?= $(BUILD_DIR)-cmake
BUILD_HOST_TARGET = $(if $(filter Linux Darwin,$(shell uname -s)),chibi-build-host,)
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

.PHONY: all help build build-debug build-release controller-build controller-run prepare-project player-run studio-build studio-run studio-dev package-studio install package-player web web-configure web-serial web-serve web-profile web-threads web-threads-serve format check-format test test-full test-tools test-slow test-fast test-list test-compiler-parity test-render benchmark clean

all: build

help:
	@printf '%s\n' \
		'make build          Build compiler, player, and controller products' \
		'make build-debug    Build products in plain Debug' \
		'make build-release  Build products in Release' \
		'make package-sdk    Package native compiler and desktop authoring SDK' \
		'make package-player Package the separate deployed player' \
		'make prepare-project PROJECT_ROOT=/game ARTIFACT=/output/game.chibip' \
		'make player-run ARTIFACT=/output/game.chibip [RESOURCE_ROOT=/resources]' \
		'make studio-build STUDIO_SOURCE=/studio STUDIO_SDK_PREFIX=/installed/sdk' \
		'make studio-run PROJECT_ROOT=/game  Run the external Studio consumer' \
		'make studio-dev PROJECT_ROOT=/game  Run Studio with checkpoint recovery' \
		'make package-studio Package the external Studio distribution' \
		'make install [PREFIX=~/.local]  Install the Release SDK, including chibi' \
		'make controller-build / controller-run PROJECT_ROOT=/game' \
		'make agent-list     List runnable CPU and explicit GPU selectors' \
		'make agent-test AGENT_SELECTOR=--source-edit' \
		'make agent-gpu-test AGENT_SELECTOR=--scene3d' \
		'make test / test-short  Six C execution-policy checks' \
		'make test-tools     Standalone C tooling integrations' \
		'make test-build     Build chibi_ctl and the full test drivers in BUILD_DIR' \
		'make test-native    CPU test_tools selectors' \
		'make test-materials Material simulation selectors' \
		'make test-projects  Project, asset import and chibi_ctl selectors' \
		'make test-language-all  Native language aggregate' \
		'make test-wasm         C language checks on WebAssembly in Node' \
		'make test-wasm-browser C language checks in Chrome (WEB_THREADS=1 for workers)' \
		'make language-build Build the public language C consumer' \
		'make test-language-{semantic,execution,program,headless,prepared,format}' \
		'make test-full      CPU native, material, project, language and tooling groups' \
		'make test-render    Explicit GPU render assertions' \
		'make test-fast TEST_ARGS=--selector [TEST_BINARY=/path/to/driver]' \
		'make test-list      List command selectors and explicit groups' \
		'make web STUDIO_SOURCE=/studio  Build external browser Studio and installed web SDK' \
		'make web-threads    Build the separate worker SDK/browser profile' \
		'make package-web-player  Build the browser player for chibi export --web' \
		'make format / check-format  Format or check project C sources' \
		'make clean          Remove default engine build outputs'

build:
	@if [ -x "$(BUILD_HOST)" ] && [ "$$("$(BUILD_HOST)" --protocol-version 2>/dev/null)" = 12 ]; then $(MAKE) toolchain-build; else $(MAKE) stage-zero-build; fi

.PHONY: toolchain-build stage-zero-build graph-reference
ifneq ($(GRAPH_BUILD),)
toolchain-build:
	@$(QUIET) third-party -- "$(BUILD_HOST)" --compiler tools/compiler --compiler-core packages/core --root "$(CURDIR)" --source "$(BUILD_PROGRAM)" --core packages/core --entry Build --timeout-ms "$(BUILD_TIMEOUT_MS)" -- third-party "$(BUILD_DIR)"
	@$(QUIET) graph -- env CCACHE_BASEDIR="$(CURDIR)" "$(BUILD_HOST)" --compiler tools/compiler --compiler-core packages/core --root "$(CURDIR)" --source "$(GRAPH_PROGRAM)" --core packages/core --collection graph=tools/graph --entry Libraries --timeout-ms "$(GRAPH_TIMEOUT_MS)" -- --build "$(abspath $(BUILD_DIR))" --profile "$(BUILD_PROFILE)" $(if $(strip $(BUILD_DEBUG_OPTIMIZATION)),--debug-optimization "$(BUILD_DEBUG_OPTIMIZATION)",) $(if $(strip $(BUILD_DEBUG_FRAME_POINTERS)),--debug-frame-pointers "$(BUILD_DEBUG_FRAME_POINTERS)",) --jobs "$(JOBS)" $(if $(ASSET_GENERATOR_HOSTS_PATH),--asset-generator-hosts "$(ASSET_GENERATOR_HOSTS_PATH)",) --all-commands on $(foreach target,$(BUILD_TARGET) $(if $(strip $(BUILD_TARGET)),$(BUILD_HOST_TARGET),),--target $(target))
	@ln -sfn "$(abspath $(BUILD_DIR))/compile_commands.json" compile_commands.json

# Recovery builds the first build host with CMake beside the build directory;
# the graph then builds its own.
stage-zero-build:
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(CMAKE_REFERENCE_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	@$(QUIET) build -- cmake --build "$(CMAKE_REFERENCE_DIR)" --target chibi-build-host --parallel $(JOBS)
	@$(MAKE) toolchain-build BUILD_HOST="$(CMAKE_REFERENCE_DIR)/sdk/chibi-build-host"
else
toolchain-build:
	@$(QUIET) native-toolchain -- "$(BUILD_HOST)" --compiler tools/compiler --compiler-core packages/core --root "$(CURDIR)" --source "$(BUILD_PROGRAM)" --core packages/core --entry Build --timeout-ms "$(BUILD_TIMEOUT_MS)" -- native "$(BUILD_DIR)" $(foreach target,$(BUILD_TARGET),--target $(target)) --jobs "$(JOBS)" -- $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) -DCHIBI_ASSET_GENERATOR_HOSTS="$(ASSET_GENERATOR_HOSTS_PATH)" $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)

# Recovery also creates the private host used by subsequent builds.
stage-zero-build:
	@$(QUIET) configure-native -- sh tools/configure_native.sh "$(BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(LOCAL_CMAKE_FLAGS) -DCHIBI_ASSET_GENERATOR_HOSTS="$(ASSET_GENERATOR_HOSTS_PATH)" $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	@$(QUIET) build -- cmake --build "$(BUILD_DIR)" $(if $(strip $(BUILD_TARGET)),--target $(BUILD_TARGET) $(BUILD_HOST_TARGET),) --parallel $(JOBS)
endif

# The CMake build the build-graph checks compare the graph with: configured
# with the sanitizer profile, with what they read built (reflect, its
# generated sources, the shaders and the build host).
graph-reference:
	@$(QUIET) configure-reference -- sh tools/configure_native.sh "$(CMAKE_REFERENCE_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(SANITIZER_CMAKE_FLAGS) $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	@$(QUIET) build-reference -- cmake --build "$(CMAKE_REFERENCE_DIR)" --target reflect generate_runtime_registry chibi_build_shaders chibi-build-host --parallel $(JOBS)
	@# Configuring links the root compile database to this tree; editors keep the graph's.
	@if [ -f "$(abspath $(BUILD_DIR))/compile_commands.json" ]; then ln -sfn "$(abspath $(BUILD_DIR))/compile_commands.json" compile_commands.json; fi

build-debug:
	@$(MAKE) build BUILD_DIR="$(DEBUG_BUILD_DIR)" LOCAL_CMAKE_FLAGS="$(DEBUG_CMAKE_FLAGS)" BUILD_PROFILE=debug BUILD_TARGET="$(BUILD_TARGET)"

.PHONY: sdk-reflect-build sdk-build sdk-install package-sdk runtime-sdk-build package-runtime-sdk
sdk-reflect-build:
	@$(QUIET) configure-sdk-reflect -- cmake -S tools/reflect -B "$(SDK_REFLECT_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) -DCMAKE_BUILD_TYPE=Release -DCHIBI_SDK_BUILD_NATIVE=OFF -DCHIBI_SDK_BUILD_DESKTOP=OFF $(if $(strip $(LIBCLANG_ROOT)),-DLIBCLANG_ROOT="$(LIBCLANG_ROOT)",)
	@$(QUIET) build-sdk-reflect -- cmake --build "$(SDK_REFLECT_BUILD_DIR)" --target reflect --parallel $(JOBS)

sdk-build: sdk-reflect-build
	@$(QUIET) configure-sdk -- sh tools/configure.sh sdk-native "$(SDK_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(SDK_CMAKE_FLAGS) -DCHIBI_SDK_BUILD_COMPILER=ON -DCHIBI_SDK_BUILD_NATIVE=ON -DCHIBI_SDK_BUILD_DESKTOP=ON -DCHIBI_REFLECT_EXECUTABLE="$(abspath $(SDK_REFLECT_BUILD_DIR))/reflect" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@$(QUIET) build-sdk -- cmake --build "$(SDK_BUILD_DIR)" --parallel $(JOBS)

sdk-install: sdk-build
	@cmake --install "$(SDK_BUILD_DIR)" --prefix "$(abspath $(SDK_PREFIX))"

package-sdk: sdk-build
	@python3 tools/package_sdk.py --build "$(SDK_BUILD_DIR)" --output "$(SDK_PREFIX)" --kind sdk

runtime-sdk-build: sdk-reflect-build
	@$(QUIET) configure-runtime-sdk -- sh tools/configure.sh sdk-native "$(RUNTIME_SDK_BUILD_DIR)" $(NATIVE_TOOLCHAIN_FLAGS) $(SDK_CMAKE_FLAGS) -DCHIBI_SDK_BUILD_COMPILER=OFF -DCHIBI_SDK_BUILD_NATIVE=ON -DCHIBI_SDK_BUILD_DESKTOP=ON -DCHIBI_REFLECT_EXECUTABLE="$(abspath $(SDK_REFLECT_BUILD_DIR))/reflect" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@$(QUIET) build-runtime-sdk -- cmake --build "$(RUNTIME_SDK_BUILD_DIR)" --parallel $(JOBS)

package-runtime-sdk: runtime-sdk-build
	@python3 tools/package_sdk.py --build "$(RUNTIME_SDK_BUILD_DIR)" --output "$(RUNTIME_SDK_PREFIX)" --kind runtime

controller-build:
	@$(MAKE) build BUILD_TARGET=chibi_ctl

PROJECT_ROOT ?=
ARTIFACT ?=
RESOURCE_ROOT ?= $(dir $(abspath $(ARTIFACT)))
SCENE ?=
SCENE_ARG = $(if $(SCENE),--scene "$(SCENE)")
# Studio's checkout, by default beside the engine's.
STUDIO_SOURCE ?= $(abspath $(CURDIR)/../chibi-studio)
STUDIO_SDK_PREFIX ?= $(SDK_PREFIX)
STUDIO_BUILD_DIR ?= build-studio
STUDIO_PACKAGE_BUILD_DIR ?= build-studio-package
STUDIO_OUTPUT ?= dist/chibi-studio-$(shell uname -s | tr '[:upper:]' '[:lower:]')-$(shell uname -m)
STUDIO_CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug
STUDIO_CHECKPOINT ?= $(STUDIO_BUILD_DIR)/workspace.json
PREFIX ?= $(HOME)/.local
INSTALL_SDK_BUILD_DIR ?= build-install-sdk
INSTALL_SDK_PREFIX ?= dist/install-sdk

controller-run:
	@test -n "$(PROJECT_ROOT)" || { printf '%s\n' 'Set PROJECT_ROOT to an authored game directory' >&2; exit 2; }
	@$(MAKE) controller-build
	env $(NATIVE_RUNTIME_ENV) "$(BUILD_DIR)/bin/chibi_ctl" --project-root "$(abspath $(PROJECT_ROOT))" $(ARGS)

prepare-project:
	@test -n "$(PROJECT_ROOT)" -a -n "$(ARTIFACT)" || { printf '%s\n' 'Set PROJECT_ROOT and ARTIFACT' >&2; exit 2; }
	@$(MAKE) package-sdk
	"$(SDK_PREFIX)/bin/chibi-compile" --project-root "$(abspath $(PROJECT_ROOT))" --output "$(abspath $(ARTIFACT))" $(SCENE_ARG) $(ARGS)

player-run:
	@test -n "$(ARTIFACT)" && test -f "$(ARTIFACT)" || { printf '%s\n' 'Set ARTIFACT to an existing prepared project' >&2; exit 2; }
	@$(MAKE) package-player
	env $(NATIVE_RUNTIME_ENV) "$(PLAYER_OUTPUT)/bin/chibi-player" "$(abspath $(ARTIFACT))" --resource-root "$(abspath $(RESOURCE_ROOT))" $(ARGS)

studio-build:
	@test -f "$(STUDIO_SOURCE)/CMakeLists.txt" || { printf '%s\n' 'No Studio checkout at $(STUDIO_SOURCE): clone chibi-studio beside chibi or set STUDIO_SOURCE' >&2; exit 2; }
	@test -f "$(STUDIO_SDK_PREFIX)/share/chibi/package.json" || { printf '%s\n' 'Set STUDIO_SDK_PREFIX to a packaged compiler/desktop SDK' >&2; exit 2; }
	@$(QUIET) configure-studio -- cmake -S "$(STUDIO_SOURCE)" -B "$(STUDIO_BUILD_DIR)" -G Ninja $(NATIVE_TOOLCHAIN_FLAGS) $(STUDIO_CMAKE_FLAGS) -DChibi_DIR="$(abspath $(STUDIO_SDK_PREFIX))/lib/cmake/Chibi"
	@$(QUIET) build-studio -- cmake --build "$(STUDIO_BUILD_DIR)" --target chibi-studio --parallel $(JOBS)

studio-run:
	@test -n "$(PROJECT_ROOT)" || { printf '%s\n' 'Set PROJECT_ROOT to an authored game directory' >&2; exit 2; }
	@$(MAKE) studio-build
	env $(NATIVE_RUNTIME_ENV) "$(STUDIO_BUILD_DIR)/bin/chibi-dev" --project-root "$(abspath $(PROJECT_ROOT))" $(SCENE_ARG) $(ARGS)

studio-dev:
	@test -n "$(PROJECT_ROOT)" || { printf '%s\n' 'Set PROJECT_ROOT to an authored game directory' >&2; exit 2; }
	@$(MAKE) studio-build
	env $(NATIVE_RUNTIME_ENV) "$(STUDIO_BUILD_DIR)/bin/chibi-dev" --checkpoint "$(abspath $(STUDIO_CHECKPOINT))" --project-root "$(abspath $(PROJECT_ROOT))" $(SCENE_ARG) $(ARGS)

package-studio:
	@$(MAKE) studio-build STUDIO_BUILD_DIR="$(STUDIO_PACKAGE_BUILD_DIR)" STUDIO_CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release -DCHIBI_STUDIO_DISTRIBUTION=ON"
	@$(QUIET) package-studio -- python3 "$(STUDIO_SDK_PREFIX)/share/chibi/tools/package_studio.py" --build "$(STUDIO_PACKAGE_BUILD_DIR)" --sdk "$(STUDIO_SDK_PREFIX)" --output "$(STUDIO_OUTPUT)"

# A Release SDK, in its own build tree so the development SDK stays as it is.
# Packaging computes the engine identity, engine.json, which exports carry;
# the install adds it to the build's files. Studio's `make install` builds
# against PREFIX.
install:
	@$(MAKE) package-sdk SDK_BUILD_DIR="$(INSTALL_SDK_BUILD_DIR)" SDK_PREFIX="$(INSTALL_SDK_PREFIX)" SDK_CMAKE_FLAGS="$(RELEASE_CMAKE_FLAGS)"
	@cmake --install "$(INSTALL_SDK_BUILD_DIR)" --prefix "$(abspath $(PREFIX))"
	@cmake -E copy "$(INSTALL_SDK_PREFIX)/share/chibi/engine.json" "$(abspath $(PREFIX))/share/chibi/engine.json"

# The Release build generates its asset compiler with its own hosts, even when
# a sanitized build in the same make borrows them.
build-release:
	@$(MAKE) build BUILD_DIR="$(RELEASE_BUILD_DIR)" LOCAL_CMAKE_FLAGS="$(RELEASE_CMAKE_FLAGS)" BUILD_PROFILE=release BUILD_DEBUG_OPTIMIZATION= BUILD_DEBUG_FRAME_POINTERS= BUILD_TARGET="$(BUILD_TARGET)" ASSET_GENERATOR_HOSTS=

package-player:
	@$(MAKE) package-runtime-sdk SDK_CMAKE_FLAGS="$(RELEASE_CMAKE_FLAGS)"
	@$(QUIET) package_player.py -- python3 tools/package_player.py --sdk "$(RUNTIME_SDK_PREFIX)" --output "$(PLAYER_OUTPUT)"

benchmark: test-build
	@$(MAKE) build-release BUILD_TARGET='chibi chibi-build-host chibi-compiler-host'
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" CHIBI_LANGUAGE_BUILD_DIR="$(BUILD_DIR)/test/language" CHIBI_CLI="$(RELEASE_BUILD_DIR)/sdk/chibi" CHIBI_LLVM_BIN="$(NATIVE_LLVM_BIN)" python3 tools/test_policy.py group benchmarks

# Browser Studio consumes a separately packaged Emscripten SDK generation.
.PHONY: web-sdk-build package-web-sdk
web-sdk-build: sdk-reflect-build
	@$(QUIET) configure-web-sdk -- sh tools/web_env.sh sh tools/configure.sh sdk-web "$(WEB_SDK_BUILD_DIR)" $(WEB_SDK_CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release -DCHIBI_SDK_BUILD_COMPILER=ON -DCHIBI_SDK_BUILD_NATIVE=ON -DCHIBI_SDK_BUILD_DESKTOP=ON -DCHIBI_ENABLE_SJLIT=OFF -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_WEB_THREADS=$(if $(filter 1,$(WEB_THREADS)),ON,OFF) -DCHIBI_WEB_WORKERS=$(WEB_WORKERS) -DCHIBI_REFLECT_EXECUTABLE="$(abspath $(SDK_REFLECT_BUILD_DIR))/reflect" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@$(QUIET) build-web-sdk -- cmake --build "$(WEB_SDK_BUILD_DIR)" --parallel $(JOBS)

package-web-sdk: web-sdk-build
	@python3 tools/package_sdk.py --build "$(WEB_SDK_BUILD_DIR)" --output "$(WEB_SDK_PREFIX)" --kind sdk

# The browser player that `chibi export --web` copies into a static site, and
# the compiler for the same target that prepares its game under Node. Both
# build apart from the web SDK package.
WEB_PLAYER_BUILD_DIR ?= build-web-player
WEB_PLAYER_OUTPUT ?= dist/web-player
.PHONY: package-web-player
package-web-player: sdk-reflect-build
	@$(QUIET) configure-web-player -- sh tools/web_env.sh sh tools/configure.sh sdk-web "$(WEB_PLAYER_BUILD_DIR)" $(WEB_SDK_CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release -DCHIBI_SDK_BUILD_COMPILER=ON -DCHIBI_SDK_BUILD_NATIVE=ON -DCHIBI_SDK_BUILD_DESKTOP=ON -DCHIBI_WEB_PLAYER=ON -DCHIBI_ENABLE_SJLIT=OFF -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_REFLECT_EXECUTABLE="$(abspath $(SDK_REFLECT_BUILD_DIR))/reflect"
	@$(QUIET) build-web-player -- cmake --build "$(WEB_PLAYER_BUILD_DIR)" --target chibi-player chibi-compile --parallel $(JOBS)
	@rm -rf "$(WEB_PLAYER_OUTPUT)"
	@cmake --install "$(WEB_PLAYER_BUILD_DIR)" --component web_player --prefix "$(abspath $(WEB_PLAYER_OUTPUT))"

web-configure:
	@test -f "$(STUDIO_SOURCE)/CMakeLists.txt" || { printf '%s\n' 'No Studio checkout at $(STUDIO_SOURCE): clone chibi-studio beside chibi or set STUDIO_SOURCE' >&2; exit 2; }
	@$(MAKE) package-web-sdk
	@$(QUIET) configure-web-studio -- sh tools/web_env.sh emcmake cmake -S "$(STUDIO_SOURCE)" -B "$(WEB_BUILD_DIR)" -G Ninja $(WEB_CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release -DChibi_DIR="$(abspath $(WEB_SDK_PREFIX))/lib/cmake/Chibi" -DCHIBI_STUDIO_WEB_PROJECTS="$(WEB_PROJECTS)" -DCHIBI_STUDIO_COLLAB_URL="$(CHIBI_COLLAB_URL)"

web: web-configure
	@$(QUIET) build-web-studio -- cmake --build "$(WEB_BUILD_DIR)" --target chibi-studio --parallel $(JOBS)
	@$(QUIET) install-web-studio -- cmake --install "$(WEB_BUILD_DIR)" --prefix "$(abspath $(WEB_OUTPUT))"

web-serial:
	@$(MAKE) web WEB_THREADS=0

web-threads:
	@$(MAKE) web WEB_THREADS=1

web-serve: web
	python3 "$(STUDIO_SOURCE)/tools/serve_web.py" --directory "$(WEB_OUTPUT)" --port $(WEB_PORT)

web-threads-serve:
	@$(MAKE) web-serve WEB_THREADS=1

web-profile:
	@$(MAKE) web-serve WEB_CMAKE_FLAGS="-DCHIBI_STUDIO_WEB_TRACE=ON $(WEB_CMAKE_FLAGS)"

format:
	@$(QUIET) clang-format.sh -- sh tools/clang-format.sh

check-format:
	@$(QUIET) clang-format.sh -- sh tools/clang-format.sh --check

# Agent selectors run existing drivers; use test-build to build them.
.PHONY: agent-list agent-test agent-gpu-test test-build test-native test-materials test-projects test-bootstrap bootstrap-artifact bootstrap-recover test-recovery test-short test-policy-inventory tools-build test-stage-one native-census
agent-list:
	@python3 tools/agent.py list

agent-test:
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" python3 tools/test_policy.py agent-env "$(BUILD_DIR)" test

agent-gpu-test:
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" python3 tools/test_policy.py agent-env "$(BUILD_DIR)" gpu-test

# The full test drivers build with the engine, its native packages and reflection.
test-build:
	@$(MAKE) build BUILD_TARGET='chibi_ctl test_tools test_language'

test-native: test-build
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" CHIBI_LANGUAGE_BUILD_DIR="$(BUILD_DIR)/test/language" python3 tools/test_policy.py group native

test-materials: test-build
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" python3 tools/test_policy.py group materials

# Project commands repeatedly load the authoring catalog. Use Release products
# for these integrations; the native driver retains its sanitizer coverage.
test-projects: test-build
	@$(MAKE) build-release BUILD_TARGET='chibi_ctl chibi chibi-build-host chibi-compiler-host'
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" CHIBI_CTL="$(RELEASE_BUILD_DIR)/bin/chibi_ctl" CHIBI_CLI="$(RELEASE_BUILD_DIR)/sdk/chibi" CHIBI_COMPILE="$(RELEASE_BUILD_DIR)/sdk/chibi-compile" CHIBI_LLVM_BIN="$(NATIVE_LLVM_BIN)" python3 tools/test_policy.py group projects

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
bootstrap-artifact:
	@$(MAKE) build-release BUILD_TARGET='test_language chibi-compiler-host'
	@rm -rf "$(BOOTSTRAP_SEED)" && mkdir -p "$(BOOTSTRAP_SEED)"
	@CHIBI_BOOTSTRAP_SEED="$(abspath $(BOOTSTRAP_SEED))" CHIBI_RELEASE_LANGUAGE_BUILD_DIR="$(RELEASE_BUILD_DIR)/test/language" python3 tools/test_policy.py tool language-package-compiler-chibi-bootstrap
	@"$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" --bootstrap "$(BOOTSTRAP_ARTIFACT)" --compiler tools/compiler --core packages/core --seed "$(BOOTSTRAP_SEED)" --revision "$$(git describe --always --dirty --abbrev=40)"
	@rm -rf "$(BOOTSTRAP_SEED)"

bootstrap-recover:
	@$(MAKE) build-release BUILD_TARGET=chibi-compiler-host
	@"$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" --bootstrap "$(BOOTSTRAP_ARTIFACT)" --compiler tools/compiler --core packages/core

test-recovery: test-build
	@$(MAKE) build-release BUILD_TARGET=chibi-compiler-host
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" CHIBI_RELEASE_COMPILER_HOST="$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" CHIBI_BOOTSTRAP_ARTIFACT="$(BOOTSTRAP_ARTIFACT)" python3 tools/test_policy.py tool bootstrap-recovery

# Stage one's route rebuilds Counter as chibi-compile does. Recovering stage
# one compiles the compiler, so it runs on a Release compiler host.
test-stage-one: test-build
	@$(MAKE) build-release BUILD_TARGET=chibi-compiler-host
	@CHIBI_TOOLS_BUILD_DIR="$(BUILD_DIR)/test/tools" CHIBI_RELEASE_COMPILER_HOST="$(RELEASE_BUILD_DIR)/sdk/chibi-compiler-host" python3 tools/test_policy.py tool game-equivalence-stage-one

# W21's census rewrites docs/native-compiler-census.md: the first native
# blocker of each self-hosted compiler function. See docs/native-llvm-ledger.md.
native-census: test-build
	@python3 tools/test_policy.py native "$(BUILD_DIR)/test/language/test_language" --native-census docs/native-compiler-census.md

test: test-short

TOOLS_BUILD_DIR ?= build-tools
TOOLS_C_COMPILER ?= $(if $(wildcard /opt/homebrew/opt/llvm/bin/clang),/opt/homebrew/opt/llvm/bin/clang,$(CC))

# Public SDK language checks configure independently of the engine and Studio.
LANGUAGE_BUILD_DIR ?= build-language
.PHONY: language-build test-language test-language-all test-language-semantic test-language-execution test-language-program test-language-headless test-language-prepared test-language-format
language-build:
	@$(QUIET) configure-language -- cmake -S test/language -B "$(LANGUAGE_BUILD_DIR)" -DCMAKE_C_COMPILER="$(TOOLS_C_COMPILER)" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@$(QUIET) build-language -- cmake --build "$(LANGUAGE_BUILD_DIR)" --parallel $(JOBS)

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

# The reflection identity cases run the reflect that `make build` produced.
TOOLS_REFLECT ?= $(wildcard $(abspath $(BUILD_DIR))/reflect)

tools-build:
	@cmake -S test/tools -B "$(TOOLS_BUILD_DIR)" -DCMAKE_C_COMPILER="$(TOOLS_C_COMPILER)" -DCMAKE_BUILD_TYPE=Debug -DCHIBI_REFLECT_EXECUTABLE="$(TOOLS_REFLECT)"
	@cmake --build "$(TOOLS_BUILD_DIR)" --parallel $(JOBS)

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
	@for dir in "$(abspath $(BUILD_DIR))" "$(abspath $(CMAKE_REFERENCE_DIR))"; do \
		if git worktree list --porcelain | awk -v root="$$dir" \
			'$$1 == "worktree" && ($$2 == root || index($$2, root "/") == 1) { found = 1 } END { exit !found }'; then \
			printf '%s\n' "Skipping $$dir: it contains a registered worktree"; \
		else \
			rm -rf -- "$$dir"; \
		fi; \
	done
	rm -rf "$(DEBUG_BUILD_DIR)" "$(DEBUG_BUILD_DIR)-cmake" "$(RELEASE_BUILD_DIR)" "$(RELEASE_BUILD_DIR)-cmake" "$(TOOLS_BUILD_DIR)" "$(WEB_BUILD_DIR)"
# Full-function browser JIT checks use the existing C language dispatcher.
WASM_BUILD_DIR ?= build-wasm-language$(WEB_SUFFIX)
.PHONY: wasm-language-build test-wasm
wasm-language-build:
	@$(QUIET) configure-wasm-language -- sh tools/web_env.sh emcmake cmake -S test/language -B "$(WASM_BUILD_DIR)" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHIBI_WEB_THREADS=$(if $(filter 1,$(WEB_THREADS)),ON,OFF)
	@$(QUIET) build-wasm-language -- cmake --build "$(WASM_BUILD_DIR)" --target test_language --parallel $(JOBS)

test-wasm: wasm-language-build
	@CHIBI_WASM_BUILD_DIR="$(WASM_BUILD_DIR)" python3 tools/test_policy.py group wasm

.PHONY: test-wasm-browser
test-wasm-browser: wasm-language-build tools-build
	@CHIBI_TOOLS_BUILD_DIR="$(TOOLS_BUILD_DIR)" CHIBI_WASM_TEST_ROOT="$(abspath $(WASM_BUILD_DIR))" python3 tools/test_policy.py tool wasm-browser
