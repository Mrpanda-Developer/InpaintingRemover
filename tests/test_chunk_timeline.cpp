#include "subtitle_remover/chunk_reader.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    using subtitle_remover::ChunkSchedule;
    const ChunkSchedule schedule(8.0, 0.5, 2.0);
    const auto first = schedule.window(0);
    const auto second = schedule.window(1);

    require(first.centralStartSeconds == 2.0, "first chunk start mismatch");
    require(first.centralEndSeconds == 10.0, "first chunk end mismatch");
    require(first.contextStartSeconds == 2.0, "first context start mismatch");
    require(first.contextEndSeconds == 10.5, "first context end mismatch");
    require(second.centralStartSeconds == 10.0, "second chunk start mismatch");
    require(second.centralEndSeconds == 18.0, "second chunk end mismatch");
    require(second.contextStartSeconds == 9.5, "second context start mismatch");
    require(second.contextEndSeconds == 18.5, "second context end mismatch");
    require(schedule.containsCentral(first, 2.0), "central interval must include its start");
    require(!schedule.containsCentral(first, 10.0), "central interval must exclude its end");
    require(schedule.containsCentral(second, 10.0), "next interval must own the boundary frame");

    constexpr double frameStep = 1.0 / 25.0;
    int firstChunkFrames = 0;
    int secondChunkFrames = 0;
    for (int frame = 0; frame <= 500; ++frame) {
        const double ptsSeconds = static_cast<double>(frame) * frameStep;
        firstChunkFrames += schedule.containsCentral(first, ptsSeconds) ? 1 : 0;
        secondChunkFrames += schedule.containsCentral(second, ptsSeconds) ? 1 : 0;
    }
        require(firstChunkFrames == 200, "8-second 25-fps chunk should contain 200 frames");
        require(secondChunkFrames == 200, "adjacent chunk should contain 200 frames");
        require(std::abs(first.centralEndSeconds - second.centralStartSeconds) < 1e-12,
            "central chunk intervals must be contiguous");

    const AVRational sourceTimeBase{1, 90000};
    const std::int64_t sourcePts = 123456;
    const double timestamp = sourcePts * av_q2d(sourceTimeBase);
        require(std::abs(timestamp - 1.3717333333333333) < 1e-12, "PTS-to-seconds conversion mismatch");
        require(av_rescale_q(sourcePts, sourceTimeBase, sourceTimeBase) == sourcePts,
            "same-time-base rescale must preserve PTS exactly");

    std::cout << "chunk boundary and timestamp assertions passed\n";
}