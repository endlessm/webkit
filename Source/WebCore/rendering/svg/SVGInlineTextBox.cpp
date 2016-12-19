/**
 * Copyright (C) 2007 Rob Buis <buis@kde.org>
 * Copyright (C) 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
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

#include "config.h"
#include "SVGInlineTextBox.h"

#include "Frame.h"
#include "FrameView.h"
#include "GraphicsContext.h"
#include "HitTestResult.h"
#include "InlineFlowBox.h"
#include "PointerEventsHitRules.h"
#include "RenderBlock.h"
#include "RenderInline.h"
#include "RenderSVGResourceSolidColor.h"
#include "RenderView.h"
#include "SVGRenderingContext.h"
#include "SVGResourcesCache.h"
#include "SVGRootInlineBox.h"
#include "TextPainter.h"

namespace WebCore {

struct ExpectedSVGInlineTextBoxSize : public InlineTextBox {
    float float1;
    uint32_t bitfields : 5;
    void* pointer;
    Vector<SVGTextFragment> vector;
};

COMPILE_ASSERT(sizeof(SVGInlineTextBox) == sizeof(ExpectedSVGInlineTextBoxSize), SVGInlineTextBox_is_not_of_expected_size);

SVGInlineTextBox::SVGInlineTextBox(RenderSVGInlineText& renderer)
    : InlineTextBox(renderer)
    , m_logicalHeight(0)
    , m_paintingResourceMode(ApplyToDefaultMode)
    , m_startsNewTextChunk(false)
    , m_paintingResource(nullptr)
{
}

void SVGInlineTextBox::dirtyOwnLineBoxes()
{
    InlineTextBox::dirtyLineBoxes();

    // Clear the now stale text fragments
    clearTextFragments();
}

void SVGInlineTextBox::dirtyLineBoxes()
{
    dirtyOwnLineBoxes();

    // And clear any following text fragments as the text on which they
    // depend may now no longer exist, or glyph positions may be wrong
    for (InlineTextBox* nextBox = nextTextBox(); nextBox; nextBox = nextBox->nextTextBox())
        nextBox->dirtyOwnLineBoxes();
}

int SVGInlineTextBox::offsetForPosition(float, bool) const
{
    // SVG doesn't use the standard offset <-> position selection system, as it's not suitable for SVGs complex needs.
    // vertical text selection, inline boxes spanning multiple lines (contrary to HTML, etc.)
    ASSERT_NOT_REACHED();
    return 0;
}

int SVGInlineTextBox::offsetForPositionInFragment(const SVGTextFragment& fragment, float position, bool includePartialGlyphs) const
{
   float scalingFactor = renderer().scalingFactor();
    ASSERT(scalingFactor);

    TextRun textRun = constructTextRun(renderer().style(), fragment);

    // Eventually handle lengthAdjust="spacingAndGlyphs".
    // FIXME: Handle vertical text.
    AffineTransform fragmentTransform;
    fragment.buildFragmentTransform(fragmentTransform);
    if (!fragmentTransform.isIdentity())
        textRun.setHorizontalGlyphStretch(narrowPrecisionToFloat(fragmentTransform.xScale()));

    return fragment.characterOffset - start() + renderer().scaledFont().offsetForPosition(textRun, position * scalingFactor, includePartialGlyphs);
}

float SVGInlineTextBox::positionForOffset(unsigned) const
{
    // SVG doesn't use the offset <-> position selection system. 
    ASSERT_NOT_REACHED();
    return 0;
}

FloatRect SVGInlineTextBox::selectionRectForTextFragment(const SVGTextFragment& fragment, unsigned startPosition, unsigned endPosition, const RenderStyle& style) const
{
    ASSERT_WITH_SECURITY_IMPLICATION(startPosition < endPosition);

    float scalingFactor = renderer().scalingFactor();
    ASSERT(scalingFactor);

    const FontCascade& scaledFont = renderer().scaledFont();
    const FontMetrics& scaledFontMetrics = scaledFont.fontMetrics();
    FloatPoint textOrigin(fragment.x, fragment.y);
    if (scalingFactor != 1)
        textOrigin.scale(scalingFactor, scalingFactor);

    textOrigin.move(0, -scaledFontMetrics.floatAscent());

    LayoutRect selectionRect = LayoutRect(textOrigin, LayoutSize(0, fragment.height * scalingFactor));
    TextRun run = constructTextRun(style, fragment);
    scaledFont.adjustSelectionRectForText(run, selectionRect, startPosition, endPosition);
    FloatRect snappedSelectionRect = snapRectToDevicePixelsWithWritingDirection(selectionRect, renderer().document().deviceScaleFactor(), run.ltr());
    if (scalingFactor == 1)
        return snappedSelectionRect;

    snappedSelectionRect.scale(1 / scalingFactor);
    return snappedSelectionRect;
}

LayoutRect SVGInlineTextBox::localSelectionRect(unsigned startPosition, unsigned endPosition) const
{
    startPosition = clampedOffset(startPosition);
    endPosition = clampedOffset(endPosition);
    if (startPosition >= endPosition)
        return LayoutRect();

    auto& style = renderer().style();

    AffineTransform fragmentTransform;
    FloatRect selectionRect;
    unsigned fragmentStartPosition = 0;
    unsigned fragmentEndPosition = 0;

    unsigned textFragmentsSize = m_textFragments.size();
    for (unsigned i = 0; i < textFragmentsSize; ++i) {
        const SVGTextFragment& fragment = m_textFragments.at(i);

        fragmentStartPosition = startPosition;
        fragmentEndPosition = endPosition;
        if (!mapStartEndPositionsIntoFragmentCoordinates(fragment, fragmentStartPosition, fragmentEndPosition))
            continue;

        FloatRect fragmentRect = selectionRectForTextFragment(fragment, fragmentStartPosition, fragmentEndPosition, style);
        fragment.buildFragmentTransform(fragmentTransform);
        if (!fragmentTransform.isIdentity())
            fragmentRect = fragmentTransform.mapRect(fragmentRect);

        selectionRect.unite(fragmentRect);
    }

    return enclosingIntRect(selectionRect);
}

static inline bool textShouldBePainted(const RenderSVGInlineText& textRenderer)
{
    // FontCascade::pixelSize(), returns FontDescription::computedPixelSize(), which returns "int(x + 0.5)".
    // If the absolute font size on screen is below x=0.5, don't render anything.
    return textRenderer.scaledFont().pixelSize();
}

void SVGInlineTextBox::paintSelectionBackground(PaintInfo& paintInfo)
{
    ASSERT(paintInfo.shouldPaintWithinRoot(renderer()));
    ASSERT(paintInfo.phase == PaintPhaseForeground || paintInfo.phase == PaintPhaseSelection);
    ASSERT(truncation() == cNoTruncation);

    if (renderer().style().visibility() != VISIBLE)
        return;

    auto& parentRenderer = parent()->renderer();
    ASSERT(!parentRenderer.document().printing());

    // Determine whether or not we're selected.
    bool paintSelectedTextOnly = paintInfo.phase == PaintPhaseSelection;
    bool hasSelection = selectionState() != RenderObject::SelectionNone;
    if (!hasSelection || paintSelectedTextOnly)
        return;

    Color backgroundColor = renderer().selectionBackgroundColor();
    if (!backgroundColor.isValid() || !backgroundColor.alpha())
        return;

    if (!textShouldBePainted(renderer()))
        return;

    auto& style = parentRenderer.style();

    unsigned startPosition;
    unsigned endPosition;
    std::tie(startPosition, endPosition) = selectionStartEnd();

    unsigned fragmentStartPosition = 0;
    unsigned fragmentEndPosition = 0;
    AffineTransform fragmentTransform;
    unsigned textFragmentsSize = m_textFragments.size();
    for (unsigned i = 0; i < textFragmentsSize; ++i) {
        SVGTextFragment& fragment = m_textFragments.at(i);
        ASSERT(!m_paintingResource);

        fragmentStartPosition = startPosition;
        fragmentEndPosition = endPosition;
        if (!mapStartEndPositionsIntoFragmentCoordinates(fragment, fragmentStartPosition, fragmentEndPosition))
            continue;

        GraphicsContextStateSaver stateSaver(paintInfo.context());
        fragment.buildFragmentTransform(fragmentTransform);
        if (!fragmentTransform.isIdentity())
            paintInfo.context().concatCTM(fragmentTransform);

        paintInfo.context().setFillColor(backgroundColor);
        paintInfo.context().fillRect(selectionRectForTextFragment(fragment, fragmentStartPosition, fragmentEndPosition, style), backgroundColor);

        m_paintingResourceMode = ApplyToDefaultMode;
    }

    ASSERT(!m_paintingResource);
}

void SVGInlineTextBox::paint(PaintInfo& paintInfo, const LayoutPoint& paintOffset, LayoutUnit, LayoutUnit)
{
    ASSERT(paintInfo.shouldPaintWithinRoot(renderer()));
    ASSERT(paintInfo.phase == PaintPhaseForeground || paintInfo.phase == PaintPhaseSelection);
    ASSERT(truncation() == cNoTruncation);

    if (renderer().style().visibility() != VISIBLE)
        return;

    // Note: We're explicitely not supporting composition & custom underlines and custom highlighters - unlike InlineTextBox.
    // If we ever need that for SVG, it's very easy to refactor and reuse the code.

    auto& parentRenderer = parent()->renderer();

    bool paintSelectedTextOnly = paintInfo.phase == PaintPhaseSelection;
    bool shouldPaintSelectionHighlight = !(paintInfo.paintBehavior & PaintBehaviorSkipSelectionHighlight);
    bool hasSelection = !parentRenderer.document().printing() && selectionState() != RenderObject::SelectionNone;
    if (!hasSelection && paintSelectedTextOnly)
        return;

    if (!textShouldBePainted(renderer()))
        return;

    auto& style = parentRenderer.style();

    const SVGRenderStyle& svgStyle = style.svgStyle();

    bool hasFill = svgStyle.hasFill();
    bool hasVisibleStroke = svgStyle.hasVisibleStroke();

    const RenderStyle* selectionStyle = &style;
    if (hasSelection && shouldPaintSelectionHighlight) {
        selectionStyle = parentRenderer.getCachedPseudoStyle(SELECTION);
        if (selectionStyle) {
            const SVGRenderStyle& svgSelectionStyle = selectionStyle->svgStyle();

            if (!hasFill)
                hasFill = svgSelectionStyle.hasFill();
            if (!hasVisibleStroke)
                hasVisibleStroke = svgSelectionStyle.hasVisibleStroke();
        } else
            selectionStyle = &style;
    }

    if (renderer().view().frameView().paintBehavior() & PaintBehaviorRenderingSVGMask) {
        hasFill = true;
        hasVisibleStroke = false;
    }

    AffineTransform fragmentTransform;
    unsigned textFragmentsSize = m_textFragments.size();
    for (unsigned i = 0; i < textFragmentsSize; ++i) {
        SVGTextFragment& fragment = m_textFragments.at(i);
        ASSERT(!m_paintingResource);

        GraphicsContextStateSaver stateSaver(paintInfo.context());
        fragment.buildFragmentTransform(fragmentTransform);
        if (!fragmentTransform.isIdentity())
            paintInfo.context().concatCTM(fragmentTransform);

        // Spec: All text decorations except line-through should be drawn before the text is filled and stroked; thus, the text is rendered on top of these decorations.
        int decorations = style.textDecorationsInEffect();
        if (decorations & TextDecorationUnderline)
            paintDecoration(paintInfo.context(), TextDecorationUnderline, fragment);
        if (decorations & TextDecorationOverline)
            paintDecoration(paintInfo.context(), TextDecorationOverline, fragment);

        auto paintOrder = style.svgStyle().paintTypesForPaintOrder();
        for (unsigned i = 0; i < paintOrder.size(); ++i) {
            switch (paintOrder.at(i)) {
            case PaintTypeFill:
                if (!hasFill)
                    continue;
                m_paintingResourceMode = ApplyToFillMode | ApplyToTextMode;
                ASSERT(selectionStyle);
                paintText(paintInfo.context(), style, *selectionStyle, fragment, hasSelection, paintSelectedTextOnly);
                break;
            case PaintTypeStroke:
                if (!hasVisibleStroke)
                    continue;
                m_paintingResourceMode = ApplyToStrokeMode | ApplyToTextMode;
                ASSERT(selectionStyle);
                paintText(paintInfo.context(), style, *selectionStyle, fragment, hasSelection, paintSelectedTextOnly);
                break;
            case PaintTypeMarkers:
                continue;
            }
        }

        // Spec: Line-through should be drawn after the text is filled and stroked; thus, the line-through is rendered on top of the text.
        if (decorations & TextDecorationLineThrough)
            paintDecoration(paintInfo.context(), TextDecorationLineThrough, fragment);

        m_paintingResourceMode = ApplyToDefaultMode;
    }

    // Finally, paint the outline if any.
    if (renderer().style().hasOutline() && is<RenderInline>(parentRenderer))
        downcast<RenderInline>(parentRenderer).paintOutline(paintInfo, paintOffset);

    ASSERT(!m_paintingResource);
}

bool SVGInlineTextBox::acquirePaintingResource(GraphicsContext*& context, float scalingFactor, RenderBoxModelObject& renderer, const RenderStyle& style)
{
    ASSERT(scalingFactor);
    ASSERT(m_paintingResourceMode != ApplyToDefaultMode);

    Color fallbackColor;
    if (m_paintingResourceMode & ApplyToFillMode)
        m_paintingResource = RenderSVGResource::fillPaintingResource(renderer, style, fallbackColor);
    else if (m_paintingResourceMode & ApplyToStrokeMode)
        m_paintingResource = RenderSVGResource::strokePaintingResource(renderer, style, fallbackColor);
    else {
        // We're either called for stroking or filling.
        ASSERT_NOT_REACHED();
    }

    if (!m_paintingResource)
        return false;

    if (!m_paintingResource->applyResource(renderer, style, context, m_paintingResourceMode)) {
        if (fallbackColor.isValid()) {
            RenderSVGResourceSolidColor* fallbackResource = RenderSVGResource::sharedSolidPaintingResource();
            fallbackResource->setColor(fallbackColor);

            m_paintingResource = fallbackResource;
            m_paintingResource->applyResource(renderer, style, context, m_paintingResourceMode);
        }
    }

    if (scalingFactor != 1 && m_paintingResourceMode & ApplyToStrokeMode)
        context->setStrokeThickness(context->strokeThickness() * scalingFactor);

    return true;
}

void SVGInlineTextBox::releasePaintingResource(GraphicsContext*& context, const Path* path)
{
    ASSERT(m_paintingResource);

    m_paintingResource->postApplyResource(parent()->renderer(), context, m_paintingResourceMode, path, /*RenderSVGShape*/ nullptr);
    m_paintingResource = nullptr;
}

