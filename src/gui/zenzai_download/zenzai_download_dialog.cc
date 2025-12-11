// Copyright 2024 AzooKey Project.
// All rights reserved.
//
// Zenzai model download dialog implementation.

#include "gui/zenzai_download/zenzai_download_dialog.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>

#include "converter/engine_config.h"

namespace mozc {
namespace gui {

namespace {
// Download URL for Zenzai model
constexpr const char* kZenzaiModelUrl =
    "https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf/resolve/main/"
    "ggml-model-Q5_K_M.gguf?download=true";
}  // namespace

ZenzaiDownloadDialog::ZenzaiDownloadDialog(QWidget *parent)
    : QDialog(parent),
      status_label_(nullptr),
      progress_bar_(nullptr),
      download_button_(nullptr),
      close_button_(nullptr),
      model_info_label_(nullptr),
      download_cancelled_(false) {
  setupUi();
  download_path_ = GetZenzaiModelPath();
}

ZenzaiDownloadDialog::~ZenzaiDownloadDialog() {
}

void ZenzaiDownloadDialog::setupUi() {
  setWindowTitle(tr("Zenzai Model Setup"));
  setFixedSize(550, 320);
  setWindowFlags(Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);

  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setSpacing(10);
  main_layout->setContentsMargins(20, 20, 20, 20);

  // Model info
  model_info_label_ = new QLabel(this);
  std::string model_info = "Model: " + std::string(kZenzaiModelVersion);
  if (ZenzaiModelExists()) {
    model_info += " (Installed)";
  } else {
    model_info += " (Not installed)";
  }
  model_info_label_->setText(QString::fromStdString(model_info));
  main_layout->addWidget(model_info_label_);

  // Instructions
  status_label_ = new QLabel(this);
  status_label_->setWordWrap(true);
  status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  QString instructions = tr("To enable Zenzai AI, please download the model manually:\n\n"
                           "1. Download the model file from the URL below\n"
                           "2. Place it in the folder shown below\n"
                           "3. Restart the IME\n");
  status_label_->setText(instructions);
  main_layout->addWidget(status_label_);

  // URL section
  QLabel *url_title = new QLabel(tr("Download URL:"), this);
  url_title->setStyleSheet("font-weight: bold;");
  main_layout->addWidget(url_title);

  url_label_ = new QLabel(this);
  url_label_->setText(QString::fromUtf8(kZenzaiModelUrl));
  url_label_->setWordWrap(true);
  url_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  url_label_->setStyleSheet("background-color: #f0f0f0; padding: 5px; border: 1px solid #ccc;");
  main_layout->addWidget(url_label_);

  // Path section
  QLabel *path_title = new QLabel(tr("Save to:"), this);
  path_title->setStyleSheet("font-weight: bold;");
  main_layout->addWidget(path_title);

  path_label_ = new QLabel(this);
  path_label_->setText(QString::fromStdString(GetZenzaiModelPath()));
  path_label_->setWordWrap(true);
  path_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  path_label_->setStyleSheet("background-color: #f0f0f0; padding: 5px; border: 1px solid #ccc;");
  main_layout->addWidget(path_label_);

  // Spacer
  main_layout->addStretch();

  // Buttons
  QHBoxLayout *button_layout = new QHBoxLayout();

  // Open URL button
  download_button_ = new QPushButton(tr("Open Download Page"), this);
  connect(download_button_, &QPushButton::clicked, this, &ZenzaiDownloadDialog::startDownload);
  button_layout->addWidget(download_button_);

  // Open folder button
  QPushButton *open_folder_button = new QPushButton(tr("Open Folder"), this);
  connect(open_folder_button, &QPushButton::clicked, this, &ZenzaiDownloadDialog::openFolder);
  button_layout->addWidget(open_folder_button);

  // Copy URL button
  QPushButton *copy_url_button = new QPushButton(tr("Copy URL"), this);
  connect(copy_url_button, &QPushButton::clicked, this, &ZenzaiDownloadDialog::copyUrl);
  button_layout->addWidget(copy_url_button);

  button_layout->addStretch();

  close_button_ = new QPushButton(tr("Close"), this);
  connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);
  button_layout->addWidget(close_button_);

  main_layout->addLayout(button_layout);

  // Hide unused progress bar
  progress_bar_ = new QProgressBar(this);
  progress_bar_->setVisible(false);
}

void ZenzaiDownloadDialog::startDownload() {
  // Open URL in default browser
#ifdef _WIN32
  ShellExecuteA(nullptr, "open", kZenzaiModelUrl, nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void ZenzaiDownloadDialog::openFolder() {
#ifdef _WIN32
  std::string dir = GetZenzaiModelDirectory();

  // Create directory if it doesn't exist
  QString dir_path = QString::fromStdString(dir);
  dir_path = QDir::toNativeSeparators(dir_path);

  QDir qdir;
  if (!qdir.exists(dir_path)) {
    qdir.mkpath(dir_path);
  }

  // Try Windows API as fallback
  if (!qdir.exists(dir_path)) {
    std::wstring wide_path = dir_path.toStdWString();
    SHCreateDirectoryExW(nullptr, wide_path.c_str(), nullptr);
  }

  // Open folder in Explorer
  ShellExecuteW(nullptr, L"open", dir_path.toStdWString().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void ZenzaiDownloadDialog::copyUrl() {
  QClipboard *clipboard = QApplication::clipboard();
  clipboard->setText(QString::fromUtf8(kZenzaiModelUrl));
  QMessageBox::information(this, tr("Copied"), tr("URL copied to clipboard."));
}

bool ZenzaiDownloadDialog::ensureDirectoryExists(const std::string& path) {
  return true;  // Not used in this version
}

void ZenzaiDownloadDialog::downloadThread() {
  // Not used in this version
}

void ZenzaiDownloadDialog::onDownloadProgress(int percent) {
  // Not used in this version
}

void ZenzaiDownloadDialog::onDownloadCompleted(bool success, const QString &message) {
  // Not used in this version
}

}  // namespace gui
}  // namespace mozc
