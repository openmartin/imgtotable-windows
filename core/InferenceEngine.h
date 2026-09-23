#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

class InferenceEngine final {
public:
    struct Tensor {
        std::vector<float> values;
        std::vector<std::int64_t> shape;
    };

    InferenceEngine();
    ~InferenceEngine();
    InferenceEngine(const InferenceEngine&) = delete;
    InferenceEngine& operator=(const InferenceEngine&) = delete;

    // Synchronous, not thread-safe. Load once; use from one worker when integrated.
    bool loadModel(const std::filesystem::path& path);
    bool isModelLoaded() const noexcept;
    const std::string& lastError() const noexcept;
    // Supports one float32 tensor input and output. Clears output on failure.
    bool run(std::span<const float> values,
             std::span<const std::int64_t> shape, Tensor& output);

private:
    // Keeps ONNX headers and optional build configuration out of callers.
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string lastError_;
};
