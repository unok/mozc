// Copyright 2024 AzooKey Project.
// All rights reserved.
//
// Zenzai download dialog entry point.

#include <QApplication>

#include "gui/zenzai_download/zenzai_download_dialog.h"

#ifdef _WIN32
int RunZenzaiDownloadDialog(int argc, char *argv[]) {
  QApplication app(argc, argv);
  mozc::gui::ZenzaiDownloadDialog dialog;
  dialog.show();
  return app.exec();
}
#endif  // _WIN32
