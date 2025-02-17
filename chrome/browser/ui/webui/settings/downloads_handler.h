// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_DOWNLOADS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_DOWNLOADS_HANDLER_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"
#include "ui/shell_dialogs/select_file_dialog.h"

namespace base {
class Value;
}

namespace settings {

// Chrome "Downloads" settings page UI handler.
class DownloadsHandler : public SettingsPageUIHandler,
                         public ui::SelectFileDialog::Listener {
 public:
  DownloadsHandler();
  ~DownloadsHandler() override;

  // SettingsPageUIHandler implementation.
  void RegisterMessages() override;

 private:
  // SelectFileDialog::Listener implementation.
  void FileSelected(const base::FilePath& path,
                    int index,
                    void* params) override;

  // Callback for the "selectDownloadLocation" message. This will prompt the
  // user for a destination folder using platform-specific APIs.
  void HandleSelectDownloadLocation(const base::ListValue* args);

  scoped_refptr<ui::SelectFileDialog> select_folder_dialog_;

  DISALLOW_COPY_AND_ASSIGN(DownloadsHandler);
};

}  // namespace settings

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_DOWNLOADS_HANDLER_H_
