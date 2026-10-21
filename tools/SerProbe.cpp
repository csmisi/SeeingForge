#include "core/SerFile.h"
#include "core/PreviewProcessor.h"
#include "core/ObjectDetector.h"
#include "core/HotPixelDetector.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    QTextStream error(stderr);

    if (application.arguments().size() < 2 || application.arguments().size() > 3) {
        error << "Usage: seeingforge_ser_probe <recording.ser> [preview.png]\n";
        return 2;
    }

    seeingforge::SerFile ser;
    if (!ser.open(application.arguments().at(1))) {
        error << "Error: " << ser.errorString() << '\n';
        return 1;
    }

    const auto &header = ser.header();
    output << "Identifier: " << header.fileId << '\n'
           << "Dimensions: " << header.width << " x " << header.height << '\n'
           << "Frames: " << header.frameCount << '\n'
           << "Pixel depth: " << header.pixelDepth << '\n'
           << "Colour: " << header.colorName() << '\n'
           << "Frame bytes: " << header.bytesPerFrame() << '\n';

    const QImage firstFrame = ser.readFrame(0);
    if (firstFrame.isNull()) {
        error << "Error: the first frame could not be decoded.\n";
        return 1;
    }
    output << "First frame: decoded successfully\n";
    QElapsedTimer timer;
    timer.start();
    const auto previews = seeingforge::PreviewProcessor::render(
        firstFrame, header.colorId, seeingforge::PreviewSettings{});
    if (previews.processed.isNull()) {
        error << "Error: the processed preview could not be rendered.\n";
        return 1;
    }
    output << "Processed preview: " << previews.processed.width() << " x "
           << previews.processed.height() << " in " << timer.elapsed() << " ms\n";
    if (application.arguments().size() == 3
        && !previews.processed.save(application.arguments().at(2))) {
        error << "Error: could not save preview image.\n";
        return 1;
    }
    timer.restart();
    const auto detection = seeingforge::ObjectDetector::detect(firstFrame, 16, 20);
    if (detection.found()) {
        output << "Detected object: " << detection.objectRect.width() << " x "
               << detection.objectRect.height() << "; crop: "
               << detection.cropRect.width() << " x " << detection.cropRect.height()
               << " in " << timer.elapsed() << " ms\n";
    } else {
        output << "Detected object: none at threshold 16\n";
    }
    timer.restart();
    const auto hotPixels = seeingforge::HotPixelDetector::detect(
        ser, seeingforge::HotPixelSettings{});
    output << "Hot pixels: " << hotPixels.hotPixels.size()
           << " from " << hotPixels.initialCandidates
           << " initial candidates across " << hotPixels.sampledFrames
           << " frames in " << timer.elapsed() << " ms\n";
    return 0;
}
