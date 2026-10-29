#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>
#include <cstdio>

#include "annexb.h"
#include "audio.h"
#include "nallistmodel.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), parser_(CodecType::kH264) {
  SetupUi();
}

MainWindow::~MainWindow() = default;

void MainWindow::SetupUi() {
  setWindowTitle("Digital Image Analyzer - H.264/H.265 Bitstream Analyzer");
  resize(1400, 800);

  QMenu* fileMenu = menuBar()->addMenu("File");
  QAction* openAct = fileMenu->addAction("Open Bitstream...");
  openAct->setShortcut(QKeySequence::Open);
  connect(openAct, &QAction::triggered, this, &MainWindow::OpenFile);
  fileMenu->addSeparator();
  recent_menu_ = fileMenu->addMenu("Recent Files");
  for (int i = 0; i < MAX_RECENT_FILES; ++i) {
    QAction* a = recent_menu_->addAction("");
    a->setVisible(false);
    a->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_0 + i % 9 + 1)));
    connect(a, &QAction::triggered, this, &MainWindow::OpenRecentFile);
    recent_actions_.append(a);
  }
  recent_menu_->addSeparator();
  QAction* clearAct = recent_menu_->addAction("Clear Recent");
  connect(clearAct, &QAction::triggered, this, [this]() {
    recent_files_.clear();
    SaveRecentFiles();
    UpdateRecentMenu();
  });

  fileMenu->addSeparator();
  QAction* quitAct = fileMenu->addAction("Quit");
  quitAct->setShortcut(QKeySequence::Quit);
  connect(quitAct, &QAction::triggered, this, &QWidget::close);

  nal_model_ = new NalListModel(this);
  nal_list_ = new QTreeView();
  nal_list_->setModel(nal_model_);
  nal_list_->setRootIsDecorated(false);
  nal_list_->setUniformRowHeights(true);
  // First 8 columns keep fixed widths (NAL Name is auto-fitted in
  // BuildNalList()); the last empty column absorbs extra width so the table
  // fills the panel.
  QHeaderView* hdr = nal_list_->header();
  hdr->setStretchLastSection(false);
  hdr->setSectionResizeMode(QHeaderView::Interactive);
  hdr->setSectionResizeMode(8, QHeaderView::Stretch);
  nal_list_->setColumnWidth(0, 60);
  nal_list_->setColumnWidth(1, 60);
  nal_list_->setColumnWidth(2, 120);
  nal_list_->setColumnWidth(3, 80);
  nal_list_->setColumnWidth(4, 110);
  nal_list_->setColumnWidth(5, 40);
  nal_list_->setColumnWidth(6, 50);
  nal_list_->setColumnWidth(7, 50);
  nal_list_->setSelectionBehavior(QAbstractItemView::SelectRows);
  nal_list_->setAlternatingRowColors(true);
  connect(nal_list_->selectionModel(), &QItemSelectionModel::currentRowChanged,
          this, [this](const QModelIndex& current, const QModelIndex&) {
            if (current.isValid()) OnNalSelected(current.row());
          });

  stats_panel_ = new QTextBrowser();
  stats_panel_->setOpenExternalLinks(false);
  stats_panel_->setMinimumHeight(240);  // give Stream Statistics more room
  stats_panel_->document()->setDocumentMargin(
      2);  // compact layout to fit all rows
  stats_panel_->document()->setDefaultStyleSheet(
      "table { border-collapse: collapse; }"
      "td { padding: 0 8px 0 0; }");

  details_ = new QTextBrowser();
  details_->setOpenExternalLinks(false);
  details_->setPlaceholderText("Select a NAL unit to view details...");

  video_label_ = new QLabel("(no video loaded)");
  video_label_->setAlignment(Qt::AlignCenter);
  video_label_->setMinimumHeight(240);
  video_label_->setStyleSheet(
      "border: 1px solid #cccccc; background: #1a1a1a; color: #aaaaaa;");
  video_label_->setScaledContents(false);
  video_label_->installEventFilter(this);
  video_label_->setToolTip("Double-click for fullscreen");
  // Ignore the pixmap size hint: the label must not force the player panel
  // wider than the NAL Details panel. The image is scaled to fit instead.
  video_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  video_label_->setMinimumSize(1, 240);

  player_timer_ = new QTimer(this);
  connect(player_timer_, &QTimer::timeout, this, &MainWindow::OnPlayerTick);
  audio_ = new AudioPlayer(this);

  auto* prevShortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
  connect(prevShortcut, &QShortcut::activated, this,
          &MainWindow::OnStepBackward);
  auto* nextShortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
  connect(nextShortcut, &QShortcut::activated, this,
          &MainWindow::OnStepForward);
  auto* playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  connect(playShortcut, &QShortcut::activated, this,
          &MainWindow::OnPlayPause);

  play_btn_ = new QPushButton("Play");
  play_btn_->setFixedWidth(64);
  play_btn_->setEnabled(false);
  connect(play_btn_, &QPushButton::clicked, this, &MainWindow::OnPlayPause);

  stop_btn_ = new QPushButton("Stop");
  stop_btn_->setFixedWidth(64);
  stop_btn_->setEnabled(false);
  connect(stop_btn_, &QPushButton::clicked, this, &MainWindow::OnStop);

  prev_frame_btn_ = new QPushButton("\u25c0");
  prev_frame_btn_->setEnabled(false);
  prev_frame_btn_->setFixedWidth(32);
  prev_frame_btn_->setToolTip("Step backward one frame");
  connect(prev_frame_btn_, &QPushButton::clicked, this,
          &MainWindow::OnStepBackward);

  next_frame_btn_ = new QPushButton("\u25b6");
  next_frame_btn_->setEnabled(false);
  next_frame_btn_->setFixedWidth(32);
  next_frame_btn_->setToolTip("Step forward one frame");
  connect(next_frame_btn_, &QPushButton::clicked, this,
          &MainWindow::OnStepForward);

  speed_combo_ = new QComboBox();
  speed_combo_->setToolTip("Playback speed");
  {
    struct SpeedItem {
      const char* label;
      double value;
    };
    static const SpeedItem speeds[] = {
        {"0.25x", 0.25}, {"0.5x", 0.5}, {"0.75x", 0.75}, {"1x", 1.0},
        {"1.5x", 1.5},   {"2x", 2.0},   {"4x", 4.0}};
    for (const auto& s : speeds)
      speed_combo_->addItem(QString::fromUtf8(s.label), s.value);
  }
  speed_combo_->setCurrentIndex(3);  // default: 1x
  speed_combo_->setFixedWidth(80);
  connect(speed_combo_, &QComboBox::currentIndexChanged, this,
          &MainWindow::OnSpeedChanged);

  seek_slider_ = new QSlider(Qt::Horizontal);
  seek_slider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  seek_slider_->setMinimumWidth(60);
  seek_slider_->setRange(0, 0);
  connect(seek_slider_, &QSlider::valueChanged, this, &MainWindow::OnSeek);

  frame_info_label_ = new QLabel("Frame: - / -");
  frame_info_label_->setMinimumWidth(80);

  QHBoxLayout* controlRow = new QHBoxLayout();
  controlRow->addWidget(play_btn_);
  controlRow->addWidget(stop_btn_);
  fullscreen_btn_ = new QPushButton();
  fullscreen_btn_->setFixedWidth(32);
  fullscreen_btn_->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
  fullscreen_btn_->setToolTip(
      "Toggle fullscreen (double-click video, Esc to exit)");
  connect(fullscreen_btn_, &QPushButton::clicked, this,
          &MainWindow::ToggleFullscreen);

  controlRow->addWidget(prev_frame_btn_);
  controlRow->addWidget(next_frame_btn_);
  controlRow->addWidget(fullscreen_btn_);

  auto* stepLabel = new QLabel("Step:");
  stepLabel->setToolTip("Frames skipped by the arrow keys");
  step_spin_ = new QSpinBox();
  step_spin_->setRange(1, 1000);
  step_spin_->setValue(1);
  step_spin_->setFixedWidth(56);
  controlRow->addWidget(stepLabel);
  controlRow->addWidget(step_spin_);
  controlRow->addWidget(speed_combo_);
  controlRow->addWidget(seek_slider_, 1);
  frame_info_label_->setMinimumWidth(0);
  controlRow->addWidget(frame_info_label_);

  QGroupBox* playerGroup = new QGroupBox("Video Player");
  QVBoxLayout* playerLayout = new QVBoxLayout(playerGroup);
  playerLayout->addWidget(video_label_, 1);
  playerLayout->addLayout(controlRow);

  QGroupBox* detailGroup = new QGroupBox("NAL Details");
  QVBoxLayout* detailLayout = new QVBoxLayout(detailGroup);
  detailLayout->addWidget(details_);

  QGroupBox* statsGroup = new QGroupBox("Stream Statistics");
  QVBoxLayout* statsLayout = new QVBoxLayout(statsGroup);
  statsLayout->addWidget(stats_panel_);

  // Top row: NAL list | Video Player
  QSplitter* topSplit = new QSplitter(Qt::Horizontal);
  topSplit->addWidget(nal_list_);
  topSplit->addWidget(playerGroup);
  topSplit->setStretchFactor(0, 1);
  topSplit->setStretchFactor(1, 1);
  topSplit->setSizes({600, 800});

  // Bottom row: Stream Statistics | NAL Details (same row => equal height)
  QSplitter* bottomSplit = new QSplitter(Qt::Horizontal);
  bottomSplit->addWidget(statsGroup);
  bottomSplit->addWidget(detailGroup);
  bottomSplit->setStretchFactor(0, 1);
  bottomSplit->setStretchFactor(1, 1);
  bottomSplit->setSizes({600, 800});

  // Vertical split: top row / bottom row
  QSplitter* mainSplit = new QSplitter(Qt::Vertical);
  mainSplit->addWidget(topSplit);
  mainSplit->addWidget(bottomSplit);
  mainSplit->setStretchFactor(0, 6);
  mainSplit->setStretchFactor(1, 5);
  mainSplit->setSizes({410, 340});

  // Keep the horizontal split position of both rows in sync
  connect(topSplit, &QSplitter::splitterMoved, this,
          [this](int, int) { SyncSplitterWidths(true); });
  connect(bottomSplit, &QSplitter::splitterMoved, this,
          [this](int, int) { SyncSplitterWidths(false); });

  setCentralWidget(mainSplit);
  top_split_ = topSplit;
  bottom_split_ = bottomSplit;
  // Align the two rows after the first layout pass.
  QTimer::singleShot(0, this, [this]() { ApplySplitFraction(); });

  time_info_label_ = new QLabel();
  statusBar()->addWidget(time_info_label_);
  file_info_label_ = new QLabel("No file loaded");
  statusBar()->addWidget(file_info_label_, 1);
  LoadRecentFiles();
  UpdateRecentMenu();
}

