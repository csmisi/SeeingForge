#include "app/MainWindow.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QCollator>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProgressBar>
#include <QPromise>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {

QStringList rawExtensionFilters()
{
    return {QStringLiteral("*.cr2"), QStringLiteral("*.CR2"),
            QStringLiteral("*.cr3"), QStringLiteral("*.CR3"),
            QStringLiteral("*.nef"), QStringLiteral("*.NEF"),
            QStringLiteral("*.arw"), QStringLiteral("*.ARW"),
            QStringLiteral("*.dng"), QStringLiteral("*.DNG"),
            QStringLiteral("*.raf"), QStringLiteral("*.RAF"),
            QStringLiteral("*.orf"), QStringLiteral("*.ORF"),
            QStringLiteral("*.rw2"), QStringLiteral("*.RW2")};
}

bool isRawFilePath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("cr2") || suffix == QLatin1String("cr3")
        || suffix == QLatin1String("nef") || suffix == QLatin1String("arw")
        || suffix == QLatin1String("dng") || suffix == QLatin1String("raf")
        || suffix == QLatin1String("orf") || suffix == QLatin1String("rw2");
}

QSize exportCropSize(QSize source, const QRect &object, int margin, QSize requested)
{
    QSize size = source;
    if (object.isValid() && margin >= 0) {
        size = QSize(object.width() + 2 * qRound(object.width() * margin / 100.0),
                     object.height() + 2 * qRound(object.height() * margin / 100.0));
    }
    if (requested.isValid()) size = requested;
    size = size.boundedTo(source);
    const auto alignUpToFour = [](int value) {
        return std::max(4, (value + 3) & ~3);
    };
    return QSize(alignUpToFour(size.width()), alignUpToFour(size.height()));
}

QPixmap createAppEmblemPixmap(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0, 0, size, size);
    bg.setColorAt(0.0, QColor(QStringLiteral("#4b2b54")));
    bg.setColorAt(1.0, QColor(QStringLiteral("#2a3d56")));
    painter.setBrush(bg);
    painter.setPen(QPen(QColor(QStringLiteral("#7c4f87")), 1.5));
    painter.drawRoundedRect(QRectF(1.0, 1.0, size - 2.0, size - 2.0), 10.0, 10.0);

    const double c = size / 2.0;
    const double r = size * 0.27;
    painter.setPen(QPen(QColor(QStringLiteral("#e3b9ec")), 1.8));
    painter.setBrush(QColor(227, 185, 236, 48));
    painter.drawEllipse(QPointF(c, c), r, r);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#f0c64f")));
    painter.drawEllipse(QPointF(c - r * 0.28, c - r * 0.2), 1.7, 1.7);
    painter.setBrush(QColor(QStringLiteral("#52d273")));
    const QPointF star(size * 0.76, size * 0.24);
    painter.drawEllipse(star, 2.2, 2.2);
    return pixmap;
}

QPixmap createStepIconPixmap(int step, int size = 38)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    struct StepVisual {
        const char *bgTop;
        const char *bgBot;
        const char *border;
        const char *stroke;
    };
    static constexpr std::array<StepVisual, 5> visuals{{
        {"#2d3e54", "#1e2836", "#44566e", "#8ec5ee"},
        {"#3c3424", "#26221b", "#5a4b34", "#f0c64f"},
        {"#4b2b54", "#2c1e33", "#8a5296", "#e3b9ec"},
        {"#1d3c2b", "#16291f", "#2f6b47", "#52d273"},
        {"#214563", "#183047", "#387fb5", "#8ec5ee"},
    }};
    const auto &vis = visuals[static_cast<std::size_t>(std::clamp(step, 0, 4))];
    QLinearGradient bg(0, 0, size, size);
    bg.setColorAt(0.0, QColor(QLatin1String(vis.bgTop)));
    bg.setColorAt(1.0, QColor(QLatin1String(vis.bgBot)));
    painter.setBrush(bg);
    painter.setPen(QPen(QColor(QLatin1String(vis.border)), 1.2));
    painter.drawRoundedRect(QRectF(1.0, 1.0, size - 2.0, size - 2.0), 10.0, 10.0);

    painter.setPen(QPen(QColor(QLatin1String(vis.stroke)), 2.0, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const double s = size;
    if (step == 0) {
        painter.drawRoundedRect(QRectF(s * 0.24, s * 0.30, s * 0.52, s * 0.40), 2.5, 2.5);
        painter.drawEllipse(QPointF(s * 0.50, s * 0.50), s * 0.12, s * 0.12);
    } else if (step == 1) {
        painter.drawEllipse(QPointF(s * 0.50, s * 0.50), s * 0.13, s * 0.13);
        painter.drawLine(QPointF(s * 0.50, s * 0.22), QPointF(s * 0.50, s * 0.30));
        painter.drawLine(QPointF(s * 0.50, s * 0.70), QPointF(s * 0.50, s * 0.78));
        painter.drawLine(QPointF(s * 0.22, s * 0.50), QPointF(s * 0.30, s * 0.50));
        painter.drawLine(QPointF(s * 0.70, s * 0.50), QPointF(s * 0.78, s * 0.50));
    } else if (step == 2) {
        painter.drawLine(QPointF(s * 0.32, s * 0.22), QPointF(s * 0.32, s * 0.68));
        painter.drawLine(QPointF(s * 0.32, s * 0.68), QPointF(s * 0.78, s * 0.68));
        painter.drawLine(QPointF(s * 0.22, s * 0.32), QPointF(s * 0.68, s * 0.32));
        painter.drawLine(QPointF(s * 0.68, s * 0.32), QPointF(s * 0.68, s * 0.78));
    } else if (step == 3) {
        QPainterPath wave;
        wave.moveTo(s * 0.20, s * 0.55);
        wave.lineTo(s * 0.34, s * 0.55);
        wave.lineTo(s * 0.43, s * 0.28);
        wave.lineTo(s * 0.55, s * 0.72);
        wave.lineTo(s * 0.65, s * 0.45);
        wave.lineTo(s * 0.80, s * 0.45);
        painter.drawPath(wave);
    } else {
        painter.drawLine(QPointF(s * 0.50, s * 0.24), QPointF(s * 0.50, s * 0.60));
        painter.drawLine(QPointF(s * 0.36, s * 0.48), QPointF(s * 0.50, s * 0.62));
        painter.drawLine(QPointF(s * 0.64, s * 0.48), QPointF(s * 0.50, s * 0.62));
        painter.drawLine(QPointF(s * 0.26, s * 0.74), QPointF(s * 0.74, s * 0.74));
    }
    return pixmap;
}

QFrame *makeCard(QWidget *parent, int padH = 14, int padV = 12)
{
    auto *card = new QFrame(parent);
    card->setProperty("sfRole", QStringLiteral("card"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(padH, padV, padH, padV);
    layout->setSpacing(8);
    return card;
}

QFrame *makeMetricTile(const QString &title, const QString &initialValue,
                       const QString &subtitle, const QString &valueColor,
                       QLabel **outValueLabel, QLabel **outSubLabel, QWidget *parent)
{
    auto *tile = new QFrame(parent);
    tile->setProperty("sfRole", QStringLiteral("tile"));
    tile->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(2);
    auto *titleLabel = new QLabel(title, tile);
    titleLabel->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px; font-weight: 600;"));
    auto *valueLabel = new QLabel(initialValue, tile);
    valueLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 16px; font-weight: 700; font-family: monospace;")
                                  .arg(valueColor));
    auto *subLabel = new QLabel(subtitle, tile);
    subLabel->setStyleSheet(QStringLiteral("color: #76808e; font-size: 11px;"));
    subLabel->setWordWrap(true);
    layout->addWidget(titleLabel);
    layout->addWidget(valueLabel);
    layout->addWidget(subLabel);
    if (outValueLabel) *outValueLabel = valueLabel;
    if (outSubLabel) *outSubLabel = subLabel;
    return tile;
}

QLabel *makeBadge(const QString &text, const QString &bg, const QString &border,
                  const QString &fg, QWidget *parent)
{
    auto *badge = new QLabel(text, parent);
    badge->setAlignment(Qt::AlignCenter);
    badge->setStyleSheet(QStringLiteral(
        "background: %1; border: 1px solid %2; color: %3; "
        "border-radius: 9px; padding: 3px 9px; font-size: 10px; font-weight: 700;")
                             .arg(bg, border, fg));
    return badge;
}

QFrame *makeStepHeaderCard(int step, const QString &title, const QString &badgeText,
                           const QString &badgeBg, const QString &badgeBorder,
                           const QString &badgeFg, const QString &description,
                           QLabel **outBadge, QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setProperty("sfRole", QStringLiteral("card"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(12, 11, 12, 11);
    row->setSpacing(11);

    auto *iconLabel = new QLabel(card);
    iconLabel->setPixmap(createStepIconPixmap(step, 38));
    iconLabel->setFixedSize(38, 38);
    row->addWidget(iconLabel, 0, Qt::AlignTop);

    auto *textCol = new QVBoxLayout;
    textCol->setSpacing(3);
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(6);
    auto *titleLabel = new QLabel(title, card);
    titleLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 15px; font-weight: 700;"));
    auto *badge = makeBadge(badgeText, badgeBg, badgeBorder, badgeFg, card);
    topRow->addWidget(titleLabel);
    topRow->addStretch(1);
    topRow->addWidget(badge);
    textCol->addLayout(topRow);

    auto *descLabel = new QLabel(description, card);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QStringLiteral("color: #9ca5b3; font-size: 12px;"));
    textCol->addWidget(descLabel);
    row->addLayout(textCol, 1);

    if (outBadge) *outBadge = badge;
    return card;
}

QScrollArea *wrapScrollablePage(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setObjectName(QStringLiteral("inspectorScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setObjectName(QStringLiteral("inspectorViewport"));
    content->setObjectName(QStringLiteral("inspectorPageContent"));
    scroll->setWidget(content);
    return scroll;
}

void repolish(QWidget *widget)
{
    if (!widget || !widget->style()) return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace

class QualityGraph final : public QWidget {
public:
    std::function<void(std::uint32_t)> onFrameSelected;

    explicit QualityGraph(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(124);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tr("Relative frame quality. Click along the curve to inspect a frame."));
    }

    void setScores(QVector<double> scores, int keepCount = 0, int keepPercent = 100,
                   bool orderedByQuality = false, double cutoffScore = 0.0,
                   QVector<std::uint32_t> frameIndices = {})
    {
        m_scores = std::move(scores);
        m_keepCount = keepCount;
        m_keepPercent = keepPercent;
        m_orderedByQuality = orderedByQuality;
        m_cutoffScore = cutoffScore;
        m_frameIndices = std::move(frameIndices);
        update();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !m_frameIndices.isEmpty()
            && m_plotRect.width() > 1.0 && onFrameSelected) {
            const double x = std::clamp(event->position().x(),
                                        m_plotRect.left(), m_plotRect.right());
            const double frac = (x - m_plotRect.left()) / m_plotRect.width();
            const qsizetype index = static_cast<qsizetype>(std::clamp(
                qRound(frac * static_cast<double>(m_frameIndices.size() - 1)),
                0, static_cast<int>(m_frameIndices.size() - 1)));
            onFrameSelected(m_frameIndices.at(index));
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(QStringLiteral("#101317")));
        m_plotRect = QRectF(rect()).adjusted(34.0, 10.0, -10.0, -20.0);
        const QRectF &plot = m_plotRect;

        painter.setPen(QColor(QStringLiteral("#2b323d")));
        painter.drawRect(plot);
        painter.drawLine(QPointF(plot.left(), plot.center().y()),
                         QPointF(plot.right(), plot.center().y()));

        QFont axisFont = painter.font();
        axisFont.setPointSize(8);
        painter.setFont(axisFont);
        painter.setPen(QColor(QStringLiteral("#8d96a5")));
        painter.drawText(QRectF(2.0, plot.top() - 4.0, 29.0, 16.0),
                         Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("100"));
        painter.drawText(QRectF(2.0, plot.bottom() - 8.0, 29.0, 16.0),
                         Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0"));
        painter.drawText(QRectF(plot.left(), plot.bottom() + 2.0, plot.width(), 16.0),
                         Qt::AlignCenter,
                         m_orderedByQuality ? tr("Ranked frames (best → worst)")
                                            : tr("Capture timeline"));

        if (m_scores.isEmpty()) {
            painter.drawText(plot, Qt::AlignCenter, tr("Analyze frames to show the quality curve"));
            return;
        }

        if (m_keepCount > 0 && m_scores.size() > 1) {
            if (m_orderedByQuality) {
                const double frac = std::clamp(
                    static_cast<double>(m_keepCount) / static_cast<double>(m_scores.size()),
                    0.0, 1.0);
                const double cutX = plot.left() + frac * plot.width();
                painter.fillRect(QRectF(plot.left(), plot.top(),
                                        cutX - plot.left(), plot.height()),
                                 QColor(82, 210, 115, 22));
                QPen cutPen(QColor(QStringLiteral("#f0c64f")), 1.4, Qt::DashLine);
                painter.setPen(cutPen);
                painter.drawLine(QPointF(cutX, plot.top()), QPointF(cutX, plot.bottom()));
                painter.drawText(
                    QRectF(std::min(cutX + 4.0, plot.right() - 68.0), plot.top() + 2.0, 66.0, 16.0),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    tr("Keep %1%").arg(m_keepPercent));
            } else {
                const double cutY = plot.bottom()
                    - std::clamp(m_cutoffScore, 0.0, 100.0) / 100.0 * plot.height();
                painter.fillRect(QRectF(plot.left(), plot.top(),
                                        plot.width(), std::max(0.0, cutY - plot.top())),
                                 QColor(82, 210, 115, 18));
                QPen cutPen(QColor(QStringLiteral("#f0c64f")), 1.4, Qt::DashLine);
                painter.setPen(cutPen);
                painter.drawLine(QPointF(plot.left(), cutY), QPointF(plot.right(), cutY));
                painter.drawText(
                    QRectF(plot.right() - 78.0,
                           std::max(plot.top() + 2.0, cutY - 16.0), 74.0, 14.0),
                    Qt::AlignRight | Qt::AlignVCenter,
                    tr("Cut %1").arg(m_cutoffScore, 0, 'f', 1));
            }
        }

        QPainterPath path;
        for (qsizetype index = 0; index < m_scores.size(); ++index) {
            const double fraction = m_scores.size() > 1
                ? static_cast<double>(index) / static_cast<double>(m_scores.size() - 1) : 0.0;
            const QPointF point(plot.left() + fraction * plot.width(),
                                plot.bottom() - std::clamp(m_scores[index], 0.0, 100.0)
                                                    / 100.0 * plot.height());
            if (index == 0) path.moveTo(point);
            else path.lineTo(point);
        }
        painter.setPen(QPen(QColor(QStringLiteral("#52d273")), 2.0));
        painter.drawPath(path);
    }

private:
    QVector<double> m_scores;
    QVector<std::uint32_t> m_frameIndices;
    QRectF m_plotRect;
    int m_keepCount = 0;
    int m_keepPercent = 100;
    bool m_orderedByQuality = false;
    double m_cutoffScore = 0.0;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("SeeingForge"));
    resize(1420, 880);
    setMinimumSize(980, 650);
    setAcceptDrops(true);

    m_frameDebounce = new QTimer(this);
    m_frameDebounce->setSingleShot(true);
    m_frameDebounce->setInterval(70);
    connect(m_frameDebounce, &QTimer::timeout, this, &MainWindow::loadSelectedFrame);

    m_detectionDebounce = new QTimer(this);
    m_detectionDebounce->setSingleShot(true);
    m_detectionDebounce->setInterval(110);
    connect(m_detectionDebounce, &QTimer::timeout, this, &MainWindow::updateDetection);

    m_hotPixelDebounce = new QTimer(this);
    m_hotPixelDebounce->setSingleShot(true);
    m_hotPixelDebounce->setInterval(400);
    connect(m_hotPixelDebounce, &QTimer::timeout, this, &MainWindow::startHotPixelAnalysis);

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(100);
    connect(m_playTimer, &QTimer::timeout, this, [this] {
        if (!m_source || !m_source->isOpen() || m_source->header().frameCount <= 1) {
            togglePlayback();
            return;
        }
        const std::uint32_t next = (m_currentFrame + 1U) % m_source->header().frameCount;
        m_frameSlider->setValue(static_cast<int>(next));
        loadFrame(next);
    });

    m_statusResetTimer = new QTimer(this);
    m_statusResetTimer->setSingleShot(true);
    connect(m_statusResetTimer, &QTimer::timeout, this, [this] {
        if (m_footerStatusLabel && !m_defaultStatusMessage.isEmpty()) {
            m_footerStatusLabel->setText(m_defaultStatusMessage);
        }
    });

    applyThemeStyleSheet();
    createActions();
    createMenus();
    createWorkspace();
    statusBar()->hide();
    loadUserPreferences();
    populatePresetCombo();

    QSettings windowSettings;
    const QByteArray savedGeometry = windowSettings.value(
        QStringLiteral("window/geometry")).toByteArray();
    if (!savedGeometry.isEmpty()) restoreGeometry(savedGeometry);
    const bool startFullScreen = windowSettings.value(
        QStringLiteral("window/fullScreen"), false).toBool();
    const bool startMaximized = windowSettings.value(
        QStringLiteral("window/maximized"), false).toBool();
    QTimer::singleShot(0, this, [this, startFullScreen, startMaximized] {
        if (startFullScreen) showFullScreen();
        else if (startMaximized) showMaximized();
        positionDisplayPopover();
    });

    m_rawLoadDialog = new QDialog(this);
    m_rawLoadDialog->setWindowTitle(tr("Loading RAW Sequence"));
    m_rawLoadDialog->setWindowModality(Qt::ApplicationModal);
    m_rawLoadDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    m_rawLoadDialog->setFixedWidth(360);
    auto *rawLoadLayout = new QVBoxLayout(m_rawLoadDialog);
    m_rawLoadLabel = new QLabel(tr("Loading RAW sequence…"), m_rawLoadDialog);
    m_rawLoadLabel->setWordWrap(true);
    rawLoadLayout->addWidget(m_rawLoadLabel);
    m_rawLoadProgress = new QProgressBar(m_rawLoadDialog);
    m_rawLoadProgress->setTextVisible(true);
    rawLoadLayout->addWidget(m_rawLoadProgress);
    m_rawLoadCancelButton = new QPushButton(tr("Cancel"), m_rawLoadDialog);
    rawLoadLayout->addWidget(m_rawLoadCancelButton, 0, Qt::AlignRight);
    m_rawLoadDialog->hide();
    connect(m_rawLoadCancelButton, &QPushButton::clicked, this, [this] {
        if (m_rawLoadCancel) m_rawLoadCancel->store(true);
        m_rawLoadLabel->setText(tr("Canceling RAW sequence load…"));
        m_rawLoadCancelButton->setEnabled(false);
    });
    connect(m_rawLoadDialog, &QDialog::rejected, this, [this] {
        if (m_rawLoadCancel) m_rawLoadCancel->store(true);
    });

    m_previewWatcher = new QFutureWatcher<seeingforge::PreviewImages>(this);
    connect(m_previewWatcher, &QFutureWatcher<seeingforge::PreviewImages>::finished,
            this, &MainWindow::previewRenderFinished);

    m_hotPixelWatcher = new QFutureWatcher<seeingforge::HotPixelResult>(this);
    connect(m_hotPixelWatcher, &QFutureWatcher<seeingforge::HotPixelResult>::finished,
            this, &MainWindow::hotPixelAnalysisFinished);

    m_qualityWatcher = new QFutureWatcher<seeingforge::QualityResult>(this);
    connect(m_qualityWatcher, &QFutureWatcher<seeingforge::QualityResult>::finished,
            this, &MainWindow::qualityAnalysisFinished);
    connect(m_qualityWatcher, &QFutureWatcher<seeingforge::QualityResult>::progressRangeChanged,
            this, [this](int minimum, int maximum) {
                m_qualityProgress->setRange(minimum, maximum);
            });
    connect(m_qualityWatcher, &QFutureWatcher<seeingforge::QualityResult>::progressValueChanged,
            this, [this](int value) { m_qualityProgress->setValue(value); });

    m_exportWatcher = new QFutureWatcher<seeingforge::ExportResult>(this);
    connect(m_exportWatcher, &QFutureWatcher<seeingforge::ExportResult>::finished,
            this, &MainWindow::exportFinished);
    connect(m_exportWatcher, &QFutureWatcher<seeingforge::ExportResult>::progressRangeChanged,
            this, [this](int minimum, int maximum) {
                m_exportProgress->setRange(minimum, maximum);
            });
    connect(m_exportWatcher, &QFutureWatcher<seeingforge::ExportResult>::progressValueChanged,
            this, [this](int value) { m_exportProgress->setValue(value); });

    m_rawLoadWatcher = new QFutureWatcher<std::shared_ptr<seeingforge::RawSequence>>(this);
    connect(m_rawLoadWatcher,
            &QFutureWatcher<std::shared_ptr<seeingforge::RawSequence>>::finished,
            this, &MainWindow::rawSequenceLoadFinished);
    connect(m_rawLoadWatcher,
            &QFutureWatcher<std::shared_ptr<seeingforge::RawSequence>>::progressRangeChanged,
            this, [this](int minimum, int maximum) {
                m_rawLoadProgress->setRange(minimum, maximum);
            });
    connect(m_rawLoadWatcher,
            &QFutureWatcher<std::shared_ptr<seeingforge::RawSequence>>::progressValueChanged,
            this, [this](int value) {
                m_rawLoadProgress->setValue(value);
                m_rawLoadLabel->setText(
                    tr("Validating RAW frame %1 of %2…")
                        .arg(value).arg(m_rawLoadProgress->maximum()));
            });

    updateWorkflowSummaries();
    updateHeroSummary();
    updateFooterNavigation();
    showStatus(tr("Ready — open or drop a SER recording or RAW image sequence to begin."));
}

void MainWindow::applyThemeStyleSheet()
{
    setStyleSheet(QStringLiteral(
        "MainWindow, #workspaceRoot { background: #181b20; color: #f0f2f6; }"
        "QDialog { background-color: #1a1d23; color: #f0f2f6; }"
        "QLabel { background: transparent; }"
        "QMenuBar { background: #15181d; color: #cfd5df; border-bottom: 1px solid #262b33; }"
        "QMenuBar::item { background: transparent; padding: 5px 10px; }"
        "QMenuBar::item:selected { background: #282d36; color: #ffffff; }"
        "QMenu { background-color: #1d2128; color: #f0f2f6; border: 1px solid #444d5c; padding: 4px; }"
        "QMenu::item { background-color: transparent; padding: 6px 20px; border-radius: 5px; }"
        "QMenu::item:selected { background-color: #387fb5; color: #ffffff; }"
        "QMenu::separator { height: 1px; background: #2f3642; margin: 4px 8px; }"
        "QToolTip { background-color: #1d2128; color: #f0f2f6; border: 1px solid #4a5464; padding: 4px 8px; border-radius: 6px; }"
        "#heroHeader {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #25222c, stop:0.55 #20242c, stop:1 #1a1e24);"
        "  border-bottom: 1px solid #333944;"
        "}"
        "#workflowPanel { background: #1a1d23; border-right: 1px solid #2d333d; }"
        "#inspectorPanel { background: #1a1d23; border-left: 1px solid #2d333d; }"
        "#inspectorScroll, #inspectorViewport, #inspectorPageContent { background: #1a1d23; border: 0; }"
        "#previewPanel, #previewScrollArea, #previewViewport, #previewCanvas { background: #101317; border: 0; }"
        "#overlayStrip { background: #16191e; border-bottom: 1px solid #252a33; }"
        "#transportDock { background: #1b1f25; border-top: 1px solid #2f3540; }"
        "#footerBar { background: #15181d; border-top: 1px solid #2d333d; }"
        "#displayPopover {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #262b33, stop:1 #1f232a);"
        "  border: 1px solid #4a5464; border-radius: 12px;"
        "}"
        "QFrame[sfRole='card'] {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #262b33, stop:1 #1f232a);"
        "  border: 1px solid #333944; border-radius: 12px;"
        "}"
        "QFrame[sfRole='tile'] {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #282d36, stop:1 #20242c);"
        "  border: 1px solid #333944; border-radius: 10px;"
        "}"
        "QFrame[sfRole='inset'] {"
        "  background: #15181d; border: 1px solid #282d36; border-radius: 9px;"
        "}"
        "QPushButton[sfRole='stepNav'] {"
        "  background: transparent; border: 1px solid transparent; border-radius: 10px;"
        "  text-align: left; padding: 8px 10px;"
        "}"
        "QPushButton[sfRole='stepNav']:hover {"
        "  background: rgba(255, 255, 255, 0.04); border-color: #2c323c;"
        "}"
        "QPushButton[sfRole='stepNav']:checked {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 rgba(84,42,92,140), stop:1 rgba(56,30,62,100));"
        "  border: 1px solid #8a5296; border-left: 3px solid #b878c4;"
        "}"
        "QPushButton, QToolButton {"
        "  background: #282d36; color: #f0f2f6; border: 1px solid #3c4350;"
        "  border-radius: 8px; padding: 6px 12px; font-weight: 600;"
        "}"
        "QPushButton:hover, QToolButton:hover { background: #313742; border-color: #505969; }"
        "QPushButton:disabled, QToolButton:disabled { color: #6e7785; background: #20242b; border-color: #2b303a; }"
        "QPushButton[sfRole='primary'] {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3f8cc6, stop:1 #3172a5);"
        "  color: #ffffff; border: 1px solid #529ed6; font-weight: 700;"
        "}"
        "QPushButton[sfRole='primary']:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #4a98d2, stop:1 #387fb5);"
        "}"
        "QPushButton[sfRole='primary']:disabled {"
        "  background: #24384a; color: #7e97ad; border-color: #2e4860;"
        "}"
        "QPushButton[sfRole='segBtn'], QToolButton[sfRole='segBtn'] {"
        "  background: transparent; color: #aab2bd; border: 1px solid transparent;"
        "  border-radius: 7px; padding: 4px 10px; font-weight: 600;"
        "}"
        "QPushButton[sfRole='segBtn']:hover, QToolButton[sfRole='segBtn']:hover {"
        "  color: #f0f2f6; background: rgba(255,255,255,0.04);"
        "}"
        "QPushButton[sfRole='segBtn']:checked, QToolButton[sfRole='segBtn']:checked {"
        "  background: #313742; color: #ffffff; border: 1px solid #4c5566;"
        "}"
        "QPushButton[sfRole='filterChip'] {"
        "  background: transparent; color: #aab2bd; border: 1px solid transparent;"
        "  border-radius: 7px; padding: 4px 10px; font-size: 11px; font-weight: 600;"
        "}"
        "QPushButton[sfRole='filterChip']:hover { color: #f0f2f6; background: rgba(255,255,255,0.04); }"
        "QPushButton[sfRole='filterChip']:checked {"
        "  background: #313742; color: #ffffff; border: 1px solid #4c5566;"
        "}"
        "QPushButton[sfRole='plumToggle'] { padding: 6px 4px; font-size: 12px; }"
        "QPushButton[sfRole='plumToggle']:checked {"
        "  background: rgba(84,42,92,135); color: #ffffff; border: 1px solid #8a5296;"
        "}"
        "QToolButton[sfRole='overlayChip'] {"
        "  background: #1e2229; color: #aab2bd; border: 1px solid #333944;"
        "  border-radius: 7px; padding: 4px 9px; font-size: 11px; font-weight: 600;"
        "}"
        "QToolButton[sfRole='overlayChip'][chipColor='green']:checked {"
        "  background: rgba(82,210,115,32); border-color: rgba(82,210,115,100); color: #52d273;"
        "}"
        "QToolButton[sfRole='overlayChip'][chipColor='red']:checked {"
        "  background: rgba(255,77,90,34); border-color: rgba(255,77,90,105); color: #ff4d5a;"
        "}"
        "QToolButton[sfRole='overlayChip'][chipColor='blue']:checked {"
        "  background: rgba(56,127,181,38); border-color: rgba(56,127,181,120); color: #8ec5ee;"
        "}"
        "QComboBox, QSpinBox {"
        "  background-color: #171a20; color: #f0f2f6; border: 1px solid #363d49;"
        "  border-radius: 8px; padding: 5px 10px; min-height: 22px;"
        "}"
        "QComboBox:hover, QSpinBox:hover { border-color: #4f5969; }"
        "QComboBox:focus, QSpinBox:focus { border-color: #387fb5; }"
        "QComboBox QAbstractItemView {"
        "  background-color: #1d2128; color: #f0f2f6; border: 1px solid #444d5c;"
        "  outline: 0; padding: 3px; selection-background-color: #387fb5; selection-color: #ffffff;"
        "}"
        "QComboBox QAbstractItemView::item {"
        "  background-color: #1d2128; color: #f0f2f6; padding: 5px 10px; min-height: 22px; border-radius: 4px;"
        "}"
        "QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected {"
        "  background-color: #387fb5; color: #ffffff;"
        "}"
        "QSlider::groove:horizontal {"
        "  height: 5px; background: #2f3540; border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: #d8dee9; border: 2px solid #387fb5; width: 12px; height: 12px;"
        "  margin: -5px 0; border-radius: 7px;"
        "}"
        "QCheckBox { background: transparent; color: #dce1ea; spacing: 7px; }"
        "QCheckBox::indicator { width: 15px; height: 15px; border-radius: 4px; border: 1px solid #485160; background: #16191f; }"
        "QCheckBox::indicator:checked { background: #387fb5; border-color: #529ed6; }"
        "QTableWidget {"
        "  background: #15181d; color: #e5e9f0; border: 1px solid #2e3440; border-radius: 8px;"
        "  gridline-color: #252a33; selection-background-color: rgba(56,127,181,80);"
        "}"
        "QHeaderView::section {"
        "  background: #1e222a; color: #9ca5b3; border: 0; border-bottom: 1px solid #2e3440;"
        "  padding: 5px 8px; font-weight: 600; font-size: 11px;"
        "}"
        "QProgressBar {"
        "  border: 1px solid #3c4452; border-radius: 5px; background: #15181d;"
        "  text-align: center; color: #ffffff; min-height: 18px;"
        "}"
        "QProgressBar::chunk { background: #387fb5; border-radius: 4px; }"
        "QScrollBar:vertical { background: transparent; width: 7px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: #363d49; border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: #4b5464; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { background: transparent; height: 7px; margin: 2px; }"
        "QScrollBar::handle:horizontal { background: #363d49; border-radius: 3px; min-width: 24px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"));
}

void MainWindow::createActions()
{
    auto *openAction = new QAction(tr("&Open SER File…"), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openSerFile);
    openAction->setObjectName(QStringLiteral("openSerAction"));

    auto *openRawAction = new QAction(tr("Open &RAW Image Sequence…"), this);
    connect(openRawAction, &QAction::triggered, this, &MainWindow::openRawFiles);
    openRawAction->setObjectName(QStringLiteral("openRawAction"));

    auto *openRawFolderAction = new QAction(tr("Open RAW &Folder…"), this);
    connect(openRawFolderAction, &QAction::triggered, this, &MainWindow::openRawFolder);
    openRawFolderAction->setObjectName(QStringLiteral("openRawFolderAction"));

    auto *exitAction = new QAction(tr("E&xit"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);
    exitAction->setObjectName(QStringLiteral("exitAction"));

    m_fitAction = new QAction(tr("&Fit to Window"), this);
    m_fitAction->setCheckable(true);
    m_fitAction->setChecked(true);
    m_fitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(m_fitAction, &QAction::triggered, this, &MainWindow::fitImage);

    m_actualSizeAction = new QAction(tr("&Actual Size"), this);
    m_actualSizeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(m_actualSizeAction, &QAction::triggered, this, &MainWindow::showActualSize);

    auto *aboutAction = new QAction(tr("&About SeeingForge"), this);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    aboutAction->setObjectName(QStringLiteral("aboutAction"));
}

void MainWindow::createMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("openSerAction")));
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("openRawAction")));
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("openRawFolderAction")));
    fileMenu->addSeparator();
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("exitAction")));

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_fitAction);
    viewMenu->addAction(m_actualSizeAction);
    auto *toggleDisplayAction = viewMenu->addAction(tr("Toggle Preview &Display Controls"));
    connect(toggleDisplayAction, &QAction::triggered, this, &MainWindow::toggleDisplayPopover);

    auto *processingMenu = menuBar()->addMenu(tr("&Processing"));
    auto *saveDefaultsAction = processingMenu->addAction(tr("Save Preferred Choices"));
    connect(saveDefaultsAction, &QAction::triggered,
            this, &MainWindow::savePreferredChoices);
    auto *resetAction = processingMenu->addAction(tr("Reset Current Step to Defaults"));
    connect(resetAction, &QAction::triggered, this, &MainWindow::resetCurrentStep);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(findChild<QAction *>(QStringLiteral("aboutAction")));
}

