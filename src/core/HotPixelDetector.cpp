#include "core/HotPixelDetector.h"

#include "core/FrameSource.h"
#include "core/SerFile.h"

#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>

namespace seeingforge {
namespace {

bool isLocalHotPixel(const QImage &frame, int x, int y,
                     int minimumBrightness, int localContrast)
{
    const int value = frame.constScanLine(y)[x];
    if (value < minimumBrightness) return false;
    std::array<int, 8> neighbors{
        frame.constScanLine(y)[x - 2], frame.constScanLine(y)[x + 2],
        frame.constScanLine(y - 2)[x], frame.constScanLine(y + 2)[x],
        frame.constScanLine(y - 2)[x - 2], frame.constScanLine(y - 2)[x + 2],
        frame.constScanLine(y + 2)[x - 2], frame.constScanLine(y + 2)[x + 2]};
    std::nth_element(neighbors.begin(), neighbors.begin() + 4, neighbors.end());
    return value - neighbors[4] >= localContrast;
}

} // namespace

HotPixelResult HotPixelDetector::detect(const FrameSource &file,
                                        const HotPixelSettings &settings)
{
    HotPixelResult result;
    if (!file.isOpen() || file.header().frameCount == 0) return result;
    const int samples = std::clamp(settings.sampleCount, 2,
                                   static_cast<int>(file.header().frameCount));
    QVector<std::uint32_t> indices;
    indices.reserve(samples);
    for (int sample = 0; sample < samples; ++sample) {
        indices.append(static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(sample) * (file.header().frameCount - 1U))
            / static_cast<std::uint64_t>(samples - 1)));
    }

    const QImage first = file.readFrame(indices.front());
    if (first.isNull()) return result;
    QVector<QPoint> candidates;
    for (int y = 2; y < first.height() - 2; ++y) {
        for (int x = 2; x < first.width() - 2; ++x) {
            if (isLocalHotPixel(first, x, y, settings.minimumBrightness,
                                settings.localContrast)) {
                candidates.append(QPoint(x, y));
            }
        }
    }
    result.initialCandidates = static_cast<int>(candidates.size());
    QVector<quint8> hits(candidates.size(), 1);
    result.sampledFrames = 1;

    for (int sample = 1; sample < indices.size(); ++sample) {
        const QImage frame = file.readFrame(indices[sample]);
        if (frame.isNull()) continue;
        ++result.sampledFrames;
        for (qsizetype candidate = 0; candidate < candidates.size(); ++candidate) {
            const QPoint point = candidates[candidate];
            if (isLocalHotPixel(frame, point.x(), point.y(), settings.minimumBrightness,
                                settings.localContrast)) {
                ++hits[candidate];
            }
        }
    }

    const int requiredHits = static_cast<int>(std::ceil(
        result.sampledFrames * std::clamp(settings.persistencePercent, 1, 100) / 100.0));
    for (qsizetype candidate = 0; candidate < candidates.size(); ++candidate) {
        if (hits[candidate] >= requiredHits) result.hotPixels.append(candidates[candidate]);
    }
    return result;
}

} // namespace seeingforge
