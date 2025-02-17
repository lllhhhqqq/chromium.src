// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/dragdrop/os_exchange_data_provider_mac.h"

#import <Cocoa/Cocoa.h>

#include "base/logging.h"
#include "base/pickle.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#import "third_party/mozilla/NSPasteboard+Utils.h"
#import "ui/base/clipboard/clipboard_util_mac.h"
#include "url/gurl.h"

namespace ui {

OSExchangeDataProviderMac::OSExchangeDataProviderMac()
    : pasteboard_(new ui::UniquePasteboard) {}

OSExchangeDataProviderMac::OSExchangeDataProviderMac(
    scoped_refptr<ui::UniquePasteboard> pb)
    : pasteboard_(pb) {}

OSExchangeDataProviderMac::~OSExchangeDataProviderMac() {
}

OSExchangeData::Provider* OSExchangeDataProviderMac::Clone() const {
  return new OSExchangeDataProviderMac(pasteboard_);
}

void OSExchangeDataProviderMac::MarkOriginatedFromRenderer() {
  NOTIMPLEMENTED();
}

bool OSExchangeDataProviderMac::DidOriginateFromRenderer() const {
  NOTIMPLEMENTED();
  return false;
}

void OSExchangeDataProviderMac::SetString(const base::string16& string) {
  [pasteboard_->get() writeObjects:@[ base::SysUTF16ToNSString(string) ]];
}

void OSExchangeDataProviderMac::SetURL(const GURL& url,
                                       const base::string16& title) {
  NSURL* ns_url = [NSURL URLWithString:base::SysUTF8ToNSString(url.spec())];
  [pasteboard_->get() writeObjects:@[ ns_url ]];

  [pasteboard_->get() setString:base::SysUTF16ToNSString(title)
                        forType:kCorePasteboardFlavorType_urln];
}

void OSExchangeDataProviderMac::SetFilename(const base::FilePath& path) {
  [pasteboard_->get() setPropertyList:@[ base::SysUTF8ToNSString(path.value()) ]
                              forType:NSFilenamesPboardType];
}

void OSExchangeDataProviderMac::SetFilenames(
    const std::vector<FileInfo>& filenames) {
  NOTIMPLEMENTED();
}

void OSExchangeDataProviderMac::SetPickledData(
    const Clipboard::FormatType& format,
    const base::Pickle& data) {
  NSData* ns_data = [NSData dataWithBytes:data.data() length:data.size()];
  [pasteboard_->get() setData:ns_data forType:format.ToNSString()];
}

bool OSExchangeDataProviderMac::GetString(base::string16* data) const {
  DCHECK(data);
  NSArray* items =
      [pasteboard_->get() readObjectsForClasses:@[ [NSString class] ]
                                        options:@{}];

  // There was no NSString, check for an NSURL.
  if ([items count] == 0) {
    GURL url;
    base::string16 title;
    bool result =
        GetURLAndTitle(OSExchangeData::DO_NOT_CONVERT_FILENAMES, &url, &title);
    if (result)
      *data = base::UTF8ToUTF16(url.spec());
    return result;
  }

  *data = base::SysNSStringToUTF16([items objectAtIndex:0]);
  return true;
}

bool OSExchangeDataProviderMac::GetURLAndTitle(
    OSExchangeData::FilenameToURLPolicy policy,
    GURL* url,
    base::string16* title) const {
  DCHECK(url);
  DCHECK(title);
  NSArray* items =
      [pasteboard_->get() readObjectsForClasses:@[ [NSURL class] ] options:@{}];
  if ([items count] == 0)
    return false;

  NSURL* ns_url = [items objectAtIndex:0];

  if (policy == OSExchangeData::DO_NOT_CONVERT_FILENAMES) {
    // If the URL matches a filename, assume that it came from SetFilename().
    // Don't return it if we are not supposed to convert filename to URL.
    NSArray* paths =
        [pasteboard_->get() propertyListForType:NSFilenamesPboardType];
    NSString* url_path = [[ns_url path] stringByStandardizingPath];
    for (NSString* path in paths) {
      if ([[path stringByStandardizingPath] isEqualToString:url_path])
        return false;
    }
  }

  *url = GURL([[ns_url absoluteString] UTF8String]);
  *title = base::SysNSStringToUTF16(
      [pasteboard_->get() stringForType:kCorePasteboardFlavorType_urln]);
  return true;
}

bool OSExchangeDataProviderMac::GetFilename(base::FilePath* path) const {
  NSArray* paths =
      [pasteboard_->get() propertyListForType:NSFilenamesPboardType];
  if ([paths count] == 0)
    return false;

  *path = base::FilePath([[paths objectAtIndex:0] UTF8String]);
  return true;
}

bool OSExchangeDataProviderMac::GetFilenames(
    std::vector<FileInfo>* filenames) const {
  NOTIMPLEMENTED();
  return false;
}

bool OSExchangeDataProviderMac::GetPickledData(
    const Clipboard::FormatType& format,
    base::Pickle* data) const {
  DCHECK(data);
  NSData* ns_data = [pasteboard_->get() dataForType:format.ToNSString()];
  if (!ns_data)
    return false;

  *data =
      base::Pickle(static_cast<const char*>([ns_data bytes]), [ns_data length]);
  return true;
}

bool OSExchangeDataProviderMac::HasString() const {
  NSArray* classes = @[ [NSString class] ];
  return [pasteboard_->get() canReadObjectForClasses:classes options:nil];
}

bool OSExchangeDataProviderMac::HasURL(
    OSExchangeData::FilenameToURLPolicy policy) const {
  GURL url;
  base::string16 title;
  return GetURLAndTitle(policy, &url, &title);
}

bool OSExchangeDataProviderMac::HasFile() const {
  return [[pasteboard_->get() types] containsObject:NSFilenamesPboardType];
}

bool OSExchangeDataProviderMac::HasCustomFormat(
    const Clipboard::FormatType& format) const {
  return [[pasteboard_->get() types] containsObject:format.ToNSString()];
}

///////////////////////////////////////////////////////////////////////////////
// OSExchangeData, public:

// static
OSExchangeData::Provider* OSExchangeData::CreateProvider() {
  return new OSExchangeDataProviderMac;
}

}  // namespace ui