void MainWindow::createWorkspace()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("workspaceRoot"));
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(createHeroHeader());

    auto *splitter = new QSplitter(Qt::Horizontal, root);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(4);
    splitter->addWidget(createWorkflowPanel());
    splitter->addWidget(createPreviewPanel());
    splitter->addWidget(createInspectorPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({246, 800, 384});
    rootLayout->addWidget(splitter, 1);

    rootLayout->addWidget(createFooterBar());
    setCentralWidget(root);

    QPalette popupPal;
    popupPal.setColor(QPalette::Window, QColor(QStringLiteral("#1d2128")));
    popupPal.setColor(QPalette::Base, QColor(QStringLiteral("#1d2128")));
    popupPal.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#232832")));
    popupPal.setColor(QPalette::Text, QColor(QStringLiteral("#f0f2f6")));
    popupPal.setColor(QPalette::WindowText, QColor(QStringLiteral("#f0f2f6")));
    popupPal.setColor(QPalette::Highlight, QColor(QStringLiteral("#387fb5")));
    popupPal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    for (QComboBox *combo : findChildren<QComboBox *>()) {
        if (QAbstractItemView *view = combo->view()) {
            view->setAutoFillBackground(true);
            view->setPalette(popupPal);
            if (QWidget *popup = view->parentWidget()) {
                popup->setObjectName(QStringLiteral("sfComboPopup"));
                popup->setAttribute(Qt::WA_TranslucentBackground, false);
                popup->setAutoFillBackground(true);
                popup->setPalette(popupPal);
                popup->setStyleSheet(QStringLiteral(
                    "#sfComboPopup { background-color: #1d2128; border: 1px solid #444d5c; }"));
            }
        }
    }

    auto canTriggerViewportShortcut = [this]() {
        QWidget *focus = QApplication::focusWidget();
        return !qobject_cast<QAbstractSpinBox *>(focus)
            && !qobject_cast<QLineEdit *>(focus)
            && !qobject_cast<QComboBox *>(focus)
            && !qobject_cast<QSlider *>(focus);
    };
    auto *leftShortcut = new QShortcut(QKeySequence(Qt::Key_Left), root);
    leftShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(leftShortcut, &QShortcut::activated, this, [this, canTriggerViewportShortcut] {
        if (canTriggerViewportShortcut()) stepFrame(-1);
    });
    auto *rightShortcut = new QShortcut(QKeySequence(Qt::Key_Right), root);
    rightShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(rightShortcut, &QShortcut::activated, this, [this, canTriggerViewportShortcut] {
        if (canTriggerViewportShortcut()) stepFrame(1);
    });
    auto *playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), root);
    playShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(playShortcut, &QShortcut::activated, this, [this] {
        QWidget *focus = QApplication::focusWidget();
        if (!qobject_cast<QAbstractSpinBox *>(focus)
            && !qobject_cast<QLineEdit *>(focus)
            && !qobject_cast<QPushButton *>(focus)) {
            togglePlayback();
        }
    });
}

QWidget *MainWindow::createHeroHeader()
{
    auto *hero = new QFrame(this);
    hero->setObjectName(QStringLiteral("heroHeader"));
    hero->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QHBoxLayout(hero);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(14);

    auto *emblem = new QLabel(hero);
    emblem->setFixedSize(42, 42);
    emblem->setPixmap(createAppEmblemPixmap(42));
    layout->addWidget(emblem);

    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(8);
    m_heroTitle = new QLabel(tr("SeeingForge"), hero);
    m_heroTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 16px; font-weight: 700;"));
    m_heroFormatBadge = makeBadge(
        tr("No source"),
        QStringLiteral("rgba(56,127,181,40)"),
        QStringLiteral("rgba(56,127,181,115)"),
        QStringLiteral("#8ec5ee"), hero);
    m_heroDimBadge = makeBadge(
        QStringLiteral("— × —"),
        QStringLiteral("#16191f"),
        QStringLiteral("#353b46"),
        QStringLiteral("#aab2bd"), hero);
    topRow->addWidget(m_heroTitle);
    topRow->addWidget(m_heroFormatBadge);
    topRow->addWidget(m_heroDimBadge);
    topRow->addStretch(1);
    titleCol->addLayout(topRow);

    m_heroSubtitle = new QLabel(
        tr("Non-destructive planetary & lunar preprocessing — original recordings are never modified"), hero);
    m_heroSubtitle->setStyleSheet(QStringLiteral("color: #98a2b0; font-size: 12px;"));
    titleCol->addWidget(m_heroSubtitle);
    layout->addLayout(titleCol, 1);

    auto *modeWell = new QFrame(hero);
    modeWell->setProperty("sfRole", QStringLiteral("inset"));
    modeWell->setAttribute(Qt::WA_StyledBackground, true);
    auto *modeLayout = new QHBoxLayout(modeWell);
    modeLayout->setContentsMargins(4, 4, 4, 4);
    modeLayout->setSpacing(3);

    m_previewMode = new QButtonGroup(this);
    auto addModeBtn = [&](const QString &label, int id, bool checked) {
        auto *btn = new QToolButton(modeWell);
        btn->setText(label);
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setProperty("sfRole", QStringLiteral("segBtn"));
        m_previewMode->addButton(btn, id);
        modeLayout->addWidget(btn);
    };
    addModeBtn(tr("Color"), 0, true);
    addModeBtn(tr("Raw Luminance"), 1, false);
    addModeBtn(tr("Raw CFA 1:1"), 2, false);
    connect(m_previewMode, &QButtonGroup::idClicked, this, &MainWindow::previewModeChanged);
    layout->addWidget(modeWell);

    m_displayPopoverButton = new QPushButton(tr("Display  [Preview]"), hero);
    m_displayPopoverButton->setCheckable(true);
    m_displayPopoverButton->setToolTip(
        tr("Open preview-only white balance, stretch, saturation, gain, and gamma controls"));
    connect(m_displayPopoverButton, &QPushButton::clicked,
            this, &MainWindow::toggleDisplayPopover);
    layout->addWidget(m_displayPopoverButton);

    auto *statusPill = new QFrame(hero);
    statusPill->setProperty("sfRole", QStringLiteral("inset"));
    statusPill->setAttribute(Qt::WA_StyledBackground, true);
    auto *pillLayout = new QHBoxLayout(statusPill);
    pillLayout->setContentsMargins(10, 5, 12, 5);
    pillLayout->setSpacing(8);
    m_statusDot = new QLabel(statusPill);
    m_statusDot->setFixedSize(8, 8);
    m_statusDot->setStyleSheet(QStringLiteral("background: #76808e; border-radius: 4px;"));
    pillLayout->addWidget(m_statusDot);

    auto *statusTextCol = new QVBoxLayout;
    statusTextCol->setSpacing(0);
    m_previewStateLabel = new QLabel(tr("Waiting for source"), statusPill);
    m_previewStateLabel->setStyleSheet(QStringLiteral("color: #e8ecf2; font-size: 11px; font-weight: 600;"));
    m_pipelineStateLabel = new QLabel(tr("Drop or open SER / RAW"), statusPill);
    m_pipelineStateLabel->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 10px;"));
    statusTextCol->addWidget(m_previewStateLabel);
    statusTextCol->addWidget(m_pipelineStateLabel);
    pillLayout->addLayout(statusTextCol);
    layout->addWidget(statusPill);

    return hero;
}

QWidget *MainWindow::createWorkflowPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("workflowPanel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setFixedWidth(246);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *headerRow = new QHBoxLayout;
    auto *heading = new QLabel(tr("EXPORT PIPELINE"), panel);
    heading->setStyleSheet(QStringLiteral("color: #8892a0; font-size: 11px; font-weight: 700; letter-spacing: 0.8px;"));
    auto *stepCount = new QLabel(tr("5 steps"), panel);
    stepCount->setStyleSheet(QStringLiteral("color: #76808e; font-size: 11px; font-family: monospace;"));
    headerRow->addWidget(heading);
    headerRow->addStretch(1);
    headerRow->addWidget(stepCount);
    layout->addLayout(headerRow);

    struct StepSpec {
        const char *title;
        const char *initialSub;
        bool optionalBadge;
    };
    static constexpr std::array<StepSpec, 5> specs{{
        {"Source & Sensor", "Load SER or RAW frames", false},
        {"Calibration", "Sensor balance & hot pixels", true},
        {"Stabilize & Crop", "Detect target & crop margin", false},
        {"Quality Ranking", "Score & filter sharp frames", false},
        {"Export SER", "Write centered SER output", false},
    }};

    m_workflowGroup = new QButtonGroup(this);
    m_workflowGroup->setExclusive(true);

    for (int i = 0; i < 5; ++i) {
        auto *btn = new QPushButton(panel);
        btn->setCheckable(true);
        btn->setChecked(i == 0);
        btn->setProperty("sfRole", QStringLiteral("stepNav"));
        btn->setAttribute(Qt::WA_StyledBackground, true);
        btn->setMinimumHeight(56);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *row = new QHBoxLayout(btn);
        row->setContentsMargins(10, 7, 10, 7);
        row->setSpacing(9);

        auto *badge = new QLabel(QString::number(i + 1), btn);
        badge->setFixedSize(24, 24);
        badge->setAlignment(Qt::AlignCenter);
        badge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        badge->setStyleSheet(QStringLiteral(
            "background: #242932; border: 1px solid #3a414e; border-radius: 7px;"
            "color: #aab2bd; font-size: 11px; font-weight: 700;"));
        row->addWidget(badge, 0, Qt::AlignTop);

        auto *textCol = new QVBoxLayout;
        textCol->setSpacing(2);
        auto *titleRow = new QHBoxLayout;
        titleRow->setSpacing(4);
        auto *titleLbl = new QLabel(tr(specs[static_cast<std::size_t>(i)].title), btn);
        titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        titleLbl->setStyleSheet(QStringLiteral("color: #edf0f5; font-size: 13px; font-weight: 600;"));
        titleRow->addWidget(titleLbl);
        titleRow->addStretch(1);
        if (specs[static_cast<std::size_t>(i)].optionalBadge) {
            auto *opt = new QLabel(tr("Opt"), btn);
            opt->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            opt->setStyleSheet(QStringLiteral(
                "background: #232830; border: 1px solid #333944; border-radius: 4px;"
                "color: #8d96a5; font-size: 10px; padding: 0px 5px;"));
            titleRow->addWidget(opt);
        }
        textCol->addLayout(titleRow);

        auto *subLbl = new QLabel(tr(specs[static_cast<std::size_t>(i)].initialSub), btn);
        subLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        subLbl->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px;"));
        textCol->addWidget(subLbl);
        row->addLayout(textCol, 1);

        m_workflowButtons[static_cast<std::size_t>(i)] = btn;
        m_navStepBadges[static_cast<std::size_t>(i)] = badge;
        m_navSubLabels[static_cast<std::size_t>(i)] = subLbl;
        m_workflowGroup->addButton(btn, i);
        layout->addWidget(btn);
    }

    connect(m_workflowGroup, &QButtonGroup::idClicked,
            this, &MainWindow::selectWorkflowStep);

    auto *displayNote = new QFrame(panel);
    displayNote->setObjectName(QStringLiteral("displayNoteBanner"));
    displayNote->setAttribute(Qt::WA_StyledBackground, true);
    displayNote->setStyleSheet(QStringLiteral(
        "#displayNoteBanner { background: rgba(240, 198, 79, 15); border: 1px solid rgba(240, 198, 79, 60); border-radius: 10px; }"));
    auto *noteLayout = new QVBoxLayout(displayNote);
    noteLayout->setContentsMargins(10, 8, 10, 8);
    noteLayout->setSpacing(2);
    auto *noteTitle = new QLabel(tr("✦ Preview Display in Top Bar"), displayNote);
    noteTitle->setStyleSheet(QStringLiteral("color: #f0c64f; font-size: 11px; font-weight: 700;"));
    auto *noteBody = new QLabel(
        tr("Color, stretch, gain, and gamma live in the top Display popover so the rail reflects export-affecting steps."),
        displayNote);
    noteBody->setWordWrap(true);
    noteBody->setStyleSheet(QStringLiteral("color: #cfd5df; font-size: 11px;"));
    noteLayout->addWidget(noteTitle);
    noteLayout->addWidget(noteBody);
    layout->addWidget(displayNote);

    layout->addStretch(1);

    auto *presetCard = makeCard(panel, 11, 10);
    auto *presetTop = new QHBoxLayout;
    auto *presetTitle = new QLabel(tr("Session Preset"), presetCard);
    presetTitle->setStyleSheet(QStringLiteral("color: #cfd5df; font-size: 12px; font-weight: 600;"));
    auto *activePill = makeBadge(
        tr("Ready"),
        QStringLiteral("rgba(82,210,115,25)"),
        QStringLiteral("rgba(82,210,115,75)"),
        QStringLiteral("#52d273"), presetCard);
    presetTop->addWidget(presetTitle);
    presetTop->addStretch(1);
    presetTop->addWidget(activePill);
    static_cast<QVBoxLayout *>(presetCard->layout())->addLayout(presetTop);

    m_presetCombo = new QComboBox(presetCard);
    connect(m_presetCombo, &QComboBox::activated, this, &MainWindow::applySelectedPreset);
    presetCard->layout()->addWidget(m_presetCombo);

    auto *presetBtns = new QHBoxLayout;
    presetBtns->setSpacing(6);
    auto *savePresetBtn = new QPushButton(tr("Save…"), presetCard);
    savePresetBtn->setToolTip(tr("Save current settings as a named preset and update defaults"));
    connect(savePresetBtn, &QPushButton::clicked, this, &MainWindow::saveNamedPreset);
    auto *quickExportBtn = new QPushButton(tr("Quick Export"), presetCard);
    quickExportBtn->setStyleSheet(QStringLiteral("color: #8ec5ee; border-color: #355470;"));
    connect(quickExportBtn, &QPushButton::clicked, this, [this] { selectWorkflowStep(4); });
    presetBtns->addWidget(savePresetBtn, 1);
    presetBtns->addWidget(quickExportBtn, 1);
    static_cast<QVBoxLayout *>(presetCard->layout())->addLayout(presetBtns);
    layout->addWidget(presetCard);

    return panel;
}

