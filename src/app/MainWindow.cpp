#include "app/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QCollator>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QProgressBar>
#include <QPromise>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

static QSize exportCropSize(QSize source, const QRect &object, int margin, QSize requested)
{
    QSize size = source;
    if (object.isValid() && margin >= 0)
        size = QSize(object.width() + 2 * qRound(object.width() * margin / 100.0),
                     object.height() + 2 * qRound(object.height() * margin / 100.0));
    if (requested.isValid()) size = requested;
    size = size.boundedTo(source);
    return QSize(std::max(2, size.width() & ~1), std::max(2, size.height() & ~1));
}

class QualityGraph final : public QWidget {
public:
    explicit QualityGraph(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(110);
        setToolTip(tr("Relative frame quality. Higher is better."));
    }

    void setScores(QVector<double> scores)
    {
        m_scores = std::move(scores);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(QStringLiteral("#101317")));
        const QRectF plot = QRectF(rect()).adjusted(36.0, 10.0, -10.0, -22.0);
        painter.setPen(QColor(QStringLiteral("#39414a")));
        painter.drawRect(plot);
        painter.drawLine(plot.left(), plot.center().y(), plot.right(), plot.center().y());
        painter.setPen(QColor(QStringLiteral("#aab2bd")));
        painter.drawText(QRectF(2.0, plot.top() - 5.0, 32.0, 18.0),
                         Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("100"));
        painter.drawText(QRectF(2.0, plot.bottom() - 9.0, 32.0, 18.0),
                         Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0"));
        painter.drawText(QRectF(plot.left(), plot.bottom() + 3.0, plot.width(), 18.0),
                         Qt::AlignCenter, tr("Frames"));
        if (m_scores.isEmpty()) {
            painter.drawText(plot, Qt::AlignCenter, tr("Analyze frames to show the quality curve"));
            return;
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
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(QStringLiteral("#52d273")), 2.0));
        painter.drawPath(path);
    }

private:
    QVector<double> m_scores;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("SeeingForge"));
    resize(1380, 860);
    setMinimumSize(900, 620);
    m_frameDebounce = new QTimer(this);
    m_frameDebounce->setSingleShot(true);
    m_frameDebounce->setInterval(90);
    connect(m_frameDebounce, &QTimer::timeout, this, &MainWindow::loadSelectedFrame);
    m_detectionDebounce = new QTimer(this);
    m_detectionDebounce->setSingleShot(true);
    m_detectionDebounce->setInterval(120);
    connect(m_detectionDebounce, &QTimer::timeout, this, &MainWindow::updateDetection);
    m_hotPixelDebounce = new QTimer(this);
    m_hotPixelDebounce->setSingleShot(true);
    m_hotPixelDebounce->setInterval(450);
    connect(m_hotPixelDebounce, &QTimer::timeout,
            this, &MainWindow::startHotPixelAnalysis);
    createActions();
    createMenus();
    createWorkspace();

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
    statusBar()->showMessage(tr("Ready — open a SER recording or RAW image sequence to begin."));
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
    auto *processingMenu = menuBar()->addMenu(tr("&Processing"));
    processingMenu->addAction(tr("Save Workflow Preset…"))->setEnabled(false);
    processingMenu->addAction(tr("Reset Current Step"))->setEnabled(false);
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(findChild<QAction *>(QStringLiteral("aboutAction")));
}

void MainWindow::createWorkspace()
{
    auto *workspace = new QWidget(this);
    auto *layout = new QHBoxLayout(workspace);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createWorkflowPanel());
    layout->addWidget(createPreviewPanel(), 1);
    layout->addWidget(createInspectorPanel());
    setCentralWidget(workspace);
}

QWidget *MainWindow::createWorkflowPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("workflowPanel"));
    panel->setFixedWidth(220);
    panel->setStyleSheet(QStringLiteral(
        "#workflowPanel { border-right: 1px solid palette(mid); background: palette(window); }"
        "QListWidget { border: 0; background: transparent; outline: 0; }"
        "QListWidget::item { padding: 13px 10px; border-radius: 5px; }"
        "QListWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); }"));
    auto *layout = new QVBoxLayout(panel);
    auto *heading = new QLabel(tr("WORKFLOW"), panel);
    heading->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 11px; letter-spacing: 1px;"));
    layout->addWidget(heading);
    m_workflowList = new QListWidget(panel);
    m_workflowList->addItems({tr("1  Source"), tr("2  Calibration"), tr("3  Colour"),
                              tr("4  Stabilize & crop"), tr("5  Quality"), tr("6  Export")});
    m_workflowList->setCurrentRow(0);
    connect(m_workflowList, &QListWidget::currentRowChanged,
            this, &MainWindow::selectWorkflowStep);
    layout->addWidget(m_workflowList, 1);
    auto *presetButton = new QPushButton(tr("Save preset"), panel);
    presetButton->setEnabled(false);
    layout->addWidget(presetButton);
    return panel;
}

