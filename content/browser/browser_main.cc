// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/browser_main.h"

#include "base/memory/scoped_ptr.h"
#include "base/trace_event/trace_event.h"
#include "content/common/content_constants_internal.h"
#include "content/public/browser/browser_main_runner.h"

namespace content {

namespace {

// Generates a pair of BrowserMain async events. We don't use the TRACE_EVENT0
// macro because the tracing infrastructure doesn't expect synchronous events
// around the main loop of a thread.
class ScopedBrowserMainEvent {
 public:
  ScopedBrowserMainEvent() {
    TRACE_EVENT_ASYNC_BEGIN0("startup", "BrowserMain", 0);
  }
  ~ScopedBrowserMainEvent() {
    TRACE_EVENT_ASYNC_END0("startup", "BrowserMain", 0);
  }
};

}  // namespace

// Main routine for running as the Browser process.
int BrowserMain(const MainFunctionParams& parameters) {
  ScopedBrowserMainEvent scoped_browser_main_event;

  base::trace_event::TraceLog::GetInstance()->SetProcessName("Browser");
  base::trace_event::TraceLog::GetInstance()->SetProcessSortIndex(
      kTraceEventBrowserProcessSortIndex);

  scoped_ptr<BrowserMainRunner> main_runner(BrowserMainRunner::Create());

  int exit_code = main_runner->Initialize(parameters);
  if (exit_code >= 0)
    return exit_code;

  exit_code = main_runner->Run();

  main_runner->Shutdown();

  return exit_code;
}

}  // namespace content
