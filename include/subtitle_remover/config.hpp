#pragma once

#include <filesystem>
#include <string>

namespace subtitle_remover {

enum class Device { Cpu, Cuda };
enum class SubtitleRegion { Full, Bottom20, Bottom30, Bottom35, Bottom40 };
enum class FlowQuality { Low, Medium, High };
enum class Quality { Fast, Balanced, High };
enum class Preset { Fast, Balanced, Broadcast };
enum class VideoCodec { H264, Hevc, Prores };
enum class AudioMode { Copy, Reencode };

struct ProcessingConfig {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    double chunkDurationSeconds = 8.0;
    double overlapSeconds = 0.5;
    int detectionInterval = 5;
    int maskPadding = 6;
    int maskDilate = 3;
    int temporalRadius = 12;
    int maxReferenceDistance = 50;
    float minTextConfidence = 0.70F;
    float inpaintThreshold = 0.55F;
    float outputConfidenceThreshold = 0.80F;
    int threads = 0;
    int gpuId = 0;
    int cropPadding = 24;
    Device device = Device::Cpu;
    SubtitleRegion subtitleRegion = SubtitleRegion::Bottom35;
    FlowQuality flowQuality = FlowQuality::Medium;
    Quality quality = Quality::Balanced;
    Preset preset = Preset::Balanced;
    VideoCodec videoCodec = VideoCodec::H264;
    AudioMode audioMode = AudioMode::Copy;
    std::filesystem::path detectorModelPath;
    std::filesystem::path inpainterModelPath;
    std::filesystem::path reviewReportPath;
    bool saveDebugMasks = false;
    bool saveDebugFrames = false;
};

void applyPreset(ProcessingConfig& config, Preset preset);

} // namespace subtitle_remover