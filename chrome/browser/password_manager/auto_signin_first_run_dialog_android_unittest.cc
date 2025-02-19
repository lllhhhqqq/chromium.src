// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "base/test/histogram_tester.h"
#include "chrome/browser/password_manager/auto_signin_first_run_dialog_android.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/password_manager/core/browser/password_manager_metrics_util.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

class AutoSigninFirstRunDialogAndroidTest
    : public ChromeRenderViewHostTestHarness {
 public:
  AutoSigninFirstRunDialogAndroidTest() {}
  ~AutoSigninFirstRunDialogAndroidTest() override {}

  PrefService* prefs();

 protected:
  std::unique_ptr<AutoSigninFirstRunDialogAndroid> CreateDialog();

 private:
  DISALLOW_COPY_AND_ASSIGN(AutoSigninFirstRunDialogAndroidTest);
};

std::unique_ptr<AutoSigninFirstRunDialogAndroid>
AutoSigninFirstRunDialogAndroidTest::CreateDialog() {
  std::unique_ptr<AutoSigninFirstRunDialogAndroid> dialog(
      new AutoSigninFirstRunDialogAndroid(web_contents()));
  return dialog;
}

PrefService* AutoSigninFirstRunDialogAndroidTest::prefs() {
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  return profile->GetPrefs();
}

TEST_F(AutoSigninFirstRunDialogAndroidTest,
       CheckPrefValueAfterFirstRunMessageWasShown) {
  prefs()->SetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown, false);
  std::unique_ptr<AutoSigninFirstRunDialogAndroid> dialog(CreateDialog());
  dialog.reset();
  EXPECT_FALSE(prefs()->GetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown));
}

TEST_F(AutoSigninFirstRunDialogAndroidTest,
       CheckResetOfPrefAfterFirstRunMessageWasShownOnTurnOkClicked) {
  base::HistogramTester histogram_tester;
  prefs()->SetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown, false);
  prefs()->SetBoolean(password_manager::prefs::kCredentialsEnableAutosignin,
                      true);
  std::unique_ptr<AutoSigninFirstRunDialogAndroid> dialog(CreateDialog());
  dialog->OnOkClicked(base::android::AttachCurrentThread(), nullptr);
  dialog.reset();
  EXPECT_TRUE(prefs()->GetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown));
  EXPECT_TRUE(prefs()->GetBoolean(
      password_manager::prefs::kCredentialsEnableAutosignin));
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.AutoSigninFirstRunDialog",
      password_manager::metrics_util::AUTO_SIGNIN_OK_GOT_IT, 1);
}

TEST_F(AutoSigninFirstRunDialogAndroidTest,
       CheckResetOfPrefAfterFirstRunMessageWasShownOnTurnOffClicked) {
  base::HistogramTester histogram_tester;
  prefs()->SetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown, false);
  prefs()->SetBoolean(password_manager::prefs::kCredentialsEnableAutosignin,
                      true);
  std::unique_ptr<AutoSigninFirstRunDialogAndroid> dialog(CreateDialog());
  dialog->OnTurnOffClicked(base::android::AttachCurrentThread(), nullptr);
  dialog.reset();
  EXPECT_TRUE(prefs()->GetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown));
  EXPECT_FALSE(prefs()->GetBoolean(
      password_manager::prefs::kCredentialsEnableAutosignin));
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.AutoSigninFirstRunDialog",
      password_manager::metrics_util::AUTO_SIGNIN_TURN_OFF, 1);
}