void MainWindow::OpenFile() {
  QString path = QFileDialog::getOpenFileName(
      this, "Open Video File", "",
      "Video Files (*.h264 *.h265 *.264 *.265 *.hevc *.mp4 *.mov *.m4v *.ts "
      "*.mkv);;All Files (*)");
  if (path.isEmpty()) return;
  if (LoadFile(path)) AddRecentFile(path);
}

bool MainWindow::LoadFile(const QString& path) {
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
  CodecType detected = CodecType::kH264;
  bool raw_annex_b = false;
  std::string probe_error;
  if (!ProbeVideoFile(sp, detected, raw_annex_b, probe_error)) {
    dlg.close();
    QMessageBox::critical(this, "Error", QString::fromStdString(probe_error));
    return false;
  }

  const qint64 probeMs = timer.restart();
  parser_ = BitstreamParser(detected);
  dlg.setLabelText(QString("Scanning %1...").arg(QFileInfo(path).fileName()));
  bool parsed = false;
  if (raw_annex_b) {
    parsed = parser_.ParseFile(sp, progress);
  } else {
    // Plain MP4: read the moov sample table and touch only NAL headers.
    parsed = parser_.ParseMp4Indexed(sp, detected, progress);
    if (!parsed && parser_.LastError() != "Cancelled") {
      // Fragmented/unsupported layout: fall back to a full demux.
      parsed = parser_.ParseAvcc(sp, detected, progress);
    }
  }
  if (!parsed) {
    dlg.close();
    QMessageBox::critical(this, "Error",
                          QString::fromStdString(parser_.LastError()));
    return false;
  }
  const qint64 demuxMs = timer.restart();

  current_path_ = sp;
  dlg.setLabelText(QString("Analyzing %1...").arg(QFileInfo(path).fileName()));
  if (!analyzer_.AnalyzeAll(parser_, progress)) {
    dlg.close();
    QMessageBox::critical(this, "Error", "Loading cancelled");
    return false;
  }
  const qint64 analyze_ms = timer.restart();
  dlg.close();

  // The NAL metadata is enough for the UI; drop resident file pages so a
  // multi-GB stream does not stay pinned in memory.
  parser_.ReleaseMappedPages();

  BuildNalList();
  UpdateStats();
  StartPlayer();
  if (audio_) audio_->open(current_path_);

  const qint64 total_ms = probeMs + demuxMs + analyze_ms + timer.restart();
  if (time_info_label_) {
    time_info_label_->setText(
        QString("Loaded in %1 s  (probe %2 s, read %3 s, analyze %4 s)  ")
            .arg(total_ms / 1000.0, 0, 'f', 1)
            .arg(probeMs / 1000.0, 0, 'f', 1)
            .arg(demuxMs / 1000.0, 0, 'f', 1)
            .arg(analyze_ms / 1000.0, 0, 'f', 1));
  }

  QFileInfo fi(path);
  auto& stats = analyzer_.stats();
  QString codec_str =
      (stats.codec == CodecType::kH264) ? "H.264/AVC" : "H.265/HEVC";
  file_info_label_->setText(QString("%1 | %2 | %3 NAL units | %4 bytes | %5x%6")
                                .arg(fi.fileName())
                                .arg(codec_str)
                                .arg(stats.total_nals)
                                .arg(stats.total_bytes)
                                .arg(stats.width)
                                .arg(stats.height));

  setWindowTitle(QString("Digital Image Analyzer - %1").arg(fi.fileName()));
  return true;
}

