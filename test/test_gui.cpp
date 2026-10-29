#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTest>
#include <QTreeView>

#include "bitstreamparser.h"
#include "mainwindow.h"
#include "nalanalyzer.h"
#include "nallistmodel.h"

#include <fstream>
#include <string>

namespace
{

    std::string dataPath(const char* name) { return std::string(TEST_DATA_DIR) + "/" + name; }

    bool exists(const std::string& path)
    {
        std::ifstream file(path);
        return file.good();
    }

    QLabel* frameLabel(MainWindow& window)
    {
        for (QLabel* label : window.findChildren<QLabel*>())
        {
            if (label->text().startsWith("Frame:"))
                return label;
        }
        return nullptr;
    }

    QPushButton* buttonWithText(MainWindow& window, const QString& text)
    {
        for (QPushButton* button : window.findChildren<QPushButton*>())
        {
            if (button->text() == text)
                return button;
        }
        return nullptr;
    }

    QPushButton* playButton(MainWindow& window)
    {
        if (QPushButton* pause = buttonWithText(window, "Pause"))
            return pause;
        return buttonWithText(window, "Play");
    }

    QWidget* fullscreenWindow(MainWindow& window)
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (widget != &window && widget->isVisible() && (widget->windowFlags() & Qt::FramelessWindowHint))
            {
                return widget;
            }
        }
        return nullptr;
    }

} // namespace

TEST(NalListModelTest, ReportsMetadata)
{
    if (!exists(dataPath("output.h264")))
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(dataPath("output.h264"))) << parser.lastError();

    NALAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyzeAll(parser));

    NalListModel model;
    model.setNalUnits(&parser.nalUnits());

    EXPECT_EQ(model.rowCount(QModelIndex()), 169);
    EXPECT_EQ(model.columnCount(QModelIndex()), 8);
    EXPECT_EQ(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(), "#");
    EXPECT_EQ(model.headerData(7, Qt::Horizontal, Qt::DisplayRole).toString(), "PTS");

    EXPECT_EQ(model.data(model.index(0, 1), Qt::DisplayRole).toInt(), 7);
    EXPECT_EQ(model.data(model.index(0, 2), Qt::DisplayRole).toString(), "SPS");
    EXPECT_EQ(model.data(model.index(3, 5), Qt::DisplayRole).toString(), "I"); // IDR slice
}

TEST(NalListModelTest, MapsFramesToRows)
{
    if (!exists(dataPath("output.h264")))
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(dataPath("output.h264"))) << parser.lastError();

    NALAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyzeAll(parser));

    NalListModel model;
    model.setNalUnits(&parser.nalUnits());

    const int firstFrame = model.firstRowForFrame(0);
    ASSERT_GE(firstFrame, 0);
    EXPECT_GT(model.firstRowForFrame(1), firstFrame);
    EXPECT_EQ(model.firstRowForFrame(-1), -1);
    EXPECT_EQ(model.firstRowForFrame(1000000), -1);
}

TEST(NalListModelTest, HighlightEmitsOnlyWhenFrameChanges)
{
    if (!exists(dataPath("output.h264")))
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(dataPath("output.h264"))) << parser.lastError();

    NALAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyzeAll(parser));

    NalListModel model;
    model.setNalUnits(&parser.nalUnits());
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    model.setHighlightedFrame(0);
    EXPECT_EQ(spy.count(), 1);

    model.setHighlightedFrame(0); // same frame: no extra signal
    EXPECT_EQ(spy.count(), 1);

    model.setHighlightedFrame(1);
    EXPECT_EQ(spy.count(), 2);
}

TEST(MainWindowTest, LoadsFileAndUpdatesViews)
{
    if (!exists(dataPath("sample_720p.mp4")))
        GTEST_SKIP() << "MP4 fixture missing";

    MainWindow window;
    window.show();
    ASSERT_TRUE(window.loadFile(QString::fromStdString(dataPath("sample_720p.mp4"))));
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
    EXPECT_EQ(slider->maximum(), 9); // 10 frames

    QLabel* label = frameLabel(window);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), "Frame: 0 / 10");

    bool statusHasNalCount = false;
    for (QLabel* statusLabel : window.statusBar()->findChildren<QLabel*>())
    {
        if (statusLabel->text().contains("NAL units"))
            statusHasNalCount = true;
    }
    EXPECT_TRUE(statusHasNalCount);
}

TEST(MainWindowTest, StepForwardUsesSpinBoxValue)
{
    if (!exists(dataPath("sample_720p.mp4")))
        GTEST_SKIP() << "MP4 fixture missing";

    MainWindow window;
    window.show();
    ASSERT_TRUE(window.loadFile(QString::fromStdString(dataPath("sample_720p.mp4"))));
    QApplication::processEvents();

    auto* step = window.findChild<QSpinBox*>();
    ASSERT_NE(step, nullptr);
    step->setValue(3);

    QTest::keyClick(&window, Qt::Key_Right);
    QApplication::processEvents();

    QLabel* label = frameLabel(window);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), "Frame: 3 / 10");

    auto* slider = window.findChild<QSlider*>();
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->value(), 3);
}

TEST(MainWindowTest, SpaceTogglesPlayback)
{
    if (!exists(dataPath("sample_720p.mp4")))
        GTEST_SKIP() << "MP4 fixture missing";

    MainWindow window;
    window.show();
    ASSERT_TRUE(window.loadFile(QString::fromStdString(dataPath("sample_720p.mp4"))));
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

TEST(MainWindowTest, BothPanelsHaveEqualWidth)
{
    if (!exists(dataPath("sample_720p.mp4")))
        GTEST_SKIP() << "MP4 fixture missing";

    MainWindow window;
    window.show();
    ASSERT_TRUE(window.loadFile(QString::fromStdString(dataPath("sample_720p.mp4"))));
    QApplication::processEvents();

    QList<QSplitter*> horizontal;
    for (QSplitter* splitter : window.findChildren<QSplitter*>())
    {
        if (splitter->orientation() == Qt::Horizontal)
            horizontal.append(splitter);
    }
    ASSERT_EQ(horizontal.size(), 2);
    EXPECT_EQ(horizontal[0]->sizes(), horizontal[1]->sizes());
}

TEST(MainWindowTest, DoubleClickEntersFullscreenAndEscapeLeaves)
{
    if (!exists(dataPath("sample_720p.mp4")))
        GTEST_SKIP() << "MP4 fixture missing";

    MainWindow window;
    window.show();
    ASSERT_TRUE(window.loadFile(QString::fromStdString(dataPath("sample_720p.mp4"))));
    QApplication::processEvents();

    QLabel* video = nullptr;
    for (QLabel* label : window.findChildren<QLabel*>())
    {
        if (label->toolTip().contains("fullscreen"))
            video = label;
    }
    ASSERT_NE(video, nullptr);

    QTest::mouseDClick(video, Qt::LeftButton, Qt::NoModifier, video->rect().center());
    QApplication::processEvents();

    QWidget* full = fullscreenWindow(window);
    ASSERT_NE(full, nullptr);
    EXPECT_TRUE(full->isVisible());

    QTest::keyClick(full, Qt::Key_Escape);
    QApplication::processEvents();
    EXPECT_FALSE(full->isVisible());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    return RUN_ALL_TESTS();
}
