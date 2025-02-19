// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/profiles/profile_helper.h"

#include "base/barrier_closure.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/browsing_data/browsing_data_remover.h"
#include "chrome/browser/browsing_data/browsing_data_remover_factory.h"
#include "chrome/browser/chromeos/base/file_flusher.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/signin/oauth2_login_manager_factory.h"
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chromeos/chromeos_constants.h"
#include "chromeos/chromeos_switches.h"
#include "components/guest_view/browser/guest_view_manager.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/guest_view/web_view/web_view_guest.h"
#include "extensions/common/constants.h"

namespace chromeos {

namespace {

// As defined in /chromeos/dbus/cryptohome_client.cc.
static const char kUserIdHashSuffix[] = "-hash";

bool ShouldAddProfileDirPrefix(const std::string& user_id_hash) {
  // Do not add profile dir prefix for legacy profile dir and test
  // user profile. The reason of not adding prefix for test user profile
  // is to keep the promise that TestingProfile::kTestUserProfileDir and
  // chrome::kTestUserProfileDir are always in sync. Otherwise,
  // TestingProfile::kTestUserProfileDir needs to be dynamically calculated
  // based on whether multi profile is enabled or not.
  return user_id_hash != chrome::kLegacyProfileDir &&
      user_id_hash != chrome::kTestUserProfileDir;
}

class UsernameHashMatcher {
 public:
  explicit UsernameHashMatcher(const std::string& h) : username_hash(h) {}
  bool operator()(const user_manager::User* user) const {
    return user->username_hash() == username_hash;
  }

 private:
  const std::string& username_hash;
};

}  // anonymous namespace

// static
bool ProfileHelper::enable_profile_to_user_testing = false;
bool ProfileHelper::always_return_primary_user_for_testing = false;

////////////////////////////////////////////////////////////////////////////////
// ProfileHelper, public

ProfileHelper::ProfileHelper()
    : browsing_data_remover_(nullptr), weak_factory_(this) {
}

ProfileHelper::~ProfileHelper() {
  // Checking whether UserManager is initialized covers case
  // when ScopedTestUserManager is used.
  if (user_manager::UserManager::IsInitialized())
    user_manager::UserManager::Get()->RemoveSessionStateObserver(this);

  if (browsing_data_remover_)
    browsing_data_remover_->RemoveObserver(this);
}

// static
ProfileHelper* ProfileHelper::Get() {
  return g_browser_process->platform_part()->profile_helper();
}

// static
Profile* ProfileHelper::GetProfileByUserIdHash(
    const std::string& user_id_hash) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  return profile_manager->GetProfile(GetProfilePathByUserIdHash(user_id_hash));
}

// static
base::FilePath ProfileHelper::GetProfilePathByUserIdHash(
    const std::string& user_id_hash) {
  // Fails for KioskTest.InstallAndLaunchApp test - crbug.com/238985
  // Will probably fail for Guest session / restart after a crash -
  // crbug.com/238998
  // TODO(nkostylev): Remove this check once these bugs are fixed.
  DCHECK(!user_id_hash.empty());
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  base::FilePath profile_path = profile_manager->user_data_dir();

  return profile_path.Append(GetUserProfileDir(user_id_hash));
}

// static
base::FilePath ProfileHelper::GetSigninProfileDir() {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  base::FilePath user_data_dir = profile_manager->user_data_dir();
  return user_data_dir.AppendASCII(chrome::kInitialProfile);
}

// static
Profile* ProfileHelper::GetSigninProfile() {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  return profile_manager->GetProfile(GetSigninProfileDir())->
      GetOffTheRecordProfile();
}

// static
std::string ProfileHelper::GetUserIdHashFromProfile(const Profile* profile) {
  if (!profile)
    return std::string();

  std::string profile_dir = profile->GetPath().BaseName().value();

  // Don't strip prefix if the dir is not supposed to be prefixed.
  if (!ShouldAddProfileDirPrefix(profile_dir))
    return profile_dir;

  // Check that profile directory starts with the correct prefix.
  std::string prefix(chrome::kProfileDirPrefix);
  if (profile_dir.find(prefix) != 0) {
    // This happens when creating a TestingProfile in browser tests.
    return std::string();
  }

  return profile_dir.substr(prefix.length(),
                            profile_dir.length() - prefix.length());
}

