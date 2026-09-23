# Project guidance for coding agents

This file applies to the entire repository. Follow the user's current request first, then these project conventions. Read `README.md` and `resources/models/README.md` when changing build, model, or deployment behavior.

## Project and platform scope

- ImgToTable is a local C++20 / Qt 6 Widgets desktop application built with CMake. Keep the implementation small and native; add dependencies only when the requested feature needs them.
- Supported targets are Windows 10/11 x64 with MSVC 2022 and macOS 14+ on Apple Silicon (arm64). Do not introduce Intel Mac or Universal build paths.
- `src/` owns startup, `ui/` the Widgets interface, `core/` inference without Qt, and `platform/` platform-specific paths. Keep ONNX Runtime headers and types inside `core/InferenceEngine.cpp` unless an API change truly requires them elsewhere.

## Current model state

- `resources/models/PP-OCRv6_small_det/`, `PP-OCRv6_small_rec/`, and `SLANet_plus/` contain the selected ONNX models and their original `inference.yml` files. See `resources/models/README.md` for pinned sources and SHA-256 values.
- The present application is still a single-model scaffold: it looks for `resources/models/model.onnx` at build time and has no OCR, table-structure, preprocessing, or postprocessing pipeline. Do not claim image-to-table works until the full path is implemented and checked with real images.
- Treat detection, recognition, and table-structure models as separate sessions. Follow each model's `inference.yml` for input normalization, resizing, character dictionaries, and output decoding. Keep table structure and recognized cell text separate until matching them explicitly.
- Model weights and downloaded SDKs are ignored by Git. Keep source/version/checksum information in text files. Do not force-add large binaries or change distribution licensing without a specific request.

## Build and verification

- Configure with CMake 3.24+ and Ninja. `ONNXRUNTIME_ROOT` points to the extracted C/C++ SDK; without it the core builds with inference disabled. `download_deps.py` downloads the pinned Qt and ONNX Runtime SDKs.
- For core-only checks: `cmake -S . -B build-core -G Ninja -DIMGTOTABLE_BUILD_APP=OFF`, then `cmake --build build-core` and `ctest --test-dir build-core --output-on-failure`.
- For a UI or SDK change, configure and build the relevant platform target when its dependencies are available. Do not report real model inference as tested unless an actual model was loaded and run.
- Add focused tests for behavior changes, especially model input/output validation, error recovery, and decoding. Keep tests runnable in Release builds; existing tests do not depend on `assert`.

## Inference and UI behavior

- Preserve clear error messages and clear output on failure. Keep session lifetime shorter than the ONNX Runtime environment lifetime.
- Run image preprocessing and inference away from the UI thread when connecting the real pipeline. Use a single worker unless evidence calls for more concurrency; prevent duplicate submissions and wait for shutdown. The current inference engine is not thread-safe.
- Keep Windows Unicode paths working and resolve bundled model paths relative to the executable, not the process working directory.
