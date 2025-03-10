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

#include <client/thing/creature/creature.h>
#include <client/thing/effect.h>
#include <client/game.h>
#include <client/thing/item.h>
#include <client/map/lightview.h>
#include <client/thing/creature/localplayer.h>
#include <client/lua/luavaluecasts.h>
#include <client/map/map.h>
#include <client/manager/thingtypemanager.h>
#include <client/map/tile.h>

#include <framework/core/clock.h>
#include <framework/core/eventdispatcher.h>
#include <framework/graphics/graphics.h>

#include <framework/graphics/framebuffermanager.h>
#include <framework/graphics/paintershaderprogram.h>
#include <framework/graphics/texturemanager.h>
#include <framework/graphics/ogl/painterogl2_shadersources.h>
#include <client/manager/spritemanager.h>

#include "framework/stdext/math.h"

double Creature::speedA = 0;
double Creature::speedB = 0;
double Creature::speedC = 0;

Creature::Creature() : Thing()
{
    m_id = 0;
    m_speed = 200;
    m_healthPercent = 101;

    m_footStep = 0;

    m_walkedPixels = 0;
    m_totalWalkedPixels = 0;
    m_walkAnimationPhase = 0;

    m_direction = Otc::South;
    m_walkTurnDirection = Otc::InvalidDirection;
    m_lastStepDirection = Otc::InvalidDirection;

    m_icon = Otc::NpcIconNone;
    m_type = Proto::CREATURE_TYPE_UNKNOW;
    m_skull = Otc::SkullNone;
    m_shield = Otc::ShieldNone;
    m_emblem = Otc::EmblemNone;

    m_outfitColor = Color::white;

    m_nameCache.setFont(g_fonts.getFont("verdana-11px-rounded"));
    m_nameCache.setAlign(Fw::AlignTopCenter);
}

void Creature::turn(Otc::Direction_t direction)
{
    // schedules to set the new direction when walk ends
    if(m_walking) {
        m_walkTurnDirection = direction;
        return;
    }

    // if is not walking change the direction right away
    setDirection(direction);
}

void Creature::walk(const Position& oldPos, const Position& newPos)
{
    if(oldPos == newPos)
        return;

    // get walk direction
    m_lastStepDirection = oldPos.getDirectionFromPosition(newPos);
    m_lastStepToPosition = newPos;
    m_lastStepFromPosition = oldPos;

    // set current walking direction
    setDirection(m_lastStepDirection);

    // starts counting walk
    m_walking = true;
    m_walkedPixels = 0;
    m_walkTimer.restart();

    // no direction need to be changed when the walk ends
    m_walkTurnDirection = Otc::InvalidDirection;

    if(m_walkFinishAnimEvent) {
        m_walkFinishAnimEvent->cancel();
        m_walkFinishAnimEvent = nullptr;
    }

    // starts updating walk
    nextWalkUpdate();
}

void Creature::stopWalk()
{
    if(!m_walking)
        return;

    // stops the walk right away
    terminateWalk();
}

void Creature::jump(int height, int duration)
{
    if(!m_jumpOffset.isNull())
        return;

    m_jumpTimer.restart();
    m_jumpHeight = height;
    m_jumpDuration = duration;

    updateJump();
}

