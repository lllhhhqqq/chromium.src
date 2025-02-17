// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/run_loop.h"
#include "content/browser/media/session/media_session.h"
#include "content/browser/media/session/mock_media_session_observer.h"
#include "content/public/test/content_browser_test.h"
#include "content/shell/browser/shell.h"

namespace content {

class MediaSessionDelegateAndroidBrowserTest : public ContentBrowserTest {};

// MAYBE_OnAudioFocusChangeAfterDtorCrash will hit a DCHECK before the crash, it
// is the only way found to actually reproduce the crash so as a result, the
// test will only run on builds without DCHECK's.
// TODO(mlamouri): the test is flaky on one bot. It is disabled until the cause
// is found.
#if defined(NDEBUG) && !defined(DCHECK_ALWAYS_ON)
#define MAYBE_OnAudioFocusChangeAfterDtorCrash \
  DISABLED_OnAudioFocusChangeAfterDtorCrash
#else
#define MAYBE_OnAudioFocusChangeAfterDtorCrash \
  DISABLED_OnAudioFocusChangeAfterDtorCrash
#endif

IN_PROC_BROWSER_TEST_F(MediaSessionDelegateAndroidBrowserTest,
                       MAYBE_OnAudioFocusChangeAfterDtorCrash) {
  scoped_ptr<MockMediaSessionObserver> media_session_observer(
      new MockMediaSessionObserver);

  MediaSession* media_session = MediaSession::Get(shell()->web_contents());
  ASSERT_TRUE(media_session);

  WebContents* other_web_contents = CreateBrowser()->web_contents();
  MediaSession* other_media_session = MediaSession::Get(other_web_contents);
  ASSERT_TRUE(other_media_session);

  media_session_observer->StartNewPlayer();
  media_session->AddPlayer(media_session_observer.get(), 0,
                           MediaSession::Type::Content);
  EXPECT_TRUE(media_session->IsActive());
  EXPECT_FALSE(other_media_session->IsActive());

  media_session_observer->StartNewPlayer();
  other_media_session->AddPlayer(media_session_observer.get(), 1,
                                 MediaSession::Type::Content);
  EXPECT_TRUE(media_session->IsActive());
  EXPECT_TRUE(other_media_session->IsActive());

  shell()->CloseAllWindows();

  // Give some time to the AudioFocusManager to send an audioFocusChange message
  // to the listeners. If the bug is still present, it will crash.
  {
    base::RunLoop run_loop;
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::TimeDelta::FromSeconds(1));
    run_loop.Run();
  }
}

}  // namespace content