QWidget *MainWindow::createPreviewPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("previewPanel"));
    panel->setStyleSheet(QStringLiteral("#previewPanel { background: #101317; }"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *bar = new QFrame(panel);
    bar->setStyleSheet(QStringLiteral("background: palette(window); border-bottom: 1px solid palette(mid);"));
    auto *barLayout = new QHBoxLayout(bar);
    auto *title = new QLabel(tr("Colour preview"), bar);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    barLayout->addWidget(title);
    m_previewStateLabel = new QLabel(tr("No source loaded"), bar);
    m_previewStateLabel->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    barLayout->addWidget(m_previewStateLabel);
    barLayout->addStretch();
    m_previewMode = new QButtonGroup(this);
    auto *processedButton = new QToolButton(bar);
    processedButton->setText(tr("Colour"));
    processedButton->setCheckable(true);
    processedButton->setChecked(true);
    auto *originalButton = new QToolButton(bar);
    originalButton->setText(tr("Raw luminance"));
    originalButton->setCheckable(true);
    m_previewMode->addButton(processedButton, 0);
    m_previewMode->addButton(originalButton, 1);
    auto *mosaicButton = new QToolButton(bar);
    mosaicButton->setText(tr("Raw CFA mosaic"));
    mosaicButton->setCheckable(true);
    m_previewMode->addButton(mosaicButton, 2);
    connect(m_previewMode, &QButtonGroup::idClicked, this, &MainWindow::previewModeChanged);
    barLayout->addWidget(processedButton);
    barLayout->addWidget(originalButton);
    barLayout->addWidget(mosaicButton);
    layout->addWidget(bar);

    m_previewScrollArea = new QScrollArea(panel);
    m_previewScrollArea->setAlignment(Qt::AlignCenter);
    m_previewScrollArea->setWidgetResizable(true);
    m_previewScrollArea->setStyleSheet(QStringLiteral("QScrollArea { border: 0; background: #101317; }"));
    m_previewCanvas = new QWidget(m_previewScrollArea);
    m_previewCanvas->setStyleSheet(QStringLiteral("background: #101317;"));
    auto *canvasLayout = new QVBoxLayout(m_previewCanvas);
    canvasLayout->setContentsMargins(24, 24, 24, 24);
    m_imageLabel = new QLabel(m_previewCanvas);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    canvasLayout->addWidget(m_imageLabel, 1, Qt::AlignCenter);
    m_emptyLabel = new QLabel(tr("Open a SER recording or RAW image sequence\n\nUse the File menu to begin"), m_previewCanvas);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 18px;"));
    canvasLayout->addWidget(m_emptyLabel, 1);
    m_previewScrollArea->setWidget(m_previewCanvas);
    layout->addWidget(m_previewScrollArea, 1);

    auto *viewControls = new QFrame(panel);
    viewControls->setStyleSheet(QStringLiteral(
        "background: palette(window); border-top: 1px solid palette(mid);"));
    auto *viewLayout = new QHBoxLayout(viewControls);
    viewLayout->setContentsMargins(8, 5, 8, 5);
    viewLayout->addStretch();
    auto *fitButton = new QPushButton(tr("Fit"), viewControls);
    fitButton->setToolTip(tr("Fit image to the available preview area (Ctrl+0)"));
    connect(fitButton, &QPushButton::clicked, this, &MainWindow::fitImage);
    viewLayout->addWidget(fitButton);
    auto *actualButton = new QPushButton(tr("1:1"), viewControls);
    actualButton->setToolTip(tr("Show one image pixel per screen pixel (Ctrl+1)"));
    connect(actualButton, &QPushButton::clicked, this, &MainWindow::showActualSize);
    viewLayout->addWidget(actualButton);
    auto *zoomOutButton = new QPushButton(QStringLiteral("−"), viewControls);
    zoomOutButton->setToolTip(tr("Zoom out"));
    zoomOutButton->setFixedWidth(36);
    connect(zoomOutButton, &QPushButton::clicked, this, &MainWindow::zoomOut);
    viewLayout->addWidget(zoomOutButton);
    m_zoomLabel = new QLabel(tr("Fit"), viewControls);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(58);
    viewLayout->addWidget(m_zoomLabel);
    auto *zoomInButton = new QPushButton(QStringLiteral("+"), viewControls);
    zoomInButton->setToolTip(tr("Zoom in"));
    zoomInButton->setFixedWidth(36);
    connect(zoomInButton, &QPushButton::clicked, this, &MainWindow::zoomIn);
    viewLayout->addWidget(zoomInButton);
    viewLayout->addStretch();
    layout->addWidget(viewControls);

    auto *timeline = new QFrame(panel);
    timeline->setStyleSheet(QStringLiteral("background: palette(window); border-top: 1px solid palette(mid);"));
    auto *timelineLayout = new QHBoxLayout(timeline);
    m_frameLabel = new QLabel(tr("Frame — / —"), timeline);
    timelineLayout->addWidget(m_frameLabel);
    m_frameSlider = new QSlider(Qt::Horizontal, timeline);
    m_frameSlider->setEnabled(false);
    connect(m_frameSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_source && m_source->isOpen()) {
            m_frameLabel->setText(tr("Frame %1 / %2")
                                      .arg(value + 1)
                                      .arg(m_source->header().frameCount));
            m_frameDebounce->start();
        }
    });
    timelineLayout->addWidget(m_frameSlider, 1);
    layout->addWidget(timeline);
    return panel;
}

QWidget *MainWindow::createInspectorPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("inspectorPanel"));
    panel->setFixedWidth(285);
    panel->setStyleSheet(QStringLiteral("#inspectorPanel { border-left: 1px solid palette(mid); background: palette(window); }"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    m_inspectorStack = new QStackedWidget(panel);
    m_inspectorStack->addWidget(createSourceInspector());
    m_inspectorStack->addWidget(createCalibrationInspector());
    m_inspectorStack->addWidget(createColourInspector());
    m_inspectorStack->addWidget(createStabilizeInspector());
    m_inspectorStack->addWidget(createQualityInspector());
    m_inspectorStack->addWidget(createExportInspector());
    m_inspectorStack->setCurrentIndex(0);
    layout->addWidget(m_inspectorStack);
    return panel;
}

QWidget *MainWindow::createSourceInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(tr("<h2>Source</h2><p>Recording information</p>"), page));
    m_metadataTable = new QTableWidget(10, 2, page);
    m_metadataTable->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    m_metadataTable->verticalHeader()->hide();
    m_metadataTable->horizontalHeader()->setStretchLastSection(true);
    m_metadataTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_metadataTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_metadataTable->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_metadataTable, 1);
    auto *continueButton = new QPushButton(tr("Continue to calibration"), page);
    connect(continueButton, &QPushButton::clicked, this,
            [this] { m_workflowList->setCurrentRow(1); });
    layout->addWidget(continueButton);
    updateMetadata();
    return page;
}

