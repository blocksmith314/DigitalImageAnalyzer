#include "mainwindow.h"
#include "annexb.h"
#include "audio.h"
#include "nallistmodel.h"
#include <QMenuBar>
#include <QToolBar>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QImage>
#include <QListWidget>
#include <QLabel>
#include <QStatusBar>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QFont>
#include <QStatusBar>
#include <QGuiApplication>
#include <QApplication>
#include <QProgressDialog>
#include <QElapsedTimer>
#include <QShortcut>
#include <QStyle>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cstdio>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_parser(CodecType::H264) {
    setupUI();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUI() {
    setWindowTitle("Digital Image Analyzer - H.264/H.265 Bitstream Analyzer");
    resize(1400, 800);

    QMenu *fileMenu = menuBar()->addMenu("File");
    QAction *openAct = fileMenu->addAction("Open Bitstream...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openFile);
    fileMenu->addSeparator();
    m_recentMenu = fileMenu->addMenu("Recent Files");
    for (int i = 0; i < MAX_RECENT_FILES; ++i) {
        QAction *a = m_recentMenu->addAction("");
        a->setVisible(false);
        a->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_0 + i % 9 + 1)));
        connect(a, &QAction::triggered, this, &MainWindow::openRecentFile);
        m_recentActions.append(a);
    }
    m_recentMenu->addSeparator();
    QAction *clearAct = m_recentMenu->addAction("Clear Recent");
    connect(clearAct, &QAction::triggered, this, [this]() {
        m_recentFiles.clear();
        saveRecentFiles();
        updateRecentMenu();
    });

    fileMenu->addSeparator();
    QAction *quitAct = fileMenu->addAction("Quit");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    m_nalModel = new NalListModel(this);
    m_nalList = new QTreeView();
    m_nalList->setModel(m_nalModel);
    m_nalList->setRootIsDecorated(false);
    m_nalList->setUniformRowHeights(true);
    // First 8 columns keep fixed widths (NAL Name is auto-fitted in buildNALList());
    // the last empty column absorbs extra width so the table fills the panel.
    QHeaderView *hdr = m_nalList->header();
    hdr->setStretchLastSection(false);
    hdr->setSectionResizeMode(QHeaderView::Interactive);
    hdr->setSectionResizeMode(8, QHeaderView::Stretch);
    m_nalList->setColumnWidth(0, 60);
    m_nalList->setColumnWidth(1, 60);
    m_nalList->setColumnWidth(2, 120);
    m_nalList->setColumnWidth(3, 80);
    m_nalList->setColumnWidth(4, 110);
    m_nalList->setColumnWidth(5, 40);
    m_nalList->setColumnWidth(6, 50);
    m_nalList->setColumnWidth(7, 50);
    m_nalList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nalList->setAlternatingRowColors(true);
    connect(m_nalList->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &current, const QModelIndex &) {
                if (current.isValid()) onNALSelected(current.row());
            });

    m_statsPanel = new QTextBrowser();
    m_statsPanel->setOpenExternalLinks(false);
    m_statsPanel->setMinimumHeight(240);   // give Stream Statistics more room
    m_statsPanel->document()->setDocumentMargin(2);    // compact layout to fit all rows
    m_statsPanel->document()->setDefaultStyleSheet(
        "table { border-collapse: collapse; }"
        "td { padding: 0 8px 0 0; }");

    m_details = new QTextBrowser();
    m_details->setOpenExternalLinks(false);
    m_details->setPlaceholderText("Select a NAL unit to view details...");

    m_videoLabel = new QLabel("(no video loaded)");
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumHeight(240);
    m_videoLabel->setStyleSheet("border: 1px solid #cccccc; background: #1a1a1a; color: #aaaaaa;");
    m_videoLabel->setScaledContents(false);
    m_videoLabel->installEventFilter(this);
    m_videoLabel->setToolTip("Double-click for fullscreen");
    // Ignore the pixmap size hint: the label must not force the player panel
    // wider than the NAL Details panel. The image is scaled to fit instead.
    m_videoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_videoLabel->setMinimumSize(1, 240);

    m_playerTimer = new QTimer(this);
    connect(m_playerTimer, &QTimer::timeout, this, &MainWindow::onPlayerTick);
    m_audio = new AudioPlayer(this);

    auto *prevShortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
    connect(prevShortcut, &QShortcut::activated, this, &MainWindow::onStepBackward);
    auto *nextShortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
    connect(nextShortcut, &QShortcut::activated, this, &MainWindow::onStepForward);
    auto *playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playShortcut, &QShortcut::activated, this, &MainWindow::onPlayPause);

    m_playBtn = new QPushButton("Play");
    m_playBtn->setFixedWidth(64);
    m_playBtn->setEnabled(false);
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::onPlayPause);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setFixedWidth(64);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);

    m_prevFrameBtn = new QPushButton("\u25c0");
    m_prevFrameBtn->setEnabled(false);
    m_prevFrameBtn->setFixedWidth(32);
    m_prevFrameBtn->setToolTip("Step backward one frame");
    connect(m_prevFrameBtn, &QPushButton::clicked, this, &MainWindow::onStepBackward);

    m_nextFrameBtn = new QPushButton("\u25b6");
    m_nextFrameBtn->setEnabled(false);
    m_nextFrameBtn->setFixedWidth(32);
    m_nextFrameBtn->setToolTip("Step forward one frame");
    connect(m_nextFrameBtn, &QPushButton::clicked, this, &MainWindow::onStepForward);

    m_speedCombo = new QComboBox();
    m_speedCombo->setToolTip("Playback speed");
    {
        struct SpeedItem { const char *label; double value; };
        static const SpeedItem speeds[] = {
            {"0.25x", 0.25}, {"0.5x", 0.5}, {"0.75x", 0.75},
            {"1x", 1.0}, {"1.5x", 1.5}, {"2x", 2.0}, {"4x", 4.0}};
        for (const auto &s : speeds)
            m_speedCombo->addItem(QString::fromUtf8(s.label), s.value);
    }
    m_speedCombo->setCurrentIndex(3);   // default: 1x
    m_speedCombo->setFixedWidth(80);
    connect(m_speedCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onSpeedChanged);

    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_seekSlider->setMinimumWidth(60);
    m_seekSlider->setRange(0, 0);
    connect(m_seekSlider, &QSlider::valueChanged, this, &MainWindow::onSeek);

    m_frameInfoLabel = new QLabel("Frame: - / -");
    m_frameInfoLabel->setMinimumWidth(80);

    QHBoxLayout *controlRow = new QHBoxLayout();
    controlRow->addWidget(m_playBtn);
    controlRow->addWidget(m_stopBtn);
    m_fullscreenBtn = new QPushButton();
    m_fullscreenBtn->setFixedWidth(32);
    m_fullscreenBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_fullscreenBtn->setToolTip("Toggle fullscreen (double-click video, Esc to exit)");
    connect(m_fullscreenBtn, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);

    controlRow->addWidget(m_prevFrameBtn);
    controlRow->addWidget(m_nextFrameBtn);
    controlRow->addWidget(m_fullscreenBtn);

    auto *stepLabel = new QLabel("Step:");
    stepLabel->setToolTip("Frames skipped by the arrow keys");
    m_stepSpin = new QSpinBox();
    m_stepSpin->setRange(1, 1000);
    m_stepSpin->setValue(1);
    m_stepSpin->setFixedWidth(56);
    controlRow->addWidget(stepLabel);
    controlRow->addWidget(m_stepSpin);
    controlRow->addWidget(m_speedCombo);
    controlRow->addWidget(m_seekSlider, 1);
    m_frameInfoLabel->setMinimumWidth(0);
    controlRow->addWidget(m_frameInfoLabel);

    QGroupBox *playerGroup = new QGroupBox("Video Player");
    QVBoxLayout *playerLayout = new QVBoxLayout(playerGroup);
    playerLayout->addWidget(m_videoLabel, 1);
    playerLayout->addLayout(controlRow);

    QGroupBox *detailGroup = new QGroupBox("NAL Details");
    QVBoxLayout *detailLayout = new QVBoxLayout(detailGroup);
    detailLayout->addWidget(m_details);

    QGroupBox *statsGroup = new QGroupBox("Stream Statistics");
    QVBoxLayout *statsLayout = new QVBoxLayout(statsGroup);
    statsLayout->addWidget(m_statsPanel);

    // Top row: NAL list | Video Player
    QSplitter *topSplit = new QSplitter(Qt::Horizontal);
    topSplit->addWidget(m_nalList);
    topSplit->addWidget(playerGroup);
    topSplit->setStretchFactor(0, 1);
    topSplit->setStretchFactor(1, 1);
    topSplit->setSizes({600, 800});

    // Bottom row: Stream Statistics | NAL Details (same row => equal height)
    QSplitter *bottomSplit = new QSplitter(Qt::Horizontal);
    bottomSplit->addWidget(statsGroup);
    bottomSplit->addWidget(detailGroup);
    bottomSplit->setStretchFactor(0, 1);
    bottomSplit->setStretchFactor(1, 1);
    bottomSplit->setSizes({600, 800});

    // Vertical split: top row / bottom row
    QSplitter *mainSplit = new QSplitter(Qt::Vertical);
    mainSplit->addWidget(topSplit);
    mainSplit->addWidget(bottomSplit);
    mainSplit->setStretchFactor(0, 6);
    mainSplit->setStretchFactor(1, 5);
    mainSplit->setSizes({410, 340});

    // Keep the horizontal split position of both rows in sync
    connect(topSplit, &QSplitter::splitterMoved, this,
            [this](int, int) { syncSplitterWidths(true); });
    connect(bottomSplit, &QSplitter::splitterMoved, this,
            [this](int, int) { syncSplitterWidths(false); });

    setCentralWidget(mainSplit);
    m_topSplit = topSplit;
    m_bottomSplit = bottomSplit;
    // Align the two rows after the first layout pass.
    QTimer::singleShot(0, this, [this]() { applySplitFraction(); });

    m_timeInfoLabel = new QLabel();
    statusBar()->addWidget(m_timeInfoLabel);
    m_fileInfoLabel = new QLabel("No file loaded");
    statusBar()->addWidget(m_fileInfoLabel, 1);
    loadRecentFiles();
    updateRecentMenu();
}

