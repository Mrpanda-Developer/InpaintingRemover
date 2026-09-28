#include "subtitle_remover/cli.hpp"
#include "subtitle_remover/logging.hpp"
#include "subtitle_remover/pipeline.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        const auto result = subtitle_remover::parseCommandLine(argc, argv);
        if (result.showHelp) {
            std::cout << subtitle_remover::usageText() << '\n';
            return 0;
        }
        return subtitle_remover::runPipeline(result.config);
    } catch (const std::exception& error) {
        subtitle_remover::log(subtitle_remover::LogLevel::Error, error.what());
        std::cerr << "Run 'subtitle-remover --help' for usage.\n";
        return 1;
    }
}