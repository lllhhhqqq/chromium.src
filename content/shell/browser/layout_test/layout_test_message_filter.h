// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_LAYOUT_TEST_LAYOUT_TEST_MESSAGE_FILTER_H_
#define CONTENT_SHELL_BROWSER_LAYOUT_TEST_LAYOUT_TEST_MESSAGE_FILTER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/macros.h"
#include "content/public/browser/browser_message_filter.h"
#include "third_party/WebKit/public/platform/modules/permissions/permission_status.mojom.h"

class GURL;

namespace net {
class URLRequestContextGetter;
}

namespace storage {
class QuotaManager;
}

namespace storage {
class DatabaseTracker;
}

namespace content {

class LayoutTestMessageFilter : public BrowserMessageFilter {
 public:
  LayoutTestMessageFilter(int render_process_id,
                     storage::DatabaseTracker* database_tracker,
                     storage::QuotaManager* quota_manager,
                     net::URLRequestContextGetter* request_context_getter);

 private:
  ~LayoutTestMessageFilter() override;

  // BrowserMessageFilter implementation.
  void OverrideThreadForMessage(const IPC::Message& message,
                                BrowserThread::ID* thread) override;
  bool OnMessageReceived(const IPC::Message& message) override;

  void OnReadFileToString(const base::FilePath& local_file,
                          std::string* contents);
  void OnRegisterIsolatedFileSystem(
      const std::vector<base::FilePath>& absolute_filenames,
      std::string* filesystem_id);
  void OnClearAllDatabases();
  void OnSetDatabaseQuota(int quota);
  void OnGrantWebNotificationPermission(const GURL& origin,
                                        bool permission_granted);
  void OnClearWebNotificationPermissions();
  void OnSimulateWebNotificationClick(const std::string& title,
                                      int action_index);
  void OnSimulateWebNotificationClose(const std::string& title, bool by_user);
  void OnSetPushMessagingPermission(const GURL& origin, bool allowed);
  void OnClearPushMessagingPermissions();
  void OnAcceptAllCookies(bool accept);
  void OnDeleteAllCookies();
  void OnSetPermission(const std::string& name,
                       blink::mojom::PermissionStatus status,
                       const GURL& origin,
                       const GURL& embedding_origin);
  void OnResetPermissions();

  int render_process_id_;

  storage::DatabaseTracker* database_tracker_;
  storage::QuotaManager* quota_manager_;
  net::URLRequestContextGetter* request_context_getter_;

  DISALLOW_COPY_AND_ASSIGN(LayoutTestMessageFilter);
};

}  // namespace content

#endif  // CONTENT_SHELL_BROWSER_LAYOUT_TEST_LAYOUT_TEST_MESSAGE_FILTER_H_