QWidget *MainWindow::createColourInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->addWidget(new QLabel(tr("<h2>Colour</h2><p>Changes update the preview without altering the source.</p>"), page));
    layout->addWidget(new QLabel(tr("Debayer method"), page));
    m_debayerMethod = new QComboBox(page);
    m_debayerMethod->addItem(tr("Bayer-aware colour preview"), true);
    m_debayerMethod->addItem(tr("No debayer — neutral raw preview"), false);
    connect(m_debayerMethod, &QComboBox::currentIndexChanged, this, &MainWindow::requestPreviewRender);
    layout->addWidget(m_debayerMethod);
    layout->addWidget(new QLabel(tr("Bayer pattern"), page));
    m_bayerPattern = new QComboBox(page);
    m_bayerPattern->addItem(tr("From source metadata"), 0);
    m_bayerPattern->addItem(tr("RGGB"), 8);
    m_bayerPattern->addItem(tr("GRBG"), 9);
    m_bayerPattern->addItem(tr("GBRG"), 10);
    m_bayerPattern->addItem(tr("BGGR"), 11);
    connect(m_bayerPattern, &QComboBox::currentIndexChanged,
            this, &MainWindow::requestPreviewRender);
    layout->addWidget(m_bayerPattern);
    m_whiteBalance = new QCheckBox(tr("Automatic white balance"), page);
    m_whiteBalance->setChecked(true);
    connect(m_whiteBalance, &QCheckBox::toggled, this, &MainWindow::requestPreviewRender);
    layout->addWidget(m_whiteBalance);

    auto addBalance = [&](const QString &name, QSlider **member, int value) {
        auto *label = new QLabel(QStringLiteral("%1: %2").arg(name).arg(value / 100.0, 0, 'f', 2), page);
        auto *slider = new QSlider(Qt::Horizontal, page);
        slider->setRange(50, 200);
        slider->setValue(value);
        connect(slider, &QSlider::valueChanged, this, [this, label, name](int current) {
            label->setText(QStringLiteral("%1: %2").arg(name).arg(current / 100.0, 0, 'f', 2));
            requestPreviewRender();
        });
        layout->addWidget(label);
        layout->addWidget(slider);
        *member = slider;
    };
    addBalance(tr("Red balance"), &m_redBalance, 100);
    addBalance(tr("Blue balance"), &m_blueBalance, 100);

    auto *saturationLabel = new QLabel(tr("Saturation: 100%"), page);
    m_saturation = new QSlider(Qt::Horizontal, page);
    m_saturation->setRange(0, 400);
    m_saturation->setValue(100);
    m_saturation->setToolTip(tr("0% is monochrome; higher values reveal weak colour differences."));
    connect(m_saturation, &QSlider::valueChanged, this,
            [this, saturationLabel](int value) {
                saturationLabel->setText(tr("Saturation: %1%").arg(value));
                requestPreviewRender();
            });
    layout->addWidget(saturationLabel);
    layout->addWidget(m_saturation);

    auto *hueLabel = new QLabel(tr("Hue shift: 0°"), page);
    m_hue = new QSlider(Qt::Horizontal, page);
    m_hue->setRange(-180, 180);
    m_hue->setValue(0);
    m_hue->setToolTip(tr("Rotate all colours while preserving brightness."));
    connect(m_hue, &QSlider::valueChanged, this,
            [this, hueLabel](int value) {
                hueLabel->setText(tr("Hue shift: %1°").arg(value));
                requestPreviewRender();
            });
    layout->addWidget(hueLabel);
    layout->addWidget(m_hue);

    auto addTone = [&](const QString &name, QSlider **member, int minimum,
                       int maximum, int value) {
        auto *label = new QLabel(QStringLiteral("%1: %2×").arg(name).arg(value / 100.0, 0, 'f', 2), page);
        auto *slider = new QSlider(Qt::Horizontal, page);
        slider->setRange(minimum, maximum);
        slider->setValue(value);
        connect(slider, &QSlider::valueChanged, this, [this, label, name](int current) {
            label->setText(QStringLiteral("%1: %2×").arg(name).arg(current / 100.0, 0, 'f', 2));
            requestPreviewRender();
        });
        layout->addWidget(label);
        layout->addWidget(slider);
        *member = slider;
    };
    addTone(tr("Preview gain"), &m_gain, 25, 400, 100);
    addTone(tr("Preview gamma"), &m_gamma, 25, 300, 100);

    layout->addWidget(new QLabel(tr("Orientation"), page));
    m_orientation = new QComboBox(page);
    m_orientation->addItem(tr("No rotation"), 0);
    m_orientation->addItem(tr("Rotate 90° clockwise"), 1);
    m_orientation->addItem(tr("Rotate 180°"), 2);
    m_orientation->addItem(tr("Rotate 90° counter-clockwise"), 3);
    connect(m_orientation, &QComboBox::currentIndexChanged,
            this, &MainWindow::requestPreviewRender);
    layout->addWidget(m_orientation);
    m_flipHorizontal = new QCheckBox(tr("Flip horizontally"), page);
    m_flipVertical = new QCheckBox(tr("Flip vertically"), page);
    connect(m_flipHorizontal, &QCheckBox::toggled, this, &MainWindow::requestPreviewRender);
    connect(m_flipVertical, &QCheckBox::toggled, this, &MainWindow::requestPreviewRender);
    layout->addWidget(m_flipHorizontal);
    layout->addWidget(m_flipVertical);

    m_autoStretch = new QCheckBox(tr("Auto-stretch preview"), page);
    m_autoStretch->setChecked(true);
    connect(m_autoStretch, &QCheckBox::toggled, this, &MainWindow::requestPreviewRender);
    layout->addWidget(m_autoStretch);
    auto *note = new QLabel(
        tr("<b>Preview only:</b> These colour adjustments do not modify the source "
           "or its raw pixel data."), page);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral(
        "color: #f0c96a; background: rgba(240, 201, 106, 0.08); "
        "border: 1px solid rgba(240, 201, 106, 0.35); border-radius: 4px; padding: 8px;"));
    layout->addWidget(note);
    layout->addStretch();
    auto *continueButton = new QPushButton(tr("Continue to stabilization"), page);
    connect(continueButton, &QPushButton::clicked, this, [this] { m_workflowList->setCurrentRow(3); });
    layout->addWidget(continueButton);
    return page;
}

