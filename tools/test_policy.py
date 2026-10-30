#!/usr/bin/env python3
"""Named test commands with exit propagation and descendant cleanup."""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import platform
import signal
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
PROCESS_GROUP = 'CHIBI_RUNNER_PROCESS_GROUP'
# Process tables can take seconds to enumerate on a loaded host. Bound a stuck
# inspection separately from the caller's test timeout.
PROCESS_INSPECTION_SECONDS = 5
TOOLS = {
    'types': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--types'],
    'execution-scope': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                        '--execution-scope'],
    'player-clock': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--player-clock'],
    'player-url': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--player-url'],
    'audio-browser': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--audio-browser'],
    'audio-analysis': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--audio-analysis'],
    'shader-webgpu': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--shader-webgpu'],
    'studio-panel-idle': [str(Path(os.environ.get('CHIBI_STUDIO_BUILD', 'build-studio')) / 'bin/studio-acceptance'),
                          '--panel-idle', '--project-root', os.environ.get('CHIBI_STUDIO_IDLE_PROJECT', '')],
    'studio-scene-collider3d': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-scene-collider3d'],
    'studio-scene-gizmo3d': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-scene-gizmo3d'],
    'studio-scene-camera': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-scene-camera'],
    'studio-scene-move': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-scene-move'],
    'studio-outliner-move': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-outliner-move'],
    'tilemap': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--tilemap'],
    'studio-graph-visibility': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-graph-visibility'],
    'studio-graph-pointer': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-graph-pointer'],
    'studio-asset-pointer': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-asset-pointer'],
    'studio-debugger-source-history': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-debugger-source-history'],
    'studio-source-edit': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-source-edit'],
    'unit-fixtures': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--unit-fixtures'],
    'shader-parameters-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--shader-parameters-prepared'],
    'shader-source-debug': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--shader-source-debug'],
    'ecs-query': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--ecs-query'],
    'ecs-strings': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--ecs-strings'],
    'ecs-query-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--ecs-query-benchmark'],
    'arena': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--arena'],
    'types': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--types'],
    'source-systems': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-systems'],
    'source-reductions': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-reductions'],
    'source-reduction-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-reduction-benchmark'],
    'source-execution-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-execution-benchmark'],
    'persistent-store': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--persistent-store'],
    'vector-nodes': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--vector-nodes'],
    'package': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--package'],
    'program-sdk': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--program-sdk'],
    'chibi-cli-build': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--chibi-cli-build', str(ROOT / os.environ.get('CHIBI_CLI', 'build-release/sdk/chibi'))],
    'chibi-cli-doctor': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--chibi-cli-doctor', str(ROOT / os.environ.get('CHIBI_CLI', 'build-release/sdk/chibi'))],
    'chibi-cli-process': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--chibi-cli-process', str(ROOT / os.environ.get('CHIBI_CLI', 'build-release/sdk/chibi'))],
    'chibi-cli': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                  '--chibi-cli', str(ROOT / os.environ.get('CHIBI_CLI', 'build-release/sdk/chibi'))],
    'native-build': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                     '--native-build', str(ROOT / os.environ.get('CHIBI_CLI', 'build-release/sdk/chibi'))],
    # W20's kernels through chibi build --native; needs CHIBI_LLVM_BIN.
    'native-workload-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                                  '--native-workload-benchmark', str(ROOT / os.environ.get('CHIBI_CLI', 'build-release/sdk/chibi'))],
    'chibi-cli-installed': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                            '--chibi-cli-installed', str(ROOT / os.environ.get('CHIBI_INSTALLED_CLI', 'dist/sdk/bin/chibi'))],
    'native-aot-driver': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                          '--native-aot-driver', str(ROOT / 'tools/native_aot/Driver.chibi')],
    'build-process': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                      '--build-process',
                      str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'policy_worker'),
                      str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'build-timings': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                      '--build-timings',
                      str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'policy_worker'),
                      str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'build-process-files': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                           '--build-process-files',
                           str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'policy_worker'),
                           str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'compiler-artifact': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                         '--compiler-artifact',
                         str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                         str(ROOT / os.environ.get('CHIBI_COMPILER_HOST', 'build/sdk/chibi-compiler-host'))],
    'compiler-bootstrap': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                           '--compiler-bootstrap',
                           str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                           str(ROOT / os.environ.get('CHIBI_COMPILER_HOST', 'build/sdk/chibi-compiler-host'))],
    'bootstrap-recovery': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                           '--bootstrap-recovery',
                           str(ROOT / os.environ.get('CHIBI_RELEASE_COMPILER_HOST', 'build-release/sdk/chibi-compiler-host')),
                           str(ROOT / os.environ.get('CHIBI_BOOTSTRAP_ARTIFACT', 'tools/compiler/bootstrap'))],
    'build-graph': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                    '--build-graph',
                    str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'build-graph-link-response': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                                 '--build-graph-link-response',
                                 str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'build-graph-support': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                            '--build-graph-support',
                            str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'build-graph-reflect': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                            '--build-graph-reflect',
                            str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'build-graph-generated': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                              '--build-graph-generated',
                              str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'build-graph-debug-optimization': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                                       '--build-graph-debug-optimization',
                                       str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'build-graph-programs': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                             '--build-graph-programs',
                             str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'build-sdk': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                  '--build-sdk',
                  str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build-release/sdk/chibi-build-host')),
                  str(ROOT / os.environ.get('CHIBI_SDK_GRAPH_BUILD_DIR', 'build-release')),
                  os.environ.get('CHIBI_SDK_THIRD_PARTY_DIR', '')],
    'runtime-sdk': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                    '--runtime-sdk',
                    str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    'third-party-publish': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--third-party-publish'],
    'collab': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--collab'],
    'whole-alias': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-alias'],
    'whole-integer': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-integer'],
    'whole-conversion': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-conversion'],
    'whole-select': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-select'],
    'whole-math': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-math'],
    'owned-tick': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--owned-tick'],
    'parallel-tick': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--parallel-tick'],
    'source-native-features': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-native-features'],
    'source-native-numeric-queries': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-native-numeric-queries'],
    'source-native-material': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-native-material'],
    'material-destruction': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-destruction'],
    'material-fluids': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-fluids'],
    'material-mpm': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-mpm'],
    'material-simulation': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-simulation'],
    'material-ecs': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-ecs'],
    'material-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-benchmark'],
    'material-render': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-render'],
    'render-resources': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--render-resources'],
    'render-geometry': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--render-geometry'],
    'interior-border': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--interior-border'],
    'viewport-resize': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--viewport-resize'],
    'sdf-visibility': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--sdf-visibility'],
    'radiance': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--radiance'],
    'radiance-depth': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--radiance-depth'],
    'material-sampling': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--material-sampling'],
    'scene2d': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene2d'],
    'material-terrain-seams': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--material-terrain-seams'],
    'scene3d-gles': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-gles'],
    'scene3d-gi': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-gi'],
    'scene3d-gi-gles': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-gi-gles'],
    'material-ramps': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--material-ramps'],
    'scene3d-post': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-post'],
    'scene3d-shapes': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-shapes'],
    'radiance-surface': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--radiance-surface'],
    'radiance-horizons4': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--radiance-horizons4'],
    'radiance-horizons8': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--radiance-horizons8'],
    'texture-transform': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--texture-transform'],
    'scene-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene-prepared'],
    'scene-hierarchy': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene-hierarchy'],
    'scene-snapshot-read': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene-snapshot-read'],
    'native-catalog': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--native-catalog'],
    'native-values': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--native-values'],
    'text-format': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--text-format'],
    'box3d': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--box3d'],
    'sky-files': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--sky-files'],
    'shader-bake': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--shader-bake'],
    'terrain-seams': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--terrain-seams'],
    'material-surface-mesh': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-surface-mesh'],
    'scene3d-camera': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene3d-camera'],
    'scene3d-volume': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene3d-volume'],
    'scene3d-terrain': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene3d-terrain'],
    'scene3d-terrain-gpu': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-terrain-gpu'],
    'scene3d-volume-gpu': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-volume-gpu'],
    'hrc-spot': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--hrc-spot'],
    'terrain-checkpoint': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--terrain-checkpoint'],
    'terrain-storage': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--terrain-storage'],
    'game-equivalence': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--game-equivalence',
        str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host')),
        str(ROOT / os.environ.get('CHIBI_REFERENCE_COMPILER', 'build/sdk/chibi-compile')),
        str(ROOT / os.environ.get('CHIBI_CANDIDATE_COMPILER', 'build/sdk/chibi-compile'))],
    'game-equivalence-stage-one': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--game-equivalence-stage-one',
        str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host')),
        str(ROOT / os.environ.get('CHIBI_REFERENCE_COMPILER', 'build/sdk/chibi-compile')),
        str(ROOT / os.environ.get('CHIBI_RELEASE_COMPILER_HOST', 'build-release/sdk/chibi-compiler-host'))],
    'game-equivalence-empty': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--game-equivalence-empty',
        str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host')),
        str(ROOT / os.environ.get('CHIBI_REFERENCE_COMPILER', 'build/sdk/chibi-compile')),
        str(ROOT / os.environ.get('CHIBI_RELEASE_COMPILER_HOST', 'build-release/sdk/chibi-compiler-host'))],
    'terrain-playable-render': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--terrain-playable-render',
        str(ROOT / os.environ.get('CHIBI_TERRAIN_PLAYER', 'dist/sdk/bin/chibi-player')),
        os.environ.get('CHIBI_TERRAIN_ARTIFACT', ''), os.environ.get('CHIBI_TERRAIN_RESOURCES', ''),
        str(ROOT / os.environ.get('CHIBI_TERRAIN_SHADERS', 'dist/sdk/share/chibi/shaders')),
        str(ROOT / os.environ.get('CHIBI_TERRAIN_RENDER_OUTPUT', '.chibi/environment-authoring/terrain-playable-render'))],
    'native-runtime': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--native'],
    'physics': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--physics'],
    'physics3d': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--physics3d'],
    'transforms': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--transforms'],
    'ui-authoring': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--ui-authoring'],
    'prototype-document': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--prototype-document'],
    'field-runtime': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--field-runtime'],
    'language-navigation': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--navigation'],
    'language-publication': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--publication'],
    'runtime-scene': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene'],
    'asset-values': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--asset-values'],
    'ui-document': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--ui-document'],
    'text-files': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--text-files'],
    'fmt': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--fmt'],
    'particles': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--particles'],
    'png-encoding': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--png-encoding'],
    'project-document': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--project-document'],
    'prototype-documents': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--prototype-documents'],
    'language-test-parallel': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--test-parallel'],
    'language-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                          '--prepared',
                          str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language')],
    'language-build-workflow': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                '--build-workflow'],
    'language-headless': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                         '--headless'],
    'script-benchmark': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                         '--script-benchmark'],
    'scheduler-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scheduler-benchmark'],
    'language-query-program': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--query-program'],
    'language-query-values': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--query-values'],
    'language-operations': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--operations'],
    'language-incremental': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--incremental'],
    'language-modules': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--modules'],
    'language-native-calls': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--native-calls'],
    'language-ranges': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--ranges'],
    'language-numeric-exact-core': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                    '--numeric-exact-core'],
    'language-integer-shift-counts': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                   '--integer-shift-counts'],
    'language-integer-shift-counts-authored': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                            '--integer-shift-counts-authored'],
    'language-integer-semantics': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                   '--integer-semantics'],
    'language-integer-arithmetic': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                    '--integer-arithmetic'],
    'language-package-graph': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                               '--package-graph'],
    'language-package-syntax': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                '--package-syntax'],
    'language-package-execution': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                   '--package-execution'],
    'language-asset-compiler-host': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--asset-compiler-host'],
    'language-asset-packaging-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                       '--asset-packaging-chibi'],
    'language-shader-source-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                     '--shader-source-chibi'],
    'language-package-graph-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                     '--package-graph-chibi'],
    'language-syntax-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                              '--syntax-chibi'],
    'language-source-tokenizer-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                        '--source-tokenizer-chibi'],
    'language-source-parser-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                     '--source-parser-chibi'],
    'language-package-syntax-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--package-syntax-chibi'],
    'language-package-paths-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                     '--package-paths-chibi'],
    'language-package-routes-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--package-routes-chibi'],
    'language-package-snapshot-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                        '--package-snapshot-chibi'],
    'language-scalar-lowering-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--scalar-lowering-chibi'],
    'language-compiler-fixture': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                  '--compiler-fixture'],
    'language-compositional-records': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--compositional-records'],
    'language-bounded-compiler-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                       '--bounded-compiler-chibi'],
    'language-prepared-writer-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--prepared-writer-chibi'],
    'language-package-compiler-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                       '--package-compiler-chibi'],
    'language-package-compiler-chibi-compiled': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                '--package-compiler-chibi-compiled'],
    'language-package-compiler-chibi-types': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                             '--package-compiler-chibi-types'],
    'language-package-compiler-chibi-fallible': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                '--package-compiler-chibi-fallible'],
    'language-package-compiler-chibi-types-compiled': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                      '--package-compiler-chibi-types-compiled'],
    'language-package-compiler-chibi-collections': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                   '--package-compiler-chibi-collections'],
    'language-package-compiler-chibi-collections-compiled': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                            '--package-compiler-chibi-collections-compiled'],
    'language-package-compiler-chibi-literals': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                '--package-compiler-chibi-literals'],
    'language-package-compiler-chibi-behaviors': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                 '--package-compiler-chibi-behaviors'],
    'language-package-compiler-chibi-actions': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                               '--package-compiler-chibi-actions'],
    'language-package-compiler-chibi-behaviors-rest': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                      '--package-compiler-chibi-behaviors-rest'],
    'language-package-compiler-chibi-queries': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                             '--package-compiler-chibi-queries'],
    'language-package-compiler-chibi-libraries': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                '--package-compiler-chibi-libraries'],
    'language-package-compiler-chibi-sharing': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                               '--package-compiler-chibi-sharing'],
    'language-package-compiler-chibi-debug-free': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                  '--package-compiler-chibi-debug-free'],
    'language-package-compiler-chibi-native-arguments': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                        '--package-compiler-chibi-native-arguments'],
    'language-package-compiler-chibi-native-arguments-oracle': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                                               '--package-compiler-chibi-native-arguments-oracle'],
    'language-package-compiler-chibi-scalar': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                              '--package-compiler-chibi-scalar'],
    # Needs Linux x86-64 and CHIBI_LLVM_BIN, the directory of opt, llc, ld.lld
    # and llvm-nm.
    'language-scalar-native': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                               '--package-compiler-chibi-native'],
    # W21's census summary; `make native-census` also writes its report.
    'language-native-census': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                               '--native-census'],
    'language-native-census-flattening': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                          '--native-census-flattening'],
    'language-package-compiler-chibi-imports': [str(ROOT / os.environ.get('CHIBI_RELEASE_LANGUAGE_BUILD_DIR', 'build-release/test/language') / 'test_language'),
                                               '--package-compiler-chibi-imports'],
    'language-package-compiler-chibi-imports-compiled': [str(ROOT / os.environ.get('CHIBI_RELEASE_LANGUAGE_BUILD_DIR', 'build-release/test/language') / 'test_language'),
                                                        '--package-compiler-chibi-imports-compiled'],
    'language-package-compiler-chibi-bootstrap': [str(ROOT / os.environ.get('CHIBI_RELEASE_LANGUAGE_BUILD_DIR', 'build-release/test/language') / 'test_language'),
                                                 '--package-compiler-chibi-bootstrap'],
    'language-type-relations-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                    '--type-relations-chibi'],
    'language-unit-algebra-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                  '--unit-algebra-chibi'],
    'language-signature-reader-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--signature-reader-chibi'],
    'language-snapshot-reader-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                     '--snapshot-reader-chibi'],
    'language-package-resolution': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                    '--package-resolution'],
    'language-package-imports': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                 '--package-imports'],
    'language-host-packages': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                               '--host-packages'],
    'language-host-packages-compiler': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                        '--host-packages-compiler'],
    'language-package-files': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                               '--package-files'],
    'language-package-core': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                              '--package-core'],
    'language-package-portable-compiler': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                           '--package-portable-compiler'],
    'language-package-portable': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                  '--package-portable'],
    'language-package-math': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--package-math'],
    'language-package-math-benchmark': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--package-math-benchmark'],
    'language-package-tests': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                               '--package-tests'],
    'language-argument-effects-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                       '--argument-effects-chibi'],
    'language-component-write-layout': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--component-write-layout'],
    'language-component-write': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--component-write'],
    'language-runtime-attribution': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--runtime-attribution'],
    'language-record-compatibility': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-compatibility'],
    'language-record-document': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-document'],
    'language-record-services': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-services'],
    'language-inferred-string-arrays': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--inferred-string-arrays'],
    'language-inferred-string-array-repro': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--inferred-string-array-repro'],
    'language-record-values': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-values'],
    'language-record-prepared': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-prepared'],
    'language-outliner-projection': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--outliner-projection'],
    'language-row-array': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--row-array'],
    'language-scalarize': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--scalarize'],
    'language-native-char-results': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                    '--native-char-results'],
    'language-program-session': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--program-session'],
    'language-program': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                        '--program'],
    'language-wasm': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--wasm'],
    'language-execution': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                           '--execution'],
    'language': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                 '--syntax', '--corpus', str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'corpus.txt')],
    'wasm-browser': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--wasm-browser'],
    'web-storage': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--web-storage'],
    'webgpu-staging': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--webgpu-staging'],
    'native-tools': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools')],
    'reflect-ranges': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--reflect-ranges'],
    'record-installed-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                                  '--record-prepared',
                                  str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                  str(ROOT / os.environ.get('CHIBI_RECORD_RUNTIME_BUILD_DIR', 'build/test/record-runtime') / 'test_tools')],
    'record-c-export': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--record-c-export'],
    'c-export-alias': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--c-export-alias'],

}
for selector in ('activation-benchmark', 'binding-benchmark', 'borrow-benchmark', 'incremental-benchmark', 'runtime-attribution-benchmark', 'query-binding-benchmark', 'scalar-read-benchmark', 'packed-benchmark'):
    TOOLS['language-' + selector] = [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--' + selector]

for selector in ('binding-schedule', 'borrow', 'activation-cache', 'reductions'):
    TOOLS['language-' + selector] = [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--' + selector]

wasm_build = ROOT / os.environ.get('CHIBI_WASM_BUILD_DIR', 'build-wasm-language')
for selector in ('wasm', 'execution', 'program', 'headless', 'format', 'publication', 'script-benchmark', 'query-program', 'ranges', 'component-write', 'scalarize', 'activation-benchmark', 'binding-benchmark', 'borrow-benchmark', 'incremental-benchmark', 'binding-schedule', 'borrow', 'activation-cache', 'reductions', 'operations', 'modules', 'runtime-attribution', 'runtime-attribution-benchmark', 'query-binding-benchmark', 'scalar-read-benchmark', 'packed-benchmark', 'record-values', 'record-semantic', 'record-runtime', 'record-prepared', 'record-document', 'record-state', 'record-compatibility', 'row-array', 'integer-arithmetic', 'outliner-projection', 'package-math'):
    TOOLS['wasm-' + selector] = ['node', str(wasm_build / 'test_language.js'), '--' + selector]
for selector in ('', '-compiled', '-types', '-types-compiled', '-collections', '-collections-compiled',
                 '-imports', '-imports-compiled', '-behaviors',
                 '-behaviors-rest', '-sharing', '-debug-free', '-literals'):
    TOOLS['wasm-package-compiler-chibi' + selector] = ['node', str(wasm_build / 'test_language.js'),
                                                       '--package-compiler-chibi' + selector]

for selector in ('package-portable', 'package-portable-compiler'):
    TOOLS['wasm-' + selector] = ['node', str(wasm_build / 'test_language.js'), '--' + selector]


GPU_TOOLS = (
    'studio-source-edit',
    'studio-graph-visibility',
    'studio-graph-pointer',
    'studio-asset-pointer',
    'studio-debugger-source-history',
    'studio-scene-move',
    'studio-outliner-move',
    'studio-scene-camera',
    'studio-scene-gizmo3d',
    'studio-scene-collider3d',
    'interior-border',
    'viewport-resize',
    'sdf-visibility',
    'radiance',
    'radiance-depth',
    'material-sampling',
    'scene2d',
    'material-terrain-seams',
    
    
    'scene3d-terrain-gpu',
    'scene3d-volume-gpu',
    'hrc-spot',
    
    'scene3d-gles',
    'scene3d-gi',
    'scene3d-gi-gles',
    'material-ramps',
    'radiance-surface',
    'radiance-horizons4',
    'radiance-horizons8',
    
    
    
    
    
    
    'scene3d-post',
    'scene3d-shapes',
    'texture-transform',
)

# Native source generation is currently provided only on these host platforms.
# Explicit tool requests still fail with the component's unavailable diagnostic.
ASSET_COMPILER_HOST_PLATFORMS = ('linux', 'darwin')

# Correctness aggregates never run timing/throughput benchmark selectors.
# Keep performance workloads opt-in through the benchmarks group or tool.
GROUPS = {
    'wasm': ['wasm-wasm', 'wasm-execution', 'wasm-program', 'wasm-headless',
             'wasm-publication', 'wasm-operations', 'wasm-modules', 'wasm-runtime-attribution', 'wasm-ranges', 'wasm-scalarize', 'wasm-component-write',
             'wasm-binding-schedule', 'wasm-borrow', 'wasm-activation-cache', 'wasm-reductions', 'wasm-row-array', 'wasm-integer-arithmetic', 'wasm-outliner-projection',
             'wasm-package-math', 'wasm-package-compiler-chibi', 'wasm-package-compiler-chibi-compiled',
             'wasm-package-portable', 'wasm-package-portable-compiler'],
    'native': ['native-runtime', 'native-catalog', 'native-values', 'audio-analysis', 'shader-webgpu',
               'player-url', 'player-clock', 'build-graph-link-response',
               'physics', 'physics3d',
               'transforms', 'box3d', 'sky-files', 'tilemap', 'runtime-scene', 'scene-prepared',
               'scene-hierarchy', 'render-resources', 'persistent-store', 'whole-alias',
               'whole-integer', 'whole-conversion', 'whole-select', 'whole-math', 'owned-tick',
               'parallel-tick', 'source-native-features', 'source-native-numeric-queries',
               'source-native-material', 
               'source-systems', 'source-reductions', 
               'shader-parameters-prepared', 'language-prepared',
               
               'vector-nodes', 'asset-values', 'ui-document', 'text-files', 'fmt', 'particles',
               'png-encoding', 'project-document', 'prototype-documents', 
               
               'render-geometry', 'arena', 'shader-bake', 'shader-source-debug', 'terrain-seams', 'terrain-storage',
               'material-surface-mesh', 'scene3d-camera', 'scene3d-volume', 'scene3d-terrain'],
    # Imported assets, Terrain Lab's authored save/load restart, the published
    # documentation site and the Release launcher's commands.
    'projects': ['terrain-checkpoint',
                 'chibi-cli', 'native-build'],
    # Independently reviewed fixed CPU workloads: cave<=2048x768, flow<=192x192,
    # two192-particle3000-stepMPM pools, four workers, no windows/children/network.
    'materials': ['material-destruction', 'material-fluids', 'material-mpm', 
                  'material-simulation', 'material-ecs', 'material-render'],
    'language': ['language-test-parallel', 'execution-scope', 'language', 'language-wasm', 'language-execution', 'language-program', 'language-query-program', 'language-query-values', 'language-incremental', 'language-operations', 'language-ranges', 'language-scalarize', 'language-component-write',
                 'language-binding-schedule', 'language-borrow', 'language-activation-cache', 'language-reductions',
                 'language-headless', 'language-package-math', 'language-package-tests', 'language-argument-effects-chibi', 'language-package-core', 'language-package-portable', 'language-package-portable-compiler', 'language-host-packages', 'language-host-packages-compiler', 'language-navigation', 'language-publication', 'language-modules', 'language-native-calls', 'language-runtime-attribution',
                 'field-runtime', 'prototype-document', 'ui-authoring', 'text-format', 
                 'language-row-array', 'language-outliner-projection',
                 'language-shader-source-chibi', 'language-compiler-fixture', 'language-compositional-records', 'language-package-compiler-chibi-scalar',
                 'language-package-compiler-chibi-fallible',
                 'language-package-compiler-chibi-behaviors', 'language-package-compiler-chibi-behaviors-rest',
                 'language-integer-semantics', 'language-integer-arithmetic', 'language-integer-shift-counts', 'language-integer-shift-counts-authored', 'language-asset-packaging-chibi', 'compiler-bootstrap',
                 'language-native-census-flattening'],
}
if sys.platform in ASSET_COMPILER_HOST_PLATFORMS:
    GROUPS['language'].append('language-asset-compiler-host')

GROUPS['render'] = [name for name in GPU_TOOLS if not name.startswith('studio-')
                    and (not name.endswith('-gles') or sys.platform.startswith('linux'))]
# Native scalar execution runs on Linux x86-64, with the LLVM tools it is
# given, and builds the bundle the other targets run (test/native_aot/targets);
# otherwise the language group reports it unrun.
UNRUN = {}
if not (sys.platform.startswith('linux') and platform.machine() == 'x86_64'):
    UNRUN['language-scalar-native'] = 'it needs Linux x86-64'
elif not os.environ.get('CHIBI_LLVM_BIN'):
    UNRUN['language-scalar-native'] = 'set NATIVE_LLVM_BIN to the directory of opt, llc, ld.lld and llvm-nm'
else:
    GROUPS['language'].append('language-scalar-native')
GROUPS['compiler'] = [name for name in TOOLS if name.startswith('language-package-compiler-chibi')]
GROUPS['benchmarks'] = [name for name in TOOLS if name.endswith('-benchmark') and not name.startswith('wasm-')]


# Only source-audited independent selectors may overlap. Missing entries are
# exclusive barriers, not skipped coverage. See docs/test-parallel-resources.md.
# Native C and authored test pools own their in-process budget: keep their
# selectors (including compiler-fixture and package-compiler-chibi-scalar)
# exclusive to avoid multiplying outer and inner worker budgets.
PARALLEL_RESOURCES = {
    name: frozenset() for name in (
        'language-package-compiler-chibi-behaviors',
        'language-package-compiler-chibi-behaviors-rest',
        'language-integer-semantics',
        'language-integer-shift-counts',
        'language-integer-shift-counts-authored',
        'language-asset-packaging-chibi',
        'language-asset-compiler-host',
        'compiler-bootstrap',
        'language-scalar-native',
    )
}


def language_shards(names, count=3):
    """Balance isolated serial runners; weights never decide coverage.

    Lifetimes rounded from Linux run 40423984810, not cross-platform promises.
    Keep each lane in group order. Unmeasured/new selectors get weight one and
    remain covered; the complete one-lane route is the rollback path.
    """
    if count not in (1, 2, 3):
        raise ValueError('language shard count must be 1, 2 or 3')
    if len(set(names)) != len(names):
        raise ValueError('duplicate language selector')
    weights = {
        'language-asset-compiler-host': 309,
        'language-scalar-native': 278,
        'language-package-compiler-chibi-scalar': 235,
        'language-package-compiler-chibi-behaviors-rest': 229,
        'compiler-bootstrap': 177,
        'language-package-compiler-chibi-behaviors': 116,
        'language-integer-shift-counts-authored': 69,
        'language-asset-packaging-chibi': 49,
    }
    loads, assignment = [0] * count, {}
    for index in sorted(range(len(names)), key=lambda i: (-weights.get(names[i], 1), i)):
        name = names[index]
        lane = min(range(count), key=lambda lane: (loads[lane], lane))
        assignment[name] = lane
        loads[lane] += weights.get(name, 1)
    return [[name for name in names if assignment[name] == lane] for lane in range(count)]


def language_manifest(count=3):
    return dict(version=1, platform=sys.platform, machine=platform.machine(),
                selectors=GROUPS['language'], shards=language_shards(GROUPS['language'], count),
                unrun=UNRUN)


def tool_command(name):
    if name in GPU_TOOLS:
        return [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--' + name]
    return TOOLS[name]


def selector_arguments(name):
    if name not in ('TEST_ARGS', 'AGENT_SELECTOR'):
        raise ValueError('unsupported selector environment variable')
    # Tokenize quoted positional arguments without executing shell syntax.
    return shlex.split(os.environ.get(name, ''))


def timeout_deadline(seconds):
    if seconds is None:
        return None
    if not math.isfinite(seconds) or seconds <= 0:
        raise ValueError('timeout must be a finite positive duration')
    return time.monotonic() + seconds


def process_children():
    # Shared portable PID/parent snapshot. Exclude the probe itself: it has
    # already been reaped when we use the snapshot and is not owned test work.
    probe = subprocess.Popen(['ps', '-A', '-o', 'pid=,ppid='],
                             stdout=subprocess.PIPE, text=True)
    try:
        rows, _ = probe.communicate(timeout=PROCESS_INSPECTION_SECONDS)
    finally:
        if probe.poll() is None:
            probe.kill()
        probe.wait()
    if probe.returncode:
        raise RuntimeError('test cleanup process inspection failed')
    children = {}
    own_process_seen = False
    for row in rows.splitlines():
        pid, parent = map(int, row.split())
        own_process_seen |= pid == os.getpid()
        if pid != probe.pid:
            children.setdefault(parent, []).append(pid)
    if not own_process_seen:
        raise RuntimeError('test cleanup cannot inspect its own process')
    return children


def proc_children():
    path = Path(f'/proc/self/task/{os.getpid()}/children')
    return [int(pid) for pid in path.read_text().split()]


def cleanup_reader(subreaper):
    # Verify the selected discovery capability before launching test work.
    if subreaper:
        try:
            proc_children()
            return proc_children
        except (OSError, ValueError):
            pass
    try:
        process_children()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        raise RuntimeError(f'test cleanup unavailable: no usable proc children reader or ps ({error}); no test started') from error
    return lambda: process_children().get(os.getpid(), [])


def cleanup_children(read_children):
    # This dedicated subreaper owns all its children, including double-forked or
    # setsid descendants adopted when their parent/nested supervisor is killed.
    # Kill parents, reap, then repeat for newly adopted children until none remain.
    while True:
        children = read_children()
        for pid in children:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        try:
            pid, _ = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            return
        if not pid:
            time.sleep(.001)


def cleanup_subtree(child):
    # Portable nested cleanup cannot kill the shared ancestor process group.
    # Freeze the live child tree before killing it so parent death cannot hide
    # descendants by reparenting them while we discover the owned subtree.
    if child.poll() is not None:
        return
    owned, pending = [child.pid], [child.pid]
    try:
        while pending:
            for pid in pending:
                try:
                    os.kill(pid, signal.SIGSTOP)
                except ProcessLookupError:
                    pass
            children = process_children()
            pending = []
            frontier = list(owned)
            seen = set(owned)
            for parent in frontier:
                for pid in children.get(parent, []):
                    if pid not in seen:
                        seen.add(pid)
                        frontier.append(pid)
                        pending.append(pid)
            owned.extend(pending)
    finally:
        for pid in reversed(owned):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        child.wait()


def wait_until(child, deadline):
    # Popen.wait adds a KeyboardInterrupt grace period; do not grant test work
    # extra runtime before cleanup on an interrupt.
    while child.poll() is None:
        if deadline is None:
            time.sleep(.005)
            continue
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(child.args, 0)
        time.sleep(min(.005, remaining))
    return child.returncode


def enable_subreaper():
    # Linux additionally owns detached/orphaned descendants. Other supported
    # POSIX hosts retain portable process-group cleanup.
    if not sys.platform.startswith('linux'):
        return False
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0):  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), 'cannot own test descendants')
    return True