QWidget *MainWindow::createPreviewPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("previewPanel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    m_previewPanel = panel;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Top overlay & telemetry strip
    m_overlayStrip = new QFrame(panel);
    m_overlayStrip->setObjectName(QStringLiteral("overlayStrip"));
    m_overlayStrip->setAttribute(Qt::WA_StyledBackground, true);
    auto *stripLayout = new QHBoxLayout(m_overlayStrip);
    stripLayout->setContentsMargins(12, 6, 12, 6);
    stripLayout->setSpacing(6);

    auto *ovLabel = new QLabel(tr("Overlays:"), m_overlayStrip);
    ovLabel->setStyleSheet(QStringLiteral("color: #8892a0; font-size: 11px; font-weight: 600;"));
    stripLayout->addWidget(ovLabel);

    m_overlayDetectBtn = new QToolButton(m_overlayStrip);
    m_overlayDetectBtn->setText(tr("Object & Crop"));
    m_overlayDetectBtn->setCheckable(true);
    m_overlayDetectBtn->setChecked(true);
    m_overlayDetectBtn->setProperty("sfRole", QStringLiteral("overlayChip"));
    m_overlayDetectBtn->setProperty("chipColor", QStringLiteral("green"));
    connect(m_overlayDetectBtn, &QToolButton::toggled, this, [this] { updateImage(); });
    stripLayout->addWidget(m_overlayDetectBtn);

    m_overlayHotPixelsBtn = new QToolButton(m_overlayStrip);
    m_overlayHotPixelsBtn->setText(tr("Hot Pixels"));
    m_overlayHotPixelsBtn->setCheckable(true);
    m_overlayHotPixelsBtn->setChecked(false);
    m_overlayHotPixelsBtn->setProperty("sfRole", QStringLiteral("overlayChip"));
    m_overlayHotPixelsBtn->setProperty("chipColor", QStringLiteral("red"));
    connect(m_overlayHotPixelsBtn, &QToolButton::toggled, this, [this] { updateImage(); });
    stripLayout->addWidget(m_overlayHotPixelsBtn);

    m_overlayCrosshairBtn = new QToolButton(m_overlayStrip);
    m_overlayCrosshairBtn->setText(tr("Center Axis"));
    m_overlayCrosshairBtn->setCheckable(true);
    m_overlayCrosshairBtn->setChecked(false);
    m_overlayCrosshairBtn->setProperty("sfRole", QStringLiteral("overlayChip"));
    m_overlayCrosshairBtn->setProperty("chipColor", QStringLiteral("blue"));
    connect(m_overlayCrosshairBtn, &QToolButton::toggled, this, [this] { updateImage(); });
    stripLayout->addWidget(m_overlayCrosshairBtn);

    stripLayout->addStretch(1);

    m_canvasObjectBadge = makeBadge(
        tr("Object: —"),
        QStringLiteral("#1d2128"),
        QStringLiteral("#323844"),
        QStringLiteral("#aab2bd"), m_overlayStrip);
    m_canvasCropBadge = makeBadge(
        tr("Crop: —"),
        QStringLiteral("rgba(240,198,79,25)"),
        QStringLiteral("rgba(240,198,79,90)"),
        QStringLiteral("#f0c64f"), m_overlayStrip);
    stripLayout->addWidget(m_canvasObjectBadge);
    stripLayout->addWidget(m_canvasCropBadge);
    layout->addWidget(m_overlayStrip);

    // Scrollable astro canvas
    m_previewScrollArea = new QScrollArea(panel);
    m_previewScrollArea->setObjectName(QStringLiteral("previewScrollArea"));
    m_previewScrollArea->setAlignment(Qt::AlignCenter);
    m_previewScrollArea->setWidgetResizable(true);
    m_previewScrollArea->setFrameShape(QFrame::NoFrame);
    m_previewScrollArea->viewport()->setObjectName(QStringLiteral("previewViewport"));
    m_previewScrollArea->viewport()->installEventFilter(this);

    m_previewCanvas = new QWidget(m_previewScrollArea);
    m_previewCanvas->setObjectName(QStringLiteral("previewCanvas"));
    auto *canvasLayout = new QVBoxLayout(m_previewCanvas);
    canvasLayout->setContentsMargins(24, 24, 24, 24);
    canvasLayout->setAlignment(Qt::AlignCenter);

    m_imageLabel = new QLabel(m_previewCanvas);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_imageLabel->hide();
    canvasLayout->addWidget(m_imageLabel, 1, Qt::AlignCenter);

    // Empty-state D112 Quick-Start Hub
    m_emptyHub = makeCard(m_previewCanvas, 28, 24);
    m_emptyHub->setMaximumWidth(500);
    auto *hubTitle = new QLabel(tr("Open a SER recording or RAW image sequence"), m_emptyHub);
    hubTitle->setAlignment(Qt::AlignCenter);
    hubTitle->setWordWrap(true);
    hubTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 17px; font-weight: 700;"));
    auto *hubSub = new QLabel(
        tr("Choose a source below, use the File menu, or drag and drop a .ser file, RAW files, or a RAW folder directly onto the window."),
        m_emptyHub);
    hubSub->setAlignment(Qt::AlignCenter);
    hubSub->setWordWrap(true);
    hubSub->setStyleSheet(QStringLiteral("color: #9ca5b3; font-size: 12px;"));
    m_emptyHub->layout()->addWidget(hubTitle);
    m_emptyHub->layout()->addWidget(hubSub);

    auto *hubBtns = new QHBoxLayout;
    hubBtns->setSpacing(8);
    auto *hubSer = new QPushButton(tr("Open SER File…"), m_emptyHub);
    hubSer->setProperty("sfRole", QStringLiteral("primary"));
    connect(hubSer, &QPushButton::clicked, this, &MainWindow::openSerFile);
    auto *hubRaw = new QPushButton(tr("RAW Files…"), m_emptyHub);
    connect(hubRaw, &QPushButton::clicked, this, &MainWindow::openRawFiles);
    auto *hubFolder = new QPushButton(tr("RAW Folder…"), m_emptyHub);
    connect(hubFolder, &QPushButton::clicked, this, &MainWindow::openRawFolder);
    hubBtns->addWidget(hubSer, 1);
    hubBtns->addWidget(hubRaw, 1);
    hubBtns->addWidget(hubFolder, 1);
    static_cast<QVBoxLayout *>(m_emptyHub->layout())->addSpacing(4);
    static_cast<QVBoxLayout *>(m_emptyHub->layout())->addLayout(hubBtns);
    canvasLayout->addWidget(m_emptyHub, 0, Qt::AlignCenter);

    m_cfaHintToast = new QLabel(
        tr("Raw CFA mosaic is shown at 1:1 — Bayer pixel pattern is expected"),
        m_previewCanvas);
    m_cfaHintToast->setAlignment(Qt::AlignCenter);
    m_cfaHintToast->setStyleSheet(QStringLiteral(
        "background: rgba(30, 34, 42, 225); border: 1px solid #3c4452; border-radius: 10px;"
        "color: #d6dce6; padding: 4px 12px; font-size: 11px;"));
    m_cfaHintToast->hide();
    canvasLayout->addWidget(m_cfaHintToast, 0, Qt::AlignCenter);

    m_previewScrollArea->setWidget(m_previewCanvas);
    layout->addWidget(m_previewScrollArea, 1);

    // Unified Transport & Zoom Dock
    auto *dock = new QFrame(panel);
    dock->setObjectName(QStringLiteral("transportDock"));
    dock->setAttribute(Qt::WA_StyledBackground, true);
    auto *dockLayout = new QHBoxLayout(dock);
    dockLayout->setContentsMargins(12, 7, 12, 7);
    dockLayout->setSpacing(10);

    auto *playBtns = new QHBoxLayout;
    playBtns->setSpacing(4);
    m_prevFrameBtn = new QPushButton(QStringLiteral("‹"), dock);
    m_prevFrameBtn->setFixedSize(28, 28);
    m_prevFrameBtn->setToolTip(tr("Previous frame (Left arrow)"));
    m_prevFrameBtn->setEnabled(false);
    connect(m_prevFrameBtn, &QPushButton::clicked, this, [this] { stepFrame(-1); });

    m_playButton = new QPushButton(QStringLiteral("▶"), dock);
    m_playButton->setFixedSize(28, 28);
    m_playButton->setToolTip(tr("Play / Pause sequence (Space)"));
    m_playButton->setEnabled(false);
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::togglePlayback);

    m_nextFrameBtn = new QPushButton(QStringLiteral("›"), dock);
    m_nextFrameBtn->setFixedSize(28, 28);
    m_nextFrameBtn->setToolTip(tr("Next frame (Right arrow)"));
    m_nextFrameBtn->setEnabled(false);
    connect(m_nextFrameBtn, &QPushButton::clicked, this, [this] { stepFrame(1); });

    playBtns->addWidget(m_prevFrameBtn);
    playBtns->addWidget(m_playButton);
    playBtns->addWidget(m_nextFrameBtn);
    dockLayout->addLayout(playBtns);

    m_frameLabel = new QLabel(tr("Frame — / —"), dock);
    m_frameLabel->setStyleSheet(QStringLiteral(
        "background: #13161b; border: 1px solid #2d333d; border-radius: 7px;"
        "padding: 4px 10px; font-family: monospace; font-size: 11px; color: #e5e9f0;"));
    dockLayout->addWidget(m_frameLabel);

    auto *scrubCol = new QVBoxLayout;
    scrubCol->setSpacing(2);
    m_frameSlider = new QSlider(Qt::Horizontal, dock);
    m_frameSlider->setEnabled(false);
    connect(m_frameSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_source && m_source->isOpen()) {
            m_frameLabel->setText(tr("Frame %1 / %2")
                                      .arg(value + 1)
                                      .arg(m_source->header().frameCount));
            m_frameDebounce->start();
        }
    });
    m_frameQualityLabel = new QLabel(tr("Open a recording to navigate frames"), dock);
    m_frameQualityLabel->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px; font-family: monospace;"));
    scrubCol->addWidget(m_frameSlider);
    scrubCol->addWidget(m_frameQualityLabel);
    dockLayout->addLayout(scrubCol, 1);

    auto *zoomWell = new QFrame(dock);
    zoomWell->setProperty("sfRole", QStringLiteral("inset"));
    zoomWell->setAttribute(Qt::WA_StyledBackground, true);
    auto *zoomLayout = new QHBoxLayout(zoomWell);
    zoomLayout->setContentsMargins(3, 3, 3, 3);
    zoomLayout->setSpacing(2);

    m_zoomFitBtn = new QToolButton(zoomWell);
    m_zoomFitBtn->setText(tr("Fit"));
    m_zoomFitBtn->setCheckable(true);
    m_zoomFitBtn->setChecked(true);
    m_zoomFitBtn->setProperty("sfRole", QStringLiteral("segBtn"));
    m_zoomFitBtn->setToolTip(tr("Fit image to viewport (Ctrl+0)"));
    connect(m_zoomFitBtn, &QToolButton::clicked, this, &MainWindow::fitImage);

    m_zoomActualBtn = new QToolButton(zoomWell);
    m_zoomActualBtn->setText(tr("1:1"));
    m_zoomActualBtn->setCheckable(true);
    m_zoomActualBtn->setProperty("sfRole", QStringLiteral("segBtn"));
    m_zoomActualBtn->setToolTip(tr("Actual pixel size (Ctrl+1)"));
    connect(m_zoomActualBtn, &QToolButton::clicked, this, &MainWindow::showActualSize);

    auto *zoomOutBtn = new QToolButton(zoomWell);
    zoomOutBtn->setText(QStringLiteral("−"));
    zoomOutBtn->setProperty("sfRole", QStringLiteral("segBtn"));
    zoomOutBtn->setToolTip(tr("Zoom out"));
    connect(zoomOutBtn, &QToolButton::clicked, this, &MainWindow::zoomOut);

    m_zoomLabel = new QLabel(tr("Fit"), zoomWell);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(46);
    m_zoomLabel->setStyleSheet(QStringLiteral("color: #d8dee9; font-family: monospace; font-size: 11px;"));

    auto *zoomInBtn = new QToolButton(zoomWell);
    zoomInBtn->setText(QStringLiteral("+"));
    zoomInBtn->setProperty("sfRole", QStringLiteral("segBtn"));
    zoomInBtn->setToolTip(tr("Zoom in"));
    connect(zoomInBtn, &QToolButton::clicked, this, &MainWindow::zoomIn);

    zoomLayout->addWidget(m_zoomFitBtn);
    zoomLayout->addWidget(m_zoomActualBtn);
    zoomLayout->addWidget(zoomOutBtn);
    zoomLayout->addWidget(m_zoomLabel);
    zoomLayout->addWidget(zoomInBtn);
    dockLayout->addWidget(zoomWell);

    layout->addWidget(dock);

    // Floating Preview Display Popover
    m_displayPopover = static_cast<QFrame *>(createDisplayPopover(panel));
    m_displayPopover->hide();

    return panel;
}

QWidget *MainWindow::createDisplayPopover(QWidget *parent)
{
    auto *pop = new QFrame(parent);
    pop->setObjectName(QStringLiteral("displayPopover"));
    pop->setAttribute(Qt::WA_StyledBackground, true);
    pop->setFixedWidth(322);
    auto *layout = new QVBoxLayout(pop);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(7);

    auto *topRow = new QHBoxLayout;
    auto *titleLbl = new QLabel(tr("Preview Display Controls"), pop);
    titleLbl->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 13px; font-weight: 700;"));
    auto *previewBadge = makeBadge(
        tr("Preview Only"),
        QStringLiteral("rgba(240,198,79,25)"),
        QStringLiteral("rgba(240,198,79,90)"),
        QStringLiteral("#f0c64f"), pop);
    auto *closeBtn = new QPushButton(QStringLiteral("✕"), pop);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setStyleSheet(QStringLiteral("padding: 0; border-radius: 6px;"));
    connect(closeBtn, &QPushButton::clicked, this, [this] {
        if (m_displayPopoverButton) m_displayPopoverButton->setChecked(false);
        m_displayPopover->hide();
    });
    topRow->addWidget(titleLbl);
    topRow->addWidget(previewBadge);
    topRow->addStretch(1);
    topRow->addWidget(closeBtn);
    layout->addLayout(topRow);

    auto *subLbl = new QLabel(
        tr("These controls adjust preview rendering only and never alter raw pixels or exported SER."), pop);
    subLbl->setWordWrap(true);
    subLbl->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px;"));
    layout->addWidget(subLbl);

    layout->addWidget(new QLabel(tr("Debayer method"), pop));
    m_debayerMethod = new QComboBox(pop);
    m_debayerMethod->addItem(tr("Bayer-aware color preview"), true);
    m_debayerMethod->addItem(tr("No debayer — neutral raw preview"), false);
    connect(m_debayerMethod, &QComboBox::currentIndexChanged,
            this, &MainWindow::requestPreviewRender);
    layout->addWidget(m_debayerMethod);

    auto *checkRow = new QHBoxLayout;
    m_whiteBalance = new QCheckBox(tr("Auto white balance"), pop);
    m_whiteBalance->setChecked(true);
    connect(m_whiteBalance, &QCheckBox::toggled, this, &MainWindow::requestPreviewRender);
    m_autoStretch = new QCheckBox(tr("Auto-stretch"), pop);
    m_autoStretch->setChecked(true);
    connect(m_autoStretch, &QCheckBox::toggled, this, &MainWindow::requestPreviewRender);
    checkRow->addWidget(m_whiteBalance);
    checkRow->addWidget(m_autoStretch);
    layout->addLayout(checkRow);

    auto addReadoutSlider = [&](const QString &name, QSlider **member, int minimum,
                                int maximum, int value, const QString &suffix,
                                double scaleDivisor, int decimals) {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(name, pop);
        lbl->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 12px;"));
        auto *valLbl = new QLabel(pop);
        valLbl->setStyleSheet(QStringLiteral("color: #ffffff; font-family: monospace; font-size: 12px;"));
        auto formatVal = [scaleDivisor, decimals, suffix](int v) {
            return scaleDivisor > 1.0
                ? QStringLiteral("%1%2").arg(v / scaleDivisor, 0, 'f', decimals).arg(suffix)
                : QStringLiteral("%1%2").arg(v).arg(suffix);
        };
        valLbl->setText(formatVal(value));
        row->addWidget(lbl);
        row->addStretch(1);
        row->addWidget(valLbl);
        layout->addLayout(row);

        auto *slider = new QSlider(Qt::Horizontal, pop);
        slider->setRange(minimum, maximum);
        slider->setValue(value);
        connect(slider, &QSlider::valueChanged, this, [this, valLbl, formatVal](int current) {
            valLbl->setText(formatVal(current));
            requestPreviewRender();
        });
        layout->addWidget(slider);
        *member = slider;
    };

    addReadoutSlider(tr("Red balance"), &m_redBalance, 50, 200, 100, QString(), 100.0, 2);
    addReadoutSlider(tr("Blue balance"), &m_blueBalance, 50, 200, 100, QString(), 100.0, 2);
    addReadoutSlider(tr("Saturation"), &m_saturation, 0, 400, 100, QStringLiteral("%"), 1.0, 0);
    addReadoutSlider(tr("Hue shift"), &m_hue, -180, 180, 0, QStringLiteral("°"), 1.0, 0);
    addReadoutSlider(tr("Preview gain"), &m_gain, 25, 400, 100, QStringLiteral("×"), 100.0, 2);
    addReadoutSlider(tr("Preview gamma"), &m_gamma, 25, 300, 100, QStringLiteral("×"), 100.0, 2);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->addStretch(1);
    auto *resetBtn = new QPushButton(tr("Reset Display"), pop);
    connect(resetBtn, &QPushButton::clicked, this, &MainWindow::resetDisplayControls);
    bottomRow->addWidget(resetBtn);
    layout->addLayout(bottomRow);

    return pop;
}

QWidget *MainWindow::createInspectorPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("inspectorPanel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setMinimumWidth(340);
    panel->setMaximumWidth(540);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    m_inspectorStack = new QStackedWidget(panel);
    m_inspectorStack->addWidget(wrapScrollablePage(createSourceInspector(), panel));
    m_inspectorStack->addWidget(wrapScrollablePage(createCalibrationInspector(), panel));
    m_inspectorStack->addWidget(wrapScrollablePage(createStabilizeInspector(), panel));
    m_inspectorStack->addWidget(wrapScrollablePage(createQualityInspector(), panel));
    m_inspectorStack->addWidget(wrapScrollablePage(createExportInspector(), panel));
    m_inspectorStack->setCurrentIndex(0);
    layout->addWidget(m_inspectorStack);
    return panel;
}

QWidget *MainWindow::createSourceInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    layout->addWidget(makeStepHeaderCard(
        0, tr("Source & Sensor"), tr("WAITING"),
        QStringLiteral("#232830"), QStringLiteral("#3a414e"), QStringLiteral("#aab2bd"),
        tr("Load a SER recording or RAW image sequence, inspect metadata, and verify the Bayer CFA pattern."),
        &m_sourceStatusBadge, page));

    auto *loadCard = makeCard(page);
    auto *loadTitle = new QLabel(tr("Open Recording or RAW Sequence"), loadCard);
    loadTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 700;"));
    loadCard->layout()->addWidget(loadTitle);
    auto *openRow = new QHBoxLayout;
    openRow->setSpacing(6);
    auto *serBtn = new QPushButton(tr("SER File…"), loadCard);
    connect(serBtn, &QPushButton::clicked, this, &MainWindow::openSerFile);
    auto *rawBtn = new QPushButton(tr("RAW Files…"), loadCard);
    connect(rawBtn, &QPushButton::clicked, this, &MainWindow::openRawFiles);
    auto *dirBtn = new QPushButton(tr("RAW Folder…"), loadCard);
    connect(dirBtn, &QPushButton::clicked, this, &MainWindow::openRawFolder);
    openRow->addWidget(serBtn, 1);
    openRow->addWidget(rawBtn, 1);
    openRow->addWidget(dirBtn, 1);
    static_cast<QVBoxLayout *>(loadCard->layout())->addLayout(openRow);
    layout->addWidget(loadCard);

    // 2x2 Source Telemetry Tiles
    auto *tileGrid = new QGridLayout;
    tileGrid->setSpacing(8);
    tileGrid->addWidget(makeMetricTile(
        tr("Frames"), QStringLiteral("—"), tr("No source loaded"),
        QStringLiteral("#ffffff"), &m_srcFramesTileValue, &m_srcFramesTileSub, page), 0, 0);
    tileGrid->addWidget(makeMetricTile(
        tr("Resolution"), QStringLiteral("— × —"), tr("Pixel dimensions"),
        QStringLiteral("#ffffff"), &m_srcResTileValue, &m_srcResTileSub, page), 0, 1);
    tileGrid->addWidget(makeMetricTile(
        tr("Sensor Format"), QStringLiteral("—"), tr("Pixel depth & CFA"),
        QStringLiteral("#52d273"), &m_srcFormatTileValue, &m_srcFormatTileSub, page), 1, 0);
    tileGrid->addWidget(makeMetricTile(
        tr("Camera WB"), QStringLiteral("—"), tr("RAW EXIF multipliers"),
        QStringLiteral("#8ec5ee"), &m_srcWbTileValue, &m_srcWbTileSub, page), 1, 1);
    layout->addLayout(tileGrid);

    // Bayer CFA Pattern Override Card
    auto *bayerCard = makeCard(page);
    auto *bayerTop = new QHBoxLayout;
    auto *bayerTitle = new QLabel(tr("Bayer CFA Pattern"), bayerCard);
    bayerTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 700;"));
    bayerTop->addWidget(bayerTitle);
    bayerTop->addStretch(1);
    bayerTop->addWidget(makeBadge(
        tr("Affects Export"),
        QStringLiteral("rgba(56,127,181,40)"),
        QStringLiteral("rgba(56,127,181,115)"),
        QStringLiteral("#8ec5ee"), bayerCard));
    static_cast<QVBoxLayout *>(bayerCard->layout())->addLayout(bayerTop);

    m_bayerPattern = new QComboBox(bayerCard);
    m_bayerPattern->addItem(tr("From source metadata"), 0);
    m_bayerPattern->addItem(tr("RGGB"), 8);
    m_bayerPattern->addItem(tr("GRBG"), 9);
    m_bayerPattern->addItem(tr("GBRG"), 10);
    m_bayerPattern->addItem(tr("BGGR"), 11);
    connect(m_bayerPattern, &QComboBox::currentIndexChanged, this, [this] {
        requestPreviewRender();
        updateWorkflowSummaries();
        updateHeroSummary();
    });
    bayerCard->layout()->addWidget(m_bayerPattern);

    auto *bayerNote = new QLabel(
        tr("Checked automatically when loading SER files. Override here if an ROI capture recorded the wrong Bayer pattern."),
        bayerCard);
    bayerNote->setWordWrap(true);
    bayerNote->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px;"));
    bayerCard->layout()->addWidget(bayerNote);

    m_bayerWarning = new QLabel(bayerCard);
    m_bayerWarning->setWordWrap(true);
    m_bayerWarning->setStyleSheet(QStringLiteral(
        "color: #f0c96a; background: rgba(240, 201, 106, 20);"
        "border: 1px solid rgba(240, 201, 106, 90); border-radius: 6px; padding: 8px; font-size: 11px;"));
    m_bayerWarning->hide();
    bayerCard->layout()->addWidget(m_bayerWarning);
    layout->addWidget(bayerCard);

    // Metadata Table Card
    auto *metaCard = makeCard(page);
    auto *metaTitle = new QLabel(tr("Recording Metadata"), metaCard);
    metaTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 700;"));
    metaCard->layout()->addWidget(metaTitle);
    m_metadataTable = new QTableWidget(10, 2, metaCard);
    m_metadataTable->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    m_metadataTable->verticalHeader()->hide();
    m_metadataTable->horizontalHeader()->setStretchLastSection(true);
    m_metadataTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_metadataTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_metadataTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_metadataTable->setMinimumHeight(250);
    metaCard->layout()->addWidget(m_metadataTable);
    layout->addWidget(metaCard);

    layout->addStretch(1);
    updateMetadata();
    return page;
}

