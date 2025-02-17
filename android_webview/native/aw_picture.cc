// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/native/aw_picture.h"

#include "android_webview/native/java_browser_view_renderer_helper.h"
#include "base/bind.h"
#include "jni/AwPicture_jni.h"
#include "third_party/skia/include/core/SkPicture.h"

namespace android_webview {

AwPicture::AwPicture(skia::RefPtr<SkPicture> picture)
    : picture_(picture) {
  DCHECK(picture_);
}

AwPicture::~AwPicture() {}

void AwPicture::Destroy(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  delete this;
}

jint AwPicture::GetWidth(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  return picture_->cullRect().roundOut().width();
}

jint AwPicture::GetHeight(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  return picture_->cullRect().roundOut().height();
}

void AwPicture::Draw(JNIEnv* env,
                     const JavaParamRef<jobject>& obj,
                     const JavaParamRef<jobject>& canvas) {
  const SkIRect bounds = picture_->cullRect().roundOut();
  std::unique_ptr<SoftwareCanvasHolder> canvas_holder =
      SoftwareCanvasHolder::Create(canvas, gfx::Vector2d(),
                                   gfx::Size(bounds.width(), bounds.height()),
                                   false);
  if (!canvas_holder || !canvas_holder->GetCanvas()) {
    LOG(ERROR) << "Couldn't draw picture";
    return;
  }
  picture_->playback(canvas_holder->GetCanvas());
}

bool RegisterAwPicture(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace android_webview
