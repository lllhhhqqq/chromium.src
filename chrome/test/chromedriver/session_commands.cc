// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/chromedriver/session_commands.h"

#include <list>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/files/file_util.h"
#include "base/logging.h"  // For CHECK macros.
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/thread_task_runner_handle.h"
#include "base/values.h"
#include "chrome/test/chromedriver/basic_types.h"
#include "chrome/test/chromedriver/capabilities.h"
#include "chrome/test/chromedriver/chrome/automation_extension.h"
#include "chrome/test/chromedriver/chrome/browser_info.h"
#include "chrome/test/chromedriver/chrome/chrome.h"
#include "chrome/test/chromedriver/chrome/chrome_android_impl.h"
#include "chrome/test/chromedriver/chrome/chrome_desktop_impl.h"
#include "chrome/test/chromedriver/chrome/device_manager.h"
#include "chrome/test/chromedriver/chrome/devtools_event_listener.h"
#include "chrome/test/chromedriver/chrome/geoposition.h"
#include "chrome/test/chromedriver/chrome/status.h"
#include "chrome/test/chromedriver/chrome/web_view.h"
#include "chrome/test/chromedriver/chrome_launcher.h"
#include "chrome/test/chromedriver/command_listener.h"
#include "chrome/test/chromedriver/logging.h"
#include "chrome/test/chromedriver/net/url_request_context_getter.h"
#include "chrome/test/chromedriver/session.h"
#include "chrome/test/chromedriver/util.h"
#include "chrome/test/chromedriver/version.h"

namespace {

const char kWindowHandlePrefix[] = "CDwindow-";

std::string WebViewIdToWindowHandle(const std::string& web_view_id) {
  return kWindowHandlePrefix + web_view_id;
}

bool WindowHandleToWebViewId(const std::string& window_handle,
                             std::string* web_view_id) {
  if (window_handle.find(kWindowHandlePrefix) != 0u)
    return false;
  *web_view_id = window_handle.substr(
      std::string(kWindowHandlePrefix).length());
  return true;
}

}  // namespace

InitSessionParams::InitSessionParams(
    scoped_refptr<URLRequestContextGetter> context_getter,
    const SyncWebSocketFactory& socket_factory,
    DeviceManager* device_manager,
    PortServer* port_server,
    PortManager* port_manager)
    : context_getter(context_getter),
      socket_factory(socket_factory),
      device_manager(device_manager),
      port_server(port_server),
      port_manager(port_manager) {}

InitSessionParams::InitSessionParams(const InitSessionParams& other) = default;

InitSessionParams::~InitSessionParams() {}