def inherited_group(environment):
    return environment.get(PROCESS_GROUP) == str(os.getpgrp())


def supervise(commands):
    subreaper = enable_subreaper()
    read_children = cleanup_reader(subreaper)
    os.environ[PROCESS_GROUP] = str(os.getpgrp())
    child = None
    try:
        for command in commands:
            child = subprocess.Popen(command, cwd=ROOT)
            result = wait_until(child, None)
            if result:
                return result if result > 0 else 128 - result
        return 0
    finally:
        # Cancellation can arrive as cleanup starts. signal.signal runs a
        # pending handler before installing the new one, so retry until
        # discovery and polling are protected.
        while True:
            try:
                signal.signal(signal.SIGTERM, signal.SIG_IGN)
                signal.signal(signal.SIGINT, signal.SIG_IGN)
                break
            except (KeyboardInterrupt, TerminationRequested):
                pass
        if subreaper:
            cleanup_children(read_children)
        elif child is not None:
            cleanup_subtree(child)


def run_commands(commands, seconds=None, env=None, output=None):
    """Run commands in order; an optional timeout covers the entire sequence."""
    deadline = timeout_deadline(seconds)
    environment = dict(os.environ if env is None else env)
    started = time.monotonic()
    owns_group = not inherited_group(environment)
    child = subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
                              '_supervise', json.dumps(commands)], cwd=ROOT,
                             env=environment, start_new_session=owns_group,
                             stdout=output, stderr=output)
    try:
        try:
            result = wait_until(child, deadline)
        except subprocess.TimeoutExpired:
            return 124
        if deadline is not None and time.monotonic() >= deadline:
            return 124
        return result if result >= 0 else 128 - result
    finally:
        if child.poll() is None:
            # Let the supervisor unwind first; Linux also reaps descendants
            # which escaped the portable process group.
            child.terminate()
        child.wait()
        if owns_group:
            # Also used on macOS: nested managed wrappers stay in this group.
            # Arbitrary descendants that call setsid need Linux subreaping.
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        print(f'Test command wall time: {time.monotonic() - started:.3f}s; '
              'includes in-test fixture setup; external configuration/build excluded.', file=output or sys.stdout, flush=True)