void MainWindow::openFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Video File", "",
        "Video Files (*.h264 *.h265 *.264 *.265 *.hevc *.mp4 *.mov *.m4v *.ts *.mkv);;All Files (*)");
    if (path.isEmpty()) return;
    if (loadFile(path)) addRecentFile(path);
}

bool MainWindow::loadFile(const QString &path) {
    std::string sp = path.toStdString();

    QProgressDialog dlg(QString("Loading %1...").arg(QFileInfo(path).fileName()),
                        "Cancel", 0, 100, this);
    dlg.setWindowTitle("Loading");
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(200);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);

    auto progress = [&dlg](double fraction) -> bool {
        if (dlg.wasCanceled()) return false;
        if (fraction < 0.0) {
            dlg.setRange(0, 0);
        } else {
            dlg.setRange(0, 100);
            dlg.setValue((int)qBound(0.0, fraction * 100.0, 100.0));
        }
        QApplication::processEvents();
        return !dlg.wasCanceled();
    };

    // Raw Annex B streams are parsed directly; containers (MP4/MOV/MKV/TS...)
    // are read through length-prefixed NAL units (AVCC/hvcC) from the original
    // file, so no temporary Annex B copy is written.
    QElapsedTimer timer;
    timer.start();
    CodecType detected = CodecType::H264;
    bool rawAnnexB = false;
    std::string probeError;
    if (!probeVideoFile(sp, detected, rawAnnexB, probeError)) {
        dlg.close();
        QMessageBox::critical(this, "Error", QString::fromStdString(probeError));
        return false;
    }

    const qint64 probeMs = timer.restart();
    m_parser = BitstreamParser(detected);
    dlg.setLabelText(QString("Scanning %1...").arg(QFileInfo(path).fileName()));
    bool parsed = false;
    if (rawAnnexB) {
        parsed = m_parser.parseFile(sp, progress);
    } else {
        // Plain MP4: read the moov sample table and touch only NAL headers.
        parsed = m_parser.parseMp4Indexed(sp, detected, progress);
        if (!parsed && m_parser.lastError() != "Cancelled") {
            // Fragmented/unsupported layout: fall back to a full demux.
            parsed = m_parser.parseAvcc(sp, detected, progress);
        }
    }
    if (!parsed) {
        dlg.close();
        QMessageBox::critical(this, "Error",
                              QString::fromStdString(m_parser.lastError()));
        return false;
    }
    const qint64 demuxMs = timer.restart();

    m_currentPath = sp;
    dlg.setLabelText(QString("Analyzing %1...").arg(QFileInfo(path).fileName()));
    if (!m_analyzer.analyzeAll(m_parser, progress)) {
        dlg.close();
        QMessageBox::critical(this, "Error", "Loading cancelled");
        return false;
    }
    const qint64 analyzeMs = timer.restart();
    dlg.close();

    // The NAL metadata is enough for the UI; drop resident file pages so a
    // multi-GB stream does not stay pinned in memory.
    m_parser.releaseMappedPages();

    buildNALList();
    updateStats();
    startPlayer();
    if (m_audio) m_audio->open(m_currentPath);

    const qint64 totalMs = probeMs + demuxMs + analyzeMs + timer.restart();
    if (m_timeInfoLabel) {
        m_timeInfoLabel->setText(
            QString("Loaded in %1 s  (probe %2 s, read %3 s, analyze %4 s)  ")
                .arg(totalMs / 1000.0, 0, 'f', 1)
                .arg(probeMs / 1000.0, 0, 'f', 1)
                .arg(demuxMs / 1000.0, 0, 'f', 1)
                .arg(analyzeMs / 1000.0, 0, 'f', 1));
    }

    QFileInfo fi(path);
    auto &stats = m_analyzer.stats();
    QString codecStr = (stats.codec == CodecType::H264) ? "H.264/AVC" : "H.265/HEVC";
    m_fileInfoLabel->setText(
        QString("%1 | %2 | %3 NAL units | %4 bytes | %5x%6")
            .arg(fi.fileName())
            .arg(codecStr)
            .arg(stats.totalNals)
            .arg(stats.totalBytes)
            .arg(stats.width)
            .arg(stats.height));

    setWindowTitle(QString("Digital Image Analyzer - %1").arg(fi.fileName()));
    return true;
}