QWidget *MainWindow::createCalibrationInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->addWidget(new QLabel(tr("<h2>Calibration</h2><p>Find persistent sensor defects across the recording.</p>"), page));

    layout->addWidget(new QLabel(tr("RAW export colour calibration"), page));
    m_rawExportWhiteBalance = new QComboBox(page);
    m_rawExportWhiteBalance->addItem(tr("Preserve linear sensor values"), false);
    m_rawExportWhiteBalance->addItem(tr("Apply camera white balance"), true);
    m_rawExportWhiteBalance->setEnabled(false);
    m_rawExportWhiteBalance->setToolTip(
        tr("Camera white balance is available after loading a compatible DSLR RAW sequence."));
    layout->addWidget(m_rawExportWhiteBalance);
    auto *whiteBalanceNote = new QLabel(
        tr("Linear preserves the normalized sensor response. Camera white balance produces a more neutral SER by applying the RAW file's recorded channel multipliers without clipping highlights."), page);
    whiteBalanceNote->setWordWrap(true);
    whiteBalanceNote->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(whiteBalanceNote);

    m_hotPixelSamples = new QComboBox(page);
    m_hotPixelSamples->addItem(tr("8 sampled frames — fast"), 8);
    m_hotPixelSamples->addItem(tr("16 sampled frames — recommended"), 16);
    m_hotPixelSamples->addItem(tr("32 sampled frames — thorough"), 32);
    m_hotPixelSamples->setCurrentIndex(1);
    connect(m_hotPixelSamples, &QComboBox::currentIndexChanged,
            this, &MainWindow::requestHotPixelAnalysis);
    layout->addWidget(new QLabel(tr("Frames distributed across recording"), page));
    layout->addWidget(m_hotPixelSamples);

    auto addSlider = [&](const QString &name, QSlider **member, int minimum,
                         int maximum, int value, const QString &suffix) {
        auto *label = new QLabel(QStringLiteral("%1: %2%3").arg(name).arg(value).arg(suffix), page);
        auto *slider = new QSlider(Qt::Horizontal, page);
        slider->setRange(minimum, maximum);
        slider->setValue(value);
        connect(slider, &QSlider::valueChanged, this, [this, label, name, suffix](int current) {
            label->setText(QStringLiteral("%1: %2%3").arg(name).arg(current).arg(suffix));
            requestHotPixelAnalysis();
        });
        layout->addWidget(label);
        layout->addWidget(slider);
        *member = slider;
    };
    addSlider(tr("Minimum brightness"), &m_hotPixelBrightness, 32, 255, 200, QString());
    addSlider(tr("Local contrast"), &m_hotPixelContrast, 5, 128, 30, QString());
    addSlider(tr("Required persistence"), &m_hotPixelPersistence, 50, 100, 80, QStringLiteral("%"));

    m_analyzeHotPixels = new QPushButton(tr("Analyze hot pixels"), page);
    connect(m_analyzeHotPixels, &QPushButton::clicked,
            this, &MainWindow::startHotPixelAnalysis);
    layout->addWidget(m_analyzeHotPixels);
    m_showHotPixels = new QCheckBox(tr("Show hot-pixel overlay"), page);
    m_showHotPixels->setChecked(true);
    connect(m_showHotPixels, &QCheckBox::toggled, this, [this] { updateImage(); });
    layout->addWidget(m_showHotPixels);
    m_hotPixelLabel = new QLabel(tr("Analysis has not been run."), page);
    m_hotPixelLabel->setWordWrap(true);
    m_hotPixelLabel->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(m_hotPixelLabel);
    auto *note = new QLabel(tr("A candidate must remain bright and locally isolated at the same sensor coordinate across the sampled frames."), page);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(note);
    layout->addStretch();
    auto *continueButton = new QPushButton(tr("Continue to colour"), page);
    connect(continueButton, &QPushButton::clicked, this,
            [this] { m_workflowList->setCurrentRow(2); });
    layout->addWidget(continueButton);
    return page;
}

QWidget *MainWindow::createPlaceholderInspector(const QString &title, const QString &description)
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->addWidget(new QLabel(QStringLiteral("<h2>%1</h2>").arg(title.toHtmlEscaped()), page));
    auto *body = new QLabel(description, page);
    body->setWordWrap(true);
    body->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(body);
    layout->addStretch();
    return page;
}

QWidget *MainWindow::createStabilizeInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->addWidget(new QLabel(tr("<h2>Stabilize & crop</h2><p>Detect the target and define safe surrounding space.</p>"), page));

    auto *thresholdLabel = new QLabel(tr("Detection threshold: 16"), page);
    m_detectionThreshold = new QSlider(Qt::Horizontal, page);
    m_detectionThreshold->setRange(1, 254);
    m_detectionThreshold->setValue(16);
    connect(m_detectionThreshold, &QSlider::valueChanged, this,
            [this, thresholdLabel](int value) {
                thresholdLabel->setText(tr("Detection threshold: %1").arg(value));
                if (m_detectionDebounce) m_detectionDebounce->start();
            });
    layout->addWidget(thresholdLabel);
    layout->addWidget(m_detectionThreshold);

    layout->addWidget(new QLabel(tr("Space around detected object"), page));
    m_cropMargin = new QComboBox(page);
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
    layout->addWidget(m_cropMargin);

    layout->addWidget(new QLabel(tr("Output dimensions"), page));
    m_outputSizeMode = new QComboBox(page);
    m_outputSizeMode->addItem(tr("Automatic from target and surrounding space"), QSize());
    m_outputSizeMode->addItem(tr("320 × 240"), QSize(320, 240));
    m_outputSizeMode->addItem(tr("640 × 480"), QSize(640, 480));
    m_outputSizeMode->addItem(tr("800 × 600"), QSize(800, 600));
    m_outputSizeMode->addItem(tr("1024 × 768"), QSize(1024, 768));
    m_outputSizeMode->addItem(tr("Custom dimensions"), QSize(-2, -2));
    layout->addWidget(m_outputSizeMode);
    auto *customSize = new QHBoxLayout;
    m_customOutputWidth = new QSpinBox(page);
    m_customOutputHeight = new QSpinBox(page);
    for (QSpinBox *box : {m_customOutputWidth, m_customOutputHeight}) {
        box->setRange(2, 16384);
        box->setSingleStep(2);
        box->setEnabled(false);
    }
    m_customOutputWidth->setValue(640);
    m_customOutputHeight->setValue(480);
    customSize->addWidget(m_customOutputWidth);
    customSize->addWidget(new QLabel(QStringLiteral("×"), page));
    customSize->addWidget(m_customOutputHeight);
    layout->addLayout(customSize);
    connect(m_outputSizeMode, &QComboBox::currentIndexChanged, this, [this] {
        const bool custom = m_outputSizeMode->currentData().toSize() == QSize(-2, -2);
        m_customOutputWidth->setEnabled(custom);
        m_customOutputHeight->setEnabled(custom);
        updateDetection();
    });
    connect(m_customOutputWidth, &QSpinBox::valueChanged, this, [this] { updateDetection(); });
    connect(m_customOutputHeight, &QSpinBox::valueChanged, this, [this] { updateDetection(); });
    auto *sizeNote = new QLabel(
        tr("Fixed dimensions add surrounding space when available and never upscale the source."), page);
    sizeNote->setWordWrap(true);
    sizeNote->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(sizeNote);

    m_showDetection = new QCheckBox(tr("Show detection and recommended crop"), page);
    m_showDetection->setChecked(true);
    connect(m_showDetection, &QCheckBox::toggled, this, [this] { updateImage(); });
    layout->addWidget(m_showDetection);

    m_detectionLabel = new QLabel(tr("Open a recording to detect its target."), page);
    m_detectionLabel->setWordWrap(true);
    m_detectionLabel->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(m_detectionLabel);
    auto *legend = new QLabel(tr("<span style='color:#52d273'>Green</span>: detected object<br>"
                                 "<span style='color:#f0c64f'>Yellow</span>: recommended crop"), page);
    layout->addWidget(legend);
    layout->addStretch();
    auto *continueButton = new QPushButton(tr("Continue to quality"), page);
    connect(continueButton, &QPushButton::clicked, this,
            [this] { m_workflowList->setCurrentRow(4); });
    layout->addWidget(continueButton);
    return page;
}