void Creature::updateJump()
{
    if(m_jumpTimer.ticksElapsed() >= m_jumpDuration) {
        m_jumpOffset = PointF(0, 0);
        return;
    }

    const int t = m_jumpTimer.ticksElapsed(),
        halfJumpDuration = m_jumpDuration / 2;

    const double a = -4 * m_jumpHeight / (m_jumpDuration * m_jumpDuration),
        b = +4 * m_jumpHeight / m_jumpDuration,
        height = a * t * t + b * t;

    const int roundHeight = stdext::round(height);

    m_jumpOffset = PointF(height, height);

    if(isLocalPlayer()) {
        g_map.notificateCameraMove(m_walkOffset);
    }

    int8 diff = 0;
    if(m_jumpTimer.ticksElapsed() < halfJumpDuration)
        diff = 1;
    else if(m_jumpTimer.ticksElapsed() > halfJumpDuration)
        diff = -1;

    int nextT, i = 1;
    do {
        nextT = stdext::round((-b + std::sqrt(std::max<double>(b * b + 4 * a * (roundHeight + diff * i), 0.0)) * diff) / (2 * a));
        ++i;

        if(nextT < halfJumpDuration)
            diff = 1;
        else if(nextT > halfJumpDuration)
            diff = -1;
    } while(nextT - m_jumpTimer.ticksElapsed() == 0 && i < 3);

    // schedules next update
    const auto self = static_self_cast<Creature>();
    g_dispatcher.scheduleEvent([self] {
        self->updateJump();
    }, nextT - m_jumpTimer.ticksElapsed());
}

void Creature::onPositionChange(const Position& newPos, const Position& oldPos)
{
    callLuaField("onPositionChange", newPos, oldPos);
}

void Creature::onAppear()
{
    // cancel any disappear event
    if(m_disappearEvent) {
        m_disappearEvent->cancel();
        m_disappearEvent = nullptr;
    }

    // creature appeared the first time or wasn't seen for a long time
    if(m_removed) {
        m_removed = false;
        stopWalk();
        callLuaField("onAppear");
    } // walk
    else if(m_oldPosition != m_position && m_oldPosition.isInRange(m_position, 1, 1) && m_allowAppearWalk) {
        m_allowAppearWalk = false;
        walk(m_oldPosition, m_position);
        callLuaField("onWalk", m_oldPosition, m_position);
    } // teleport
    else if(m_oldPosition != m_position) {
        stopWalk();
        callLuaField("onDisappear");
        callLuaField("onAppear");
    } // else turn
}

void Creature::onDisappear()
{
    if(m_disappearEvent)
        m_disappearEvent->cancel();

    m_oldPosition = m_position;

    // a pair onDisappear and onAppear events are fired even when creatures walks or turns,
    // so we must filter
    auto self = static_self_cast<Creature>();
    m_disappearEvent = g_dispatcher.addEvent([self] {
        self->m_removed = true;
        self->stopWalk();

        self->callLuaField("onDisappear");

        // invalidate this creature position
        if(!self->isLocalPlayer())
            self->setPosition(Position());

        self->m_oldPosition = Position();
        self->m_disappearEvent = nullptr;
    });
}

void Creature::onDeath()
{
    callLuaField("onDeath");
}

void Creature::updateWalkAnimation()
{
    if(m_outfit.getCategory() != ThingCategoryCreature)
        return;

    const int footAnimPhases = getTotalAnimationPhase();

    // looktype has no animations
    if(footAnimPhases == 0) {
        return;
    }

    const int footDelay = std::max<int>(m_stepCache.getDuration(m_lastStepDirection) / footAnimPhases, 20);
    if(m_footTimer.ticksElapsed() >= footDelay) {
        if(m_walkAnimationPhase == footAnimPhases) m_walkAnimationPhase = 1;
        else ++m_walkAnimationPhase;

        m_footTimer.restart();
    }
}

void Creature::updateWalkOffset(int totalPixelsWalked)
{
    m_walkOffset = Point();
    if(m_direction == Otc::North || m_direction == Otc::NorthEast || m_direction == Otc::NorthWest)
        m_walkOffset.y = SPRITE_SIZE - totalPixelsWalked;
    else if(m_direction == Otc::South || m_direction == Otc::SouthEast || m_direction == Otc::SouthWest)
        m_walkOffset.y = totalPixelsWalked - SPRITE_SIZE;

    if(m_direction == Otc::East || m_direction == Otc::NorthEast || m_direction == Otc::SouthEast)
        m_walkOffset.x = totalPixelsWalked - SPRITE_SIZE;
    else if(m_direction == Otc::West || m_direction == Otc::NorthWest || m_direction == Otc::SouthWest)
        m_walkOffset.x = SPRITE_SIZE - totalPixelsWalked;
}

