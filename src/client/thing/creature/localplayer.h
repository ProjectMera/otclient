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

#ifndef LOCALPLAYER_H
#define LOCALPLAYER_H

#include <client/thing/creature/player.h>

 // @bindclass
class LocalPlayer : public Player
{
    enum {
        PREWALK_TIMEOUT = 1000
    };

public:
    void unlockWalk() { m_walkLockExpiration = 0; }
    void lockWalk(int millis = 250);
    void stopAutoWalk();
    bool autoWalk(const Position& destination);
    bool canWalk(Otc::Direction_t direction);

    void setIcons(int icons);
    void setSkill(Otc::skills_t skill, uint8 level, uint8 levelPercent);
    void setBaseSkill(Otc::skills_t skill, int baseLevel);
    void setHealth(double health, double maxHealth);
    void setFreeCapacity(double freeCapacity);
    void setTotalCapacity(double totalCapacity);
    void setExperience(double experience);
    void setLevel(double level, double levelPercent);
    void setMana(double mana, double maxMana);
    void setMagicLevel(double magicLevel, double magicLevelPercent);
    void setBaseMagicLevel(double baseMagicLevel);
    void setSoul(double soul);
    void setStamina(double stamina);
    void setKnown(bool known) { m_known = known; }
    void setPendingGame(bool pending) { m_pending = pending; }
    void setInventoryItem(Otc::InventorySlot_t inventory, const ItemPtr& item);
    void setVocation(int vocation);
    void setPremium(bool premium, uint32 premiumExpiration);
    void setRegenerationTime(double regenerationTime);
    void setOfflineTrainingTime(double offlineTrainingTime);
    void setSpells(const std::vector<uint8>& spells);
    void setBlessings(int blessings);

    void setOpenPreyWindow(const bool can) { m_openPreyWindow = can; }
    bool canOpenPreyWindow() { return m_openPreyWindow; }

    void setMagicShield(const bool v) { m_magicShield = v; }
    bool hasMagicShield() { return m_magicShield; }

    int getIcons() { return m_icons; }
    uint16 getSkillLevel(const Otc::skills_t skill) { return m_skills[skill].level; }
    uint16 getSkillBaseLevel(const Otc::skills_t skill) { return m_skills[skill].baseLevel; }
    uint16 getSkillLevelPercent(const Otc::skills_t skill) { return m_skills[skill].percent; }
    int getVocation() { return m_vocation; }
    double getHealth() { return m_health; }
    double getMaxHealth() { return m_maxHealth; }
    double getFreeCapacity() { return m_freeCapacity; }
    double getTotalCapacity() { return m_totalCapacity; }
    double getExperience() { return m_experience; }
    double getLevel() { return m_level; }
    double getLevelPercent() { return m_levelPercent; }
    double getMana() { return m_mana; }
    double getMaxMana() { return m_maxMana; }
    double getMagicLevel() { return m_magicLevel; }
    double getMagicLevelPercent() { return m_magicLevelPercent; }
    double getBaseMagicLevel() { return m_baseMagicLevel; }
    double getSoul() { return m_soul; }
    double getStamina() { return m_stamina; }
    double getRegenerationTime() { return m_regenerationTime; }
    double getOfflineTrainingTime() { return m_offlineTrainingTime; }
    std::vector<uint8> getSpells() { return m_spells; }
    ItemPtr getInventoryItem(const Otc::InventorySlot_t inventory) { return m_inventoryItems[inventory]; }
    int getBlessings() { return m_blessings; }

    bool hasSight(const Position& pos);
    bool isKnown() { return m_known; }
    bool isPreWalking() { return m_preWalking; }
    bool isAutoWalking() { return m_autoWalkDestination.isValid(); }
    bool isServerWalking() { return m_serverWalking; }
    bool isPremium() { return m_premium; }
    bool isPendingGame() { return m_pending; }

    LocalPlayerPtr asLocalPlayer() { return static_self_cast<LocalPlayer>(); }
    bool isLocalPlayer() override { return true; }

    void onAppear() override;
    void onPositionChange(const Position& newPos, const Position& oldPos) override;

protected:
    void walk(const Position& oldPos, const Position& newPos) override;
    void preWalk(Otc::Direction_t direction);
    void cancelWalk(Otc::Direction_t direction = Otc::InvalidDirection);
    void stopWalk() override;
    void updateWalk() override;

    friend class Game;

protected:
    void updateWalkOffset(int totalPixelsWalked) override;
    void terminateWalk() override;

private:
    struct Skill {
        uint16_t level = 0,
            baseLevel = 0,
            percent = 0;
    };

    // walk related
    Position m_lastPrewalkDestination,
        m_autoWalkDestination,
        m_lastAutoWalkPosition;
    ScheduledEventPtr m_serverWalkEndEvent,
        m_autoWalkContinueEvent;

    ticks_t m_walkLockExpiration;

    bool m_preWalking{ false },
        m_serverWalking{ false },
        m_knownCompletePath{ false },
        m_premium{ false },
        m_known{ false },
        m_pending{ false },
        m_openPreyWindow{ false },
        m_magicShield{ false };

    uint32_t m_premiumExpiration;

    ItemPtr m_inventoryItems[Otc::LastInventorySlot];

    std::array<Skill, Otc::SKILL_LAST + 1> m_skills;
    std::vector<uint8> m_spells;

    int m_icons{ 0 },
        m_vocation{ 0 },
        m_blessings{ Otc::BlessingNone };

    double m_health{ -1 };
    double m_maxHealth{ -1 };
    double m_freeCapacity{ -1 };
    double m_totalCapacity{ -1 };
    double m_experience{ -1 };
    double m_level{ -1 };
    double m_levelPercent{ -1 };
    double m_mana{ -1 };
    double m_maxMana{ -1 };
    double m_magicLevel{ -1 };
    double m_magicLevelPercent{ -1 };
    double m_baseMagicLevel{ -1 };
    double m_soul{ -1 };
    double m_stamina{ -1 };
    double m_regenerationTime{ -1 };
    double m_offlineTrainingTime{ -1 };
};

#endif