QWidget *MainWindow::createQualityInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->addWidget(new QLabel(
        tr("<h2>Quality</h2><p>Rank frames by target-local fine detail, contrast, exposure stability, and clipping.</p>"), page));

    m_analyzeQuality = new QPushButton(tr("Analyze all frames"), page);
    connect(m_analyzeQuality, &QPushButton::clicked,
            this, &MainWindow::startQualityAnalysis);
    m_cancelQuality = new QPushButton(tr("Cancel"), page);
    m_cancelQuality->setEnabled(false);
    connect(m_cancelQuality, &QPushButton::clicked,
            this, &MainWindow::cancelQualityAnalysis);
    auto *qualityButtons = new QHBoxLayout;
    qualityButtons->addWidget(m_analyzeQuality, 1);
    qualityButtons->addWidget(m_cancelQuality);
    layout->addLayout(qualityButtons);
    m_qualityProgress = new QProgressBar(page);
    m_qualityProgress->setMinimumHeight(20);
    m_qualityProgress->setStyleSheet(QStringLiteral(
        "QProgressBar { border: 1px solid #69717d; border-radius: 3px; background: #252930; text-align: center; color: white; }"
        "QProgressBar::chunk { background: #387fb5; }"));
    m_qualityProgress->setTextVisible(true);
    m_qualityProgress->setFormat(tr("%v / %m frames (%p%)"));
    m_qualityProgress->hide();
    layout->addWidget(m_qualityProgress);

    layout->addWidget(new QLabel(tr("Frames to keep"), page));
    m_qualityKeep = new QComboBox(page);
    m_qualityKeep->addItem(tr("Best 10% — strict"), 10);
    m_qualityKeep->addItem(tr("Best 25% — recommended starting point"), 25);
    m_qualityKeep->addItem(tr("Best 50%"), 50);
    m_qualityKeep->addItem(tr("Best 75%"), 75);
    m_qualityKeep->addItem(tr("100% — keep all"), 100);
    m_qualityKeep->setCurrentIndex(1);
    connect(m_qualityKeep, &QComboBox::currentIndexChanged,
            this, &MainWindow::updateQualityDisplay);
    layout->addWidget(m_qualityKeep);

    m_rejectClipped = new QCheckBox(tr("Reject overexposed frames"), page);
    m_rejectClipped->setChecked(true);
    layout->addWidget(m_rejectClipped);
    auto *clippedRow = new QHBoxLayout;
    clippedRow->addWidget(new QLabel(tr("Maximum clipped target pixels"), page));
    m_maxClippedPercent = new QSpinBox(page);
    m_maxClippedPercent->setRange(1, 100);
    m_maxClippedPercent->setValue(5);
    m_maxClippedPercent->setSuffix(QStringLiteral("%"));
    clippedRow->addWidget(m_maxClippedPercent);
    layout->addLayout(clippedRow);
    connect(m_rejectClipped, &QCheckBox::toggled,
            m_maxClippedPercent, &QSpinBox::setEnabled);
    connect(m_rejectClipped, &QCheckBox::toggled,
            this, &MainWindow::updateQualityDisplay);
    connect(m_maxClippedPercent, &QSpinBox::valueChanged,
            this, &MainWindow::updateQualityDisplay);

    layout->addWidget(new QLabel(tr("Frame order"), page));
    m_qualityOrder = new QComboBox(page);
    m_qualityOrder->addItem(tr("Original capture order"), 0);
    m_qualityOrder->addItem(tr("Quality score — best first"), 1);
    connect(m_qualityOrder, &QComboBox::currentIndexChanged,
            this, &MainWindow::updateQualityDisplay);
    layout->addWidget(m_qualityOrder);

    m_qualityLabel = new QLabel(tr("Quality has not been analyzed."), page);
    m_qualityLabel->setWordWrap(true);
    m_qualityLabel->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(m_qualityLabel);

    m_qualityGraph = new QualityGraph(page);
    layout->addWidget(m_qualityGraph);

    m_qualityTable = new QTableWidget(0, 3, page);
    m_qualityTable->setHorizontalHeaderLabels({tr("Frame"), tr("Score"), tr("Keep")});
    m_qualityTable->verticalHeader()->hide();
    m_qualityTable->horizontalHeader()->setStretchLastSection(true);
    m_qualityTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_qualityTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_qualityTable->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_qualityTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                if (!m_source || !m_source->isOpen()) return;
                const auto *item = m_qualityTable->item(row, 0);
                if (!item) return;
                const int frameIndex = item->data(Qt::UserRole).toInt();
                m_frameSlider->setValue(frameIndex);
                loadFrame(static_cast<std::uint32_t>(frameIndex));
            });
    layout->addWidget(m_qualityTable, 1);

    auto *note = new QLabel(
        tr("Double-click a row to inspect that frame. Ranking does not alter raw data."), page);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(note);
    auto *continueButton = new QPushButton(tr("Continue to export"), page);
    connect(continueButton, &QPushButton::clicked, this,
            [this] { m_workflowList->setCurrentRow(5); });
    layout->addWidget(continueButton);
    return page;
}

