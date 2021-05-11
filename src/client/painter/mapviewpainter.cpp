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

#include "mapviewpainter.h"
#include "tilepainter.h"
#include "thingpainter.h"
#include "../map.h"
#include "../game.h"
#include "../missile.h"
#include "../shadermanager.h"

#include <framework/core/declarations.h>
#include <framework/graphics/framebuffermanager.h>
#include <framework/graphics/graphics.h>

void MapViewPainter::draw(const MapViewPtr& mapView, const Rect& rect)
{
    // update visible tiles cache when needed
    if(mapView->m_mustUpdateVisibleTilesCache)
        mapView->updateVisibleTilesCache();

    const Position cameraPosition = mapView->getCameraPosition();
    const auto redrawThing = mapView->m_frameCache.tile->canUpdate();
    const auto redrawLight = mapView->m_drawLights && mapView->m_lightView->canUpdate();

    if(mapView->m_rectCache.rect != rect) {
        mapView->m_rectCache.rect = rect;
        mapView->m_rectCache.srcRect = mapView->calcFramebufferSource(rect.size());
        mapView->m_rectCache.drawOffset = mapView->m_rectCache.srcRect.topLeft();
        mapView->m_rectCache.horizontalStretchFactor = rect.width() / static_cast<float>(mapView->m_rectCache.srcRect.width());
        mapView->m_rectCache.verticalStretchFactor = rect.height() / static_cast<float>(mapView->m_rectCache.srcRect.height());
    }

    if(redrawThing || redrawLight) {
        if(redrawLight) mapView->m_frameCache.flags |= Otc::FUpdateLight;

        if(redrawThing) {
            mapView->m_frameCache.tile->bind();
            mapView->m_frameCache.flags |= Otc::FUpdateThing;
        }

        const auto& lightView = redrawLight ? mapView->m_lightView.get() : nullptr;
        for(int_fast8_t z = mapView->m_floorMax; z >= mapView->m_floorMin; --z) {
            if(lightView) {
                const int8 nextFloor = z - 1;
                if(nextFloor >= mapView->m_floorMin) {
                    lightView->setFloor(nextFloor);
                    for(const auto& tile : mapView->m_cachedVisibleTiles[nextFloor]) {
                        const auto& ground = tile->getGround();
                        if(ground && !ground->isTranslucent()) {
                            auto pos2D = mapView->transformPositionTo2D(tile->getPosition(), cameraPosition);
                            if(ground->isTopGround()) {
                                const auto currentPos = tile->getPosition();
                                for(const auto& pos : currentPos.translatedToDirections({ Otc::South, Otc::East })) {
                                    const auto& nextDownTile = g_map.getTile(pos);
                                    if(nextDownTile && nextDownTile->hasGround() && !nextDownTile->isTopGround()) {
                                        lightView->setShade(pos2D);
                                        break;
                                    }
                                }

                                pos2D -= mapView->m_tileSize;
                        }

                            lightView->setShade(pos2D);
                    }
                }
            }
        }

            mapView->onFloorDrawingStart(z);

#if DRAW_ALL_GROUND_FIRST == 1
            drawSeparately(z, viewPort, lightView);
#else
            if(lightView) lightView->setFloor(z);
            for(const auto& tile : mapView->m_cachedVisibleTiles[z]) {
                const auto hasLight = redrawLight && tile->hasLight();

                if((!redrawThing && !hasLight) || !mapView->canRenderTile(tile, mapView->m_viewport, lightView)) continue;

                TilePainter::drawStart(tile, mapView);
                TilePainter::draw(tile, mapView->transformPositionTo2D(tile->getPosition(), cameraPosition), mapView->m_scaleFactor, mapView->m_frameCache.flags, lightView);
                TilePainter::drawEnd(tile, mapView);
            }
#endif
            for(const MissilePtr& missile : g_map.getFloorMissiles(z)) {
                ThingPainter::draw(missile, mapView->transformPositionTo2D(missile->getPosition(), cameraPosition), mapView->m_scaleFactor, mapView->m_frameCache.flags, lightView);
            }

            mapView->onFloorDrawingEnd(z);
    }

        if(redrawThing) {
            if(mapView->m_crosshairTexture && mapView->m_mousePosition.isValid()) {
                const Point& point = mapView->transformPositionTo2D(mapView->m_mousePosition, cameraPosition);
                const Rect crosshairRect = Rect(point, mapView->m_tileSize, mapView->m_tileSize);
                g_painter->drawTexturedRect(crosshairRect, mapView->m_crosshairTexture);
            }

            mapView->m_frameCache.tile->release();
        }
}

    // generating mipmaps each frame can be slow in older cards
    //m_framebuffer->getTexture()->buildHardwareMipmaps();

    float fadeOpacity = 1.0f;
    if(!mapView->m_shaderSwitchDone && mapView->m_fadeOutTime > 0) {
        fadeOpacity = 1.0f - (mapView->m_fadeTimer.timeElapsed() / mapView->m_fadeOutTime);
        if(fadeOpacity < 0.0f) {
            mapView->m_shader = mapView->m_nextShader;
            mapView->m_nextShader = nullptr;
            mapView->m_shaderSwitchDone = true;
            mapView->m_fadeTimer.restart();
        }
    }

    if(mapView->m_shaderSwitchDone && mapView->m_shader && mapView->m_fadeInTime > 0)
        fadeOpacity = std::min<float>(mapView->m_fadeTimer.timeElapsed() / mapView->m_fadeInTime, 1.0f);

    if(mapView->m_shader && g_painter->hasShaders() && g_graphics.shouldUseShaders() && mapView->m_viewMode == MapView::NEAR_VIEW) {
        const Point center = mapView->m_rectCache.srcRect.center();
        const Point globalCoord = Point(cameraPosition.x - mapView->m_drawDimension.width() / 2, -(cameraPosition.y - mapView->m_drawDimension.height() / 2)) * mapView->m_tileSize;
        mapView->m_shader->bind();
        mapView->m_shader->setUniformValue(ShaderManager::MAP_CENTER_COORD, center.x / static_cast<float>(mapView->m_rectDimension.width()), 1.0f - center.y / static_cast<float>(mapView->m_rectDimension.height()));
        mapView->m_shader->setUniformValue(ShaderManager::MAP_GLOBAL_COORD, globalCoord.x / static_cast<float>(mapView->m_rectDimension.height()), globalCoord.y / static_cast<float>(mapView->m_rectDimension.height()));
        mapView->m_shader->setUniformValue(ShaderManager::MAP_ZOOM, mapView->m_scaleFactor);
        g_painter->setShaderProgram(mapView->m_shader);
    }

    g_painter->setOpacity(fadeOpacity);
    glDisable(GL_BLEND);
    mapView->m_frameCache.tile->draw(rect, mapView->m_rectCache.srcRect);
    g_painter->resetShaderProgram();
    g_painter->resetOpacity();
    glEnable(GL_BLEND);

    // this could happen if the player position is not known yet
    if(!cameraPosition.isValid())
        return;

    // avoid drawing texts on map in far zoom outs
#if DRAW_CREATURE_INFORMATION_AFTER_LIGHT == 0
    drawCreatureInformation(mapView);
#endif

    // lights are drawn after names and before texts
    if(mapView->m_drawLights) {
        mapView->m_lightView->draw(rect, mapView->m_rectCache.srcRect);
    }

#if DRAW_CREATURE_INFORMATION_AFTER_LIGHT == 1
    drawCreatureInformation();
#endif

    drawText(mapView);

    mapView->m_frameCache.flags = 0;
}

