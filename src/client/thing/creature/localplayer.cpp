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

#include <client/thing/creature/localplayer.h>
#include <framework/core/eventdispatcher.h>
#include <framework/graphics/graphics.h>
#include <client/game.h>
#include <client/map/map.h>
#include <client/map/tile.h>

void LocalPlayer::lockWalk(int millis)
{
    m_walkLockExpiration = std::max<int>(m_walkLockExpiration, g_clock.millis() + millis);
}

bool LocalPlayer::canWalk(Otc::Direction_t)
{
    // paralyzed
    if(isParalyzed())
        return false;

    // cannot walk while locked
    if(m_walkLockExpiration != 0 && g_clock.millis() < m_walkLockExpiration)
        return false;

    if(!isAutoWalking()) {
        if(m_forceWalk) m_forceWalk = false;
        else {
            const int stepDuration = std::max<int>(getStepDuration(), g_game.getPing());
            if(m_walkTimer.ticksElapsed() < stepDuration)
                return false;
        }
    }

    return true;
}

void LocalPlayer::walk(const Position& oldPos, const Position& newPos)
{
    // a prewalk was going on
    if(m_preWalking) {
        // switch to normal walking
        m_preWalking = false;
        // if is to the last prewalk destination, updates the walk preserving the animation
        if(newPos == m_lastPrewalkDestination) {
            updateWalk();
            // was to another direction, replace the walk
        } else
            Creature::walk(oldPos, newPos);
    }
    // no prewalk was going on, this must be an server side automated walk
    else {
        m_serverWalking = true;
        if(m_serverWalkEndEvent)
            m_serverWalkEndEvent->cancel();

        Creature::walk(oldPos, newPos);
    }
}

void LocalPlayer::preWalk(Otc::Direction_t direction)
{
    const Position newPos = m_position.translatedToDirection(direction);

    // avoid reanimating prewalks
    if(m_preWalking) {
        return;
    }

    m_preWalking = true;

    if(m_serverWalkEndEvent)
        m_serverWalkEndEvent->cancel();

    // start walking to direction
    m_lastPrewalkDestination = newPos;
    Creature::walk(m_position, newPos);
}

void LocalPlayer::cancelWalk(Otc::Direction_t direction)
{
    // only cancel client side walks
    if(m_walking && m_preWalking)
        stopWalk();

    lockWalk();

    if(m_autoWalkDestination.isValid()) {
        g_game.stop();

        if(m_autoWalkContinueEvent) {
            m_autoWalkContinueEvent->cancel();
        }

        auto self = asLocalPlayer();
        m_autoWalkContinueEvent = g_dispatcher.scheduleEvent([self]() {
            if(self->m_autoWalkDestination.isValid()) {
                self->autoWalk(self->m_autoWalkDestination);
            }
        }, 1000);
    }

    // turn to the cancel direction
    if(direction != Otc::InvalidDirection)
        setDirection(direction);

    callLuaField("onCancelWalk", direction);
}

bool LocalPlayer::autoWalk(const Position& destination)
{
    if(m_position == destination) return false;

    if(m_position.isInRange(destination, 1, 1))
        return g_game.walk(m_position.getDirectionFromPosition(destination));

    bool tryKnownPath = false;
    if(destination != m_autoWalkDestination) {
        m_knownCompletePath = false;
        tryKnownPath = true;
    }

    std::tuple<std::vector<Otc::Direction_t>, Otc::PathFindResult_t> result;
    std::vector<Otc::Direction_t> limitedPath;

    if(destination == m_position)
        return true;

    // try to find a path that we know
    if(tryKnownPath || m_knownCompletePath) {
        result = g_map.findPath(m_position, destination, 10000, 0);
        if(std::get<1>(result) == Otc::PathFindResultOk) {
            limitedPath = std::get<0>(result);
            // limit to 127 steps
            if(limitedPath.size() > 127)
                limitedPath.resize(127);

            m_knownCompletePath = true;
        }
    }

    // no known path found, try to discover one
    if(limitedPath.empty()) {
        result = g_map.findPath(m_position, destination, 10000, Otc::PathFindAllowNotSeenTiles);
        if(std::get<1>(result) != Otc::PathFindResultOk) {
            callLuaField("onAutoWalkFail", std::get<1>(result));
            stopAutoWalk();
            return false;
        }

        Position currentPos = m_position;
        for(auto dir : std::get<0>(result)) {
            currentPos = currentPos.translatedToDirection(dir);
            if(!hasSight(currentPos))
                break;
            limitedPath.push_back(dir);
        }
    }

    m_autoWalkDestination = destination;
    m_lastAutoWalkPosition = m_position.translatedToDirections(limitedPath).back();

    g_game.autoWalk(limitedPath);
    return true;
}