QWidget *MainWindow::createExportInspector()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->addWidget(new QLabel(
        tr("<h2>Export</h2><p>Create a stabilized SER recording from the selected frames.</p>"), page));

    layout->addWidget(new QLabel(tr("Format"), page));
    auto *format = new QComboBox(page);
    format->addItem(tr("SER recording — preserve source pixel depth"));
    format->setEnabled(false);
    layout->addWidget(format);

    layout->addWidget(new QLabel(tr("Output intensity"), page));
    m_exportIntensity = new QComboBox(page);
    m_exportIntensity->addItem(tr("Preserve source intensity (1×)"), 1.0);
    m_exportIntensity->addItem(tr("Automatic — target peak near 90%"), 0.0);
    m_exportIntensity->addItem(tr("Increase values 2×"), 2.0);
    m_exportIntensity->addItem(tr("Increase values 4×"), 4.0);
    m_exportIntensity->addItem(tr("Increase values 8×"), 8.0);
    m_exportIntensity->addItem(tr("Increase values 16×"), 16.0);
    connect(m_exportIntensity, &QComboBox::currentIndexChanged,
            this, &MainWindow::updateExportIntensityDisplay);
    layout->addWidget(m_exportIntensity);
    m_exportIntensityLabel = new QLabel(tr("Load a source to measure its intensity."), page);
    m_exportIntensityLabel->setWordWrap(true);
    m_exportIntensityLabel->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(m_exportIntensityLabel);

    m_exportMonochrome = new QCheckBox(tr("Export in monochrome"), page);
    m_exportMonochrome->setToolTip(
        tr("Convert to monochrome at full resolution. The source file is never changed."));
    layout->addWidget(m_exportMonochrome);
    auto *monochromeWarning = new QLabel(
        tr("The exported copy will permanently discard all colour information."), page);
    monochromeWarning->setWordWrap(true);
    monochromeWarning->setStyleSheet(QStringLiteral("color: #f0c96a; font-weight: 600;"));
    layout->addWidget(monochromeWarning);
    m_exportMonoBinning = new QCheckBox(tr("2×2 binning (half width and height)"), page);
    m_exportMonoBinning->setEnabled(false);
    connect(m_exportMonochrome, &QCheckBox::toggled,
            m_exportMonoBinning, &QCheckBox::setEnabled);
    layout->addWidget(m_exportMonoBinning);
    auto *monochromeNote = new QLabel(
        tr("Monochrome preserves full resolution by default. Optional 2×2 binning reduces "
           "width and height by half. Cropping still applies."), page);
    monochromeNote->setWordWrap(true);
    monochromeNote->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(monochromeNote);

    m_exportSummary = new QLabel(
        tr("The output will be placed beside the source in a new seeingforge_yyyymmdd_hhmm folder."), page);
    m_exportSummary->setWordWrap(true);
    layout->addWidget(m_exportSummary);

    auto *processing = new QLabel(
        tr("<b>Applied:</b> quality selection and order, recommended crop size, "
           "and per-frame object centering.<br><br>"
           "SER metadata is preserved. Raw values remain unchanged at 1× unless camera white balance or an intensity increase is selected. Preview colour controls and overlays remain preview-only."), page);
    processing->setWordWrap(true);
    processing->setStyleSheet(QStringLiteral(
        "color: #aab2bd; background: rgba(255,255,255,0.035); "
        "border: 1px solid palette(mid); border-radius: 4px; padding: 8px;"));
    layout->addWidget(processing);

    m_exportButton = new QPushButton(tr("Export SER"), page);
    connect(m_exportButton, &QPushButton::clicked, this, &MainWindow::startExport);
    m_cancelExport = new QPushButton(tr("Cancel"), page);
    m_cancelExport->setEnabled(false);
    connect(m_cancelExport, &QPushButton::clicked, this, &MainWindow::cancelExport);
    auto *exportButtons = new QHBoxLayout;
    exportButtons->addWidget(m_exportButton, 1);
    exportButtons->addWidget(m_cancelExport);
    layout->addLayout(exportButtons);
    m_exportProgress = new QProgressBar(page);
    m_exportProgress->setMinimumHeight(20);
    m_exportProgress->setStyleSheet(QStringLiteral(
        "QProgressBar { border: 1px solid #69717d; border-radius: 3px; background: #252930; text-align: center; color: white; }"
        "QProgressBar::chunk { background: #387fb5; }"));
    m_exportProgress->setFormat(tr("%v / %m frames (%p%)"));
    m_exportProgress->hide();
    layout->addWidget(m_exportProgress);
    m_exportResultLabel = new QLabel(tr("Nothing exported yet."), page);
    m_exportResultLabel->setWordWrap(true);
    m_exportResultLabel->setStyleSheet(QStringLiteral("color: #aab2bd;"));
    layout->addWidget(m_exportResultLabel);
    layout->addStretch();
    return page;
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
    QStringList files = directory.entryList(
        {QStringLiteral("*.cr2"), QStringLiteral("*.CR2"), QStringLiteral("*.cr3"), QStringLiteral("*.CR3"),
         QStringLiteral("*.nef"), QStringLiteral("*.NEF"), QStringLiteral("*.arw"), QStringLiteral("*.ARW"),
         QStringLiteral("*.dng"), QStringLiteral("*.DNG"), QStringLiteral("*.raf"), QStringLiteral("*.RAF"),
         QStringLiteral("*.orf"), QStringLiteral("*.ORF"), QStringLiteral("*.rw2"), QStringLiteral("*.RW2")},
        QDir::Files, QDir::Name);
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
    statusBar()->showMessage(tr("Loading RAW sequence…"));

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
        statusBar()->showMessage(tr("RAW sequence loading canceled."), 5000);
        return;
    }
    if (!source || !source->isOpen()) {
        QMessageBox::critical(this, tr("Cannot Open RAW Sequence"),
                              source ? source->errorString() : tr("The RAW sequence could not be loaded."));
        statusBar()->showMessage(tr("RAW sequence loading failed."), 5000);
        return;
    }
    m_source = source;
    QSettings settings;
    settings.setValue(QStringLiteral("files/lastOpenFolder"), m_source->sourceDirectory());
    finishLoadingSource();
}

void MainWindow::finishLoadingSource()
{
    m_currentFrame = 0;
    m_frameSlider->setRange(0, static_cast<int>(m_source->header().frameCount - 1));
    m_frameSlider->setValue(0);
    m_frameSlider->setEnabled(true);
    m_debayerMethod->setEnabled(seeingforge::PreviewProcessor::supportsDebayer(m_source->header().colorId));
    m_bayerPattern->setItemText(0, tr("From source — %1").arg(m_source->header().colorName()));
    const bool hasCameraWhiteBalance = m_source->hasCameraWhiteBalance();
    m_rawExportWhiteBalance->setEnabled(hasCameraWhiteBalance);
    if (!hasCameraWhiteBalance) m_rawExportWhiteBalance->setCurrentIndex(0);
    m_hotPixels = {};
    m_hotPixelLabel->setText(tr("Analysis has not been run."));
    m_qualityResult = {};
    m_qualityTable->setRowCount(0);
    m_qualityGraph->setScores({});
    m_qualityLabel->setText(tr("Quality has not been analyzed."));
    m_workflowList->setCurrentRow(0);
    loadFrame(0);
    updateMetadata();
    setWindowTitle(QStringLiteral("%1 — SeeingForge").arg(m_source->displayName()));
    statusBar()->showMessage(tr("Loaded %1").arg(m_source->displayName()));
}

void MainWindow::loadFrame(std::uint32_t index)
{
    m_rawFrame = m_source->readFrame(index);
    if (m_rawFrame.isNull()) {
        QMessageBox::critical(this, tr("Cannot Read Frame"), tr("Frame %1 could not be decoded.").arg(index + 1));
        return;
    }
    m_currentFrame = index;
    m_emptyLabel->hide();
    m_frameLabel->setText(tr("Frame %1 / %2").arg(index + 1).arg(m_source->header().frameCount));
    updateExportIntensityDisplay();
    requestPreviewRender();
    if (m_detectionDebounce) m_detectionDebounce->start();
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
    settings.rotationQuarterTurns = m_orientation->currentData().toInt();
    settings.flipHorizontal = m_flipHorizontal->isChecked();
    settings.flipVertical = m_flipVertical->isChecked();
    return settings;
}

