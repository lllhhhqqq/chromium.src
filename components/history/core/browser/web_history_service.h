// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_HISTORY_CORE_BROWSER_WEB_HISTORY_SERVICE_H_
#define COMPONENTS_HISTORY_CORE_BROWSER_WEB_HISTORY_SERVICE_H_

#include <stddef.h>

#include <set>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/keyed_service.h"

namespace base {
class DictionaryValue;
}

namespace net {
class URLRequestContextGetter;
}

class OAuth2TokenService;
class SigninManagerBase;

namespace history {

// Provides an API for querying Google servers for a signed-in user's
// synced history visits. It is roughly analogous to HistoryService, and
// supports a similar API.
class WebHistoryService : public KeyedService {
 public:
  // Handles all the work of making an API request. This class encapsulates
  // the entire state of the request. When an instance is destroyed, all
  // aspects of the request are cancelled.
  class Request {
   public:
    virtual ~Request();

    // Returns true if the request is "pending" (i.e., it has been started, but
    // is not yet been complete).
    virtual bool IsPending() = 0;

    // Returns the response code received from the server, which will only be
    // valid if the request succeeded.
    virtual int GetResponseCode() = 0;

    // Returns the contents of the response body received from the server.
    virtual const std::string& GetResponseBody() = 0;

    virtual void SetPostData(const std::string& post_data) = 0;

    // Tells the request to begin.
    virtual void Start() = 0;

   protected:
    Request();
  };

  // Callback with the result of a call to QueryHistory(). Currently, the
  // DictionaryValue is just the parsed JSON response from the server.
  // TODO(dubroy): Extract the DictionaryValue into a structured results object.
  typedef base::Callback<void(Request*, const base::DictionaryValue*)>
      QueryWebHistoryCallback;

  typedef base::Callback<void(bool success)> ExpireWebHistoryCallback;

  typedef base::Callback<void(bool success, bool new_enabled_value)>
      AudioWebHistoryCallback;

  typedef base::Callback<void(bool success)> QueryWebAndAppActivityCallback;

  typedef base::Callback<void(Request*, bool success)> CompletionCallback;

  WebHistoryService(
      OAuth2TokenService* token_service,
      SigninManagerBase* signin_manager,
      const scoped_refptr<net::URLRequestContextGetter>& request_context);
  ~WebHistoryService() override;

  // Searches synced history for visits matching |text_query|. The timeframe to
  // search, along with other options, is specified in |options|. If
  // |text_query| is empty, all visits in the timeframe will be returned.
  // This method is the equivalent of HistoryService::QueryHistory.
  // The caller takes ownership of the returned Request. If it is destroyed, the
  // request is cancelled.
  scoped_ptr<Request> QueryHistory(
      const base::string16& text_query,
      const QueryOptions& options,
      const QueryWebHistoryCallback& callback);

  // Removes all visits to specified URLs in specific time ranges.
  // This is the of equivalent HistoryService::ExpireHistory().
  void ExpireHistory(const std::vector<ExpireHistoryArgs>& expire_list,
                     const ExpireWebHistoryCallback& callback);

  // Removes all visits to specified URLs in the given time range.
  // This is the of equivalent HistoryService::ExpireHistoryBetween().
  void ExpireHistoryBetween(const std::set<GURL>& restrict_urls,
                            base::Time begin_time,
                            base::Time end_time,
                            const ExpireWebHistoryCallback& callback);

  // Requests whether audio history recording is enabled.
  virtual void GetAudioHistoryEnabled(const AudioWebHistoryCallback& callback);

  // Sets the state of audio history recording to |new_enabled_value|.
  virtual void SetAudioHistoryEnabled(bool new_enabled_value,
                                      const AudioWebHistoryCallback& callback);

  // Queries whether web and app activity is enabled on the server.
  virtual void QueryWebAndAppActivity(
      const QueryWebAndAppActivityCallback& callback);

  // Used for tests.
  size_t GetNumberOfPendingAudioHistoryRequests();

  // Whether there are other forms of browsing history stored on the server.
  bool HasOtherFormsOfBrowsingHistory() const;

 protected:
  // This function is pulled out for testing purposes. Caller takes ownership of
  // the new Request.
  virtual Request* CreateRequest(const GURL& url,
                                 const CompletionCallback& callback);

  // Extracts a JSON-encoded HTTP response into a DictionaryValue.
  // If |request|'s HTTP response code indicates failure, or if the response
  // body is not JSON, a null pointer is returned.
  static scoped_ptr<base::DictionaryValue> ReadResponse(Request* request);

  // Called by |request| when a web history query has completed. Unpacks the
  // response and calls |callback|, which is the original callback that was
  // passed to QueryHistory().
  static void QueryHistoryCompletionCallback(
      const WebHistoryService::QueryWebHistoryCallback& callback,
      WebHistoryService::Request* request,
      bool success);

  // Called by |request| when a request to delete history from the server has
  // completed. Unpacks the response and calls |callback|, which is the original
  // callback that was passed to ExpireHistory().
  void ExpireHistoryCompletionCallback(
      const WebHistoryService::ExpireWebHistoryCallback& callback,
      WebHistoryService::Request* request,
      bool success);

  // Called by |request| when a request to get or set audio history from the
  // server has completed. Unpacks the response and calls |callback|, which is
  // the original callback that was passed to AudioHistory().
  void AudioHistoryCompletionCallback(
    const WebHistoryService::AudioWebHistoryCallback& callback,
    WebHistoryService::Request* request,
    bool success);

  // Called by |request| when a web and app activity query has completed.
  // Unpacks the response and calls |callback|, which is the original callback
  // that was passed to QueryWebAndAppActivity().
  void QueryWebAndAppActivityCompletionCallback(
    const WebHistoryService::QueryWebAndAppActivityCallback& callback,
    WebHistoryService::Request* request,
    bool success);

 private:
  friend class WebHistoryServiceTest;

  // Stores pointer to OAuth2TokenService and SigninManagerBase instance. They
  // must outlive the WebHistoryService and can be null during tests.
  OAuth2TokenService* token_service_;
  SigninManagerBase* signin_manager_;

  // Request context getter to use.
  scoped_refptr<net::URLRequestContextGetter> request_context_;

  // Stores the version_info token received from the server in response to
  // a mutation operation (e.g., deleting history). This is used to ensure that
  // subsequent reads see a version of the data that includes the mutation.
  std::string server_version_info_;

  // Pending expiration requests to be canceled if not complete by profile
  // shutdown.
  std::set<Request*> pending_expire_requests_;

  // Pending requests to be canceled if not complete by profile shutdown.
  std::set<Request*> pending_audio_history_requests_;

  // Pending web and app activity queries to be canceled if not complete by
  // profile shutdown.
  std::set<Request*> pending_web_and_app_activity_requests_;

  base::WeakPtrFactory<WebHistoryService> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(WebHistoryService);
};

}  // namespace history

#endif  // COMPONENTS_HISTORY_CORE_BROWSER_WEB_HISTORY_SERVICE_H_
