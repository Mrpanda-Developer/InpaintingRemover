#pragma once

#include "subtitle_remover/config.hpp"

#include <filesystem>
#include <stdexcept>

namespace subtitle_remover {

class OnnxSession {
public:
    OnnxSession(const std::filesystem::path& modelPath, Device device, int gpuId) {
        (void)modelPath;
        (void)device;
        (void)gpuId;
        throw std::runtime_error("ONNX Runtime session support is not implemented in this MVP");
    }
};

} // namespace subtitle_remover