std::uint32_t MainWindow::effectiveColorId() const
{
    if (m_bayerPattern && m_bayerPattern->currentData().toUInt() != 0)
        return m_bayerPattern->currentData().toUInt();
    return m_source->header().colorId;
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
        m_detectionLabel->setText(
            tr("Object: %1 × %2 px\nRecommended crop: %3 × %4 px")
                .arg(m_detection.objectRect.width()).arg(m_detection.objectRect.height())
                .arg(m_detection.cropRect.width()).arg(m_detection.cropRect.height()));
    } else {
        m_detectionLabel->setText(tr("No object detected. Lower the threshold until the target is outlined."));
    }
    updateImage();
    updateExportIntensityDisplay();
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
    m_hotPixelLabel->setText(
        tr("Detected %1 persistent hot pixels from %2 initial candidates across %3 frames.")
            .arg(m_hotPixels.hotPixels.size())
            .arg(m_hotPixels.initialCandidates)
            .arg(m_hotPixels.sampledFrames));
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
        tr("Analyzing %1 frames… Large recordings can take a while because every frame is read.")
            .arg(m_source->header().frameCount));
    statusBar()->showMessage(tr("Analyzing frame quality…"));
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
        statusBar()->showMessage(tr("Quality analysis canceled."), 5000);
        return;
    }
    m_qualityResult = result;
    statusBar()->showMessage(tr("Quality analysis complete."), 5000);
    updateQualityDisplay();
}

