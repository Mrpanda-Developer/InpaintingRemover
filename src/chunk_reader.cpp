#include "subtitle_remover/chunk_reader.hpp"

#include <algorithm>
#include <stdexcept>

namespace subtitle_remover {

ChunkSchedule::ChunkSchedule(double durationSeconds, double overlapSeconds, double originSeconds)
    : durationSeconds_(durationSeconds), overlapSeconds_(overlapSeconds), originSeconds_(originSeconds) {
    if (durationSeconds_ <= 0.0 || overlapSeconds_ < 0.0 || overlapSeconds_ * 2.0 >= durationSeconds_) {
        throw std::invalid_argument("invalid chunk duration or overlap");
    }
}

ChunkWindow ChunkSchedule::window(std::size_t sequence) const {
    const double start = originSeconds_ + static_cast<double>(sequence) * durationSeconds_;
    return ChunkWindow{sequence, start, start + durationSeconds_,
        std::max(originSeconds_, start - overlapSeconds_), start + durationSeconds_ + overlapSeconds_};
}

bool ChunkSchedule::containsCentral(const ChunkWindow& window, double timestampSeconds) const noexcept {
    return timestampSeconds >= window.centralStartSeconds && timestampSeconds < window.centralEndSeconds;
}

ChunkReader::ChunkReader(VideoReader& reader, double durationSeconds, double overlapSeconds)
        : reader_(reader), schedule_(durationSeconds, overlapSeconds, 0.0),
            durationSeconds_(durationSeconds), overlapSeconds_(overlapSeconds) {}

bool ChunkReader::fillThrough(double timestampSeconds) {
    while (!exhausted_ && (bufferedFrames_.empty() || bufferedFrames_.back().timestampSeconds < timestampSeconds)) {
        VideoFrame frame;
        if (!reader_.read(frame)) {
            exhausted_ = true;
            break;
        }
        bufferedFrames_.push_back(std::move(frame));
    }
    return !bufferedFrames_.empty();
}

bool ChunkReader::next(ChunkWindow& output) {
    while (true) {
        if (bufferedFrames_.empty() && !exhausted_) {
            VideoFrame first;
            if (reader_.read(first)) bufferedFrames_.push_back(std::move(first));
            else exhausted_ = true;
        }
        if (bufferedFrames_.empty()) return false;
        if (!scheduleReady_) {
            schedule_ = ChunkSchedule(durationSeconds_, overlapSeconds_, bufferedFrames_.front().timestampSeconds);
            scheduleReady_ = true;
        }

        const ChunkWindow window = schedule_.window(sequence_);
        fillThrough(window.contextEndSeconds);
        while (!bufferedFrames_.empty() && bufferedFrames_.front().timestampSeconds < window.contextStartSeconds) {
            bufferedFrames_.pop_front();
        }

        output = window;
        output.frames.clear();
        output.centralBeginIndex = 0;
        output.centralEndIndex = 0;
        bool foundCentralFrame = false;
        for (const auto& frame : bufferedFrames_) {
            if (frame.timestampSeconds > window.contextEndSeconds && !exhausted_) break;
            if (frame.timestampSeconds < window.contextStartSeconds) continue;
            const std::size_t frameIndex = output.frames.size();
            output.frames.push_back(frame);
            if (schedule_.containsCentral(window, frame.timestampSeconds)) {
                if (!foundCentralFrame) {
                    output.centralBeginIndex = frameIndex;
                    foundCentralFrame = true;
                }
                output.centralEndIndex = frameIndex + 1;
            }
        }

        ++sequence_;
        if (foundCentralFrame) return true;
        if (exhausted_ && bufferedFrames_.empty()) return false;
    }
}

} // namespace subtitle_remover