#include "mainwindow.h"
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

    m_nalList = new QTreeWidget();
    m_nalList->setColumnCount(9);
    m_nalList->setHeaderLabels({"#", "NAL Type", "NAL Name", "Size", "Offset", "Frame Type", "DTS", "PTS", ""});
    // 前 8 列保持固定宽度（NAL Name 在 buildNALList() 里按内容自适应）；
    // 最后一列是空白占位列，由它吸收窗口多出来的宽度，这样全屏时表格会铺满
    // 左侧面板，而各数据列不会被拉宽。
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
    connect(m_nalList, &QTreeWidget::currentItemChanged, this, &MainWindow::onNALSelected);

    m_statsPanel = new QTextBrowser();
    m_statsPanel->setOpenExternalLinks(false);

    m_details = new QTextBrowser();
    m_details->setOpenExternalLinks(false);
    m_details->setPlaceholderText("Select a NAL unit to view details...");

    m_videoLabel = new QLabel("(no video loaded)");
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumHeight(240);
    m_videoLabel->setStyleSheet("border: 1px solid #cccccc; background: #1a1a1a; color: #aaaaaa;");
    m_videoLabel->setScaledContents(false);
    m_videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_playerTimer = new QTimer(this);
    connect(m_playerTimer, &QTimer::timeout, this, &MainWindow::onPlayerTick);

    m_playBtn = new QPushButton("Play");
    m_playBtn->setEnabled(false);
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::onPlayPause);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);

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
    m_speedCombo->setCurrentIndex(3);   // 默认 1x
    m_speedCombo->setFixedWidth(80);
    connect(m_speedCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onSpeedChanged);

    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_seekSlider->setMinimumWidth(300);
    m_seekSlider->setRange(0, 0);
    connect(m_seekSlider, &QSlider::valueChanged, this, &MainWindow::onSeek);

    m_frameInfoLabel = new QLabel("Frame: - / -");
    m_frameInfoLabel->setMinimumWidth(80);

    QHBoxLayout *controlRow = new QHBoxLayout();
    controlRow->addWidget(m_playBtn);
    controlRow->addWidget(m_stopBtn);
    controlRow->addWidget(m_speedCombo);
    controlRow->addWidget(m_seekSlider, 1);
    controlRow->addWidget(m_frameInfoLabel);

    QWidget *rightPanel = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *playerGroup = new QGroupBox("Video Player");
    QVBoxLayout *playerLayout = new QVBoxLayout(playerGroup);
    playerLayout->addWidget(m_videoLabel, 1);
    playerLayout->addLayout(controlRow);
    rightLayout->addWidget(playerGroup);

    QGroupBox *detailGroup = new QGroupBox("NAL Details");
    QVBoxLayout *detailLayout = new QVBoxLayout(detailGroup);
    detailLayout->addWidget(m_details);
    rightLayout->addWidget(detailGroup, 1);

    QGroupBox *statsGroup = new QGroupBox("Stream Statistics");
    QVBoxLayout *statsLayout = new QVBoxLayout(statsGroup);
    statsLayout->addWidget(m_statsPanel);

    QSplitter *leftSplit = new QSplitter(Qt::Vertical);
    leftSplit->addWidget(m_nalList);
    leftSplit->addWidget(statsGroup);
    leftSplit->setStretchFactor(0, 3);
    leftSplit->setStretchFactor(1, 1);

    QSplitter *mainSplit = new QSplitter(Qt::Horizontal);
    mainSplit->addWidget(leftSplit);
    mainSplit->addWidget(rightPanel);
    mainSplit->setStretchFactor(0, 1);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setSizes({600, 800});

    setCentralWidget(mainSplit);

    m_fileInfoLabel = new QLabel("No file loaded");
    statusBar()->addWidget(m_fileInfoLabel, 1);
    loadRecentFiles();
    updateRecentMenu();
}

void MainWindow::openFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Video File", "",
        "Video Files (*.h264 *.h265 *.264 *.265 *.hevc);;All Files (*)");
    if (path.isEmpty()) return;
    if (loadFile(path)) addRecentFile(path);
}