void Creature::updateWalkingTile()
{
    // determine new walking tile
    TilePtr newWalkingTile;

    const Rect virtualCreatureRect(SPRITE_SIZE + (m_walkOffset.x - getDisplacementX()),
                                   SPRITE_SIZE + (m_walkOffset.y - getDisplacementY()),
                                   SPRITE_SIZE, SPRITE_SIZE);

    for(int xi = -1; xi <= 1 && !newWalkingTile; ++xi) {
        for(int yi = -1; yi <= 1 && !newWalkingTile; ++yi) {
            Rect virtualTileRect((xi + 1) * SPRITE_SIZE, (yi + 1) * SPRITE_SIZE, SPRITE_SIZE, SPRITE_SIZE);

            // only render creatures where bottom right is inside tile rect
            if(virtualTileRect.contains(virtualCreatureRect.bottomRight())) {
                newWalkingTile = g_map.getOrCreateTile(m_position.translated(xi, yi, 0));
            }
        }
    }

    if(newWalkingTile == m_walkingTile) return;

    if(m_walkingTile)
        m_walkingTile->removeWalkingCreature(static_self_cast<Creature>());

    if(newWalkingTile) {
        newWalkingTile->addWalkingCreature(static_self_cast<Creature>());

        // recache visible tiles in map views
        if(newWalkingTile->isEmpty()) {
            g_map.notificateTileUpdate(newWalkingTile->getPosition());
        }
    }

    m_walkingTile = newWalkingTile;
}

void Creature::nextWalkUpdate()
{
    // remove any previous scheduled walk updates
    if(m_walkUpdateEvent)
        m_walkUpdateEvent->cancel();

    // do the update
    updateWalk();

    if(!m_walking) return;

    // schedules next update
    auto self = static_self_cast<Creature>();
    m_walkUpdateEvent = g_dispatcher.scheduleEvent([self] {
        self->m_walkUpdateEvent = nullptr;
        self->nextWalkUpdate();
    }, std::max<int>(m_stepCache.duration / SPRITE_SIZE, 16));
}

void Creature::updateWalk()
{
    int stepDuration = getStepDuration(true);
    stepDuration += (20 - stepDuration * .05);

    if(m_walking && m_walkTimer.ticksElapsed() >= stepDuration) {
        terminateWalk();
        return;
    }

    const float walkTicksPerPixel = static_cast<float>(stepDuration) / SPRITE_SIZE;
    const int totalPixelsWalked = std::min<int>(m_walkTimer.ticksElapsed() / walkTicksPerPixel, SPRITE_SIZE);

    // needed for paralyze effect
    m_walkedPixels = std::max<int>(m_walkedPixels, totalPixelsWalked);

    // total number of pixels walked
    m_totalWalkedPixels += m_walkedPixels;

    updateWalkAnimation();
    updateWalkOffset(m_walkedPixels);
    updateWalkingTile();
}

void Creature::terminateWalk()
{
    // remove any scheduled walk update
    if(m_walkUpdateEvent) {
        m_walkUpdateEvent->cancel();
        m_walkUpdateEvent = nullptr;
    }

    // now the walk has ended, do any scheduled turn
    if(m_walkTurnDirection != Otc::InvalidDirection) {
        setDirection(m_walkTurnDirection);
        m_walkTurnDirection = Otc::InvalidDirection;
    }

    if(m_walkingTile) {
        m_walkingTile->removeWalkingCreature(static_self_cast<Creature>());
        m_walkingTile = nullptr;
    }

    m_walking = false;
    m_walkOffset = Point();
    m_walkedPixels = 0;

    const auto self = static_self_cast<Creature>();
    m_walkFinishAnimEvent = g_dispatcher.scheduleEvent([self] {
        self->m_totalWalkedPixels = 0;
        self->m_walkAnimationPhase = 0;
        self->m_walkFinishAnimEvent = nullptr;
    }, 50);
}

