// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/download/chrome_download_manager_overwrite_infobar_delegate.h"

#include <memory>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/files/file_util.h"
#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/ui/android/infobars/download_overwrite_infobar.h"
#include "components/infobars/core/infobar.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"

namespace {

void DeleteExistingDownloadFile(
    const base::FilePath& download_path,
    const DownloadTargetDeterminerDelegate::FileSelectedCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::FILE);
  base::File::Info info;
  if (GetFileInfo(download_path, &info) && !info.is_directory)
    base::DeleteFile(download_path, false);

  content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
                                   base::Bind(callback, download_path));
}

}  // namespace

namespace chrome {
namespace android {

ChromeDownloadManagerOverwriteInfoBarDelegate::
    ~ChromeDownloadManagerOverwriteInfoBarDelegate() {
}

// static
void ChromeDownloadManagerOverwriteInfoBarDelegate::Create(
    InfoBarService* infobar_service,
    const base::FilePath& suggested_path,
    const DownloadTargetDeterminerDelegate::FileSelectedCallback& callback) {
  infobar_service->AddInfoBar(DownloadOverwriteInfoBar::CreateInfoBar(
      base::WrapUnique(new ChromeDownloadManagerOverwriteInfoBarDelegate(
          suggested_path, callback))));
}

ChromeDownloadManagerOverwriteInfoBarDelegate::
    ChromeDownloadManagerOverwriteInfoBarDelegate(
        const base::FilePath& suggested_download_path,
        const DownloadTargetDeterminerDelegate::FileSelectedCallback&
            file_selected_callback)
    : suggested_download_path_(suggested_download_path),
      file_selected_callback_(file_selected_callback) {
}

infobars::InfoBarDelegate::InfoBarIdentifier
ChromeDownloadManagerOverwriteInfoBarDelegate::GetIdentifier() const {
  return CHROME_DOWNLOAD_MANAGER_OVERWRITE_INFOBAR_DELEGATE;
}

bool ChromeDownloadManagerOverwriteInfoBarDelegate::OverwriteExistingFile() {
  content::BrowserThread::PostTask(
      content::BrowserThread::FILE, FROM_HERE,
      base::Bind(&DeleteExistingDownloadFile, suggested_download_path_,
                 file_selected_callback_));
  return true;
}

bool ChromeDownloadManagerOverwriteInfoBarDelegate::CreateNewFile() {
  content::BrowserThread::PostTask(
      content::BrowserThread::FILE, FROM_HERE,
      base::Bind(
          &ChromeDownloadManagerOverwriteInfoBarDelegate::CreateNewFileInternal,
          suggested_download_path_, file_selected_callback_));
  return true;
}

std::string ChromeDownloadManagerOverwriteInfoBarDelegate::GetFileName() const {
  return suggested_download_path_.BaseName().value();
}

std::string ChromeDownloadManagerOverwriteInfoBarDelegate::GetDirName() const {
  return suggested_download_path_.DirName().BaseName().value();
}

std::string ChromeDownloadManagerOverwriteInfoBarDelegate::GetDirFullPath()
    const {
  return suggested_download_path_.DirName().value();
}

void ChromeDownloadManagerOverwriteInfoBarDelegate::InfoBarDismissed() {
  file_selected_callback_.Run(base::FilePath());
}

void ChromeDownloadManagerOverwriteInfoBarDelegate::CreateNewFileInternal(
    const base::FilePath& suggested_download_path,
    const DownloadTargetDeterminerDelegate::FileSelectedCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::FILE);
  int uniquifier = base::GetUniquePathNumber(suggested_download_path,
                                             base::FilePath::StringType());
  base::FilePath new_path = suggested_download_path;
  if (uniquifier > 0) {
    new_path = suggested_download_path.InsertBeforeExtensionASCII(
        base::StringPrintf(" (%d)", uniquifier));
  }
  content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
                                   base::Bind(callback, new_path));
}

}  // namespace android
}  // namespace chrome
