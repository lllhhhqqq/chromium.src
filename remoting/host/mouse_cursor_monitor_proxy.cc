// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/mouse_cursor_monitor_proxy.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/single_thread_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_frame.h"
#include "third_party/webrtc/modules/desktop_capture/mouse_cursor.h"
#include "third_party/webrtc/modules/desktop_capture/mouse_cursor_monitor.h"

namespace remoting {

class MouseCursorMonitorProxy::Core
    : public webrtc::MouseCursorMonitor::Callback {
 public:
  Core(base::WeakPtr<MouseCursorMonitorProxy> proxy,
       scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner,
       std::unique_ptr<webrtc::MouseCursorMonitor> mouse_cursor_monitor);
  ~Core() override;

  void Init(webrtc::MouseCursorMonitor::Mode mode);
  void Capture();

 private:
  // webrtc::MouseCursorMonitor::Callback implementation.
  void OnMouseCursor(webrtc::MouseCursor* mouse_cursor) override;
  void OnMouseCursorPosition(webrtc::MouseCursorMonitor::CursorState state,
                             const webrtc::DesktopVector& position) override;

  base::ThreadChecker thread_checker_;

  base::WeakPtr<MouseCursorMonitorProxy> proxy_;
  scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner_;
  std::unique_ptr<webrtc::MouseCursorMonitor> mouse_cursor_monitor_;

  DISALLOW_COPY_AND_ASSIGN(Core);
};

MouseCursorMonitorProxy::Core::Core(
    base::WeakPtr<MouseCursorMonitorProxy> proxy,
    scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner,
    std::unique_ptr<webrtc::MouseCursorMonitor> mouse_cursor_monitor)
    : proxy_(proxy),
      caller_task_runner_(caller_task_runner),
      mouse_cursor_monitor_(std::move(mouse_cursor_monitor)) {
  thread_checker_.DetachFromThread();
}

MouseCursorMonitorProxy::Core::~Core() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void MouseCursorMonitorProxy::Core::Init(
    webrtc::MouseCursorMonitor::Mode mode) {
  DCHECK(thread_checker_.CalledOnValidThread());

  mouse_cursor_monitor_->Init(this, mode);
}

void MouseCursorMonitorProxy::Core::Capture() {
  DCHECK(thread_checker_.CalledOnValidThread());

  mouse_cursor_monitor_->Capture();
}

void MouseCursorMonitorProxy::Core::OnMouseCursor(webrtc::MouseCursor* cursor) {
  DCHECK(thread_checker_.CalledOnValidThread());

  std::unique_ptr<webrtc::MouseCursor> owned_cursor(cursor);
  caller_task_runner_->PostTask(
      FROM_HERE, base::Bind(&MouseCursorMonitorProxy::OnMouseCursor, proxy_,
                            base::Passed(&owned_cursor)));
}

void MouseCursorMonitorProxy::Core::OnMouseCursorPosition(
    webrtc::MouseCursorMonitor::CursorState state,
    const webrtc::DesktopVector& position) {
  DCHECK(thread_checker_.CalledOnValidThread());

  caller_task_runner_->PostTask(
      FROM_HERE, base::Bind(&MouseCursorMonitorProxy::OnMouseCursorPosition,
                            proxy_, state, position));
}

MouseCursorMonitorProxy::MouseCursorMonitorProxy(
    scoped_refptr<base::SingleThreadTaskRunner> capture_task_runner,
    std::unique_ptr<webrtc::MouseCursorMonitor> mouse_cursor_monitor)
    : capture_task_runner_(capture_task_runner), weak_factory_(this) {
  core_.reset(new Core(weak_factory_.GetWeakPtr(),
                       base::ThreadTaskRunnerHandle::Get(),
                       std::move(mouse_cursor_monitor)));
}

MouseCursorMonitorProxy::~MouseCursorMonitorProxy() {
  capture_task_runner_->DeleteSoon(FROM_HERE, core_.release());
}

void MouseCursorMonitorProxy::Init(Callback* callback, Mode mode) {
  DCHECK(thread_checker_.CalledOnValidThread());
  callback_ = callback;
  capture_task_runner_->PostTask(
      FROM_HERE, base::Bind(&Core::Init, base::Unretained(core_.get()), mode));
}

void MouseCursorMonitorProxy::Capture() {
  DCHECK(thread_checker_.CalledOnValidThread());
  capture_task_runner_->PostTask(
      FROM_HERE, base::Bind(&Core::Capture, base::Unretained(core_.get())));
}

void MouseCursorMonitorProxy::OnMouseCursor(
    std::unique_ptr<webrtc::MouseCursor> cursor) {
  DCHECK(thread_checker_.CalledOnValidThread());
  callback_->OnMouseCursor(cursor.release());
}

void MouseCursorMonitorProxy::OnMouseCursorPosition(
    CursorState state,
    const webrtc::DesktopVector& position) {
  DCHECK(thread_checker_.CalledOnValidThread());
  callback_->OnMouseCursorPosition(state, position);
}

}  // namespace remoting
