#ifndef GAME_MWMECHANICS_XPLEVELING_H
#define GAME_MWMECHANICS_XPLEVELING_H

#include <string>

namespace ESM
{
    struct Book;
    struct Class;
}

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{
    namespace XPLeveling
    {
        bool isEnabled();

        float getXpForNextLevel(const MWWorld::Ptr& player);
        int getSkillPointCost(float skillBase);

        // Converts a normal OpenMW skill-use event into character XP. The
        // original skill itself is not advanced while native XP leveling is on.
        void awardSkillUse(const MWWorld::Ptr& player, int skillId, int usageType,
            float extraFactor, const ESM::Class& class_);

        // Discrete, data-driven reward sources.
        void awardKill(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker);
        void awardQuestProgress(const std::string& questId, int journalIndex, bool completed);
        void awardTravel(const MWWorld::Ptr& player);
        void awardSuccessfulTrade(const MWWorld::Ptr& player);
        void awardCriticalHit(const MWWorld::Ptr& player, const MWWorld::Ptr& victim);
        void awardSuccessfulTheft(const MWWorld::Ptr& player, const MWWorld::Ptr& item, int count);
        void awardBookRead(const MWWorld::Ptr& player, const ESM::Book& book);

        // Arena Y012 penalties affect only unbanked XP in the current level.
        // Earned levels, Skill Points and skills are never reduced.
        void applyDeathPenalty(const MWWorld::Ptr& player);
        void applyJailPenalty(const MWWorld::Ptr& player);

        // If XP is already zero at death, add level * configured seconds to the
        // ordinary respawn delay. A non-zero XP pool is wiped instead, with no
        // additional level cooldown.
        float getDeathRespawnDelay(const MWWorld::Ptr& player, float baseDelay);

        // Statistics-window allocation. Returns true only when a skill was
        // actually increased and its point cost was paid.
        bool spendSkillPoints(const MWWorld::Ptr& player, int skillId);
    }
}

#endif
