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

#include "creaturepainter.h"
#include "thingpainter.h"
#include "thingpainter.h"

#include "../map.h"
#include "../game.h"

#include <framework/core/declarations.h>
#include <framework/graphics/graphics.h>

void TilePainter::drawStart(const TilePtr& tile, const MapViewPtr& mapView)
{
    if(tile->m_completelyCovered) return;

    tile->m_drawElevation = 0;

    if(tile->m_highlight.update) {
        tile->m_highlight.fadeLevel += 10 * (tile->m_highlight.invertedColorSelection ? 1 : -1);
        tile->m_highlight.update = false;
        tile->m_highlight.rgbColor = Color(255, 255, 0, tile->m_highlight.fadeLevel);

        if(tile->m_highlight.invertedColorSelection ? tile->m_highlight.fadeLevel > 120 : tile->m_highlight.fadeLevel < 0) {
            tile->m_highlight.invertedColorSelection = !tile->m_highlight.invertedColorSelection;
        }
    }

    if(tile->hasBorderShadowColor()) {
        tile->m_shadowColor = mapView->getLastFloorShadowingColor();
        g_painter->setColor(tile->m_borderShadowColor);
    }
}

void TilePainter::drawEnd(const TilePtr& tile, const MapViewPtr& /*mapView*/)
{
    if(tile->m_completelyCovered) return;

    // Reset Border Shadow Color
    if(tile->hasBorderShadowColor()) {
        g_painter->setColor(tile->m_shadowColor);
    }
}

void TilePainter::drawThing(const TilePtr& tile, const ThingPtr& thing, const Point& dest, float scaleFactor, bool animate, int frameFlag, LightView* lightView)
{
    if(tile->m_completelyCovered) {
        frameFlag = 0;

        if(lightView && tile->hasLight())
            frameFlag = Otc::FUpdateLight;
    }

    const auto putShadowColor = g_painter->getColor() == tile->m_borderShadowColor && (!thing->isGroundBorder() && !thing->isTall());

    if(putShadowColor) {
        g_painter->setColor(tile->m_shadowColor);
    }

    if(thing->isEffect()) {
        ThingPainter::draw(thing->static_self_cast<Effect>(), dest, scaleFactor, frameFlag, lightView);
    } else {
        if(thing->isCreature()) {
            CreaturePainter::draw(thing->static_self_cast<Creature>(), dest, scaleFactor, animate, tile->m_highlight, frameFlag, lightView);
        } else if(thing->isItem()) {
            ThingPainter::draw(thing->static_self_cast<Item>(), dest, scaleFactor, animate, tile->m_highlight, frameFlag, lightView);
        }

        tile->m_drawElevation += thing->getElevation();
        if(tile->m_drawElevation > Otc::MAX_ELEVATION)
            tile->m_drawElevation = Otc::MAX_ELEVATION;
    }

    // Reset Border Shadow Color
    if(putShadowColor) {
        g_painter->setColor(tile->m_borderShadowColor);
    }
}

void TilePainter::drawGround(const TilePtr& tile, const Point& dest, float scaleFactor, int frameFlags, LightView* lightView)
{
    if(!tile->hasGroundToDraw()) return;

    for(const auto& ground : tile->m_things) {
        if(!ground->isGroundOrBorder()) break;
        drawThing(tile, ground, dest - tile->m_drawElevation * scaleFactor, scaleFactor, true, frameFlags, lightView);
    }
}