void MainWindow::BuildNalList() {
  nal_model_->SetNalUnits(&parser_.NalUnits());
  nal_list_->resizeColumnToContents(2);
}

void MainWindow::UpdateStats() {
  const auto& s = analyzer_.stats();
  const QString codec_str =
      (s.codec == CodecType::kH264) ? "H.264/AVC" : "H.265/HEVC";

  const double kb = s.total_bytes / 1024.0;
  const double mb = kb / 1024.0;
  const int frames = (int)s.FrameCount;
  const double fps = frame_rate_ > 0.0 ? frame_rate_ : 0.0;
  const double duration = (fps > 0.0 && frames > 0) ? frames / fps : 0.0;
  const double mbps =
      duration > 0.0 ? (s.total_bytes * 8.0 / duration / 1e6) : 0.0;
  const double avg_nal =
      s.total_nals ? (double)s.total_bytes / s.total_nals : 0.0;
  const double avg_gop = s.idr_count ? (double)frames / s.idr_count : 0.0;
  const double slices_per_frame = frames ? (double)s.slice_count / frames : 0.0;

  QString html;
  html += QString("<p style='margin:0 0 3px 0'><b><big>%1</big></b></p>")
              .arg(codec_str);
  html += "<table border='0' cellspacing='0' cellpadding='0'>";
  auto row = [&html](const QString& k, const QString& v) {
    html +=
        QString("<tr><td>%1&nbsp;&nbsp;</td><td><b>%2</b></td></tr>").arg(k, v);
  };

  row("NAL units", QString::number(s.total_nals));
  row("Total size",
      QString("%1 KB (%2 MB)").arg(kb, 0, 'f', 2).arg(mb, 0, 'f', 2));
  QStringList formatBits;
  if (s.width > 0 && s.height > 0)
    formatBits << QString("%1 x %2").arg(s.width).arg(s.height);
  if (!s.profile.empty()) formatBits << QString::fromStdString(s.profile);
  if (!s.level.empty())
    formatBits << QString("Level %1").arg(QString::fromStdString(s.level));
  if (!formatBits.isEmpty()) row("Format", formatBits.join("  \u00b7  "));
  row("Frames", QString("%1  (I %2 / P %3 / B %4)")
                    .arg(frames)
                    .arg(s.i_frame_count)
                    .arg(s.p_frame_count)
                    .arg(s.b_frame_count));
  if (fps > 0.0) row("Frame rate", QString("%1 fps").arg(fps, 0, 'f', 2));
  if (duration > 0.0) {
    row("Duration", QString("%1 s").arg(duration, 0, 'f', 2));
    row("Bitrate", QString("%1 Mbps").arg(mbps, 0, 'f', 3));
  }
  row("Slices", QString("%1  (%2 / frame)")
                    .arg(s.slice_count)
                    .arg(slices_per_frame, 0, 'f', 2));
  if (s.idr_count > 0)
    row("GOP", QString("%1 IDR, avg %2 frames")
                   .arg(s.idr_count)
                   .arg(avg_gop, 0, 'f', 2));
  if (s.total_nals > 0)
    row("NAL size", QString("avg %1 B / max %2 B / min %3 B")
                        .arg((int)avg_nal)
                        .arg(s.max_nal_size)
                        .arg(s.min_nal_size));
  html += "</table>";

  html += "<p style='margin:6px 0 3px 0'><b>NAL Type Distribution</b></p>";
  html += "<table border='0' cellspacing='0' cellpadding='0'>";
  row("IDR/Keyframe", QString::number(s.idr_count));
  row("SPS", QString::number(s.sps_count));
  row("PPS", QString::number(s.pps_count));
  if (s.vps_count > 0) row("VPS", QString::number(s.vps_count));
  row("SEI / AUD", QString("%1 / %2").arg(s.sei_count).arg(s.aud_count));
  row("Slice NALs", QString::number(s.slice_count));
  html += "</table>";

  stats_panel_->setHtml(html);
}

