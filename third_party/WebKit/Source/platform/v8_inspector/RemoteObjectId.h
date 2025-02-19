// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RemoteObjectId_h
#define RemoteObjectId_h

#include "platform/inspector_protocol/String16.h"
#include "platform/inspector_protocol/TypeBuilder.h"
#include "wtf/PassOwnPtr.h"

namespace blink {

namespace protocol {
class DictionaryValue;
}

class RemoteObjectIdBase {
public:
    int contextId() const { return m_injectedScriptId; }

protected:
    RemoteObjectIdBase();
    ~RemoteObjectIdBase() { }

    PassOwnPtr<protocol::DictionaryValue> parseInjectedScriptId(const String16&);

    int m_injectedScriptId;
};

class RemoteObjectId final : public RemoteObjectIdBase {
public:
    static PassOwnPtr<RemoteObjectId> parse(ErrorString*, const String16&);
    ~RemoteObjectId() { }
    int id() const { return m_id; }

private:
    RemoteObjectId();

    int m_id;
};

class RemoteCallFrameId final : public RemoteObjectIdBase {
public:
    static PassOwnPtr<RemoteCallFrameId> parse(ErrorString*, const String16&);
    ~RemoteCallFrameId() { }

    int frameOrdinal() const { return m_frameOrdinal; }

    static String16 serialize(int injectedScriptId, int frameOrdinal);
private:
    RemoteCallFrameId();

    int m_frameOrdinal;
};

} // namespace blink

#endif // !defined(RemoteObjectId_h)