bool SVGInlineTextBox::prepareGraphicsContextForTextPainting(GraphicsContext*& context, float scalingFactor, const RenderStyle& style)
{
    return acquirePaintingResource(context, scalingFactor, parent()->renderer(), style);
}

void SVGInlineTextBox::restoreGraphicsContextAfterTextPainting(GraphicsContext*& context)
{
    releasePaintingResource(context, /* path */nullptr);
}

TextRun SVGInlineTextBox::constructTextRun(const RenderStyle& style, const SVGTextFragment& fragment) const
{
    TextRun run(StringView(renderer().text()).substring(fragment.characterOffset, fragment.length)
                , 0 /* xPos, only relevant with allowTabs=true */
                , 0 /* padding, only relevant for justified text, not relevant for SVG */
                , AllowTrailingExpansion
                , direction()
                , dirOverride() || style.rtlOrdering() == VisualOrder /* directionalOverride */);

    // We handle letter & word spacing ourselves.
    run.disableSpacing();

    // Propagate the maximum length of the characters buffer to the TextRun, even when we're only processing a substring.
    run.setCharactersLength(renderer().textLength() - fragment.characterOffset);
    ASSERT(run.charactersLength() >= run.length());
    return run;
}

bool SVGInlineTextBox::mapStartEndPositionsIntoFragmentCoordinates(const SVGTextFragment& fragment, unsigned& startPosition, unsigned& endPosition) const
{
    if (startPosition >= endPosition)
        return false;

    ASSERT(fragment.characterOffset >= start());
    unsigned offset = fragment.characterOffset - start();
    unsigned length = fragment.length;

    if (startPosition >= offset + length || endPosition <= offset)
        return false;

    if (startPosition < offset)
        startPosition = 0;
    else {
        ASSERT(startPosition >= offset);
        startPosition -= offset;
    }

    if (endPosition > offset + length)
        endPosition = length;
    else {
        ASSERT(endPosition >= offset);
        endPosition -= offset;
    }

    ASSERT_WITH_SECURITY_IMPLICATION(startPosition < endPosition);
    return true;
}

