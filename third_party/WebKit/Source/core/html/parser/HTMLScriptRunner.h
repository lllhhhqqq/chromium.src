/*
 * Copyright (C) 2010 Google, Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HTMLScriptRunner_h
#define HTMLScriptRunner_h

#include "bindings/core/v8/ScriptStreamer.h"
#include "core/dom/PendingScript.h"
#include "core/fetch/ResourceClient.h"
#include "platform/heap/Handle.h"
#include "wtf/Deque.h"
#include "wtf/PassRefPtr.h"
#include "wtf/text/TextPosition.h"

namespace blink {

class Resource;
class Document;
class Element;
class HTMLScriptRunnerHost;

class HTMLScriptRunner final : public GarbageCollectedFinalized<HTMLScriptRunner>, private ScriptResourceClient {
    WTF_MAKE_NONCOPYABLE(HTMLScriptRunner);
    USING_PRE_FINALIZER(HTMLScriptRunner, detach);
public:
    static RawPtr<HTMLScriptRunner> create(Document* document, HTMLScriptRunnerHost* host)
    {
        return new HTMLScriptRunner(document, host);
    }
    ~HTMLScriptRunner();

    void detach();

    // Processes the passed in script and any pending scripts if possible.
    void execute(RawPtr<Element> scriptToProcess, const TextPosition& scriptStartPosition);

    void executeScriptsWaitingForLoad(Resource*);
    bool hasScriptsWaitingForResources() const { return m_hasScriptsWaitingForResources; }
    void executeScriptsWaitingForResources();
    bool executeScriptsWaitingForParsing();

    bool hasParserBlockingScript() const;
    bool isExecutingScript() const { return !!m_scriptNestingLevel; }

    // ResourceClient
    void notifyFinished(Resource*) override;
    String debugName() const override { return "HTMLScriptRunner"; }

    DECLARE_TRACE();

private:
    HTMLScriptRunner(Document*, HTMLScriptRunnerHost*);

    void executeParsingBlockingScript();
    void executePendingScriptAndDispatchEvent(PendingScript*, ScriptStreamer::Type);
    void executeParsingBlockingScripts();

    void requestParsingBlockingScript(Element*);
    void requestDeferredScript(Element*);
    bool requestPendingScript(PendingScript*, Element*) const;

    void runScript(Element*, const TextPosition& scriptStartPosition);

    bool isPendingScriptReady(const PendingScript*);

    void stopWatchingResourceForLoad(Resource*);

    Member<Document> m_document;
    Member<HTMLScriptRunnerHost> m_host;
    Member<PendingScript> m_parserBlockingScript;
    // http://www.whatwg.org/specs/web-apps/current-work/#list-of-scripts-that-will-execute-when-the-document-has-finished-parsing
    HeapDeque<Member<PendingScript>> m_scriptsToExecuteAfterParsing;
    unsigned m_scriptNestingLevel;

    // We only want stylesheet loads to trigger script execution if script
    // execution is currently stopped due to stylesheet loads, otherwise we'd
    // cause nested script execution when parsing <style> tags since </style>
    // tags can cause Document to call executeScriptsWaitingForResources.
    bool m_hasScriptsWaitingForResources;
};

} // namespace blink

#endif