void MainWindow::buildNALList() {
    m_nalModel->setNalUnits(&m_parser.nalUnits());
    m_nalList->resizeColumnToContents(2);
}

void MainWindow::updateStats() {
    const auto &s = m_analyzer.stats();
    const QString codecStr = (s.codec == CodecType::H264) ? "H.264/AVC" : "H.265/HEVC";

    const double kb = s.totalBytes / 1024.0;
    const double mb = kb / 1024.0;
    const int frames = (int)s.frameCount;
    const double fps = m_frameRate > 0.0 ? m_frameRate : 0.0;
    const double duration = (fps > 0.0 && frames > 0) ? frames / fps : 0.0;
    const double mbps = duration > 0.0 ? (s.totalBytes * 8.0 / duration / 1e6) : 0.0;
    const double avgNal = s.totalNals ? (double)s.totalBytes / s.totalNals : 0.0;
    const double avgGop = s.idrCount ? (double)frames / s.idrCount : 0.0;
    const double slicesPerFrame = frames ? (double)s.sliceCount / frames : 0.0;

    QString html;
    html += QString("<p style='margin:0 0 3px 0'><b><big>%1</big></b></p>").arg(codecStr);
    html += "<table border='0' cellspacing='0' cellpadding='0'>";
    auto row = [&html](const QString &k, const QString &v) {
        html += QString("<tr><td>%1&nbsp;&nbsp;</td><td><b>%2</b></td></tr>").arg(k, v);
    };

    row("NAL units", QString::number(s.totalNals));
    row("Total size", QString("%1 KB (%2 MB)").arg(kb, 0, 'f', 2).arg(mb, 0, 'f', 2));
    QStringList formatBits;
    if (s.width > 0 && s.height > 0)
        formatBits << QString("%1 x %2").arg(s.width).arg(s.height);
    if (!s.profile.empty())
        formatBits << QString::fromStdString(s.profile);
    if (!s.level.empty())
        formatBits << QString("Level %1").arg(QString::fromStdString(s.level));
    if (!formatBits.isEmpty())
        row("Format", formatBits.join("  \u00b7  "));
    row("Frames", QString("%1  (I %2 / P %3 / B %4)")
            .arg(frames).arg(s.iFrameCount).arg(s.pFrameCount).arg(s.bFrameCount));
    if (fps > 0.0)
        row("Frame rate", QString("%1 fps").arg(fps, 0, 'f', 2));
    if (duration > 0.0) {
        row("Duration", QString("%1 s").arg(duration, 0, 'f', 2));
        row("Bitrate", QString("%1 Mbps").arg(mbps, 0, 'f', 3));
    }
    row("Slices", QString("%1  (%2 / frame)").arg(s.sliceCount).arg(slicesPerFrame, 0, 'f', 2));
    if (s.idrCount > 0)
        row("GOP", QString("%1 IDR, avg %2 frames").arg(s.idrCount).arg(avgGop, 0, 'f', 2));
    if (s.totalNals > 0)
        row("NAL size", QString("avg %1 B / max %2 B / min %3 B")
                .arg((int)avgNal).arg(s.maxNalSize).arg(s.minNalSize));
    html += "</table>";

    html += "<p style='margin:6px 0 3px 0'><b>NAL Type Distribution</b></p>";
    html += "<table border='0' cellspacing='0' cellpadding='0'>";
    row("IDR/Keyframe", QString::number(s.idrCount));
    row("SPS", QString::number(s.spsCount));
    row("PPS", QString::number(s.ppsCount));
    if (s.vpsCount > 0) row("VPS", QString::number(s.vpsCount));
    row("SEI / AUD", QString("%1 / %2").arg(s.seiCount).arg(s.audCount));
    row("Slice NALs", QString::number(s.sliceCount));
    html += "</table>";

    m_statsPanel->setHtml(html);
}