void Creature::setName(const std::string& name)
{
    m_nameCache.setText(name);
    m_name = name;
}

void Creature::setHealthPercent(uint8 healthPercent)
{
    if(m_healthPercent == healthPercent) return;

    if(healthPercent > 92)
        m_informationColor = Color(0x00, 0xBC, 0x00);
    else if(healthPercent > 60)
        m_informationColor = Color(0x50, 0xA1, 0x50);
    else if(healthPercent > 30)
        m_informationColor = Color(0xA1, 0xA1, 0x00);
    else if(healthPercent > 8)
        m_informationColor = Color(0xBF, 0x0A, 0x0A);
    else if(healthPercent > 3)
        m_informationColor = Color(0x91, 0x0F, 0x0F);
    else
        m_informationColor = Color(0x85, 0x0C, 0x0C);

    const uint8 oldHealthPercent = m_healthPercent;
    m_healthPercent = healthPercent;
    callLuaField("onHealthPercentChange", healthPercent, oldHealthPercent);

    if(isDead()) onDeath();
}

void Creature::setDirection(Otc::Direction_t direction)
{
    assert(direction != Otc::InvalidDirection);
    m_direction = direction;
}

void Creature::setOutfit(const Outfit& outfit)
{
    const Outfit oldOutfit = m_outfit;
    if(outfit.getCategory() != ThingCategoryCreature) {
        if(!g_things.isValidDatId(outfit.getAuxId(), outfit.getCategory()))
            return;

        m_outfit.setAuxId(outfit.getAuxId());
        m_outfit.setCategory(outfit.getCategory());
    } else {
        if(outfit.getClothes().id > 0 && !g_things.isValidDatId(outfit.getClothes().id, ThingCategoryCreature))
            return;

        m_outfit = outfit;
    }

    m_walkAnimationPhase = 0; // might happen when player is walking and outfit is changed.

    callLuaField("onOutfitChange", m_outfit, oldOutfit);

    // Cache
    {
        if(m_outfit.getCategory() == ThingCategoryCreature)
            m_drawCache.exactSize = getExactSize();
        else
            m_drawCache.exactSize = g_things.rawGetThingType(m_outfit.getAuxId(), m_outfit.getCategory())->getExactSize();

        m_drawCache.frameSizeNotResized = std::max<int>(m_drawCache.exactSize * 0.75f, 2 * SPRITE_SIZE * 0.75f);
    }
}

void Creature::setOutfitColor(const Color& color, int duration)
{
    if(m_outfitColorUpdateEvent) {
        m_outfitColorUpdateEvent->cancel();
        m_outfitColorUpdateEvent = nullptr;
    }

    if(duration <= 0) {
        m_outfitColor = color;
        return;
    }

    const Color delta = (color - m_outfitColor) / static_cast<float>(duration);
    m_outfitColorTimer.restart();
    updateOutfitColor(m_outfitColor, color, delta, duration);
}

void Creature::updateOutfitColor(Color color, Color finalColor, Color delta, int duration)
{
    if(m_outfitColorTimer.ticksElapsed() >= duration) {
        m_outfitColor = finalColor;
        return;
    }

    m_outfitColor = color + delta * m_outfitColorTimer.ticksElapsed();

    const auto self = static_self_cast<Creature>();
    m_outfitColorUpdateEvent = g_dispatcher.scheduleEvent([=] {
        self->updateOutfitColor(color, finalColor, delta, duration);
    }, 100);
}

void Creature::setSpeed(uint16 speed)
{
    const uint16 oldSpeed = m_speed;
    m_speed = speed;

    // Cache for stepSpeed Law
    speed *= 2;

    if(speed > -speedB) {
        m_calculatedStepSpeed = floor((speedA * log((speed / 2) + speedB) + speedC) + 0.5);
        if(m_calculatedStepSpeed == 0) m_calculatedStepSpeed = 1;
    } else m_calculatedStepSpeed = 1;

    // speed can change while walking (utani hur, paralyze, etc..)
    if(m_walking)
        nextWalkUpdate();

    callLuaField("onSpeedChange", m_speed, oldSpeed);
}

