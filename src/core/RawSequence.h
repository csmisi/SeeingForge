#pragma once

#include "core/FrameSource.h"
#include "core/SerFile.h"

#include <QStringList>
#include <QVector>

#include <functional>

namespace seeingforge {

class RawSequence final : public FrameSource {
public:
    [[nodiscard]] bool open(const QStringList &paths,
                            const std::function<void(int, int)> &progress = {},
                            const std::function<bool()> &shouldCancel = {});
    void close();

    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] QString path() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString sourceDirectory() const override;
    [[nodiscard]] const SerHeader &header() const override;
    [[nodiscard]] const QString &errorString() const;
    [[nodiscard]] const QStringList &files() const;

    [[nodiscard]] QImage readFrame(std::uint32_t index) const override;
    [[nodiscard]] QByteArray readRawFrame(std::uint32_t index) const override;
    [[nodiscard]] QImage decodeFrame(const QByteArray &rawFrame) const override;
    [[nodiscard]] std::uint64_t frameTimestamp(std::uint32_t index) const override;
    [[nodiscard]] std::array<double, 3> cameraWhiteBalance() const override;
    [[nodiscard]] bool hasCameraWhiteBalance() const override;

private:
    QStringList m_paths;
    QVector<std::uint64_t> m_timestamps;
    SerHeader m_header;
    QString m_error;
    std::array<double, 3> m_cameraWhiteBalance{1.0, 1.0, 1.0};
};

} // namespace seeingforge
