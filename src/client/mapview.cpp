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

#include "mapview.h"

#include "animatedtext.h"
#include "creature.h"
#include "game.h"
#include "lightview.h"
#include "map.h"
#include "missile.h"
#include "manager/shadermanager.h"
#include "statictext.h"
#include "tile.h"

#include <framework/core/application.h>
#include <framework/core/eventdispatcher.h>
#include <framework/core/resourcemanager.h>
#include <framework/graphics/framebuffermanager.h>
#include <framework/graphics/graphics.h>
#include <framework/graphics/image.h>

enum {
    // 3840x2160 => 1080p optimized
    // 2560x1440 => 720p optimized
    // 1728x972 => 480p optimized

    NEAR_VIEW_AREA = 32 * 32,
    MID_VIEW_AREA = 64 * 64,
    FAR_VIEW_AREA = 128 * 128
};

MapView::MapView()
{
    m_viewMode = NEAR_VIEW;
    m_frameCache.flags = Otc::FUpdateAll;
    m_lockedFirstVisibleFloor = UINT8_MAX;
    m_cachedFirstVisibleFloor = Otc::SEA_FLOOR;
    m_cachedLastVisibleFloor = Otc::SEA_FLOOR;
    m_minimumAmbientLight = 0;
    m_fadeOutTime = 0;
    m_fadeInTime = 0;
    m_floorMax = 0;
    m_floorMin = 0;

    m_optimizedSize = Size(g_map.getAwareRange().horizontal(), g_map.getAwareRange().vertical()) * Otc::TILE_PIXELS;

    m_frameCache.tile = g_framebuffers.createFrameBuffer();
    m_frameCache.creatureInformation = g_framebuffers.createFrameBuffer(true);
    m_frameCache.staticText = g_framebuffers.createFrameBuffer(true, 0);
    m_frameCache.dynamicText = g_framebuffers.createFrameBuffer(true, 50);

    m_shader = g_shaders.getDefaultMapShader();

    m_lastFloorShadowingColor = Color::white;

    m_renderScale = 100;

    setVisibleDimension(Size(15, 11));
}

MapView::~MapView()
{
#ifndef NDEBUG
    assert(!g_app.isTerminated());
#endif
}