void MapViewPainter::drawCreatureInformation(const MapViewPtr& mapView)
{
    if(!mapView->m_drawNames && !mapView->m_drawHealthBars && !mapView->m_drawManaBar) return;

    if(mapView->m_frameCache.creatureInformation->canUpdate()) {
        const Position cameraPosition = mapView->getCameraPosition();

        uint32_t flags = 0;
        if(mapView->m_drawNames) { flags = Otc::DrawNames; }
        if(mapView->m_drawHealthBars) { flags |= Otc::DrawBars; }
        if(mapView->m_drawManaBar) { flags |= Otc::DrawManaBar; }

        mapView->m_frameCache.creatureInformation->bind();
        for(const auto& creature : mapView->m_visibleCreatures) {
            CreaturePainter::drawInformation(creature, mapView->m_rectCache.rect, mapView->transformPositionTo2D(creature->getPosition(), cameraPosition), mapView->m_scaleFactor, mapView->m_rectCache.drawOffset, mapView->m_rectCache.horizontalStretchFactor, mapView->m_rectCache.verticalStretchFactor, flags);
        }

        mapView->m_frameCache.creatureInformation->release();
    }

    mapView->m_frameCache.creatureInformation->draw();
}

void MapViewPainter::drawText(const MapViewPtr& mapView)
{
    if(!mapView->m_drawTexts) return;

    const Position cameraPosition = mapView->getCameraPosition();

    if(!g_map.getStaticTexts().empty()) {
        if(mapView->m_frameCache.staticText->canUpdate()) {
            mapView->m_frameCache.staticText->bind();
            for(const StaticTextPtr& staticText : g_map.getStaticTexts()) {
                const Position pos = staticText->getPosition();

                if(pos.z != cameraPosition.z && staticText->getMessageMode() == Otc::MessageNone)
                    continue;

                Point p = mapView->transformPositionTo2D(pos, cameraPosition) - mapView->m_rectCache.drawOffset;
                p.x *= mapView->m_rectCache.horizontalStretchFactor;
                p.y *= mapView->m_rectCache.verticalStretchFactor;
                p += mapView->m_rectCache.rect.topLeft();
                ThingPainter::drawText(staticText, p, mapView->m_rectCache.rect);
            }
            mapView->m_frameCache.staticText->release();
        }

        mapView->m_frameCache.staticText->draw();
    }

    if(!g_map.getAnimatedTexts().empty()) {
        if(mapView->m_frameCache.dynamicText->canUpdate()) {
            mapView->m_frameCache.dynamicText->bind();

            for(const AnimatedTextPtr& animatedText : g_map.getAnimatedTexts()) {
                const Position pos = animatedText->getPosition();

                if(pos.z != cameraPosition.z)
                    continue;

                Point p = mapView->transformPositionTo2D(pos, cameraPosition) - mapView->m_rectCache.drawOffset;
                p.x *= mapView->m_rectCache.horizontalStretchFactor;
                p.y *= mapView->m_rectCache.verticalStretchFactor;
                p += mapView->m_rectCache.rect.topLeft();

                ThingPainter::drawText(animatedText, p, mapView->m_rectCache.rect);
            }
            mapView->m_frameCache.dynamicText->release();
        }
        mapView->m_frameCache.dynamicText->draw();
    }
}

