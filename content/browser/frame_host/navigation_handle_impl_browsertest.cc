// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/frame_host/navigation_handle_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "ui/base/page_transition_types.h"
#include "url/url_constants.h"

namespace content {

namespace {

class NavigationHandleObserver : public WebContentsObserver {
 public:
  NavigationHandleObserver(WebContents* web_contents, const GURL& expected_url)
      : WebContentsObserver(web_contents),
        handle_(nullptr),
        has_committed_(false),
        is_error_(false),
        is_main_frame_(false),
        is_parent_main_frame_(false),
        is_synchronous_(false),
        is_srcdoc_(false),
        was_redirected_(false),
        frame_tree_node_id_(-1),
        page_transition_(ui::PAGE_TRANSITION_LINK),
        expected_url_(expected_url) {}

  void DidStartNavigation(NavigationHandle* navigation_handle) override {
    if (handle_ || navigation_handle->GetURL() != expected_url_)
      return;

    handle_ = navigation_handle;
    has_committed_ = false;
    is_error_ = false;
    page_transition_ = ui::PAGE_TRANSITION_LINK;
    last_committed_url_ = GURL();

    is_main_frame_ = navigation_handle->IsInMainFrame();
    is_parent_main_frame_ = navigation_handle->IsParentMainFrame();
    is_synchronous_ = navigation_handle->IsSynchronousNavigation();
    is_srcdoc_ = navigation_handle->IsSrcdoc();
    was_redirected_ = navigation_handle->WasServerRedirect();
    frame_tree_node_id_ = navigation_handle->GetFrameTreeNodeId();
  }

  void DidFinishNavigation(NavigationHandle* navigation_handle) override {
    if (navigation_handle != handle_)
      return;

    DCHECK_EQ(is_main_frame_, navigation_handle->IsInMainFrame());
    DCHECK_EQ(is_parent_main_frame_, navigation_handle->IsParentMainFrame());
    DCHECK_EQ(is_synchronous_, navigation_handle->IsSynchronousNavigation());
    DCHECK_EQ(is_srcdoc_, navigation_handle->IsSrcdoc());
    DCHECK_EQ(frame_tree_node_id_, navigation_handle->GetFrameTreeNodeId());

    was_redirected_ = navigation_handle->WasServerRedirect();

    if (navigation_handle->HasCommitted()) {
      has_committed_ = true;
      if (!navigation_handle->IsErrorPage()) {
        page_transition_ = navigation_handle->GetPageTransition();
        last_committed_url_ = navigation_handle->GetURL();
      } else {
        is_error_ = true;
      }
    } else {
      has_committed_ = false;
      is_error_ = true;
    }

    handle_ = nullptr;
  }

  bool has_committed() { return has_committed_; }
  bool is_error() { return is_error_; }
  bool is_main_frame() { return is_main_frame_; }
  bool is_parent_main_frame() { return is_parent_main_frame_; }
  bool is_synchronous() { return is_synchronous_; }
  bool is_srcdoc() { return is_srcdoc_; }
  bool was_redirected() { return was_redirected_; }
  int frame_tree_node_id() { return frame_tree_node_id_; }

  const GURL& last_committed_url() { return last_committed_url_; }

  ui::PageTransition page_transition() { return page_transition_; }

 private:
  // A reference to the NavigationHandle so this class will track only
  // one navigation at a time. It is set at DidStartNavigation and cleared
  // at DidFinishNavigation before the NavigationHandle is destroyed.
  NavigationHandle* handle_;
  bool has_committed_;
  bool is_error_;
  bool is_main_frame_;
  bool is_parent_main_frame_;
  bool is_synchronous_;
  bool is_srcdoc_;
  bool was_redirected_;
  int frame_tree_node_id_;
  ui::PageTransition page_transition_;
  GURL expected_url_;
  GURL last_committed_url_;
};

// A test NavigationThrottle that will return pre-determined checks and run
// callbacks when the various NavigationThrottle methods are called.
class TestNavigationThrottle : public NavigationThrottle {
 public:
  TestNavigationThrottle(
      NavigationHandle* handle,
      NavigationThrottle::ThrottleCheckResult will_start_result,
      NavigationThrottle::ThrottleCheckResult will_redirect_result,
      NavigationThrottle::ThrottleCheckResult will_process_result,
      base::Closure did_call_will_start,
      base::Closure did_call_will_redirect,
      base::Closure did_call_will_process)
      : NavigationThrottle(handle),
        will_start_result_(will_start_result),
        will_redirect_result_(will_redirect_result),
        will_process_result_(will_process_result),
        did_call_will_start_(did_call_will_start),
        did_call_will_redirect_(did_call_will_redirect),
        did_call_will_process_(did_call_will_process) {}
  ~TestNavigationThrottle() override {}