void MapView::updateVisibleTilesCache()
{
    // there is no tile to render on invalid positions
    const Position cameraPosition = getCameraPosition();
    if(!cameraPosition.isValid())
        return;

    if(m_lastCameraPosition != cameraPosition) {
        if(m_mousePosition.isValid()) {
            if(cameraPosition.z == m_lastCameraPosition.z) {
                m_mousePosition = m_mousePosition.translatedToDirection(m_lastCameraPosition.getDirectionFromPosition(cameraPosition));
            } else {
                m_mousePosition.z += cameraPosition.z - m_lastCameraPosition.z;
            }

            onMouseMove(m_mousePosition, true);
        }

        onPositionChange(cameraPosition, m_lastCameraPosition);

        if(m_lastCameraPosition.z != cameraPosition.z) {
            onFloorChange(cameraPosition.z, m_lastCameraPosition.z);
        }
    }

    const uint8 cachedFirstVisibleFloor = calcFirstVisibleFloor();
    uint8 cachedLastVisibleFloor = calcLastVisibleFloor();

    assert(cachedFirstVisibleFloor >= 0 && cachedLastVisibleFloor >= 0 &&
           cachedFirstVisibleFloor <= Otc::MAX_Z && cachedLastVisibleFloor <= Otc::MAX_Z);

    if(cachedLastVisibleFloor < cachedFirstVisibleFloor)
        cachedLastVisibleFloor = cachedFirstVisibleFloor;

    m_lastCameraPosition = cameraPosition;
    m_cachedFirstVisibleFloor = cachedFirstVisibleFloor;
    m_cachedLastVisibleFloor = cachedLastVisibleFloor;

    // clear current visible tiles cache
    do {
        m_cachedVisibleTiles[m_floorMin].clear();
    } while(++m_floorMin <= m_floorMax);

    m_floorMin = m_floorMax = cameraPosition.z;
    if(m_mustUpdateVisibleCreaturesCache) {
        m_visibleCreatures.clear();
    }

    // cache visible tiles in draw order
    // draw from last floor (the lower) to first floor (the higher)
    const uint32 numDiagonals = m_drawDimension.width() + m_drawDimension.height() - 1;
    for(int_fast32_t iz = m_cachedLastVisibleFloor; iz >= m_cachedFirstVisibleFloor; --iz) {
        auto& floor = m_cachedVisibleTiles[iz];

        // loop through / diagonals beginning at top left and going to top right
        for(uint_fast32_t diagonal = 0; diagonal < numDiagonals; ++diagonal) {
            // loop current diagonal tiles
            const uint32 advance = std::max<uint32>(diagonal - m_drawDimension.height(), 0);
            for(int_fast32_t iy = diagonal - advance, ix = advance; iy >= 0 && ix < m_drawDimension.width(); --iy, ++ix) {
                // position on current floor
                //TODO: check position limits
                Position tilePos = cameraPosition.translated(ix - m_virtualCenterOffset.x, iy - m_virtualCenterOffset.y);
                // adjust tilePos to the wanted floor
                tilePos.coveredUp(cameraPosition.z - iz);
                if(const TilePtr& tile = g_map.getTile(tilePos)) {
                    // skip tiles that have nothing
                    if(!tile->isDrawable())
                        continue;

                    if(m_mustUpdateVisibleCreaturesCache) {
                        const auto& tileCreatures = tile->getCreatures();
                        if(isInRange(tilePos) && !tileCreatures.empty()) {
                            m_visibleCreatures.insert(m_visibleCreatures.end(), tileCreatures.rbegin(), tileCreatures.rend());
                        }
                    }

                    // skip tiles that are completely behind another tile
                    if(tile->isCompletelyCovered(m_cachedFirstVisibleFloor) && !tile->hasLight())
                        continue;

                    floor.push_back(tile);

                    tile->onAddVisibleTileList(this);

                    if(iz < m_floorMin)
                        m_floorMin = iz;
                    else if(iz > m_floorMax)
                        m_floorMax = iz;
                }
            }
        }
    }

    m_mustUpdateVisibleCreaturesCache = false;
    m_mustUpdateVisibleTilesCache = false;
}

void MapView::updateGeometry(const Size& visibleDimension, const Size& optimizedSize)
{
    const uint8 tileSize = Otc::TILE_PIXELS * (static_cast<float>(m_renderScale) / 100);
    const Size drawDimension = visibleDimension + Size(3),
        bufferSize = drawDimension * tileSize;

    if(bufferSize.width() > g_graphics.getMaxTextureSize() || bufferSize.height() > g_graphics.getMaxTextureSize()) {
        g_logger.traceError("reached max zoom out");
        return;
    }

    const Point virtualCenterOffset = (drawDimension / 2 - Size(1)).toPoint(),
        visibleCenterOffset = virtualCenterOffset;

    ViewMode viewMode = m_viewMode;
    if(m_autoViewMode) {
        if(tileSize >= Otc::TILE_PIXELS && visibleDimension.area() <= NEAR_VIEW_AREA)
            viewMode = NEAR_VIEW;
        else if(tileSize >= 16 && visibleDimension.area() <= MID_VIEW_AREA)
            viewMode = MID_VIEW;
        else if(tileSize >= 8 && visibleDimension.area() <= FAR_VIEW_AREA)
            viewMode = FAR_VIEW;
        else
            viewMode = HUGE_VIEW;

        m_multifloor = viewMode < FAR_VIEW;
    }

    // draw actually more than what is needed to avoid massive recalculations on huge views
    /* if(viewMode >= HUGE_VIEW) {
        Size oldDimension = drawDimension;
        drawDimension = (m_framebuffer->getSize() / tileSize);
        virtualCenterOffset += (drawDimension - oldDimension).toPoint() / 2;
    }*/

    m_viewMode = viewMode;
    m_visibleDimension = visibleDimension;
    m_drawDimension = drawDimension;
    m_tileSize = tileSize;
    m_virtualCenterOffset = virtualCenterOffset;
    m_visibleCenterOffset = visibleCenterOffset;
    m_optimizedSize = optimizedSize;

    m_rectDimension = Rect(0, 0, bufferSize);

    m_scaleFactor = m_tileSize / static_cast<float>(Otc::TILE_PIXELS);

    m_frameCache.tile->resize(bufferSize);
    if(m_drawLights) m_lightView->resize();

    for(const auto& frame : { m_frameCache.creatureInformation, m_frameCache.staticText, m_frameCache.dynamicText })
        frame->resize(g_graphics.getViewportSize());

    m_awareRange.left = std::min<uint16>(g_map.getAwareRange().left, (m_drawDimension.width() / 2) - 1);
    m_awareRange.top = std::min<uint16>(g_map.getAwareRange().top, (m_drawDimension.height() / 2) - 1);
    m_awareRange.bottom = m_awareRange.top + 1;
    m_awareRange.right = m_awareRange.left + 1;
    m_rectCache.rect = Rect();

    updateViewportDirectionCache();
    requestVisibleTilesCacheUpdate();
}

