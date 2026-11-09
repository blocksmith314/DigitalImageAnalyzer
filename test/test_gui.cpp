#include <gtest/gtest.h>

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTest>
#include <QTreeView>
#include <QUrl>
#include <fstream>
#include <string>

#include "bitstreamparser.h"
#include "mainwindow.h"
#include "nalanalyzer.h"
#include "nallistmodel.h"

namespace {

std::string data_path(const char* name) {
  return std::string(TEST_DATA_DIR) + "/" + name;
}

bool exists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

QLabel* frameLabel(MainWindow& window) {
  for (QLabel* label : window.findChildren<QLabel*>()) {
    if (label->text().startsWith("Frame:")) {
      return label;
    }
  }
  return nullptr;
}

QPushButton* buttonWithText(MainWindow& window, const QString& text) {
  for (QPushButton* button : window.findChildren<QPushButton*>()) {
    if (button->text() == text) {
      return button;
    }
  }
  return nullptr;
}

QPushButton* playButton(MainWindow& window) {
  if (QPushButton* pause = buttonWithText(window, "Pause")) {
    return pause;
  }
  return buttonWithText(window, "Play");
}

QWidget* fullscreenWindow(MainWindow& window) {
  for (QWidget* widget : QApplication::topLevelWidgets()) {
    if (widget != &window && widget->isVisible() &&
        (widget->windowFlags() & Qt::FramelessWindowHint)) {
      return widget;
    }
  }
  return nullptr;
}

}  // namespace

TEST(NalListModelTest, ReportsMetadata) {
  if (!exists(data_path("output.h264"))) {
    GTEST_SKIP() << "sample stream missing";
  }

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(data_path("output.h264"))) << parser.LastError();

  NALAnalyzer analyzer;
  ASSERT_TRUE(analyzer.AnalyzeAll(parser));

  NalListModel model;
  model.SetNalUnits(&parser.NalUnits());

  EXPECT_EQ(model.rowCount(QModelIndex()), 169);
  EXPECT_EQ(model.columnCount(QModelIndex()), 8);
  EXPECT_EQ(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(),
            "#");
  EXPECT_EQ(model.headerData(7, Qt::Horizontal, Qt::DisplayRole).toString(),
            "PTS");

  EXPECT_EQ(model.data(model.index(0, 1), Qt::DisplayRole).toInt(), 7);
  EXPECT_EQ(model.data(model.index(0, 2), Qt::DisplayRole).toString(), "SPS");
  EXPECT_EQ(model.data(model.index(3, 5), Qt::DisplayRole).toString(),
            "I");  // IDR slice
}

TEST(NalListModelTest, MapsFramesToRows) {
  if (!exists(data_path("output.h264"))) {
    GTEST_SKIP() << "sample stream missing";
  }

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(data_path("output.h264"))) << parser.LastError();

  NALAnalyzer analyzer;
  ASSERT_TRUE(analyzer.AnalyzeAll(parser));

  NalListModel model;
  model.SetNalUnits(&parser.NalUnits());

  const int first_frame = model.FirstRowForFrame(0);
  ASSERT_GE(first_frame, 0);
  EXPECT_GT(model.FirstRowForFrame(1), first_frame);
  EXPECT_EQ(model.FirstRowForFrame(-1), -1);
  EXPECT_EQ(model.FirstRowForFrame(1000000), -1);
}

TEST(NalListModelTest, HighlightEmitsOnlyWhenFrameChanges) {
  if (!exists(data_path("output.h264"))) {
    GTEST_SKIP() << "sample stream missing";
  }

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(data_path("output.h264"))) << parser.LastError();

  NALAnalyzer analyzer;
  ASSERT_TRUE(analyzer.AnalyzeAll(parser));

  NalListModel model;
  model.SetNalUnits(&parser.NalUnits());
  QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

  model.SetHighlightedFrame(0);
  EXPECT_EQ(spy.count(), 1);

  model.SetHighlightedFrame(0);  // same frame: no extra signal
  EXPECT_EQ(spy.count(), 1);

  // Switching frames repaints the old and the new frame rows.
  model.SetHighlightedFrame(1);
  EXPECT_EQ(spy.count(), 3);
}

TEST(MainWindowTest, LoadsFileAndUpdatesViews) {
  if (!exists(data_path("sample_720p.mp4"))) {
    GTEST_SKIP() << "MP4 fixture missing";
  }

  MainWindow window;
  window.show();
  ASSERT_TRUE(
      window.LoadFile(QString::fromStdString(data_path("sample_720p.mp4"))));
  QApplication::processEvents();

  EXPECT_TRUE(window.windowTitle().contains("sample_720p.mp4"));

  auto* tree = window.findChild<QTreeView*>();
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(tree->model(), nullptr);
  EXPECT_GT(tree->model()->rowCount(), 0);
  EXPECT_EQ(tree->model()->columnCount(), 8);

  auto* slider = window.findChild<QSlider*>();
  ASSERT_NE(slider, nullptr);
  EXPECT_EQ(slider->minimum(), 0);
  EXPECT_EQ(slider->maximum(), 9);  // 10 frames

  QLabel* label = frameLabel(window);
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->text(), "Frame: 1 / 10");

  bool status_has_nal_count = false;
  for (QLabel* statusLabel : window.statusBar()->findChildren<QLabel*>()) {
    if (statusLabel->text().contains("NAL units")) {
      status_has_nal_count = true;
    }
  }
  EXPECT_TRUE(status_has_nal_count);
}

