// Copyright 2024 AzooKey Project.
// All rights reserved.
//
// Zenzai model download dialog implementation.
//
// モデルをユーザー領域 (%LOCALAPPDATA%\Mozc\models) へ自動ダウンロードする。
// ユーザー権限で書き込めるため UAC/管理者権限は不要。ダウンロード後に
// サイズ・GGUFマジック・SHA256 を検証し、検証成功時のみ最終ファイルに確定する。

#include "gui/zenzai_download/zenzai_download_dialog.h"

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <shlobj.h>
#include <wininet.h>
#include <bcrypt.h>
// clang-format on
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "bcrypt.lib")
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#endif  // _WIN32

#include <QHBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "converter/engine_config.h"

namespace mozc {
namespace gui {

namespace {
// ダウンロード対象 (コミット固定 + SHA256 で内容を不変にする)。
// モデル更新時は URL / SHA256 / サイズの3つを揃えて更新すること。
constexpr const wchar_t *kZenzaiModelUrl =
    L"https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf/resolve/"
    L"c67e03e07d215c869f591b274c1631170d3e11fe/"
    L"ggml-model-Q5_K_M.gguf";
constexpr const char *kZenzaiModelSha256 =
    "29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673";
constexpr unsigned long long kZenzaiModelSize = 73871936ULL;
constexpr DWORD kDownloadBufferSize = 65536;

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string &s) {
  if (s.empty()) return std::wstring();
  const int len =
      MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(len > 0 ? len - 1 : 0, L'\0');
  if (len > 0) {
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
  }
  return w;
}
#endif  // _WIN32
}  // namespace

ZenzaiDownloadDialog::ZenzaiDownloadDialog(QWidget *parent)
    : QDialog(parent),
      status_label_(nullptr),
      progress_bar_(nullptr),
      retry_button_(nullptr),
      close_button_(nullptr),
      model_info_label_(nullptr),
      downloading_(false) {
  setupUi();

  download_dir_ = GetZenzaiModelDirectory();
  download_path_ = download_dir_ + kZenzaiModelName;

  // ワーカースレッド -> UI スレッドへはキュー接続で安全に通知する。
  connect(this, &ZenzaiDownloadDialog::downloadProgressChanged, this,
          &ZenzaiDownloadDialog::onDownloadProgress, Qt::QueuedConnection);
  connect(this, &ZenzaiDownloadDialog::downloadCompleted, this,
          &ZenzaiDownloadDialog::onDownloadCompleted, Qt::QueuedConnection);

  if (ZenzaiModelExists()) {
    status_label_->setText(tr("Zenzai モデルは既にインストールされています。"));
    progress_bar_->setVisible(false);
    retry_button_->setText(tr("再ダウンロード"));
  } else {
    // モデルが無ければ自動でダウンロードを開始する。
    QTimer::singleShot(0, this, &ZenzaiDownloadDialog::startDownload);
  }
}

ZenzaiDownloadDialog::~ZenzaiDownloadDialog() {
  if (download_thread_ && download_thread_->joinable()) {
    download_thread_->join();
  }
}

void ZenzaiDownloadDialog::setupUi() {
  setWindowTitle(tr("Zenzai モデルのセットアップ"));
  setFixedSize(520, 200);
  setWindowFlags(Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);

  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setSpacing(12);
  main_layout->setContentsMargins(20, 20, 20, 20);

  model_info_label_ = new QLabel(this);
  model_info_label_->setText(
      tr("モデル: %1").arg(QString::fromUtf8(kZenzaiModelVersion)));
  main_layout->addWidget(model_info_label_);

  status_label_ = new QLabel(this);
  status_label_->setWordWrap(true);
  status_label_->setText(tr("Zenzai AI モデルをダウンロードしています..."));
  main_layout->addWidget(status_label_);

  progress_bar_ = new QProgressBar(this);
  progress_bar_->setRange(0, 100);
  progress_bar_->setValue(0);
  main_layout->addWidget(progress_bar_);

  main_layout->addStretch();

  QHBoxLayout *button_layout = new QHBoxLayout();
  retry_button_ = new QPushButton(tr("再試行"), this);
  retry_button_->setEnabled(false);
  connect(retry_button_, &QPushButton::clicked, this,
          &ZenzaiDownloadDialog::startDownload);
  button_layout->addWidget(retry_button_);
  button_layout->addStretch();
  close_button_ = new QPushButton(tr("閉じる"), this);
  connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);
  button_layout->addWidget(close_button_);
  main_layout->addLayout(button_layout);
}

void ZenzaiDownloadDialog::startDownload() {
  if (downloading_.load()) {
    return;
  }
  // 直前のスレッドが残っていれば回収する。
  if (download_thread_ && download_thread_->joinable()) {
    download_thread_->join();
  }
  downloading_.store(true);
  retry_button_->setEnabled(false);
  progress_bar_->setVisible(true);
  progress_bar_->setValue(0);
  status_label_->setText(tr("Zenzai AI モデルをダウンロードしています..."));
  download_thread_ =
      std::make_unique<std::thread>(&ZenzaiDownloadDialog::downloadWorker, this);
}

