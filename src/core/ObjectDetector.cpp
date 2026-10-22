#include "core/ObjectDetector.h"

#include <QVector>

#include <algorithm>

namespace seeingforge {

DetectionResult ObjectDetector::detect(const QImage &frame, int threshold,
                                       int marginPercent, int analysisMaxDimension)
{
    if (frame.isNull()) return {};
    QImage gray = frame.convertToFormat(QImage::Format_Grayscale8);
    if (std::max(gray.width(), gray.height()) > analysisMaxDimension) {
        gray = gray.scaled(analysisMaxDimension, analysisMaxDimension,
                           Qt::KeepAspectRatio, Qt::FastTransformation);
    }

    const int width = gray.width();
    const int height = gray.height();
    QVector<quint8> visited(width * height, 0);
    QVector<int> queue;
    QRect best;
    int bestArea = 0;
    constexpr int minimumComponentArea = 9;

    for (int y = 0; y < height; ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const int start = y * width + x;
            if (visited[start] || line[x] < threshold) continue;
            visited[start] = 1;
            queue.clear();
            queue.append(start);
            int head = 0;
            int area = 0;
            int minX = x, maxX = x, minY = y, maxY = y;
            while (head < queue.size()) {
                const int index = queue[head++];
                const int currentX = index % width;
                const int currentY = index / width;
                ++area;
                minX = std::min(minX, currentX);
                maxX = std::max(maxX, currentX);
                minY = std::min(minY, currentY);
                maxY = std::max(maxY, currentY);
                constexpr int dx[] = {-1, 1, 0, 0};
                constexpr int dy[] = {0, 0, -1, 1};
                for (int direction = 0; direction < 4; ++direction) {
                    const int nextX = currentX + dx[direction];
                    const int nextY = currentY + dy[direction];
                    if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) continue;
                    const int next = nextY * width + nextX;
                    if (visited[next] || gray.constScanLine(nextY)[nextX] < threshold) continue;
                    visited[next] = 1;
                    queue.append(next);
                }
            }
            if (area >= minimumComponentArea && area > bestArea) {
                bestArea = area;
                best = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
            }
        }
    }

    if (!best.isValid()) return {};
    const double scaleX = static_cast<double>(frame.width()) / width;
    const double scaleY = static_cast<double>(frame.height()) / height;
    QRect objectRect(
        static_cast<int>(best.left() * scaleX),
        static_cast<int>(best.top() * scaleY),
        std::max(1, static_cast<int>(best.width() * scaleX)),
        std::max(1, static_cast<int>(best.height() * scaleY)));
    objectRect = objectRect.intersected(frame.rect());

    if (marginPercent < 0)
        return {objectRect, frame.rect(), bestArea};

    const int marginX = qRound(objectRect.width() * marginPercent / 100.0);
    const int marginY = qRound(objectRect.height() * marginPercent / 100.0);
    const QRect cropRect = objectRect.adjusted(-marginX, -marginY, marginX, marginY)
                               .intersected(frame.rect());
    return {objectRect, cropRect, bestArea};
}

} // namespace seeingforge
