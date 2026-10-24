#include "core/BayerPatternAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace seeingforge {

BayerPatternAnalysis BayerPatternAnalyzer::analyze(
    const QImage &frame, std::uint32_t declaredColorId)
{
    BayerPatternAnalysis result;
    result.declaredColorId = declaredColorId;
    result.suggestedColorId = declaredColorId;
    if (declaredColorId < 8 || declaredColorId > 11 || frame.isNull()
        || frame.width() < 16 || frame.height() < 16) {
        return result;
    }

    const QImage gray = frame.format() == QImage::Format_Grayscale8
        ? frame : frame.convertToFormat(QImage::Format_Grayscale8);
    std::uint64_t diagonalDifference = 0;
    std::uint64_t offDiagonalDifference = 0;
    std::array<std::uint64_t, 4> sums{};
    std::uint64_t samples = 0;
    for (int y = 0; y + 1 < gray.height(); y += 2) {
        const uchar *top = gray.constScanLine(y);
        const uchar *bottom = gray.constScanLine(y + 1);
        for (int x = 0; x + 1 < gray.width(); x += 2) {
            const std::array<int, 4> values{
                top[x], top[x + 1], bottom[x], bottom[x + 1]};
            const auto [minimum, maximum] = std::minmax_element(values.begin(), values.end());
            if (*maximum < 12 || *minimum > 250) continue;
            diagonalDifference += static_cast<std::uint64_t>(std::abs(values[0] - values[3]));
            offDiagonalDifference += static_cast<std::uint64_t>(std::abs(values[1] - values[2]));
            for (std::size_t index = 0; index < values.size(); ++index)
                sums[index] += static_cast<std::uint64_t>(values[index]);
            ++samples;
        }
    }
    if (samples < 1024) return result;

    const double sampleCount = static_cast<double>(samples);
    const double diagonal = static_cast<double>(diagonalDifference) / sampleCount;
    const double offDiagonal = static_cast<double>(offDiagonalDifference) / sampleCount;
    const bool detectedDiagonalGreens = diagonal < offDiagonal;
    const bool declaredDiagonalGreens = declaredColorId == 9 || declaredColorId == 10;
    const double lower = std::min(diagonal, offDiagonal);
    const double higher = std::max(diagonal, offDiagonal);
    result.confidence = higher > 0.0 ? 1.0 - lower / higher : 0.0;
    if (detectedDiagonalGreens == declaredDiagonalGreens || result.confidence < 0.35)
        return result;

    const auto mean = [&sums, sampleCount](std::size_t index) {
        return static_cast<double>(sums[index]) / sampleCount;
    };
    if (detectedDiagonalGreens) {
        result.suggestedColorId = mean(1) >= mean(2) ? 9U : 10U;
    } else {
        result.suggestedColorId = mean(0) >= mean(3) ? 8U : 11U;
    }
    result.discrepancy = true;
    return result;
}

std::array<double, 3> BayerPatternAnalyzer::neutralBalanceGains(
    const QImage &frame, std::uint32_t colorId, const QRect &target)
{
    std::array<double, 3> gains{1.0, 1.0, 1.0};
    if (colorId < 8 || colorId > 11 || frame.isNull()) return gains;

    const QImage gray = frame.format() == QImage::Format_Grayscale8
        ? frame : frame.convertToFormat(QImage::Format_Grayscale8);
    QRect area = target.isValid() ? target.intersected(gray.rect()) : gray.rect();
    if (area.width() < 2 || area.height() < 2) return gains;

    int brightest = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = area.left(); x <= area.right(); ++x)
            brightest = std::max(brightest, static_cast<int>(line[x]));
    }
    const int foregroundFloor = std::max(8, brightest / 8);
    std::array<std::uint64_t, 3> sums{};
    std::array<std::uint64_t, 3> counts{};
    const int firstX = area.left() + (area.left() & 1);
    const int firstY = area.top() + (area.top() & 1);
    for (int y = firstY; y + 1 <= area.bottom(); y += 2) {
        for (int x = firstX; x + 1 <= area.right(); x += 2) {
            const std::array<int, 4> values{
                gray.constScanLine(y)[x], gray.constScanLine(y)[x + 1],
                gray.constScanLine(y + 1)[x], gray.constScanLine(y + 1)[x + 1]};
            if (*std::max_element(values.begin(), values.end()) < foregroundFloor) continue;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const int sampleX = x + dx;
                    const int sampleY = y + dy;
                    int channel = 1;
                    const bool evenX = (sampleX & 1) == 0;
                    const bool evenY = (sampleY & 1) == 0;
                    if ((colorId == 8 && evenX && evenY)
                        || (colorId == 9 && !evenX && evenY)
                        || (colorId == 10 && evenX && !evenY)
                        || (colorId == 11 && !evenX && !evenY)) channel = 0;
                    else if ((colorId == 8 && !evenX && !evenY)
                             || (colorId == 9 && evenX && !evenY)
                             || (colorId == 10 && !evenX && evenY)
                             || (colorId == 11 && evenX && evenY)) channel = 2;
                    const int value = gray.constScanLine(sampleY)[sampleX];
                    sums[static_cast<std::size_t>(channel)]
                        += static_cast<std::uint64_t>(value);
                    ++counts[static_cast<std::size_t>(channel)];
                }
            }
        }
    }
    if (std::ranges::any_of(counts, [](std::uint64_t count) { return count == 0; }))
        return gains;
    std::array<double, 3> means{};
    for (std::size_t channel = 0; channel < means.size(); ++channel)
        means[channel] = static_cast<double>(sums[channel])
            / static_cast<double>(counts[channel]);
    const double weakest = *std::min_element(means.begin(), means.end());
    if (weakest <= 0.0) return gains;
    for (std::size_t channel = 0; channel < gains.size(); ++channel)
        gains[channel] = std::clamp(weakest / means[channel], 0.1, 1.0);
    return gains;
}

} // namespace seeingforge