static inline float positionOffsetForDecoration(TextDecoration decoration, const FontMetrics& fontMetrics, float thickness)
{
    // FIXME: For SVG Fonts we need to use the attributes defined in the <font-face> if specified.
    // Compatible with Batik/Opera.
    if (decoration == TextDecorationUnderline)
        return fontMetrics.floatAscent() + thickness * 1.5f;
    if (decoration == TextDecorationOverline)
        return thickness;
    if (decoration == TextDecorationLineThrough)
        return fontMetrics.floatAscent() * 5 / 8.0f;

    ASSERT_NOT_REACHED();
    return 0.0f;
}

static inline float thicknessForDecoration(TextDecoration, const FontCascade& font)
{
    // FIXME: For SVG Fonts we need to use the attributes defined in the <font-face> if specified.
    // Compatible with Batik/Opera
    return font.size() / 20.0f;
}

static inline RenderBoxModelObject& findRendererDefininingTextDecoration(InlineFlowBox* parentBox)
{
    // Lookup first render object in parent hierarchy which has text-decoration set.
    RenderBoxModelObject* renderer = nullptr;
    while (parentBox) {
        renderer = &parentBox->renderer();

        if (renderer->style().textDecoration() != TextDecorationNone)
            break;

        parentBox = parentBox->parent();
    }

    ASSERT(renderer);
    return *renderer;
}

