#pragma once

#include "subtitle_remover/config.hpp"

#include <string_view>

namespace subtitle_remover {

struct CliResult {
    ProcessingConfig config;
    bool showHelp = false;
};

CliResult parseCommandLine(int argc, char** argv);
std::string_view usageText() noexcept;

} // namespace subtitle_remover