namespace {

scoped_ptr<base::DictionaryValue> CreateCapabilities(Chrome* chrome) {
  scoped_ptr<base::DictionaryValue> caps(new base::DictionaryValue());
  caps->SetString("browserName", "chrome");
  caps->SetString("version", chrome->GetBrowserInfo()->browser_version);
  caps->SetString("chrome.chromedriverVersion", kChromeDriverVersion);
  caps->SetString("platform", chrome->GetOperatingSystemName());
  caps->SetBoolean("javascriptEnabled", true);
  caps->SetBoolean("takesScreenshot", true);
  caps->SetBoolean("takesHeapSnapshot", true);
  caps->SetBoolean("handlesAlerts", true);
  caps->SetBoolean("databaseEnabled", false);
  caps->SetBoolean("locationContextEnabled", true);
  caps->SetBoolean("mobileEmulationEnabled",
                   chrome->IsMobileEmulationEnabled());
  caps->SetBoolean("applicationCacheEnabled", false);
  caps->SetBoolean("browserConnectionEnabled", false);
  caps->SetBoolean("cssSelectorsEnabled", true);
  caps->SetBoolean("webStorageEnabled", true);
  caps->SetBoolean("rotatable", false);
  caps->SetBoolean("acceptSslCerts", true);
  caps->SetBoolean("nativeEvents", true);
  caps->SetBoolean("hasTouchScreen", chrome->HasTouchScreen());

  ChromeDesktopImpl* desktop = NULL;
  Status status = chrome->GetAsDesktop(&desktop);
  if (status.IsOk()) {
    caps->SetString("chrome.userDataDir",
                    desktop->command().GetSwitchValueNative("user-data-dir"));
  }

  return caps;
}

Status CheckSessionCreated(Session* session) {
  WebView* web_view = NULL;
  Status status = session->GetTargetWindow(&web_view);
  if (status.IsError())
    return Status(kSessionNotCreatedException, status);

  status = web_view->ConnectIfNecessary();
  if (status.IsError())
    return Status(kSessionNotCreatedException, status);

  base::ListValue args;
  scoped_ptr<base::Value> result(new base::FundamentalValue(0));
  status = web_view->CallFunction(session->GetCurrentFrameId(),
                                  "function(s) { return 1; }", args, &result);
  if (status.IsError())
    return Status(kSessionNotCreatedException, status);

  int response;
  if (!result->GetAsInteger(&response) || response != 1) {
    return Status(kSessionNotCreatedException,
                  "unexpected response from browser");
  }

  return Status(kOk);
}

Status InitSessionHelper(
    const InitSessionParams& bound_params,
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  session->driver_log.reset(
      new WebDriverLog(WebDriverLog::kDriverType, Log::kAll));
  const base::DictionaryValue* desired_caps;
  if (!params.GetDictionary("desiredCapabilities", &desired_caps))
    return Status(kUnknownError, "cannot find dict 'desiredCapabilities'");

  Capabilities capabilities;
  Status status = capabilities.Parse(*desired_caps);
  if (status.IsError())
    return status;

  Log::Level driver_level = Log::kWarning;
  if (capabilities.logging_prefs.count(WebDriverLog::kDriverType))
    driver_level = capabilities.logging_prefs[WebDriverLog::kDriverType];
  session->driver_log->set_min_level(driver_level);

  // Create Log's and DevToolsEventListener's for ones that are DevTools-based.
  // Session will own the Log's, Chrome will own the listeners.
  // Also create |CommandListener|s for the appropriate logs.
  ScopedVector<DevToolsEventListener> devtools_event_listeners;
  ScopedVector<CommandListener> command_listeners;
  status = CreateLogs(capabilities,
                      session,
                      &session->devtools_logs,
                      &devtools_event_listeners,
                      &command_listeners);
  if (status.IsError())
    return status;

  // |session| will own the |CommandListener|s.
  session->command_listeners.swap(command_listeners);

  status = LaunchChrome(bound_params.context_getter.get(),
                        bound_params.socket_factory,
                        bound_params.device_manager,
                        bound_params.port_server,
                        bound_params.port_manager,
                        capabilities,
                        &devtools_event_listeners,
                        &session->chrome);
  if (status.IsError())
    return status;

  std::list<std::string> web_view_ids;
  status = session->chrome->GetWebViewIds(&web_view_ids);
  if (status.IsError() || web_view_ids.empty()) {
    return status.IsError() ? status :
        Status(kUnknownError, "unable to discover open window in chrome");
  }

  session->window = web_view_ids.front();
  session->detach = capabilities.detach;
  session->force_devtools_screenshot = capabilities.force_devtools_screenshot;
  session->capabilities = CreateCapabilities(session->chrome.get());
  value->reset(session->capabilities->DeepCopy());
  return CheckSessionCreated(session);
}

}  // namespace

Status ExecuteInitSession(
    const InitSessionParams& bound_params,
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  Status status = InitSessionHelper(bound_params, session, params, value);
  if (status.IsError()) {
    session->quit = true;
    if (session->chrome != NULL)
      session->chrome->Quit();
  }
  return status;
}

Status ExecuteQuit(
    bool allow_detach,
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  session->quit = true;
  if (allow_detach && session->detach)
    return Status(kOk);
  else
    return session->chrome->Quit();
}

Status ExecuteGetSessionCapabilities(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  value->reset(session->capabilities->DeepCopy());
  return Status(kOk);
}

Status ExecuteGetCurrentWindowHandle(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  WebView* web_view = NULL;
  Status status = session->GetTargetWindow(&web_view);
  if (status.IsError())
    return status;

  value->reset(
      new base::StringValue(WebViewIdToWindowHandle(web_view->GetId())));
  return Status(kOk);
}

