#include "core/InferenceEngine.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
}

int main(int argc, char* argv[]) {
    try {
        InferenceEngine engine;
        InferenceEngine::Tensor output{{42.0F}, {1}};
        require(!engine.isModelLoaded(), "New engine must be unloaded");
        require(!engine.run({}, {}, output), "Run without a model must fail");
        require(output.values.empty() && output.shape.empty(), "Failure must clear output");
        require(!engine.lastError().empty(), "Failure must have a diagnostic");
        require(!engine.loadModel({}), "Empty model path must fail");
        require(!engine.isModelLoaded(), "Failed load must leave engine unloaded");
        require(!engine.lastError().empty(), "Load failure must have a diagnostic");

        if (argc == 2) {
            const std::string argument(argv[1]);
            const std::u8string utf8Path(argument.begin(), argument.end());
            if (!engine.loadModel(std::filesystem::path(utf8Path))) {
                throw std::runtime_error(engine.lastError());
            }
            const std::array values{1.0F, 2.0F, 3.0F};
            const std::array<std::int64_t, 2> shape{1, 3};
            if (!engine.run(values, shape, output)) {
                throw std::runtime_error(engine.lastError());
            }
            require(output.values == std::vector<float>(values.begin(), values.end()), "Identity values differ");
            require(output.shape == std::vector<std::int64_t>(shape.begin(), shape.end()), "Identity shape differs");
            const std::array<std::int64_t, 2> invalidShape{1, 4};
            require(!engine.run(values, invalidShape, output), "Mismatched shape must fail");
            require(engine.isModelLoaded(), "Run failure must retain the model");
            require(engine.run(values, shape, output), "Engine must recover after bad input");
            require(!engine.loadModel({}), "Reload from invalid path must fail");
            require(!engine.isModelLoaded(), "Failed reload must discard previous model");
        }
        std::cout << "InferenceEngine tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
