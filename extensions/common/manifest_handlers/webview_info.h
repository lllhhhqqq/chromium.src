// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_MANIFEST_HANDLERS_WEBVIEW_INFO_H_
#define EXTENSIONS_COMMON_MANIFEST_HANDLERS_WEBVIEW_INFO_H_

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handler.h"

namespace extensions {

class PartitionItem;

// A class to hold the <webview> accessible extension resources
// that may be specified in the manifest of an extension using the
// "webview" key.
class WebviewInfo : public Extension::ManifestData {
 public:
  // Returns true if |extension|'s resource at |relative_path| is accessible
  // from the WebView partition with ID |partition_id|.
  static bool IsResourceWebviewAccessible(const Extension* extension,
                                          const std::string& partition_id,
                                          const std::string& relative_path);
  static bool IsURLWebviewAccessible(const Extension* extension,
                                     const std::string& partition_id,
                                     const GURL& url,
                                     bool* file_scheme = nullptr);

  // Define out of line constructor/destructor to please Clang.
  WebviewInfo(const std::string& extension_id);
  ~WebviewInfo() override;

  void AddPartitionItem(scoped_ptr<PartitionItem> item);

 private:
  std::string extension_id_;
  std::vector<scoped_ptr<PartitionItem>> partition_items_;

  DISALLOW_COPY_AND_ASSIGN(WebviewInfo);
};

// Parses the "webview" manifest key.
class WebviewHandler : public ManifestHandler {
 public:
  WebviewHandler();
  ~WebviewHandler() override;

  bool Parse(Extension* extension, base::string16* error) override;

 private:
  const std::vector<std::string> Keys() const override;

  DISALLOW_COPY_AND_ASSIGN(WebviewHandler);
};

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_MANIFEST_HANDLERS_WEBVIEW_INFO_H_
