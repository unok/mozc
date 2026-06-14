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
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace mozc {
namespace gui {

class ZenzaiDownloadDialog : public QDialog {
  Q_OBJECT;

 public:
  explicit ZenzaiDownloadDialog(QWidget *parent = nullptr);
  ~ZenzaiDownloadDialog() override;

 signals:
  void downloadProgressChanged(qint64 received, qint64 total);
  void downloadCompleted(bool success, const QString &message);

 private slots:
  void startDownload();
  void onDownloadProgress(qint64 received, qint64 total);
  void onDownloadCompleted(bool success, const QString &message);

 private:
  void setupUi();
  // ダウンロード処理本体 (ワーカースレッドで実行)。
  void downloadWorker();

  QLabel *status_label_;
  QProgressBar *progress_bar_;
  QPushButton *retry_button_;
  QPushButton *close_button_;
  QLabel *model_info_label_;

  std::string download_dir_;   // 保存先ディレクトリ (LOCALAPPDATA の models)
  std::string download_path_;  // download_dir_ + モデルファイル名
  std::unique_ptr<std::thread> download_thread_;
  std::atomic<bool> downloading_;
};

}  // namespace gui
}  // namespace mozc

#endif  // MOZC_GUI_ZENZAI_DOWNLOAD_ZENZAI_DOWNLOAD_DIALOG_H_
