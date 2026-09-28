#include "subtitle_remover/cli.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace subtitle_remover {
namespace {

template <typename T>
T parseNumber(std::string_view text, std::string_view option) {
    T value{};
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::runtime_error("invalid value for " + std::string(option) + ": " + std::string(text));
    }
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value)) throw std::runtime_error("value for " + std::string(option) + " must be finite");
    }
    return value;
}

std::string_view requireValue(int& index, int argc, char** argv, std::string_view option) {
    if (index + 1 >= argc) throw std::runtime_error("missing value for " + std::string(option));
    return argv[++index];
}

} // namespace

void applyPreset(ProcessingConfig& config, Preset preset) {
    config.preset = preset;
    switch (preset) {
    case Preset::Fast:
        config.detectionInterval = 10;
        config.temporalRadius = 6;
        config.maxReferenceDistance = 24;
        config.overlapSeconds = 0.25;
        config.flowQuality = FlowQuality::Low;
        config.quality = Quality::Fast;
        break;
    case Preset::Balanced:
        config.detectionInterval = 5;
        config.temporalRadius = 12;
        config.maxReferenceDistance = 50;
        config.overlapSeconds = 0.5;
        config.flowQuality = FlowQuality::Medium;
        config.quality = Quality::Balanced;
        break;
    case Preset::Broadcast:
        config.detectionInterval = 3;
        config.temporalRadius = 24;
        config.maxReferenceDistance = 100;
        config.overlapSeconds = 1.0;
        config.maskPadding = 8;
        config.maskDilate = 5;
        config.flowQuality = FlowQuality::High;
        config.quality = Quality::High;
        config.outputConfidenceThreshold = 0.9F;
        break;
    }
}

CliResult parseCommandLine(int argc, char** argv) {
    CliResult result;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--preset") {
            const auto value = requireValue(i, argc, argv, "--preset");
            if (value == "fast") applyPreset(result.config, Preset::Fast);
            else if (value == "balanced") applyPreset(result.config, Preset::Balanced);
            else if (value == "broadcast") applyPreset(result.config, Preset::Broadcast);
            else throw std::runtime_error("--preset must be fast, balanced, or broadcast");
        }
    }

    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--preset") { ++i; continue; }
        if (option == "--help" || option == "-h") { result.showHelp = true; continue; }
        if (option == "-i" || option == "--input") result.config.inputPath = requireValue(i, argc, argv, option);
        else if (option == "-o" || option == "--output") result.config.outputPath = requireValue(i, argc, argv, option);
        else if (option == "--chunk-duration") result.config.chunkDurationSeconds = parseNumber<double>(requireValue(i, argc, argv, option), option);
        else if (option == "--overlap") result.config.overlapSeconds = parseNumber<double>(requireValue(i, argc, argv, option), option);
        else if (option == "--threads") result.config.threads = parseNumber<int>(requireValue(i, argc, argv, option), option);
        else if (option == "--gpu-id") result.config.gpuId = parseNumber<int>(requireValue(i, argc, argv, option), option);
        else if (option == "--device") {
            const auto value = requireValue(i, argc, argv, option);
            if (value == "cpu") result.config.device = Device::Cpu;
            else if (value == "cuda") result.config.device = Device::Cuda;
            else throw std::runtime_error("--device must be cpu or cuda");
        } else if (option == "--subtitle-region") {
            const auto value = requireValue(i, argc, argv, option);
            if (value == "full") result.config.subtitleRegion = SubtitleRegion::Full;
            else if (value == "bottom-20") result.config.subtitleRegion = SubtitleRegion::Bottom20;
            else if (value == "bottom-30") result.config.subtitleRegion = SubtitleRegion::Bottom30;
            else if (value == "bottom-35") result.config.subtitleRegion = SubtitleRegion::Bottom35;
            else if (value == "bottom-40") result.config.subtitleRegion = SubtitleRegion::Bottom40;
            else throw std::runtime_error("--subtitle-region must be full, bottom-20, bottom-30, bottom-35, or bottom-40");
        } else if (option == "--detection-interval") result.config.detectionInterval = parseNumber<int>(requireValue(i, argc, argv, option), option);
        else if (option == "--mask-padding") result.config.maskPadding = parseNumber<int>(requireValue(i, argc, argv, option), option);
        else if (option == "--mask-dilate") result.config.maskDilate = parseNumber<int>(requireValue(i, argc, argv, option), option);
        else if (option == "--min-text-confidence") result.config.minTextConfidence = parseNumber<float>(requireValue(i, argc, argv, option), option);
        else if (option == "--temporal-radius") result.config.temporalRadius = parseNumber<int>(requireValue(i, argc, argv, option), option);
        else if (option == "--max-reference-distance") result.config.maxReferenceDistance = parseNumber<int>(requireValue(i, argc, argv, option), option);
        else if (option == "--flow-quality") {
            const auto value = requireValue(i, argc, argv, option);
            if (value == "low") result.config.flowQuality = FlowQuality::Low;
            else if (value == "medium") result.config.flowQuality = FlowQuality::Medium;
            else if (value == "high") result.config.flowQuality = FlowQuality::High;
            else throw std::runtime_error("--flow-quality must be low, medium, or high");
        } else if (option == "--detector") result.config.detectorModelPath = requireValue(i, argc, argv, option);
        else if (option == "--inpainter") result.config.inpainterModelPath = requireValue(i, argc, argv, option);
        else if (option == "--inpaint-threshold") result.config.inpaintThreshold = parseNumber<float>(requireValue(i, argc, argv, option), option);
        else if (option == "--crop-padding") result.config.cropPadding = parseNumber<int>(requireValue(i, argc, argv, option), option);
        else if (option == "--quality") {
            const auto value = requireValue(i, argc, argv, option);
            if (value == "fast") result.config.quality = Quality::Fast;
            else if (value == "balanced") result.config.quality = Quality::Balanced;
            else if (value == "high") result.config.quality = Quality::High;
            else throw std::runtime_error("--quality must be fast, balanced, or high");
        } else if (option == "--confidence-threshold") result.config.outputConfidenceThreshold = parseNumber<float>(requireValue(i, argc, argv, option), option);
        else if (option == "--review-report") result.config.reviewReportPath = requireValue(i, argc, argv, option);
        else if (option == "--save-debug-masks") result.config.saveDebugMasks = true;
        else if (option == "--save-debug-frames") result.config.saveDebugFrames = true;
        else if (option == "--video-codec") {
            const auto value = requireValue(i, argc, argv, option);
            if (value == "h264") result.config.videoCodec = VideoCodec::H264;
            else if (value == "hevc") result.config.videoCodec = VideoCodec::Hevc;
            else if (value == "prores") result.config.videoCodec = VideoCodec::Prores;
            else throw std::runtime_error("--video-codec must be h264, hevc, or prores");
        } else if (option == "--audio-mode") {
            const auto value = requireValue(i, argc, argv, option);
            if (value == "copy") result.config.audioMode = AudioMode::Copy;
            else if (value == "reencode") result.config.audioMode = AudioMode::Reencode;
            else throw std::runtime_error("--audio-mode must be copy or reencode");
        } else {
            throw std::runtime_error("unknown option: " + std::string(option));
        }
    }

    if (result.showHelp) return result;
    if (result.config.inputPath.empty() || result.config.outputPath.empty()) {
        throw std::runtime_error("both --input and --output are required");
    }
    if (result.config.chunkDurationSeconds <= 0.0 || result.config.overlapSeconds < 0.0 ||
        result.config.overlapSeconds * 2.0 >= result.config.chunkDurationSeconds) {
        throw std::runtime_error("chunk duration must be positive and overlap must be non-negative and less than half the chunk duration");
    }
    if (result.config.detectionInterval <= 0 || result.config.threads < 0 || result.config.gpuId < 0 ||
        result.config.maskPadding < 0 || result.config.maskDilate < 0 || result.config.temporalRadius < 0 ||
        result.config.maxReferenceDistance < 0 || result.config.cropPadding < 0) {
        throw std::runtime_error("integer processing options must be non-negative; detection interval must be positive");
    }
    const auto validUnitFloat = [](float value) { return value >= 0.0F && value <= 1.0F; };
    if (!validUnitFloat(result.config.minTextConfidence) || !validUnitFloat(result.config.inpaintThreshold) ||
        !validUnitFloat(result.config.outputConfidenceThreshold)) {
        throw std::runtime_error("confidence and inpaint thresholds must be between 0 and 1");
    }
    return result;
}