void SVGInlineTextBox::paintDecoration(GraphicsContext& context, TextDecoration decoration, const SVGTextFragment& fragment)
{
    if (renderer().style().textDecorationsInEffect() == TextDecorationNone)
        return;

    // Find out which render style defined the text-decoration, as its fill/stroke properties have to be used for drawing instead of ours.
    auto& decorationRenderer = findRendererDefininingTextDecoration(parent());
    const RenderStyle& decorationStyle = decorationRenderer.style();

    if (decorationStyle.visibility() == HIDDEN)
        return;

    const SVGRenderStyle& svgDecorationStyle = decorationStyle.svgStyle();

    bool hasDecorationFill = svgDecorationStyle.hasFill();
    bool hasVisibleDecorationStroke = svgDecorationStyle.hasVisibleStroke();

    if (hasDecorationFill) {
        m_paintingResourceMode = ApplyToFillMode;
        paintDecorationWithStyle(context, decoration, fragment, decorationRenderer);
    }

    if (hasVisibleDecorationStroke) {
        m_paintingResourceMode = ApplyToStrokeMode;
        paintDecorationWithStyle(context, decoration, fragment, decorationRenderer);
    }
}

void SVGInlineTextBox::paintDecorationWithStyle(GraphicsContext& context, TextDecoration decoration, const SVGTextFragment& fragment, RenderBoxModelObject& decorationRenderer)
{
    ASSERT(!m_paintingResource);
    ASSERT(m_paintingResourceMode != ApplyToDefaultMode);

    auto& decorationStyle = decorationRenderer.style();

    float scalingFactor = 1;
    FontCascade scaledFont;
    RenderSVGInlineText::computeNewScaledFontForStyle(decorationRenderer, decorationStyle, scalingFactor, scaledFont);
    ASSERT(scalingFactor);

    // The initial y value refers to overline position.
    float thickness = thicknessForDecoration(decoration, scaledFont);

    if (fragment.width <= 0 && thickness <= 0)
        return;

    FloatPoint decorationOrigin(fragment.x, fragment.y);
    float width = fragment.width;
    const FontMetrics& scaledFontMetrics = scaledFont.fontMetrics();

    GraphicsContextStateSaver stateSaver(context);
    if (scalingFactor != 1) {
        width *= scalingFactor;
        decorationOrigin.scale(scalingFactor, scalingFactor);
        context.scale(FloatSize(1 / scalingFactor, 1 / scalingFactor));
    }

    decorationOrigin.move(0, -scaledFontMetrics.floatAscent() + positionOffsetForDecoration(decoration, scaledFontMetrics, thickness));

    Path path;
    path.addRect(FloatRect(decorationOrigin, FloatSize(width, thickness)));

    GraphicsContext* contextPtr = &context;
    if (acquirePaintingResource(contextPtr, scalingFactor, decorationRenderer, decorationStyle))
        releasePaintingResource(contextPtr, &path);
}

