#pragma once

#include <QImage>
#include <QRect>

namespace seeingforge {

struct DetectionResult {
    QRect objectRect;
    QRect cropRect;
    int detectedArea = 0;

    [[nodiscard]] bool found() const { return objectRect.isValid(); }
};

class ObjectDetector {
public:
    [[nodiscard]] static DetectionResult detect(const QImage &frame,
                                                int threshold,
                                                int marginPercent,
                                                int analysisMaxDimension = 900);
};

} // namespace seeingforge