void MainWindow::startPlayer() {
    auto &stats = m_analyzer.stats();
    m_playerTimer->stop();
    m_isPlaying = false;
    m_playBtn->setText("Play");

    m_playerDecoder.close();
    if (!m_playerDecoder.openFile(m_currentPath, stats.codec)) {
        m_videoLabel->setText(QString("Decoder error: %1").arg(m_playerDecoder.lastError().c_str()));
        m_playBtn->setEnabled(false);
        m_stopBtn->setEnabled(false);
        m_prevFrameBtn->setEnabled(false);
        m_nextFrameBtn->setEnabled(false);
        m_seekSlider->setEnabled(false);
        return;
    }

    // No upfront decode: frames are decoded on demand while playing/seeking.
    m_totalFrames = (int)stats.frameCount;
    m_frameRate = m_playerDecoder.frameRate();
    updateStats();   // refresh stats that depend on decoded frames / frame rate

    if (m_totalFrames <= 0) {
        m_videoLabel->setText("No frames found");
        m_playBtn->setEnabled(false);
        m_stopBtn->setEnabled(false);
        m_prevFrameBtn->setEnabled(false);
        m_nextFrameBtn->setEnabled(false);
        m_seekSlider->setRange(0, 0);
        return;
    }

    m_seekSlider->setRange(0, qMax(0, m_totalFrames - 1));
    m_seekSlider->setValue(0);
    m_playBtn->setEnabled(true);
    m_stopBtn->setEnabled(true);
    m_prevFrameBtn->setEnabled(true);
    m_nextFrameBtn->setEnabled(true);
    m_seekSlider->setEnabled(true);

    m_videoSize = m_videoLabel->size();
    m_currentFrameIndex = -1;
    showFrameAt(0);
}