void SVGInlineTextBox::paintTextWithShadows(GraphicsContext& context, const RenderStyle& style, TextRun& textRun, const SVGTextFragment& fragment, unsigned startPosition, unsigned endPosition)
{
    float scalingFactor = renderer().scalingFactor();
    ASSERT(scalingFactor);

    const FontCascade& scaledFont = renderer().scaledFont();
    const ShadowData* shadow = style.textShadow();

    FloatPoint textOrigin(fragment.x, fragment.y);
    FloatSize textSize(fragment.width, fragment.height);

    if (scalingFactor != 1) {
        textOrigin.scale(scalingFactor, scalingFactor);
        textSize.scale(scalingFactor);
    }

    FloatRect shadowRect(FloatPoint(textOrigin.x(), textOrigin.y() - scaledFont.fontMetrics().floatAscent()), textSize);

    GraphicsContext* usedContext = &context;
    do {
        if (!prepareGraphicsContextForTextPainting(usedContext, scalingFactor, style))
            break;

        {
            ShadowApplier shadowApplier(*usedContext, shadow, shadowRect);

            if (!shadowApplier.didSaveContext())
                usedContext->save();
            usedContext->scale(FloatSize(1 / scalingFactor, 1 / scalingFactor));

            scaledFont.drawText(*usedContext, textRun, textOrigin + shadowApplier.extraOffset(), startPosition, endPosition);

            if (!shadowApplier.didSaveContext())
                usedContext->restore();
        }

        restoreGraphicsContextAfterTextPainting(usedContext);

        if (!shadow)
            break;

        shadow = shadow->next();
    } while (shadow);
}

