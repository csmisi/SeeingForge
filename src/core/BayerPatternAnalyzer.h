#pragma once

#include <QImage>
#include <QRect>

#include <array>
#include <cstdint>

namespace seeingforge {

struct BayerPatternAnalysis {
    std::uint32_t declaredColorId = 0;
    std::uint32_t suggestedColorId = 0;
    double confidence = 0.0;
    bool discrepancy = false;
};

class BayerPatternAnalyzer {
public:
    [[nodiscard]] static BayerPatternAnalysis analyze(
        const QImage &frame, std::uint32_t declaredColorId);
    [[nodiscard]] static std::array<double, 3> neutralBalanceGains(
        const QImage &frame, std::uint32_t colorId, const QRect &target = {});
};

} // namespace seeingforge
