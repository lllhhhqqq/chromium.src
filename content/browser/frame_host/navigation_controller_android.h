// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FRAME_HOST_NAVIGATION_CONTROLLER_ANDROID_H_
#define CONTENT_BROWSER_FRAME_HOST_NAVIGATION_CONTROLLER_ANDROID_H_

#include <jni.h>

#include "base/android/scoped_java_ref.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "content/common/content_export.h"

namespace content {

class NavigationController;

// Android wrapper around NavigationController that provides safer passage
// from java and back to native and provides java with a means of communicating
// with its native counterpart.
class CONTENT_EXPORT NavigationControllerAndroid {
 public:
  static bool Register(JNIEnv* env);

  explicit NavigationControllerAndroid(
      NavigationController* navigation_controller);
  ~NavigationControllerAndroid();

  NavigationController* navigation_controller() const {
    return navigation_controller_;
  }

  base::android::ScopedJavaLocalRef<jobject> GetJavaObject();

  jboolean CanGoBack(JNIEnv* env,
                     const base::android::JavaParamRef<jobject>& obj);
  jboolean CanGoForward(JNIEnv* env,
                        const base::android::JavaParamRef<jobject>& obj);
  jboolean CanGoToOffset(JNIEnv* env,
                         const base::android::JavaParamRef<jobject>& obj,
                         jint offset);
  void GoBack(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void GoForward(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void GoToOffset(JNIEnv* env,
                  const base::android::JavaParamRef<jobject>& obj,
                  jint offset);
  jboolean IsInitialNavigation(JNIEnv* env,
                               const base::android::JavaParamRef<jobject>& obj);
  void LoadIfNecessary(JNIEnv* env,
                       const base::android::JavaParamRef<jobject>& obj);
  void ContinuePendingReload(JNIEnv* env,
                             const base::android::JavaParamRef<jobject>& obj);
  void Reload(JNIEnv* env,
              const base::android::JavaParamRef<jobject>& obj,
              jboolean check_for_repost);
  void ReloadToRefreshContent(JNIEnv* env,
                              jobject obj,
                              jboolean check_for_repost);
  void ReloadBypassingCache(JNIEnv* env,
                            const base::android::JavaParamRef<jobject>& obj,
                            jboolean check_for_repost);
  void ReloadDisableLoFi(JNIEnv* env,
                         const base::android::JavaParamRef<jobject>& obj,
                         jboolean check_for_repost);
  void RequestRestoreLoad(JNIEnv* env,
                          const base::android::JavaParamRef<jobject>& obj);
  void CancelPendingReload(JNIEnv* env,
                           const base::android::JavaParamRef<jobject>& obj);
  void GoToNavigationIndex(JNIEnv* env,
                           const base::android::JavaParamRef<jobject>& obj,
                           jint index);
  void LoadUrl(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jstring>& url,
      jint load_url_type,
      jint transition_type,
      const base::android::JavaParamRef<jstring>& j_referrer_url,
      jint referrer_policy,
      jint ua_override_option,
      const base::android::JavaParamRef<jstring>& extra_headers,
      const base::android::JavaParamRef<jbyteArray>& post_data,
      const base::android::JavaParamRef<jstring>& base_url_for_data_url,
      const base::android::JavaParamRef<jstring>& virtual_url_for_data_url,
      const base::android::JavaParamRef<jstring>& data_url_as_string,
      jboolean can_load_local_resources,
      jboolean is_renderer_initiated,
      jboolean should_replace_current_entry);
  void ClearSslPreferences(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& /* obj */);
  bool GetUseDesktopUserAgent(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& /* obj */);
  void SetUseDesktopUserAgent(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& /* obj */,
      jboolean state,
      jboolean reload_on_state_change);
  base::android::ScopedJavaLocalRef<jobject> GetEntryAtIndex(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      int index);
  base::android::ScopedJavaLocalRef<jobject> GetPendingEntry(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& /* obj */);
  int GetNavigationHistory(JNIEnv* env,
                           const base::android::JavaParamRef<jobject>& obj,
                           const base::android::JavaParamRef<jobject>& history);
  void GetDirectedNavigationHistory(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jobject>& history,
      jboolean is_forward,
      jint max_entries);
  base::android::ScopedJavaLocalRef<jstring>
  GetOriginalUrlForVisibleNavigationEntry(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj);
  void ClearHistory(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj);
  int GetLastCommittedEntryIndex(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj);
  jboolean RemoveEntryAtIndex(JNIEnv* env,
                              const base::android::JavaParamRef<jobject>& obj,
                              jint index);
  jboolean CanCopyStateOver(JNIEnv* env,
                            const base::android::JavaParamRef<jobject>& obj);
  jboolean CanPruneAllButLastCommitted(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj);
  void CopyStateFrom(JNIEnv* env,
                     const base::android::JavaParamRef<jobject>& obj,
                     jlong source_native_navigation_controller_android);
  void CopyStateFromAndPrune(JNIEnv* env,
                             const base::android::JavaParamRef<jobject>& obj,
                             jlong source_native_navigation_controller_android,
                             jboolean replace_entry);

 private:
  NavigationController* navigation_controller_;
  base::android::ScopedJavaGlobalRef<jobject> obj_;

  DISALLOW_COPY_AND_ASSIGN(NavigationControllerAndroid);
};

}  // namespace content

#endif  // CONTENT_BROWSER_FRAME_HOST_NAVIGATION_CONTROLLER_ANDROID_H_
