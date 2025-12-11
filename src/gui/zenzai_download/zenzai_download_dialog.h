// Copyright 2024 AzooKey Project.
// All rights reserved.
//
// Zenzai model download dialog.

#ifndef MOZC_GUI_ZENZAI_DOWNLOAD_ZENZAI_DOWNLOAD_DIALOG_H_
#define MOZC_GUI_ZENZAI_DOWNLOAD_ZENZAI_DOWNLOAD_DIALOG_H_

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

namespace mozc {
namespace gui {

class ZenzaiDownloadDialog : public QDialog {
  Q_OBJECT;

 public:
  explicit ZenzaiDownloadDialog(QWidget *parent = nullptr);
  ~ZenzaiDownloadDialog() override;

 signals:
  void downloadProgressChanged(int percent);
  void downloadCompleted(bool success, const QString &message);

 private slots:
  void startDownload();
  void onDownloadProgress(int percent);
  void onDownloadCompleted(bool success, const QString &message);
  void openFolder();
  void copyUrl();

 private:
  void setupUi();
  bool ensureDirectoryExists(const std::string& path);
  void downloadThread();

  QLabel *status_label_;
  QProgressBar *progress_bar_;
  QPushButton *download_button_;
  QPushButton *close_button_;
  QLabel *model_info_label_;
  QLabel *url_label_;
  QLabel *path_label_;

  std::string download_path_;
  std::unique_ptr<std::thread> download_thread_;
  std::atomic<bool> download_cancelled_;
};

}  // namespace gui
}  // namespace mozc

#endif  // MOZC_GUI_ZENZAI_DOWNLOAD_ZENZAI_DOWNLOAD_DIALOG_H_
