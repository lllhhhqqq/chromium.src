// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file can be empty. Its purpose is to contain the relatively short lived
// definitions required for experimental flags.

#include "ios/chrome/browser/experimental_flags.h"

#include <dispatch/dispatch.h>
#import <Foundation/Foundation.h>

#include <string>

#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string_util.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/variations/variations_associated_data.h"
#include "ios/chrome/browser/chrome_switches.h"
#include "ios/web/public/web_view_creation_util.h"

namespace {
NSString* const kEnableAlertOnBackgroundUpload =
    @"EnableAlertsOnBackgroundUpload";
NSString* const kEnableViewCopyPasswords = @"EnableViewCopyPasswords";
NSString* const kHeuristicsForPasswordGeneration =
    @"HeuristicsForPasswordGeneration";
NSString* const kEnableReadingList = @"EnableReadingList";
}  // namespace

namespace experimental_flags {

bool IsAlertOnBackgroundUploadEnabled() {
  return [[NSUserDefaults standardUserDefaults]
      boolForKey:kEnableAlertOnBackgroundUpload];
}

bool IsBookmarkCollectionEnabled() {
  return true;
}

bool IsLRUSnapshotCacheEnabled() {
  // Check if the experimental flag is forced on or off.
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableLRUSnapshotCache)) {
    return true;
  } else if (command_line->HasSwitch(switches::kDisableLRUSnapshotCache)) {
    return false;
  }

  // Check if the finch experiment is turned on.
  std::string group_name =
      base::FieldTrialList::FindFullName("IOSLRUSnapshotCache");
  return base::StartsWith(group_name, "Enabled",
                          base::CompareCase::INSENSITIVE_ASCII);
}

bool IsViewCopyPasswordsEnabled() {
  NSString* viewCopyPasswordFlag = [[NSUserDefaults standardUserDefaults]
      objectForKey:kEnableViewCopyPasswords];
  if ([viewCopyPasswordFlag isEqualToString:@"Enabled"])
    return true;
  return false;
}

bool IsPasswordGenerationEnabled() {
  // This call activates the field trial, if needed, so it must come before any
  // early returns.
  std::string group_name =
      base::FieldTrialList::FindFullName("PasswordGeneration");
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableIOSPasswordGeneration))
    return true;
  if (command_line->HasSwitch(switches::kDisableIOSPasswordGeneration))
    return false;
  return group_name != "Disabled";
}

bool UseOnlyLocalHeuristicsForPasswordGeneration() {
  if ([[NSUserDefaults standardUserDefaults]
          boolForKey:kHeuristicsForPasswordGeneration]) {
    return true;
  }
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  return command_line->HasSwitch(
      autofill::switches::kLocalHeuristicsOnlyForPasswordGeneration);
}

bool IsTabSwitcherEnabled() {
  // Check if the experimental flag is forced on or off.
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableTabSwitcher)) {
    return true;
  } else if (command_line->HasSwitch(switches::kDisableTabSwitcher)) {
    return false;
  }

  // Check if the finch experiment is turned on.
  std::string group_name = base::FieldTrialList::FindFullName("IOSTabSwitcher");
  return base::StartsWith(group_name, "Enabled",
                          base::CompareCase::INSENSITIVE_ASCII);
}

bool IsReadingListEnabled() {
  return [[NSUserDefaults standardUserDefaults] boolForKey:kEnableReadingList];
}

}  // namespace experimental_flags
