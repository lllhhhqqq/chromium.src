// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/app_mode/kiosk_app_update_service.h"

#include <memory>
#include <string>

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/scoped_path_override.h"
#include "base/thread_task_runner_handle.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/time/time.h"
#include "chrome/browser/apps/app_browsertest_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/chromeos/system/automatic_reboot_manager.h"
#include "chrome/browser/chromeos/system/automatic_reboot_manager_observer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chromeos/chromeos_paths.h"
#include "chromeos/dbus/update_engine_client.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/common/extension.h"
#include "extensions/test/extension_test_message_listener.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

namespace {

void RunCallback(scoped_refptr<base::SingleThreadTaskRunner> task_runner,
                 const base::Closure& callback) {
  task_runner->PostTask(FROM_HERE, callback);
}

} // namespace

class KioskAppUpdateServiceTest
    : public extensions::PlatformAppBrowserTest,
      public system::AutomaticRebootManagerObserver {
 public:
  KioskAppUpdateServiceTest()
      : app_(NULL),
        update_service_(NULL),
        automatic_reboot_manager_(NULL) {}

  ~KioskAppUpdateServiceTest() override {}

  // extensions::PlatformAppBrowserTest overrides:
  void SetUpInProcessBrowserTestFixture() override {
    extensions::PlatformAppBrowserTest::SetUpInProcessBrowserTestFixture();

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath& temp_dir = temp_dir_.path();

    const base::TimeDelta uptime = base::TimeDelta::FromHours(3);
    const std::string uptime_seconds =
        base::DoubleToString(uptime.InSecondsF());
    const base::FilePath uptime_file = temp_dir.Append("uptime");
    ASSERT_EQ(static_cast<int>(uptime_seconds.size()),
              base::WriteFile(
                  uptime_file, uptime_seconds.c_str(), uptime_seconds.size()));
    uptime_file_override_.reset(
        new base::ScopedPathOverride(chromeos::FILE_UPTIME, uptime_file));
  }

  void SetUpOnMainThread() override {
    extensions::PlatformAppBrowserTest::SetUpOnMainThread();

    app_ = LoadExtension(
        test_data_dir_.AppendASCII("api_test/runtime/on_restart_required"));

    // Fake app mode command line.
    base::CommandLine* command = base::CommandLine::ForCurrentProcess();
    command->AppendSwitch(switches::kForceAppMode);
    command->AppendSwitchASCII(switches::kAppId, app_->id());

    automatic_reboot_manager_ =
        g_browser_process->platform_part()->automatic_reboot_manager();

    // Wait for the |automatic_reboot_manager_| to finish initializing.
    base::RunLoop run_loop;
    base::SequencedWorkerPool* worker_pool =
        content::BrowserThread::GetBlockingPool();
    // Ensure that the initialization task the |automatic_reboot_manager_| posts
    // to the blocking pool has finished by posting another task with the same
    // |SequenceToken| and waiting for it to finish.
    worker_pool->PostSequencedWorkerTask(
        worker_pool->GetNamedSequenceToken("automatic-reboot-manager"),
        FROM_HERE, base::Bind(&RunCallback, base::ThreadTaskRunnerHandle::Get(),
                              run_loop.QuitClosure()));
    run_loop.Run();
    // Ensure that the |automatic_reboot_manager_| has had a chance to fully
    // process the result of the task posted to the blocking pool.
    base::RunLoop().RunUntilIdle();

    automatic_reboot_manager_->AddObserver(this);
  }

  // system::AutomaticRebootManagerObserver:
  void OnRebootRequested(
      system::AutomaticRebootManagerObserver::Reason) override {
    if (run_loop_)
      run_loop_->Quit();
  }

  void WillDestroyAutomaticRebootManager() override {
    automatic_reboot_manager_->RemoveObserver(this);
  }

  void CreateKioskAppUpdateService() {
    EXPECT_FALSE(update_service_);
    update_service_ = KioskAppUpdateServiceFactory::GetForProfile(profile());
    update_service_->Init(app_->id());
  }

  void FireAppUpdateAvailable() {
    update_service_->OnAppUpdateAvailable(app_);
  }

  void FireUpdatedNeedReboot() {
    UpdateEngineClient::Status status;
    status.status = UpdateEngineClient::UPDATE_STATUS_UPDATED_NEED_REBOOT;
    run_loop_.reset(new base::RunLoop);
    automatic_reboot_manager_->UpdateStatusChanged(status);
    run_loop_->Run();
  }

  void RequestPeriodicReboot() {
    run_loop_.reset(new base::RunLoop);
    g_browser_process->local_state()->SetInteger(
        prefs::kUptimeLimit, base::TimeDelta::FromHours(2).InSeconds());
    run_loop_->Run();
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedPathOverride> uptime_file_override_;
  const extensions::Extension* app_;  // Not owned.
  KioskAppUpdateService* update_service_;  // Not owned.
  system::AutomaticRebootManager* automatic_reboot_manager_;  // Not owned.
  std::unique_ptr<base::RunLoop> run_loop_;

  DISALLOW_COPY_AND_ASSIGN(KioskAppUpdateServiceTest);
};

// Verifies that the app is notified a reboot is required when an app update
// becomes available.
IN_PROC_BROWSER_TEST_F(KioskAppUpdateServiceTest, AppUpdate) {
  CreateKioskAppUpdateService();

  ExtensionTestMessageListener listener("app_update", false);
  FireAppUpdateAvailable();
  listener.WaitUntilSatisfied();
}

// Verifies that the app is notified a reboot is required when an OS update is
// applied while Chrome is running and the policy to reboot after update is
// enabled.
IN_PROC_BROWSER_TEST_F(KioskAppUpdateServiceTest, OsUpdate) {
  CreateKioskAppUpdateService();

  g_browser_process->local_state()->SetBoolean(prefs::kRebootAfterUpdate, true);
  ExtensionTestMessageListener listener("os_update", false);
  FireUpdatedNeedReboot();
  listener.WaitUntilSatisfied();
}

// Verifies that the app is notified a reboot is required when a periodic reboot
// is requested while Chrome is running.
IN_PROC_BROWSER_TEST_F(KioskAppUpdateServiceTest, Periodic) {
  CreateKioskAppUpdateService();

  ExtensionTestMessageListener listener("periodic", false);
  RequestPeriodicReboot();
  listener.WaitUntilSatisfied();
}

// Verifies that the app is notified a reboot is required when an OS update was
// applied before Chrome was started and the policy to reboot after update is
// enabled.
IN_PROC_BROWSER_TEST_F(KioskAppUpdateServiceTest, StartAfterOsUpdate) {
  g_browser_process->local_state()->SetBoolean(prefs::kRebootAfterUpdate, true);
  FireUpdatedNeedReboot();

  ExtensionTestMessageListener listener("os_update", false);
  CreateKioskAppUpdateService();
  listener.WaitUntilSatisfied();
}

// Verifies that the app is notified a reboot is required when a periodic reboot
// was requested before Chrome was started.
IN_PROC_BROWSER_TEST_F(KioskAppUpdateServiceTest, StartAfterPeriodic) {
  RequestPeriodicReboot();

  ExtensionTestMessageListener listener("periodic", false);
  CreateKioskAppUpdateService();
  listener.WaitUntilSatisfied();
}

}  // namespace chromeos