void MainWindow::StartPlayer() {
  auto& stats = analyzer_.stats();
  player_timer_->stop();
  is_playing_ = false;
  play_btn_->setText("Play");

  player_decoder_.close();
  if (!player_decoder_.OpenFile(current_path_, stats.codec)) {
    video_label_->setText(
        QString("Decoder error: %1").arg(player_decoder_.LastError().c_str()));
    play_btn_->setEnabled(false);
    stop_btn_->setEnabled(false);
    prev_frame_btn_->setEnabled(false);
    next_frame_btn_->setEnabled(false);
    seek_slider_->setEnabled(false);
    return;
  }

  // No upfront decode: frames are decoded on demand while playing/seeking.
  total_frames_ = (int)stats.FrameCount;
  frame_rate_ = player_decoder_.FrameRate();
  UpdateStats();  // refresh stats that depend on decoded frames / frame rate

  if (total_frames_ <= 0) {
    video_label_->setText("No frames found");
    play_btn_->setEnabled(false);
    stop_btn_->setEnabled(false);
    prev_frame_btn_->setEnabled(false);
    next_frame_btn_->setEnabled(false);
    seek_slider_->setRange(0, 0);
    return;
  }

  seek_slider_->setRange(0, qMax(0, total_frames_ - 1));
  seek_slider_->setValue(0);
  play_btn_->setEnabled(true);
  stop_btn_->setEnabled(true);
  prev_frame_btn_->setEnabled(true);
  next_frame_btn_->setEnabled(true);
  seek_slider_->setEnabled(true);

  video_size_ = video_label_->size();
  current_frame_index_ = -1;
  ShowFrameAt(0);
}

