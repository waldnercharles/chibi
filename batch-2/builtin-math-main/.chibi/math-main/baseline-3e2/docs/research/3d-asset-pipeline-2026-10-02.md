# 3D asset pipeline research, 2026-10-02

Scope: a Mac authoring/review client and a Linux generation workstation with an RTX 4080 Super, 16 GB VRAM, and 32 GB RAM. This is source research, not a tested installation or a measured model comparison. Recommendations below are engineering judgments; reported requirements and capabilities come from the linked maintainers.

## Recommendation

Build around Blender and image-to-3D generation. Use the Linux PC for GPU jobs and the Mac for reference preparation, review and editing. Start by comparing TRELLIS.2 through `trellis.cpp` against Pixal3D through the current ComfyUI workflow. Keep Hunyuan3D 2.0 as a documented 16 GB baseline and Stable Fast 3D as a lighter iteration option. A single optional cloud provider is enough for comparison and assets that take too much local cleanup.

This choice balances installability, available material output and the memory limit. It is not a finding that one model produces the best art. That requires the same inputs and actual Blender cleanup on this hardware. The 32 GB system RAM also needs measurement: CPU offload and mesh postprocessing consume host memory, so fitting weights in VRAM alone is insufficient.

For repeated mechanical parts, ships, architecture and exact dimensions, keep a second creation route using Blender modeling, modifiers, Geometry Nodes and Python. My recommendation is to use image generation for concepts and organic shapes, and procedural construction when separate editable parts and precise constraints matter. These routes can share the same baking, export and validation steps. [Blender Geometry Nodes](https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/introduction.html), [Blender Python API](https://docs.blender.org/api/current/info_quickstart.html)

## Local generators

| Candidate | Published hardware evidence | Output and role | Recommendation for this PC |
| --- | --- | --- | --- |
| TRELLIS.2 through `trellis.cpp` | Community author documents a 1024 cascade on a 16 GB card; CUDA measurements use a 5060 Ti 16 GB. | Textured GLB with PBR, native CLI/server and Linux binaries. | First installation pilot. These are maintainer results, not a 4080 Super validation. [Release v0.8.1](https://github.com/pwilkin/trellis.cpp/releases/tag/v0.8.1), [README](https://github.com/pwilkin/trellis.cpp/blob/v0.8.1/README.md) |
| Pixal3D | Official low-VRAM mode loads stages on demand and defaults to 1024 resolution; no numeric 16 GB guarantee found. | PBR asset generation aligned to the input view; upstream added multiview inference in September 2026. | Strong quality comparison candidate through ComfyUI; validate memory before making it the default. [Official source](https://github.com/TencentARC/Pixal3D), [ComfyUI workflow](https://docs.comfy.org/tutorials/3d/pixal3d) |
| Hunyuan3D 2.0 and mini | Upstream states 6 GB for shape and 16 GB for shape plus texture. Mini is a smaller 0.6B shape model. | Separate geometry and painting stages, multiview shape generation and texturing existing meshes. | Established baseline, though the complete published requirement leaves little GPU headroom. Use stage unloading/offload. [Official source](https://github.com/Tencent-Hunyuan/Hunyuan3D-2) |
| Hunyuan3D 2.1 | Upstream states 10 GB shape, 21 GB painting and 29 GB combined. | Geometry plus explicit PBR texture generation. | Shape is plausible locally; the official painting configuration exceeds 16 GB. [Official source](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1) |
| Stable Fast 3D | About 6 GB VRAM according to upstream. | Single-image GLB, UV unwrap, illumination disentanglement and predicted material parameters. | Lighter baseline for iteration. [Official source](https://github.com/Stability-AI/stable-fast-3d) |
| SPAR3D | Its specific memory section states about 10.5 GB default or 7 GB with low-VRAM mode; a later generic 6 GB sentence conflicts. | Editable intermediate point cloud, textured GLB and optional remeshing. | Useful if correcting inferred geometry through points helps the workflow. Use the more conservative documented memory figures. [Official source](https://github.com/Stability-AI/stable-point-aware-3d) |

Microsoft's reference TRELLIS.2 setup still requires Linux and at least 24 GB of NVIDIA VRAM. Its H100 timings are not estimates for this PC. ComfyUI and `trellis.cpp` are different integrations with different memory behavior. TRELLIS v1 documents a 16 GB minimum and exports textured meshes, but I would first evaluate the newer low-memory paths before maintaining another environment. [TRELLIS.2 requirements](https://github.com/microsoft/TRELLIS.2#-installation), [TRELLIS v1 requirements](https://github.com/microsoft/TRELLIS#-installation)

The Hunyuan3D 2.1 Gradio app's `--low_vram_mode` is not proof that painting fits: its model-offload call is commented out. Version 2.0 actually invokes texture model CPU offload. ComfyUI's Hunyuan tutorial also quotes 12 GB for the complete process, conflicting with Tencent's 16 GB README; this report uses the model owner's figure. [2.1 app source](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/gradio_app.py), [2.0 app source](https://github.com/Tencent-Hunyuan/Hunyuan3D-2/blob/main/gradio_app.py), [ComfyUI Hunyuan documentation](https://docs.comfy.org/tutorials/3d/hunyuan3D-2)

`trellis.cpp` is promising because it reduces installation work, but retain these boundaries:

- Its published 5060 Ti results include examples taking 3:16 and 7:23 at 1024 resolution, showing substantial input dependence. No time estimate for the 4080 Super is established. Its profiling notes also report host-memory exhaustion when successive reference jobs retain data in a roughly 32 GB machine. Initially run one job per process. [Maintainer measurements](https://github.com/pwilkin/trellis.cpp/blob/v0.8.1/docs/spec/29-perf-profile.md)
- The installer lists roughly 16.5 GB of full-precision weights, 9.5 GB Q8 and 6 GB Q4 downloads. Download size is not peak runtime VRAM. Start with a 512-resolution smoke run and evaluate 1024 after measuring memory. [Installation guide](https://github.com/pwilkin/trellis.cpp/blob/v0.8.1/docs/getting-started.md)
- The Pixal3D port has material limitations: automatic camera/FOV estimation is absent, full numerical parity has not been verified, and 512 mode has no matching texture model. Prefer the upstream Python implementation or documented ComfyUI workflow when comparing Pixal3D quality. [Port limitations](https://github.com/pwilkin/trellis.cpp/blob/v0.8.1/docs/pixal3d/README.md)

Other candidates have narrower roles. TripoSR is a simple MIT-licensed, roughly 6 GB image-to-mesh baseline. Hunyuan3D-Omni adds point, voxel, box and pose constraints with a documented 10 GB geometry requirement. HY3D-Bench releases 0.8B Small geometry weights without an inference VRAM figure. Hunyuan3D-Buffalo's project page still labels code as forthcoming. None replaces the complete first pilot above. [TripoSR](https://github.com/VAST-AI-Research/TripoSR), [Omni](https://github.com/Tencent-Hunyuan/Hunyuan3D-Omni), [HY3D-Bench](https://github.com/Tencent-Hunyuan/HY3D-Bench), [Buffalo](https://tencent-hunyuan.github.io/Hunyuan3D-Buffalo1.0/)

## Cloud comparison

These are optional hosted services, with installable API clients rather than downloadable local model runtimes. Compare the cost of an accepted asset including rerolls and cleanup. Credits have different values across providers, and web subscriptions can be separate from API billing.

| Service | Why evaluate it | Current API/pricing facts |
| --- | --- | --- |
| Meshy 7.1 / T2 | Broad generation and processing API. T2 generates separated parts with a directly specified 100–15,000 triangle target. Meshy also offers remesh, retexture, UV, rigging and animation endpoints. | Standard 7.1 textured image generation costs 30 credits at 2K/4K or 35 at 8K; T2 costs 15/20 respectively. Enable PBR explicitly. Current 7.1 does not generate an emission map. [Image API](https://docs.meshy.ai/en/api/image-to-3d), [API pricing](https://docs.meshy.ai/en/api/pricing) |
| Tripo H3.1 | Text/image/multiview, low-poly and quad options, plus part segmentation, retopology and rigging. | $0.01 per API credit; standard textured image/multiview generation is $0.30 before extras. HD textures, detailed geometry, quad conversion and rigging add cost. [H3.1 API](https://developers.tripo3d.ai/en/models/v3-1), [Pricing](https://developers.tripo3d.ai/en/pricing) |
| Rodin Gen-2.5 | Explicit mesh and material controls, multiple input images, quad/triangle output and detail baking. | Base generation is 0.5 vendor credits. Business currently lists API access at $120 monthly or $96 monthly billed annually, so it has a higher entry cost for automation. [API](https://docs.hyper3d.ai/en/api-specification/rodin-gen2-5), [Plans](https://hyper3d.ai/pricing?lang=en) |

My first cloud comparison would be Meshy T2 for low-poly game assets or Tripo H3.1 for a simple metered API. Rodin is another quality candidate when its API plan is justified. No paid generations or direct output comparisons were performed for this report.

## Licenses and asset rights

TRELLIS.2's model card and `trellis.cpp` use MIT; Pixal3D also publishes code and weights under MIT. Their dependencies retain separate terms. Tencent's Hunyuan3D 2/2.1 licenses are custom community licenses with geographic and usage restrictions, so they should not be grouped with MIT models. Stability's SF3D/SPAR3D community license requires registration for commercial use below its $1 million annual organizational revenue threshold and a separate agreement above it. These are source summaries, not a blanket clearance of generated artwork. [TRELLIS.2 model card](https://huggingface.co/microsoft/TRELLIS.2-4B#license), [trellis.cpp license](https://github.com/pwilkin/trellis.cpp/blob/v0.8.1/LICENSE), [Pixal3D notice](https://github.com/TencentARC/Pixal3D/blob/main/NOTICE), [Hunyuan license](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/LICENSE), [Stability license](https://github.com/Stability-AI/stable-point-aware-3d/blob/main/LICENSE.md)

Meshy's free outputs use CC BY 4.0 with attribution; its paid plans offer different ownership/privacy terms. Tripo's Studio Free outputs are public and noncommercial. Do not assume a free web account grants the same rights as a paid API account. Keep input provenance and the applicable terms with the asset record. [Meshy plans and ownership FAQ](https://www.meshy.ai/pricing), [Tripo commercial use](https://www.tripo3d.ai/help/privacy-policy/how-to-use-tripo-models-commercially)

## Working from the Mac

The durable arrangement is local Blender plus a browser or CLI client for the Linux worker. Mac-local generation is also possible in some implementations: `trellis.cpp` v0.8.1 reports a Metal source build tested on an M5 with 24 GB unified memory, and SF3D/SPAR3D describe experimental MPS support. These reports do not establish compatibility with an unspecified Mac. Hunyuan's macOS claim also should not imply full local painting, because its texture rasterizer builds CUDA code. [trellis.cpp Mac report](https://github.com/pwilkin/trellis.cpp/blob/v0.8.1/README.md#performance), [SF3D](https://github.com/Stability-AI/stable-fast-3d), [SPAR3D](https://github.com/Stability-AI/stable-point-aware-3d), [Hunyuan rasterizer](https://github.com/Tencent-Hunyuan/Hunyuan3D-2/blob/main/hy3dgen/texgen/custom_rasterizer/setup.py)

## Recommended installation architecture

Start with Blender plus a pinned native `trellis.cpp` backend as a pilot. The community port supplies Linux CUDA release binaries, a CLI, an HTTP server, and a browser interface; it exports UV-textured GLB with PBR materials without a Python runtime. It is a separate implementation of Microsoft's TRELLIS.2, so its memory and quality claims need testing independently. Its README also says the prebuilt binaries omit its optional quad-retopology path. [trellis.cpp README](https://github.com/pwilkin/trellis.cpp)

Use a small Bash installer to download a specific release, verify checksums, fetch model files into a separate cache, and write launch commands. Keep native generators as native binaries. Add separate Python environments or CUDA containers only for models that need them. This avoids making an entire workstation depend on one Python/CUDA combination.

For Python generators, prefer a reproducible image per model backend, built from its supported CUDA/PyTorch/Python versions, with a pinned source commit and locked Python dependencies. Where upstream expects Conda or source-built extensions, preserve that supported recipe inside the image rather than forcing every model into uv. uv supports explicit PyTorch indexes and platform-specific sources, but GPU extensions can require matching Torch builds and special build dependencies. macOS has no PyTorch CUDA wheels. [uv PyTorch guide](https://docs.astral.sh/uv/guides/integration/pytorch/)

NVIDIA's container setup requires a host NVIDIA driver and container runtime integration. The CUDA toolkit need not be installed on the host when it is supplied inside the container. A container does not package the host kernel driver: validate driver compatibility with the chosen CUDA runtime. [NVIDIA installation guide](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html), [NVIDIA toolkit README](https://github.com/NVIDIA/nvidia-container-toolkit), [CUDA compatibility](https://docs.nvidia.com/deploy/cuda-compatibility/minor-version-compatibility.html)

| Approach | Appropriate role | Practical tradeoff |
| --- | --- | --- |
| Bash + pinned native binaries | First installation and launch layer | Small and straightforward; must record versions, hashes, OS requirements and model revisions explicitly. |
| Bash + uv environment per model | Python models with compatible wheels | Fast iteration; native dependencies and CUDA extensions still need a matching build environment. |
| Bash + pinned CUDA containers | Python models with conflicting native requirements | Stronger environment isolation and repeatable system libraries; adds image storage and runtime setup. |
| Nix flake/devShell | Host tooling or a fully maintained Nix deployment | Declarative, but CUDA packaging and extension builds require maintenance; better after a working reference installation exists. |

Nix is a viable option, not an automatic fix for upstream GPU packaging. Nixpkgs supports CUDA package sets and an Ada target of `8.9`; its manual warns that CUDA setup hooks can complicate devShell builds and changing one package's CUDA version may leave its transitive dependencies on another version. This is why the initial recommendation favors native binaries and model-specific images. [Nixpkgs CUDA manual source](https://github.com/NixOS/nixpkgs/blob/master/doc/languages-frameworks/cuda.section.md)

The later installer should separate software, models and authored outputs. Persist a manifest for every generation: source image and its hash, model ID/revision/hash, generator release or source commit, settings, seed, logs, hardware/backend and exported files. Schedule one GPU-heavy generation job at a time initially. Actual peak RAM, VRAM, duration and output quality must be measured on the 4080 Super before treating a preset as supported.

## ComfyUI and remote operation

ComfyUI can provide a visual workflow and a browser client on the Mac, while execution runs on Linux. Its local server has a prompt queue, HTTP routes for submitting workflows and querying history, and WebSocket progress. Its documentation recommends an isolated environment and allows shared external model paths. Upstream explicitly does not provide an official Docker image. [ComfyUI server API](https://docs.comfy.org/development/comfyui-server/comms_routes), [manual installation](https://docs.comfy.org/installation/manual_install)

Recommendation: treat ComfyUI as an optional interface, not the installation authority for every 3D model. Start by validating the model's own CLI/API. Add a pinned ComfyUI workflow and integration only when it improves the image preparation or generation workflow. For `trellis.cpp`, its existing server/browser interface may already cover the first pilot. Use an SSH tunnel to access a worker bound to localhost, and transfer results for Blender review on the Mac. That arrangement keeps the CUDA environment on the machine with the NVIDIA GPU.

Current official ComfyUI documentation includes a shared native TRELLIS.2/Pixal3D workflow using `int8_convrot` diffusion checkpoints, shape/texture VAEs, and DINOv3 conditioning. Its finishing stages include remeshing, decimation, UV unwrapping, and baking. Pixal3D also has a multi-view workflow. These documented core integrations are a credible alternate pilot; they do not inherently require community custom nodes. The pages warn that missing core nodes may require a nightly build, and provide no numeric VRAM guarantee for this workstation. Pin a version containing the needed nodes and measure fit before recommending 1024/1536 presets. [ComfyUI TRELLIS.2](https://docs.comfy.org/tutorials/3d/trellis2), [ComfyUI Pixal3D](https://docs.comfy.org/tutorials/3d/pixal3d)

## From generated mesh to usable asset

The proposed production flow is: reference image → generated textured mesh → Blender inspection and correction → topology/UV work where needed → PBR baking → GLB export → review in the target renderer. Preserve the original generated mesh and textures as source material, and make the edited `.blend` file the editable master.

1. Inspect the silhouette from every side, including hidden surfaces; fix disconnected pieces, intersections, unwanted cavities, normals and scale. Compare the result with the intended object rather than accepting its preview image as evidence of quality.
2. For a static prop, decimate when the geometry is otherwise sound; remesh only when geometry needs reconstruction. For a deforming character, build deliberate topology for joints. Blender documents that automatic remesh generally does not produce suitable deformation topology and recommends manual retopology with snapping and Poly Build. [Blender remeshing and retopology](https://docs.blender.org/manual/en/4.5/modeling/meshes/retopology.html)
3. If topology changes, unwrap the final mesh and transfer the high-resolution detail to it. Blender's Cycles baker requires UVs and a target image/color attribute; selected-to-active baking and cages support transfer between meshes. Bake base color without scene lighting, plus tangent-space normals and the material channels required by the renderer. Include adequate island padding and inspect seams under mipmapping. [Blender baking](https://docs.blender.org/UATEST/manual/en/4.5/render/cycles/baking.html)
4. Export GLB with materials and textures, then inspect it independently of the `.blend` file. Blender's glTF exporter supports meshes, textures and Principled BSDF materials; its core PBR channels include base color, metallic, roughness, normal, occlusion and emission. General Blender shader graphs do not constitute a portable material contract, so use exportable nodes or bake procedural detail. [Blender glTF exporter](https://docs.blender.org/manual/en/4.2/addons/import_export/scene_gltf2.html), [Blender 4.5 PBR channels](https://docs.blender.org/manual/ja/4.5/addons/import_export/scene_gltf2.html)

Recommendation: evaluate three representative objects before automating cleanup: an organic prop, a hard-surface object with thin parts, and an intended animated character. Record geometry defects, texture consistency, cleanup time and final asset performance. A successful generated GLB does not establish animation readiness or suitability for this engine's import/material contract.

For c-blueprints specifically, the current imported-mesh fixture uses a `.mesh.json` source and an authored material. Its documentation also identifies planar collision and HRC transport. Keep GLB and `.blend` as portable production sources, then treat the engine-specific conversion and in-scene review as a separate export step. [Current imported-mesh fixture](../imported-folded-card.md)

## Optional capture branch

For real objects that can be photographed, photogrammetry provides a separate path. AliceVision's Meshroom offers multi-view reconstruction workflows and cached node-based execution; this is useful for captured physical objects rather than invented designs. It produces source geometry that still needs the Blender finishing steps above. [Meshroom upstream](https://github.com/alicevision/meshroom)

No installer, model downloads, services or engine assets were changed during this research.
