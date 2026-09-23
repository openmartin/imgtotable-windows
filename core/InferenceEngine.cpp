#include "InferenceEngine.h"

#include <exception>
#include <limits>
#include <stdexcept>

#ifdef IMGTOTABLE_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif

struct InferenceEngine::Impl {
#ifdef IMGTOTABLE_HAS_ONNX
    // Reverse destruction order guarantees the environment outlives the session.
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "ImgToTable"};
    Ort::Session session{nullptr};
#endif
};

InferenceEngine::InferenceEngine() = default;
InferenceEngine::~InferenceEngine() = default;

bool InferenceEngine::loadModel(const std::filesystem::path& path) {
    lastError_.clear();
    impl_.reset();
#ifdef IMGTOTABLE_HAS_ONNX
    try {
        if (!std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("Model file does not exist or is not a regular file.");
        }
        auto state = std::make_unique<Impl>();
        Ort::SessionOptions options;
        // Default CPU provider. Append an explicit provider here when needed.
        state->session = Ort::Session(state->env, path.c_str(), options);
        if (state->session.GetInputCount() != 1 || state->session.GetOutputCount() != 1) {
            throw std::runtime_error("This starter supports exactly one input and one output.");
        }
        const auto input = state->session.GetInputTypeInfo();
        const auto output = state->session.GetOutputTypeInfo();
        for (const auto* type : {&input, &output}) {
            if (type->GetONNXType() != ONNX_TYPE_TENSOR ||
                type->GetTensorTypeAndShapeInfo().GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                throw std::runtime_error("This starter supports float32 tensors only.");
            }
        }
        impl_ = std::move(state);
        return true;
    } catch (const Ort::Exception& error) {
        lastError_ = std::string("ONNX Runtime: ") + error.what();
    } catch (const std::exception& error) {
        lastError_ = error.what();
    }
#else
    (void)path;
    lastError_ = "ONNX Runtime is not configured. Set ONNXRUNTIME_ROOT and rebuild.";
#endif
    return false;
}

bool InferenceEngine::isModelLoaded() const noexcept { return impl_ != nullptr; }
const std::string& InferenceEngine::lastError() const noexcept { return lastError_; }

bool InferenceEngine::run(std::span<const float> values,
                          std::span<const std::int64_t> shape, Tensor& output) {
    output = {};
    lastError_.clear();
    if (!isModelLoaded()) {
        lastError_ = "No model is loaded.";
        return false;
    }
#ifdef IMGTOTABLE_HAS_ONNX
    try {
        std::size_t count = 1;
        for (const auto dimension : shape) {
            if (dimension <= 0 || static_cast<std::uint64_t>(dimension) >
                    std::numeric_limits<std::size_t>::max() / count) {
                throw std::runtime_error("Input dimensions must be positive and fit in memory.");
            }
            count *= static_cast<std::size_t>(dimension);
        }
        if (count != values.size()) {
            throw std::runtime_error("Input shape does not match the number of values.");
        }
        // Own the buffer rather than casting away constness from the caller's span.
        std::vector<float> buffer(values.begin(), values.end());
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto input = Ort::Value::CreateTensor<float>(memory, buffer.data(), buffer.size(),
                                                    shape.data(), shape.size());
        Ort::AllocatorWithDefaultOptions allocator;
        const auto inputName = impl_->session.GetInputNameAllocated(0, allocator);
        const auto outputName = impl_->session.GetOutputNameAllocated(0, allocator);
        const char* inputNames[] = {inputName.get()};
        const char* outputNames[] = {outputName.get()};
        auto results = impl_->session.Run(Ort::RunOptions{nullptr}, inputNames, &input, 1,
                                          outputNames, 1);
        const auto info = results.front().GetTensorTypeAndShapeInfo();
        Tensor result;
        result.shape = info.GetShape();
        const auto size = info.GetElementCount();
        if (size != 0) {
            const auto* data = results.front().GetTensorData<float>();
            result.values.assign(data, data + size);
        }
        output = std::move(result);
        return true;
    } catch (const Ort::Exception& error) {
        lastError_ = std::string("ONNX Runtime: ") + error.what();
    } catch (const std::exception& error) {
        lastError_ = error.what();
    }
#else
    (void)values;
    (void)shape;
#endif
    return false;
}