void MainWindow::ShowFrame(const QImage& image, int frame_idx) {
  if (image.isNull()) return;
  current_image_ = image;
  QSize target = video_size_.isValid() ? video_size_ : video_label_->size();
  QImage scaled =
      image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  video_label_->setPixmap(QPixmap::fromImage(scaled));
  frame_info_label_->setText(
      QString("Frame: %1 / %2").arg(frame_idx).arg(total_frames_));
  if (!seek_slider_->isSliderDown()) {
    QSignalBlocker blocker(seek_slider_);
    seek_slider_->setValue(frame_idx);
  }
  HighlightFrame(frame_idx);
  if (fullscreen_) UpdateFullscreenFrame();
}

void MainWindow::ShowFrameAt(int idx) {
  if (total_frames_ <= 0) return;
  idx = qBound(0, idx, total_frames_ - 1);
  if (idx == current_frame_index_) return;

  // Decode sequentially when moving forward by one; otherwise seek.
  const bool jump =
      (idx < current_frame_index_ || idx > current_frame_index_ + 1);
  if (jump) {
    if (!player_decoder_.SeekToFrame(idx)) return;
  }

  DecodedFrame frame;
  if (!player_decoder_.DecodeNextFrame(frame)) {
    // End of decodable frames (container may have fewer than the NAL count).
    player_timer_->stop();
    is_playing_ = false;
    play_btn_->setText("Play");
    return;
  }
  current_frame_index_ = idx;
  current_pts_ = frame.pts_sec;
  ShowFrame(frame.image, idx);

  if (jump) {
    has_pending_ = false;
    clock_start_pts_ = current_pts_;
    play_clock_.restart();
    if (audio_ && audio_->HasAudio()) {
      const double seconds =
          player_decoder_.HasTimestamps() && frame_rate_ > 0.0
              ? current_pts_
              : (frame_rate_ > 0.0 ? (double)idx / frame_rate_ : 0.0);
      audio_->SeekTo(seconds);
    }
  }
}

