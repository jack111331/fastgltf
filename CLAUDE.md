# CLAUDE.md

## Project overview

A renderer that loads a **modified glTF 2.0 format** in which parts of the scene are
synthesized by neural networks at load time or on demand. Examples:

- **Text-to-camera-trajectory**: a model drives the scene camera from a text prompt.
- **Text-to-motion**: a model animates a skinned human from a text prompt.

The parser is built on top of [fastgltf](https://github.com/spnda/fastgltf) (C++, simdjson-based).

## Core design principles

1. **Extend glTF, never fork the format.** All AI-specific data lives in a vendor
   extension (working name: `MYLAB_generative`; rename to our real prefix). The
   `KHR_` and `EXT_` prefixes are reserved and must not be used.
2. **Keep the extension optional.** List it in `extensionsUsed` only, not
   `extensionsRequired`, so standard viewers (Blender, three.js, glTF Validator) still
   load the file and play the baked fallbacks.
3. **Generators output standard glTF data.** Model outputs are converted into ordinary
   accessors, animation samplers, and channels. The renderer's animation/skinning path
   stays 100% standard glTF; it never needs to know an animation was generated.
   - Camera trajectory -> animation channels on the camera node's translation/rotation.
     Camera intrinsics (e.g. `yfov`) -> `KHR_animation_pointer` (check fastgltf support).
   - Text-to-motion -> per-joint rotation channels + root translation on the `skin` joints.
4. **Registry vs. requests.** Models are declared once at the root (`generators`);
   nodes reference a generator by index along with their conditioning (prompt, seed, ...).
5. **Reproducibility.** Every model is identified by URI **and** SHA-256 hash. Output is
   a pure function of (model hash, prompt, seed, constraints, upstream outputs), which
   is also the cache key.
6. **Baking is first-class.** Each generative node points to a `fallbackAnimation`. A
   bake tool runs inference and writes results into normal buffers while keeping the
   generation metadata for later regeneration.

## Extension schema (draft)

```json
{
  "extensionsUsed": ["MYLAB_generative"],
  "extensions": {
    "MYLAB_generative": {
      "generators": [
        {
          "name": "motion_gen",
          "task": "text_to_motion",
          "model": { "uri": "models/t2m_v2.onnx", "sha256": "...", "skeleton": "smpl_22" },
          "output": { "fps": 20, "rotation": "axis_angle", "upAxis": "Z", "units": "meters" }
        },
        {
          "name": "cam_gen",
          "task": "text_to_camera_trajectory",
          "model": { "uri": "models/camtraj.onnx", "sha256": "..." },
          "output": { "fps": 30, "cameraConvention": "opencv", "poseType": "cam_to_world" }
        }
      ],
      "timeline": { "duration": 8.0 }
    }
  },
  "nodes": [
    {
      "name": "Actor", "skin": 0, "mesh": 0,
      "extensions": { "MYLAB_generative": {
        "generator": 0,
        "prompt": "a person walks forward, then waves with the right hand",
        "seed": 42, "start": 0.0, "duration": 6.0,
        "jointMap": { "pelvis": 3, "left_hip": 4, "right_hip": 8 },
        "constraints": { "initialPose": "bindPose" },
        "fallbackAnimation": 0
      }}
    },
    {
      "name": "MainCamera", "camera": 0,
      "extensions": { "MYLAB_generative": {
        "generator": 1,
        "prompt": "slow orbit around the actor, ending on a close-up",
        "seed": 7,
        "conditionOn": { "lookAtNode": 0, "useGeneratedMotionOf": 0 },
        "fallbackAnimation": 1
      }}
    }
  ]
}
```

TODO: formalize this as a JSON Schema in `schema/` and validate against it.

## Coordinate conventions and retargeting (main source of bugs)

- **glTF**: right-handed, +Y up, meters, quaternion rotations (x, y, z, w),
  asset front is +Z, cameras look down local -Z.
- **Text-to-motion models** (e.g. HumanML3D-style, 22-joint SMPL skeleton) are often
  Z-up, output axis-angle or 6D rotations, and are relative to their own rest pose.
- **Camera models** may output OpenCV convention (+Y down, +Z forward), and either
  camera-to-world or world-to-camera matrices.

Rules:
- Each generator declares its output conventions (`upAxis`, `units`, `rotation`,
  `fps`, `cameraConvention`, `poseType`). Never hardcode them in the converter.
- Motion output needs a `jointMap` (model joint name -> glTF joint node index) plus a
  rest-pose correction from the model skeleton to the skin's bind pose.
- All conversions live in one converter module with unit tests (round-trip checks,
  known poses).

## Dependency graph

- `conditionOn` references between generative nodes form a DAG.
- Loader builds the DAG, rejects cycles at validation time, and runs generators in
  topological order, feeding each the resolved outputs of its dependencies.
- Design the scheduler so it can later evaluate incrementally (streaming /
  autoregressive models).

## Planned architecture

```
fastgltf parse -> extension parse -> validate -> DAG scheduler + cache
    -> inference backend -> converter (retarget + coord conversion)
    -> write accessors/samplers/channels into fastgltf::Asset -> standard renderer
```

- **Parsing**: fastgltf for all standard glTF; separate code path for `MYLAB_generative`.
- **Validation**: JSON Schema check, joint maps reference valid skin joints,
  generator indices in range, DAG acyclic, fallback animations exist.
- **Inference backend**: behind an interface so we can swap between ONNX Runtime
  (in-process) and a Python sidecar service (for PyTorch models).
- **Converter**: model output -> glTF animation data. Kept separate from inference so
  models with the same task but different conventions are interchangeable.
- **Bake tool / export**: use `fastgltf::Exporter` to write a plain .glb with generated
  animations for debugging in Blender.

## fastgltf integration notes (verify against the repo before relying on these)

- fastgltf knows a fixed set of extensions via its `fastgltf::Extensions` flags.
  If an **unknown** extension appears in `extensionsRequired`, loading fails
  (unsupported/missing extension errors). Another reason to keep ours optional.
- fastgltf has `Parser::setExtrasParseCallback` (receives the simdjson object,
  object index, and `fastgltf::Category`) and `Exporter` supports writing extras.
  Useful for quick prototyping via `extras`, but it does not cover `extensions`.
- Options for reading our extension, in order of preference to evaluate:
  1. Second pass over the raw JSON with simdjson (already a dependency) to read
     `extensions.MYLAB_generative` on the root and on nodes, keyed by object index.
     Minimal coupling to fastgltf internals; easy to upgrade fastgltf.
  2. Prototype through `extras` + the extras callback, migrate to (1) later.
  3. Fork fastgltf and add the extension natively (new flag, parse functions,
     fields on `fastgltf::Node` / `Asset`). Most integrated, most maintenance.
- Check whether the pinned fastgltf version supports `KHR_animation_pointer`;
  if not, camera intrinsics animation needs our own handling.
- Generated animation data must be appended to the `fastgltf::Asset` as new
  buffers / bufferViews / accessors / animations with correct indices.

## Open decisions

- Final extension name/prefix.
- Option 1 vs 2 vs 3 for reading the extension from fastgltf.
- Inference backend for v1 (ONNX Runtime vs Python sidecar).
- Cache location and format for generated results.
- Whether model weights can ever be embedded in the .glb (default: no, external URI).

## Working conventions for Claude

- Keep standard glTF handling and generative handling in separate modules.
- Any change to the extension schema must update the JSON Schema and an example file
  in `examples/`.
- Add unit tests for every coordinate/rotation conversion and for DAG resolution.
- Prefer a baked-output debug path (export .glb) when diagnosing animation bugs.