bool MainWindow::loadFile(const QString &path) {
    std::string sp = path.toStdString();
    BitstreamParser detectParser(CodecType::H264);
    std::vector<uint8_t> probeBuf(4096);
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        f.read(reinterpret_cast<char *>(probeBuf.data()), 4096);
        f.close();
    }
    CodecType detected = BitstreamParser::detectCodec(probeBuf.data(), probeBuf.size());

    m_parser = BitstreamParser(detected);
    if (!m_parser.parseFile(sp)) {
        QMessageBox::critical(this, "Error", QString::fromStdString(m_parser.lastError()));
        return false;
    }

    m_currentPath = sp;
    m_analyzer.analyzeAll(m_parser.nalUnitsMut());

    buildNALList();
    updateStats();
    startPlayer();

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
    m_nalList->clear();
    const auto &nals = m_parser.nalUnits();
    for (size_t i = 0; i < nals.size(); i++) {
        const auto &nal = nals[i];
        QStringList items;
        items << QString::number(i);
        items << QString::number(nal.nalType);
        items << QString::fromStdString(nal.typeName());
        items << QString::number(nal.size);
        items << QString("0x%1").arg(nal.offset, 8, 16, QChar('0')).toUpper();
        items << (nal.frameType ? QString(QChar(nal.frameType)) : QString("-"));
        items << (nal.dtsIndex >= 0 ? QString::number(nal.dtsIndex) : QString("-"));
        items << (nal.frameIndex >= 0 ? QString::number(nal.frameIndex) : QString("-"));
        items << QString();   // 空白占位列

        QTreeWidgetItem *item = new QTreeWidgetItem(items);
        item->setData(0, Qt::UserRole, nal.frameIndex);

        static QFont monoFont("Menlo");
        monoFont.setStyleHint(QFont::Monospace);
        item->setFont(3, monoFont);
        item->setFont(4, monoFont);
        item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(5, Qt::AlignHCenter | Qt::AlignVCenter);
        item->setTextAlignment(6, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(7, Qt::AlignRight | Qt::AlignVCenter);

        QColor color;
        if (nal.codec == CodecType::H264) {
            switch (nal.nalType) {
                case 7: color = QColor(220, 200, 255); break;
                case 8: color = QColor(200, 255, 220); break;
                case 5: color = QColor(255, 200, 200); break;
                case 6: color = QColor(255, 255, 200); break;
                default: break;
            }
        } else {
            switch (nal.nalType) {
                case 32: color = QColor(255, 220, 255); break;
                case 33: color = QColor(220, 200, 255); break;
                case 34: color = QColor(200, 255, 220); break;
                case 19: case 20: case 21: color = QColor(255, 200, 200); break;
                case 39: case 40: color = QColor(255, 255, 200); break;
                default: break;
            }
        }
        if (color.isValid()) {
            for (int c = 0; c < item->columnCount(); c++) {
                item->setBackground(c, color);
            }
        }
        item->setData(0, Qt::UserRole + 1, color.isValid() ? color : QVariant());

        m_nalList->addTopLevelItem(item);
    }

    // NAL Name 列按内容自适应宽度（其余列保持固定宽度）
    m_nalList->resizeColumnToContents(2);
}