QWidget *MainWindow::createCalibrationInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    layout->addWidget(makeStepHeaderCard(
        1, tr("Calibration"), tr("EXPORT + QC"),
        QStringLiteral("rgba(56,127,181,40)"),
        QStringLiteral("rgba(56,127,181,115)"),
        QStringLiteral("#8ec5ee"),
        tr("Configure export Bayer channel balance and inspect persistent hot pixels across sampled frames."),
        nullptr, page));

    auto *balanceCard = makeCard(page);
    auto *balTop = new QHBoxLayout;
    auto *balTitle = new QLabel(tr("RAW / Bayer Export Balance"), balanceCard);
    balTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 700;"));
    balTop->addWidget(balTitle);
    balTop->addStretch(1);
    balTop->addWidget(makeBadge(
        tr("Affects Export"),
        QStringLiteral("rgba(56,127,181,40)"),
        QStringLiteral("rgba(56,127,181,115)"),
        QStringLiteral("#8ec5ee"), balanceCard));
    static_cast<QVBoxLayout *>(balanceCard->layout())->addLayout(balTop);

    m_rawExportWhiteBalance = new QComboBox(balanceCard);
    m_rawExportWhiteBalance->addItem(tr("Preserve linear sensor values"), 0);
    m_rawExportWhiteBalance->setEnabled(false);
    m_rawExportWhiteBalance->setToolTip(
        tr("Choose how Bayer channel levels are written to the exported SER."));
    connect(m_rawExportWhiteBalance, &QComboBox::currentIndexChanged, this, [this] {
        updateWorkflowSummaries();
    });
    balanceCard->layout()->addWidget(m_rawExportWhiteBalance);

    auto *wbNote = new QLabel(
        tr("Linear preserves normalized sensor values. Camera WB applies EXIF multipliers. Automatic neutral balance measures the target and attenuates stronger channels without clipping highlights."),
        balanceCard);
    wbNote->setWordWrap(true);
    wbNote->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px;"));
    balanceCard->layout()->addWidget(wbNote);
    layout->addWidget(balanceCard);

    auto *hotCard = makeCard(page);
    auto *hotTop = new QHBoxLayout;
    auto *hotTitle = new QLabel(tr("Persistent Hot-Pixel Inspection"), hotCard);
    hotTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 700;"));
    hotTop->addWidget(hotTitle);
    hotTop->addStretch(1);
    hotTop->addWidget(makeBadge(
        tr("Inspection Only"),
        QStringLiteral("rgba(240,198,79,25)"),
        QStringLiteral("rgba(240,198,79,90)"),
        QStringLiteral("#f0c64f"), hotCard));
    static_cast<QVBoxLayout *>(hotCard->layout())->addLayout(hotTop);

    auto *hotTiles = new QHBoxLayout;
    hotTiles->setSpacing(8);
    hotTiles->addWidget(makeMetricTile(
        tr("Persistent Hot Px"), QStringLiteral("—"), tr("Across sampled frames"),
        QStringLiteral("#ff4d5a"), &m_calHotCountTileValue, nullptr, hotCard), 1);
    hotTiles->addWidget(makeMetricTile(
        tr("Sampled Frames"), QStringLiteral("16"), tr("Distributed across source"),
        QStringLiteral("#ffffff"), &m_calSampledTileValue, nullptr, hotCard), 1);
    static_cast<QVBoxLayout *>(hotCard->layout())->addLayout(hotTiles);

    auto *sampleLbl = new QLabel(tr("Frames distributed across recording"), hotCard);
    sampleLbl->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 12px;"));
    hotCard->layout()->addWidget(sampleLbl);
    m_hotPixelSamples = new QComboBox(hotCard);
    m_hotPixelSamples->addItem(tr("8 sampled frames — fast"), 8);
    m_hotPixelSamples->addItem(tr("16 sampled frames — recommended"), 16);
    m_hotPixelSamples->addItem(tr("32 sampled frames — thorough"), 32);
    m_hotPixelSamples->setCurrentIndex(1);
    connect(m_hotPixelSamples, &QComboBox::currentIndexChanged,
            this, &MainWindow::requestHotPixelAnalysis);
    hotCard->layout()->addWidget(m_hotPixelSamples);

    auto addHotSlider = [&](const QString &name, QSlider **member, int minimum,
                            int maximum, int value, const QString &suffix) {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(name, hotCard);
        lbl->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 12px;"));
        auto *valLbl = new QLabel(QStringLiteral("%1%2").arg(value).arg(suffix), hotCard);
        valLbl->setStyleSheet(QStringLiteral("color: #ffffff; font-family: monospace; font-size: 12px;"));
        row->addWidget(lbl);
        row->addStretch(1);
        row->addWidget(valLbl);
        static_cast<QVBoxLayout *>(hotCard->layout())->addLayout(row);
        auto *slider = new QSlider(Qt::Horizontal, hotCard);
        slider->setRange(minimum, maximum);
        slider->setValue(value);
        connect(slider, &QSlider::valueChanged, this, [this, valLbl, suffix](int current) {
            valLbl->setText(QStringLiteral("%1%2").arg(current).arg(suffix));
            requestHotPixelAnalysis();
        });
        hotCard->layout()->addWidget(slider);
        *member = slider;
    };
    addHotSlider(tr("Minimum brightness"), &m_hotPixelBrightness, 32, 255, 200, QString());
    addHotSlider(tr("Local contrast"), &m_hotPixelContrast, 5, 128, 30, QString());
    addHotSlider(tr("Required persistence"), &m_hotPixelPersistence, 50, 100, 80, QStringLiteral("%"));

    m_analyzeHotPixels = new QPushButton(tr("Analyze hot pixels"), hotCard);
    connect(m_analyzeHotPixels, &QPushButton::clicked,
            this, &MainWindow::startHotPixelAnalysis);
    hotCard->layout()->addWidget(m_analyzeHotPixels);

    m_hotPixelLabel = new QLabel(tr("Analysis has not been run."), hotCard);
    m_hotPixelLabel->setWordWrap(true);
    m_hotPixelLabel->setStyleSheet(QStringLiteral("color: #9ca5b3; font-size: 11px;"));
    hotCard->layout()->addWidget(m_hotPixelLabel);
    layout->addWidget(hotCard);

    layout->addStretch(1);
    return page;
}

QWidget *MainWindow::createStabilizeInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    layout->addWidget(makeStepHeaderCard(
        2, tr("Stabilize & Crop"), tr("AFFECTS EXPORT"),
        QStringLiteral("rgba(56,127,181,40)"),
        QStringLiteral("rgba(56,127,181,115)"),
        QStringLiteral("#8ec5ee"),
        tr("Detect the target, center every frame on export, and define safe surrounding crop dimensions."),
        nullptr, page));

    auto *kpiRow = new QHBoxLayout;
    kpiRow->setSpacing(8);
    kpiRow->addWidget(makeMetricTile(
        tr("Detected Object"), QStringLiteral("— × —"), tr("Bounding box (px)"),
        QStringLiteral("#52d273"), &m_stabObjTileValue, nullptr, page), 1);
    kpiRow->addWidget(makeMetricTile(
        tr("Export Canvas"), QStringLiteral("— × —"), tr("÷4 aligned · Bayer-safe"),
        QStringLiteral("#f0c64f"), &m_stabCropTileValue, nullptr, page), 1);
    layout->addLayout(kpiRow);

    // Orientation Card
    auto *orientCard = makeCard(page);
    auto *orientTop = new QHBoxLayout;
    auto *orientTitle = new QLabel(tr("Output Orientation"), orientCard);
    orientTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 700;"));
    m_orientationChip = makeBadge(
        tr("0°"),
        QStringLiteral("#16191f"), QStringLiteral("#323844"),
        QStringLiteral("#aab2bd"), orientCard);
    orientTop->addWidget(orientTitle);
    orientTop->addStretch(1);
    orientTop->addWidget(m_orientationChip);
    static_cast<QVBoxLayout *>(orientCard->layout())->addLayout(orientTop);

    m_orientationGroup = new QButtonGroup(this);
    m_orientationGroup->setExclusive(true);
    auto *rotGrid = new QHBoxLayout;
    rotGrid->setSpacing(5);
    const std::array<QString, 4> rotLabels{
        tr("0°"), tr("90° CW"), tr("180°"), tr("90° CCW")
    };
    for (int turns = 0; turns < 4; ++turns) {
        auto *btn = new QPushButton(rotLabels[static_cast<std::size_t>(turns)], orientCard);
        btn->setCheckable(true);
        btn->setChecked(turns == 0);
        btn->setProperty("sfRole", QStringLiteral("plumToggle"));
        m_orientationGroup->addButton(btn, turns);
        rotGrid->addWidget(btn, 1);
    }
    connect(m_orientationGroup, &QButtonGroup::idClicked, this, [this](int turns) {
        static constexpr std::array<const char *, 4> chips{"0°", "90° CW", "180°", "90° CCW"};
        if (m_orientationChip) {
            m_orientationChip->setText(tr(chips[static_cast<std::size_t>(turns & 3)]));
        }
        requestPreviewRender();
        updateWorkflowSummaries();
    });
    static_cast<QVBoxLayout *>(orientCard->layout())->addLayout(rotGrid);

    auto *flipRow = new QHBoxLayout;
    flipRow->setSpacing(8);
    m_flipHorizontal = new QCheckBox(tr("Flip horizontally"), orientCard);
    m_flipVertical = new QCheckBox(tr("Flip vertically"), orientCard);
    connect(m_flipHorizontal, &QCheckBox::toggled, this, &MainWindow::requestPreviewRender);
    connect(m_flipVertical, &QCheckBox::toggled, this, &MainWindow::requestPreviewRender);
    flipRow->addWidget(m_flipHorizontal);
    flipRow->addWidget(m_flipVertical);
    static_cast<QVBoxLayout *>(orientCard->layout())->addLayout(flipRow);
    layout->addWidget(orientCard);

    // Detection & Crop Card
    auto *cropCard = makeCard(page);
    auto *cropTitle = new QLabel(tr("Target Detection & Margin"), cropCard);
    cropTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 700;"));
    cropCard->layout()->addWidget(cropTitle);

    auto *threshRow = new QHBoxLayout;
    auto *threshName = new QLabel(tr("Detection threshold"), cropCard);
    threshName->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 12px;"));
    auto *threshValueLbl = new QLabel(QStringLiteral("16"), cropCard);
    threshValueLbl->setStyleSheet(QStringLiteral("color: #ffffff; font-family: monospace; font-size: 12px;"));
    threshRow->addWidget(threshName);
    threshRow->addStretch(1);
    threshRow->addWidget(threshValueLbl);
    static_cast<QVBoxLayout *>(cropCard->layout())->addLayout(threshRow);

    m_detectionThreshold = new QSlider(Qt::Horizontal, cropCard);
    m_detectionThreshold->setRange(1, 254);
    m_detectionThreshold->setValue(16);
    connect(m_detectionThreshold, &QSlider::valueChanged, this,
            [this, threshValueLbl](int value) {
                threshValueLbl->setText(QString::number(value));
                if (m_detectionDebounce) m_detectionDebounce->start();
            });
    cropCard->layout()->addWidget(m_detectionThreshold);

    auto *marginLbl = new QLabel(tr("Space around detected object"), cropCard);
    marginLbl->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 12px;"));
    cropCard->layout()->addWidget(marginLbl);
    m_cropMargin = new QComboBox(cropCard);
    m_cropMargin->addItem(tr("10% on each side"), 10);
    m_cropMargin->addItem(tr("20% on each side — recommended"), 20);
    m_cropMargin->addItem(tr("30% on each side"), 30);
    m_cropMargin->addItem(tr("50% on each side"), 50);
    m_cropMargin->addItem(tr("100% on each side — 3× object size"), 100);
    m_cropMargin->addItem(tr("200% on each side — 5× object size"), 200);
    m_cropMargin->addItem(tr("400% on each side — 9× object size"), 400);
    m_cropMargin->addItem(tr("Keep the full source frame"), -1);
    m_cropMargin->setCurrentIndex(1);
    connect(m_cropMargin, &QComboBox::currentIndexChanged, this,
            [this] { if (m_detectionDebounce) m_detectionDebounce->start(); });
    cropCard->layout()->addWidget(m_cropMargin);

    auto *dimLbl = new QLabel(tr("Output dimensions"), cropCard);
    dimLbl->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 12px;"));
    cropCard->layout()->addWidget(dimLbl);
    m_outputSizeMode = new QComboBox(cropCard);
    m_outputSizeMode->addItem(tr("Automatic from target and surrounding space"), QSize());
    m_outputSizeMode->addItem(tr("320 × 240"), QSize(320, 240));
    m_outputSizeMode->addItem(tr("640 × 480"), QSize(640, 480));
    m_outputSizeMode->addItem(tr("800 × 600"), QSize(800, 600));
    m_outputSizeMode->addItem(tr("1024 × 768"), QSize(1024, 768));
    m_outputSizeMode->addItem(tr("Custom dimensions"), QSize(-2, -2));
    cropCard->layout()->addWidget(m_outputSizeMode);

    m_customSizeContainer = new QWidget(cropCard);
    auto *customSize = new QHBoxLayout(m_customSizeContainer);
    customSize->setContentsMargins(0, 0, 0, 0);
    m_customOutputWidth = new QSpinBox(m_customSizeContainer);
    m_customOutputHeight = new QSpinBox(m_customSizeContainer);
    for (QSpinBox *box : {m_customOutputWidth, m_customOutputHeight}) {
        box->setRange(2, 16384);
        box->setSingleStep(4);
    }
    m_customOutputWidth->setValue(640);
    m_customOutputHeight->setValue(480);
    customSize->addWidget(m_customOutputWidth, 1);
    customSize->addWidget(new QLabel(QStringLiteral("×"), m_customSizeContainer));
    customSize->addWidget(m_customOutputHeight, 1);
    m_customSizeContainer->hide();
    cropCard->layout()->addWidget(m_customSizeContainer);

    connect(m_outputSizeMode, &QComboBox::currentIndexChanged, this, [this] {
        const bool custom = m_outputSizeMode->currentData().toSize() == QSize(-2, -2);
        m_customSizeContainer->setVisible(custom);
        updateDetection();
    });
    connect(m_customOutputWidth, &QSpinBox::valueChanged, this, [this] { updateDetection(); });
    connect(m_customOutputHeight, &QSpinBox::valueChanged, this, [this] { updateDetection(); });

    m_detectionLabel = new QLabel(tr("Open a recording to detect its target."), cropCard);
    m_detectionLabel->setWordWrap(true);
    m_detectionLabel->setStyleSheet(QStringLiteral("color: #9ca5b3; font-size: 11px;"));
    cropCard->layout()->addWidget(m_detectionLabel);

    auto *legend = new QLabel(
        tr("<span style='color:#52d273'>● Green</span>: detected object &nbsp; "
           "<span style='color:#f0c64f'>● Gold</span>: export crop (÷4 aligned)"),
        cropCard);
    legend->setWordWrap(true);
    legend->setStyleSheet(QStringLiteral("font-size: 11px; color: #aab2bd;"));
    cropCard->layout()->addWidget(legend);
    layout->addWidget(cropCard);

    layout->addStretch(1);
    return page;
}

QWidget *MainWindow::createQualityInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    layout->addWidget(makeStepHeaderCard(
        3, tr("Quality Ranking"), tr("PENDING"),
        QStringLiteral("#232830"), QStringLiteral("#3a414e"), QStringLiteral("#aab2bd"),
        tr("Rank frames by target sharpness, contrast, exposure stability, and highlight clipping."),
        &m_qualStatusBadge, page));

    auto *kpiRow = new QHBoxLayout;
    kpiRow->setSpacing(8);
    kpiRow->addWidget(makeMetricTile(
        tr("Frames Kept"), QStringLiteral("—"), tr("Run analysis to rank"),
        QStringLiteral("#52d273"), &m_qualKeptTileValue, nullptr, page), 1);
    kpiRow->addWidget(makeMetricTile(
        tr("Cutoff Score"), QStringLiteral("—"), tr("Minimum kept quality"),
        QStringLiteral("#f0c64f"), &m_qualCutoffTileValue, nullptr, page), 1);
    layout->addLayout(kpiRow);

    auto *curveCard = makeCard(page);
    auto *curveHeader = new QHBoxLayout;
    auto *curveTitle = new QLabel(tr("Quality Analysis & Cutoff"), curveCard);
    curveTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 700;"));
    curveHeader->addWidget(curveTitle);
    curveHeader->addStretch(1);
    static_cast<QVBoxLayout *>(curveCard->layout())->addLayout(curveHeader);

    auto *qualityButtons = new QHBoxLayout;
    qualityButtons->setSpacing(6);
    m_analyzeQuality = new QPushButton(tr("Analyze all frames"), curveCard);
    m_analyzeQuality->setProperty("sfRole", QStringLiteral("primary"));
    connect(m_analyzeQuality, &QPushButton::clicked,
            this, &MainWindow::startQualityAnalysis);
    m_cancelQuality = new QPushButton(tr("Cancel"), curveCard);
    m_cancelQuality->setEnabled(false);
    connect(m_cancelQuality, &QPushButton::clicked,
            this, &MainWindow::cancelQualityAnalysis);
    qualityButtons->addWidget(m_analyzeQuality, 1);
    qualityButtons->addWidget(m_cancelQuality);
    static_cast<QVBoxLayout *>(curveCard->layout())->addLayout(qualityButtons);

    m_qualityProgress = new QProgressBar(curveCard);
    m_qualityProgress->setTextVisible(true);
    m_qualityProgress->setFormat(tr("%v / %m frames (%p%)"));
    m_qualityProgress->hide();
    curveCard->layout()->addWidget(m_qualityProgress);

    m_qualityGraph = new QualityGraph(curveCard);
    m_qualityGraph->onFrameSelected = [this](std::uint32_t frameIndex) {
        if (!m_source || !m_source->isOpen()) return;
        m_frameSlider->setValue(static_cast<int>(frameIndex));
        loadFrame(frameIndex);
    };
    curveCard->layout()->addWidget(m_qualityGraph);

    auto *comboGrid = new QGridLayout;
    comboGrid->setSpacing(8);
    auto *keepLbl = new QLabel(tr("Frames to keep"), curveCard);
    keepLbl->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px;"));
    auto *orderLbl = new QLabel(tr("Frame order"), curveCard);
    orderLbl->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px;"));
    comboGrid->addWidget(keepLbl, 0, 0);
    comboGrid->addWidget(orderLbl, 0, 1);

    m_qualityKeep = new QComboBox(curveCard);
    m_qualityKeep->addItem(tr("Best 10% — strict"), 10);
    m_qualityKeep->addItem(tr("Best 25% — recommended"), 25);
    m_qualityKeep->addItem(tr("Best 50%"), 50);
    m_qualityKeep->addItem(tr("Best 75%"), 75);
    m_qualityKeep->addItem(tr("100% — keep all"), 100);
    m_qualityKeep->setCurrentIndex(1);
    connect(m_qualityKeep, &QComboBox::currentIndexChanged,
            this, &MainWindow::updateQualityDisplay);

    m_qualityOrder = new QComboBox(curveCard);
    m_qualityOrder->addItem(tr("Original capture order"), 0);
    m_qualityOrder->addItem(tr("Quality score — best first"), 1);
    connect(m_qualityOrder, &QComboBox::currentIndexChanged,
            this, &MainWindow::updateQualityDisplay);

    comboGrid->addWidget(m_qualityKeep, 1, 0);
    comboGrid->addWidget(m_qualityOrder, 1, 1);
    static_cast<QVBoxLayout *>(curveCard->layout())->addLayout(comboGrid);

    auto *clipRow = new QHBoxLayout;
    m_rejectClipped = new QCheckBox(tr("Reject overexposed (max clipped)"), curveCard);
    m_rejectClipped->setChecked(true);
    m_maxClippedPercent = new QSpinBox(curveCard);
    m_maxClippedPercent->setRange(1, 100);
    m_maxClippedPercent->setValue(5);
    m_maxClippedPercent->setSuffix(QStringLiteral("%"));
    connect(m_rejectClipped, &QCheckBox::toggled,
            m_maxClippedPercent, &QSpinBox::setEnabled);
    connect(m_rejectClipped, &QCheckBox::toggled,
            this, &MainWindow::updateQualityDisplay);
    connect(m_maxClippedPercent, &QSpinBox::valueChanged,
            this, &MainWindow::updateQualityDisplay);
    clipRow->addWidget(m_rejectClipped, 1);
    clipRow->addWidget(m_maxClippedPercent);
    static_cast<QVBoxLayout *>(curveCard->layout())->addLayout(clipRow);

    m_clippedRejectedCountLabel = new QLabel(tr("No clipped frames counted yet."), curveCard);
    m_clippedRejectedCountLabel->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px;"));
    curveCard->layout()->addWidget(m_clippedRejectedCountLabel);

    m_qualityLabel = new QLabel(tr("Quality has not been analyzed."), curveCard);
    m_qualityLabel->setWordWrap(true);
    m_qualityLabel->setStyleSheet(QStringLiteral("color: #9ca5b3; font-size: 11px;"));
    curveCard->layout()->addWidget(m_qualityLabel);
    layout->addWidget(curveCard);

    // Filterable Frame List Card
    auto *tableCard = makeCard(page);
    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(4);
    m_qualityFilterGroup = new QButtonGroup(this);
    m_qualityFilterGroup->setExclusive(true);
    auto addFilterChip = [&](const QString &label, int id, bool checked) {
        auto *btn = new QPushButton(label, tableCard);
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setProperty("sfRole", QStringLiteral("filterChip"));
        m_qualityFilterGroup->addButton(btn, id);
        filterRow->addWidget(btn);
    };
    addFilterChip(tr("All"), 0, true);
    addFilterChip(tr("Kept"), 1, false);
    addFilterChip(tr("Clipped"), 2, false);
    connect(m_qualityFilterGroup, &QButtonGroup::idClicked,
            this, &MainWindow::updateQualityDisplay);
    filterRow->addStretch(1);
    auto *clickHint = new QLabel(tr("Click row to inspect"), tableCard);
    clickHint->setStyleSheet(QStringLiteral("color: #7e8795; font-size: 11px;"));
    filterRow->addWidget(clickHint);
    static_cast<QVBoxLayout *>(tableCard->layout())->addLayout(filterRow);

    m_qualityTable = new QTableWidget(0, 3, tableCard);
    m_qualityTable->setHorizontalHeaderLabels({tr("Frame"), tr("Score"), tr("Status")});
    m_qualityTable->verticalHeader()->hide();
    m_qualityTable->horizontalHeader()->setStretchLastSection(true);
    m_qualityTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_qualityTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_qualityTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_qualityTable->setMinimumHeight(210);

    auto inspectQualityRow = [this](int row, int) {
        if (!m_source || !m_source->isOpen()) return;
        const auto *item = m_qualityTable->item(row, 0);
        if (!item) return;
        const int frameIndex = item->data(Qt::UserRole).toInt();
        m_frameSlider->setValue(frameIndex);
        loadFrame(static_cast<std::uint32_t>(frameIndex));
    };
    connect(m_qualityTable, &QTableWidget::cellClicked, this, inspectQualityRow);
    connect(m_qualityTable, &QTableWidget::cellDoubleClicked, this, inspectQualityRow);
    tableCard->layout()->addWidget(m_qualityTable);
    layout->addWidget(tableCard);

    layout->addStretch(1);
    return page;
}

