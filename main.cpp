#include <QApplication>
#include <QSysInfo>
#include <cstdio>
#include <cstring>

#include "mainwindow.h"
#include "version.h"

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--version") == 0) {
      std::printf("Digital Image Analyzer %s\ncommit %s (%s)\nQt %s, %s\n",
                  DIA_VERSION, DIA_GIT_COMMIT, DIA_GIT_COMMIT_DATE, qVersion(),
                  QSysInfo::prettyProductName().toUtf8().constData());
      return 0;
    }
  }

  QApplication a(argc, argv);

  MainWindow w;
  w.show();

  return a.exec();
}