void MainWindow::showFrame(const QImage &image, int frameIdx) {
    if (image.isNull()) return;
    m_currentImage = image;
    QSize target = m_videoSize.isValid() ? m_videoSize : m_videoLabel->size();
    QImage scaled = image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_videoLabel->setPixmap(QPixmap::fromImage(scaled));
    m_frameInfoLabel->setText(QString("Frame: %1 / %2").arg(frameIdx).arg(m_totalFrames));
    if (!m_seekSlider->isSliderDown()) {
        QSignalBlocker blocker(m_seekSlider);
        m_seekSlider->setValue(frameIdx);
    }
    highlightFrame(frameIdx);
    if (m_fullscreen) updateFullscreenFrame();
}

void MainWindow::showFrameAt(int idx) {
    if (m_totalFrames <= 0) return;
    idx = qBound(0, idx, m_totalFrames - 1);
    if (idx == m_currentFrameIndex) return;

    // Decode sequentially when moving forward by one; otherwise seek.
    const bool jump = (idx < m_currentFrameIndex || idx > m_currentFrameIndex + 1);
    if (jump) {
        if (!m_playerDecoder.seekToFrame(idx)) return;
    }

    DecodedFrame frame;
    if (!m_playerDecoder.decodeNextFrame(frame)) {
        // End of decodable frames (container may have fewer than the NAL count).
        m_playerTimer->stop();
        m_isPlaying = false;
        m_playBtn->setText("Play");
        return;
    }
    m_currentFrameIndex = idx;
    m_currentPts = frame.ptsSec;
    showFrame(frame.image, idx);

    if (jump) {
        m_hasPending = false;
        m_clockStartPts = m_currentPts;
        m_playClock.restart();
        if (m_audio && m_audio->hasAudio()) {
            const double seconds = m_playerDecoder.hasTimestamps() && m_frameRate > 0.0
                                       ? m_currentPts
                                       : (m_frameRate > 0.0 ? (double)idx / m_frameRate : 0.0);
            m_audio->seekTo(seconds);
        }
    }
}