QWidget *MainWindow::createExportInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    layout->addWidget(makeStepHeaderCard(
        4, tr("Export SER"), tr("READY"),
        QStringLiteral("rgba(56,127,181,40)"),
        QStringLiteral("rgba(56,127,181,115)"),
        QStringLiteral("#8ec5ee"),
        tr("Write a centered, cropped SER recording beside the source without altering original files."),
        &m_expStatusBadge, page));

    auto *kpiRow = new QHBoxLayout;
    kpiRow->setSpacing(8);
    kpiRow->addWidget(makeMetricTile(
        tr("Output Frames"), QStringLiteral("—"), tr("Waiting for source"),
        QStringLiteral("#ffffff"), &m_expFramesTileValue, &m_expFramesTileSub, page), 1);
    kpiRow->addWidget(makeMetricTile(
        tr("Target Peak → Out"), QStringLiteral("—"), tr("Intensity 1.00×"),
        QStringLiteral("#f0c64f"), &m_expPeakTileValue, &m_expPeakTileSub, page), 1);
    layout->addLayout(kpiRow);

    auto *optionsCard = makeCard(page);
    auto *fmtLbl = new QLabel(tr("Format"), optionsCard);
    fmtLbl->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 12px;"));
    optionsCard->layout()->addWidget(fmtLbl);
    auto *format = new QComboBox(optionsCard);
    format->addItem(tr("SER recording — preserve source pixel depth"));
    format->setEnabled(false);
    optionsCard->layout()->addWidget(format);

    auto *intLbl = new QLabel(tr("Output intensity"), optionsCard);
    intLbl->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 12px;"));
    optionsCard->layout()->addWidget(intLbl);
    m_exportIntensity = new QComboBox(optionsCard);
    m_exportIntensity->addItem(tr("Preserve source intensity (1×)"), 1.0);
    m_exportIntensity->addItem(tr("Automatic — target peak near 90%"), 0.0);
    m_exportIntensity->addItem(tr("Increase values 2×"), 2.0);
    m_exportIntensity->addItem(tr("Increase values 4×"), 4.0);
    m_exportIntensity->addItem(tr("Increase values 8×"), 8.0);
    m_exportIntensity->addItem(tr("Increase values 16×"), 16.0);
    connect(m_exportIntensity, &QComboBox::currentIndexChanged, this, [this] {
        updateExportIntensityDisplay();
        updateWorkflowSummaries();
    });
    optionsCard->layout()->addWidget(m_exportIntensity);

    m_exportIntensityLabel = new QLabel(tr("Load a source to measure its intensity."), optionsCard);
    m_exportIntensityLabel->setWordWrap(true);
    m_exportIntensityLabel->setStyleSheet(QStringLiteral("color: #9ca5b3; font-size: 11px;"));
    optionsCard->layout()->addWidget(m_exportIntensityLabel);

    auto *monoBox = new QFrame(optionsCard);
    monoBox->setProperty("sfRole", QStringLiteral("inset"));
    monoBox->setAttribute(Qt::WA_StyledBackground, true);
    auto *monoLayout = new QVBoxLayout(monoBox);
    monoLayout->setContentsMargins(10, 8, 10, 8);
    monoLayout->setSpacing(5);

    m_exportMonochrome = new QCheckBox(tr("Export in monochrome"), monoBox);
    m_exportMonochrome->setToolTip(
        tr("Convert to monochrome at full resolution. The source file is never changed."));
    auto *monoWarn = new QLabel(
        tr("Exported copy permanently discards color information."), monoBox);
    monoWarn->setWordWrap(true);
    monoWarn->setStyleSheet(QStringLiteral("color: #f0c96a; font-size: 11px; font-weight: 600;"));
    monoWarn->hide();

    m_exportMonoBinning = new QCheckBox(tr("2×2 binning (half width and height)"), monoBox);
    m_exportMonoBinning->setEnabled(false);
    connect(m_exportMonochrome, &QCheckBox::toggled, this, [this, monoWarn](bool checked) {
        m_exportMonoBinning->setEnabled(checked);
        monoWarn->setVisible(checked);
        updateWorkflowSummaries();
    });
    connect(m_exportMonoBinning, &QCheckBox::toggled,
            this, [this] { updateWorkflowSummaries(); });
    monoLayout->addWidget(m_exportMonochrome);
    monoLayout->addWidget(monoWarn);
    monoLayout->addWidget(m_exportMonoBinning);
    optionsCard->layout()->addWidget(monoBox);
    layout->addWidget(optionsCard);

    // Summary Card
    auto *summaryCard = makeCard(page);
    auto *sumTitle = new QLabel(tr("Export Summary Checklist"), summaryCard);
    sumTitle->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 700;"));
    summaryCard->layout()->addWidget(sumTitle);
    m_exportSummary = new QLabel(
        tr("Output will be placed beside the source in a timestamped seeingforge_yyyymmdd_hhmm folder."),
        summaryCard);
    m_exportSummary->setWordWrap(true);
    m_exportSummary->setStyleSheet(QStringLiteral("color: #b4bcc8; font-size: 12px;"));
    summaryCard->layout()->addWidget(m_exportSummary);
    layout->addWidget(summaryCard);

    // Export Action Controls
    auto *actionCard = makeCard(page);
    auto *exportButtons = new QHBoxLayout;
    exportButtons->setSpacing(6);
    m_exportButton = new QPushButton(tr("Export SER"), actionCard);
    m_exportButton->setProperty("sfRole", QStringLiteral("primary"));
    connect(m_exportButton, &QPushButton::clicked, this, &MainWindow::startExport);
    m_cancelExport = new QPushButton(tr("Cancel"), actionCard);
    m_cancelExport->setEnabled(false);
    connect(m_cancelExport, &QPushButton::clicked, this, &MainWindow::cancelExport);
    exportButtons->addWidget(m_exportButton, 1);
    exportButtons->addWidget(m_cancelExport);
    static_cast<QVBoxLayout *>(actionCard->layout())->addLayout(exportButtons);

    m_exportProgress = new QProgressBar(actionCard);
    m_exportProgress->setTextVisible(true);
    m_exportProgress->setFormat(tr("%v / %m frames (%p%)"));
    m_exportProgress->hide();
    actionCard->layout()->addWidget(m_exportProgress);

    m_exportResultLabel = new QLabel(tr("Nothing exported yet."), actionCard);
    m_exportResultLabel->setWordWrap(true);
    m_exportResultLabel->setStyleSheet(QStringLiteral("color: #9ca5b3; font-size: 11px;"));
    actionCard->layout()->addWidget(m_exportResultLabel);
    layout->addWidget(actionCard);

    // Post-Export Quick Actions Banner
    m_exportDoneBanner = new QFrame(page);
    m_exportDoneBanner->setObjectName(QStringLiteral("exportDoneBanner"));
    m_exportDoneBanner->setAttribute(Qt::WA_StyledBackground, true);
    m_exportDoneBanner->setStyleSheet(QStringLiteral(
        "#exportDoneBanner { background: rgba(82,210,115,24); border: 1px solid rgba(82,210,115,90); border-radius: 12px; }"));
    auto *doneLayout = new QVBoxLayout(m_exportDoneBanner);
    doneLayout->setContentsMargins(12, 10, 12, 10);
    doneLayout->setSpacing(8);
    m_exportDoneText = new QLabel(tr("Export complete."), m_exportDoneBanner);
    m_exportDoneText->setWordWrap(true);
    m_exportDoneText->setStyleSheet(QStringLiteral("color: #52d273; font-size: 12px; font-weight: 700;"));
    doneLayout->addWidget(m_exportDoneText);

    auto *doneBtns = new QHBoxLayout;
    doneBtns->setSpacing(6);
    auto *openFolderBtn = new QPushButton(tr("Open Folder"), m_exportDoneBanner);
    connect(openFolderBtn, &QPushButton::clicked, this, [this] {
        if (!m_lastExportPath.isEmpty()) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(m_lastExportPath).absolutePath()));
        }
    });
    auto *inspectOutputBtn = new QPushButton(tr("Inspect Output"), m_exportDoneBanner);
    inspectOutputBtn->setStyleSheet(QStringLiteral("color: #52d273; border-color: rgba(82,210,115,95);"));
    connect(inspectOutputBtn, &QPushButton::clicked, this, [this] {
        if (!m_lastExportPath.isEmpty() && QFileInfo::exists(m_lastExportPath)) {
            loadSerFile(m_lastExportPath);
        }
    });
    auto *copyPathBtn = new QPushButton(tr("Copy Path"), m_exportDoneBanner);
    connect(copyPathBtn, &QPushButton::clicked, this, [this] {
        if (!m_lastExportPath.isEmpty() && QGuiApplication::clipboard()) {
            QGuiApplication::clipboard()->setText(m_lastExportPath);
            showStatus(tr("Copied export file path to clipboard."), 4000);
        }
    });
    doneBtns->addWidget(openFolderBtn, 1);
    doneBtns->addWidget(inspectOutputBtn, 1);
    doneBtns->addWidget(copyPathBtn);
    doneLayout->addLayout(doneBtns);
    m_exportDoneBanner->hide();
    layout->addWidget(m_exportDoneBanner);

    layout->addStretch(1);
    return page;
}