void LocalPlayer::stopAutoWalk()
{
    m_autoWalkDestination = Position();
    m_lastAutoWalkPosition = Position();
    m_knownCompletePath = false;

    if(m_autoWalkContinueEvent)
        m_autoWalkContinueEvent->cancel();
}

void LocalPlayer::stopWalk()
{
    Creature::stopWalk(); // will call terminateWalk
    m_lastPrewalkDestination = Position();
}

void LocalPlayer::updateWalk()
{
    Creature::updateWalk();
    g_map.notificateCameraMove(m_walkOffset);
}

void LocalPlayer::updateWalkOffset(int totalPixelsWalked)
{
    if(!m_preWalking) {
        Creature::updateWalkOffset(totalPixelsWalked);
        return;
    }

    // pre walks offsets are calculated in the oposite direction
    m_walkOffset = Point();
    if(m_direction == Otc::North || m_direction == Otc::NorthEast || m_direction == Otc::NorthWest)
        m_walkOffset.y = -totalPixelsWalked;
    else if(m_direction == Otc::South || m_direction == Otc::SouthEast || m_direction == Otc::SouthWest)
        m_walkOffset.y = totalPixelsWalked;

    if(m_direction == Otc::East || m_direction == Otc::NorthEast || m_direction == Otc::SouthEast)
        m_walkOffset.x = totalPixelsWalked;
    else if(m_direction == Otc::West || m_direction == Otc::NorthWest || m_direction == Otc::SouthWest)
        m_walkOffset.x = -totalPixelsWalked;
}

void LocalPlayer::terminateWalk()
{
    Creature::terminateWalk();

    m_preWalking = false;

    if(m_serverWalking) {
        if(m_serverWalkEndEvent)
            m_serverWalkEndEvent->cancel();

        const auto self = asLocalPlayer();
        m_serverWalkEndEvent = g_dispatcher.scheduleEvent([self] {
            self->m_serverWalking = false;
        }, 100);
    }
}

void LocalPlayer::onAppear()
{
    if(m_position != m_oldPosition) {
        g_map.notificateCameraMove(m_walkOffset);
    }

    Creature::onAppear();
}

void LocalPlayer::onPositionChange(const Position& newPos, const Position& oldPos)
{
    Creature::onPositionChange(newPos, oldPos);

    if(newPos == m_autoWalkDestination)
        stopAutoWalk();
    else if(m_autoWalkDestination.isValid() && newPos == m_lastAutoWalkPosition)
        autoWalk(m_autoWalkDestination);

    if(newPos.z != oldPos.z) {
        m_forceWalk = true;
    }
}

void LocalPlayer::setIcons(int icons)
{
    if(m_icons == icons) return;

    const int oldIcons = m_icons;
    m_icons = icons;

    callLuaField("onIconsChange", icons, oldIcons);
}

void LocalPlayer::setSkill(const Otc::skills_t skill, const uint8 level, const uint8 levelPercent)
{
    if(skill > Otc::skills_t::SKILL_LAST) {
        g_logger.traceError("invalid skill");
        return;
    }

    const Skill oldSkill = m_skills[skill];

    if(level != oldSkill.level || levelPercent != oldSkill.percent) {
        m_skills[skill].level;
        m_skills[skill].percent = levelPercent;

        callLuaField("onSkillChange", skill, level, levelPercent, oldSkill.level, oldSkill.percent);
    }
}

void LocalPlayer::setBaseSkill(Otc::skills_t skill, int baseLevel)
{
    if(skill > Otc::SKILL_LAST) {
        g_logger.traceError("invalid skill");
        return;
    }

    const int oldBaseLevel = m_skills[skill].baseLevel;
    if(baseLevel != oldBaseLevel) {
        m_skills[skill].baseLevel = baseLevel;

        callLuaField("onBaseSkillChange", skill, baseLevel, oldBaseLevel);
    }
}

void LocalPlayer::setHealth(double health, double maxHealth)
{
    if(m_health == health && m_maxHealth == maxHealth)
        return;

    const double oldHealth = m_health;
    const double oldMaxHealth = m_maxHealth;
    m_health = health;
    m_maxHealth = maxHealth;

    callLuaField("onHealthChange", health, maxHealth, oldHealth, oldMaxHealth);

    // cannot walk while dying
    if(health == 0) {
        if(isPreWalking())
            stopWalk();

        lockWalk();
    }
}

void LocalPlayer::setFreeCapacity(double freeCapacity)
{
    if(m_freeCapacity == freeCapacity) return;

    const double oldFreeCapacity = m_freeCapacity;
    m_freeCapacity = freeCapacity;

    callLuaField("onFreeCapacityChange", freeCapacity, oldFreeCapacity);
}

void LocalPlayer::setTotalCapacity(double totalCapacity)
{
    if(m_totalCapacity == totalCapacity) return;

    const double oldTotalCapacity = m_totalCapacity;
    m_totalCapacity = totalCapacity;

    callLuaField("onTotalCapacityChange", totalCapacity, oldTotalCapacity);
}