TEST(MainWindowTest, HasHelpMenu) {
  MainWindow window;
  bool has_help = false;
  for (QAction* action : window.menuBar()->actions()) {
    if (action->text().contains("Help")) {
      has_help = true;
    }
  }
  EXPECT_TRUE(has_help);
}

TEST(MainWindowTest, HelpMenuHasEntries) {
  MainWindow window;
  QMenu* help = nullptr;
  for (QAction* action : window.menuBar()->actions()) {
    if (action->text().contains("Help")) {
      help = action->menu();
    }
  }
  ASSERT_NE(help, nullptr);
  QStringList entries;
  for (QAction* action : help->actions()) {
    entries << action->text();
  }
  EXPECT_TRUE(entries.contains("Keyboard Shortcuts"));
  EXPECT_TRUE(entries.contains("Documentation"));
  EXPECT_TRUE(entries.contains("Report an Issue"));
}

TEST(MainWindowTest, OpensFileByDrop) {
  if (!exists(data_path("output.h264"))) {
    GTEST_SKIP() << "H.264 fixture missing";
  }

  MainWindow window;
  window.show();

  QMimeData mime;
  mime.setUrls(
      {QUrl::fromLocalFile(QString::fromStdString(data_path("output.h264")))});

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton,
                        Qt::NoModifier);
#else
  QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton,
                        Qt::NoModifier);
#endif
  QApplication::sendEvent(&window, &enter);
  EXPECT_TRUE(enter.isAccepted());

  QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton,
                  Qt::NoModifier);
  QApplication::sendEvent(&window, &drop);
  QApplication::processEvents();

  EXPECT_TRUE(window.windowTitle().contains("output.h264"));
}

TEST(MainWindowTest, StepForwardUsesSpinBoxValue) {
  if (!exists(data_path("sample_720p.mp4"))) {
    GTEST_SKIP() << "MP4 fixture missing";
  }

  MainWindow window;
  window.show();
  ASSERT_TRUE(
      window.LoadFile(QString::fromStdString(data_path("sample_720p.mp4"))));
  QApplication::processEvents();

  auto* step = window.findChild<QSpinBox*>();
  ASSERT_NE(step, nullptr);
  step->setValue(3);

  QTest::keyClick(&window, Qt::Key_Right);
  QApplication::processEvents();

  QLabel* label = frameLabel(window);
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->text(), "Frame: 4 / 10");

  auto* slider = window.findChild<QSlider*>();
  ASSERT_NE(slider, nullptr);
  EXPECT_EQ(slider->value(), 3);
}

TEST(MainWindowTest, SpaceTogglesPlayback) {
  if (!exists(data_path("sample_720p.mp4"))) {
    GTEST_SKIP() << "MP4 fixture missing";
  }

  MainWindow window;
  window.show();
  ASSERT_TRUE(
      window.LoadFile(QString::fromStdString(data_path("sample_720p.mp4"))));
  QApplication::processEvents();

  QTest::keyClick(&window, Qt::Key_Space);
  QApplication::processEvents();
  QPushButton* button = playButton(window);
  ASSERT_NE(button, nullptr);
  EXPECT_EQ(button->text(), "Pause");

  QTest::keyClick(&window, Qt::Key_Space);
  QApplication::processEvents();
  EXPECT_EQ(button->text(), "Play");
}

TEST(MainWindowTest, BothPanelsHaveEqualWidth) {
  if (!exists(data_path("sample_720p.mp4"))) {
    GTEST_SKIP() << "MP4 fixture missing";
  }

  MainWindow window;
  window.show();
  ASSERT_TRUE(
      window.LoadFile(QString::fromStdString(data_path("sample_720p.mp4"))));
  QApplication::processEvents();

  QList<QSplitter*> horizontal;
  for (QSplitter* splitter : window.findChildren<QSplitter*>()) {
    if (splitter->orientation() == Qt::Horizontal) {
      horizontal.append(splitter);
    }
  }
  ASSERT_EQ(horizontal.size(), 2);
  EXPECT_EQ(horizontal[0]->sizes(), horizontal[1]->sizes());
}

TEST(MainWindowTest, DoubleClickEntersFullscreenAndEscapeLeaves) {
  if (!exists(data_path("sample_720p.mp4"))) {
    GTEST_SKIP() << "MP4 fixture missing";
  }

  MainWindow window;
  window.show();
  ASSERT_TRUE(
      window.LoadFile(QString::fromStdString(data_path("sample_720p.mp4"))));
  QApplication::processEvents();

  QLabel* video = nullptr;
  for (QLabel* label : window.findChildren<QLabel*>()) {
    if (label->toolTip().contains("fullscreen")) {
      video = label;
    }
  }
  ASSERT_NE(video, nullptr);

  QTest::mouseDClick(video, Qt::LeftButton, Qt::NoModifier,
                     video->rect().center());
  QApplication::processEvents();

  QWidget* full = fullscreenWindow(window);
  ASSERT_NE(full, nullptr);
  EXPECT_TRUE(full->isVisible());

  QTest::keyClick(full, Qt::Key_Escape);
  QApplication::processEvents();
  EXPECT_FALSE(full->isVisible());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