QWidget *MainWindow::createFooterBar()
{
    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("footerBar"));
    footer->setAttribute(Qt::WA_StyledBackground, true);
    footer->setFixedHeight(52);
    auto *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(12);

    auto *resetBtn = new QPushButton(tr("↺ Reset Step"), footer);
    resetBtn->setToolTip(tr("Reset the active step to default parameters"));
    connect(resetBtn, &QPushButton::clicked, this, &MainWindow::resetCurrentStep);
    layout->addWidget(resetBtn);

    m_footerStatusLabel = new QLabel(footer);
    m_footerStatusLabel->setStyleSheet(QStringLiteral("color: #9ba4b2; font-size: 12px;"));
    layout->addWidget(m_footerStatusLabel, 1);

    m_prevStepButton = new QPushButton(tr("← Back"), footer);
    connect(m_prevStepButton, &QPushButton::clicked, this, [this] {
        if (m_activeStep > 0) selectWorkflowStep(m_activeStep - 1);
    });
    layout->addWidget(m_prevStepButton);

    m_nextStepButton = new QPushButton(tr("Continue to Calibration →"), footer);
    m_nextStepButton->setProperty("sfRole", QStringLiteral("primary"));
    connect(m_nextStepButton, &QPushButton::clicked, this, [this] {
        if (m_activeStep < 4) selectWorkflowStep(m_activeStep + 1);
        else startExport();
    });
    layout->addWidget(m_nextStepButton);

    return footer;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        QMainWindow::dropEvent(event);
        return;
    }
    QStringList localPaths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) localPaths.append(url.toLocalFile());
    }
    if (localPaths.isEmpty()) return;

    if (localPaths.size() == 1) {
        const QFileInfo info(localPaths.constFirst());
        if (info.isDir()) {
            QDir directory(info.absoluteFilePath());
            QStringList files = directory.entryList(rawExtensionFilters(), QDir::Files, QDir::Name);
            for (QString &file : files) file = directory.absoluteFilePath(file);
            if (files.isEmpty()) {
                QMessageBox::information(this, tr("Open RAW Folder"),
                                         tr("No supported RAW files were found in the dropped folder."));
                return;
            }
            loadRawFiles(files);
            event->acceptProposedAction();
            return;
        }
        if (info.suffix().compare(QStringLiteral("ser"), Qt::CaseInsensitive) == 0) {
            loadSerFile(info.absoluteFilePath());
            event->acceptProposedAction();
            return;
        }
    }

    QStringList rawFiles;
    for (const QString &path : localPaths) {
        if (isRawFilePath(path)) rawFiles.append(path);
    }
    if (!rawFiles.isEmpty()) {
        loadRawFiles(rawFiles);
        event->acceptProposedAction();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_previewScrollArea && watched == m_previewScrollArea->viewport()) {
        if (event->type() == QEvent::Wheel && m_source && m_source->isOpen()) {
            const auto *wheelEvent = static_cast<QWheelEvent *>(event);
            const int deltaY = wheelEvent->angleDelta().y();
            if (deltaY > 0) zoomIn();
            else if (deltaY < 0) zoomOut();
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress && !m_fitToWindow
            && m_source && m_source->isOpen()) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_panning = true;
                m_panLastPos = mouseEvent->globalPosition().toPoint();
                m_previewScrollArea->viewport()->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove && m_panning) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint current = mouseEvent->globalPosition().toPoint();
            const QPoint delta = current - m_panLastPos;
            m_panLastPos = current;
            auto *hBar = m_previewScrollArea->horizontalScrollBar();
            auto *vBar = m_previewScrollArea->verticalScrollBar();
            hBar->setValue(hBar->value() - delta.x());
            vBar->setValue(vBar->value() - delta.y());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && m_panning) {
            m_panning = false;
            m_previewScrollArea->viewport()->setCursor(
                m_fitToWindow ? Qt::ArrowCursor : Qt::OpenHandCursor);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::openSerFile()
{
    QSettings settings;
    const QString savedFolder = settings.value(
        QStringLiteral("files/lastOpenFolder")).toString();
    const QString initialFolder = QDir(savedFolder).exists()
        ? savedFolder
        : (m_source && m_source->isOpen() ? m_source->sourceDirectory()
                              : QDir::homePath());

    QFileDialog dialog(this, tr("Open SER Recording"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilters({tr("SER recordings (*.ser *.SER)"), tr("All files (*)")});
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setDirectory(initialFolder);
    dialog.resize(1050, 650);
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
        loadSerFile(dialog.selectedFiles().constFirst());
}

void MainWindow::openRawFiles()
{
    QSettings settings;
    QFileDialog dialog(this, tr("Open RAW Image Sequence"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilters({tr("Camera RAW files (*.cr2 *.CR2 *.cr3 *.CR3 *.nef *.NEF *.arw *.ARW *.dng *.DNG *.raf *.RAF *.orf *.ORF *.rw2 *.RW2)"),
                           tr("All files (*)")});
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setDirectory(settings.value(QStringLiteral("files/lastOpenFolder"), QDir::homePath()).toString());
    dialog.resize(1050, 650);
    if (dialog.exec() == QDialog::Accepted) loadRawFiles(dialog.selectedFiles());
}

void MainWindow::openRawFolder()
{
    QSettings settings;
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Open RAW Folder"),
        settings.value(QStringLiteral("files/lastOpenFolder"), QDir::homePath()).toString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (folder.isEmpty()) return;
    QDir directory(folder);
    QStringList files = directory.entryList(rawExtensionFilters(), QDir::Files, QDir::Name);
    for (QString &file : files) file = directory.absoluteFilePath(file);
    loadRawFiles(files);
}

void MainWindow::loadSerFile(const QString &path)
{
    auto source = std::make_shared<seeingforge::SerFile>();
    if (!source->open(path)) {
        QMessageBox::critical(this, tr("Cannot Open SER File"), source->errorString());
        return;
    }
    m_source = std::move(source);
    QSettings settings;
    settings.setValue(QStringLiteral("files/lastOpenFolder"),
                      QFileInfo(path).absolutePath());
    finishLoadingSource();
}

void MainWindow::loadRawFiles(const QStringList &selectedPaths)
{
    if (selectedPaths.isEmpty()) return;
    if (m_rawLoadWatcher->isRunning()) {
        QMessageBox::information(this, tr("RAW Sequence Loading"),
                                 tr("Another RAW sequence is already being loaded."));
        return;
    }
    QStringList paths = selectedPaths;
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(paths.begin(), paths.end(), [&collator](const QString &left, const QString &right) {
        return collator.compare(QFileInfo(left).fileName(), QFileInfo(right).fileName()) < 0;
    });
    m_rawLoadProgress->setRange(0, paths.size());
    m_rawLoadProgress->setValue(0);
    m_rawLoadLabel->setText(tr("Validating %1 RAW frames…").arg(paths.size()));
    m_rawLoadCancelButton->setEnabled(true);
    m_rawLoadDialog->show();
    showStatus(tr("Loading RAW sequence…"));

    m_rawLoadCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelFlag = m_rawLoadCancel;
    m_rawLoadWatcher->setFuture(QtConcurrent::run(
        [paths, cancelFlag](QPromise<std::shared_ptr<seeingforge::RawSequence>> &promise) {
            promise.setProgressRange(0, static_cast<int>(paths.size()));
            auto source = std::make_shared<seeingforge::RawSequence>();
            const bool loaded = source->open(paths,
                [&promise](int completed, int) {
                    promise.setProgressValue(completed);
                },
                [cancelFlag] { return cancelFlag->load(); });
            Q_UNUSED(loaded);
            promise.addResult(std::move(source));
        }));
}

void MainWindow::rawSequenceLoadFinished()
{
    const auto source = m_rawLoadWatcher->result();
    const bool canceled = m_rawLoadCancel && m_rawLoadCancel->load();
    m_rawLoadDialog->hide();
    m_rawLoadCancel.reset();
    if (canceled) {
        showStatus(tr("RAW sequence loading canceled."), 5000);
        return;
    }
    if (!source || !source->isOpen()) {
        QMessageBox::critical(this, tr("Cannot Open RAW Sequence"),
                              source ? source->errorString() : tr("The RAW sequence could not be loaded."));
        showStatus(tr("RAW sequence loading failed."), 5000);
        return;
    }
    m_source = source;
    QSettings settings;
    settings.setValue(QStringLiteral("files/lastOpenFolder"), m_source->sourceDirectory());
    finishLoadingSource();
}

void MainWindow::finishLoadingSource()
{
    if (m_playing) togglePlayback();
    m_frameCache.clear();
    m_currentFrame = 0;

    {
        const QSignalBlocker sliderBlocker(m_frameSlider);
        m_frameSlider->setRange(0, static_cast<int>(m_source->header().frameCount - 1));
        m_frameSlider->setValue(0);
    }
    const bool multiFrame = m_source->header().frameCount > 1;
    m_frameSlider->setEnabled(true);
    m_prevFrameBtn->setEnabled(multiFrame);
    m_nextFrameBtn->setEnabled(multiFrame);
    m_playButton->setEnabled(multiFrame);

    m_debayerMethod->setEnabled(seeingforge::PreviewProcessor::supportsDebayer(m_source->header().colorId));
    {
        const QSignalBlocker bayerBlocker(m_bayerPattern);
        m_bayerPattern->setItemText(0, tr("From source — %1").arg(m_source->header().colorName()));
        m_bayerPattern->setCurrentIndex(0);
    }
    m_bayerWarning->hide();

    m_hotPixels = {};
    m_hotPixelLabel->setText(tr("Analysis has not been run."));
    m_calHotCountTileValue->setText(QStringLiteral("—"));

    m_qualityResult = {};
    m_qualityTable->setRowCount(0);
    m_qualityGraph->setScores({});
    m_qualityLabel->setText(tr("Quality has not been analyzed."));
    m_qualKeptTileValue->setText(QStringLiteral("—"));
    m_qualCutoffTileValue->setText(QStringLiteral("—"));
    m_clippedRejectedCountLabel->setText(tr("No clipped frames counted yet."));
    m_qualStatusBadge->setText(tr("PENDING"));
    m_qualStatusBadge->setStyleSheet(QStringLiteral(
        "background: #232830; border: 1px solid #3a414e; color: #aab2bd;"
        "border-radius: 9px; padding: 2px 8px; font-size: 10px; font-weight: 700;"));

    m_exportDoneBanner->hide();
    m_exportResultLabel->setText(tr("Nothing exported yet."));

    selectWorkflowStep(0);
    loadFrame(0);
    checkBayerMetadata();

    {
        const QSignalBlocker wbBlocker(m_rawExportWhiteBalance);
        m_rawExportWhiteBalance->clear();
        m_rawExportWhiteBalance->addItem(tr("Preserve linear sensor values"), 0);
        if (m_source->hasCameraWhiteBalance())
            m_rawExportWhiteBalance->addItem(tr("Apply camera white balance"), 1);
        if (seeingforge::PreviewProcessor::supportsDebayer(effectiveColorId()))
            m_rawExportWhiteBalance->addItem(tr("Apply automatic neutral balance"), 2);
        const int preferredBalance = QSettings().value(
            QStringLiteral("preferences/calibration/sensorBalance"), 0).toInt();
        const int preferredBalanceIndex = m_rawExportWhiteBalance->findData(preferredBalance);
        m_rawExportWhiteBalance->setCurrentIndex(
            preferredBalanceIndex >= 0 ? preferredBalanceIndex : 0);
        m_rawExportWhiteBalance->setEnabled(m_rawExportWhiteBalance->count() > 1);
    }

    updateMetadata();
    updateHeroSummary();
    updateWorkflowSummaries();
    updateFrameQualityReadout();
    setWindowTitle(QStringLiteral("%1 — SeeingForge").arg(m_source->displayName()));
    m_defaultStatusMessage = tr("Loaded %1 · %2 frames (%3 × %4 px)")
                                 .arg(m_source->displayName())
                                 .arg(m_source->header().frameCount)
                                 .arg(m_source->header().width)
                                 .arg(m_source->header().height);
    showStatus(m_defaultStatusMessage);
}

void MainWindow::checkBayerMetadata()
{
    if (!std::dynamic_pointer_cast<seeingforge::SerFile>(m_source)) return;
    const auto analysis = seeingforge::BayerPatternAnalyzer::analyze(
        m_rawFrame, m_source->header().colorId);
    if (!analysis.discrepancy) return;

    const int suggestedIndex = m_bayerPattern->findData(analysis.suggestedColorId);
    if (suggestedIndex >= 0) m_bayerPattern->setCurrentIndex(suggestedIndex);
    seeingforge::SerHeader suggestedHeader = m_source->header();
    suggestedHeader.colorId = analysis.suggestedColorId;
    m_bayerWarning->setText(
        tr("<b>Probable Bayer mismatch:</b> metadata says %1; the mosaic looks like %2. "
           "ROI capture commonly causes this. SeeingForge selected %2. Open the Display popover, "
           "temporarily increase Saturation, and compare patterns.")
            .arg(m_source->header().colorName(), suggestedHeader.colorName()));
    m_bayerWarning->show();
    QMessageBox::warning(
        this, tr("Probable Bayer Metadata Mismatch"),
        tr("The recorded Bayer pattern (%1) does not match the pixel mosaic. "
           "SeeingForge detected %2 as the probable pattern and selected it automatically.\n\n"
           "This commonly happens when a camera ROI was used and the capture software stored "
           "the full-sensor Bayer pattern instead of the ROI-adjusted pattern. Review the "
           "selection in Source & Sensor if necessary. The selected pattern will be used for "
           "preview and export.")
            .arg(m_source->header().colorName(), suggestedHeader.colorName()));
}

QImage MainWindow::readCachedFrame(std::uint32_t index)
{
    if (!m_source || !m_source->isOpen()) return {};
    for (qsizetype i = 0; i < m_frameCache.size(); ++i) {
        if (m_frameCache[i].first == index) {
            if (i > 0) m_frameCache.move(i, 0);
            return m_frameCache.first().second;
        }
    }
    const QImage frame = m_source->readFrame(index);
    if (!frame.isNull()) {
        m_frameCache.prepend(qMakePair(index, frame));
        while (m_frameCache.size() > 8) m_frameCache.removeLast();
    }
    return frame;
}

void MainWindow::loadFrame(std::uint32_t index)
{
    const QImage frame = readCachedFrame(index);
    if (frame.isNull()) {
        QMessageBox::critical(this, tr("Cannot Read Frame"), tr("Frame %1 could not be decoded.").arg(index + 1));
        return;
    }
    m_rawFrame = frame;
    m_currentFrame = index;
    m_emptyHub->hide();
    m_imageLabel->show();
    m_frameLabel->setText(tr("Frame %1 / %2").arg(index + 1).arg(m_source->header().frameCount));
    updateFrameQualityReadout();
    updateExportIntensityDisplay();
    requestPreviewRender();
    if (m_detectionDebounce) m_detectionDebounce->start();
}

void MainWindow::togglePlayback()
{
    if (!m_source || !m_source->isOpen() || m_source->header().frameCount <= 1) {
        m_playing = false;
        m_playTimer->stop();
        if (m_playButton) m_playButton->setText(QStringLiteral("▶"));
        return;
    }
    m_playing = !m_playing;
    m_playButton->setText(m_playing ? QStringLiteral("❚❚") : QStringLiteral("▶"));
    if (m_playing) m_playTimer->start();
    else m_playTimer->stop();
}

void MainWindow::stepFrame(int delta)
{
    if (!m_source || !m_source->isOpen() || m_source->header().frameCount == 0) return;
    const int count = static_cast<int>(m_source->header().frameCount);
    const int next = std::clamp(static_cast<int>(m_currentFrame) + delta, 0, count - 1);
    if (next == static_cast<int>(m_currentFrame)) return;
    {
        const QSignalBlocker blocker(m_frameSlider);
        m_frameSlider->setValue(next);
    }
    loadFrame(static_cast<std::uint32_t>(next));
}

void MainWindow::toggleDisplayPopover()
{
    if (!m_displayPopover) return;
    const bool show = !m_displayPopover->isVisible();
    if (m_displayPopoverButton) m_displayPopoverButton->setChecked(show);
    if (show) {
        positionDisplayPopover();
        m_displayPopover->show();
        m_displayPopover->raise();
    } else {
        m_displayPopover->hide();
    }
}

void MainWindow::positionDisplayPopover()
{
    if (!m_displayPopover || !m_previewPanel || !m_overlayStrip) return;
    m_displayPopover->adjustSize();
    const int popW = 322;
    const int popH = std::max(m_displayPopover->sizeHint().height(), 360);
    m_displayPopover->resize(popW, popH);
    const int x = std::max(12, m_previewPanel->width() - popW - 14);
    const int y = m_overlayStrip->geometry().bottom() + 8;
    m_displayPopover->move(x, y);
    if (m_displayPopover->isVisible()) m_displayPopover->raise();
}

void MainWindow::resetDisplayControls()
{
    {
        const QSignalBlocker b1(m_debayerMethod);
        const QSignalBlocker b2(m_whiteBalance);
        const QSignalBlocker b3(m_redBalance);
        const QSignalBlocker b4(m_blueBalance);
        const QSignalBlocker b5(m_saturation);
        const QSignalBlocker b6(m_hue);
        const QSignalBlocker b7(m_gain);
        const QSignalBlocker b8(m_gamma);
        const QSignalBlocker b9(m_autoStretch);
        m_debayerMethod->setCurrentIndex(0);
        m_whiteBalance->setChecked(true);
        m_redBalance->setValue(100);
        m_blueBalance->setValue(100);
        m_saturation->setValue(100);
        m_hue->setValue(0);
        m_gain->setValue(100);
        m_gamma->setValue(100);
        m_autoStretch->setChecked(true);
    }
    QSettings settings;
    settings.remove(QStringLiteral("preferences/colour"));
    requestPreviewRender();
    showStatus(tr("Preview display controls reset to defaults."), 4000);
}

int MainWindow::currentOrientationTurns() const
{
    return m_orientationGroup ? std::max(0, m_orientationGroup->checkedId()) : 0;
}

std::uint32_t MainWindow::referenceFrameIndex() const
{
    if (!m_source || !m_source->isOpen()) return 0;
    if (!m_qualityResult.isEmpty() && !m_qualityResult.rankedFrameIndices.isEmpty()) {
        QVector<double> clippedByIndex(
            static_cast<qsizetype>(m_source->header().frameCount), 0.0);
        for (const auto &frame : m_qualityResult.frames)
            clippedByIndex[static_cast<qsizetype>(frame.frameIndex)] = frame.clippedPercent;
        for (const std::uint32_t frameIndex : m_qualityResult.rankedFrameIndices) {
            if (!m_rejectClipped->isChecked()
                || clippedByIndex[static_cast<qsizetype>(frameIndex)]
                       <= m_maxClippedPercent->value()) {
                return frameIndex;
            }
        }
        return m_qualityResult.rankedFrameIndices.front();
    }
    return m_currentFrame;
}

seeingforge::PreviewSettings MainWindow::previewSettings() const
{
    seeingforge::PreviewSettings settings;
    settings.debayer = m_debayerMethod->currentData().toBool();
    settings.autoStretch = m_autoStretch->isChecked();
    settings.whiteBalance = m_whiteBalance->isChecked();
    settings.redBalance = m_redBalance->value() / 100.0;
    settings.blueBalance = m_blueBalance->value() / 100.0;
    settings.saturation = m_saturation->value() / 100.0;
    settings.hueShiftDegrees = m_hue->value();
    settings.gain = m_gain->value() / 100.0;
    settings.gamma = m_gamma->value() / 100.0;
    settings.rotationQuarterTurns = currentOrientationTurns();
    settings.flipHorizontal = m_flipHorizontal->isChecked();
    settings.flipVertical = m_flipVertical->isChecked();
    return settings;
}

std::uint32_t MainWindow::effectiveColorId() const
{
    if (m_bayerPattern && m_bayerPattern->currentData().toUInt() != 0)
        return m_bayerPattern->currentData().toUInt();
    return m_source ? m_source->header().colorId : 0U;
}

void MainWindow::requestPreviewRender()
{
    if (m_rawFrame.isNull() || !m_previewWatcher) return;
    if (m_previewWatcher->isRunning()) {
        m_renderDirty = true;
        return;
    }
    startPreviewRender();
}

void MainWindow::startPreviewRender()
{
    m_renderDirty = false;
    m_previewStateLabel->setText(tr("Rendering preview…"));
    m_statusDot->setStyleSheet(QStringLiteral("background: #f0c64f; border-radius: 4px;"));
    const QImage frame = m_rawFrame;
    const std::uint32_t colorId = effectiveColorId();
    const auto settings = previewSettings();
    m_previewWatcher->setFuture(QtConcurrent::run([frame, colorId, settings] {
        return seeingforge::PreviewProcessor::render(frame, colorId, settings);
    }));
}

void MainWindow::previewRenderFinished()
{
    m_previews = m_previewWatcher->result();
    m_previewStateLabel->setText(tr("Preview ready"));
    m_statusDot->setStyleSheet(QStringLiteral("background: #52d273; border-radius: 4px;"));
    updateImage();
    if (m_renderDirty) startPreviewRender();
}

void MainWindow::updateDetection()
{
    if (m_rawFrame.isNull() || !m_detectionThreshold || !m_cropMargin) return;
    m_detection = seeingforge::ObjectDetector::detect(
        m_rawFrame, m_detectionThreshold->value(), m_cropMargin->currentData().toInt());
    if (m_detection.found()) {
        QSize requested = m_outputSizeMode->currentData().toSize();
        if (requested == QSize(-2, -2))
            requested = QSize(m_customOutputWidth->value(), m_customOutputHeight->value());
        const QSize size = exportCropSize(m_rawFrame.size(), m_detection.objectRect,
                                          m_cropMargin->currentData().toInt(), requested);
        QPoint origin = m_detection.objectRect.center() - QPoint(size.width() / 2, size.height() / 2);
        if (m_source->header().colorId >= 8 && m_source->header().colorId <= 19)
            origin = QPoint(origin.x() & ~1, origin.y() & ~1);
        m_detection.cropRect = QRect(origin, size);
        const QString objText = QStringLiteral("%1 × %2")
                                    .arg(m_detection.objectRect.width())
                                    .arg(m_detection.objectRect.height());
        const QString cropText = QStringLiteral("%1 × %2")
                                     .arg(m_detection.cropRect.width())
                                     .arg(m_detection.cropRect.height());
        m_stabObjTileValue->setText(objText);
        m_stabCropTileValue->setText(cropText);
        m_canvasObjectBadge->setText(tr("Object: %1 px").arg(objText));
        m_canvasCropBadge->setText(tr("Crop: %1 px").arg(cropText));
        m_detectionLabel->setText(
            tr("Target %1 px · Recommended export canvas %2 px (rounded up to ÷4).")
                .arg(objText, cropText));
    } else {
        m_stabObjTileValue->setText(tr("None"));
        m_stabCropTileValue->setText(QStringLiteral("— × —"));
        m_canvasObjectBadge->setText(tr("Object: none"));
        m_canvasCropBadge->setText(tr("Crop: full"));
        m_detectionLabel->setText(tr("No object detected. Lower the threshold until the target is outlined."));
    }
    updateImage();
    updateExportIntensityDisplay();
    updateWorkflowSummaries();
}

void MainWindow::startHotPixelAnalysis()
{
    if (!m_source || !m_source->isOpen()) return;
    if (m_hotPixelWatcher->isRunning()) {
        m_hotPixelDirty = true;
        return;
    }
    m_hotPixelDirty = false;
    seeingforge::HotPixelSettings settings;
    settings.sampleCount = m_hotPixelSamples->currentData().toInt();
    settings.minimumBrightness = m_hotPixelBrightness->value();
    settings.localContrast = m_hotPixelContrast->value();
    settings.persistencePercent = m_hotPixelPersistence->value();
    const auto source = m_source;
    m_analyzeHotPixels->setEnabled(false);
    m_hotPixelLabel->setText(tr("Analyzing sampled frames…"));
    m_hotPixelWatcher->setFuture(QtConcurrent::run([source, settings] {
        return seeingforge::HotPixelDetector::detect(*source, settings);
    }));
}

void MainWindow::requestHotPixelAnalysis()
{
    if (!m_source || !m_source->isOpen() || !m_hotPixelDebounce) return;
    m_hotPixelDirty = true;
    m_hotPixelLabel->setText(tr("Settings changed — updated analysis scheduled…"));
    m_hotPixelDebounce->start();
}

void MainWindow::hotPixelAnalysisFinished()
{
    m_hotPixels = m_hotPixelWatcher->result();
    m_analyzeHotPixels->setEnabled(true);
    m_calHotCountTileValue->setText(QString::number(m_hotPixels.hotPixels.size()));
    m_calSampledTileValue->setText(QString::number(m_hotPixels.sampledFrames));
    m_overlayHotPixelsBtn->setText(
        tr("Hot Pixels (%1)").arg(m_hotPixels.hotPixels.size()));
    m_hotPixelLabel->setText(
        tr("Detected %1 persistent hot pixels from %2 initial candidates across %3 frames.")
            .arg(m_hotPixels.hotPixels.size())
            .arg(m_hotPixels.initialCandidates)
            .arg(m_hotPixels.sampledFrames));
    updateWorkflowSummaries();
    updateImage();
    if (m_hotPixelDirty) startHotPixelAnalysis();
}

void MainWindow::startQualityAnalysis()
{
    if (!m_source || !m_source->isOpen() || m_qualityWatcher->isRunning()) return;
    const auto source = m_source;
    const int backgroundThreshold = m_detectionThreshold
        ? m_detectionThreshold->value() : 12;
    m_analyzeQuality->setEnabled(false);
    m_cancelQuality->setEnabled(true);
    m_qualityProgress->setRange(0, static_cast<int>(m_source->header().frameCount));
    m_qualityProgress->setValue(0);
    m_qualityProgress->show();
    m_qualityKeep->setEnabled(false);
    m_qualityOrder->setEnabled(false);
    m_qualityLabel->setText(
        tr("Analyzing %1 frames across CPU threads…")
            .arg(m_source->header().frameCount));
    showStatus(tr("Analyzing frame quality…"));
    m_qualityCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelFlag = m_qualityCancel;
    m_qualityWatcher->setFuture(QtConcurrent::run(
        [source, backgroundThreshold, cancelFlag](QPromise<seeingforge::QualityResult> &promise) {
            promise.setProgressRange(0, static_cast<int>(source->header().frameCount));
            auto result = seeingforge::QualityAnalyzer::analyze(
                *source, backgroundThreshold,
                [&promise](int completed, int) { promise.setProgressValue(completed); },
                [cancelFlag] { return cancelFlag->load(); });
            promise.addResult(std::move(result));
        }));
}

void MainWindow::qualityAnalysisFinished()
{
    const seeingforge::QualityResult result = m_qualityWatcher->result();
    m_analyzeQuality->setEnabled(true);
    m_cancelQuality->setEnabled(false);
    m_qualityProgress->hide();
    m_qualityKeep->setEnabled(true);
    m_qualityOrder->setEnabled(true);
    if (result.canceled) {
        m_qualityLabel->setText(tr("Quality analysis canceled. Previous results were preserved."));
        showStatus(tr("Quality analysis canceled."), 5000);
        return;
    }
    m_qualityResult = result;
    m_qualStatusBadge->setText(tr("ANALYZED"));
    m_qualStatusBadge->setStyleSheet(QStringLiteral(
        "background: rgba(82,210,115,32); border: 1px solid rgba(82,210,115,90); color: #52d273;"
        "border-radius: 9px; padding: 2px 8px; font-size: 10px; font-weight: 700;"));
    showStatus(tr("Quality analysis complete."), 5000);
    updateQualityDisplay();
}

void MainWindow::cancelQualityAnalysis()
{
    if (!m_qualityWatcher->isRunning() || !m_qualityCancel) return;
    m_qualityCancel->store(true);
    m_cancelQuality->setEnabled(false);
    m_qualityLabel->setText(tr("Canceling after active frames…"));
}

void MainWindow::updateQualityDisplay()
{
    if (m_qualityResult.isEmpty() || !m_qualityTable) return;
    const int total = static_cast<int>(m_qualityResult.frames.size());
    QVector<double> clippedByIndex(
        static_cast<qsizetype>(m_source->header().frameCount), 0.0);
    for (const auto &frame : m_qualityResult.frames)
        clippedByIndex[static_cast<qsizetype>(frame.frameIndex)] = frame.clippedPercent;

    QVector<std::uint32_t> eligibleFrames;
    int clippedCount = 0;
    for (const std::uint32_t frameIndex : m_qualityResult.rankedFrameIndices) {
        const bool isClipped = m_rejectClipped->isChecked()
            && clippedByIndex[static_cast<qsizetype>(frameIndex)]
                   > m_maxClippedPercent->value();
        if (isClipped) ++clippedCount;
        else eligibleFrames.append(frameIndex);
    }

    const int keepPercent = m_qualityKeep->currentData().toInt();
    const int keepCount = eligibleFrames.isEmpty() ? 0 : std::clamp(
        static_cast<int>(std::ceil(eligibleFrames.size() * keepPercent / 100.0)),
        1, static_cast<int>(eligibleFrames.size()));

    QSet<std::uint32_t> accepted;
    for (int rank = 0; rank < keepCount; ++rank)
        accepted.insert(eligibleFrames[rank]);

    QVector<double> scoreByIndex(
        static_cast<qsizetype>(m_source->header().frameCount), 0.0);
    for (const auto &frame : m_qualityResult.frames)
        scoreByIndex[static_cast<qsizetype>(frame.frameIndex)] = frame.score;

    const bool orderedByQuality = m_qualityOrder->currentData().toInt() == 1;
    QVector<std::uint32_t> displayOrder;
    if (orderedByQuality) {
        displayOrder = m_qualityResult.rankedFrameIndices;
    } else {
        displayOrder.reserve(m_qualityResult.frames.size());
        for (const auto &frame : m_qualityResult.frames)
            displayOrder.append(frame.frameIndex);
    }

    const int filterMode = m_qualityFilterGroup ? m_qualityFilterGroup->checkedId() : 0;
    QVector<std::uint32_t> tableFrames;
    tableFrames.reserve(displayOrder.size());
    for (const std::uint32_t frameIndex : displayOrder) {
        const bool isClipped = m_rejectClipped->isChecked()
            && clippedByIndex[static_cast<qsizetype>(frameIndex)]
                   > m_maxClippedPercent->value();
        if (filterMode == 1 && !accepted.contains(frameIndex)) continue;
        if (filterMode == 2 && !isClipped) continue;
        tableFrames.append(frameIndex);
    }

    m_qualityTable->setSortingEnabled(false);
    m_qualityTable->setRowCount(tableFrames.size());
    for (qsizetype row = 0; row < tableFrames.size(); ++row) {
        const std::uint32_t frameIndex = tableFrames[row];
        auto *frameItem = new QTableWidgetItem(QStringLiteral("#%1").arg(frameIndex + 1U));
        frameItem->setData(Qt::UserRole, static_cast<qulonglong>(frameIndex));
        auto *scoreItem = new QTableWidgetItem(
            QString::number(scoreByIndex[static_cast<qsizetype>(frameIndex)], 'f', 1));
        const bool clippedRejected = m_rejectClipped->isChecked()
            && clippedByIndex[static_cast<qsizetype>(frameIndex)]
                   > m_maxClippedPercent->value();
        auto *keepItem = new QTableWidgetItem(clippedRejected ? tr("Clipped")
            : (accepted.contains(frameIndex) ? tr("Keep") : tr("Skip")));

        if (clippedRejected) {
            const QBrush red(QColor(QStringLiteral("#ff4d5a")));
            frameItem->setForeground(red);
            scoreItem->setForeground(red);
            keepItem->setForeground(red);
        } else if (accepted.contains(frameIndex)) {
            keepItem->setForeground(QBrush(QColor(QStringLiteral("#52d273"))));
        } else {
            const QBrush muted(QColor(QStringLiteral("#7d838c")));
            frameItem->setForeground(muted);
            scoreItem->setForeground(muted);
            keepItem->setForeground(muted);
        }
        m_qualityTable->setItem(static_cast<int>(row), 0, frameItem);
        m_qualityTable->setItem(static_cast<int>(row), 1, scoreItem);
        m_qualityTable->setItem(static_cast<int>(row), 2, keepItem);
    }

    QVector<double> displayedScores;
    displayedScores.reserve(displayOrder.size());
    for (const std::uint32_t frameIndex : displayOrder)
        displayedScores.append(scoreByIndex[static_cast<qsizetype>(frameIndex)]);

    const double cutoffScore = keepCount > 0
        ? scoreByIndex[static_cast<qsizetype>(eligibleFrames[keepCount - 1])]
        : 0.0;
    m_qualityGraph->setScores(std::move(displayedScores), keepCount, keepPercent,
                              orderedByQuality, cutoffScore, displayOrder);

    m_clippedRejectedCountLabel->setText(
        tr("%1 frames exceed %2% clipped limit").arg(clippedCount).arg(m_maxClippedPercent->value()));

    if (keepCount == 0) {
        m_qualKeptTileValue->setText(QStringLiteral("0 / %1").arg(total));
        m_qualCutoffTileValue->setText(QStringLiteral("—"));
        m_qualityLabel->setText(tr("All %1 frames exceed the overexposure limit.").arg(total));
        m_exportSummary->setText(tr("No frames currently qualify for export."));
    } else {
        const std::uint32_t bestIndex = m_qualityResult.rankedFrameIndices.front();
        m_qualKeptTileValue->setText(QStringLiteral("%1 / %2").arg(keepCount).arg(total));
        m_qualCutoffTileValue->setText(QString::number(cutoffScore, 'f', 1));
        m_qualityLabel->setText(
            tr("Keep %1 of %2 eligible frames (%3 discarded). Best frame: #%4 (%5).")
                .arg(keepCount).arg(eligibleFrames.size()).arg(total - keepCount)
                .arg(bestIndex + 1U)
                .arg(scoreByIndex[static_cast<qsizetype>(bestIndex)], 0, 'f', 1));
    }

    updateFrameQualityReadout();
    updateWorkflowSummaries();
}

void MainWindow::startExport()
{
    if (!m_source || !m_source->isOpen() || m_exportWatcher->isRunning()) return;

    QVector<std::uint32_t> frameIndices;
    if (!m_qualityResult.isEmpty()) {
        QVector<double> clippedByIndex(
            static_cast<qsizetype>(m_source->header().frameCount), 0.0);
        for (const auto &frame : m_qualityResult.frames)
            clippedByIndex[static_cast<qsizetype>(frame.frameIndex)] = frame.clippedPercent;
        QVector<std::uint32_t> eligibleFrames;
        for (const std::uint32_t frameIndex : m_qualityResult.rankedFrameIndices) {
            if (!m_rejectClipped->isChecked()
                || clippedByIndex[static_cast<qsizetype>(frameIndex)]
                       <= m_maxClippedPercent->value())
                eligibleFrames.append(frameIndex);
        }
        if (eligibleFrames.isEmpty()) {
            QMessageBox::warning(this, tr("No Frames to Export"),
                                 tr("All frames exceed the overexposure limit."));
            return;
        }
        const int keepCount = std::clamp(static_cast<int>(std::ceil(
            eligibleFrames.size() * m_qualityKeep->currentData().toInt() / 100.0)),
            1, static_cast<int>(eligibleFrames.size()));
        frameIndices.reserve(keepCount);
        for (int rank = 0; rank < keepCount; ++rank)
            frameIndices.append(eligibleFrames[rank]);
        if (m_qualityOrder->currentData().toInt() == 0)
            std::sort(frameIndices.begin(), frameIndices.end());
    } else {
        frameIndices.reserve(static_cast<qsizetype>(m_source->header().frameCount));
        for (std::uint32_t index = 0; index < m_source->header().frameCount; ++index)
            frameIndices.append(index);
    }

    const std::uint32_t refIdx = referenceFrameIndex();
    const QImage refFrame = (refIdx == m_currentFrame && !m_rawFrame.isNull())
        ? m_rawFrame : readCachedFrame(refIdx);
    seeingforge::DetectionResult referenceDetection;
    if (refIdx == m_currentFrame && m_detection.found()) {
        referenceDetection = m_detection;
    } else if (!refFrame.isNull()) {
        referenceDetection = seeingforge::ObjectDetector::detect(
            refFrame, m_detectionThreshold->value(), m_cropMargin->currentData().toInt());
    }

    const int margin = m_cropMargin->currentData().toInt();
    QSize requestedSize = m_outputSizeMode->currentData().toSize();
    if (requestedSize == QSize(-2, -2))
        requestedSize = QSize(m_customOutputWidth->value(), m_customOutputHeight->value());
    const QSize baseSize = !refFrame.isNull()
        ? refFrame.size()
        : QSize(static_cast<int>(m_source->header().width),
                static_cast<int>(m_source->header().height));
    const QSize outputSize = exportCropSize(baseSize, referenceDetection.objectRect,
                                            margin, requestedSize);

    const QFileInfo sourceInfo(m_source->path());
    const QString baseFolder = QStringLiteral("seeingforge_%1").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm")));
    QDir parent(sourceInfo.absolutePath());
    QString folderName = baseFolder;
    int suffix = 2;
    while (parent.exists(folderName))
        folderName = QStringLiteral("%1_%2").arg(baseFolder).arg(suffix++);
    if (!parent.mkpath(folderName)) {
        QMessageBox::critical(this, tr("Cannot Export"),
                              tr("Could not create the export folder beside the source."));
        return;
    }
    const bool serInput = dynamic_cast<seeingforge::SerFile *>(m_source.get()) != nullptr;
    const QString outputName = serInput ? sourceInfo.fileName() : QStringLiteral("raw_sequence.ser");
    const QString outputPath = parent.filePath(folderName + QLatin1Char('/') + outputName);

    seeingforge::SerExportSettings settings;
    settings.frameIndices = frameIndices;
    settings.centerObject = true;
    settings.monochrome = m_exportMonochrome->isChecked();
    settings.binMonochrome = settings.monochrome && m_exportMonoBinning->isChecked();
    const int balanceMode = m_rawExportWhiteBalance->isEnabled()
        ? m_rawExportWhiteBalance->currentData().toInt() : 0;
    if (balanceMode == 1 && m_source->hasCameraWhiteBalance())
        settings.bayerChannelGains = m_source->cameraWhiteBalance();
    else if (balanceMode == 2 && !refFrame.isNull())
        settings.bayerChannelGains = seeingforge::BayerPatternAnalyzer::neutralBalanceGains(
            refFrame, effectiveColorId(), referenceDetection.objectRect);
    settings.intensityGain = selectedExportIntensityGain();
    settings.sourceColorId = effectiveColorId();
    settings.detectionThreshold = m_detectionThreshold->value();
    settings.outputSize = outputSize;
    settings.rotationQuarterTurns = currentOrientationTurns();
    settings.flipHorizontal = m_flipHorizontal->isChecked();
    settings.flipVertical = m_flipVertical->isChecked();

    if (!m_qualityResult.isEmpty()
        && m_qualityResult.detectionThreshold == settings.detectionThreshold) {
        for (const auto &fq : m_qualityResult.frames) {
            if (fq.objectRect.isValid()) {
                settings.cachedObjectRects.insert(fq.frameIndex, fq.objectRect);
            }
        }
    }

    const auto source = m_source;
    m_exportDoneBanner->hide();
    m_exportButton->setEnabled(false);
    m_cancelExport->setEnabled(true);
    m_exportProgress->setRange(0, static_cast<int>(frameIndices.size()));
    m_exportProgress->setValue(0);
    m_exportProgress->show();
    m_exportResultLabel->setText(tr("Exporting and centering frames…"));
    showStatus(tr("Exporting SER recording…"));

    m_exportCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelFlag = m_exportCancel;
    m_exportWatcher->setFuture(QtConcurrent::run(
        [source, outputPath, settings, cancelFlag](QPromise<seeingforge::ExportResult> &promise) {
            promise.setProgressRange(0, static_cast<int>(settings.frameIndices.size()));
            auto result = seeingforge::SerExporter::exportFrames(
                *source, outputPath, settings,
                [&promise](int completed, int) { promise.setProgressValue(completed); },
                [cancelFlag] { return cancelFlag->load(); });
            promise.addResult(std::move(result));
        }));
}

void MainWindow::exportFinished()
{
    const seeingforge::ExportResult result = m_exportWatcher->result();
    m_exportButton->setEnabled(true);
    m_cancelExport->setEnabled(false);
    m_exportProgress->hide();
    if (result.canceled) {
        m_exportResultLabel->setText(tr("Export canceled. No partial SER file was saved."));
        showStatus(tr("Export canceled."), 5000);
        return;
    }
    if (!result.success) {
        m_exportResultLabel->setText(tr("Export failed: %1").arg(result.error));
        showStatus(tr("Export failed."), 5000);
        return;
    }
    m_lastExportPath = result.outputPath;
    m_exportResultLabel->setText(
        tr("Exported %1 centered frames to:\n%2%3")
            .arg(result.exportedFrames).arg(result.outputPath)
            .arg(result.timestampsPreserved ? tr("\nFrame timestamps preserved.") : QString()));
    m_exportDoneText->setText(
        tr("✓ Exported %1 centered frames%2")
            .arg(result.exportedFrames)
            .arg(result.timestampsPreserved ? tr(" (timestamps preserved)") : QString()));
    m_exportDoneBanner->show();
    showStatus(tr("SER export complete."), 5000);
}

void MainWindow::cancelExport()
{
    if (!m_exportWatcher->isRunning() || !m_exportCancel) return;
    m_exportCancel->store(true);
    m_cancelExport->setEnabled(false);
    m_exportResultLabel->setText(tr("Canceling after active batch…"));
}

void MainWindow::previewModeChanged()
{
    if (m_previewMode->checkedId() == 2) {
        m_fitToWindow = false;
        m_zoomFactor = 1.0;
        m_fitAction->setChecked(false);
        m_zoomFitBtn->setChecked(false);
        m_zoomActualBtn->setChecked(true);
        if (m_zoomLabel) m_zoomLabel->setText(QStringLiteral("100%"));
        if (m_cfaHintToast) m_cfaHintToast->show();
        showStatus(tr("Raw CFA mosaic is shown at 1:1; its pixel pattern is expected."), 5000);
    } else {
        if (m_cfaHintToast) m_cfaHintToast->hide();
    }
    updateImage();
}

void MainWindow::loadSelectedFrame()
{
    loadFrame(static_cast<std::uint32_t>(m_frameSlider->value()));
}

void MainWindow::selectWorkflowStep(int step)
{
    if (step < 0 || step >= m_inspectorStack->count()) return;
    m_activeStep = step;
    if (m_workflowButtons[static_cast<std::size_t>(step)]) {
        m_workflowButtons[static_cast<std::size_t>(step)]->setChecked(true);
    }
    m_inspectorStack->setCurrentIndex(step);
    if (step == 1 && !m_hotPixels.hotPixels.isEmpty()) {
        m_overlayHotPixelsBtn->setChecked(true);
    }
    if (step == 2) updateDetection();
    else updateImage();
    if (step == 4) {
        updateExportIntensityDisplay();
        updateWorkflowSummaries();
    }
    updateWorkflowSummaries();
    updateFooterNavigation();
}

void MainWindow::updateFooterNavigation()
{
    static constexpr std::array<const char *, 5> stepNames{
        "Source & Sensor", "Calibration", "Stabilize & Crop", "Quality Ranking", "Export SER"
    };
    if (m_prevStepButton) {
        m_prevStepButton->setEnabled(m_activeStep > 0);
        m_prevStepButton->setText(m_activeStep > 0
            ? tr("← %1").arg(tr(stepNames[static_cast<std::size_t>(m_activeStep - 1)]))
            : tr("← Start"));
    }
    if (m_nextStepButton) {
        m_nextStepButton->setText(m_activeStep < 4
            ? tr("Continue to %1 →").arg(tr(stepNames[static_cast<std::size_t>(m_activeStep + 1)]))
            : tr("Export SER Recording →"));
        repolish(m_nextStepButton);
    }
}

void MainWindow::updateHeroSummary()
{
    if (!m_source || !m_source->isOpen()) {
        m_heroTitle->setText(tr("SeeingForge"));
        m_heroFormatBadge->setText(tr("No source"));
        m_heroDimBadge->setText(QStringLiteral("— × —"));
        m_heroSubtitle->setText(
            tr("Non-destructive planetary & lunar preprocessing — original recordings are never modified"));
        m_pipelineStateLabel->setText(tr("Drop or open SER / RAW"));
        return;
    }
    const auto &header = m_source->header();
    seeingforge::SerHeader effectiveHeader = header;
    effectiveHeader.colorId = effectiveColorId();

    m_heroTitle->setText(m_source->displayName());
    m_heroFormatBadge->setText(
        tr("%1-bit · %2").arg(header.pixelDepth).arg(effectiveHeader.colorName()));
    m_heroDimBadge->setText(QStringLiteral("%1 × %2 px").arg(header.width).arg(header.height));
    m_heroSubtitle->setText(
        header.instrument.isEmpty()
            ? tr("%1 frames · Non-destructive workflow — original recording is never modified")
                  .arg(header.frameCount)
            : tr("%1 · %2 frames · Non-destructive workflow")
                  .arg(header.instrument).arg(header.frameCount));
    m_pipelineStateLabel->setText(
        m_qualityResult.isEmpty() ? tr("Source loaded") : tr("Quality ranked"));

    m_sourceStatusBadge->setText(tr("LOADED"));
    m_sourceStatusBadge->setStyleSheet(QStringLiteral(
        "background: rgba(82,210,115,32); border: 1px solid rgba(82,210,115,90); color: #52d273;"
        "border-radius: 9px; padding: 2px 8px; font-size: 10px; font-weight: 700;"));
    m_srcFramesTileValue->setText(QString::number(header.frameCount));
    m_srcFramesTileSub->setText(
        dynamic_cast<seeingforge::RawSequence *>(m_source.get()) != nullptr
            ? tr("DSLR RAW sequence") : tr("SER recording"));
    m_srcResTileValue->setText(QStringLiteral("%1×%2").arg(header.width).arg(header.height));
    m_srcResTileSub->setText(tr("%1 bytes / frame").arg(header.bytesPerFrame()));
    m_srcFormatTileValue->setText(tr("%1-bit").arg(header.pixelDepth));
    m_srcFormatTileSub->setText(effectiveHeader.colorName());
    m_srcWbTileValue->setText(m_source->hasCameraWhiteBalance() ? tr("Available") : tr("None"));
    m_srcWbTileSub->setText(m_source->hasCameraWhiteBalance()
                                ? tr("EXIF multipliers read")
                                : tr("Neutral fallback"));
}

void MainWindow::updateWorkflowSummaries()
{
    const bool open = m_source && m_source->isOpen();
    for (int i = 0; i < 5; ++i) {
        auto *badge = m_navStepBadges[static_cast<std::size_t>(i)];
        const bool isActive = (i == m_activeStep);
        const bool isDone = open && ((i == 0) || (i == 3 && !m_qualityResult.isEmpty()));
        if (isActive) {
            badge->setText(QString::number(i + 1));
            badge->setStyleSheet(QStringLiteral(
                "background: rgba(138,82,150,75); border: 1px solid #9e65aa; border-radius: 7px;"
                "color: #f0d0f7; font-size: 11px; font-weight: 700;"));
        } else if (isDone) {
            badge->setText(QStringLiteral("✓"));
            badge->setStyleSheet(QStringLiteral(
                "background: rgba(82,210,115,32); border: 1px solid rgba(82,210,115,90); border-radius: 7px;"
                "color: #52d273; font-size: 11px; font-weight: 700;"));
        } else {
            badge->setText(QString::number(i + 1));
            badge->setStyleSheet(QStringLiteral(
                "background: #242932; border: 1px solid #3a414e; border-radius: 7px;"
                "color: #aab2bd; font-size: 11px; font-weight: 700;"));
        }
    }

    if (!open) return;
    seeingforge::SerHeader effectiveHeader = m_source->header();
    effectiveHeader.colorId = effectiveColorId();
    m_navSubLabels[0]->setText(
        tr("%1 frames · %2").arg(effectiveHeader.frameCount).arg(effectiveHeader.colorName()));

    const int balMode = m_rawExportWhiteBalance->isEnabled()
        ? m_rawExportWhiteBalance->currentData().toInt() : 0;
    const QString balText = balMode == 2 ? tr("Auto Neutral")
        : (balMode == 1 ? tr("Camera WB") : tr("Linear"));
    m_navSubLabels[1]->setText(m_hotPixels.sampledFrames > 0
        ? tr("%1 · %2 hot px").arg(balText).arg(m_hotPixels.hotPixels.size())
        : tr("%1 · Not scanned").arg(balText));

    const int margin = m_cropMargin->currentData().toInt();
    m_navSubLabels[2]->setText(m_detection.found()
        ? tr("%1×%2 · %3")
              .arg(m_detection.cropRect.width())
              .arg(m_detection.cropRect.height())
              .arg(margin < 0 ? tr("Full") : tr("%1% margin").arg(margin))
        : tr("Threshold %1").arg(m_detectionThreshold->value()));

    int keptCount = static_cast<int>(effectiveHeader.frameCount);
    if (!m_qualityResult.isEmpty()) {
        QVector<double> clippedByIndex(
            static_cast<qsizetype>(effectiveHeader.frameCount), 0.0);
        for (const auto &frame : m_qualityResult.frames)
            clippedByIndex[static_cast<qsizetype>(frame.frameIndex)] = frame.clippedPercent;
        int eligibleCount = 0;
        for (const std::uint32_t frameIndex : m_qualityResult.rankedFrameIndices) {
            if (!m_rejectClipped->isChecked()
                || clippedByIndex[static_cast<qsizetype>(frameIndex)]
                       <= m_maxClippedPercent->value()) {
                ++eligibleCount;
            }
        }
        const int keepPct = m_qualityKeep->currentData().toInt();
        keptCount = eligibleCount == 0 ? 0 : std::clamp(
            static_cast<int>(std::ceil(eligibleCount * keepPct / 100.0)),
            1, eligibleCount);
        m_navSubLabels[3]->setText(
            tr("Best %1% · %2 / %3 kept").arg(keepPct).arg(keptCount).arg(effectiveHeader.frameCount));
        m_expFramesTileValue->setText(QString::number(keptCount));
        m_expFramesTileSub->setText(tr("Best %1% selected").arg(keepPct));
    } else {
        m_navSubLabels[3]->setText(tr("All %1 frames (unranked)").arg(effectiveHeader.frameCount));
        m_expFramesTileValue->setText(QString::number(effectiveHeader.frameCount));
        m_expFramesTileSub->setText(tr("All frames (unranked)"));
    }

    const bool mono = m_exportMonochrome->isChecked();
    const double intensityVal = m_exportIntensity->currentData().toDouble();
    const QString intLabel = qFuzzyIsNull(intensityVal)
        ? tr("Auto 90%") : tr("%1×").arg(intensityVal, 0, 'f', 0);
    m_navSubLabels[4]->setText(
        tr("%1 · %2").arg(mono ? (m_exportMonoBinning->isChecked() ? tr("Mono 2×2") : tr("Mono"))
                               : tr("%1-bit").arg(effectiveHeader.pixelDepth),
                          intLabel));

    const std::uint32_t refIdx = referenceFrameIndex();
    const QString orderStr = (!m_qualityResult.isEmpty() && m_qualityOrder->currentData().toInt() == 1)
        ? tr("quality-score order") : tr("capture order");
    const QString refStr = !m_qualityResult.isEmpty()
        ? tr("Best ranked frame #%1").arg(refIdx + 1U)
        : tr("Current frame #%1").arg(refIdx + 1U);
    const QString dimStr = m_detection.found()
        ? QStringLiteral("%1 × %2 px").arg(m_detection.cropRect.width()).arg(m_detection.cropRect.height())
        : QStringLiteral("%1 × %2 px").arg(effectiveHeader.width).arg(effectiveHeader.height);

    m_exportSummary->setText(
        tr("<b>Frames:</b> %1 of %2 (%3)<br>"
           "<b>Canvas:</b> %4 &nbsp;·&nbsp; <b>Balance:</b> %5<br>"
           "<b>Reference:</b> %6<br>"
           "Saved beside source in a new <code>seeingforge_yyyymmdd_hhmm</code> folder.")
            .arg(keptCount).arg(effectiveHeader.frameCount).arg(orderStr, dimStr, balText, refStr));
}

void MainWindow::updateFrameQualityReadout()
{
    if (!m_frameQualityLabel) return;
    if (!m_source || !m_source->isOpen()) {
        m_frameQualityLabel->setText(tr("Open a recording to navigate frames"));
        m_frameQualityLabel->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px; font-family: monospace;"));
        return;
    }
    if (m_qualityResult.isEmpty()) {
        m_frameQualityLabel->setText(
            tr("Unranked — run Quality Ranking to score & filter frames"));
        m_frameQualityLabel->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px; font-family: monospace;"));
        return;
    }
    double score = 0.0;
    double clipped = 0.0;
    for (const auto &f : m_qualityResult.frames) {
        if (f.frameIndex == m_currentFrame) {
            score = f.score;
            clipped = f.clippedPercent;
            break;
        }
    }
    const bool clippedRej = m_rejectClipped->isChecked()
        && clipped > m_maxClippedPercent->value();
    int eligibleCount = 0;
    int eligibleRank = -1;
    QVector<double> clippedByIndex(
        static_cast<qsizetype>(m_source->header().frameCount), 0.0);
    for (const auto &f : m_qualityResult.frames)
        clippedByIndex[static_cast<qsizetype>(f.frameIndex)] = f.clippedPercent;
    for (const std::uint32_t idx : m_qualityResult.rankedFrameIndices) {
        if (!m_rejectClipped->isChecked()
            || clippedByIndex[static_cast<qsizetype>(idx)] <= m_maxClippedPercent->value()) {
            if (idx == m_currentFrame) eligibleRank = eligibleCount + 1;
            ++eligibleCount;
        }
    }
    const int keepCount = eligibleCount == 0 ? 0 : std::clamp(
        static_cast<int>(std::ceil(eligibleCount * m_qualityKeep->currentData().toInt() / 100.0)),
        1, eligibleCount);
    if (clippedRej) {
        m_frameQualityLabel->setText(
            tr("Quality: %1 · Clipped (%2% > %3% limit) — Rejected")
                .arg(score, 0, 'f', 1).arg(clipped, 0, 'f', 1).arg(m_maxClippedPercent->value()));
        m_frameQualityLabel->setStyleSheet(QStringLiteral("color: #ff4d5a; font-size: 11px; font-family: monospace;"));
    } else if (eligibleRank > 0 && eligibleRank <= keepCount) {
        m_frameQualityLabel->setText(
            tr("Quality: %1 · Kept (Rank #%2 of %3)")
                .arg(score, 0, 'f', 1).arg(eligibleRank).arg(eligibleCount));
        m_frameQualityLabel->setStyleSheet(QStringLiteral("color: #52d273; font-size: 11px; font-family: monospace;"));
    } else {
        m_frameQualityLabel->setText(
            tr("Quality: %1 · Discarded (Rank #%2 of %3)")
                .arg(score, 0, 'f', 1).arg(eligibleRank).arg(eligibleCount));
        m_frameQualityLabel->setStyleSheet(QStringLiteral("color: #8d96a5; font-size: 11px; font-family: monospace;"));
    }
}

void MainWindow::loadUserPreferences()
{
    QSettings settings;
    const auto setComboData = [](QComboBox *combo, const QVariant &value) {
        const int index = combo->findData(value);
        if (index >= 0) combo->setCurrentIndex(index);
    };

    const QSignalBlocker b1(m_debayerMethod);
    const QSignalBlocker b2(m_whiteBalance);
    const QSignalBlocker b3(m_redBalance);
    const QSignalBlocker b4(m_blueBalance);
    const QSignalBlocker b5(m_saturation);
    const QSignalBlocker b6(m_hue);
    const QSignalBlocker b7(m_gain);
    const QSignalBlocker b8(m_gamma);
    const QSignalBlocker b9(m_autoStretch);
    const QSignalBlocker b10(m_cropMargin);
    const QSignalBlocker b11(m_qualityKeep);
    const QSignalBlocker b12(m_rejectClipped);
    const QSignalBlocker b13(m_maxClippedPercent);
    const QSignalBlocker b14(m_qualityOrder);
    const QSignalBlocker b15(m_exportIntensity);

    setComboData(m_debayerMethod,
                 settings.value(QStringLiteral("preferences/colour/debayer"), true));
    m_whiteBalance->setChecked(
        settings.value(QStringLiteral("preferences/colour/whiteBalance"), true).toBool());
    m_redBalance->setValue(
        settings.value(QStringLiteral("preferences/colour/redBalance"), 100).toInt());
    m_blueBalance->setValue(
        settings.value(QStringLiteral("preferences/colour/blueBalance"), 100).toInt());
    m_saturation->setValue(
        settings.value(QStringLiteral("preferences/colour/saturation"), 100).toInt());
    m_hue->setValue(settings.value(QStringLiteral("preferences/colour/hue"), 0).toInt());
    m_gain->setValue(settings.value(QStringLiteral("preferences/colour/gain"), 100).toInt());
    m_gamma->setValue(settings.value(QStringLiteral("preferences/colour/gamma"), 100).toInt());
    m_autoStretch->setChecked(
        settings.value(QStringLiteral("preferences/colour/autoStretch"), true).toBool());

    setComboData(m_cropMargin,
                 settings.value(QStringLiteral("preferences/stabilize/cropMargin"), 20));
    setComboData(m_qualityKeep,
                 settings.value(QStringLiteral("preferences/quality/keepPercent"), 25));
    m_rejectClipped->setChecked(
        settings.value(QStringLiteral("preferences/quality/rejectClipped"), true).toBool());
    m_maxClippedPercent->setValue(
        settings.value(QStringLiteral("preferences/quality/maxClippedPercent"), 5).toInt());
    setComboData(m_qualityOrder,
                 settings.value(QStringLiteral("preferences/quality/frameOrder"), 0));
    setComboData(m_exportIntensity,
                 settings.value(QStringLiteral("preferences/export/intensity"), 1.0));
}

void MainWindow::populatePresetCombo()
{
    if (!m_presetCombo) return;
    const QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();
    m_presetCombo->addItem(tr("Saved Defaults"), QStringLiteral("builtin:defaults"));
    m_presetCombo->addItem(tr("Full-Disc Lunar (CR2)"), QStringLiteral("builtin:lunar"));
    m_presetCombo->addItem(tr("Planetary ROI (SER)"), QStringLiteral("builtin:planetary"));
    m_presetCombo->addItem(tr("Full-Disc Solar Mono 2×2"), QStringLiteral("builtin:solar"));

    QSettings settings;
    settings.beginGroup(QStringLiteral("presets"));
    const QStringList customNames = settings.childGroups();
    settings.endGroup();
    for (const QString &name : customNames) {
        m_presetCombo->addItem(name, QStringLiteral("user:%1").arg(name));
    }
}

void MainWindow::applySelectedPreset()
{
    if (!m_presetCombo) return;
    const QString id = m_presetCombo->currentData().toString();
    const auto setComboData = [](QComboBox *combo, const QVariant &value) {
        const int idx = combo->findData(value);
        if (idx >= 0) combo->setCurrentIndex(idx);
    };

    if (id == QLatin1String("builtin:defaults")) {
        loadUserPreferences();
    } else if (id == QLatin1String("builtin:lunar")) {
        const QSignalBlocker b1(m_cropMargin), b2(m_qualityKeep), b3(m_exportIntensity),
            b4(m_exportMonochrome), b5(m_exportMonoBinning), b6(m_rawExportWhiteBalance);
        setComboData(m_rawExportWhiteBalance, 2);
        setComboData(m_cropMargin, 20);
        setComboData(m_qualityKeep, 25);
        setComboData(m_exportIntensity, 0.0);
        m_exportMonochrome->setChecked(false);
        m_exportMonoBinning->setChecked(false);
        m_exportMonoBinning->setEnabled(false);
    } else if (id == QLatin1String("builtin:planetary")) {
        const QSignalBlocker b1(m_cropMargin), b2(m_qualityKeep), b3(m_exportIntensity),
            b4(m_exportMonochrome), b5(m_exportMonoBinning), b6(m_rawExportWhiteBalance);
        setComboData(m_rawExportWhiteBalance, 0);
        setComboData(m_cropMargin, 50);
        setComboData(m_qualityKeep, 25);
        setComboData(m_exportIntensity, 1.0);
        m_exportMonochrome->setChecked(false);
        m_exportMonoBinning->setChecked(false);
        m_exportMonoBinning->setEnabled(false);
    } else if (id == QLatin1String("builtin:solar")) {
        const QSignalBlocker b1(m_cropMargin), b2(m_qualityKeep), b3(m_exportIntensity),
            b4(m_exportMonochrome), b5(m_exportMonoBinning), b6(m_rawExportWhiteBalance);
        setComboData(m_rawExportWhiteBalance, 0);
        setComboData(m_cropMargin, 20);
        setComboData(m_qualityKeep, 10);
        setComboData(m_exportIntensity, 0.0);
        m_exportMonochrome->setChecked(true);
        m_exportMonoBinning->setEnabled(true);
        m_exportMonoBinning->setChecked(true);
    } else if (id.startsWith(QLatin1String("user:"))) {
        const QString name = id.mid(5);
        QSettings settings;
        settings.beginGroup(QStringLiteral("presets/%1").arg(name));
        const QSignalBlocker b1(m_rawExportWhiteBalance), b2(m_cropMargin), b3(m_qualityKeep),
            b4(m_rejectClipped), b5(m_maxClippedPercent), b6(m_qualityOrder),
            b7(m_exportIntensity), b8(m_exportMonochrome), b9(m_exportMonoBinning),
            b10(m_detectionThreshold);
        setComboData(m_rawExportWhiteBalance, settings.value(QStringLiteral("sensorBalance"), 0));
        setComboData(m_cropMargin, settings.value(QStringLiteral("cropMargin"), 20));
        m_detectionThreshold->setValue(settings.value(QStringLiteral("threshold"), 16).toInt());
        setComboData(m_qualityKeep, settings.value(QStringLiteral("keepPercent"), 25));
        m_rejectClipped->setChecked(settings.value(QStringLiteral("rejectClipped"), true).toBool());
        m_maxClippedPercent->setValue(settings.value(QStringLiteral("maxClippedPercent"), 5).toInt());
        setComboData(m_qualityOrder, settings.value(QStringLiteral("frameOrder"), 0));
        setComboData(m_exportIntensity, settings.value(QStringLiteral("intensity"), 1.0));
        const bool mono = settings.value(QStringLiteral("monochrome"), false).toBool();
        m_exportMonochrome->setChecked(mono);
        m_exportMonoBinning->setEnabled(mono);
        m_exportMonoBinning->setChecked(mono && settings.value(QStringLiteral("binning"), false).toBool());
        settings.endGroup();
    }

    requestPreviewRender();
    updateDetection();
    updateQualityDisplay();
    updateExportIntensityDisplay();
    updateWorkflowSummaries();
    showStatus(tr("Applied preset: %1").arg(m_presetCombo->currentText()), 4000);
}

void MainWindow::saveNamedPreset()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Save Session Preset"),
        tr("Preset name (also updates default preferences):"),
        QLineEdit::Normal, QStringLiteral("My Astro Preset"), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    QSettings settings;
    settings.beginGroup(QStringLiteral("presets/%1").arg(name));
    settings.setValue(QStringLiteral("sensorBalance"), m_rawExportWhiteBalance->currentData());
    settings.setValue(QStringLiteral("cropMargin"), m_cropMargin->currentData());
    settings.setValue(QStringLiteral("threshold"), m_detectionThreshold->value());
    settings.setValue(QStringLiteral("keepPercent"), m_qualityKeep->currentData());
    settings.setValue(QStringLiteral("rejectClipped"), m_rejectClipped->isChecked());
    settings.setValue(QStringLiteral("maxClippedPercent"), m_maxClippedPercent->value());
    settings.setValue(QStringLiteral("frameOrder"), m_qualityOrder->currentData());
    settings.setValue(QStringLiteral("intensity"), m_exportIntensity->currentData());
    settings.setValue(QStringLiteral("monochrome"), m_exportMonochrome->isChecked());
    settings.setValue(QStringLiteral("binning"), m_exportMonoBinning->isChecked());
    settings.endGroup();
    settings.sync();

    savePreferredChoices();
    populatePresetCombo();
    const int idx = m_presetCombo->findData(QStringLiteral("user:%1").arg(name));
    if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
    showStatus(tr("Saved preset '%1' and updated preferred choices.").arg(name), 4000);
}

void MainWindow::savePreferredChoices()
{
    QSettings settings;
    settings.setValue(QStringLiteral("preferences/calibration/sensorBalance"),
                      m_rawExportWhiteBalance->currentData());
    settings.setValue(QStringLiteral("preferences/colour/debayer"),
                      m_debayerMethod->currentData());
    settings.setValue(QStringLiteral("preferences/colour/whiteBalance"),
                      m_whiteBalance->isChecked());
    settings.setValue(QStringLiteral("preferences/colour/redBalance"), m_redBalance->value());
    settings.setValue(QStringLiteral("preferences/colour/blueBalance"), m_blueBalance->value());
    settings.setValue(QStringLiteral("preferences/colour/saturation"), m_saturation->value());
    settings.setValue(QStringLiteral("preferences/colour/hue"), m_hue->value());
    settings.setValue(QStringLiteral("preferences/colour/gain"), m_gain->value());
    settings.setValue(QStringLiteral("preferences/colour/gamma"), m_gamma->value());
    settings.setValue(QStringLiteral("preferences/colour/autoStretch"),
                      m_autoStretch->isChecked());
    settings.setValue(QStringLiteral("preferences/stabilize/cropMargin"),
                      m_cropMargin->currentData());
    settings.setValue(QStringLiteral("preferences/quality/keepPercent"),
                      m_qualityKeep->currentData());
    settings.setValue(QStringLiteral("preferences/quality/rejectClipped"),
                      m_rejectClipped->isChecked());
    settings.setValue(QStringLiteral("preferences/quality/maxClippedPercent"),
                      m_maxClippedPercent->value());
    settings.setValue(QStringLiteral("preferences/quality/frameOrder"),
                      m_qualityOrder->currentData());
    settings.setValue(QStringLiteral("preferences/export/intensity"),
                      m_exportIntensity->currentData());
    settings.sync();
    showStatus(tr("Preferred workflow choices saved."), 4000);
}

void MainWindow::resetCurrentStep()
{
    QSettings settings;
    switch (m_activeStep) {
    case 0:
        m_bayerPattern->setCurrentIndex(0);
        requestPreviewRender();
        break;
    case 1: {
        const QSignalBlocker b1(m_hotPixelSamples), b2(m_hotPixelBrightness),
            b3(m_hotPixelContrast), b4(m_hotPixelPersistence);
        if (const int index = m_rawExportWhiteBalance->findData(0); index >= 0)
            m_rawExportWhiteBalance->setCurrentIndex(index);
        m_hotPixelSamples->setCurrentIndex(m_hotPixelSamples->findData(16));
        m_hotPixelBrightness->setValue(200);
        m_hotPixelContrast->setValue(30);
        m_hotPixelPersistence->setValue(80);
        m_overlayHotPixelsBtn->setChecked(true);
        settings.setValue(QStringLiteral("preferences/calibration/sensorBalance"), 0);
        updateWorkflowSummaries();
        break;
    }
    case 2:
        if (auto *btn = m_orientationGroup->button(0)) btn->setChecked(true);
        m_orientationChip->setText(QStringLiteral("0°"));
        m_flipHorizontal->setChecked(false);
        m_flipVertical->setChecked(false);
        m_detectionThreshold->setValue(16);
        if (const int index = m_cropMargin->findData(20); index >= 0)
            m_cropMargin->setCurrentIndex(index);
        m_outputSizeMode->setCurrentIndex(0);
        m_customOutputWidth->setValue(640);
        m_customOutputHeight->setValue(480);
        m_overlayDetectBtn->setChecked(true);
        settings.setValue(QStringLiteral("preferences/stabilize/cropMargin"), 20);
        requestPreviewRender();
        updateDetection();
        break;
    case 3:
        m_qualityKeep->setCurrentIndex(m_qualityKeep->findData(25));
        m_rejectClipped->setChecked(true);
        m_maxClippedPercent->setValue(5);
        m_qualityOrder->setCurrentIndex(m_qualityOrder->findData(0));
        if (auto *btn = m_qualityFilterGroup->button(0)) btn->setChecked(true);
        settings.remove(QStringLiteral("preferences/quality"));
        updateQualityDisplay();
        break;
    case 4:
        m_exportIntensity->setCurrentIndex(m_exportIntensity->findData(1.0));
        m_exportMonochrome->setChecked(false);
        m_exportMonoBinning->setChecked(false);
        settings.setValue(QStringLiteral("preferences/export/intensity"), 1.0);
        updateExportIntensityDisplay();
        updateWorkflowSummaries();
        break;
    default:
        showStatus(tr("This step has no saved user defaults."), 4000);
        return;
    }
    showStatus(tr("Current step reset to defaults."), 4000);
}

double MainWindow::selectedExportIntensityGain() const
{
    if (!m_exportIntensity) return 1.0;
    const double selected = m_exportIntensity->currentData().toDouble();
    if (selected > 0.0 || m_rawFrame.isNull()) return std::max(selected, 1.0);

    std::array<std::uint64_t, 256> histogram{};
    QRect region = m_detection.found() ? m_detection.objectRect : m_rawFrame.rect();
    region = region.intersected(m_rawFrame.rect());
    std::uint64_t count = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
        for (int x = region.left(); x <= region.right(); ++x) {
            const int value = m_rawFrame.format() == QImage::Format_Grayscale8
                ? m_rawFrame.constScanLine(y)[x] : qGray(m_rawFrame.pixel(x, y));
            ++histogram[static_cast<std::size_t>(value)];
            ++count;
        }
    }
    if (count == 0) return 1.0;
    const std::uint64_t target = count - std::max<std::uint64_t>(1, count / 1000U);
    std::uint64_t cumulative = 0;
    int high = 1;
    for (int value = 0; value < 256; ++value) {
        cumulative += histogram[static_cast<std::size_t>(value)];
        if (cumulative >= target) { high = std::max(value, 1); break; }
    }
    return std::clamp(230.0 / high, 1.0, 16.0);
}

void MainWindow::updateExportIntensityDisplay()
{
    if (!m_exportIntensityLabel || m_rawFrame.isNull()) return;
    QRect region = m_detection.found() ? m_detection.objectRect : m_rawFrame.rect();
    region = region.intersected(m_rawFrame.rect());
    int maximum = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
        for (int x = region.left(); x <= region.right(); ++x) {
            const int value = m_rawFrame.format() == QImage::Format_Grayscale8
                ? m_rawFrame.constScanLine(y)[x] : qGray(m_rawFrame.pixel(x, y));
            maximum = std::max(maximum, value);
        }
    }
    const double gain = selectedExportIntensityGain();
    const double measuredPercent = maximum * 100.0 / 255.0;
    const double predictedPercent = std::min(100.0, measuredPercent * gain);
    const bool clips = measuredPercent * gain > 100.0;
    m_expPeakTileValue->setText(
        QStringLiteral("%1% → %2%")
            .arg(measuredPercent, 0, 'f', 0)
            .arg(predictedPercent, 0, 'f', 0));
    m_expPeakTileSub->setText(
        clips ? tr("Gain %1× (clips)").arg(gain, 0, 'f', 2)
              : tr("Gain %1×").arg(gain, 0, 'f', 2));
    m_exportIntensityLabel->setText(
        tr("Current target peak: %1%. Export gain: %2×. Estimated output peak: %3%%4")
            .arg(measuredPercent, 0, 'f', 1)
            .arg(gain, 0, 'f', 2)
            .arg(predictedPercent, 0, 'f', 1)
            .arg(clips ? tr(" — brightest pixels will clip") : QString()));
}

void MainWindow::fitImage()
{
    m_fitToWindow = true;
    m_fitAction->setChecked(true);
    m_zoomFitBtn->setChecked(true);
    m_zoomActualBtn->setChecked(false);
    if (m_zoomLabel) m_zoomLabel->setText(tr("Fit"));
    updateImage();
}

void MainWindow::showActualSize()
{
    m_fitToWindow = false;
    m_zoomFactor = 1.0;
    m_fitAction->setChecked(false);
    m_zoomFitBtn->setChecked(false);
    m_zoomActualBtn->setChecked(true);
    if (m_zoomLabel) m_zoomLabel->setText(QStringLiteral("100%"));
    updateImage();
}

void MainWindow::zoomIn()
{
    m_fitToWindow = false;
    m_fitAction->setChecked(false);
    m_zoomFitBtn->setChecked(false);
    m_zoomActualBtn->setChecked(false);
    m_zoomFactor = std::min(m_zoomFactor * 1.25, 8.0);
    if (m_zoomLabel) m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_zoomFactor * 100.0)));
    updateImage();
}