Status ExecuteLaunchApp(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  std::string id;
  if (!params.GetString("id", &id))
    return Status(kUnknownError, "'id' must be a string");

  ChromeDesktopImpl* desktop = NULL;
  Status status = session->chrome->GetAsDesktop(&desktop);
  if (status.IsError())
    return status;

  AutomationExtension* extension = NULL;
  status = desktop->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  return extension->LaunchApp(id);
}

Status ExecuteClose(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  std::list<std::string> web_view_ids;
  Status status = session->chrome->GetWebViewIds(&web_view_ids);
  if (status.IsError())
    return status;
  bool is_last_web_view = web_view_ids.size() == 1u;
  web_view_ids.clear();

  WebView* web_view = NULL;
  status = session->GetTargetWindow(&web_view);
  if (status.IsError())
    return status;

  status = session->chrome->CloseWebView(web_view->GetId());
  if (status.IsError())
    return status;

  status = session->chrome->GetWebViewIds(&web_view_ids);
  if ((status.code() == kChromeNotReachable && is_last_web_view) ||
      (status.IsOk() && web_view_ids.empty())) {
    // If no window is open, close is the equivalent of calling "quit".
    session->quit = true;
    return session->chrome->Quit();
  }

  return status;
}

Status ExecuteGetWindowHandles(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  std::list<std::string> web_view_ids;
  Status status = session->chrome->GetWebViewIds(&web_view_ids);
  if (status.IsError())
    return status;
  scoped_ptr<base::ListValue> window_ids(new base::ListValue());
  for (std::list<std::string>::const_iterator it = web_view_ids.begin();
       it != web_view_ids.end(); ++it) {
    window_ids->AppendString(WebViewIdToWindowHandle(*it));
  }
  value->reset(window_ids.release());
  return Status(kOk);
}

Status ExecuteSwitchToWindow(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  std::string name;
  if (!params.GetString("name", &name))
    return Status(kUnknownError, "'name' must be a string");

  std::list<std::string> web_view_ids;
  Status status = session->chrome->GetWebViewIds(&web_view_ids);
  if (status.IsError())
    return status;

  std::string web_view_id;
  bool found = false;
  if (WindowHandleToWebViewId(name, &web_view_id)) {
    // Check if any web_view matches |web_view_id|.
    for (std::list<std::string>::const_iterator it = web_view_ids.begin();
         it != web_view_ids.end(); ++it) {
      if (*it == web_view_id) {
        found = true;
        break;
      }
    }
  } else {
    // Check if any of the tab window names match |name|.
    const char* kGetWindowNameScript = "function() { return window.name; }";
    base::ListValue args;
    for (std::list<std::string>::const_iterator it = web_view_ids.begin();
         it != web_view_ids.end(); ++it) {
      scoped_ptr<base::Value> result;
      WebView* web_view;
      status = session->chrome->GetWebViewById(*it, &web_view);
      if (status.IsError())
        return status;
      status = web_view->ConnectIfNecessary();
      if (status.IsError())
        return status;
      status = web_view->CallFunction(
          std::string(), kGetWindowNameScript, args, &result);
      if (status.IsError())
        return status;
      std::string window_name;
      if (!result->GetAsString(&window_name))
        return Status(kUnknownError, "failed to get window name");
      if (window_name == name) {
        web_view_id = *it;
        found = true;
        break;
      }
    }
  }

  if (!found)
    return Status(kNoSuchWindow);

  if (session->overridden_geoposition) {
    WebView* web_view;
    Status status = session->chrome->GetWebViewById(web_view_id, &web_view);
    if (status.IsError())
      return status;
    status = web_view->ConnectIfNecessary();
    if (status.IsError())
      return status;
    status = web_view->OverrideGeolocation(*session->overridden_geoposition);
    if (status.IsError())
      return status;
  }

  if (session->overridden_network_conditions) {
    WebView* web_view;
    Status status = session->chrome->GetWebViewById(web_view_id, &web_view);
    if (status.IsError())
      return status;
    status = web_view->ConnectIfNecessary();
    if (status.IsError())
      return status;
    status = web_view->OverrideNetworkConditions(
        *session->overridden_network_conditions);
    if (status.IsError())
      return status;
  }

  session->window = web_view_id;
  session->SwitchToTopFrame();
  session->mouse_position = WebPoint(0, 0);
  return Status(kOk);
}