// static
base::FilePath ProfileHelper::GetUserProfileDir(
    const std::string& user_id_hash) {
  CHECK(!user_id_hash.empty());
  return ShouldAddProfileDirPrefix(user_id_hash)
             ? base::FilePath(chrome::kProfileDirPrefix + user_id_hash)
             : base::FilePath(user_id_hash);
}

// static
bool ProfileHelper::IsSigninProfile(const Profile* profile) {
  return profile &&
         profile->GetPath().BaseName().value() == chrome::kInitialProfile;
}

// static
bool ProfileHelper::IsOwnerProfile(const Profile* profile) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          chromeos::switches::kStubCrosSettings)) {
    return true;
  }

  if (!profile)
    return false;
  const user_manager::User* user =
      ProfileHelper::Get()->GetUserByProfile(profile);
  if (!user)
    return false;
  return user->GetAccountId() ==
         user_manager::UserManager::Get()->GetOwnerAccountId();
}

// static
bool ProfileHelper::IsPrimaryProfile(const Profile* profile) {
  if (!profile)
    return false;
  const user_manager::User* user =
      ProfileHelper::Get()->GetUserByProfile(profile);
  if (!user)
    return false;
  return user == user_manager::UserManager::Get()->GetPrimaryUser();
}

// static
bool ProfileHelper::IsEphemeralUserProfile(const Profile* profile) {
  if (!profile)
    return false;

  // Owner profile is always persistent.
  if (IsOwnerProfile(profile))
    return false;

  const user_manager::User* user =
      ProfileHelper::Get()->GetUserByProfile(profile);
  if (!user)
    return false;

  // Guest and public account is ephemeral.
  const user_manager::UserType user_type = user->GetType();
  if (user_type == user_manager::USER_TYPE_GUEST ||
      user_type == user_manager::USER_TYPE_PUBLIC_ACCOUNT) {
    return true;
  }

  // Otherwise, users are ephemeral when the policy is enabled.
  return ChromeUserManager::Get()->AreEphemeralUsersEnabled();
}

void ProfileHelper::ProfileStartup(Profile* profile, bool process_startup) {
  // Initialize Chrome OS preferences like touch pad sensitivity. For the
  // preferences to work in the guest mode, the initialization has to be
  // done after |profile| is switched to the incognito profile (which
  // is actually GuestSessionProfile in the guest mode). See the
  // GetOffTheRecordProfile() call above.
  profile->InitChromeOSPreferences();

  // Add observer so we can see when the first profile's session restore is
  // completed. After that, we won't need the default profile anymore.
  if (!IsSigninProfile(profile) &&
      user_manager::UserManager::Get()->IsLoggedInAsUserWithGaiaAccount() &&
      !user_manager::UserManager::Get()->IsLoggedInAsStub()) {
    chromeos::OAuth2LoginManager* login_manager =
        chromeos::OAuth2LoginManagerFactory::GetInstance()->GetForProfile(
            profile);
    if (login_manager)
      login_manager->AddObserver(this);
  }
}

base::FilePath ProfileHelper::GetActiveUserProfileDir() {
  return ProfileHelper::GetUserProfileDir(active_user_id_hash_);
}

void ProfileHelper::Initialize() {
  user_manager::UserManager::Get()->AddSessionStateObserver(this);
}

void ProfileHelper::ClearSigninProfile(const base::Closure& on_clear_callback) {
  on_clear_callbacks_.push_back(on_clear_callback);

  // Profile is already clearing.
  if (on_clear_callbacks_.size() > 1)
    return;

  on_clear_profile_stage_finished_ =
      base::BarrierClosure(2, base::Bind(&ProfileHelper::OnSigninProfileCleared,
                                         weak_factory_.GetWeakPtr()));

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  // Check if signin profile was loaded.
  if (profile_manager->GetProfileByPath(GetSigninProfileDir())) {
    LOG_ASSERT(!browsing_data_remover_);
    browsing_data_remover_ =
        BrowsingDataRemoverFactory::GetForBrowserContext(GetSigninProfile());
    browsing_data_remover_->AddObserver(this);
    browsing_data_remover_->Remove(BrowsingDataRemover::Unbounded(),
                                   BrowsingDataRemover::REMOVE_SITE_DATA,
                                   BrowsingDataHelper::ALL);
  } else {
    on_clear_profile_stage_finished_.Run();
  }

  if (content::StoragePartition* partition = login::GetSigninPartition()) {
    partition->ClearData(
        content::StoragePartition::REMOVE_DATA_MASK_ALL,
        content::StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL, GURL(),
        content::StoragePartition::OriginMatcherFunction(), base::Time(),
        base::Time::Now(), on_clear_profile_stage_finished_);
  } else {
    on_clear_profile_stage_finished_.Run();
  }
}