void MainWindow::zoomOut()
{
    m_fitToWindow = false;
    m_fitAction->setChecked(false);
    m_zoomFitBtn->setChecked(false);
    m_zoomActualBtn->setChecked(false);
    m_zoomFactor = std::max(m_zoomFactor / 1.25, 0.1);
    if (m_zoomLabel) m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_zoomFactor * 100.0)));
    updateImage();
}

void MainWindow::updateImage()
{
    QImage image;
    switch (m_previewMode->checkedId()) {
    case 1: image = m_previews.rawLuminance; break;
    case 2: image = m_previews.rawMosaic; break;
    default: image = m_previews.processed; break;
    }
    if (image.isNull()) return;

    const int turns = currentOrientationTurns() & 3;
    auto orientedPoint = [this, &image, turns](double sourceX, double sourceY) {
        double x = sourceX / std::max(m_rawFrame.width(), 1);
        double y = sourceY / std::max(m_rawFrame.height(), 1);
        if (turns == 1) { const double oldX = x; x = 1.0 - y; y = oldX; }
        else if (turns == 2) { x = 1.0 - x; y = 1.0 - y; }
        else if (turns == 3) { const double oldX = x; x = y; y = 1.0 - oldX; }
        if (m_flipHorizontal && m_flipHorizontal->isChecked()) x = 1.0 - x;
        if (m_flipVertical && m_flipVertical->isChecked()) y = 1.0 - y;
        return QPointF(x * image.width(), y * image.height());
    };
    auto orientedRect = [&orientedPoint](const QRect &rect) {
        const QPointF points[] = {
            orientedPoint(rect.left(), rect.top()),
            orientedPoint(rect.right() + 1.0, rect.top()),
            orientedPoint(rect.left(), rect.bottom() + 1.0),
            orientedPoint(rect.right() + 1.0, rect.bottom() + 1.0)};
        double left = points[0].x(), right = points[0].x();
        double top = points[0].y(), bottom = points[0].y();
        for (const QPointF &point : points) {
            left = std::min(left, point.x()); right = std::max(right, point.x());
            top = std::min(top, point.y()); bottom = std::max(bottom, point.y());
        }
        return QRectF(QPointF(left, top), QPointF(right, bottom));
    };

    const bool stabilizationOverlay = m_activeStep == 2 && m_overlayDetectBtn
        && m_overlayDetectBtn->isChecked();
    const bool persistentCropPreview = m_activeStep >= 3;
    QRect activeCropCanvas;
    bool croppedToCanvas = false;

    if (m_detection.found() && (stabilizationOverlay || persistentCropPreview)) {
        if (stabilizationOverlay) {
            image = image.copy();
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(QColor(QStringLiteral("#f0c64f")), 3));
            painter.drawRect(orientedRect(m_detection.cropRect));
            painter.setPen(QPen(QColor(QStringLiteral("#52d273")), 3));
            painter.drawRect(orientedRect(m_detection.objectRect));
            painter.end();
        }
        activeCropCanvas = orientedRect(m_detection.cropRect).toAlignedRect();
        image = image.copy(activeCropCanvas);
        croppedToCanvas = true;
    } else if (m_detection.found() && m_overlayDetectBtn && m_overlayDetectBtn->isChecked()) {
        image = image.copy();
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF cropF = orientedRect(m_detection.cropRect);
        QPainterPath dimPath;
        dimPath.setFillRule(Qt::OddEvenFill);
        dimPath.addRect(QRectF(image.rect()));
        dimPath.addRect(cropF);
        painter.fillPath(dimPath, QColor(0, 0, 0, 82));
        painter.setPen(QPen(QColor(QStringLiteral("#f0c64f")), 2));
        painter.drawRect(cropF);
        painter.setPen(QPen(QColor(QStringLiteral("#52d273")), 2));
        painter.drawRect(orientedRect(m_detection.objectRect));
        painter.end();
    }

    if (m_overlayHotPixelsBtn && m_overlayHotPixelsBtn->isChecked()
        && !m_hotPixels.hotPixels.isEmpty()) {
        image = image.copy();
        QPainter painter(&image);
        painter.setPen(QPen(QColor(QStringLiteral("#ff4d5a")), 2));
        for (const QPoint point : m_hotPixels.hotPixels) {
            QPointF pt = orientedPoint(point.x() + 0.5, point.y() + 0.5);
            if (croppedToCanvas) pt -= activeCropCanvas.topLeft();
            if (QRectF(image.rect()).contains(pt)) {
                painter.drawEllipse(pt, 4.0, 4.0);
            }
        }
        painter.end();
    }

    if (m_overlayCrosshairBtn && m_overlayCrosshairBtn->isChecked()) {
        image = image.copy();
        QPainter painter(&image);
        painter.setPen(QPen(QColor(56, 127, 181, 160), 1.5, Qt::DashLine));
        const double cx = image.width() / 2.0;
        const double cy = image.height() / 2.0;
        painter.drawLine(QPointF(cx, 0.0), QPointF(cx, image.height()));
        painter.drawLine(QPointF(0.0, cy), QPointF(image.width(), cy));
        painter.end();
    }

    if (m_previewScrollArea && m_previewScrollArea->viewport()) {
        m_previewScrollArea->viewport()->setCursor(
            m_fitToWindow ? Qt::ArrowCursor : Qt::OpenHandCursor);
    }

    if (!m_fitToWindow) {
        const QSize zoomedSize(
            std::max(1, qRound(image.width() * m_zoomFactor)),
            std::max(1, qRound(image.height() * m_zoomFactor)));
        const QImage shown = qFuzzyCompare(m_zoomFactor, 1.0)
            ? image
            : image.scaled(zoomedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_imageLabel->setPixmap(QPixmap::fromImage(shown));
        m_imageLabel->setFixedSize(shown.size());
        m_previewCanvas->setMinimumSize(shown.width() + 48, shown.height() + 48);
        m_previewCanvas->updateGeometry();
        centerPreview();
        return;
    }

    m_previewCanvas->setMinimumSize(0, 0);
    QSize available = m_previewScrollArea->maximumViewportSize() - QSize(48, 48);
    available.setWidth(std::max(available.width(), 1));
    available.setHeight(std::max(available.height(), 1));
    const QImage shown = image.scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(QPixmap::fromImage(shown));
    m_imageLabel->setFixedSize(shown.size());
    m_previewCanvas->updateGeometry();
    centerPreview();
}

