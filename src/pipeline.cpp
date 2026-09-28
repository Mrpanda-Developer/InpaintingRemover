#include "subtitle_remover/pipeline.hpp"

#include "subtitle_remover/chunk_reader.hpp"
#include "subtitle_remover/bounded_queue.hpp"
#include "subtitle_remover/logging.hpp"
#include "subtitle_remover/video_reader.hpp"
#include "subtitle_remover/video_writer.hpp"

#include <csignal>
#include <fstream>
#include <exception>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>

namespace subtitle_remover {
namespace {

volatile std::sig_atomic_t interrupted = 0;

void handleInterrupt(int) {
    interrupted = 1;
}

struct AudioPacketEvent {
    PacketPtr packet;
    int streamIndex = -1;
};

using DecoderEvent = std::variant<ChunkWindow, AudioPacketEvent>;
using EncoderEvent = std::variant<ChunkWindow, AudioPacketEvent>;

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '"' || character == '\\') escaped.push_back('\\');
        if (character == '\n') escaped += "\\n";
        else if (character == '\r') escaped += "\\r";
        else escaped.push_back(character);
    }
    return escaped;
}

void writeReport(const ProcessingConfig& config, std::size_t chunkCount,
                 std::size_t frameCount, bool complete) {
    const auto reportPath = config.reviewReportPath.empty()
        ? std::filesystem::path(config.outputPath.string() + ".review.json")
        : config.reviewReportPath;
    std::ofstream report(reportPath);
    if (!report) throw std::runtime_error("cannot create review report: " + reportPath.string());
    report << "{\n  \"input\": \"" << jsonEscape(config.inputPath.string())
           << "\",\n  \"status\": \"passthrough_only\",\n  \"subtitle_removal_enabled\": false,\n"
           << "  \"complete\": " << (complete ? "true" : "false")
           << ",\n  \"chunks_processed\": " << chunkCount
           << ",\n  \"frames_written\": " << frameCount
           << ",\n  \"events\": []\n}\n";
}

} // namespace

int runPipeline(const ProcessingConfig& config) {
    const auto inputPath = std::filesystem::absolute(config.inputPath).lexically_normal();
    const auto outputPath = std::filesystem::absolute(config.outputPath).lexically_normal();
    if (inputPath == outputPath) throw std::runtime_error("input and output paths must be different");
    std::error_code filesystemError;
    if (std::filesystem::exists(outputPath, filesystemError) &&
        std::filesystem::equivalent(inputPath, outputPath, filesystemError) && !filesystemError) {
        throw std::runtime_error("input and output resolve to the same file");
    }
    if (!config.reviewReportPath.empty()) {
        const auto reportPath = std::filesystem::absolute(config.reviewReportPath).lexically_normal();
        if (reportPath == inputPath || reportPath == outputPath) {
            throw std::runtime_error("review report path must be different from input and output paths");
        }
    }
    if (config.device == Device::Cuda) {
        throw std::runtime_error("CUDA inference is not available in this passthrough MVP; use --device cpu");
    }
    if (!config.detectorModelPath.empty() || !config.inpainterModelPath.empty()) {
        log(LogLevel::Warning, "model paths are accepted for forward compatibility but no model inference is active in this MVP");
    }
    interrupted = 0;
    const auto previousHandler = std::signal(SIGINT, handleInterrupt);
    try {
        VideoReader reader(config.inputPath);
        VideoWriter writer(config.outputPath, reader.info(), reader.formatContext(), config);
        BoundedQueue<DecoderEvent> decodedEvents(3);
        BoundedQueue<EncoderEvent> processedEvents(3);
        std::mutex failureMutex;
        std::exception_ptr workerFailure;
        const auto rememberFailure = [&] {
            std::lock_guard lock(failureMutex);
            if (!workerFailure) workerFailure = std::current_exception();
        };

        reader.setAudioPacketSink([&decodedEvents](const AVPacket& packet, int streamIndex) {
            PacketPtr copy(av_packet_clone(&packet));
            if (!copy || !decodedEvents.push(DecoderEvent{AudioPacketEvent{std::move(copy), streamIndex}})) {
                throw std::runtime_error("decoder event queue closed while forwarding audio");
            }
        });

        std::thread decoderThread([&] {
            try {
                ChunkReader chunks(reader, config.chunkDurationSeconds, config.overlapSeconds);
                ChunkWindow chunk;
                while (interrupted == 0 && chunks.next(chunk)) {
                    if (!decodedEvents.push(DecoderEvent{std::move(chunk)})) break;
                }
                decodedEvents.close();
            } catch (...) {
                rememberFailure();
                decodedEvents.close();
                processedEvents.close();
            }
        });
        std::thread processingThread([&] {
            try {
                while (auto event = decodedEvents.pop()) {
                    if (auto* chunk = std::get_if<ChunkWindow>(&*event)) {
                        processedEvents.push(EncoderEvent{std::move(*chunk)});
                    } else {
                        processedEvents.push(EncoderEvent{std::move(std::get<AudioPacketEvent>(*event))});
                    }
                }
                processedEvents.close();
            } catch (...) {
                rememberFailure();
                decodedEvents.close();
                processedEvents.close();
            }
        });

        std::size_t chunkCount = 0;
        std::size_t frameCount = 0;
        std::exception_ptr encoderFailure;
        try {
            while (auto event = processedEvents.pop()) {
                if (auto* audio = std::get_if<AudioPacketEvent>(&*event)) {
                    writer.writeAudioPacket(*audio->packet, audio->streamIndex);
                } else {
                    auto& chunk = std::get<ChunkWindow>(*event);
                    for (std::size_t index = chunk.centralBeginIndex; index < chunk.centralEndIndex; ++index) {
                        writer.writeVideoFrame(chunk.frames[index]);
                        ++frameCount;
                    }
                    ++chunkCount;
                    log(LogLevel::Info, "processed chunk " + std::to_string(chunk.sequence) +
                                        " with " + std::to_string(chunk.centralEndIndex - chunk.centralBeginIndex) +
                                        " central frames");
                }
            }
        } catch (...) {
            encoderFailure = std::current_exception();
            decodedEvents.close();
            processedEvents.close();
        }
        decoderThread.join();
        processingThread.join();
        if (encoderFailure) std::rethrow_exception(encoderFailure);
        if (workerFailure) std::rethrow_exception(workerFailure);
        writer.finish();
        const bool complete = interrupted == 0;
        writeReport(config, chunkCount, frameCount, complete);
        std::signal(SIGINT, previousHandler);
        if (!complete) log(LogLevel::Warning, "interrupted; finalized a partial output and review report");
        return complete ? 0 : 130;
    } catch (...) {
        std::signal(SIGINT, previousHandler);
        throw;
    }
}

} // namespace subtitle_remover