void TilePainter::drawCreature(const TilePtr& tile, const Point& dest, float scaleFactor, int frameFlags, LightView* lightView)
{
#if RENDER_CREATURE_BEHIND == 1
    for(const auto& creature : m_walkingCreatures) {
        drawThing(creature, Point(
            dest.x + ((creature->getPosition().x - m_position.x) * Otc::TILE_PIXELS - m_drawElevation) * scaleFactor,
            dest.y + ((creature->getPosition().y - m_position.y) * Otc::TILE_PIXELS - m_drawElevation) * scaleFactor
        ), scaleFactor, true, frameFlags, lightView);
    }

    if(hasCreature()) {
        for(auto it = m_creatures.rbegin(); it != m_creatures.rend(); ++it) {
            const auto& creature = *it;
            if(creature->isWalking()) continue;
            drawThing(creature, dest - m_drawElevation * scaleFactor, scaleFactor, true, frameFlags, lightView);
        }
    }
#else
    if(tile->hasCreature()) {
        for(const auto& thing : tile->m_things) {
            if(!thing->isCreature() || thing->static_self_cast<Creature>()->isWalking()) continue;

            drawThing(tile, thing, dest - tile->m_drawElevation * scaleFactor, scaleFactor, true, frameFlags, lightView);
        }
    }

    for(const auto& creature : tile->m_walkingCreatures) {
        drawThing(tile, creature, Point(
            dest.x + ((creature->getPosition().x - tile->m_position.x) * Otc::TILE_PIXELS - tile->m_drawElevation) * scaleFactor,
            dest.y + ((creature->getPosition().y - tile->m_position.y) * Otc::TILE_PIXELS - tile->m_drawElevation) * scaleFactor
        ), scaleFactor, true, frameFlags, lightView);
    }
#endif    
}


void TilePainter::drawBottom(const TilePtr& tile, const Point& dest, float scaleFactor, int frameFlags, LightView* lightView)
{
    if(tile->m_countFlag.hasBottomItem) {
        for(const auto& item : tile->m_things) {
            if(!item->isOnBottom()) continue;
            drawThing(tile, item, dest - tile->m_drawElevation * scaleFactor, scaleFactor, true, frameFlags, lightView);
        }
    }

    uint8 redrawPreviousTopW = 0,
        redrawPreviousTopH = 0;

    if(tile->m_countFlag.hasCommonItem) {
        for(auto it = tile->m_things.rbegin(); it != tile->m_things.rend(); ++it) {
            const auto& item = *it;
            if(!item->isCommon()) continue;

            drawThing(tile, item, dest - tile->m_drawElevation * scaleFactor, scaleFactor, true, frameFlags, lightView);

            if(item->isLyingCorpse()) {
                redrawPreviousTopW = std::max<int>(item->getWidth(), redrawPreviousTopW);
                redrawPreviousTopH = std::max<int>(item->getHeight(), redrawPreviousTopH);
            }
        }
    }

    // after we render 2x2 lying corpses, we must redraw previous creatures/ontop above them	
    if(redrawPreviousTopH > 0 || redrawPreviousTopW > 0) {
        // after we render 2x2 lying corpses, we must redraw previous creatures/ontop above them	
        if(redrawPreviousTopH > 0 || redrawPreviousTopW > 0) {
            for(int x = -redrawPreviousTopW; x <= 0; ++x) {
                for(int y = -redrawPreviousTopH; y <= 0; ++y) {
                    if(x == 0 && y == 0)
                        continue;
                    const TilePtr& otherTile = g_map.getTile(tile->m_position.translated(x, y));
                    if(otherTile) {
                        const auto& newDest = dest + (Point(x, y) * Otc::TILE_PIXELS) * scaleFactor;
                        drawCreature(otherTile, newDest, scaleFactor, frameFlags);
                        drawTop(otherTile, newDest, scaleFactor, frameFlags);
                    }
                }
            }
        }
    }

    drawCreature(tile, dest, scaleFactor, frameFlags, lightView);
}

void TilePainter::drawTop(const TilePtr& tile, const Point& dest, float scaleFactor, int frameFlags, LightView* lightView)
{
    for(const auto& effect : tile->m_effects) {
        drawThing(tile, effect, dest - tile->m_drawElevation * scaleFactor, scaleFactor, true, frameFlags, lightView);
    }

    if(tile->m_countFlag.hasTopItem) {
        for(const auto& item : tile->m_things) {
            if(!item->isOnTop()) continue;
            drawThing(tile, item, dest, scaleFactor, true, frameFlags, lightView);
        }
    }
}

void TilePainter::draw(const TilePtr& tile, const Point& dest, float scaleFactor, int frameFlags, LightView* lightView)
{
    drawGround(tile, dest, scaleFactor, frameFlags, lightView);
    drawBottom(tile, dest, scaleFactor, frameFlags, lightView);
    drawTop(tile, dest, scaleFactor, frameFlags, lightView);
}