void MapView::onCameraMove(const Point& /*offset*/)
{
    m_rectCache.rect = Rect();

    for(const auto& frame : { m_frameCache.tile, m_frameCache.staticText, m_frameCache.dynamicText })
        frame->update();

    if(m_drawLights) m_lightView->update();

    if(isFollowingCreature()) {
        if(m_followingCreature->isWalking()) {
            m_viewport = m_viewPortDirection[m_followingCreature->getDirection()];
        } else {
            m_viewport = m_viewPortDirection[Otc::InvalidDirection];
            m_mustUpdateVisibleCreaturesCache = true;
        }
    }
}

void MapView::onGlobalLightChange(const Light&)
{
    updateLight();
}

void MapView::onFloorChange(const uint8 /*floor*/, const uint8 /*previousFloor*/)
{
    updateLight();
}

const static Color STATIC_SHADOWING_COLOR(static_cast<uint8>(215), static_cast<uint8>(0), .6f);
void MapView::onFloorDrawingStart(const uint8 floor)
{
    const auto cameraPosition = getCameraPosition();

    if(hasFloorShadowingFlag()) {
        Color shadowColor = Color::white;

        if(floor > Otc::SEA_FLOOR) { // Cave
            if((hasFloorShadowingFlag(Otc::SHADOWFLOOR_BOTTOM) && floor > cameraPosition.z) ||
               (hasFloorShadowingFlag(Otc::SHADOWFLOOR_UPSIDE) && floor < cameraPosition.z)
               ) {
                float brightnessLevelStart = .6f;
                float brightnessLevel = cameraPosition.z - floor;
                if(floor > cameraPosition.z)
                    brightnessLevel *= -1;
                else brightnessLevelStart -= .1f;

                brightnessLevel *= .12f;

                shadowColor = Color(static_cast<uint8>(215), static_cast<uint8>(0), brightnessLevelStart - brightnessLevel);
            }
        } else {
            if(hasFloorShadowingFlag(Otc::SHADOWFLOOR_BOTTOM) && floor > cameraPosition.z) {
                shadowColor = STATIC_SHADOWING_COLOR;
            } else if(hasFloorShadowingFlag(Otc::SHADOWFLOOR_UPSIDE) && floor < cameraPosition.z) {
                shadowColor = m_drawLights ?
                    Color(m_lightView->getGlobalLight().color, std::floor<uint8>(m_lightView->getGlobalLight().intensity / 100), .8f)
                    : STATIC_SHADOWING_COLOR;
            }
        }

        g_painter->setColor(shadowColor);
        m_lastFloorShadowingColor = shadowColor;
    }
}

void MapView::onFloorDrawingEnd(const uint8 /*floor*/)
{
    if(hasFloorShadowingFlag()) {
        g_painter->resetColor();
    }
}

void MapView::onTileUpdate(const Position&)
{
    requestVisibleTilesCacheUpdate();
}

void MapView::onPositionChange(const Position& /*newPos*/, const Position& /*oldPos*/) {}