void LocalPlayer::setExperience(double experience)
{
    if(m_experience == experience)  return;

    const double oldExperience = m_experience;
    m_experience = experience;

    callLuaField("onExperienceChange", experience, oldExperience);
}

void LocalPlayer::setLevel(double level, double levelPercent)
{
    if(m_level == level && m_levelPercent == levelPercent)
        return;

    const double oldLevel = m_level;
    const double oldLevelPercent = m_levelPercent;
    m_level = level;
    m_levelPercent = levelPercent;

    callLuaField("onLevelChange", level, levelPercent, oldLevel, oldLevelPercent);
}

void LocalPlayer::setMana(double mana, double maxMana)
{
    if(m_mana == mana && m_maxMana == maxMana)
        return;

    const double oldMana = m_mana;
    double oldMaxMana;
    m_mana = mana;
    m_maxMana = maxMana;

    callLuaField("onManaChange", mana, maxMana, oldMana, oldMaxMana);
}

void LocalPlayer::setMagicLevel(double magicLevel, double magicLevelPercent)
{
    if(m_magicLevel == magicLevel && m_magicLevelPercent == magicLevelPercent)
        return;

    const double oldMagicLevel = m_magicLevel;
    const double oldMagicLevelPercent = m_magicLevelPercent;
    m_magicLevel = magicLevel;
    m_magicLevelPercent = magicLevelPercent;

    callLuaField("onMagicLevelChange", magicLevel, magicLevelPercent, oldMagicLevel, oldMagicLevelPercent);
}

void LocalPlayer::setBaseMagicLevel(double baseMagicLevel)
{
    if(m_baseMagicLevel == baseMagicLevel) return;

    const double oldBaseMagicLevel = m_baseMagicLevel;
    m_baseMagicLevel = baseMagicLevel;

    callLuaField("onBaseMagicLevelChange", baseMagicLevel, oldBaseMagicLevel);
}

void LocalPlayer::setSoul(double soul)
{
    if(m_soul == soul) return;

    const double oldSoul = m_soul;
    m_soul = soul;

    callLuaField("onSoulChange", soul, oldSoul);
}

void LocalPlayer::setStamina(double stamina)
{
    if(m_stamina == stamina) return;

    const double oldStamina = m_stamina;
    m_stamina = stamina;

    callLuaField("onStaminaChange", stamina, oldStamina);
}

void LocalPlayer::setInventoryItem(Otc::InventorySlot_t inventory, const ItemPtr& item)
{
    if(inventory >= Otc::LastInventorySlot) {
        g_logger.traceError("invalid slot");
        return;
    }

    if(m_inventoryItems[inventory] == item) return;

    const ItemPtr oldItem = m_inventoryItems[inventory];
    m_inventoryItems[inventory] = item;

    callLuaField("onInventoryChange", inventory, item, oldItem);
}

void LocalPlayer::setVocation(int vocation)
{
    if(m_vocation == vocation) return;

    const int oldVocation = m_vocation;
    m_vocation = vocation;

    callLuaField("onVocationChange", vocation, oldVocation);
}

void LocalPlayer::setPremium(bool premium, uint32 premiumExpiration)
{
    if(m_premium == premium && m_premiumExpiration == premiumExpiration) return;

    m_premium = premium;
    m_premiumExpiration = premiumExpiration;

    callLuaField("onPremiumChange", premium, m_premiumExpiration);
}

void LocalPlayer::setRegenerationTime(double regenerationTime)
{
    if(m_regenerationTime == regenerationTime) return;

    const double oldRegenerationTime = m_regenerationTime;
    m_regenerationTime = regenerationTime;

    callLuaField("onRegenerationChange", regenerationTime, oldRegenerationTime);
}

void LocalPlayer::setOfflineTrainingTime(double offlineTrainingTime)
{
    if(m_offlineTrainingTime == offlineTrainingTime) return;

    const double oldOfflineTrainingTime = m_offlineTrainingTime;
    m_offlineTrainingTime = offlineTrainingTime;

    callLuaField("onOfflineTrainingChange", offlineTrainingTime, oldOfflineTrainingTime);
}

void LocalPlayer::setSpells(const std::vector<uint8>& spells)
{
    if(m_spells == spells) return;

    const std::vector<uint8> oldSpells = m_spells;
    m_spells = spells;

    callLuaField("onSpellsChange", spells, oldSpells);
}

void LocalPlayer::setBlessings(int blessings)
{
    if(blessings == m_blessings) return;

    const int oldBlessings = m_blessings;
    m_blessings = blessings;

    callLuaField("onBlessingsChange", blessings, oldBlessings);
}

bool LocalPlayer::hasSight(const Position& pos)
{
    return m_position.isInRange(pos, g_map.getAwareRange().left - 1, g_map.getAwareRange().top - 1);
}