  void Resume() { navigation_handle()->Resume(); }

 private:
  // NavigationThrottle implementation.
  NavigationThrottle::ThrottleCheckResult WillStartRequest() override {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, did_call_will_start_);
    return will_start_result_;
  }

  NavigationThrottle::ThrottleCheckResult WillRedirectRequest() override {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            did_call_will_redirect_);
    return will_redirect_result_;
  }

  NavigationThrottle::ThrottleCheckResult WillProcessResponse() override {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            did_call_will_process_);
    return will_process_result_;
  }

  NavigationThrottle::ThrottleCheckResult will_start_result_;
  NavigationThrottle::ThrottleCheckResult will_redirect_result_;
  NavigationThrottle::ThrottleCheckResult will_process_result_;
  base::Closure did_call_will_start_;
  base::Closure did_call_will_redirect_;
  base::Closure did_call_will_process_;
};

// Install a TestNavigationThrottle on all requests and allows waiting for
// various NavigationThrottle related events.
class TestNavigationThrottleInstaller : public WebContentsObserver {
 public:
  TestNavigationThrottleInstaller(
      WebContents* web_contents,
      NavigationThrottle::ThrottleCheckResult will_start_result,
      NavigationThrottle::ThrottleCheckResult will_redirect_result,
      NavigationThrottle::ThrottleCheckResult will_process_result)
      : WebContentsObserver(web_contents),
        will_start_result_(will_start_result),
        will_redirect_result_(will_redirect_result),
        will_process_result_(will_process_result),
        will_start_called_(0),
        will_redirect_called_(0),
        will_process_called_(0),
        navigation_throttle_(nullptr) {}
  ~TestNavigationThrottleInstaller() override{};

  TestNavigationThrottle* navigation_throttle() { return navigation_throttle_; }

  void WaitForThrottleWillStart() {
    if (will_start_called_)
      return;
    will_start_loop_runner_ = new MessageLoopRunner();
    will_start_loop_runner_->Run();
    will_start_loop_runner_ = nullptr;
  }

  void WaitForThrottleWillRedirect() {
    if (will_redirect_called_)
      return;
    will_redirect_loop_runner_ = new MessageLoopRunner();
    will_redirect_loop_runner_->Run();
    will_redirect_loop_runner_ = nullptr;
  }

  void WaitForThrottleWillProcess() {
    if (will_process_called_)
      return;
    will_process_loop_runner_ = new MessageLoopRunner();
    will_process_loop_runner_->Run();
    will_process_loop_runner_ = nullptr;
  }

  int will_start_called() { return will_start_called_; }
  int will_redirect_called() { return will_redirect_called_; }
  int will_process_called() { return will_process_called_; }

 private:
  void DidStartNavigation(NavigationHandle* handle) override {
    scoped_ptr<NavigationThrottle> throttle(new TestNavigationThrottle(
        handle, will_start_result_, will_redirect_result_, will_process_result_,
        base::Bind(&TestNavigationThrottleInstaller::DidCallWillStartRequest,
                   base::Unretained(this)),
        base::Bind(&TestNavigationThrottleInstaller::DidCallWillRedirectRequest,
                   base::Unretained(this)),
        base::Bind(&TestNavigationThrottleInstaller::DidCallWillProcessResponse,
                   base::Unretained(this))));
    navigation_throttle_ = static_cast<TestNavigationThrottle*>(throttle.get());
    handle->RegisterThrottleForTesting(std::move(throttle));
  }