Status ExecuteSetTimeout(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  double ms_double;
  if (!params.GetDouble("ms", &ms_double))
    return Status(kUnknownError, "'ms' must be a double");
  std::string type;
  if (!params.GetString("type", &type))
    return Status(kUnknownError, "'type' must be a string");

  base::TimeDelta timeout =
      base::TimeDelta::FromMilliseconds(static_cast<int>(ms_double));
  // TODO(frankf): implicit and script timeout should be cleared
  // if negative timeout is specified.
  if (type == "implicit") {
    session->implicit_wait = timeout;
  } else if (type == "script") {
    session->script_timeout = timeout;
  } else if (type == "page load") {
    session->page_load_timeout =
        ((timeout < base::TimeDelta()) ? Session::kDefaultPageLoadTimeout
                                       : timeout);
  } else {
    return Status(kUnknownError, "unknown type of timeout:" + type);
  }
  return Status(kOk);
}

Status ExecuteSetScriptTimeout(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  double ms;
  if (!params.GetDouble("ms", &ms) || ms < 0)
    return Status(kUnknownError, "'ms' must be a non-negative number");
  session->script_timeout =
      base::TimeDelta::FromMilliseconds(static_cast<int>(ms));
  return Status(kOk);
}

Status ExecuteImplicitlyWait(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  double ms;
  if (!params.GetDouble("ms", &ms) || ms < 0)
    return Status(kUnknownError, "'ms' must be a non-negative number");
  session->implicit_wait =
      base::TimeDelta::FromMilliseconds(static_cast<int>(ms));
  return Status(kOk);
}

Status ExecuteIsLoading(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  WebView* web_view = NULL;
  Status status = session->GetTargetWindow(&web_view);
  if (status.IsError())
    return status;

  status = web_view->ConnectIfNecessary();
  if (status.IsError())
    return status;

  bool is_pending;
  status = web_view->IsPendingNavigation(
      session->GetCurrentFrameId(), &is_pending);
  if (status.IsError())
    return status;
  value->reset(new base::FundamentalValue(is_pending));
  return Status(kOk);
}

Status ExecuteGetLocation(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  if (!session->overridden_geoposition) {
    return Status(kUnknownError,
                  "Location must be set before it can be retrieved");
  }
  base::DictionaryValue location;
  location.SetDouble("latitude", session->overridden_geoposition->latitude);
  location.SetDouble("longitude", session->overridden_geoposition->longitude);
  location.SetDouble("accuracy", session->overridden_geoposition->accuracy);
  // Set a dummy altitude to make WebDriver clients happy.
  // https://code.google.com/p/chromedriver/issues/detail?id=281
  location.SetDouble("altitude", 0);
  value->reset(location.DeepCopy());
  return Status(kOk);
}

Status ExecuteGetNetworkConditions(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  if (!session->overridden_network_conditions) {
    return Status(kUnknownError,
                  "network conditions must be set before it can be retrieved");
  }
  base::DictionaryValue conditions;
  conditions.SetBoolean("offline",
                        session->overridden_network_conditions->offline);
  conditions.SetInteger("latency",
                        session->overridden_network_conditions->latency);
  conditions.SetInteger(
      "download_throughput",
      session->overridden_network_conditions->download_throughput);
  conditions.SetInteger(
      "upload_throughput",
      session->overridden_network_conditions->upload_throughput);
  value->reset(conditions.DeepCopy());
  return Status(kOk);
}

Status ExecuteGetWindowPosition(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  ChromeDesktopImpl* desktop = NULL;
  Status status = session->chrome->GetAsDesktop(&desktop);
  if (status.IsError())
    return status;

  AutomationExtension* extension = NULL;
  status = desktop->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  int x, y;
  status = extension->GetWindowPosition(&x, &y);
  if (status.IsError())
    return status;

  base::DictionaryValue position;
  position.SetInteger("x", x);
  position.SetInteger("y", y);
  value->reset(position.DeepCopy());
  return Status(kOk);
}

Status ExecuteSetWindowPosition(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  double x = 0;
  double y = 0;
  if (!params.GetDouble("x", &x) || !params.GetDouble("y", &y))
    return Status(kUnknownError, "missing or invalid 'x' or 'y'");

  ChromeDesktopImpl* desktop = NULL;
  Status status = session->chrome->GetAsDesktop(&desktop);
  if (status.IsError())
    return status;

  AutomationExtension* extension = NULL;
  status = desktop->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  return extension->SetWindowPosition(static_cast<int>(x), static_cast<int>(y));
}