void MainWindow::HighlightFrame(int frame_idx) {
  nal_model_->SetHighlightedFrame(frame_idx);
  if (frame_idx < 0) return;

  const int row = nal_model_->FirstRowForFrame(frame_idx);
  if (row < 0) return;
  const QModelIndex idx = nal_model_->index(row, 0);
  if (!nal_list_->viewport()->rect().intersects(nal_list_->visualRect(idx))) {
    QTimer::singleShot(0, this, [this, idx]() {
      nal_list_->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    });
  }
}

void MainWindow::OnPlayPause() {
  if (total_frames_ == 0) return;

  if (!is_playing_) {
    if (current_frame_index_ >= total_frames_ - 1) ShowFrameAt(0);
    has_pending_ = false;
    clock_start_pts_ = current_pts_;
    play_clock_.start();
    player_timer_->start(
        player_decoder_.HasTimestamps() ? 5 : PlayerIntervalMs());
    play_btn_->setText("Pause");
    is_playing_ = true;
    if (audio_) audio_->play();
  } else {
    player_timer_->stop();
    play_btn_->setText("Play");
    is_playing_ = false;
    if (audio_) audio_->pause();
  }
}

int MainWindow::PlayerIntervalMs() const {
  const double fps = frame_rate_ > 0.0 ? frame_rate_ : 30.0;
  const double speed = playback_speed_ > 0.0 ? playback_speed_ : 1.0;
  const double ms = 1000.0 / (fps * speed);
  return qMax(1, (int)(ms + 0.5));
}

void MainWindow::OnSpeedChanged(int index) {
  if (!speed_combo_) return;
  double speed = speed_combo_->itemData(index).toDouble();
  if (speed <= 0.0) speed = 1.0;
  playback_speed_ = speed;
  if (is_playing_) {
    clock_start_pts_ = current_pts_;
    play_clock_.restart();
    player_timer_->start(
        player_decoder_.HasTimestamps() ? 5 : PlayerIntervalMs());
  }
}

void MainWindow::OnStop() {
  player_timer_->stop();
  is_playing_ = false;
  play_btn_->setText("Play");
  if (audio_) audio_->stop();
  ShowFrameAt(0);
}

void MainWindow::OnStepForward() {
  if (total_frames_ <= 0) return;
  player_timer_->stop();
  is_playing_ = false;
  play_btn_->setText("Play");
  if (audio_) audio_->pause();
  const int step = step_spin_ ? step_spin_->value() : 1;
  ShowFrameAt(current_frame_index_ + step);
}

void MainWindow::OnStepBackward() {
  if (total_frames_ <= 0) return;
  player_timer_->stop();
  is_playing_ = false;
  play_btn_->setText("Play");
  if (audio_) audio_->pause();
  const int step = step_spin_ ? step_spin_->value() : 1;
  ShowFrameAt(current_frame_index_ - step);
}