Profile* ProfileHelper::GetProfileByUser(const user_manager::User* user) {
  // This map is non-empty only in tests.
  if (!user_to_profile_for_testing_.empty()) {
    std::map<const user_manager::User*, Profile*>::const_iterator it =
        user_to_profile_for_testing_.find(user);
    return it == user_to_profile_for_testing_.end() ? NULL : it->second;
  }

  if (!user->is_profile_created())
    return NULL;
  Profile* profile =
      ProfileHelper::GetProfileByUserIdHash(user->username_hash());

  // GetActiveUserProfile() or GetProfileByUserIdHash() returns a new instance
  // of ProfileImpl(), but actually its OffTheRecordProfile() should be used.
  if (user_manager::UserManager::Get()->IsLoggedInAsGuest())
    profile = profile->GetOffTheRecordProfile();

  return profile;
}

Profile* ProfileHelper::GetProfileByUserUnsafe(const user_manager::User* user) {
  // This map is non-empty only in tests.
  if (!user_to_profile_for_testing_.empty()) {
    std::map<const user_manager::User*, Profile*>::const_iterator it =
        user_to_profile_for_testing_.find(user);
    return it == user_to_profile_for_testing_.end() ? NULL : it->second;
  }

  Profile* profile = NULL;
  if (user->is_profile_created()) {
    profile = ProfileHelper::GetProfileByUserIdHash(user->username_hash());
  } else {
    LOG(ERROR) << "ProfileHelper::GetProfileByUserUnsafe is called when "
                  "|user|'s profile is not created. It probably means that "
                  "something is wrong with a calling code. Please report in "
                  "http://crbug.com/361528 if you see this message. user_id: "
               << user->email();
    profile = ProfileManager::GetActiveUserProfile();
  }

  // GetActiveUserProfile() or GetProfileByUserIdHash() returns a new instance
  // of ProfileImpl(), but actually its OffTheRecordProfile() should be used.
  if (profile && user_manager::UserManager::Get()->IsLoggedInAsGuest())
    profile = profile->GetOffTheRecordProfile();
  return profile;
}

const user_manager::User* ProfileHelper::GetUserByProfile(
    const Profile* profile) const {
  // This map is non-empty only in tests.
  if (enable_profile_to_user_testing || !user_list_for_testing_.empty()) {
    if (always_return_primary_user_for_testing)
      return user_manager::UserManager::Get()->GetPrimaryUser();

    const std::string& user_name = profile->GetProfileUserName();
    for (user_manager::UserList::const_iterator it =
             user_list_for_testing_.begin();
         it != user_list_for_testing_.end();
         ++it) {
      if ((*it)->email() == user_name)
        return *it;
    }

    // In case of test setup we should always default to primary user.
    return user_manager::UserManager::Get()->GetPrimaryUser();
  }

  DCHECK(!content::BrowserThread::IsThreadInitialized(
             content::BrowserThread::UI) ||
         content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (ProfileHelper::IsSigninProfile(profile))
    return NULL;

  user_manager::UserManager* user_manager = user_manager::UserManager::Get();

  // Special case for non-CrOS tests that do create several profiles
  // and don't really care about mapping to the real user.
  // Without multi-profiles on Chrome OS such tests always got active_user_.
  // Now these tests will specify special flag to continue working.
  // In future those tests can get a proper CrOS configuration i.e. register
  // and login several users if they want to work with an additional profile.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kIgnoreUserProfileMappingForTests)) {
    return user_manager->GetActiveUser();
  }

  const std::string username_hash =
      ProfileHelper::GetUserIdHashFromProfile(profile);
  const user_manager::UserList& users = user_manager->GetUsers();
  const user_manager::UserList::const_iterator pos = std::find_if(
      users.begin(), users.end(), UsernameHashMatcher(username_hash));
  if (pos != users.end())
    return *pos;

  // Many tests do not have their users registered with UserManager and
  // runs here. If |active_user_| matches |profile|, returns it.
  const user_manager::User* active_user = user_manager->GetActiveUser();
  return active_user &&
                 ProfileHelper::GetProfilePathByUserIdHash(
                     active_user->username_hash()) == profile->GetPath()
             ? active_user
             : NULL;
}