void Creature::setBaseSpeed(double baseSpeed)
{
    if(m_baseSpeed != baseSpeed) {
        const double oldBaseSpeed = m_baseSpeed;
        m_baseSpeed = baseSpeed;

        callLuaField("onBaseSpeedChange", baseSpeed, oldBaseSpeed);
    }
}

void Creature::setSkull(uint8 skull)
{
    m_skull = skull;
    callLuaField("onSkullChange", m_skull);
}

void Creature::setShield(uint8 shield)
{
    m_shield = shield;
    callLuaField("onShieldChange", m_shield);
}

void Creature::setEmblem(uint8 emblem)
{
    m_emblem = emblem;
    callLuaField("onEmblemChange", m_emblem);
}

void Creature::setType(uint8 type)
{
    m_type = type;
    callLuaField("onTypeChange", m_type);
}

void Creature::setIcon(uint8 icon)
{
    m_icon = icon;
    callLuaField("onIconChange", m_icon);
}

void Creature::setSkullTexture(const std::string& filename)
{
    m_skullTexture = g_textures.getTexture(filename);
}

void Creature::setShieldTexture(const std::string& filename, bool blink)
{
    m_shieldTexture = g_textures.getTexture(filename);
    m_showShieldTexture = true;

    if(blink && !m_shieldBlink) {
        auto self = static_self_cast<Creature>();
        g_dispatcher.scheduleEvent([self]() {
            self->updateShield();
        }, SHIELD_BLINK_TICKS);
    }

    m_shieldBlink = blink;
}

void Creature::updateShield()
{
    m_showShieldTexture = !m_showShieldTexture;

    if(m_shield != Otc::ShieldNone && m_shieldBlink) {
        auto self = static_self_cast<Creature>();
        g_dispatcher.scheduleEvent([self]() {
            self->updateShield();
        }, SHIELD_BLINK_TICKS);
    } else if(!m_shieldBlink)
        m_showShieldTexture = true;
}

void Creature::setEmblemTexture(const std::string& filename)
{
    m_emblemTexture = g_textures.getTexture(filename);
}

void Creature::setTypeTexture(const std::string& filename)
{
    m_typeTexture = g_textures.getTexture(filename);
}

void Creature::setIconTexture(const std::string& filename)
{
    m_iconTexture = g_textures.getTexture(filename);
}

void Creature::addTimedSquare(uint8 color)
{
    m_showTimedSquare = true;
    m_timedSquareColor = Color::from8bit(color);

    // schedule removal
    const auto self = static_self_cast<Creature>();
    g_dispatcher.scheduleEvent([self]() {
        self->removeTimedSquare();
    }, VOLATILE_SQUARE_DURATION);
}

Point Creature::getDrawOffset()
{
    Point drawOffset;
    if(m_walking) {
        if(m_walkingTile)
            drawOffset -= Point(m_walkingTile->getDrawElevation());
        drawOffset += m_walkOffset;
    } else {
        const TilePtr& tile = getTile();
        if(tile)
            drawOffset -= Point(tile->getDrawElevation());
    }

    return drawOffset;
}

