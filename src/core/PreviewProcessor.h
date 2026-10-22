#pragma once

#include <QImage>

#include <cstdint>

namespace seeingforge {

struct PreviewSettings {
    bool debayer = true;
    bool autoStretch = true;
    bool whiteBalance = true;
    double redBalance = 1.0;
    double blueBalance = 1.0;
    double saturation = 1.0;
    int hueShiftDegrees = 0;
    double gain = 1.0;
    double gamma = 1.0;
    int rotationQuarterTurns = 0;
    bool flipHorizontal = false;
    bool flipVertical = false;
    int maxPreviewDimension = 1400;
};

struct PreviewImages {
    QImage rawMosaic;
    QImage rawLuminance;
    QImage processed;
};

class PreviewProcessor {
public:
    [[nodiscard]] static PreviewImages render(const QImage &rawFrame,
                                              std::uint32_t colorId,
                                              const PreviewSettings &settings);
    [[nodiscard]] static bool supportsDebayer(std::uint32_t colorId);
};

} // namespace seeingforge
