#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <cstdint>
#include <array>

namespace seeingforge {

struct SerHeader;

class FrameSource {
public:
    virtual ~FrameSource() = default;

    [[nodiscard]] virtual bool isOpen() const = 0;
    [[nodiscard]] virtual QString path() const = 0;
    [[nodiscard]] virtual QString displayName() const = 0;
    [[nodiscard]] virtual QString sourceDirectory() const = 0;
    [[nodiscard]] virtual const SerHeader &header() const = 0;
    [[nodiscard]] virtual QImage readFrame(std::uint32_t index) const = 0;
    [[nodiscard]] virtual QByteArray readRawFrame(std::uint32_t index) const = 0;
    [[nodiscard]] virtual QImage decodeFrame(const QByteArray &rawFrame) const = 0;
    [[nodiscard]] virtual std::uint64_t frameTimestamp(std::uint32_t index) const = 0;
    [[nodiscard]] virtual std::array<double, 3> cameraWhiteBalance() const
    {
        return {1.0, 1.0, 1.0};
    }
    [[nodiscard]] virtual bool hasCameraWhiteBalance() const { return false; }
};

} // namespace seeingforge
