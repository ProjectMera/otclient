/*
 * Copyright (c) 2010-2020 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "thingpainter.h"
#include "../lightview.h"
#include "../thingtype.h"
#include "../game.h"
#include "../missile.h"
#include "../effect.h"
#include "../item.h"
#include "../animatedtext.h"
#include "../statictext.h"

#include <framework/graphics/graphics.h>

void ThingPainter::drawText(const StaticTextPtr& text, const Point& dest, const Rect& parentRect)
{
    const Size textSize = text->m_cachedText.getTextSize();
    const Rect rect = Rect(dest - Point(textSize.width() / 2, textSize.height()) + Point(20, 5), textSize);
    Rect boundRect = rect;
    boundRect.bind(parentRect);

    // draw only if the real center is not too far from the parent center, or its a yell
    //if(g_map.isAwareOfPosition(m_position) || isYell()) {
    g_painter->setColor(text->m_color);
    text->m_cachedText.draw(boundRect);
    //}
}

void ThingPainter::drawText(const AnimatedTextPtr& text, const Point& dest, const Rect& visibleRect)
{
    const static float tf = Otc::ANIMATED_TEXT_DURATION;
    const static float tftf = Otc::ANIMATED_TEXT_DURATION * Otc::ANIMATED_TEXT_DURATION;

    Point p = dest;
    const Size textSize = text->m_cachedText.getTextSize();
    const float t = text->m_animationTimer.ticksElapsed();
    p.x += (24 - textSize.width() / 2);

    if(g_game.getFeature(Otc::GameDiagonalAnimatedText)) {
        p.x -= (4 * t / tf) + (8 * t * t / tftf);
    }

    p.y += 8 + (-48 * t) / tf;
    p += text->m_offset;
    const Rect rect(p, textSize);

    if(visibleRect.contains(rect)) {
        //TODO: cache into a framebuffer
        const float t0 = tf / 1.2;
        if(t > t0) {
            Color color = text->m_color;
            color.setAlpha(static_cast<float>(1 - (t - t0) / (tf - t0)));
            g_painter->setColor(color);
        } else
            g_painter->setColor(text->m_color);
        text->m_cachedText.draw(rect);
    }
}

void ThingPainter::draw(const ItemPtr& item, const Point& dest, float scaleFactor, bool animate, const Highlight& highLight, int frameFlag, LightView* lightView)
{
    if(item->m_clientId == 0 || !item->canDraw())
        return;

    // determine animation phase
    const int animationPhase = item->calculateAnimationPhase(animate);

    // determine x,y,z patterns
    int xPattern = 0, yPattern = 0, zPattern = 0;
    item->calculatePatterns(xPattern, yPattern, zPattern);

    if(item->m_color != Color::alpha)
        g_painter->setColor(item->m_color);

    draw(item->rawGetThingType(), dest, scaleFactor, 0, xPattern, yPattern, zPattern, animationPhase, false, frameFlag, lightView);

    /// Sanity check
    /// This is just to ensure that we don't overwrite some color and
    /// screw up the whole rendering.
    if(item->m_color != Color::alpha)
        g_painter->resetColor();

    if(highLight.enabled && item == highLight.thing) {
        g_painter->setColor(highLight.rgbColor);
        draw(item->rawGetThingType(), dest, scaleFactor, 0, xPattern, yPattern, zPattern, animationPhase, true, frameFlag, nullptr);
        g_painter->resetColor();
    }
}

void ThingPainter::draw(const MissilePtr& missile, const Point& dest, float scaleFactor, int frameFlag, LightView* lightView)
{
    if(missile->m_id == 0)
        return;

    int xPattern = 0, yPattern = 0;
    if(missile->m_direction == Otc::NorthWest) {
        xPattern = 0;
        yPattern = 0;
    } else if(missile->m_direction == Otc::North) {
        xPattern = 1;
        yPattern = 0;
    } else if(missile->m_direction == Otc::NorthEast) {
        xPattern = 2;
        yPattern = 0;
    } else if(missile->m_direction == Otc::East) {
        xPattern = 2;
        yPattern = 1;
    } else if(missile->m_direction == Otc::SouthEast) {
        xPattern = 2;
        yPattern = 2;
    } else if(missile->m_direction == Otc::South) {
        xPattern = 1;
        yPattern = 2;
    } else if(missile->m_direction == Otc::SouthWest) {
        xPattern = 0;
        yPattern = 2;
    } else if(missile->m_direction == Otc::West) {
        xPattern = 0;
        yPattern = 1;
    } else {
        xPattern = 1;
        yPattern = 1;
    }

    const float fraction = missile->m_animationTimer.ticksElapsed() / missile->m_duration;
    draw(missile->rawGetThingType(), dest + missile->m_delta * fraction * scaleFactor, scaleFactor, 0, xPattern, yPattern, 0, 0, false, frameFlag, lightView);
}

void ThingPainter::draw(const EffectPtr& effect, const Point& dest, float scaleFactor, int frameFlag, LightView* lightView)
{
    if(effect->m_id == 0) return;

    // It only starts to draw when the first effect as it is about to end.
    if(effect->m_animationTimer.ticksElapsed() < effect->m_timeToStartDrawing) return;

    // This requires a separate getPhaseAt method as using getPhase would make all magic effects use the same phase regardless of their appearance time
    int animationPhase = effect->rawGetThingType()->getAnimator()->getPhaseAt(effect->m_animationTimer.ticksElapsed());

    const int xPattern = effect->m_position.x % effect->getNumPatternX();
    const int yPattern = effect->m_position.y % effect->getNumPatternY();

    draw(effect->rawGetThingType(), dest, scaleFactor, 0, xPattern, yPattern, 0, animationPhase, false, frameFlag, lightView);
}

void ThingPainter::draw(const ThingTypePtr& thingType, const Point& dest, float scaleFactor, int layer, int xPattern, int yPattern, int zPattern, int animationPhase, bool useBlankTexture, int frameFlags, LightView* lightView)
{
    if(thingType->m_null)
        return;

    if(animationPhase >= thingType->m_animationPhases)
        return;

    const TexturePtr& texture = thingType->getTexture(animationPhase, useBlankTexture); // texture might not exists, neither its rects.
    if(!texture)
        return;

    const uint frameIndex = thingType->getTextureIndex(layer, xPattern, yPattern, zPattern);
    if(frameIndex >= thingType->m_texturesFramesRects[animationPhase].size())
        return;

    Point textureOffset;
    Rect textureRect;

    if(scaleFactor != 1.0f) {
        textureRect = thingType->m_texturesFramesOriginRects[animationPhase][frameIndex];
    } else {
        textureOffset = thingType->m_texturesFramesOffsets[animationPhase][frameIndex];
        textureRect = thingType->m_texturesFramesRects[animationPhase][frameIndex];
    }

    const Rect screenRect(dest + (textureOffset - thingType->m_displacement - (thingType->m_size.toPoint() - Point(1)) * Otc::TILE_PIXELS) * scaleFactor,
                          textureRect.size() * scaleFactor);

    if(frameFlags & Otc::FUpdateThing) {
        const bool useOpacity = thingType->m_opacity < 1.0f;

        if(useOpacity)
            g_painter->setColor(Color(1.0f, 1.0f, 1.0f, thingType->m_opacity));

        g_painter->drawTexturedRect(screenRect, texture, textureRect);

        if(useOpacity)
            g_painter->resetColor();
    }

    if(lightView && thingType->hasLight() && frameFlags & Otc::FUpdateLight) {
        const Light light = thingType->getLight();
        if(light.intensity > 0) {
            lightView->addLightSource(screenRect.center(), light);
        }
    }
}