  void DidFinishNavigation(NavigationHandle* handle) override {
    if (!navigation_throttle_)
      return;

    if (handle == navigation_throttle_->navigation_handle())
      navigation_throttle_ = nullptr;
  }

  void DidCallWillStartRequest() {
    will_start_called_++;
    if (will_start_loop_runner_)
      will_start_loop_runner_->Quit();
  }

  void DidCallWillRedirectRequest() {
    will_redirect_called_++;
    if (will_redirect_loop_runner_)
      will_redirect_loop_runner_->Quit();
  }

  void DidCallWillProcessResponse() {
    will_process_called_++;
    if (will_process_loop_runner_)
      will_process_loop_runner_->Quit();
  }

  NavigationThrottle::ThrottleCheckResult will_start_result_;
  NavigationThrottle::ThrottleCheckResult will_redirect_result_;
  NavigationThrottle::ThrottleCheckResult will_process_result_;
  int will_start_called_;
  int will_redirect_called_;
  int will_process_called_;
  TestNavigationThrottle* navigation_throttle_;
  scoped_refptr<MessageLoopRunner> will_start_loop_runner_;
  scoped_refptr<MessageLoopRunner> will_redirect_loop_runner_;
  scoped_refptr<MessageLoopRunner> will_process_loop_runner_;
};

}  // namespace

class NavigationHandleImplBrowserTest : public ContentBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    SetupCrossSiteRedirector(embedded_test_server());
  }
};

// Ensure that PageTransition is properly set on the NavigationHandle.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, VerifyPageTransition) {
  {
    // Test browser initiated navigation, which should have a PageTransition as
    // if it came from the omnibox.
    GURL url(embedded_test_server()->GetURL("/title1.html"));
    NavigationHandleObserver observer(shell()->web_contents(), url);
    ui::PageTransition expected_transition = ui::PageTransitionFromInt(
        ui::PAGE_TRANSITION_TYPED | ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);

    EXPECT_TRUE(NavigateToURL(shell(), url));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_EQ(url, observer.last_committed_url());
    EXPECT_EQ(expected_transition, observer.page_transition());
  }

  {
    // Test navigating to a page with subframe. The subframe will have
    // PageTransition of type AUTO_SUBFRAME.
    GURL url(
        embedded_test_server()->GetURL("/frame_tree/page_with_one_frame.html"));
    NavigationHandleObserver observer(
        shell()->web_contents(),
        embedded_test_server()->GetURL("/cross-site/baz.com/title1.html"));
    ui::PageTransition expected_transition =
        ui::PageTransitionFromInt(ui::PAGE_TRANSITION_AUTO_SUBFRAME);

    EXPECT_TRUE(NavigateToURL(shell(), url));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_EQ(embedded_test_server()->GetURL("baz.com", "/title1.html"),
              observer.last_committed_url());
    EXPECT_EQ(expected_transition, observer.page_transition());
    EXPECT_FALSE(observer.is_main_frame());
  }
}

// Ensure that the following methods on NavigationHandle behave correctly:
// * IsInMainFrame
// * IsParentMainFrame
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, VerifyFrameTree) {
  GURL main_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(c))"));
  GURL b_url(embedded_test_server()->GetURL(
      "b.com", "/cross_site_iframe_factory.html?b(c())"));
  GURL c_url(embedded_test_server()->GetURL(
      "c.com", "/cross_site_iframe_factory.html?c()"));

  NavigationHandleObserver main_observer(shell()->web_contents(), main_url);
  NavigationHandleObserver b_observer(shell()->web_contents(), b_url);
  NavigationHandleObserver c_observer(shell()->web_contents(), c_url);

  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  // Verify the main frame.
  EXPECT_TRUE(main_observer.has_committed());
  EXPECT_FALSE(main_observer.is_error());
  EXPECT_EQ(main_url, main_observer.last_committed_url());
  EXPECT_TRUE(main_observer.is_main_frame());
  EXPECT_EQ(root->frame_tree_node_id(), main_observer.frame_tree_node_id());

  // Verify the b.com frame.
  EXPECT_TRUE(b_observer.has_committed());
  EXPECT_FALSE(b_observer.is_error());
  EXPECT_EQ(b_url, b_observer.last_committed_url());
  EXPECT_FALSE(b_observer.is_main_frame());
  EXPECT_TRUE(b_observer.is_parent_main_frame());
  EXPECT_EQ(root->child_at(0)->frame_tree_node_id(),
            b_observer.frame_tree_node_id());

  // Verify the c.com frame.
  EXPECT_TRUE(c_observer.has_committed());
  EXPECT_FALSE(c_observer.is_error());
  EXPECT_EQ(c_url, c_observer.last_committed_url());
  EXPECT_FALSE(c_observer.is_main_frame());
  EXPECT_FALSE(c_observer.is_parent_main_frame());
  EXPECT_EQ(root->child_at(0)->child_at(0)->frame_tree_node_id(),
            c_observer.frame_tree_node_id());
}