void MainWindow::updateStats() {
    const auto &s = m_analyzer.stats();
    QString codecStr = (s.codec == CodecType::H264) ? "H.264/AVC" : "H.265/HEVC";

    QString html;
    html += QString("<h3>%1</h3>").arg(codecStr);
    html += QString("<p>Total NAL units: <b>%1</b></p>").arg(s.totalNals);
    html += QString("<p>Total size: <b>%1 bytes</b> (%2 KB / %3 MB)</p>")
                .arg(s.totalBytes)
                .arg(s.totalBytes / 1024.0, 0, 'f', 2)
                .arg(s.totalBytes / 1024.0 / 1024.0, 0, 'f', 2);

    if (s.width > 0 && s.height > 0) {
        html += QString("<p>Resolution: <b>%1 x %2</b></p>").arg(s.width).arg(s.height);
    }
    if (!s.profile.empty()) {
        html += QString("<p>Profile: <b>%1</b></p>").arg(s.profile.c_str());
    }
    if (!s.level.empty()) {
        html += QString("<p>Level: <b>%1</b></p>").arg(s.level.c_str());
    }

    html += "<h4>NAL Type Distribution</h4>";
    html += "<table border='0'>";
    html += QString("<tr><td>IDR/Keyframe</td><td><b>%1</b></td></tr>").arg(s.idrCount);
    html += QString("<tr><td>SPS</td><td><b>%1</b></td></tr>").arg(s.spsCount);
    html += QString("<tr><td>PPS</td><td><b>%1</b></td></tr>").arg(s.ppsCount);
    html += QString("<tr><td>VPS</td><td><b>%1</b></td></tr>").arg(s.vpsCount);
    html += QString("<tr><td>SEI</td><td><b>%1</b></td></tr>").arg(s.seiCount);
    html += QString("<tr><td>AUD</td><td><b>%1</b></td></tr>").arg(s.audCount);
    html += QString("<tr><td>Frame slices</td><td><b>%1</b></td></tr>").arg(s.frameCount);
    html += "</table>";

    m_statsPanel->setHtml(html);
}

void MainWindow::startPlayer() {
    auto &stats = m_analyzer.stats();
    m_playerTimer->stop();
    m_isPlaying = false;

    m_playerDecoder.close();
    if (!m_playerDecoder.openFile(m_currentPath, stats.codec)) {
        m_videoLabel->setText(QString("Decoder error: %1").arg(m_playerDecoder.lastError().c_str()));
        m_playBtn->setEnabled(false);
        m_stopBtn->setEnabled(false);
        m_seekSlider->setEnabled(false);
        return;
    }

    auto &nals = m_parser.nalUnitsMut();
    buildNALList();

    m_frames.clear();
    DecodedFrame frame;
    while (m_playerDecoder.decodeNextFrame(frame)) {
        m_frames.push_back(std::move(frame));
    }
    m_totalFrames = m_frames.size();
    m_frameRate = m_playerDecoder.frameRate();
    m_playerDecoder.close();

    if (m_frames.empty()) {
        m_videoLabel->setText("No frames decoded");
        m_playBtn->setEnabled(false);
        m_stopBtn->setEnabled(false);
        m_seekSlider->setRange(0, 0);
        return;
    }

    m_seekSlider->setRange(0, qMax(0, m_totalFrames - 1));
    m_seekSlider->setValue(0);
    m_playBtn->setEnabled(true);
    m_stopBtn->setEnabled(true);

    m_videoSize = m_videoLabel->size();
    showFrame(m_frames[0].image, 0);
}

void MainWindow::showFrame(const QImage &image, int frameIdx) {
    if (image.isNull()) return;
    QSize target = m_videoSize.isValid() ? m_videoSize : m_videoLabel->size();
    QImage scaled = image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_videoLabel->setPixmap(QPixmap::fromImage(scaled));
    m_frameInfoLabel->setText(QString("Frame: %1 / %2").arg(frameIdx).arg(m_totalFrames));
    if (!m_seekSlider->isSliderDown()) {
        QSignalBlocker blocker(m_seekSlider);
        m_seekSlider->setValue(frameIdx);
    }
    highlightFrame(frameIdx);
}