Status ExecuteGetWindowSize(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  ChromeDesktopImpl* desktop = NULL;
  Status status = session->chrome->GetAsDesktop(&desktop);
  if (status.IsError())
    return status;

  AutomationExtension* extension = NULL;
  status = desktop->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  int width, height;
  status = extension->GetWindowSize(&width, &height);
  if (status.IsError())
    return status;

  base::DictionaryValue size;
  size.SetInteger("width", width);
  size.SetInteger("height", height);
  value->reset(size.DeepCopy());
  return Status(kOk);
}

Status ExecuteSetWindowSize(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  double width = 0;
  double height = 0;
  if (!params.GetDouble("width", &width) ||
      !params.GetDouble("height", &height))
    return Status(kUnknownError, "missing or invalid 'width' or 'height'");

  ChromeDesktopImpl* desktop = NULL;
  Status status = session->chrome->GetAsDesktop(&desktop);
  if (status.IsError())
    return status;

  AutomationExtension* extension = NULL;
  status = desktop->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  return extension->SetWindowSize(
      static_cast<int>(width), static_cast<int>(height));
}

Status ExecuteMaximizeWindow(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  ChromeDesktopImpl* desktop = NULL;
  Status status = session->chrome->GetAsDesktop(&desktop);
  if (status.IsError())
    return status;

  AutomationExtension* extension = NULL;
  status = desktop->GetAutomationExtension(&extension);
  if (status.IsError())
    return status;

  return extension->MaximizeWindow();
}

Status ExecuteGetAvailableLogTypes(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  scoped_ptr<base::ListValue> types(new base::ListValue());
  std::vector<WebDriverLog*> logs = session->GetAllLogs();
  for (std::vector<WebDriverLog*>::const_iterator log = logs.begin();
       log != logs.end();
       ++log) {
    types->AppendString((*log)->type());
  }
  *value = std::move(types);
  return Status(kOk);
}

Status ExecuteGetLog(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  std::string log_type;
  if (!params.GetString("type", &log_type)) {
    return Status(kUnknownError, "missing or invalid 'type'");
  }
  std::vector<WebDriverLog*> logs = session->GetAllLogs();
  for (std::vector<WebDriverLog*>::const_iterator log = logs.begin();
       log != logs.end();
       ++log) {
    if (log_type == (*log)->type()) {
      *value = (*log)->GetAndClearEntries();
      return Status(kOk);
    }
  }
  return Status(kUnknownError, "log type '" + log_type + "' not found");
}

Status ExecuteUploadFile(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
    std::string base64_zip_data;
  if (!params.GetString("file", &base64_zip_data))
    return Status(kUnknownError, "missing or invalid 'file'");
  std::string zip_data;
  if (!Base64Decode(base64_zip_data, &zip_data))
    return Status(kUnknownError, "unable to decode 'file'");

  if (!session->temp_dir.IsValid()) {
    if (!session->temp_dir.CreateUniqueTempDir())
      return Status(kUnknownError, "unable to create temp dir");
  }
  base::FilePath upload_dir;
  if (!base::CreateTemporaryDirInDir(session->temp_dir.path(),
                                     FILE_PATH_LITERAL("upload"),
                                     &upload_dir)) {
    return Status(kUnknownError, "unable to create temp dir");
  }
  std::string error_msg;
  base::FilePath upload;
  Status status = UnzipSoleFile(upload_dir, zip_data, &upload);
  if (status.IsError())
    return Status(kUnknownError, "unable to unzip 'file'", status);

  value->reset(new base::StringValue(upload.value()));
  return Status(kOk);
}

Status ExecuteIsAutoReporting(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  value->reset(new base::FundamentalValue(session->auto_reporting_enabled));
  return Status(kOk);
}

Status ExecuteSetAutoReporting(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value) {
  bool enabled;
  if (!params.GetBoolean("enabled", &enabled))
    return Status(kUnknownError, "missing parameter 'enabled'");
  session->auto_reporting_enabled = enabled;
  return Status(kOk);
}
