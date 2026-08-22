#ifndef OPENMW_MWMECHANICS_EQUIPMENTREQUIREMENTS_H
#define OPENMW_MWMECHANICS_EQUIPMENTREQUIREMENTS_H

#include <string>

#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    struct EquipmentRequirementResult
    {
        bool mApplicable = false;
        bool mAllowed = true;
        int mTier = 1;
        int mSkill = -1;
        int mAttribute = -1;
        int mSkillValue = 0;
        int mAttributeValue = 0;
        int mSkillRequired = 0;
        int mAttributeRequired = 0;
        std::string mSkillName;
        std::string mAttributeName;
        std::string mItemName;
    };

    /// Native ArenaMW interpretation of Armor Requirements / Weapon Requirements.
    /// Only player equipment is gated by ActionEquip; this helper is also used by item tooltips.
    EquipmentRequirementResult getEquipmentRequirement(const MWWorld::ConstPtr& item,
        const MWWorld::Ptr& actor);

    std::string formatEquipmentRequirementMessage(const EquipmentRequirementResult& result);
    std::string formatEquipmentRequirementTooltip(const EquipmentRequirementResult& result);

    /// Re-check currently equipped player items. This mirrors the original
    /// mods' periodic validation when Drain/Damage/Fortify changes current stats.
    void enforceEquipmentRequirements(const MWWorld::Ptr& actor, bool showMessages = true);
}

#endif