void MainWindow::highlightFrame(int frameIdx) {
    QList<QTreeWidgetItem *> items = m_nalList->findItems("*", Qt::MatchWildcard);
    QTreeWidgetItem *firstMatch = nullptr;
    for (QTreeWidgetItem *item : items) {
        int fIdx = item->data(0, Qt::UserRole).toInt();
        bool match = (fIdx == frameIdx && frameIdx >= 0);
        QColor origColor = item->data(0, Qt::UserRole + 1).value<QColor>();
        QColor bg = match ? QColor(255, 165, 30) : origColor;
        for (int c = 0; c < item->columnCount(); c++) {
            if (bg.isValid())
                item->setBackground(c, bg);
            else
                item->setBackground(c, QBrush());
        }
        if (match && !firstMatch) firstMatch = item;
    }
    if (!firstMatch) return;

    // 只在目标行不可见时才滚动，并延迟到事件循环之后执行。
    // 若在 currentItemChanged（鼠标按下时）里立刻 scrollToItem，视图会在
    // 鼠标按下和释放之间滚动，释放时指针已落在另一行上，选中就被改到那一行，
    // 表现为“点第 40 帧却跳到别的帧”。行已可见时不滚动，点击就不会受影响。
    QRect itemRect = m_nalList->visualItemRect(firstMatch);
    if (!m_nalList->viewport()->rect().intersects(itemRect)) {
        QTimer::singleShot(0, this, [this, frameIdx]() {
            const QList<QTreeWidgetItem *> list =
                m_nalList->findItems("*", Qt::MatchWildcard);
            for (QTreeWidgetItem *it : list) {
                if (it->data(0, Qt::UserRole).toInt() == frameIdx) {
                    m_nalList->scrollToItem(it, QAbstractItemView::PositionAtCenter);
                    break;
                }
            }
        });
    }
}

void MainWindow::onPlayPause() {
    if (m_totalFrames == 0) return;

    if (!m_isPlaying) {
        m_playerTimer->start(playerIntervalMs());
        m_playBtn->setText("Pause");
        m_isPlaying = true;
    } else {
        m_playerTimer->stop();
        m_playBtn->setText("Play");
        m_isPlaying = false;
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
    if (m_isPlaying)                       // 播放中切换倍速立即生效
        m_playerTimer->start(playerIntervalMs());
}

void MainWindow::onStop() {
    m_playerTimer->stop();
    m_isPlaying = false;
    m_playBtn->setText("Play");
    m_seekSlider->setValue(0);
    if (!m_frames.empty()) showFrame(m_frames[0].image, 0);
}

void MainWindow::onSeek(int value) {
    if (value >= 0 && value < (int)m_frames.size()) {
        showFrame(m_frames[value].image, value);
    }
}

void MainWindow::onPlayerTick() {
    int next = m_seekSlider->value() + 1;
    if (next >= m_totalFrames) {
        m_playerTimer->stop();
        m_isPlaying = false;
        m_playBtn->setText("Play");
        next = 0;
    }
    if (next < (int)m_frames.size()) {
        showFrame(m_frames[next].image, next);
    }
}

void MainWindow::onNALSelected(QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    Q_UNUSED(previous);
    if (!current) return;

    int idx = m_nalList->indexOfTopLevelItem(current);
    if (idx < 0) return;

    const auto &nals = m_parser.nalUnits();
    if ((size_t)idx >= nals.size()) return;

    NALAnalysis analysis = m_analyzer.analyzeOne(nals[idx]);
    m_details->setPlainText(QString::fromStdString(analysis.details));

    int fIdx = nals[idx].frameIndex;
    if (fIdx >= 0 && fIdx < (int)m_frames.size()) {
        if (m_isPlaying) {
            m_playerTimer->stop();
            m_isPlaying = false;
            m_playBtn->setText("Play");
        }
        showFrame(m_frames[fIdx].image, fIdx);
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

void MainWindow::closeEvent(QCloseEvent *event) {
    saveRecentFiles();
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (!m_frames.empty() && m_videoLabel) {
        QSize newSize = m_videoLabel->size();
        if (newSize != m_videoSize && newSize.width() > 0 && newSize.height() > 0) {
            m_videoSize = newSize;
            int curIdx = m_seekSlider->value();
            if (curIdx >= 0 && curIdx < (int)m_frames.size()) {
                showFrame(m_frames[curIdx].image, curIdx);
            }
        }
    }
}