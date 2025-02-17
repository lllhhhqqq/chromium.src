// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/local_discovery/local_discovery_ui_handler.h"

#include <set>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/local_discovery/service_discovery_shared_client.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service_factory.h"
#include "chrome/browser/printing/cloud_print/privet_confirm_api_flow.h"
#include "chrome/browser/printing/cloud_print/privet_constants.h"
#include "chrome/browser/printing/cloud_print/privet_device_lister_impl.h"
#include "chrome/browser/printing/cloud_print/privet_http_asynchronous_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/cloud_devices/common/cloud_devices_urls.h"
#include "components/prefs/pref_service.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(ENABLE_PRINT_PREVIEW) && !defined(OS_CHROMEOS)
#define CLOUD_PRINT_CONNECTOR_UI_AVAILABLE
#endif

using cloud_print::CloudPrintPrinterList;
using cloud_print::DeviceDescription;
using cloud_print::GCDApiFlow;
using cloud_print::PrivetRegisterOperation;

namespace local_discovery {

namespace {

const char kDictionaryKeyServiceName[] = "service_name";
const char kDictionaryKeyDisplayName[] = "display_name";
const char kDictionaryKeyDescription[] = "description";
const char kDictionaryKeyType[] = "type";
const char kDictionaryKeyIsWifi[] = "is_wifi";
const char kDictionaryKeyID[] = "id";

const char kKeyPrefixMDns[] = "MDns:";

int g_num_visible = 0;

const int kCloudDevicesPrivetVersion = 3;

std::unique_ptr<base::DictionaryValue> CreateDeviceInfo(
    const CloudPrintPrinterList::Device& description) {
  std::unique_ptr<base::DictionaryValue> return_value(
      new base::DictionaryValue);

  return_value->SetString(kDictionaryKeyID, description.id);
  return_value->SetString(kDictionaryKeyDisplayName, description.display_name);
  return_value->SetString(kDictionaryKeyDescription, description.description);
  return_value->SetString(kDictionaryKeyType, "printer");

  return return_value;
}

void ReadDevicesList(const CloudPrintPrinterList::DeviceList& devices,
                     const std::set<std::string>& local_ids,
                     base::ListValue* devices_list) {
  for (const auto& i : devices) {
    if (local_ids.count(i.id) > 0) {
      devices_list->Append(CreateDeviceInfo(i).release());
    }
  }

  for (const auto& i : devices) {
    if (local_ids.count(i.id) == 0) {
      devices_list->Append(CreateDeviceInfo(i).release());
    }
  }
}

}  // namespace

LocalDiscoveryUIHandler::LocalDiscoveryUIHandler()
    : is_visible_(false),
      failed_list_count_(0),
      succeded_list_count_(0) {
#if defined(CLOUD_PRINT_CONNECTOR_UI_AVAILABLE)
#if !defined(GOOGLE_CHROME_BUILD) && defined(OS_WIN)
  // On Windows, we need the PDF plugin which is only guaranteed to exist on
  // Google Chrome builds. Use a command-line switch for Windows non-Google
  //  Chrome builds.
  cloud_print_connector_ui_enabled_ =
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableCloudPrintProxy);
#else
  // Always enabled for Linux and Google Chrome Windows builds.
  // Never enabled for Chrome OS, we don't even need to indicate it.
  cloud_print_connector_ui_enabled_ = true;
#endif
#endif  // defined(CLOUD_PRINT_CONNECTOR_UI_AVAILABLE)
}

LocalDiscoveryUIHandler::~LocalDiscoveryUIHandler() {
  Profile* profile = Profile::FromWebUI(web_ui());
  SigninManagerBase* signin_manager =
      SigninManagerFactory::GetInstance()->GetForProfile(profile);
  if (signin_manager)
    signin_manager->RemoveObserver(this);
  ResetCurrentRegistration();
  SetIsVisible(false);
}

// static
bool LocalDiscoveryUIHandler::GetHasVisible() {
  return g_num_visible != 0;
}

void LocalDiscoveryUIHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("start", base::Bind(
      &LocalDiscoveryUIHandler::HandleStart,
      base::Unretained(this)));
  web_ui()->RegisterMessageCallback("isVisible", base::Bind(
      &LocalDiscoveryUIHandler::HandleIsVisible,
      base::Unretained(this)));
  web_ui()->RegisterMessageCallback("registerDevice", base::Bind(
      &LocalDiscoveryUIHandler::HandleRegisterDevice,
      base::Unretained(this)));
  web_ui()->RegisterMessageCallback("cancelRegistration", base::Bind(
      &LocalDiscoveryUIHandler::HandleCancelRegistration,
      base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "requestDeviceList",
      base::Bind(&LocalDiscoveryUIHandler::HandleRequestDeviceList,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("openCloudPrintURL", base::Bind(
      &LocalDiscoveryUIHandler::HandleOpenCloudPrintURL,
      base::Unretained(this)));
  web_ui()->RegisterMessageCallback("showSyncUI", base::Bind(
      &LocalDiscoveryUIHandler::HandleShowSyncUI,
      base::Unretained(this)));

  // Cloud print connector related messages
#if defined(CLOUD_PRINT_CONNECTOR_UI_AVAILABLE)
  if (cloud_print_connector_ui_enabled_) {
    web_ui()->RegisterMessageCallback(
        "showCloudPrintSetupDialog",
        base::Bind(&LocalDiscoveryUIHandler::ShowCloudPrintSetupDialog,
                   base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "disableCloudPrintConnector",
        base::Bind(&LocalDiscoveryUIHandler::HandleDisableCloudPrintConnector,
                   base::Unretained(this)));
  }
#endif  // defined(CLOUD_PRINT_CONNECTOR_UI_AVAILABLE)
}

void LocalDiscoveryUIHandler::HandleStart(const base::ListValue* args) {
  Profile* profile = Profile::FromWebUI(web_ui());

  // If privet_lister_ is already set, it is a mock used for tests or the result
  // of a reload.
  if (!privet_lister_) {
    service_discovery_client_ = ServiceDiscoverySharedClient::GetInstance();
    privet_lister_.reset(
        new cloud_print::PrivetDeviceListerImpl(service_discovery_client_.get(),
                                                this));
    privet_http_factory_ =
        cloud_print::PrivetHTTPAsynchronousFactory::CreateInstance(
            profile->GetRequestContext());

    SigninManagerBase* signin_manager =
        SigninManagerFactory::GetInstance()->GetForProfile(profile);
    if (signin_manager)
      signin_manager->AddObserver(this);
  }

  privet_lister_->Start();
  privet_lister_->DiscoverNewDevices(false);

#if defined(CLOUD_PRINT_CONNECTOR_UI_AVAILABLE)
  StartCloudPrintConnector();
#endif

  CheckUserLoggedIn();
}

void LocalDiscoveryUIHandler::HandleIsVisible(const base::ListValue* args) {
  bool is_visible = false;
  bool rv = args->GetBoolean(0, &is_visible);
  DCHECK(rv);
  SetIsVisible(is_visible);
}

void LocalDiscoveryUIHandler::HandleRegisterDevice(
    const base::ListValue* args) {
  std::string device;

  bool rv = args->GetString(0, &device);
  DCHECK(rv);

  DeviceDescriptionMap::iterator found = device_descriptions_.find(device);
  if (found == device_descriptions_.end()) {
    OnSetupError();
    return;
  }

  if (found->second.version < kCloudDevicesPrivetVersion) {
    privet_resolution_ = privet_http_factory_->CreatePrivetHTTP(device);
    privet_resolution_->Start(
        found->second.address,
        base::Bind(&LocalDiscoveryUIHandler::StartRegisterHTTP,
                   base::Unretained(this)));
  } else {
    OnSetupError();
  }
}

void LocalDiscoveryUIHandler::HandleCancelRegistration(
    const base::ListValue* args) {
  ResetCurrentRegistration();
}

void LocalDiscoveryUIHandler::HandleRequestDeviceList(
    const base::ListValue* args) {
  failed_list_count_ = 0;
  succeded_list_count_ = 0;
  cloud_devices_.clear();

  cloud_print_printer_list_ = CreateApiFlow();

  if (cloud_print_printer_list_) {
    cloud_print_printer_list_->Start(
        base::WrapUnique<GCDApiFlow::Request>(new CloudPrintPrinterList(this)));
  }

  CheckListingDone();
}

void LocalDiscoveryUIHandler::HandleOpenCloudPrintURL(
    const base::ListValue* args) {
  std::string id;
  bool rv = args->GetString(0, &id);
  DCHECK(rv);

  Browser* browser = chrome::FindBrowserWithWebContents(
      web_ui()->GetWebContents());
  DCHECK(browser);

  chrome::AddSelectedTabWithURL(browser,
                                cloud_devices::GetCloudPrintManageDeviceURL(id),
                                ui::PAGE_TRANSITION_FROM_API);
}

void LocalDiscoveryUIHandler::HandleShowSyncUI(
    const base::ListValue* args) {
  Browser* browser = chrome::FindBrowserWithWebContents(
      web_ui()->GetWebContents());
  DCHECK(browser);
  chrome::ShowBrowserSignin(
      browser, signin_metrics::AccessPoint::ACCESS_POINT_DEVICES_PAGE);
}

void LocalDiscoveryUIHandler::StartRegisterHTTP(
    std::unique_ptr<cloud_print::PrivetHTTPClient> http_client) {
  current_http_client_ =
      cloud_print::PrivetV1HTTPClient::CreateDefault(std::move(http_client));

  std::string user = GetSyncAccount();

  if (!current_http_client_) {
    SendRegisterError();
    return;
  }

  current_register_operation_ =
      current_http_client_->CreateRegisterOperation(user, this);
  current_register_operation_->Start();
}

void LocalDiscoveryUIHandler::OnPrivetRegisterClaimToken(
    cloud_print::PrivetRegisterOperation* operation,
    const std::string& token,
    const GURL& url) {
  web_ui()->CallJavascriptFunction(
      "local_discovery.onRegistrationConfirmedOnPrinter");
  if (device_descriptions_.count(current_http_client_->GetName()) == 0) {
    SendRegisterError();
    return;
  }

  confirm_api_call_flow_ = CreateApiFlow();
  if (!confirm_api_call_flow_) {
    SendRegisterError();
    return;
  }
  confirm_api_call_flow_->Start(base::WrapUnique<GCDApiFlow::Request>(
      new cloud_print::PrivetConfirmApiCallFlow(
          token, base::Bind(&LocalDiscoveryUIHandler::OnConfirmDone,
                            base::Unretained(this)))));
}

void LocalDiscoveryUIHandler::OnPrivetRegisterError(
    PrivetRegisterOperation* operation,
    const std::string& action,
    PrivetRegisterOperation::FailureReason reason,
    int printer_http_code,
    const base::DictionaryValue* json) {
  std::string error;

  if (reason == PrivetRegisterOperation::FAILURE_JSON_ERROR &&
      json->GetString(cloud_print::kPrivetKeyError, &error)) {
    if (error == cloud_print::kPrivetErrorTimeout) {
        web_ui()->CallJavascriptFunction(
            "local_discovery.onRegistrationTimeout");
      return;
    } else if (error == cloud_print::kPrivetErrorCancel) {
      web_ui()->CallJavascriptFunction(
            "local_discovery.onRegistrationCanceledPrinter");
      return;
    }
  }

  SendRegisterError();
}

void LocalDiscoveryUIHandler::OnPrivetRegisterDone(
    PrivetRegisterOperation* operation,
    const std::string& device_id) {
  std::string name = operation->GetHTTPClient()->GetName();
  current_register_operation_.reset();
  current_http_client_.reset();
  SendRegisterDone(name);
}

void LocalDiscoveryUIHandler::OnSetupError() {
  ResetCurrentRegistration();
  SendRegisterError();
}

void LocalDiscoveryUIHandler::OnConfirmDone(GCDApiFlow::Status status) {
  if (status == GCDApiFlow::SUCCESS) {
    confirm_api_call_flow_.reset();
    current_register_operation_->CompleteRegistration();
  } else {
    SendRegisterError();
  }
}

void LocalDiscoveryUIHandler::DeviceChanged(
    bool added,
    const std::string& name,
    const DeviceDescription& description) {
  device_descriptions_[name] = description;

  base::DictionaryValue info;

  base::StringValue service_key(kKeyPrefixMDns + name);

  if (description.id.empty()) {
    info.SetString(kDictionaryKeyServiceName, name);
    info.SetString(kDictionaryKeyDisplayName, description.name);
    info.SetString(kDictionaryKeyDescription, description.description);
    info.SetString(kDictionaryKeyType, description.type);
    info.SetBoolean(kDictionaryKeyIsWifi, false);

    web_ui()->CallJavascriptFunction(
        "local_discovery.onUnregisteredDeviceUpdate", service_key, info);
  } else {
    std::unique_ptr<base::Value> null_value = base::Value::CreateNullValue();

    web_ui()->CallJavascriptFunction(
        "local_discovery.onUnregisteredDeviceUpdate", service_key, *null_value);
  }
}

void LocalDiscoveryUIHandler::DeviceRemoved(const std::string& name) {
  device_descriptions_.erase(name);
  std::unique_ptr<base::Value> null_value = base::Value::CreateNullValue();
  base::StringValue name_value(kKeyPrefixMDns + name);

  web_ui()->CallJavascriptFunction("local_discovery.onUnregisteredDeviceUpdate",
                                   name_value, *null_value);
}

void LocalDiscoveryUIHandler::DeviceCacheFlushed() {
  web_ui()->CallJavascriptFunction("local_discovery.onDeviceCacheFlushed");
  privet_lister_->DiscoverNewDevices(false);
}

void LocalDiscoveryUIHandler::OnDeviceListReady(
    const CloudPrintPrinterList::DeviceList& devices) {
  cloud_devices_.insert(cloud_devices_.end(), devices.begin(), devices.end());
  ++succeded_list_count_;
  CheckListingDone();
}

void LocalDiscoveryUIHandler::OnDeviceListUnavailable() {
  ++failed_list_count_;
  CheckListingDone();
}

void LocalDiscoveryUIHandler::GoogleSigninSucceeded(
    const std::string& account_id,
    const std::string& username,
    const std::string& password) {
  CheckUserLoggedIn();
}

void LocalDiscoveryUIHandler::GoogleSignedOut(const std::string& account_id,
                                              const std::string& username) {
  CheckUserLoggedIn();
}

void LocalDiscoveryUIHandler::SendRegisterError() {
  web_ui()->CallJavascriptFunction("local_discovery.onRegistrationFailed");
}

void LocalDiscoveryUIHandler::SendRegisterDone(
    const std::string& service_name) {
  // HACK(noamsml): Generate network traffic so the Windows firewall doesn't
  // block the printer's announcement.
  privet_lister_->DiscoverNewDevices(false);

  DeviceDescriptionMap::iterator found =
      device_descriptions_.find(service_name);

  if (found == device_descriptions_.end()) {
    // TODO(noamsml): Handle the case where a printer's record is not present at
    // the end of registration.
    SendRegisterError();
    return;
  }

  const DeviceDescription& device = found->second;
  base::DictionaryValue device_value;

  device_value.SetString(kDictionaryKeyType, device.type);
  device_value.SetString(kDictionaryKeyID, device.id);
  device_value.SetString(kDictionaryKeyDisplayName, device.name);
  device_value.SetString(kDictionaryKeyDescription, device.description);
  device_value.SetString(kDictionaryKeyServiceName, service_name);

  web_ui()->CallJavascriptFunction("local_discovery.onRegistrationSuccess",
                                   device_value);
}

void LocalDiscoveryUIHandler::SetIsVisible(bool visible) {
  if (visible != is_visible_) {
    g_num_visible += visible ? 1 : -1;
    is_visible_ = visible;
  }
}

std::string LocalDiscoveryUIHandler::GetSyncAccount() {
  Profile* profile = Profile::FromWebUI(web_ui());
  SigninManagerBase* signin_manager =
      SigninManagerFactory::GetForProfileIfExists(profile);

  if (!signin_manager) {
    return "";
  }

  return signin_manager->GetAuthenticatedAccountInfo().email;
}

// TODO(noamsml): Create master object for registration flow.
void LocalDiscoveryUIHandler::ResetCurrentRegistration() {
  if (current_register_operation_) {
    current_register_operation_->Cancel();
    current_register_operation_.reset();
  }

  confirm_api_call_flow_.reset();
  privet_resolution_.reset();
  current_http_client_.reset();
}

void LocalDiscoveryUIHandler::CheckUserLoggedIn() {
  base::FundamentalValue logged_in_value(!GetSyncAccount().empty());
  base::FundamentalValue is_supervised_value(IsUserSupervisedOrOffTheRecord());
  web_ui()->CallJavascriptFunction(
      "local_discovery.setUserLoggedIn", logged_in_value, is_supervised_value);
}

void LocalDiscoveryUIHandler::CheckListingDone() {
  int started = 0;
  if (cloud_print_printer_list_)
    ++started;

  if (started > failed_list_count_ + succeded_list_count_)
    return;

  if (succeded_list_count_ <= 0) {
    web_ui()->CallJavascriptFunction(
        "local_discovery.onCloudDeviceListUnavailable");
    return;
  }

  base::ListValue devices_list;
  std::set<std::string> local_ids;

  for (DeviceDescriptionMap::iterator i = device_descriptions_.begin();
       i != device_descriptions_.end(); i++) {
    local_ids.insert(i->second.id);
  }

  ReadDevicesList(cloud_devices_, local_ids, &devices_list);

  web_ui()->CallJavascriptFunction(
      "local_discovery.onCloudDeviceListAvailable", devices_list);
  cloud_print_printer_list_.reset();
}

std::unique_ptr<GCDApiFlow> LocalDiscoveryUIHandler::CreateApiFlow() {
  Profile* profile = Profile::FromWebUI(web_ui());
  if (!profile)
    return std::unique_ptr<GCDApiFlow>();
  ProfileOAuth2TokenService* token_service =
      ProfileOAuth2TokenServiceFactory::GetForProfile(profile);
  if (!token_service)
    return std::unique_ptr<GCDApiFlow>();
  SigninManagerBase* signin_manager =
      SigninManagerFactory::GetInstance()->GetForProfile(profile);
  if (!signin_manager)
    return std::unique_ptr<GCDApiFlow>();
  return GCDApiFlow::Create(profile->GetRequestContext(),
                            token_service,
                            signin_manager->GetAuthenticatedAccountId());
}

bool LocalDiscoveryUIHandler::IsUserSupervisedOrOffTheRecord() {
  Profile* profile = Profile::FromWebUI(web_ui());

  return profile->IsSupervised() || profile->IsOffTheRecord();
}

#if defined(CLOUD_PRINT_CONNECTOR_UI_AVAILABLE)
void LocalDiscoveryUIHandler::StartCloudPrintConnector() {
  Profile* profile = Profile::FromWebUI(web_ui());

  base::Closure cloud_print_callback = base::Bind(
      &LocalDiscoveryUIHandler::OnCloudPrintPrefsChanged,
          base::Unretained(this));

  if (cloud_print_connector_email_.GetPrefName().empty()) {
    cloud_print_connector_email_.Init(
        prefs::kCloudPrintEmail, profile->GetPrefs(), cloud_print_callback);
  }

  if (cloud_print_connector_enabled_.GetPrefName().empty()) {
    cloud_print_connector_enabled_.Init(
        prefs::kCloudPrintProxyEnabled, profile->GetPrefs(),
        cloud_print_callback);
  }

  if (cloud_print_connector_ui_enabled_) {
    SetupCloudPrintConnectorSection();
    RefreshCloudPrintStatusFromService();
  } else {
    RemoveCloudPrintConnectorSection();
  }
}

void LocalDiscoveryUIHandler::OnCloudPrintPrefsChanged() {
  if (cloud_print_connector_ui_enabled_)
    SetupCloudPrintConnectorSection();
}

void LocalDiscoveryUIHandler::ShowCloudPrintSetupDialog(
    const base::ListValue* args) {
  content::RecordAction(
      base::UserMetricsAction("Options_EnableCloudPrintProxy"));
  // Open the connector enable page in the current tab.
  Profile* profile = Profile::FromWebUI(web_ui());
  content::OpenURLParams params(
      cloud_devices::GetCloudPrintEnableURL(
          CloudPrintProxyServiceFactory::GetForProfile(profile)->proxy_id()),
      content::Referrer(),
      CURRENT_TAB,
      ui::PAGE_TRANSITION_LINK,
      false);
  web_ui()->GetWebContents()->OpenURL(params);
}

void LocalDiscoveryUIHandler::HandleDisableCloudPrintConnector(
    const base::ListValue* args) {
  content::RecordAction(
      base::UserMetricsAction("Options_DisableCloudPrintProxy"));
  CloudPrintProxyServiceFactory::GetForProfile(Profile::FromWebUI(web_ui()))->
      DisableForUser();
}

void LocalDiscoveryUIHandler::SetupCloudPrintConnectorSection() {
  Profile* profile = Profile::FromWebUI(web_ui());

  if (!CloudPrintProxyServiceFactory::GetForProfile(profile)) {
    cloud_print_connector_ui_enabled_ = false;
    RemoveCloudPrintConnectorSection();
    return;
  }

  bool cloud_print_connector_allowed =
      !cloud_print_connector_enabled_.IsManaged() ||
      cloud_print_connector_enabled_.GetValue();
  base::FundamentalValue allowed(cloud_print_connector_allowed);

  std::string email;
  if (profile->GetPrefs()->HasPrefPath(prefs::kCloudPrintEmail) &&
      cloud_print_connector_allowed) {
    email = profile->GetPrefs()->GetString(prefs::kCloudPrintEmail);
  }
  base::FundamentalValue disabled(email.empty());

  base::string16 label_str;
  if (email.empty()) {
    label_str = l10n_util::GetStringFUTF16(
        IDS_LOCAL_DISCOVERY_CLOUD_PRINT_CONNECTOR_DISABLED_LABEL,
        l10n_util::GetStringUTF16(IDS_GOOGLE_CLOUD_PRINT));
  } else {
    label_str = l10n_util::GetStringFUTF16(
        IDS_OPTIONS_CLOUD_PRINT_CONNECTOR_ENABLED_LABEL,
        l10n_util::GetStringUTF16(IDS_GOOGLE_CLOUD_PRINT),
        base::UTF8ToUTF16(email));
  }
  base::StringValue label(label_str);

  web_ui()->CallJavascriptFunction(
      "local_discovery.setupCloudPrintConnectorSection", disabled, label,
      allowed);
}

void LocalDiscoveryUIHandler::RemoveCloudPrintConnectorSection() {
  web_ui()->CallJavascriptFunction(
      "local_discovery.removeCloudPrintConnectorSection");
}

void LocalDiscoveryUIHandler::RefreshCloudPrintStatusFromService() {
  if (cloud_print_connector_ui_enabled_)
    CloudPrintProxyServiceFactory::GetForProfile(Profile::FromWebUI(web_ui()))->
        RefreshStatusFromService();
}

#endif // cloud print connector option stuff

}  // namespace local_discovery