// isVirtualMove is when the mouse is stopped, but the camera moves,
// so the onMouseMove event is triggered by sending the new tile position that the mouse is in.
void MapView::onMouseMove(const Position& mousePos, const bool /*isVirtualMove*/)
{
    { // Highlight Target System
        if(m_lastHighlightTile) {
            m_lastHighlightTile->unselect();
            m_lastHighlightTile = nullptr;
        }

        if(m_drawHighlightTarget) {
            if(m_lastHighlightTile = g_map.getTile(mousePos))
                m_lastHighlightTile->select();
        }
    }
}

void MapView::onMapCenterChange(const Position&)
{
    requestVisibleTilesCacheUpdate();
}

void MapView::updateLight()
{
    if(!m_drawLights) return;

    const auto cameraPosition = getCameraPosition();

    Light ambientLight = cameraPosition.z > Otc::SEA_FLOOR ? Light() : g_map.getLight();
    ambientLight.intensity = std::max<uint8>(m_minimumAmbientLight * 255, ambientLight.intensity);

    m_lightView->setGlobalLight(ambientLight);
    m_lightView->update();
}

void MapView::lockFirstVisibleFloor(uint8 firstVisibleFloor)
{
    m_lockedFirstVisibleFloor = firstVisibleFloor;
    requestVisibleTilesCacheUpdate();
}

void MapView::unlockFirstVisibleFloor()
{
    m_lockedFirstVisibleFloor = UINT8_MAX;
    requestVisibleTilesCacheUpdate();
}

void MapView::setVisibleDimension(const Size& visibleDimension)
{
    if(visibleDimension == m_visibleDimension)
        return;

    if(visibleDimension.width() % 2 != 1 || visibleDimension.height() % 2 != 1) {
        g_logger.traceError("visible dimension must be odd");
        return;
    }

    if(visibleDimension < Size(3)) {
        g_logger.traceError("reach max zoom in");
        return;
    }

    updateGeometry(visibleDimension, m_optimizedSize);
}

void MapView::setViewMode(ViewMode viewMode)
{
    m_viewMode = viewMode;
    requestVisibleTilesCacheUpdate();
}

void MapView::setAutoViewMode(bool enable)
{
    m_autoViewMode = enable;
    if(enable)
        updateGeometry(m_visibleDimension, m_optimizedSize);
}

void MapView::optimizeForSize(const Size& visibleSize)
{
    updateGeometry(m_visibleDimension, visibleSize);
}

void MapView::setAntiAliasing(const bool enable)
{
    m_frameCache.tile->cleanTexture();
    m_frameCache.tile->setSmooth(enable);

    updateGeometry(m_visibleDimension, m_optimizedSize);
}

void MapView::setRenderScale(const uint8 scale)
{
    m_renderScale = scale;
    updateGeometry(m_visibleDimension, m_optimizedSize);
    updateLight();
}

void MapView::followCreature(const CreaturePtr& creature)
{
    m_follow = true;
    m_followingCreature = creature;
    m_lastCameraPosition = Position();

    requestVisibleTilesCacheUpdate();
}

void MapView::setCameraPosition(const Position& pos)
{
    m_follow = false;
    m_customCameraPosition = pos;
    requestVisibleTilesCacheUpdate();
}

Position MapView::getPosition(const Point& point, const Size& mapSize)
{
    const Position cameraPosition = getCameraPosition();

    // if we have no camera, its impossible to get the tile
    if(!cameraPosition.isValid())
        return Position();

    const Rect srcRect = calcFramebufferSource(mapSize);
    const float sh = srcRect.width() / static_cast<float>(mapSize.width());
    const float sv = srcRect.height() / static_cast<float>(mapSize.height());

    const Point framebufferPos = Point(point.x * sh, point.y * sv);
    const Point centerOffset = (framebufferPos + srcRect.topLeft()) / m_tileSize;

    const Point tilePos2D = getVisibleCenterOffset() - m_drawDimension.toPoint() + centerOffset + Point(2);
    if(tilePos2D.x + cameraPosition.x < 0 && tilePos2D.y + cameraPosition.y < 0)
        return Position();

    Position position = Position(tilePos2D.x, tilePos2D.y, 0) + cameraPosition;

    if(!position.isValid())
        return Position();

    return position;
}