void SVGInlineTextBox::paintText(GraphicsContext& context, const RenderStyle& style, const RenderStyle& selectionStyle, const SVGTextFragment& fragment, bool hasSelection, bool paintSelectedTextOnly)
{
    unsigned startPosition = 0;
    unsigned endPosition = 0;
    if (hasSelection) {
        std::tie(startPosition, endPosition) = selectionStartEnd();
        hasSelection = mapStartEndPositionsIntoFragmentCoordinates(fragment, startPosition, endPosition);
    }

    // Fast path if there is no selection, just draw the whole chunk part using the regular style
    TextRun textRun = constructTextRun(style, fragment);
    if (!hasSelection || startPosition >= endPosition) {
        paintTextWithShadows(context, style, textRun, fragment, 0, fragment.length);
        return;
    }

    // Eventually draw text using regular style until the start position of the selection
    if (startPosition > 0 && !paintSelectedTextOnly)
        paintTextWithShadows(context, style, textRun, fragment, 0, startPosition);

    // Draw text using selection style from the start to the end position of the selection
    if (style != selectionStyle)
        SVGResourcesCache::clientStyleChanged(parent()->renderer(), StyleDifferenceRepaint, selectionStyle);

    paintTextWithShadows(context, selectionStyle, textRun, fragment, startPosition, endPosition);

    if (style != selectionStyle)
        SVGResourcesCache::clientStyleChanged(parent()->renderer(), StyleDifferenceRepaint, style);

    // Eventually draw text using regular style from the end position of the selection to the end of the current chunk part
    if (endPosition < fragment.length && !paintSelectedTextOnly)
        paintTextWithShadows(context, style, textRun, fragment, endPosition, fragment.length);
}

