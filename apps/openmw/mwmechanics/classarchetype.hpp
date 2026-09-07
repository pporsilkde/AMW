#ifndef OPENMW_MWMECHANICS_CLASSARCHETYPE_H
#define OPENMW_MWMECHANICS_CLASSARCHETYPE_H

#include <string>
#include <vector>

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{
    class MagicEffects;

    namespace ClassArchetype
    {
        struct DisplayInfo
        {
            std::string id;
            std::string name;
            std::string perk;
            std::string drawback;
        };

        struct PassiveEffect
        {
            int effectId;
            float magnitude;
        };

        // Y043: compact runtime state for UI. The same values are derived from
        // the mechanics table, so the displayed power/state cannot drift away
        // from the actual archetype bonuses and drawbacks.
        struct RuntimeState
        {
            int attribute0 = -1;
            int attribute1 = -1;
            float power = 0.f;
            float armorLoad = 0.f;
            bool hasConditionalBuff = false;
            bool conditionalBuffActive = true;
            bool hasConditionalDrawback = false;
            bool conditionalDrawbackActive = true;
        };

        // There are exactly 28 unordered pairs for Morrowind's eight attributes.
        // A class archetype is derived from the two favourite attributes, so no
        // extra save or network state is required.
        bool getDisplayInfo(int attribute0, int attribute1, bool russian, DisplayInfo& out);
        bool getDisplayInfo(const MWWorld::Ptr& actor, bool russian, DisplayInfo& out);
        float getPairPower(const MWWorld::Ptr& actor);
        float getPairPower(const MWWorld::Ptr& actor, int attribute0, int attribute1);
        bool getRuntimeState(const MWWorld::Ptr& actor, bool sneaking, RuntimeState& out);

        float getHealthRegenFractionPerSecond(const MWWorld::Ptr& actor);
        float getMagickaRegenFractionPerSecond(const MWWorld::Ptr& actor);
        float getFatigueRegenMultiplier(const MWWorld::Ptr& actor);
        float getMovementSpeedMultiplier(const MWWorld::Ptr& actor);
        float getCarryCapacityMultiplier(const MWWorld::Ptr& actor);
        float getWeaponDamageMultiplier(const MWWorld::Ptr& actor);
        float getHitChanceBonus(const MWWorld::Ptr& actor);
        float getEvasionBonus(const MWWorld::Ptr& actor);
        float getSpellSuccessBonus(const MWWorld::Ptr& actor);
        float getIncomingDamageMultiplier(const MWWorld::Ptr& actor);
        float getBarterAdvantage(const MWWorld::Ptr& actor);
        float getDispositionBonus(const MWWorld::Ptr& actor);

        // Y037 signature mechanics. These are derived from the existing class
        // attribute pair and current stats, so no save/network fields are added.
        void getPassiveMagicEffects(const MWWorld::Ptr& actor, bool sneaking, std::vector<PassiveEffect>& out);
        void addPassiveMagicEffects(const MWWorld::Ptr& actor, bool sneaking, MagicEffects& effects);
        float getSneakChameleonMagnitude(const MWWorld::Ptr& actor);
        float getSpellAbsorptionChance(const MWWorld::Ptr& actor);
        bool getWeaponElementalBonus(const MWWorld::Ptr& actor, float physicalDamage, int& effectId, float& rawDamage);
    }
}

#endif