def available_test_memory(read_text=None):
    """Available bytes, or None if host/container admission cannot be checked.

    read_text(path) is injectable for deterministic cgroup/proc fixtures.
    """
    read_text = read_text or (lambda path: Path(path).read_text())
    try:
        if sys.platform == 'darwin':
            result = subprocess.run(['vm_stat'], capture_output=True, text=True,
                                    timeout=PROCESS_INSPECTION_SECONDS, check=True)
            lines = result.stdout.splitlines()
            page_size = int(lines[0].split('page size of ', 1)[1].split(' bytes', 1)[0])
            pages = dict(line.split(':', 1) for line in lines[1:] if ':' in line)
            # Free and reclaimable inactive pages only; do not count compressed
            # or wired memory as immediately available admission capacity.
            counts = [int(pages[name].strip().rstrip('.'))
                      for name in ('Pages free', 'Pages inactive', 'Pages speculative')]
            if page_size <= 0 or min(counts) < 0:
                return None
            return page_size * sum(counts)
        if not sys.platform.startswith('linux'):
            return None
        fields = dict(line.split(':', 1) for line in read_text('/proc/meminfo').splitlines())
        amount, unit = fields['MemAvailable'].split()
        available = int(amount) * 1024
        if unit != 'kB' or available < 0:
            return None
        memberships = []
        for line in read_text('/proc/self/cgroup').splitlines():
            hierarchy, controllers, member = line.split(':', 2)
            if hierarchy == '0' and not controllers:
                memberships.append(('cgroup2', member))
            elif 'memory' in controllers.split(','):
                memberships.append(('cgroup', member))
        if not memberships:
            return None
        mounts = []
        for line in read_text('/proc/self/mountinfo').splitlines():
            before, after = line.split(' - ', 1)
            details, kind = before.split(), after.split()
            if kind[0] not in ('cgroup', 'cgroup2'):
                continue
            if kind[0] == 'cgroup' and 'memory' not in kind[2].split(','):
                continue
            # mountinfo escapes path whitespace and backslashes as octal.
            paths = []
            for value in details[3:5]:
                for escaped, decoded in ((r'\040', ' '), (r'\011', '\t'),
                                         (r'\012', '\n'), (r'\134', '\\')):
                    value = value.replace(escaped, decoded)
                paths.append(Path(value))
            mounts.append((kind[0], *paths))
        for kind, member_text in memberships:
            member = Path(member_text)
            if not member.is_absolute() or '..' in member.parts:
                return None
            # A subtree-only mount hides ancestor limits. Without the complete
            # hierarchy, do not mistake visible headroom for actual headroom.
            roots = [mount for mount_kind, root, mount in mounts
                     if mount_kind == kind and root == Path('/')]
            if not roots:
                return None
            mount = roots[0]
            if not mount.is_absolute() or '..' in mount.parts:
                return None
            current = mount / str(member).lstrip('/')
            while True:
                limit_name, used_name = ('memory.max', 'memory.current') if kind == 'cgroup2' else (
                    'memory.limit_in_bytes', 'memory.usage_in_bytes')
                try:
                    limit = read_text(str(current / limit_name)).strip()
                except FileNotFoundError:
                    # The genuine v2 hierarchy root has no memory.max control.
                    if kind != 'cgroup2' or current != mount:
                        raise
                    break
                if limit != 'max':
                    limit_bytes = int(limit)
                    used = int(read_text(str(current / used_name)).strip())
                    if limit_bytes < 0 or used < 0:
                        return None
                    available = min(available, max(0, limit_bytes - used))
                elif kind != 'cgroup2':
                    return None
                if current == mount:
                    break
                current = current.parent
        return available
    except (OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError):
        return None