FloatRect SVGInlineTextBox::calculateBoundaries() const
{
    FloatRect textRect;

    float scalingFactor = renderer().scalingFactor();
    ASSERT(scalingFactor);

    float baseline = renderer().scaledFont().fontMetrics().floatAscent() / scalingFactor;

    AffineTransform fragmentTransform;
    unsigned textFragmentsSize = m_textFragments.size();
    for (unsigned i = 0; i < textFragmentsSize; ++i) {
        const SVGTextFragment& fragment = m_textFragments.at(i);
        FloatRect fragmentRect(fragment.x, fragment.y - baseline, fragment.width, fragment.height);
        fragment.buildFragmentTransform(fragmentTransform);
        if (!fragmentTransform.isIdentity())
            fragmentRect = fragmentTransform.mapRect(fragmentRect);

        textRect.unite(fragmentRect);
    }

    return textRect;
}

bool SVGInlineTextBox::nodeAtPoint(const HitTestRequest& request, HitTestResult& result, const HitTestLocation& locationInContainer, const LayoutPoint& accumulatedOffset, LayoutUnit, LayoutUnit, HitTestAction)
{
    // FIXME: integrate with InlineTextBox::nodeAtPoint better.
    ASSERT(!isLineBreak());

    PointerEventsHitRules hitRules(PointerEventsHitRules::SVG_TEXT_HITTESTING, request, renderer().style().pointerEvents());
    bool isVisible = renderer().style().visibility() == VISIBLE;
    if (isVisible || !hitRules.requireVisible) {
        if ((hitRules.canHitStroke && (renderer().style().svgStyle().hasStroke() || !hitRules.requireStroke))
            || (hitRules.canHitFill && (renderer().style().svgStyle().hasFill() || !hitRules.requireFill))) {
            FloatPoint boxOrigin(x(), y());
            boxOrigin.moveBy(accumulatedOffset);
            FloatRect rect(boxOrigin, size());
            if (locationInContainer.intersects(rect)) {

                float scalingFactor = renderer().scalingFactor();
                ASSERT(scalingFactor);
                
                float baseline = renderer().scaledFont().fontMetrics().floatAscent() / scalingFactor;

                AffineTransform fragmentTransform;
                for (auto& fragment : m_textFragments) {
                    FloatQuad fragmentQuad(FloatRect(fragment.x, fragment.y - baseline, fragment.width, fragment.height));
                    fragment.buildFragmentTransform(fragmentTransform);
                    if (!fragmentTransform.isIdentity())
                        fragmentQuad = fragmentTransform.mapQuad(fragmentQuad);
                    
                    if (fragmentQuad.containsPoint(locationInContainer.point())) {
                        renderer().updateHitTestResult(result, locationInContainer.point() - toLayoutSize(accumulatedOffset));
                        if (!result.addNodeToRectBasedTestResult(&renderer().textNode(), request, locationInContainer, rect))
                            return true;
                    }
                }
             }
        }
    }
    return false;
}

} // namespace WebCore