void MainWindow::highlightFrame(int frameIdx) {
    m_nalModel->setHighlightedFrame(frameIdx);
    if (frameIdx < 0) return;

    const int row = m_nalModel->firstRowForFrame(frameIdx);
    if (row < 0) return;
    const QModelIndex idx = m_nalModel->index(row, 0);
    if (!m_nalList->viewport()->rect().intersects(m_nalList->visualRect(idx))) {
        QTimer::singleShot(0, this, [this, idx]() {
            m_nalList->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        });
    }
}

void MainWindow::onPlayPause() {
    if (m_totalFrames == 0) return;

    if (!m_isPlaying) {
        if (m_currentFrameIndex >= m_totalFrames - 1) showFrameAt(0);
        m_hasPending = false;
        m_clockStartPts = m_currentPts;
        m_playClock.start();
        m_playerTimer->start(m_playerDecoder.hasTimestamps() ? 5 : playerIntervalMs());
        m_playBtn->setText("Pause");
        m_isPlaying = true;
        if (m_audio) m_audio->play();
    } else {
        m_playerTimer->stop();
        m_playBtn->setText("Play");
        m_isPlaying = false;
        if (m_audio) m_audio->pause();
    }
}

int MainWindow::playerIntervalMs() const {
    const double fps = m_frameRate > 0.0 ? m_frameRate : 30.0;
    const double speed = m_playbackSpeed > 0.0 ? m_playbackSpeed : 1.0;
    const double ms = 1000.0 / (fps * speed);
    return qMax(1, (int)(ms + 0.5));
}

void MainWindow::onSpeedChanged(int index) {
    if (!m_speedCombo) return;
    double speed = m_speedCombo->itemData(index).toDouble();
    if (speed <= 0.0) speed = 1.0;
    m_playbackSpeed = speed;
    if (m_isPlaying) {
        m_clockStartPts = m_currentPts;
        m_playClock.restart();
        m_playerTimer->start(m_playerDecoder.hasTimestamps() ? 5 : playerIntervalMs());
    }
}

void MainWindow::onStop() {
    m_playerTimer->stop();
    m_isPlaying = false;
    m_playBtn->setText("Play");
    if (m_audio) m_audio->stop();
    showFrameAt(0);
}

void MainWindow::onStepForward() {
    if (m_totalFrames <= 0) return;
    m_playerTimer->stop();
    m_isPlaying = false;
    m_playBtn->setText("Play");
    if (m_audio) m_audio->pause();
    const int step = m_stepSpin ? m_stepSpin->value() : 1;
    showFrameAt(m_currentFrameIndex + step);
}

void MainWindow::onStepBackward() {
    if (m_totalFrames <= 0) return;
    m_playerTimer->stop();
    m_isPlaying = false;
    m_playBtn->setText("Play");
    if (m_audio) m_audio->pause();
    const int step = m_stepSpin ? m_stepSpin->value() : 1;
    showFrameAt(m_currentFrameIndex - step);
}

void MainWindow::onSeek(int value) {
    if (m_totalFrames <= 0) return;
    showFrameAt(value);
}

