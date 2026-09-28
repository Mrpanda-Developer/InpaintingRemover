#include "subtitle_remover/config.hpp"
#include "subtitle_remover/pipeline.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: passthrough-integration INPUT_VIDEO OUTPUT_VIDEO\n";
        return 2;
    }
    try {
        subtitle_remover::ProcessingConfig config;
        config.inputPath = argv[1];
        config.outputPath = argv[2];
        config.reviewReportPath = std::string(argv[2]) + ".review.json";
        return subtitle_remover::runPipeline(config);
    } catch (const std::exception& error) {
        std::cerr << "Integration harness failed: " << error.what() << '\n';
        return 1;
    }
}