int Creature::getStepDuration(bool ignoreDiagonal, Otc::Direction_t dir)
{
    if(isParalyzed())
        return 0;

    Position tilePos = dir == Otc::InvalidDirection ? m_lastStepToPosition : m_position.translatedToDirection(dir);
    if(!tilePos.isValid())
        tilePos = m_position;

    const TilePtr& tile = g_map.getTile(tilePos);

    int groundSpeed = 0;
    if(tile) groundSpeed = tile->getGroundSpeed();
    if(groundSpeed == 0)
        groundSpeed = 150;

    if(m_speed != m_stepCache.speed || groundSpeed != m_stepCache.groundSpeed) {
        const auto serverBeat = g_game.getServerBeat();

        m_stepCache.speed = m_speed;
        m_stepCache.groundSpeed = groundSpeed;

        double stepDuration = std::floor(1000 * groundSpeed / m_calculatedStepSpeed);
        stepDuration = std::ceil(stepDuration / serverBeat) * serverBeat;

        m_stepCache.duration = stepDuration;
        m_stepCache.diagonalDuration = stepDuration * 3;
    }

    return ignoreDiagonal ? m_stepCache.duration : m_stepCache.getDuration(m_lastStepDirection);
}

Point Creature::getDisplacement()
{
    if(m_outfit.getCategory() == ThingCategoryEffect)
        return Point(8);

    if(m_outfit.getCategory() == ThingCategoryItem)
        return Point();

    return Thing::getDisplacement();
}

int Creature::getDisplacementX()
{
    if(m_outfit.getCategory() == ThingCategoryEffect)
        return 8;

    if(m_outfit.getCategory() == ThingCategoryItem)
        return 0;

    if(m_outfit.hasMount())
        return rawGetMountThingType()->getDisplacementX();

    return Thing::getDisplacementX();
}

int Creature::getDisplacementY()
{
    if(m_outfit.getCategory() == ThingCategoryEffect)
        return 8;

    if(m_outfit.getCategory() == ThingCategoryItem)
        return 0;

    if(m_outfit.hasMount()) {
        return rawGetMountThingType()->getDisplacementY();
    }

    return Thing::getDisplacementY();
}

Light Creature::getLight()
{
    Light light = Thing::getLight();

    if(m_light.color > 0 && m_light.intensity >= light.intensity)
        light = m_light;

    return light;
}

int Creature::getTotalAnimationPhase()
{
    if(!m_outfit.hasMount()) return getAnimationPhases();

    return rawGetMountThingType()->getAnimationPhases();
}

int Creature::getCurrentAnimationPhase(const bool mount)
{
    const auto& thingType = mount ? rawGetMountThingType() : rawGetThingType();

    const auto idleAnimator = thingType->getIdleAnimator();
    if(idleAnimator) {
        if(m_walkAnimationPhase == 0) return idleAnimator->getPhase();
        return m_walkAnimationPhase + idleAnimator->getAnimationPhases() - 1;
    }

    if(thingType->isAnimateAlways()) {
        const int ticksPerFrame = std::round(1000 / thingType->getAnimationPhases());
        return (g_clock.millis() % (ticksPerFrame * thingType->getAnimationPhases())) / ticksPerFrame;
    }

    return m_walkAnimationPhase;
}

int Creature::getExactSize(int layer, int xPattern, int yPattern, int zPattern, int animationPhase)
{
    const int numPatternY = getNumPatternY(),
        layers = getLayers();

    animationPhase = 0;

    if(m_outfit.hasMount())
        zPattern = m_outfit.hasMount() ? 1 : 0;

    int exactSize = 0;
    for(yPattern = 0; yPattern < numPatternY; ++yPattern) {
        if(yPattern > 0 && !(m_outfit.getAddons() & (1 << (yPattern - 1))))
            continue;

        for(layer = 0; layer < layers; ++layer)
            exactSize = std::max<int>(exactSize, Thing::getExactSize(layer, xPattern, yPattern, zPattern, animationPhase));
    }

    return exactSize;
}

const ThingTypePtr& Creature::getThingType()
{
    return g_things.getThingType(m_outfit.getClothes().id, ThingCategoryCreature);
}

ThingType* Creature::rawGetThingType()
{
    return g_things.rawGetThingType(m_outfit.getClothes().id, ThingCategoryCreature);
}

ThingType* Creature::rawGetMountThingType()
{
    return g_things.rawGetThingType(m_outfit.getMountClothes().id, ThingCategoryCreature);
}