void MainWindow::OnSeek(int value) {
  if (total_frames_ <= 0) return;
  ShowFrameAt(value);
}

void MainWindow::OnPlayerTick() {
  if (total_frames_ <= 0) return;

  // Raw streams have no container timestamps: fall back to the fixed timer.
  if (!player_decoder_.HasTimestamps()) {
    if (current_frame_index_ + 1 >= total_frames_) {
      StopPlayback();
      return;
    }
    ShowFrameAt(current_frame_index_ + 1);
    return;
  }

  if (!has_pending_) {
    if (!player_decoder_.DecodeNextFrame(pending_frame_)) {
      StopPlayback();
      return;
    }
    has_pending_ = true;
  }

  const double now =
      clock_start_pts_ + (play_clock_.elapsed() / 1000.0) * playback_speed_;
  if (pending_frame_.pts_sec <= now) {
    const DecodedFrame frame = pending_frame_;
    has_pending_ = false;
    current_frame_index_++;
    current_pts_ = frame.pts_sec;
    ShowFrame(frame.image, current_frame_index_);
    if (current_frame_index_ >= total_frames_ - 1) {
      StopPlayback();
    }
  }
}

void MainWindow::StopPlayback() {
  player_timer_->stop();
  is_playing_ = false;
  play_btn_->setText("Play");
  if (audio_) audio_->pause();
}

void MainWindow::OnNalSelected(int idx) {
  if (idx < 0) return;

  const auto& nals = parser_.NalUnits();
  if ((size_t)idx >= nals.size()) return;

  NALAnalysis analysis = analyzer_.AnalyzeOne(
      nals[(size_t)idx], parser_.NalData(nals[(size_t)idx]));
  details_->setPlainText(QString::fromStdString(analysis.details));

  const int f_idx = nals[(size_t)idx].frame_index;
  if (f_idx >= 0 && f_idx < total_frames_) {
    if (is_playing_) StopPlayback();
    ShowFrameAt(f_idx);
  }
}

void MainWindow::OpenRecentFile() {
  QAction* action = qobject_cast<QAction*>(sender());
  if (!action) return;
  int idx = action->data().toInt();
  if (idx < 0 || idx >= recent_files_.size()) return;
  QString path = recent_files_.at(idx);
  if (!QFile::exists(path)) {
    QMessageBox::warning(this, "File Missing",
                         QString("File no longer exists:\n%1").arg(path));
    recent_files_.removeAt(idx);
    SaveRecentFiles();
    UpdateRecentMenu();
    return;
  }
  if (LoadFile(path)) AddRecentFile(path);
}

void MainWindow::AddRecentFile(const QString& path) {
  QString canonical = QFileInfo(path).canonicalFilePath();
  if (canonical.isEmpty()) canonical = path;
  recent_files_.removeAll(canonical);
  recent_files_.prepend(canonical);
  while (recent_files_.size() > MAX_RECENT_FILES) recent_files_.removeLast();
  SaveRecentFiles();
  UpdateRecentMenu();
}