std::string_view usageText() noexcept {
    return R"(subtitle-remover - chunked video decode/re-encode MVP

Usage:
  subtitle-remover -i INPUT -o OUTPUT [options]

Required:
  -i, --input PATH                 Input video
  -o, --output PATH                Output video

Chunking and processing:
  --chunk-duration FLOAT           Central chunk duration in seconds (default: 8)
  --overlap FLOAT                  Context on each boundary (default: 0.5)
  --threads INT                    Processing thread hint (default: 0)
  --device cpu|cuda                 Inference device (default: cpu)
  --gpu-id INT                     CUDA device index
  --subtitle-region REGION         full|bottom-20|bottom-30|bottom-35|bottom-40
  --detection-interval INT         Detect every N frames (default: 5)
  --mask-padding INT               Subtitle mask padding (default: 6)
  --mask-dilate INT                Additional mask dilation (default: 3)
  --min-text-confidence FLOAT      Detection threshold (default: 0.70)
  --temporal-radius INT             Temporal reconstruction radius (default: 12)
  --max-reference-distance INT      Maximum reference-frame distance (default: 50)
  --flow-quality low|medium|high    Optical-flow quality
  --detector PATH                  Optional detector model path
  --inpainter PATH                 Optional inpainter model path
  --inpaint-threshold FLOAT         Inpaint unresolved confidence threshold
  --crop-padding INT                Inference ROI padding
  --quality fast|balanced|high      Quality policy
  --confidence-threshold FLOAT      Review threshold
  --review-report PATH              Write machine-readable JSON report
  --save-debug-masks                Save detector masks (not active in passthrough MVP)
  --save-debug-frames               Save debug frames (not active in passthrough MVP)
  --video-codec h264|hevc|prores    Output video encoder (default: h264)
  --audio-mode copy|reencode        Audio policy (reencode is not yet implemented)
  --preset fast|balanced|broadcast  Configure processing defaults
  -h, --help                        Show this help

This MVP currently validates chunked decode, timestamp-preserving video encode,
and compatible audio remux. Subtitle-removal processing is not yet enabled.)";
}

} // namespace subtitle_remover