void ZenzaiDownloadDialog::downloadWorker() {
#ifdef _WIN32
  auto fail = [this](const QString &msg) {
    Q_EMIT downloadCompleted(false, msg);
  };

  const std::wstring dir = Utf8ToWide(download_dir_);
  if (!dir.empty()) {
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  }
  const std::wstring final_path = Utf8ToWide(download_path_);
  const std::wstring part_path = final_path + L".part";

  HINTERNET hInternet =
      InternetOpenW(L"MozcZenzaiDownloader/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
                    nullptr, nullptr, 0);
  if (!hInternet) {
    fail(tr("ネットワークの初期化に失敗しました。"));
    return;
  }
  HINTERNET hUrl = InternetOpenUrlW(
      hInternet, kZenzaiModelUrl, nullptr, 0,
      INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
          INTERNET_FLAG_SECURE,
      0);
  if (!hUrl) {
    InternetCloseHandle(hInternet);
    fail(tr("ダウンロード先に接続できませんでした。"));
    return;
  }

  DWORD status = 0, sz = sizeof(status);
  if (!HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                      &status, &sz, nullptr) ||
      status != 200) {
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    fail(tr("サーバーエラー (HTTP %1)。").arg(static_cast<int>(status)));
    return;
  }
  DWORD content_length = 0, lsz = sizeof(content_length);
  if (!HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                      &content_length, &lsz, nullptr)) {
    content_length = 0;
  }
  const qint64 total =
      content_length ? content_length : static_cast<qint64>(kZenzaiModelSize);

  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCRYPT_HASH_HANDLE hHash = nullptr;
  bool hash_ok =
      NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM,
                                             nullptr, 0)) &&
      NT_SUCCESS(BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0));
  if (!hash_ok) {
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    fail(tr("ハッシュ初期化に失敗しました。"));
    return;
  }

  HANDLE hFile = CreateFileW(part_path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    fail(tr("保存先ファイルを作成できませんでした。"));
    return;
  }

  std::vector<BYTE> buffer(kDownloadBufferSize);
  unsigned long long received = 0;
  bool read_ok = true;
  BYTE magic[4] = {0};
  for (;;) {
    DWORD got = 0;
    if (!InternetReadFile(hUrl, buffer.data(), kDownloadBufferSize, &got)) {
      read_ok = false;
      break;
    }
    if (got == 0) {
      break;  // EOF
    }
    if (received < 4) {
      for (DWORD i = 0; i < got && received + i < 4; ++i) {
        magic[received + i] = buffer[i];
      }
    }
    if (!NT_SUCCESS(BCryptHashData(hHash, buffer.data(), got, 0))) {
      read_ok = false;
      break;
    }
    DWORD written = 0;
    if (!WriteFile(hFile, buffer.data(), got, &written, nullptr) ||
        written != got) {
      read_ok = false;
      break;
    }
    received += got;
    Q_EMIT downloadProgressChanged(static_cast<qint64>(received), total);
  }

  BYTE digest[32] = {0};
  bool sha_ok = false;
  if (NT_SUCCESS(BCryptFinishHash(hHash, digest, sizeof(digest), 0))) {
    char hex[65] = {0};
    for (int i = 0; i < 32; ++i) {
      sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
    }
    sha_ok = (strcmp(hex, kZenzaiModelSha256) == 0);
  }
  BCryptDestroyHash(hHash);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  CloseHandle(hFile);
  InternetCloseHandle(hUrl);
  InternetCloseHandle(hInternet);

  const bool size_ok = (received == kZenzaiModelSize);
  const bool magic_ok = (received >= 4 && magic[0] == 'G' && magic[1] == 'G' &&
                         magic[2] == 'U' && magic[3] == 'F');
  if (!read_ok || !size_ok || !magic_ok || !sha_ok) {
    DeleteFileW(part_path.c_str());
    fail(tr("ダウンロードの検証に失敗しました。再試行してください。"));
    return;
  }

  if (!MoveFileExW(part_path.c_str(), final_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(part_path.c_str());
    fail(tr("ファイルの確定に失敗しました。"));
    return;
  }

  Q_EMIT downloadCompleted(true, QString());
#else   // _WIN32
  Q_EMIT downloadCompleted(false, tr("このプラットフォームは未対応です。"));
#endif  // _WIN32
}

void ZenzaiDownloadDialog::onDownloadProgress(qint64 received, qint64 total) {
  if (total > 0) {
    progress_bar_->setValue(static_cast<int>(received * 100 / total));
  }
  status_label_->setText(tr("ダウンロード中... %1 / %2 MB")
                             .arg(received / (1024 * 1024))
                             .arg(total / (1024 * 1024)));
}

void ZenzaiDownloadDialog::onDownloadCompleted(bool success,
                                               const QString &message) {
  downloading_.store(false);
  retry_button_->setEnabled(true);
  if (success) {
    progress_bar_->setValue(100);
    status_label_->setText(
        tr("ダウンロードが完了しました。IME を再起動すると Zenzai が有効になります。"));
    model_info_label_->setText(
        tr("モデル: %1 (インストール済み)")
            .arg(QString::fromUtf8(kZenzaiModelVersion)));
  } else {
    status_label_->setText(message);
  }
}

}  // namespace gui
}  // namespace mozc
