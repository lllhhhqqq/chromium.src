// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/CSSFontFamilyValue.h"

#include "core/css/CSSMarkup.h"
#include "wtf/text/WTFString.h"

namespace blink {

CSSFontFamilyValue::CSSFontFamilyValue(const String& str)
    : CSSValue(FontFamilyClass)
    , m_string(str) { }

String CSSFontFamilyValue::customCSSText() const
{
    return serializeFontFamily(m_string);
}

DEFINE_TRACE_AFTER_DISPATCH(CSSFontFamilyValue)
{
    CSSValue::traceAfterDispatch(visitor);
}

} // namespace blink