void MainWindow::centerPreview()
{
    if (!m_previewScrollArea) return;
    QTimer::singleShot(0, this, [this] {
        auto *horizontal = m_previewScrollArea->horizontalScrollBar();
        auto *vertical = m_previewScrollArea->verticalScrollBar();
        horizontal->setValue((horizontal->minimum() + horizontal->maximum()) / 2);
        vertical->setValue((vertical->minimum() + vertical->maximum()) / 2);
    });
}

void MainWindow::updateMetadata()
{
    if (!m_metadataTable) return;
    if (!m_source || !m_source->isOpen()) {
        for (int row = 0; row < m_metadataTable->rowCount(); ++row)
            setMetadataRow(row, row == 0 ? tr("Status") : QString(),
                           row == 0 ? tr("No source loaded") : QString());
        return;
    }
    const auto &header = m_source->header();
    setMetadataRow(0, tr("Source"), m_source->displayName());
    setMetadataRow(1, tr("Dimensions"), tr("%1 × %2 px").arg(header.width).arg(header.height));
    setMetadataRow(2, tr("Frames"), QString::number(header.frameCount));
    setMetadataRow(3, tr("Pixel depth"), tr("%1-bit").arg(header.pixelDepth));
    setMetadataRow(4, tr("Color format"), header.colorName());
    setMetadataRow(5, tr("Identifier"), header.fileId);
    setMetadataRow(6, tr("Observer"), header.observer.isEmpty() ? tr("—") : header.observer);
    setMetadataRow(7, tr("Instrument"), header.instrument.isEmpty() ? tr("—") : header.instrument);
    setMetadataRow(8, tr("Telescope"), header.telescope.isEmpty() ? tr("—") : header.telescope);
    setMetadataRow(9, tr("Frame size"), tr("%1 bytes").arg(header.bytesPerFrame()));
}

void MainWindow::setMetadataRow(int row, const QString &name, const QString &value)
{
    m_metadataTable->setItem(row, 0, new QTableWidgetItem(name));
    m_metadataTable->setItem(row, 1, new QTableWidgetItem(value));
}

void MainWindow::showStatus(const QString &message, int timeoutMs)
{
    if (m_footerStatusLabel) m_footerStatusLabel->setText(message);
    if (timeoutMs > 0 && m_statusResetTimer) {
        m_statusResetTimer->start(timeoutMs);
    } else {
        m_defaultStatusMessage = message;
        if (m_statusResetTimer) m_statusResetTimer->stop();
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About SeeingForge"),
        tr("<h2>SeeingForge 0.3.0</h2><p>Open-source planetary imaging preparation.</p>"
           "<p>Guided SER and DSLR RAW sequence preprocessing for stacking.</p>"));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_playing) togglePlayback();
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/maximized"), isMaximized());
    settings.setValue(QStringLiteral("window/fullScreen"), isFullScreen());
    settings.sync();
    QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    positionDisplayPopover();
    if (m_fitToWindow) updateImage();
}
