#pragma once

#include <QPoint>
#include <QVector>

namespace seeingforge {

class FrameSource;

struct HotPixelSettings {
    int sampleCount = 16;
    int minimumBrightness = 200;
    int localContrast = 30;
    int persistencePercent = 80;
};

struct HotPixelResult {
    QVector<QPoint> hotPixels;
    int sampledFrames = 0;
    int initialCandidates = 0;
};

class HotPixelDetector {
public:
    [[nodiscard]] static HotPixelResult detect(const FrameSource &file,
                                               const HotPixelSettings &settings);
};

} // namespace seeingforge
