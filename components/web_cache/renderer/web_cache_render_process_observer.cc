// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/web_cache/renderer/web_cache_render_process_observer.h"

#include <limits>

#include "base/bind.h"
#include "base/numerics/safe_conversions.h"
#include "content/public/common/service_registry.h"
#include "content/public/renderer/render_thread.h"
#include "third_party/WebKit/public/web/WebCache.h"

namespace web_cache {

WebCacheRenderProcessObserver::WebCacheRenderProcessObserver()
    : clear_cache_state_(kInit) {
  content::ServiceRegistry* service_registry =
      content::RenderThread::Get()->GetServiceRegistry();
  service_registry->AddService(base::Bind(
      &WebCacheRenderProcessObserver::BindRequest, base::Unretained(this)));
}

WebCacheRenderProcessObserver::~WebCacheRenderProcessObserver() {
}

void WebCacheRenderProcessObserver::BindRequest(
    mojo::InterfaceRequest<mojom::WebCache> web_cache_request) {
  bindings_.AddBinding(this, std::move(web_cache_request));
}

void WebCacheRenderProcessObserver::ExecutePendingClearCache() {
  switch (clear_cache_state_) {
    case kInit:
      clear_cache_state_ = kNavigate_Pending;
      break;
    case kNavigate_Pending:
      break;
    case kClearCache_Pending:
      blink::WebCache::clear();
      clear_cache_state_ = kInit;
      break;
  }
}

void WebCacheRenderProcessObserver::SetCacheCapacities(
    uint64_t min_dead_capacity,
    uint64_t max_dead_capacity,
    uint64_t capacity64) {
  size_t min_dead_capacity2 = base::checked_cast<size_t>(min_dead_capacity);
  size_t max_dead_capacity2 = base::checked_cast<size_t>(max_dead_capacity);
  size_t capacity = base::checked_cast<size_t>(capacity64);

  blink::WebCache::setCapacities(min_dead_capacity2, max_dead_capacity2,
                                 capacity);
}

void WebCacheRenderProcessObserver::ClearCache(bool on_navigation) {
  if (!on_navigation) {
    blink::WebCache::clear();
    return;
  }

  switch (clear_cache_state_) {
    case kInit:
      clear_cache_state_ = kClearCache_Pending;
      break;
    case kNavigate_Pending:
      blink::WebCache::clear();
      clear_cache_state_ = kInit;
      break;
    case kClearCache_Pending:
      break;
  }
}

}  // namespace web_cache