// Ensure that the WasRedirected() method on NavigationHandle behaves correctly.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, VerifyRedirect) {
  {
    GURL url(embedded_test_server()->GetURL("/title1.html"));
    NavigationHandleObserver observer(shell()->web_contents(), url);

    EXPECT_TRUE(NavigateToURL(shell(), url));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_FALSE(observer.was_redirected());
  }

  {
    GURL url(embedded_test_server()->GetURL("/cross-site/baz.com/title1.html"));
    NavigationHandleObserver observer(shell()->web_contents(), url);

    NavigateToURL(shell(), url);

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_TRUE(observer.was_redirected());
  }
}

// Ensure that the IsSrcdoc() method on NavigationHandle behaves correctly.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, VerifySrcdoc) {
  GURL url(embedded_test_server()->GetURL(
      "/frame_tree/page_with_srcdoc_frame.html"));
  NavigationHandleObserver observer(shell()->web_contents(),
                                    GURL(url::kAboutBlankURL));

  EXPECT_TRUE(NavigateToURL(shell(), url));

  EXPECT_TRUE(observer.has_committed());
  EXPECT_FALSE(observer.is_error());
  EXPECT_TRUE(observer.is_srcdoc());
}

// Ensure that the IsSynchronousNavigation() method on NavigationHandle behaves
// correctly.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, VerifySynchronous) {
  GURL url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(a())"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  NavigationHandleObserver observer(
      shell()->web_contents(), embedded_test_server()->GetURL("a.com", "/bar"));
  EXPECT_TRUE(ExecuteScript(root->child_at(0)->current_frame_host(),
                            "window.history.pushState({}, '', 'bar');"));

  EXPECT_TRUE(observer.has_committed());
  EXPECT_FALSE(observer.is_error());
  EXPECT_TRUE(observer.is_synchronous());
}

// Ensure that a NavigationThrottle can cancel the navigation at navigation
// start.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, ThrottleCancelStart) {
  GURL start_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), start_url));

  GURL redirect_url(
      embedded_test_server()->GetURL("/cross-site/bar.com/title2.html"));
  NavigationHandleObserver observer(shell()->web_contents(), redirect_url);
  TestNavigationThrottleInstaller installer(
      shell()->web_contents(), NavigationThrottle::CANCEL,
      NavigationThrottle::PROCEED, NavigationThrottle::PROCEED);

  EXPECT_FALSE(NavigateToURL(shell(), redirect_url));

  EXPECT_FALSE(observer.has_committed());
  EXPECT_TRUE(observer.is_error());

  // The navigation should have been canceled before being redirected.
  EXPECT_FALSE(observer.was_redirected());
  EXPECT_EQ(shell()->web_contents()->GetLastCommittedURL(), start_url);
}