user_manager::User* ProfileHelper::GetUserByProfile(Profile* profile) const {
  return const_cast<user_manager::User*>(
      GetUserByProfile(static_cast<const Profile*>(profile)));
}

void ProfileHelper::OnSigninProfileCleared() {
  std::vector<base::Closure> callbacks;
  callbacks.swap(on_clear_callbacks_);
  for (const base::Closure& callback : callbacks) {
    if (!callback.is_null())
      callback.Run();
  }
}

////////////////////////////////////////////////////////////////////////////////
// ProfileHelper, BrowsingDataRemover::Observer implementation:

void ProfileHelper::OnBrowsingDataRemoverDone() {
  LOG_ASSERT(browsing_data_remover_);
  browsing_data_remover_->RemoveObserver(this);
  browsing_data_remover_ = nullptr;

  on_clear_profile_stage_finished_.Run();
}

////////////////////////////////////////////////////////////////////////////////
// ProfileHelper, OAuth2LoginManager::Observer implementation:

void ProfileHelper::OnSessionRestoreStateChanged(
    Profile* user_profile,
    OAuth2LoginManager::SessionRestoreState state) {
  if (state == OAuth2LoginManager::SESSION_RESTORE_DONE ||
      state == OAuth2LoginManager::SESSION_RESTORE_FAILED ||
      state == OAuth2LoginManager::SESSION_RESTORE_CONNECTION_FAILED) {
    chromeos::OAuth2LoginManager* login_manager =
        chromeos::OAuth2LoginManagerFactory::GetInstance()->
            GetForProfile(user_profile);
    login_manager->RemoveObserver(this);
    ClearSigninProfile(base::Closure());
  }
}

////////////////////////////////////////////////////////////////////////////////
// ProfileHelper, UserManager::UserSessionStateObserver implementation:

void ProfileHelper::ActiveUserHashChanged(const std::string& hash) {
  active_user_id_hash_ = hash;
}

void ProfileHelper::SetProfileToUserMappingForTesting(
    user_manager::User* user) {
  user_list_for_testing_.push_back(user);
}

// static
void ProfileHelper::SetProfileToUserForTestingEnabled(bool enabled) {
  enable_profile_to_user_testing = enabled;
}

// static
void ProfileHelper::SetAlwaysReturnPrimaryUserForTesting(bool value) {
  always_return_primary_user_for_testing = true;
  ProfileHelper::SetProfileToUserForTestingEnabled(true);
}

void ProfileHelper::SetUserToProfileMappingForTesting(
    const user_manager::User* user,
    Profile* profile) {
  user_to_profile_for_testing_[user] = profile;
}

// static
std::string ProfileHelper::GetUserIdHashByUserIdForTesting(
    const std::string& user_id) {
  return user_id + kUserIdHashSuffix;
}

void ProfileHelper::FlushProfile(Profile* profile) {
  if (!profile_flusher_)
    profile_flusher_.reset(new FileFlusher);

  // Files/directories that do not need to be flushed.
  std::vector<base::FilePath> excludes;

  // Preferences file is handled by ImportantFileWriter.
  excludes.push_back(base::FilePath(chrome::kPreferencesFilename));
  // Do not flush cache files.
  excludes.push_back(base::FilePath(chrome::kCacheDirname));
  excludes.push_back(base::FilePath(chrome::kMediaCacheDirname));
  excludes.push_back(base::FilePath(FILE_PATH_LITERAL("GPUCache")));
  // Do not flush user Downloads.
  excludes.push_back(
      DownloadPrefs::FromBrowserContext(profile)->DownloadPath());
  // Let extension system handle extension files.
  excludes.push_back(base::FilePath(extensions::kInstallDirectoryName));
  // Do not flush Drive cache.
  excludes.push_back(base::FilePath(chromeos::kDriveCacheDirname));

  profile_flusher_->RequestFlush(profile->GetPath(), excludes, base::Closure());
}

}  // namespace chromeos
