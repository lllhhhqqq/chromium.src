/*
 * Copyright (C) 1997 Martin Jones (mjones@kde.org)
 *           (C) 1997 Torben Weis (weis@kde.org)
 *           (C) 1998 Waldo Bastian (bastian@kde.org)
 *           (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2009 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
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
 */

#ifndef LayoutTableCol_h
#define LayoutTableCol_h

#include "core/layout/LayoutBox.h"

namespace blink {

class LayoutTable;
class LayoutTableCell;

// LayoutTableCol is used to represent table column or column groups
// (display: table-column and display: table-column-group).
//
// The reason to use the same LayoutObject is that both objects behave in a very
// similar way. The main difference between the 2 is that table-column-group
// allows table-column children, when table-column don't.
// Note that this matches how <col> and <colgroup> map to the same class:
// HTMLTableColElement.
//
// In HTML and CSS, table columns and colgroups don't own the cells, they are
// descendants of the rows.
// As such table columns and colgroups have a very limited scope in the table:
// - column / cell sizing (the 'width' property)
// - background painting (the 'background' property).
// - border collapse resolution
//   (http://www.w3.org/TR/CSS21/tables.html#border-conflict-resolution)
//
// See http://www.w3.org/TR/CSS21/tables.html#columns for the standard.
// Note that we don't implement the "visibility: collapse" inheritance to the
// cells.
//
// Because table columns and column groups are placeholder elements (see
// previous paragraph), they are never laid out and layout() should not be
// called on them.
class LayoutTableCol final : public LayoutBox {
public:
    explicit LayoutTableCol(Element*);

    LayoutObject* firstChild() const { ASSERT(children() == virtualChildren()); return children()->firstChild(); }

    // If you have a LayoutTableCol, use firstChild or lastChild instead.
    void slowFirstChild() const = delete;
    void slowLastChild() const = delete;

    const LayoutObjectChildList* children() const { return &m_children; }
    LayoutObjectChildList* children() { return &m_children; }

    void clearPreferredLogicalWidthsDirtyBits();

    // The 'span' attribute in HTML.
    // For CSS table columns or colgroups, this is always 1.
    unsigned span() const { return m_span; }

    bool isTableColumnGroupWithColumnChildren() { return firstChild(); }
    bool isTableColumn() const { return style()->display() == TABLE_COLUMN; }
    bool isTableColumnGroup() const { return style()->display() == TABLE_COLUMN_GROUP; }

    LayoutTableCol* enclosingColumnGroup() const;

    // Returns the next column or column-group.
    LayoutTableCol* nextColumn() const;

    const BorderValue& borderAdjoiningCellStartBorder(const LayoutTableCell*) const;
    const BorderValue& borderAdjoiningCellEndBorder(const LayoutTableCell*) const;
    const BorderValue& borderAdjoiningCellBefore(const LayoutTableCell*) const;
    const BorderValue& borderAdjoiningCellAfter(const LayoutTableCell*) const;

    const char* name() const override { return "LayoutTableCol"; }

private:
    LayoutObjectChildList* virtualChildren() override { return children(); }
    const LayoutObjectChildList* virtualChildren() const override { return children(); }

    bool isOfType(LayoutObjectType type) const override { return type == LayoutObjectLayoutTableCol || LayoutBox::isOfType(type); }
    void updateFromElement() override;
    void computePreferredLogicalWidths() override { ASSERT_NOT_REACHED(); }

    void insertedIntoTree() override;
    void willBeRemovedFromTree() override;

    bool isChildAllowed(LayoutObject*, const ComputedStyle&) const override;
    bool canHaveChildren() const override;
    PaintLayerType layerTypeRequired() const override { return NoPaintLayer; }

    LayoutRect localOverflowRectForPaintInvalidation() const override;
    void imageChanged(WrappedImagePtr, const IntRect* = nullptr) override;

    void styleDidChange(StyleDifference, const ComputedStyle* oldStyle) override;

    LayoutTable* table() const;

    LayoutObjectChildList m_children;
    unsigned m_span;
};

DEFINE_LAYOUT_OBJECT_TYPE_CASTS(LayoutTableCol, isLayoutTableCol());

} // namespace blink

#endif
