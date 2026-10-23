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
    QAction *quitAct = fileMenu->addAction("Quit");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    m_nalList = new QTreeWidget();
    m_nalList->setColumnCount(5);
    m_nalList->setHeaderLabels({"#", "Type", "Size", "Offset", "Name"});
    m_nalList->header()->setStretchLastSection(true);
    m_nalList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nalList->setAlternatingRowColors(true);
    connect(m_nalList, &QTreeWidget::currentItemChanged, this, &MainWindow::onNALSelected);

    m_statsPanel = new QTextBrowser();
    m_statsPanel->setOpenExternalLinks(false);

    m_details = new QTextBrowser();
    m_details->setOpenExternalLinks(false);
    m_details->setPlaceholderText("Select a NAL unit to view details...");

    m_thumbnailLabel = new QLabel("(no frame decoded)");
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    m_thumbnailLabel->setMinimumHeight(200);
    m_thumbnailLabel->setStyleSheet("border: 1px solid #cccccc; background: #f8f8f8;");
    m_thumbnailLabel->setScaledContents(false);

    QWidget *rightPanel = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *thumbGroup = new QGroupBox("Keyframe Thumbnails");
    QVBoxLayout *thumbLayout = new QVBoxLayout(thumbGroup);
    thumbLayout->addWidget(m_thumbnailLabel);
    rightLayout->addWidget(thumbGroup);

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
}

void MainWindow::openFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Bitstream File", "",
        "Bitstream Files (*.h264 *.h265 *.264 *.265 *.hevc);;All Files (*)");
    if (path.isEmpty()) return;

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
        return;
    }

    m_currentPath = sp;
    m_analyzer.analyzeAll(m_parser.nalUnits());

    buildNALList();
    updateStats();
    decodeThumbnails();

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
}

void MainWindow::buildNALList() {
    m_nalList->clear();
    const auto &nals = m_parser.nalUnits();
    for (size_t i = 0; i < nals.size(); i++) {
        const auto &nal = nals[i];
        QStringList items;
        items << QString::number(i);
        items << QString::number(nal.nalType);
        items << QString::number(nal.size);
        items << QString("0x%1").arg(nal.offset, 0, 16);
        items << QString::fromStdString(nal.typeName());

        QTreeWidgetItem *item = new QTreeWidgetItem(items);

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

        m_nalList->addTopLevelItem(item);
    }

    for (int c = 0; c < m_nalList->columnCount(); c++) {
        m_nalList->resizeColumnToContents(c);
    }
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

void MainWindow::decodeThumbnails() {
    auto &stats = m_analyzer.stats();
    m_decoder.close();

    if (!m_decoder.openFile(m_currentPath, stats.codec)) {
        m_thumbnailLabel->setText(QString("Decoder error: %1").arg(m_decoder.lastError().c_str()));
        return;
    }

    std::vector<DecodedFrame> frames;
    m_decoder.decodeKeyframes(frames, 5);

    if (frames.empty()) {
        m_thumbnailLabel->setText("No frames decoded");
        return;
    }

    QString label;
    for (size_t i = 0; i < frames.size(); i++) {
        label += QString("Frame %1 (PTS=%2) - %3x%4\n")
                     .arg(i)
                     .arg(frames[i].pts)
                     .arg(frames[i].width)
                     .arg(frames[i].height);
    }

    QImage thumbnail = frames[0].image.scaled(
        400, 225, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_thumbnailLabel->setPixmap(QPixmap::fromImage(thumbnail));
    m_thumbnailLabel->setText(label);
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
}