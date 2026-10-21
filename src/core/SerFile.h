#pragma once

#include <QImage>
#include <QByteArray>
#include <QString>

#include <cstdint>

namespace seeingforge {

struct SerHeader {
    QString fileId;
    std::uint32_t cameraId = 0;
    std::uint32_t colorId = 0;
    std::uint32_t endianFlag = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pixelDepth = 0;
    std::uint32_t frameCount = 0;
    QString observer;
    QString instrument;
    QString telescope;
    std::uint64_t dateTimeLocal = 0;
    std::uint64_t dateTimeUtc = 0;

    [[nodiscard]] QString colorName() const;
    [[nodiscard]] int planeCount() const;
    [[nodiscard]] std::uint64_t bytesPerFrame() const;
};

class SerFile {
public:
    static constexpr std::uint64_t HeaderSize = 178;

    [[nodiscard]] bool open(const QString &path);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] const QString &path() const;
    [[nodiscard]] const SerHeader &header() const;
    [[nodiscard]] const QString &errorString() const;

    [[nodiscard]] QImage readFrame(std::uint32_t index) const;
    [[nodiscard]] QByteArray readRawFrame(std::uint32_t index) const;
    [[nodiscard]] QImage decodeFrame(const QByteArray &rawFrame) const;

private:
    QString m_path;
    SerHeader m_header;
    QString m_error;
    bool m_isOpen = false;
};

} // namespace seeingforge