void MapView::move(int32 x, int32 y)
{
    m_moveOffset.x += x;
    m_moveOffset.y += y;

    int32_t tmp = m_moveOffset.x / Otc::TILE_PIXELS;
    bool requestTilesUpdate = false;
    if(tmp != 0) {
        m_customCameraPosition.x += tmp;
        m_moveOffset.x %= Otc::TILE_PIXELS;
        requestTilesUpdate = true;
    }

    tmp = m_moveOffset.y / Otc::TILE_PIXELS;
    if(tmp != 0) {
        m_customCameraPosition.y += tmp;
        m_moveOffset.y %= Otc::TILE_PIXELS;
        requestTilesUpdate = true;
    }

    if(requestTilesUpdate) {
        requestVisibleTilesCacheUpdate();
        onCameraMove(m_moveOffset);
    }
}

Rect MapView::calcFramebufferSource(const Size& destSize)
{
    Point drawOffset = ((m_drawDimension - m_visibleDimension - Size(1)).toPoint() / 2) * m_tileSize;
    if(isFollowingCreature())
        drawOffset += m_followingCreature->getWalkOffset() * m_scaleFactor;
    else if(!m_moveOffset.isNull())
        drawOffset += m_moveOffset * m_scaleFactor;

    Size srcSize = destSize;
    const Size srcVisible = m_visibleDimension * m_tileSize;
    srcSize.scale(srcVisible, Fw::KeepAspectRatio);
    drawOffset.x += (srcVisible.width() - srcSize.width()) / 2;
    drawOffset.y += (srcVisible.height() - srcSize.height()) / 2;

    return Rect(drawOffset, srcSize);
}

uint8 MapView::calcFirstVisibleFloor()
{
    uint8 z = Otc::SEA_FLOOR;
    // return forced first visible floor
    if(m_lockedFirstVisibleFloor != UINT8_MAX) {
        z = m_lockedFirstVisibleFloor;
    } else {
        const Position cameraPosition = getCameraPosition();

        // this could happens if the player is not known yet
        if(cameraPosition.isValid()) {
            // avoid rendering multifloors in far views
            if(!m_multifloor) {
                z = cameraPosition.z;
            } else {
                // if nothing is limiting the view, the first visible floor is 0
                uint8 firstFloor = 0;

                // limits to underground floors while under sea level
                if(cameraPosition.z > Otc::SEA_FLOOR)
                    firstFloor = std::max<uint8>(cameraPosition.z - Otc::AWARE_UNDEGROUND_FLOOR_RANGE, Otc::UNDERGROUND_FLOOR);

                // loop in 3x3 tiles around the camera
                for(int_fast32_t ix = -1; ix <= 1 && firstFloor < cameraPosition.z; ++ix) {
                    for(int_fast32_t iy = -1; iy <= 1 && firstFloor < cameraPosition.z; ++iy) {
                        const Position pos = cameraPosition.translated(ix, iy);

                        // process tiles that we can look through, e.g. windows, doors
                        if((ix == 0 && iy == 0) || ((std::abs(ix) != std::abs(iy)) && g_map.isLookPossible(pos))) {
                            Position upperPos = pos;
                            Position coveredPos = pos;

                            const auto isLookPossible = g_map.isLookPossible(pos);
                            while(coveredPos.coveredUp() && upperPos.up() && upperPos.z >= firstFloor) {
                                // check tiles physically above
                                TilePtr tile = g_map.getTile(upperPos);
                                if(tile && tile->limitsFloorsView(!isLookPossible)) {
                                    firstFloor = upperPos.z + 1;
                                    break;
                                }

                                // check tiles geometrically above
                                tile = g_map.getTile(coveredPos);
                                if(tile && tile->limitsFloorsView(isLookPossible)) {
                                    firstFloor = coveredPos.z + 1;
                                    break;
                                }
                            }
                        }
                    }
                }

                z = firstFloor;
            }
        }
    }

    // just ensure the that the floor is in the valid range
    z = stdext::clamp<int>(z, 0, static_cast<int>(Otc::MAX_Z));
    return z;
}