void MainWindow::onPlayerTick() {
    if (m_totalFrames <= 0) return;

    // Raw streams have no container timestamps: fall back to the fixed timer.
    if (!m_playerDecoder.hasTimestamps()) {
        if (m_currentFrameIndex + 1 >= m_totalFrames) {
            stopPlayback();
            return;
        }
        showFrameAt(m_currentFrameIndex + 1);
        return;
    }

    if (!m_hasPending) {
        if (!m_playerDecoder.decodeNextFrame(m_pendingFrame)) {
            stopPlayback();
            return;
        }
        m_hasPending = true;
    }

    const double now = m_clockStartPts +
                       (m_playClock.elapsed() / 1000.0) * m_playbackSpeed;
    if (m_pendingFrame.ptsSec <= now) {
        const DecodedFrame frame = m_pendingFrame;
        m_hasPending = false;
        m_currentFrameIndex++;
        m_currentPts = frame.ptsSec;
        showFrame(frame.image, m_currentFrameIndex);
        if (m_currentFrameIndex >= m_totalFrames - 1) {
            stopPlayback();
        }
    }
}

void MainWindow::stopPlayback() {
    m_playerTimer->stop();
    m_isPlaying = false;
    m_playBtn->setText("Play");
    if (m_audio) m_audio->pause();
}

void MainWindow::onNALSelected(int idx) {
    if (idx < 0) return;

    const auto &nals = m_parser.nalUnits();
    if ((size_t)idx >= nals.size()) return;

    NALAnalysis analysis = m_analyzer.analyzeOne(nals[(size_t)idx], m_parser.nalData(nals[(size_t)idx]));
    m_details->setPlainText(QString::fromStdString(analysis.details));

    const int fIdx = nals[(size_t)idx].frameIndex;
    if (fIdx >= 0 && fIdx < m_totalFrames) {
        if (m_isPlaying) stopPlayback();
        showFrameAt(fIdx);
    }
}

void MainWindow::openRecentFile() {
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) return;
    int idx = action->data().toInt();
    if (idx < 0 || idx >= m_recentFiles.size()) return;
    QString path = m_recentFiles.at(idx);
    if (!QFile::exists(path)) {
        QMessageBox::warning(this, "File Missing",
                             QString("File no longer exists:\n%1").arg(path));
        m_recentFiles.removeAt(idx);
        saveRecentFiles();
        updateRecentMenu();
        return;
    }
    if (loadFile(path)) addRecentFile(path);
}

void MainWindow::addRecentFile(const QString &path) {
    QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty()) canonical = path;
    m_recentFiles.removeAll(canonical);
    m_recentFiles.prepend(canonical);
    while (m_recentFiles.size() > MAX_RECENT_FILES)
        m_recentFiles.removeLast();
    saveRecentFiles();
    updateRecentMenu();
}

void MainWindow::updateRecentMenu() {
    for (int i = 0; i < m_recentActions.size(); ++i) {
        QAction *a = m_recentActions.at(i);
        if (i < m_recentFiles.size()) {
            QString path = m_recentFiles.at(i);
            QFileInfo fi(path);
            QString label = QString("&%1  %2").arg(i + 1).arg(fi.filePath());
            a->setText(label);
            a->setData(i);
            a->setVisible(true);
            a->setEnabled(QFile::exists(path));
        } else {
            a->setVisible(false);
        }
    }
    m_recentMenu->setEnabled(!m_recentFiles.isEmpty());
}

void MainWindow::loadRecentFiles() {
    QSettings settings;
    m_recentFiles = settings.value("recentFiles").toStringList();
}

void MainWindow::saveRecentFiles() {
    QSettings settings;
    settings.setValue("recentFiles", m_recentFiles);
}

void MainWindow::syncSplitterWidths(bool fromTop) {
    if (!m_topSplit || !m_bottomSplit || m_syncingSplitters) return;

    // Remember the split position the user chose and apply it to both rows.
    QSplitter *source = fromTop ? m_topSplit : m_bottomSplit;
    const QList<int> sizes = source->sizes();
    if (sizes.size() < 2) return;
    const int total = sizes[0] + sizes[1] + m_topSplit->handleWidth();
    // Ignore degenerate layouts reported before the window is fully sized.
    if (total > 400) m_splitFraction = (double)sizes[0] / (double)total;

    applySplitFraction();
}

