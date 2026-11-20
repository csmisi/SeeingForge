#include "core/HotPixelDetector.h"

#include "core/FrameSource.h"
#include "core/SerFile.h"

#include <QImage>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

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
    QVector<int> hits(candidates.size(), 1);
    std::atomic<int> sampledFrames{1};

    const qsizetype remainingSamples = indices.size() - 1;
    if (remainingSamples > 0) {
        const unsigned hwThreads = std::thread::hardware_concurrency();
        const std::size_t workerCount = remainingSamples == 1
            ? 1U
            : std::min<std::size_t>(
                  static_cast<std::size_t>(remainingSamples),
                  std::clamp<std::size_t>(hwThreads == 0 ? 4U : hwThreads, 1U, 6U));
        std::vector<std::vector<int>> threadHits(
            workerCount, std::vector<int>(static_cast<std::size_t>(candidates.size()), 0));
        std::atomic<qsizetype> nextSample{1};

        auto worker = [&](std::size_t workerIndex) {
            std::vector<int> &localHits = threadHits[workerIndex];
            for (;;) {
                const qsizetype sample = nextSample.fetch_add(1);
                if (sample >= indices.size()) break;
                const QImage frame = file.readFrame(indices[sample]);
                if (frame.isNull()) continue;
                sampledFrames.fetch_add(1);
                for (qsizetype candidate = 0; candidate < candidates.size(); ++candidate) {
                    const QPoint point = candidates[candidate];
                    if (isLocalHotPixel(frame, point.x(), point.y(), settings.minimumBrightness,
                                        settings.localContrast)) {
                        ++localHits[static_cast<std::size_t>(candidate)];
                    }
                }
            }
        };

        if (workerCount <= 1) {
            worker(0);
        } else {
            std::vector<std::thread> threads;
            threads.reserve(workerCount);
            for (std::size_t t = 0; t < workerCount; ++t) {
                threads.emplace_back(worker, t);
            }
            for (auto &thread : threads) {
                thread.join();
            }
        }

        for (const auto &localHits : threadHits) {
            for (qsizetype candidate = 0; candidate < candidates.size(); ++candidate) {
                hits[candidate] += localHits[static_cast<std::size_t>(candidate)];
            }
        }
    }

    result.sampledFrames = sampledFrames.load();

    const int requiredHits = static_cast<int>(std::ceil(
        result.sampledFrames * std::clamp(settings.persistencePercent, 1, 100) / 100.0));
    for (qsizetype candidate = 0; candidate < candidates.size(); ++candidate) {
        if (hits[candidate] >= requiredHits) result.hotPixels.append(candidates[candidate]);
    }
    return result;
}

} // namespace seeingforge