void MainWindow::UpdateRecentMenu() {
  for (int i = 0; i < recent_actions_.size(); ++i) {
    QAction* a = recent_actions_.at(i);
    if (i < recent_files_.size()) {
      QString path = recent_files_.at(i);
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
  recent_menu_->setEnabled(!recent_files_.isEmpty());
}

void MainWindow::LoadRecentFiles() {
  QSettings settings;
  recent_files_ = settings.value("recentFiles").toStringList();
}

void MainWindow::SaveRecentFiles() {
  QSettings settings;
  settings.setValue("recentFiles", recent_files_);
}

void MainWindow::SyncSplitterWidths(bool from_top) {
  if (!top_split_ || !bottom_split_ || syncing_splitters_) return;

  // Remember the split position the user chose and apply it to both rows.
  QSplitter* source = from_top ? top_split_ : bottom_split_;
  const QList<int> sizes = source->sizes();
  if (sizes.size() < 2) return;
  const int total = sizes[0] + sizes[1] + top_split_->handleWidth();
  // Ignore degenerate layouts reported before the window is fully sized.
  if (total > 400) split_fraction_ = (double)sizes[0] / (double)total;

  ApplySplitFraction();
}

void MainWindow::ApplySplitFraction() {
  if (!top_split_ || !bottom_split_) return;

  const int width = top_split_->width() - top_split_->handleWidth();
  if (width <= 0) return;

  const int left = qBound(0, (int)(split_fraction_ * width), width);
  const QList<int> sizes{left, width - left};

  syncing_splitters_ = true;
  top_split_->setSizes(sizes);
  bottom_split_->setSizes(sizes);

  // If a row could not honour the split because of its minimum widths,
  // align both rows to the wider right-hand panel that resulted.
  const QList<int> top = top_split_->sizes();
  const QList<int> bottom = bottom_split_->sizes();
  if (top.size() >= 2 && bottom.size() >= 2) {
    const int total = top[0] + top[1] + top_split_->handleWidth();
    const int right = qMax(top[1], bottom[1]);
    const int aligned_left = qMax(0, total - right - top_split_->handleWidth());
    const QList<int> aligned{aligned_left,
                             total - aligned_left - top_split_->handleWidth()};
    top_split_->setSizes(aligned);
    bottom_split_->setSizes(aligned);
  }
  syncing_splitters_ = false;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::MouseButtonDblClick &&
      (watched == video_label_ || watched == fullscreen_label_)) {
    ToggleFullscreen();
    return true;
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::ToggleFullscreen() {
  if (fullscreen_)
    ExitFullscreen();
  else
    EnterFullscreen();
}

void MainWindow::EnterFullscreen() {
  if (fullscreen_ || current_image_.isNull()) return;

  if (!fullscreen_window_) {
    fullscreen_window_ =
        new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
    fullscreen_window_->setStyleSheet("background:black");
    fullscreen_label_ = new QLabel(fullscreen_window_);
    fullscreen_label_->setAlignment(Qt::AlignCenter);
    fullscreen_label_->setStyleSheet("background:black");
    fullscreen_label_->installEventFilter(this);
    auto* layout = new QVBoxLayout(fullscreen_window_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(fullscreen_label_);
    auto* escape =
        new QShortcut(QKeySequence(Qt::Key_Escape), fullscreen_window_);
    QObject::connect(escape, &QShortcut::activated, this,
                     &MainWindow::ExitFullscreen);
    auto* prev = new QShortcut(QKeySequence(Qt::Key_Left), fullscreen_window_);
    QObject::connect(prev, &QShortcut::activated, this,
                     &MainWindow::OnStepBackward);
    auto* next = new QShortcut(QKeySequence(Qt::Key_Right), fullscreen_window_);
    QObject::connect(next, &QShortcut::activated, this,
                     &MainWindow::OnStepForward);
    auto* space =
        new QShortcut(QKeySequence(Qt::Key_Space), fullscreen_window_);
    QObject::connect(space, &QShortcut::activated, this,
                     &MainWindow::OnPlayPause);
  }

  fullscreen_window_->showFullScreen();
  fullscreen_ = true;
  UpdateFullscreenFrame();
  hide();  // hide window frame, control bar and progress slider
}

void MainWindow::ExitFullscreen() {
  if (!fullscreen_) return;
  fullscreen_ = false;
  if (fullscreen_window_) fullscreen_window_->hide();
  show();
  raise();
  activateWindow();
}

void MainWindow::UpdateFullscreenFrame() {
  if (!fullscreen_ || !fullscreen_label_ || current_image_.isNull()) return;
  QSize target = fullscreen_window_ ? fullscreen_window_->size() : QSize();
  if (target.isEmpty()) target = fullscreen_label_->size();
  fullscreen_label_->setPixmap(QPixmap::fromImage(current_image_.scaled(
      target, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (fullscreen_window_) fullscreen_window_->close();
  if (audio_) audio_->close();
  SaveRecentFiles();
  event->accept();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);

  // Keep the right-hand panels (Video Player / NAL Details) equally wide.
  ApplySplitFraction();
  if (!current_image_.isNull() && video_label_) {
    const QSize newSize = video_label_->size();
    if (newSize != video_size_ && newSize.width() > 0 && newSize.height() > 0) {
      video_size_ = newSize;
      ShowFrame(current_image_, current_frame_index_);
    }
  }
}