void MainWindow::applySplitFraction() {
    if (!m_topSplit || !m_bottomSplit) return;

    const int width = m_topSplit->width() - m_topSplit->handleWidth();
    if (width <= 0) return;

    const int left = qBound(0, (int)(m_splitFraction * width), width);
    const QList<int> sizes{left, width - left};

    m_syncingSplitters = true;
    m_topSplit->setSizes(sizes);
    m_bottomSplit->setSizes(sizes);

    // If a row could not honour the split because of its minimum widths,
    // align both rows to the wider right-hand panel that resulted.
    const QList<int> top = m_topSplit->sizes();
    const QList<int> bottom = m_bottomSplit->sizes();
    if (top.size() >= 2 && bottom.size() >= 2) {
        const int total = top[0] + top[1] + m_topSplit->handleWidth();
        const int right = qMax(top[1], bottom[1]);
        const int alignedLeft = qMax(0, total - right - m_topSplit->handleWidth());
        const QList<int> aligned{alignedLeft, total - alignedLeft - m_topSplit->handleWidth()};
        m_topSplit->setSizes(aligned);
        m_bottomSplit->setSizes(aligned);
    }
    m_syncingSplitters = false;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonDblClick &&
        (watched == m_videoLabel || watched == m_fullscreenLabel)) {
        toggleFullscreen();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::toggleFullscreen() {
    if (m_fullscreen) exitFullscreen();
    else enterFullscreen();
}

void MainWindow::enterFullscreen() {
    if (m_fullscreen || m_currentImage.isNull()) return;

    if (!m_fullscreenWindow) {
        m_fullscreenWindow = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
        m_fullscreenWindow->setStyleSheet("background:black");
        m_fullscreenLabel = new QLabel(m_fullscreenWindow);
        m_fullscreenLabel->setAlignment(Qt::AlignCenter);
        m_fullscreenLabel->setStyleSheet("background:black");
        m_fullscreenLabel->installEventFilter(this);
        auto *layout = new QVBoxLayout(m_fullscreenWindow);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_fullscreenLabel);
        auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), m_fullscreenWindow);
        QObject::connect(escape, &QShortcut::activated, this, &MainWindow::exitFullscreen);
        auto *prev = new QShortcut(QKeySequence(Qt::Key_Left), m_fullscreenWindow);
        QObject::connect(prev, &QShortcut::activated, this, &MainWindow::onStepBackward);
        auto *next = new QShortcut(QKeySequence(Qt::Key_Right), m_fullscreenWindow);
        QObject::connect(next, &QShortcut::activated, this, &MainWindow::onStepForward);
        auto *space = new QShortcut(QKeySequence(Qt::Key_Space), m_fullscreenWindow);
        QObject::connect(space, &QShortcut::activated, this, &MainWindow::onPlayPause);
    }

    m_fullscreenWindow->showFullScreen();
    m_fullscreen = true;
    updateFullscreenFrame();
    hide();   // hide window frame, control bar and progress slider
}

void MainWindow::exitFullscreen() {
    if (!m_fullscreen) return;
    m_fullscreen = false;
    if (m_fullscreenWindow) m_fullscreenWindow->hide();
    show();
    raise();
    activateWindow();
}

void MainWindow::updateFullscreenFrame() {
    if (!m_fullscreen || !m_fullscreenLabel || m_currentImage.isNull()) return;
    QSize target = m_fullscreenWindow ? m_fullscreenWindow->size() : QSize();
    if (target.isEmpty()) target = m_fullscreenLabel->size();
    m_fullscreenLabel->setPixmap(QPixmap::fromImage(
        m_currentImage.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_fullscreenWindow) m_fullscreenWindow->close();
    if (m_audio) m_audio->close();
    saveRecentFiles();
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);

    // Keep the right-hand panels (Video Player / NAL Details) equally wide.
    applySplitFraction();
    if (!m_currentImage.isNull() && m_videoLabel) {
        const QSize newSize = m_videoLabel->size();
        if (newSize != m_videoSize && newSize.width() > 0 && newSize.height() > 0) {
            m_videoSize = newSize;
            showFrame(m_currentImage, m_currentFrameIndex);
        }
    }
}