#if DRAW_ALL_GROUND_FIRST == 1
void MapViewPainter::drawSeparately(const MapViewPtr& mapView, const uint8 floor, const ViewPort& viewPort, LightView* lightView)
{
    const Position cameraPosition = getCameraPosition();
    const auto& tiles = m_cachedVisibleTiles[floor];
    const auto redrawThing = m_frameCache.flags & Otc::FUpdateThing;
    const auto redrawLight = m_drawLights && m_frameCache.flags & Otc::FUpdateLight;

    for(const auto& tile : tiles) {
        if(!tile->hasGroundToDraw()) continue;

        const auto hasLight = redrawLight && tile->hasLight();

        if(!redrawThing && !hasLight || !canRenderTile(tile, viewPort, lightView)) continue;

        const Position& tilePos = tile->getPosition();
        tile->drawStart(this);
        tile->drawGround(transformPositionTo2D(tilePos, cameraPosition), m_scaleFactor, m_frameCache.flags, lightView);
        tile->drawEnd(this);
    }

    for(const auto& tile : tiles) {
        if(!tile->hasBottomToDraw() && !tile->hasTopToDraw()) continue;

        const auto hasLight = redrawLight && tile->hasLight();

        if(!redrawThing && !hasLight || !canRenderTile(tile, viewPort, lightView)) continue;

        const Position& tilePos = tile->getPosition();

        const Point pos2d = transformPositionTo2D(tilePos, cameraPosition);

        if(!tile->hasGroundToDraw()) tile->drawStart(this);

        tile->drawBottom(pos2d, m_scaleFactor, m_frameCache.flags, lightView);
        tile->drawTop(pos2d, m_scaleFactor, m_frameCache.flags, lightView);

        if(!tile->hasGroundToDraw()) tile->drawEnd(this);
    }
}
#endif