// Ensure that a NavigationThrottle can cancel the navigation when a navigation
// is redirected.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       ThrottleCancelRedirect) {
  GURL start_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), start_url));

  // A navigation with a redirect should be canceled.
  {
    GURL redirect_url(
        embedded_test_server()->GetURL("/cross-site/bar.com/title2.html"));
    NavigationHandleObserver observer(shell()->web_contents(), redirect_url);
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::CANCEL, NavigationThrottle::PROCEED);

    EXPECT_FALSE(NavigateToURL(shell(), redirect_url));

    EXPECT_FALSE(observer.has_committed());
    EXPECT_TRUE(observer.is_error());
    EXPECT_TRUE(observer.was_redirected());
    EXPECT_EQ(shell()->web_contents()->GetLastCommittedURL(), start_url);
  }

  // A navigation without redirects should be successful.
  {
    GURL no_redirect_url(embedded_test_server()->GetURL("/title2.html"));
    NavigationHandleObserver observer(shell()->web_contents(), no_redirect_url);
    TestNavigationThrottleInstaller installer(
        shell()->web_contents(), NavigationThrottle::PROCEED,
        NavigationThrottle::CANCEL, NavigationThrottle::PROCEED);

    EXPECT_TRUE(NavigateToURL(shell(), no_redirect_url));

    EXPECT_TRUE(observer.has_committed());
    EXPECT_FALSE(observer.is_error());
    EXPECT_FALSE(observer.was_redirected());
    EXPECT_EQ(shell()->web_contents()->GetLastCommittedURL(), no_redirect_url);
  }
}

// Ensure that a NavigationThrottle can cancel the navigation when the response
// is received.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest,
                       ThrottleCancelResponse) {
  GURL start_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), start_url));

  GURL redirect_url(
      embedded_test_server()->GetURL("/cross-site/bar.com/title2.html"));
  NavigationHandleObserver observer(shell()->web_contents(), redirect_url);
  TestNavigationThrottleInstaller installer(
      shell()->web_contents(), NavigationThrottle::PROCEED,
      NavigationThrottle::PROCEED, NavigationThrottle::CANCEL);

  EXPECT_FALSE(NavigateToURL(shell(), redirect_url));

  EXPECT_FALSE(observer.has_committed());
  EXPECT_TRUE(observer.is_error());
  // The navigation should have been redirected first, and then canceled when
  // the response arrived.
  EXPECT_TRUE(observer.was_redirected());
  EXPECT_EQ(shell()->web_contents()->GetLastCommittedURL(), start_url);
}

// Ensure that a NavigationThrottle can defer and resume the navigation at
// navigation start, navigation redirect and response received.
IN_PROC_BROWSER_TEST_F(NavigationHandleImplBrowserTest, ThrottleDefer) {
  GURL start_url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), start_url));

  GURL redirect_url(
      embedded_test_server()->GetURL("/cross-site/bar.com/title2.html"));
  TestNavigationObserver navigation_observer(shell()->web_contents(), 1);
  NavigationHandleObserver observer(shell()->web_contents(), redirect_url);
  TestNavigationThrottleInstaller installer(
      shell()->web_contents(), NavigationThrottle::DEFER,
      NavigationThrottle::DEFER, NavigationThrottle::DEFER);

  shell()->LoadURL(redirect_url);

  // Wait for WillStartRequest.
  installer.WaitForThrottleWillStart();
  EXPECT_EQ(1, installer.will_start_called());
  EXPECT_EQ(0, installer.will_redirect_called());
  EXPECT_EQ(0, installer.will_process_called());
  installer.navigation_throttle()->Resume();

  // Wait for WillRedirectRequest.
  installer.WaitForThrottleWillRedirect();
  EXPECT_EQ(1, installer.will_start_called());
  EXPECT_EQ(1, installer.will_redirect_called());
  EXPECT_EQ(0, installer.will_process_called());
  installer.navigation_throttle()->Resume();

  // Wait for WillProcessResponse.
  installer.WaitForThrottleWillProcess();
  EXPECT_EQ(1, installer.will_start_called());
  EXPECT_EQ(1, installer.will_redirect_called());
  EXPECT_EQ(1, installer.will_process_called());
  installer.navigation_throttle()->Resume();

  // Wait for the end of the navigation.
  navigation_observer.Wait();

  EXPECT_TRUE(observer.has_committed());
  EXPECT_TRUE(observer.was_redirected());
  EXPECT_FALSE(observer.is_error());
  EXPECT_EQ(shell()->web_contents()->GetLastCommittedURL(),
            GURL(embedded_test_server()->GetURL("bar.com", "/title2.html")));
}

}  // namespace content