uint8 MapView::calcLastVisibleFloor()
{
    if(!m_multifloor)
        return calcFirstVisibleFloor();

    uint8 z = Otc::SEA_FLOOR;

    const Position cameraPosition = getCameraPosition();
    // this could happens if the player is not known yet
    if(cameraPosition.isValid()) {
        // view only underground floors when below sea level
        if(cameraPosition.z > Otc::SEA_FLOOR)
            z = cameraPosition.z + Otc::AWARE_UNDEGROUND_FLOOR_RANGE;
        else
            z = Otc::SEA_FLOOR;
    }

    if(m_lockedFirstVisibleFloor != UINT8_MAX)
        z = std::max<int>(m_lockedFirstVisibleFloor, z);

    // just ensure the that the floor is in the valid range
    z = stdext::clamp<int>(z, 0, static_cast<int>(Otc::MAX_Z));
    return z;
}

Position MapView::getCameraPosition()
{
    if(isFollowingCreature())
        return m_followingCreature->getPosition();

    return m_customCameraPosition;
}

void MapView::setShader(const PainterShaderProgramPtr& shader, float fadein, float fadeout)
{
    if((m_shader == shader && m_shaderSwitchDone) || (m_nextShader == shader && !m_shaderSwitchDone))
        return;

    if(fadeout > 0.0f && m_shader) {
        m_nextShader = shader;
        m_shaderSwitchDone = false;
    } else {
        m_shader = shader;
        m_nextShader = nullptr;
        m_shaderSwitchDone = true;
    }
    m_fadeTimer.restart();
    m_fadeInTime = fadein;
    m_fadeOutTime = fadeout;
}

void MapView::setDrawLights(bool enable)
{
    if(enable == m_drawLights) return;

    m_lightView = enable ? LightViewPtr(new LightView(this)) : nullptr;
    m_drawLights = enable;

    updateLight();
}

void MapView::updateViewportDirectionCache()
{
    for(uint8 dir = Otc::North; dir <= Otc::InvalidDirection; ++dir) {
        AwareRange& vp = m_viewPortDirection[dir];
        vp.top = m_awareRange.top;
        vp.right = m_awareRange.right;
        vp.bottom = vp.top;
        vp.left = vp.right;

        switch(dir) {
        case Otc::North:
        case Otc::South:
            vp.top += 1;
            vp.bottom += 1;
            break;

        case Otc::West:
        case Otc::East:
            vp.right += 1;
            vp.left += 1;
            break;

        case Otc::NorthEast:
        case Otc::SouthEast:
        case Otc::NorthWest:
        case Otc::SouthWest:
            vp.left += 1;
            vp.bottom += 1;
            vp.top += 1;
            vp.right += 1;
            break;

        case Otc::InvalidDirection:
            vp.left -= 1;
            vp.right -= 1;
            break;

        default:
            break;
        }
    }
}

std::vector<CreaturePtr> MapView::getSightSpectators(const Position& centerPos, bool multiFloor)
{
    return g_map.getSpectatorsInRangeEx(centerPos, multiFloor, m_awareRange.left - 1, m_awareRange.right - 2, m_awareRange.top - 1, m_awareRange.bottom - 2);
}

std::vector<CreaturePtr> MapView::getSpectators(const Position& centerPos, bool multiFloor)
{
    return g_map.getSpectatorsInRangeEx(centerPos, multiFloor, m_awareRange.left, m_awareRange.right, m_awareRange.top, m_awareRange.bottom);
}

bool MapView::isInRange(const Position& pos, const bool ignoreZ)
{
    return getCameraPosition().isInRange(pos, m_awareRange.left - 1, m_awareRange.right - 2, m_awareRange.top - 1, m_awareRange.bottom - 2, ignoreZ);
}

void MapView::setCrosshairTexture(const std::string& texturePath)
{
    m_crosshairTexture = texturePath.empty() ? nullptr : g_textures.getTexture(texturePath);
}

void MapView::addVisibleCreature(const CreaturePtr& creature)
{
    if(creature->isLocalPlayer() || !isInRange(creature->getPosition())) return;
    m_visibleCreatures.push_back(creature);
}

void MapView::removeVisibleCreature(const CreaturePtr& creature)
{
    if(creature->isLocalPlayer()) return;
    const auto it = std::find(m_visibleCreatures.begin(), m_visibleCreatures.end(), creature);
    if(it != m_visibleCreatures.end())
        m_visibleCreatures.erase(it);
}

/* vim: set ts=4 sw=4 et: */