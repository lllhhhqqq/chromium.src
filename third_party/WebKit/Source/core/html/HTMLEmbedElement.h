/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2004, 2006, 2008, 2009 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef HTMLEmbedElement_h
#define HTMLEmbedElement_h

#include "core/html/HTMLPlugInElement.h"

namespace blink {

class HTMLEmbedElement final : public HTMLPlugInElement {
    DEFINE_WRAPPERTYPEINFO();
public:
    static RawPtr<HTMLEmbedElement> create(Document&, bool createdByParser = false);

    bool isExposed() const;

private:
    HTMLEmbedElement(Document&, bool createdByParser);

    void parseAttribute(const QualifiedName&, const AtomicString&, const AtomicString&) override;
    bool isPresentationAttribute(const QualifiedName&) const override;
    void collectStyleForPresentationAttribute(const QualifiedName&, const AtomicString&, MutableStylePropertySet*) override;

    bool layoutObjectIsNeeded(const ComputedStyle&) override;

    bool isURLAttribute(const Attribute&) const override;
    const QualifiedName& subResourceAttributeName() const override;

    LayoutPart* existingLayoutPart() const override;

    void updateWidgetInternal() override;

    void parametersForPlugin(Vector<String>& paramNames, Vector<String>& paramValues);

    bool shouldRegisterAsNamedItem() const override { return true; }
    bool isInteractiveContent() const override;
};

} // namespace blink

#endif // HTMLEmbedElement_h
