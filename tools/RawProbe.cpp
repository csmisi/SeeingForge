#include "core/RawSequence.h"
#include "core/PreviewProcessor.h"

#include <QCoreApplication>
#include <QTextStream>

#include <algorithm>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() < 2) return 2;
    seeingforge::RawSequence sequence;
    if (!sequence.open(application.arguments().mid(1))) {
        QTextStream(stderr) << sequence.errorString() << Qt::endl;
        return 1;
    }
    const auto &header = sequence.header();
    QTextStream(stderr) << sequence.displayName() << ' ' << header.width << ' ' << header.height
                        << ' ' << header.colorName() << ' ' << header.instrument << Qt::endl;
    const auto whiteBalance = sequence.cameraWhiteBalance();
    QTextStream(stderr) << "camera white balance " << whiteBalance[0] << ' '
                        << whiteBalance[1] << ' ' << whiteBalance[2] << Qt::endl;
    const QByteArray raw = sequence.readRawFrame(0);
    const QImage image = sequence.decodeFrame(raw);
    int minimum = 255;
    int maximum = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            minimum = std::min(minimum, static_cast<int>(line[x]));
            maximum = std::max(maximum, static_cast<int>(line[x]));
        }
    }
    QTextStream(stderr) << "raw bytes " << raw.size() << " image "
                        << image.width() << 'x' << image.height()
                        << " display range " << minimum << ".." << maximum << Qt::endl;
    seeingforge::PreviewSettings settings;
    settings.debayer = true;
    settings.autoStretch = true;
    const auto preview = seeingforge::PreviewProcessor::render(
        image, header.colorId, settings);
    QTextStream(stderr) << "preview " << preview.processed.width() << 'x'
                        << preview.processed.height() << Qt::endl;
    return image.isNull() ? 1 : 0;
}
