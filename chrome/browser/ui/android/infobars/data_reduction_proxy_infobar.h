// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ANDROID_INFOBARS_DATA_REDUCTION_PROXY_INFOBAR_H_
#define CHROME_BROWSER_UI_ANDROID_INFOBARS_DATA_REDUCTION_PROXY_INFOBAR_H_

#include "base/macros.h"
#include "base/strings/string16.h"
#include "chrome/browser/net/spdyproxy/data_reduction_proxy_infobar_delegate_android.h"
#include "chrome/browser/ui/android/infobars/confirm_infobar.h"

class DataReductionProxyInfoBar : public ConfirmInfoBar {
 public:
  static void Launch(JNIEnv* env,
                     jclass,
                     jobject jweb_contents,
                     jstring jlink_url);

  static bool Register(JNIEnv* env);

  explicit DataReductionProxyInfoBar(
      std::unique_ptr<DataReductionProxyInfoBarDelegateAndroid> delegate);

  ~DataReductionProxyInfoBar() override;

 private:
  // ConfirmInfoBar:
  base::android::ScopedJavaLocalRef<jobject> CreateRenderInfoBar(
      JNIEnv* env) override;

  DataReductionProxyInfoBarDelegateAndroid* GetDelegate();

  base::android::ScopedJavaGlobalRef<jobject>
  java_data_reduction_proxy_delegate_;

  DISALLOW_COPY_AND_ASSIGN(DataReductionProxyInfoBar);
};

bool RegisterDataReductionProxyInfoBar(JNIEnv* env);

#endif  // CHROME_BROWSER_UI_ANDROID_INFOBARS_DATA_REDUCTION_PROXY_INFOBAR_H_