void MainWindow::cancelQualityAnalysis()
{
    if (!m_qualityWatcher->isRunning() || !m_qualityCancel) return;
    m_qualityCancel->store(true);
    m_cancelQuality->setEnabled(false);
    m_qualityLabel->setText(tr("Canceling after the current frame…"));
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
    for (const std::uint32_t frameIndex : m_qualityResult.rankedFrameIndices) {
        if (!m_rejectClipped->isChecked()
            || clippedByIndex[static_cast<qsizetype>(frameIndex)]
                   <= m_maxClippedPercent->value())
            eligibleFrames.append(frameIndex);
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

    QVector<std::uint32_t> displayOrder;
    if (m_qualityOrder->currentData().toInt() == 1) {
        displayOrder = m_qualityResult.rankedFrameIndices;
    } else {
        displayOrder.reserve(m_qualityResult.frames.size());
        for (const auto &frame : m_qualityResult.frames)
            displayOrder.append(frame.frameIndex);
    }

    m_qualityTable->setSortingEnabled(false);
    m_qualityTable->setRowCount(displayOrder.size());
    QVector<double> displayedScores;
    displayedScores.reserve(displayOrder.size());
    for (qsizetype row = 0; row < displayOrder.size(); ++row) {
        const std::uint32_t frameIndex = displayOrder[row];
        displayedScores.append(scoreByIndex[static_cast<qsizetype>(frameIndex)]);
        auto *frameItem = new QTableWidgetItem(QString::number(frameIndex + 1U));
        frameItem->setData(Qt::UserRole, static_cast<qulonglong>(frameIndex));
        auto *scoreItem = new QTableWidgetItem(
            QString::number(scoreByIndex[static_cast<qsizetype>(frameIndex)], 'f', 1));
        const bool clippedRejected = m_rejectClipped->isChecked()
            && clippedByIndex[static_cast<qsizetype>(frameIndex)]
                   > m_maxClippedPercent->value();
        auto *keepItem = new QTableWidgetItem(clippedRejected ? tr("Clipped")
            : (accepted.contains(frameIndex) ? tr("Yes") : tr("No")));
        if (!accepted.contains(frameIndex)) {
            const QBrush muted(QColor(QStringLiteral("#7d838c")));
            frameItem->setForeground(muted);
            scoreItem->setForeground(muted);
            keepItem->setForeground(muted);
        }
        m_qualityTable->setItem(static_cast<int>(row), 0, frameItem);
        m_qualityTable->setItem(static_cast<int>(row), 1, scoreItem);
        m_qualityTable->setItem(static_cast<int>(row), 2, keepItem);
    }
    m_qualityGraph->setScores(std::move(displayedScores));
    if (keepCount == 0) {
        m_qualityLabel->setText(tr("All %1 frames exceed the overexposure limit.").arg(total));
        if (m_exportSummary) m_exportSummary->setText(tr("No frames currently qualify for export."));
        return;
    }
    const std::uint32_t cutoffIndex = eligibleFrames[keepCount - 1];
    const std::uint32_t bestIndex = m_qualityResult.rankedFrameIndices.front();
    m_qualityLabel->setText(
        tr("Keep %1 of %2 eligible frames; reject/discard %3. Cutoff score: %4. Best frame: %5 (%6).")
            .arg(keepCount).arg(eligibleFrames.size()).arg(total - keepCount)
            .arg(scoreByIndex[static_cast<qsizetype>(cutoffIndex)], 0, 'f', 1)
            .arg(bestIndex + 1U)
            .arg(scoreByIndex[static_cast<qsizetype>(bestIndex)], 0, 'f', 1));
    if (m_exportSummary) {
        m_exportSummary->setText(
            tr("Export selection: %1 of %2 frames, in %3. A timestamped subfolder will be created beside the source.")
                .arg(keepCount).arg(total)
                .arg(m_qualityOrder->currentData().toInt() == 1
                         ? tr("quality-score order") : tr("capture order")));
    }
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

    seeingforge::DetectionResult referenceDetection = m_detection;
    if (!referenceDetection.found()) {
        referenceDetection = seeingforge::ObjectDetector::detect(
            m_rawFrame, m_detectionThreshold->value(), m_cropMargin->currentData().toInt());
    }
    QSize outputSize(static_cast<int>(m_source->header().width),
                     static_cast<int>(m_source->header().height));
    const int margin = m_cropMargin->currentData().toInt();
    if (referenceDetection.found() && margin >= 0) {
        outputSize = QSize(
            std::min(static_cast<int>(m_source->header().width),
                     referenceDetection.objectRect.width()
                         + 2 * qRound(referenceDetection.objectRect.width() * margin / 100.0)),
            std::min(static_cast<int>(m_source->header().height),
                     referenceDetection.objectRect.height()
                         + 2 * qRound(referenceDetection.objectRect.height() * margin / 100.0)));
    }
    QSize requestedSize = m_outputSizeMode->currentData().toSize();
    if (requestedSize == QSize(-2, -2))
        requestedSize = QSize(m_customOutputWidth->value(), m_customOutputHeight->value());
    if (requestedSize.isValid()) {
        outputSize = QSize(std::min(requestedSize.width(),
                                    static_cast<int>(m_source->header().width)),
                           std::min(requestedSize.height(),
                                    static_cast<int>(m_source->header().height)));
    }
    outputSize = exportCropSize(m_rawFrame.size(), referenceDetection.objectRect,
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
    settings.applyCameraWhiteBalance = m_rawExportWhiteBalance->isEnabled()
        && m_rawExportWhiteBalance->currentData().toBool();
    settings.intensityGain = selectedExportIntensityGain();
    settings.detectionThreshold = m_detectionThreshold->value();
    settings.outputSize = outputSize;
    settings.rotationQuarterTurns = m_orientation->currentData().toInt();
    settings.flipHorizontal = m_flipHorizontal->isChecked();
    settings.flipVertical = m_flipVertical->isChecked();
    const auto source = m_source;

    m_exportButton->setEnabled(false);
    m_cancelExport->setEnabled(true);
    m_exportProgress->setRange(0, frameIndices.size());
    m_exportProgress->setValue(0);
    m_exportProgress->show();
    m_exportResultLabel->setText(tr("Exporting and centering frames…"));
    statusBar()->showMessage(tr("Exporting SER recording…"));
    m_exportCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelFlag = m_exportCancel;
    m_exportWatcher->setFuture(QtConcurrent::run(
        [source, outputPath, settings, cancelFlag](QPromise<seeingforge::ExportResult> &promise) {
            promise.setProgressRange(0, settings.frameIndices.size());
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
        statusBar()->showMessage(tr("Export canceled."), 5000);
        return;
    }
    if (!result.success) {
        m_exportResultLabel->setText(tr("Export failed: %1").arg(result.error));
        statusBar()->showMessage(tr("Export failed."), 5000);
        return;
    }
    m_exportResultLabel->setText(
        tr("Exported %1 centered frames to:\n%2%3")
            .arg(result.exportedFrames).arg(result.outputPath)
            .arg(result.timestampsPreserved ? tr("\nFrame timestamps preserved.") : QString()));
    statusBar()->showMessage(tr("SER export complete."), 5000);
}

void MainWindow::cancelExport()
{
    if (!m_exportWatcher->isRunning() || !m_exportCancel) return;
    m_exportCancel->store(true);
    m_cancelExport->setEnabled(false);
    m_exportResultLabel->setText(tr("Canceling after the current frame…"));
}

void MainWindow::previewModeChanged()
{
    if (m_previewMode->checkedId() == 2) {
        m_fitToWindow = false;
        m_zoomFactor = 1.0;
        m_fitAction->setChecked(false);
        if (m_zoomLabel) m_zoomLabel->setText(QStringLiteral("100%"));
        statusBar()->showMessage(tr("Raw CFA mosaic is shown at 1:1; its pixel pattern is expected."), 5000);
    }
    updateImage();
}
void MainWindow::loadSelectedFrame() { loadFrame(static_cast<std::uint32_t>(m_frameSlider->value())); }

void MainWindow::selectWorkflowStep(int row)
{
    if (row >= 0 && row < m_inspectorStack->count()) {
        m_inspectorStack->setCurrentIndex(row);
        if (row == 3) updateDetection();
        else updateImage();
        if (row == 5) updateExportIntensityDisplay();
    }
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
    if (m_zoomLabel) m_zoomLabel->setText(tr("Fit"));
    updateImage();
}

void MainWindow::showActualSize()
{
    m_fitToWindow = false;
    m_zoomFactor = 1.0;
    m_fitAction->setChecked(false);
    if (m_zoomLabel) m_zoomLabel->setText(QStringLiteral("100%"));
    updateImage();
}

void MainWindow::zoomIn()
{
    m_fitToWindow = false;
    m_fitAction->setChecked(false);
    m_zoomFactor = std::min(m_zoomFactor * 1.25, 8.0);
    if (m_zoomLabel) m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_zoomFactor * 100.0)));
    updateImage();
}

void MainWindow::zoomOut()
{
    m_fitToWindow = false;
    m_fitAction->setChecked(false);
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
    auto orientedPoint = [this, &image](double sourceX, double sourceY) {
        double x = sourceX / std::max(m_rawFrame.width(), 1);
        double y = sourceY / std::max(m_rawFrame.height(), 1);
        const int turns = m_orientation ? m_orientation->currentData().toInt() & 3 : 0;
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
    const int workflowStep = m_workflowList->currentRow();
    const bool stabilizationOverlay = workflowStep == 3 && m_showDetection
        && m_showDetection->isChecked();
    const bool persistentCropPreview = workflowStep >= 4;
    if (m_detection.found() && (stabilizationOverlay || persistentCropPreview)) {
        if (stabilizationOverlay) {
            image = image.copy();
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            QPen cropPen(QColor(QStringLiteral("#f0c64f")), 3);
            QPen objectPen(QColor(QStringLiteral("#52d273")), 3);
            painter.setPen(cropPen);
            painter.drawRect(orientedRect(m_detection.cropRect));
            painter.setPen(objectPen);
            painter.drawRect(orientedRect(m_detection.objectRect));
            painter.end();
        }
        // Show the same centered canvas as export, including black padding outside the source.
        const QRect canvas = orientedRect(m_detection.cropRect).toAlignedRect();
        image = image.copy(canvas);
    }
    if (workflowStep == 1 && m_showHotPixels
        && m_showHotPixels->isChecked() && !m_hotPixels.hotPixels.isEmpty()) {
        image = image.copy();
        QPainter painter(&image);
        painter.setPen(QPen(QColor(QStringLiteral("#ff4d5a")), 3));
        for (const QPoint point : m_hotPixels.hotPixels) {
            painter.drawPoint(orientedPoint(point.x() + 0.5, point.y() + 0.5));
        }
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
            setMetadataRow(row, row == 0 ? tr("Status") : QString(), row == 0 ? tr("No source loaded") : QString());
        return;
    }
    const auto &header = m_source->header();
    setMetadataRow(0, tr("Source"), m_source->displayName());
    setMetadataRow(1, tr("Dimensions"), tr("%1 × %2 px").arg(header.width).arg(header.height));
    setMetadataRow(2, tr("Frames"), QString::number(header.frameCount));
    setMetadataRow(3, tr("Pixel depth"), tr("%1-bit").arg(header.pixelDepth));
    setMetadataRow(4, tr("Colour format"), header.colorName());
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

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About SeeingForge"),
        tr("<h2>SeeingForge 0.2.1</h2><p>Open-source planetary imaging preparation.</p>"
           "<p>This build provides guided SER and DSLR RAW sequence processing.</p>"));
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_fitToWindow) updateImage();
}