def diagnostic(event, **fields):
    # Policy metadata never goes into a test program's stdout or environment dump.
    print('Test policy: ' + json.dumps(dict(event=event, **fields), sort_keys=True),
          file=sys.stderr, flush=True)


def automatic_admission():
    # Admission reserve, NOT a measured peak-RSS bound. Compiler children may
    # themselves use threads. Unknown capacity keeps the original serial route.
    admission = dict(mode='automatic', selected_jobs=1, reason='cpu-unavailable',
                     cpu_slots=None, available_bytes=None, reserve_bytes=2 * 1024 ** 3,
                     memory_probe='not-run')
    try:
        cpus = len(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else os.cpu_count() or 1
    except OSError:
        return admission
    available = available_test_memory()
    admission.update(cpu_slots=cpus, available_bytes=available,
                     memory_probe='unknown' if available is None else 'available',
                     reason='memory-unavailable' if available is None else 'cpu-memory-admission')
    if available is not None:
        admission['selected_jobs'] = max(1, min(2, cpus, available // admission['reserve_bytes']))
    return admission


def automatic_jobs():
    return automatic_admission()['selected_jobs']


def run_group(names, jobs=None, seconds=None):
    """Schedule audited selectors; preserve the complete serial route."""
    deadline = timeout_deadline(seconds)
    started = time.monotonic()
    if jobs is not None and jobs < 1:
        raise ValueError('jobs must be a positive integer')
    commands = [tool_command(name) for name in names]
    eligible = any(name in PARALLEL_RESOURCES for name in names)
    if jobs is None and eligible:
        admission = automatic_admission()
    else:
        admission = dict(mode='automatic' if jobs is None else 'explicit',
                         selected_jobs=jobs or 1, reason='explicit', cpu_slots=None,
                         available_bytes=None, reserve_bytes=2 * 1024 ** 3,
                         memory_probe='not-run')
    if not eligible:
        admission.update(selected_jobs=1, reason='no-parallel-eligible-selectors')
    diagnostic('admission', requested_jobs=jobs, selectors=len(names), **admission)
    jobs = admission['selected_jobs']
    remaining = None if deadline is None else deadline - time.monotonic()
    if remaining is not None and remaining <= 0:
        for name in names:
            print(f'[{name}] not run: group deadline expired', flush=True)
            diagnostic('selector', name=name, start=None, observed_finish=None,
                       status='not-started', exit_code=None)
        diagnostic('group', wall_seconds=time.monotonic() - started,
                   peak_live_workers=0, result=124, lifecycle_scope='selector-supervisor')
        return 124
    if jobs == 1:
        # Preserve the original one-supervisor sequence. Do not invent selector
        # intervals or claim a measured peak for this uninstrumented legacy route.
        result = 2
        try:
            result = run_commands(commands, seconds=remaining)
            return result
        except (KeyboardInterrupt, TerminationRequested):
            result = 130
            raise
        finally:
            diagnostic('group', wall_seconds=time.monotonic() - started,
                       peak_live_workers=None, result=result, lifecycle_scope='unobserved-serial')
    active, completed = {}, {}
    next_index = emit_index = 0
    result = peak_live = 0
    records = [dict(name=name, start=None, observed_finish=None,
                    status='not-started', exit_code=None) for name in names]

    def poll_workers():
        nonlocal result
        for index, (child, log, scratch, resources) in list(active.items()):
            status = child.poll()
            if status is None:
                continue
            finished = time.monotonic()
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            status = status if status >= 0 else 128 - status
            records[index].update(observed_finish=finished, exit_code=status,
                                  status='failed' if status else 'passed')
            completed[index] = (status, log)
            if status and not result:
                result = status
            del active[index]

    # Defer signals to scheduler boundaries: raising between Popen and recording
    # the child would lose ownership of a just-created worker.
    cancelled = False

    def request_cancel(_signal, _frame):
        nonlocal cancelled
        cancelled = True

    handlers = {number: signal.getsignal(number)
                for number in (signal.SIGTERM, signal.SIGINT)}
    try:
        for number in handlers:
            signal.signal(number, request_cancel)
        # Scratch and logs belong to this invocation, never the source/build tree.
        with tempfile.TemporaryDirectory(prefix='chibi-tests-') as directory:
            try:
                while next_index < len(names) or active:
                    if cancelled:
                        raise TerminationRequested()
                    if deadline is not None and time.monotonic() >= deadline:
                        result = 124
                        break
                    while next_index < len(names) and len(active) < jobs:
                        if cancelled:
                            raise TerminationRequested()
                        # Refresh exits before each admission, not merely once
                        # per loop, so dead workers cannot inflate the live peak.
                        poll_workers()
                        if result:
                            break
                        resources = PARALLEL_RESOURCES.get(names[next_index])
                        if active and (resources is None or any(
                                item[3] is None or resources & item[3]
                                for item in active.values())):
                            break
                        scratch = Path(directory) / str(next_index)
                        scratch.mkdir()
                        environment = dict(os.environ, TMPDIR=str(scratch),
                                           TMP=str(scratch), TEMP=str(scratch))
                        # A worker always owns its group, even inside a managed
                        # caller. Its supervisor alone adopts/reaps its orphans.
                        environment.pop(PROCESS_GROUP, None)
                        log = open(Path(directory) / f'{next_index}.log', 'w+', errors='replace')
                        launched = time.monotonic()
                        try:
                            child = subprocess.Popen(
                                [sys.executable, str(Path(__file__).resolve()),
                                 '_supervise', json.dumps([commands[next_index]])],
                                cwd=ROOT, env=environment, start_new_session=True,
                                stdout=log, stderr=log)
                        except BaseException:
                            log.close()
                            raise
                        active[next_index] = (child, log, scratch, resources)
                        records[next_index].update(start=launched, status='running')
                        poll_workers()
                        peak_live = max(peak_live, len(active))
                        next_index += 1
                        if resources is None:
                            break
                    poll_workers()
                    while emit_index in completed:
                        status, log = completed.pop(emit_index)
                        print(f'[{names[emit_index]}] exit {status}', flush=True)
                        log.seek(0)
                        shutil.copyfileobj(log, sys.stdout)
                        log.close()
                        sys.stdout.flush()
                        if status and not result:
                            result = status
                        emit_index += 1
                    if result:
                        break
                    if active:
                        time.sleep(.005)
            except BaseException:
                if not result:
                    result = 130 if cancelled else 2
                raise
            finally:
                # Send cancellation to every owned supervisor before waiting. Ignore
                # repeated interrupts only while all workers finish their cleanup.
                for number in (signal.SIGTERM, signal.SIGINT):
                    signal.signal(number, signal.SIG_IGN)
                try:
                    cancelled_workers = set()
                    for index, (child, log, scratch, resources) in active.items():
                        if child.poll() is None:
                            cancelled_workers.add(index)
                            child.terminate()
                    for index, (child, log, scratch, resources) in active.items():
                        child.wait()
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        status = child.returncode
                        status = status if status >= 0 else 128 - status
                        records[index].update(observed_finish=time.monotonic(), exit_code=status,
                                              status='cancelled' if index in cancelled_workers
                                              else 'failed' if status else 'passed')
                        completed[index] = (status, log)
                    # Preserve partial worker diagnostics on cancellation/timeout.
                    for index in sorted(completed):
                        status, log = completed[index]
                        state = 'cancelled; exit' if index in cancelled_workers else 'exit'
                        print(f'[{names[index]}] {state} {status}', flush=True)
                        log.seek(0)
                        shutil.copyfileobj(log, sys.stdout)
                        log.close()
                    for name in names[next_index:]:
                        print(f'[{name}] not run: group stopped', flush=True)
                finally:
                    for record in records:
                        diagnostic('selector', **record)
                    diagnostic('group', wall_seconds=time.monotonic() - started,
                               peak_live_workers=peak_live, result=130 if cancelled else result,
                               lifecycle_scope='selector-supervisor')
                    print(f'Test command wall time: {time.monotonic() - started:.3f}s; '
                          'includes in-test fixture setup; external configuration/build excluded.', flush=True)
    finally:
        for number, handler in handlers.items():
            signal.signal(number, handler)
    if cancelled:
        raise TerminationRequested()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, help='Optional timeout in seconds for this command')
    parser.add_argument('--jobs', type=int, default=os.environ.get('CHIBI_TEST_JOBS'),
                        help='Group workers (automatic by default; 1 keeps serial execution)')
    parser.add_argument('--shard', help='Isolated language runner INDEX/COUNT (1-based; serial only)')
    parser.add_argument('action', choices=['inventory', 'unit', 'short', 'tool', 'group',
                                         'native', 'native-env', 'agent-env', '_supervise',
                                         'shard-manifest'])
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    arguments = args.arguments
    if arguments[:1] == ['--']:
        arguments = arguments[1:]
    try:
        timeout_deadline(args.timeout)
        if args.jobs is not None and args.jobs < 1:
            raise ValueError('jobs must be a positive integer')
    except ValueError as error:
        parser.error(str(error))
    shard_names = None
    if args.shard is not None:
        try:
            index, count = map(int, args.shard.split('/'))
            if args.action != 'group' or arguments != ['language']:
                raise ValueError('shards require group language')
            if args.jobs not in (None, 1):
                raise ValueError('isolated language shards require --jobs 1')
            shards = language_shards(GROUPS['language'], count)
            if not 1 <= index <= count:
                raise ValueError('language shard index out of range')
            shard_names = shards[index - 1]
        except ValueError as error:
            parser.error(str(error))
    if args.action == 'shard-manifest':
        if arguments:
            parser.error('shard-manifest takes no arguments')
        print(json.dumps(language_manifest(), sort_keys=True))
        return 0
    if args.action == '_supervise':
        return supervise(json.loads(arguments[0]))
    if args.action == 'inventory':
        for name in TOOLS:
            print('python3 tools/test_policy.py tool', name)
        for name in GROUPS:
            print(f'python3 tools/test_policy.py group {name}')
        print('make test / test-short: C execution-policy units')
        print('make test-tools: standalone C tooling and process integrations')
        print('See docs/test-inventory.md for coverage and build requirements.')
        return 0
    if args.action in ('unit', 'short'):
        if len(arguments) != 1:
            parser.error('unit/short requires the native C tooling test binary')
        command = [str(Path(arguments[0]).resolve())]
        if args.action == 'unit':
            command.append('--unit')
        return run_commands([command], seconds=args.timeout)
    if args.action == 'group':
        if len(arguments) != 1 or arguments[0] not in GROUPS:
            parser.error('unknown test group')
        if arguments[0] == 'language':
            for name, reason in UNRUN.items():
                print(f'{name} not run: {reason}')
        if shard_names is not None:
            diagnostic('shard', index=index, count=count, selectors=shard_names)
            return run_group(shard_names, jobs=1, seconds=args.timeout)
        return run_group(GROUPS[arguments[0]], jobs=args.jobs, seconds=args.timeout)
    if args.action == 'tool':
        if len(arguments) != 1 or arguments[0] not in TOOLS:
            parser.error('unknown tooling command')
        return run_commands([tool_command(arguments[0])], seconds=args.timeout)
    if args.action in ('native-env', 'agent-env'):
        if len(arguments) != 2:
            parser.error('invalid environment-selector invocation')
        name = 'AGENT_SELECTOR' if args.action == 'agent-env' else arguments[0]
        selectors = selector_arguments(name)
        if args.action == 'agent-env':
            if arguments[1] not in ('test', 'gpu-test'):
                parser.error('invalid agent test mode')
            return run_commands([[sys.executable, str(ROOT / 'tools/agent.py'),
                                  '--build-dir', arguments[0], arguments[1], '--', *selectors]],
                                seconds=args.timeout)
        return run_commands([[arguments[1], *selectors]], seconds=args.timeout)
    if not arguments:
        parser.error('native requires a test binary and optional arguments')
    return run_commands([arguments], seconds=args.timeout)


class TerminationRequested(BaseException):
    pass


def terminate(_signal, _frame):
    raise TerminationRequested()


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGINT, terminate)
    try:
        sys.exit(main())
    except (KeyboardInterrupt, TerminationRequested):
        sys.exit(130)
    except (OSError, ValueError, RuntimeError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(2)

