/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WebGLContextGroup_h
#define WebGLContextGroup_h

#include "modules/webgl/WebGLRenderingContextBase.h"
#include "wtf/HashSet.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefCounted.h"

namespace blink {
class WebGraphicsContext3D;
}

namespace blink {

class WebGLSharedObject;
class WebGLRenderingContextBase;

typedef int ExceptionCode;

class WebGLContextGroup final : public RefCounted<WebGLContextGroup> {
public:
    static PassRefPtr<WebGLContextGroup> create();
    ~WebGLContextGroup();

    void addContext(WebGLRenderingContextBase*);
    void removeContext(WebGLRenderingContextBase*);

    void addObject(WebGLSharedObject*);
    void removeObject(WebGLSharedObject*);

    gpu::gles2::GLES2Interface* getAGLInterface();

    void loseContextGroup(WebGLRenderingContextBase::LostContextMode, WebGLRenderingContextBase::AutoRecoveryMethod);

private:
    friend class WebGLObject;

    WebGLContextGroup();

    void detachAndRemoveAllObjects();

    // FIXME: Oilpan: this object is not on the heap, but keeps untraced
    // pointers to on-heap objects in the two hash sets below.
    // The objects are responsible for managing their
    // registration with WebGLContextGroup, and vice versa, the
    // WebGLContextGroup takes care of detaching the group objects if
    // the set of WebGLRenderingContextBase contexts becomes empty.
    HashSet<UntracedMember<WebGLRenderingContextBase>> m_contexts;
    HashSet<UntracedMember<WebGLSharedObject>> m_groupObjects;
};

} // namespace blink

#endif // WebGLContextGroup_h
