// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/compositing/CompositingInputsUpdater.h"

#include "core/layout/LayoutBlock.h"
#include "core/layout/compositing/CompositedLayerMapping.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/paint/PaintLayer.h"
#include "platform/TraceEvent.h"

namespace blink {

CompositingInputsUpdater::CompositingInputsUpdater(PaintLayer* rootLayer)
    : m_geometryMap(UseTransforms)
    , m_rootLayer(rootLayer)
{
}

CompositingInputsUpdater::~CompositingInputsUpdater()
{
}

void CompositingInputsUpdater::update()
{
    TRACE_EVENT0("blink", "CompositingInputsUpdater::update");
    updateRecursive(m_rootLayer, DoNotForceUpdate, AncestorInfo());
}

static const PaintLayer* findParentLayerOnClippingContainerChain(const PaintLayer* layer)
{
    LayoutObject* current = layer->layoutObject();
    while (current) {
        if (current->style()->position() == FixedPosition) {
            for (current = current->parent(); current && !current->canContainFixedPositionObjects(); current = current->parent()) {
                // All types of clips apply to fixed-position descendants of other fixed-position elements.
                // Note: it's unclear whether this is what the spec says. Firefox does not clip, but Chrome does.
                if (current->style()->position() == FixedPosition && current->hasClipRelatedProperty()) {
                    ASSERT(current->hasLayer());
                    return static_cast<const LayoutBoxModelObject*>(current)->layer();
                }

                // CSS clip applies to fixed position elements even for ancestors that are not what the
                // fixed element is positioned with respect to.
                if (current->hasClip()) {
                    ASSERT(current->hasLayer());
                    return static_cast<const LayoutBoxModelObject*>(current)->layer();
                }
            }
        } else {
            current = current->containingBlock();
        }

        if (current->hasLayer())
            return static_cast<const LayoutBoxModelObject*>(current)->layer();
        // Having clip or overflow clip forces the LayoutObject to become a layer.
        ASSERT(!current->hasClipRelatedProperty());
    }
    ASSERT_NOT_REACHED();
    return nullptr;
}

static const PaintLayer* findParentLayerOnContainingBlockChain(const LayoutObject* object)
{
    for (const LayoutObject* current = object; current; current = current->containingBlock()) {
        if (current->hasLayer())
            return static_cast<const LayoutBoxModelObject*>(current)->layer();
    }
    ASSERT_NOT_REACHED();
    return nullptr;
}

static bool hasClippedStackingAncestor(const PaintLayer* layer, const PaintLayer* clippingLayer)
{
    if (layer == clippingLayer)
        return false;
    bool foundInterveningClip = false;
    const LayoutObject* clippingLayoutObject = clippingLayer->layoutObject();
    for (const PaintLayer* current = layer->compositingContainer(); current; current = current->compositingContainer()) {
        if (current == clippingLayer)
            return foundInterveningClip;

        if (current->layoutObject()->hasClipRelatedProperty() && !clippingLayoutObject->isDescendantOf(current->layoutObject()))
            foundInterveningClip = true;

        if (const LayoutObject* container = current->clippingContainer()) {
            if (clippingLayoutObject != container && !clippingLayoutObject->isDescendantOf(container))
                foundInterveningClip = true;
        }
    }
    return false;
}

void CompositingInputsUpdater::updateRecursive(PaintLayer* layer, UpdateType updateType, AncestorInfo info)
{
    if (!layer->childNeedsCompositingInputsUpdate() && updateType != ForceUpdate)
        return;

    m_geometryMap.pushMappingsToAncestor(layer, layer->parent());

    if (layer->hasCompositedLayerMapping())
        info.enclosingCompositedLayer = layer;

    if (layer->needsCompositingInputsUpdate()) {
        if (info.enclosingCompositedLayer)
            info.enclosingCompositedLayer->compositedLayerMapping()->setNeedsGraphicsLayerUpdate(GraphicsLayerUpdateSubtree);
        updateType = ForceUpdate;
    }

    if (updateType == ForceUpdate) {
        PaintLayer::AncestorDependentCompositingInputs properties;
        PaintLayer::RareAncestorDependentCompositingInputs rareProperties;

        if (!layer->isRootLayer()) {
            properties.clippedAbsoluteBoundingBox = enclosingIntRect(m_geometryMap.absoluteRect(FloatRect(layer->boundingBoxForCompositingOverlapTest())));
            // FIXME: Setting the absBounds to 1x1 instead of 0x0 makes very little sense,
            // but removing this code will make JSGameBench sad.
            // See https://codereview.chromium.org/13912020/
            if (properties.clippedAbsoluteBoundingBox.isEmpty())
                properties.clippedAbsoluteBoundingBox.setSize(IntSize(1, 1));

            IntRect clipRect = pixelSnappedIntRect(layer->clipper().backgroundClipRect(ClipRectsContext(m_rootLayer, AbsoluteClipRects)).rect());
            properties.clippedAbsoluteBoundingBox.intersect(clipRect);

            const PaintLayer* parent = layer->parent();
            rareProperties.opacityAncestor = parent->isTransparent() ? parent : parent->opacityAncestor();
            rareProperties.transformAncestor = parent->hasTransformRelatedProperty() ? parent : parent->transformAncestor();
            rareProperties.filterAncestor = parent->hasFilterInducingProperty() ? parent : parent->filterAncestor();
            bool layerIsFixedPosition = layer->layoutObject()->style()->position() == FixedPosition;
            rareProperties.nearestFixedPositionLayer = layerIsFixedPosition ? layer : parent->nearestFixedPositionLayer();

            if (info.hasAncestorWithClipRelatedProperty) {
                const PaintLayer* parentLayerOnClippingContainerChain = findParentLayerOnClippingContainerChain(layer);
                const bool parentHasClipRelatedProperty = parentLayerOnClippingContainerChain->layoutObject()->hasClipRelatedProperty();
                properties.clippingContainer = parentHasClipRelatedProperty ? parentLayerOnClippingContainerChain->layoutObject() : parentLayerOnClippingContainerChain->clippingContainer();
            }

            if (info.lastScrollingAncestor) {
                const LayoutObject* containingBlock = layer->layoutObject()->containingBlock();
                const PaintLayer* parentLayerOnContainingBlockChain = findParentLayerOnContainingBlockChain(containingBlock);

                rareProperties.ancestorScrollingLayer = parentLayerOnContainingBlockChain->ancestorScrollingLayer();
                if (parentLayerOnContainingBlockChain->scrollsOverflow())
                    rareProperties.ancestorScrollingLayer = parentLayerOnContainingBlockChain;

                if (layer->layoutObject()->isOutOfFlowPositioned() && !layer->subtreeIsInvisible()) {
                    const PaintLayer* clippingLayer = properties.clippingContainer ? properties.clippingContainer->enclosingLayer() : layer->compositor()->rootLayer();
                    if (hasClippedStackingAncestor(layer, clippingLayer))
                        rareProperties.clipParent = clippingLayer;
                }

                if (layer->stackingNode()->isStacked()
                    && rareProperties.ancestorScrollingLayer
                    && !info.ancestorStackingContext->layoutObject()->isDescendantOf(rareProperties.ancestorScrollingLayer->layoutObject()))
                    rareProperties.scrollParent = rareProperties.ancestorScrollingLayer;
            }
        }

        layer->updateAncestorDependentCompositingInputs(properties, rareProperties, info.hasAncestorWithClipPath);
    }

    if (layer->stackingNode()->isStackingContext())
        info.ancestorStackingContext = layer;

    if (layer->scrollsOverflow())
        info.lastScrollingAncestor = layer;

    if (layer->layoutObject()->hasClipRelatedProperty())
        info.hasAncestorWithClipRelatedProperty = true;

    if (layer->layoutObject()->hasClipPath())
        info.hasAncestorWithClipPath = true;

    bool hasDescendantWithClipPath = false;
    bool hasNonIsolatedDescendantWithBlendMode = false;
    for (PaintLayer* child = layer->firstChild(); child; child = child->nextSibling()) {
        updateRecursive(child, updateType, info);

        hasDescendantWithClipPath |= child->hasDescendantWithClipPath() || child->layoutObject()->hasClipPath();
        hasNonIsolatedDescendantWithBlendMode |= (!child->stackingNode()->isStackingContext() && child->hasNonIsolatedDescendantWithBlendMode()) || child->layoutObject()->style()->hasBlendMode();
    }

    layer->updateDescendantDependentCompositingInputs(hasDescendantWithClipPath, hasNonIsolatedDescendantWithBlendMode);
    layer->didUpdateCompositingInputs();

    m_geometryMap.popMappingsToAncestor(layer->parent());
}

#if ENABLE(ASSERT)

void CompositingInputsUpdater::assertNeedsCompositingInputsUpdateBitsCleared(PaintLayer* layer)
{
    ASSERT(!layer->childNeedsCompositingInputsUpdate());
    ASSERT(!layer->needsCompositingInputsUpdate());

    for (PaintLayer* child = layer->firstChild(); child; child = child->nextSibling())
        assertNeedsCompositingInputsUpdateBitsCleared(child);
}

#endif

} // namespace blink
