#pragma once

#include "subtitle_remover/video_reader.hpp"

#include <deque>

namespace subtitle_remover {

class ChunkSchedule {
public:
    ChunkSchedule(double durationSeconds, double overlapSeconds, double originSeconds);
    ChunkWindow window(std::size_t sequence) const;
    bool containsCentral(const ChunkWindow& window, double timestampSeconds) const noexcept;

private:
    double durationSeconds_;
    double overlapSeconds_;
    double originSeconds_;
};

class ChunkReader {
public:
    ChunkReader(VideoReader& reader, double durationSeconds, double overlapSeconds);
    bool next(ChunkWindow& output);

private:
    bool fillThrough(double timestampSeconds);

    VideoReader& reader_;
    ChunkSchedule schedule_;
    double durationSeconds_;
    double overlapSeconds_;
    std::deque<VideoFrame> bufferedFrames_;
    std::size_t sequence_ = 0;
    bool exhausted_ = false;
    bool scheduleReady_ = false;
};

} // namespace subtitle_remover