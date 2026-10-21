#pragma once

#include <QImage>
#include <QVector>

#include <cstdint>
#include <functional>

namespace seeingforge {

class SerFile;

struct FrameQuality {
    std::uint32_t frameIndex = 0;
    double sharpness = 0.0;
    double contrast = 0.0;
    double meanBrightness = 0.0;
    double clippedPercent = 0.0;
    double score = 0.0;
};

struct QualityResult {
    QVector<FrameQuality> frames;
    QVector<std::uint32_t> rankedFrameIndices;
    bool canceled = false;

    [[nodiscard]] bool isEmpty() const { return frames.isEmpty(); }
};

class QualityAnalyzer {
public:
    [[nodiscard]] static FrameQuality measureFrame(const QImage &frame,
                                                   std::uint32_t frameIndex,
                                                   int backgroundThreshold = 12);
    [[nodiscard]] static QualityResult analyze(const SerFile &file,
        int backgroundThreshold = 12,
        const std::function<void(int, int)> &progress = {},
        const std::function<bool()> &shouldCancel = {});
};

} // namespace seeingforge
