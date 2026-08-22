#include "equipmentrequirements.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <typeinfo>
#include <vector>

#include <MyGUI_LanguageManager.h>

#include <components/esm/loadarmo.hpp>
#include <components/esm/loadweap.hpp>
#include <components/esm/loadskil.hpp>
#include <components/esm/attr.hpp>
#include <components/misc/stringops.hpp>
#include <components/settings/settings.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "npcstats.hpp"
#include "creaturestats.hpp"

namespace
{
    bool getBool(const char* key, bool fallback)
    {
        try { return Settings::Manager::getBool(key, "Equipment Requirements"); }
        catch (...) { return fallback; }
    }

    int getInt(const char* key, int fallback)
    {
        try { return Settings::Manager::getInt(key, "Equipment Requirements"); }
        catch (...) { return fallback; }
    }

    float getFloat(const char* key, float fallback)
    {
        try { return Settings::Manager::getFloat(key, "Equipment Requirements"); }
        catch (...) { return fallback; }
    }

    bool startsWithCi(const std::string& value, const std::string& prefix)
    {
        const std::string lhs = Misc::StringUtils::lowerCaseUtf8(value);
        const std::string rhs = Misc::StringUtils::lowerCaseUtf8(prefix);
        return lhs.compare(0, rhs.size(), rhs) == 0;
    }

    std::string tr(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    std::string skillName(int skill)
    {
        switch (skill)
        {
            case ESM::Skill::HeavyArmor: return tr("requirements.skill.heavyarmor");
            case ESM::Skill::MediumArmor: return tr("requirements.skill.mediumarmor");
            case ESM::Skill::LightArmor: return tr("requirements.skill.lightarmor");
            case ESM::Skill::Axe: return tr("requirements.skill.axe");
            case ESM::Skill::BluntWeapon: return tr("requirements.skill.bluntweapon");
            case ESM::Skill::LongBlade: return tr("requirements.skill.longblade");
            case ESM::Skill::ShortBlade: return tr("requirements.skill.shortblade");
            case ESM::Skill::Spear: return tr("requirements.skill.spear");
            case ESM::Skill::Marksman: return tr("requirements.skill.marksman");
            default: return tr("requirements.skill.unknown");
        }
    }

    std::string attrName(int attr)
    {
        switch (attr)
        {
            case ESM::Attribute::Strength: return tr("requirements.attr.strength");
            case ESM::Attribute::Agility: return tr("requirements.attr.agility");
            case ESM::Attribute::Speed: return tr("requirements.attr.speed");
            case ESM::Attribute::Endurance: return tr("requirements.attr.endurance");
            default: return tr("requirements.attr.unknown");
        }
    }

    int valueForSkill(const MWWorld::Ptr& actor, int skill, bool modified)
    {
        const MWMechanics::NpcStats& stats = actor.getClass().getNpcStats(actor);
        return modified ? static_cast<int>(stats.getSkill(skill).getModified())
                        : static_cast<int>(stats.getSkill(skill).getBase());
    }

    int valueForAttribute(const MWWorld::Ptr& actor, int attr, bool modified)
    {
        const MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
        return modified ? static_cast<int>(stats.getAttribute(attr).getModified())
                        : static_cast<int>(stats.getAttribute(attr).getBase());
    }

    int tierFromThresholds(float value, int t2, int t3, int t4)
    {
        if (value >= t4) return 4;
        if (value >= t3) return 3;
        if (value >= t2) return 2;
        return 1;
    }

    void fillActorValues(MWMechanics::EquipmentRequirementResult& out, const MWWorld::Ptr& actor)
    {
        const bool modified = getBool("use modified stats", true);
        out.mSkillValue = valueForSkill(actor, out.mSkill, modified);
        out.mAttributeValue = valueForAttribute(actor, out.mAttribute, modified);
        out.mSkillName = skillName(out.mSkill);
        out.mAttributeName = attrName(out.mAttribute);
        out.mAllowed = out.mSkillValue >= out.mSkillRequired
            && out.mAttributeValue >= out.mAttributeRequired;
    }

    float clamp01(float value)
    {
        return std::clamp(value, 0.f, 1.f);
    }

    float exponentialScore(float value, float scale)
    {
        if (value <= 0.f)
            return 0.f;
        return clamp01(1.f - std::exp(-value / std::max(scale, 0.001f)));
    }

    float logarithmicScore(float value, float reference)
    {
        if (value <= 0.f)
            return 0.f;
        const float safeReference = std::max(reference, 1.f);
        return clamp01(std::log1p(value) / std::log1p(safeReference));
    }

    float weightedScore(const std::array<float, 6>& values, const std::array<float, 6>& weights)
    {
        float total = 0.f;
        float weightTotal = 0.f;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            const float weight = std::max(weights[i], 0.f);
            total += clamp01(values[i]) * weight;
            weightTotal += weight;
        }
        return weightTotal > 0.0001f ? clamp01(total / weightTotal) : 0.f;
    }

    int quantizeRequirement(float value)
    {
        const int step = std::clamp(getInt("automatic requirement step", 5), 1, 20);
        const int rounded = static_cast<int>(std::floor(value / static_cast<float>(step) + 0.5f)) * step;
        return std::clamp(rounded, 0, 100);
    }

    int requirementFromScore(float score, bool attribute)
    {
        const char* minKey = attribute ? "automatic minimum attribute" : "automatic minimum skill";
        const char* maxKey = attribute ? "automatic maximum attribute" : "automatic maximum skill";
        const int fallbackMin = attribute ? 10 : 10;
        const int fallbackMax = attribute ? 80 : 90;
        const int minimum = std::clamp(getInt(minKey, fallbackMin), 0, 100);
        const int maximum = std::clamp(getInt(maxKey, fallbackMax), minimum, 100);
        return quantizeRequirement(static_cast<float>(minimum) + clamp01(score) * (maximum - minimum));
    }

    int tierFromRequirements(int skill, int attribute)
    {
        const int requirement = std::max(skill, attribute);
        if (requirement <= 20) return 1;
        if (requirement <= 35) return 2;
        if (requirement <= 50) return 3;
        if (requirement <= 65) return 4;
        if (requirement <= 80) return 5;
        return 6;
    }

    float armorSlotImportance(int type)
    {
        using A = ESM::Armor;
        switch (type)
        {
            case A::Cuirass: return 1.00f;
            case A::Shield: return 0.85f;
            case A::Greaves: return 0.75f;
            case A::Helmet: return 0.55f;
            case A::Boots: return 0.55f;
            case A::LPauldron:
            case A::RPauldron: return 0.42f;
            case A::LGauntlet:
            case A::RGauntlet:
            case A::LBracer:
            case A::RBracer: return 0.32f;
            default: return 0.50f;
        }
    }

    float armorClassWeightReference(int skill)
    {
        if (skill == ESM::Skill::LightArmor)
            return getFloat("automatic light armor weight reference", 8.f);
        if (skill == ESM::Skill::MediumArmor)
            return getFloat("automatic medium armor weight reference", 18.f);
        return getFloat("automatic heavy armor weight reference", 38.f);
    }

    MWMechanics::EquipmentRequirementResult automaticArmorRequirement(const MWWorld::ConstPtr& item,
        const MWWorld::Ptr& actor)
    {
        MWMechanics::EquipmentRequirementResult out;
        const auto* ref = item.get<ESM::Armor>();
        if (!ref || !ref->mBase || !getBool("armor enabled", true))
            return out;

        const std::string id = Misc::StringUtils::lowerCase(item.getCellRef().getRefId());
        if (id == "wraithguard" || id == "wraithguard_jury_rig")
            return out;

        const bool bound = startsWithCi(id, "bound_") || startsWithCi(ref->mBase->mName, "bound ");
        if (bound && !getBool("bound armor requirements", true))
            return out;

        const int skill = item.getClass().getEquipmentSkill(item);
        int attribute = -1;
        if (skill == ESM::Skill::HeavyArmor)
        {
            if (!getBool("heavy armor enabled", true)) return out;
            attribute = ESM::Attribute::Endurance;
        }
        else if (skill == ESM::Skill::MediumArmor)
        {
            if (!getBool("medium armor enabled", true)) return out;
            attribute = ESM::Attribute::Endurance;
        }
        else if (skill == ESM::Skill::LightArmor)
        {
            if (!getBool("light armor enabled", true)) return out;
            attribute = ESM::Attribute::Agility;
        }
        else
            return out;

        // Requirements are intentionally based on the record's undamaged/base
        // statistics. A worn cuirass must not become easier to equip and then
        // suddenly become harder again after repair. "Durability" therefore
        // means the item's maximum condition, not its current remaining health.
        const float protection = exponentialScore(static_cast<float>(std::max(ref->mBase->mData.mArmor, 0)),
            getFloat("automatic armor protection scale", 55.f));
        const float durability = logarithmicScore(static_cast<float>(std::max(ref->mBase->mData.mHealth, 0)),
            getFloat("automatic armor durability reference", 6000.f));
        const float value = logarithmicScore(static_cast<float>(std::max(ref->mBase->mData.mValue, 0)),
            getFloat("automatic armor value reference", 100000.f));
        const float slot = armorSlotImportance(ref->mBase->mData.mType);
        const float slotWeightScale = std::max(0.20f, slot);
        const float weight = exponentialScore(std::max(ref->mBase->mData.mWeight, 0.f),
            armorClassWeightReference(skill) * slotWeightScale);

        const std::array<float, 6> components = { protection, durability, value, weight, slot, 0.f };
        const std::array<float, 6> weights = {
            getFloat("automatic armor protection influence", 0.58f),
            getFloat("automatic armor durability influence", 0.17f),
            getFloat("automatic armor value influence", 0.12f),
            getFloat("automatic armor weight influence", 0.13f),
            0.f,
            0.f
        };
        const float power = weightedScore(components, weights);

        const std::array<float, 6> burdenComponents = { power, weight, slot, 0.f, 0.f, 0.f };
        const std::array<float, 6> burdenWeights = {
            getFloat("automatic armor attribute power influence", 0.28f),
            getFloat("automatic armor attribute weight influence", 0.60f),
            getFloat("automatic armor attribute slot influence", 0.12f),
            0.f, 0.f, 0.f
        };
        const float burden = weightedScore(burdenComponents, burdenWeights);

        const float skillCurve = std::max(getFloat("automatic skill curve", 1.35f), 0.1f);
        const float attributeCurve = std::max(getFloat("automatic attribute curve", 1.50f), 0.1f);

        out.mSkillRequired = requirementFromScore(std::pow(power, skillCurve), false);
        out.mAttributeRequired = requirementFromScore(std::pow(burden, attributeCurve), true);
        out.mTier = tierFromRequirements(out.mSkillRequired, out.mAttributeRequired);
        out.mRating = static_cast<int>(std::floor(power * 100.f + 0.5f));
        out.mApplicable = true;
        out.mAutomatic = true;
        out.mSkill = skill;
        out.mAttribute = attribute;
        out.mItemName = ref->mBase->mName.empty() ? item.getCellRef().getRefId() : ref->mBase->mName;
        fillActorValues(out, actor);
        return out;
    }

    MWMechanics::EquipmentRequirementResult legacyArmorRequirement(const MWWorld::ConstPtr& item,
        const MWWorld::Ptr& actor)
    {
        MWMechanics::EquipmentRequirementResult out;
        const auto* ref = item.get<ESM::Armor>();
        if (!ref || !ref->mBase || !getBool("armor enabled", true))
            return out;

        const std::string id = Misc::StringUtils::lowerCase(item.getCellRef().getRefId());
        if (id == "wraithguard" || id == "wraithguard_jury_rig")
            return out;

        const bool bound = startsWithCi(id, "bound_")
            || startsWithCi(ref->mBase->mName, "bound ");
        if (bound && !getBool("bound armor requirements", true))
            return out;

        const int skill = item.getClass().getEquipmentSkill(item);
        int attr = -1;
        const char* group = nullptr;
        if (skill == ESM::Skill::HeavyArmor)
        {
            if (!getBool("heavy armor enabled", true)) return out;
            attr = ESM::Attribute::Endurance; group = "heavy";
        }
        else if (skill == ESM::Skill::MediumArmor)
        {
            if (!getBool("medium armor enabled", true)) return out;
            attr = ESM::Attribute::Endurance; group = "medium";
        }
        else if (skill == ESM::Skill::LightArmor)
        {
            if (!getBool("light armor enabled", true)) return out;
            attr = ESM::Attribute::Agility; group = "light";
        }
        else
            return out;

        int t2 = 0, t3 = 0, t4 = 0;
        if (std::string(group) == "heavy") { t2 = getInt("heavy tier 2 armor", 16); t3 = getInt("heavy tier 3 armor", 59); t4 = getInt("heavy tier 4 armor", 65); }
        if (std::string(group) == "medium") { t2 = getInt("medium tier 2 armor", 15); t3 = getInt("medium tier 3 armor", 39); t4 = getInt("medium tier 4 armor", 44); }
        if (std::string(group) == "light") { t2 = getInt("light tier 2 armor", 8); t3 = getInt("light tier 3 armor", 19); t4 = getInt("light tier 4 armor", 44); }

        out.mTier = tierFromThresholds(static_cast<float>(ref->mBase->mData.mArmor), t2, t3, t4);
        if (out.mTier <= 1)
            return out;

        const std::string prefix(group);
        out.mSkillRequired = getInt((prefix + " tier " + std::to_string(out.mTier) + " skill").c_str(),
            out.mTier == 2 ? 30 : out.mTier == 3 ? 60 : 80);
        out.mAttributeRequired = getInt((prefix + " tier " + std::to_string(out.mTier) + " attribute").c_str(),
            out.mTier == 2 ? 30 : out.mTier == 3 ? 60 : 80);
        out.mApplicable = true;
        out.mSkill = skill;
        out.mAttribute = attr;
        out.mItemName = ref->mBase->mName.empty() ? item.getCellRef().getRefId() : ref->mBase->mName;
        fillActorValues(out, actor);
        return out;
    }

    struct WeaponRule
    {
        int skill;
        int attr;
        enum Damage { Chop, Slash, Thrust, Max } damage;
        const char* group;
        int d2, d3, d4;
    };

    bool weaponRule(int type, WeaponRule& r)
    {
        using W = ESM::Weapon;
        switch (type)
        {
            case W::AxeOneHand: r={ESM::Skill::Axe,ESM::Attribute::Strength,WeaponRule::Chop,"axe 1h",10,17,19}; return true;
            case W::AxeTwoHand: r={ESM::Skill::Axe,ESM::Attribute::Strength,WeaponRule::Chop,"axe 2h",17,22,38}; return true;
            case W::BluntOneHand: r={ESM::Skill::BluntWeapon,ESM::Attribute::Strength,WeaponRule::Chop,"mace",5,10,999}; return true;
            case W::BluntTwoClose: r={ESM::Skill::BluntWeapon,ESM::Attribute::Strength,WeaponRule::Chop,"hammer",15,22,999}; return true;
            case W::BluntTwoWide: r={ESM::Skill::BluntWeapon,ESM::Attribute::Strength,WeaponRule::Slash,"staff",6,8,999}; return true;
            case W::LongBladeOneHand: r={ESM::Skill::LongBlade,ESM::Attribute::Strength,WeaponRule::Max,"long blade 1h",10,16,999}; return true;
            case W::LongBladeTwoHand: r={ESM::Skill::LongBlade,ESM::Attribute::Strength,WeaponRule::Max,"long blade 2h",13,20,30}; return true;
            case W::ShortBladeOneHand: r={ESM::Skill::ShortBlade,ESM::Attribute::Speed,WeaponRule::Max,"short blade",6,12,999}; return true;
            case W::SpearTwoWide: r={ESM::Skill::Spear,ESM::Attribute::Endurance,WeaponRule::Thrust,"spear",13,18,999}; return true;
            case W::MarksmanBow: r={ESM::Skill::Marksman,ESM::Attribute::Agility,WeaponRule::Chop,"bow",10,17,24}; return true;
            case W::MarksmanCrossbow: r={ESM::Skill::Marksman,ESM::Attribute::Agility,WeaponRule::Chop,"crossbow",15,28,37}; return true;
            case W::MarksmanThrown: r={ESM::Skill::Marksman,ESM::Attribute::Agility,WeaponRule::Chop,"thrown",3,5,999}; return true;
            default: return false;
        }
    }

    float avgPair(const unsigned char pair[2])
    {
        return (static_cast<float>(pair[0]) + static_cast<float>(pair[1])) * 0.5f;
    }

    float weaponHandlingWeightReference(int type)
    {
        using W = ESM::Weapon;
        switch (type)
        {
            case W::ShortBladeOneHand: return 8.f;
            case W::LongBladeOneHand: return 20.f;
            case W::LongBladeTwoHand: return 35.f;
            case W::BluntOneHand: return 25.f;
            case W::BluntTwoClose: return 45.f;
            case W::BluntTwoWide: return 15.f;
            case W::SpearTwoWide: return 15.f;
            case W::AxeOneHand: return 25.f;
            case W::AxeTwoHand: return 40.f;
            case W::MarksmanBow: return 12.f;
            case W::MarksmanCrossbow: return 20.f;
            case W::MarksmanThrown: return 3.f;
            default: return 20.f;
        }
    }

    MWMechanics::EquipmentRequirementResult automaticWeaponRequirement(const MWWorld::ConstPtr& item,
        const MWWorld::Ptr& actor)
    {
        MWMechanics::EquipmentRequirementResult out;
        const auto* ref = item.get<ESM::Weapon>();
        if (!ref || !ref->mBase || !getBool("weapon enabled", true))
            return out;

        const std::string id = Misc::StringUtils::lowerCase(item.getCellRef().getRefId());
        if (id == "sunder" || id == "keening")
            return out;
        if (ref->mBase->mData.mType == ESM::Weapon::Arrow || ref->mBase->mData.mType == ESM::Weapon::Bolt)
            return out;

        const bool bound = startsWithCi(id, "bound_") || startsWithCi(ref->mBase->mName, "bound ");
        if (bound && !getBool("bound weapon requirements", true))
            return out;

        WeaponRule rule;
        if (!weaponRule(ref->mBase->mData.mType, rule))
            return out;

        const float chop = avgPair(ref->mBase->mData.mChop);
        const float slash = avgPair(ref->mBase->mData.mSlash);
        const float thrust = avgPair(ref->mBase->mData.mThrust);
        const float maxAttack = std::max({ chop, slash, thrust });
        const float meanAttack = (chop + slash + thrust) / 3.f;
        const float representativeDamage = maxAttack * 0.72f + meanAttack * 0.28f;
        const float speed = std::max(ref->mBase->mData.mSpeed, 0.05f);

        const float damage = exponentialScore(representativeDamage,
            getFloat("automatic weapon damage scale", 28.f));
        const float dps = exponentialScore(representativeDamage * speed,
            getFloat("automatic weapon dps scale", 35.f));
        const float reach = clamp01((ref->mBase->mData.mReach - 0.65f)
            / std::max(getFloat("automatic weapon reach range", 1.25f), 0.01f));
        const float durability = logarithmicScore(static_cast<float>(ref->mBase->mData.mHealth),
            getFloat("automatic weapon durability reference", 6000.f));
        const float value = logarithmicScore(static_cast<float>(std::max(ref->mBase->mData.mValue, 0)),
            getFloat("automatic weapon value reference", 100000.f));
        const float weight = exponentialScore(std::max(ref->mBase->mData.mWeight, 0.f),
            weaponHandlingWeightReference(ref->mBase->mData.mType)
                * std::max(getFloat("automatic weapon weight scale", 1.f), 0.05f));

        const std::array<float, 6> components = { damage, dps, reach, durability, value, weight };
        const std::array<float, 6> weights = {
            getFloat("automatic weapon damage influence", 0.42f),
            getFloat("automatic weapon dps influence", 0.18f),
            getFloat("automatic weapon reach influence", 0.10f),
            getFloat("automatic weapon durability influence", 0.12f),
            getFloat("automatic weapon value influence", 0.10f),
            getFloat("automatic weapon weight influence", 0.08f)
        };
        const float power = weightedScore(components, weights);

        const std::array<float, 6> burdenComponents = { power, weight, reach, 0.f, 0.f, 0.f };
        const std::array<float, 6> burdenWeights = {
            getFloat("automatic weapon attribute power influence", 0.45f),
            getFloat("automatic weapon attribute weight influence", 0.45f),
            getFloat("automatic weapon attribute reach influence", 0.10f),
            0.f, 0.f, 0.f
        };
        const float burden = weightedScore(burdenComponents, burdenWeights);

        const float skillCurve = std::max(getFloat("automatic skill curve", 1.35f), 0.1f);
        const float attributeCurve = std::max(getFloat("automatic attribute curve", 1.50f), 0.1f);

        out.mSkillRequired = requirementFromScore(std::pow(power, skillCurve), false);
        out.mAttributeRequired = requirementFromScore(std::pow(burden, attributeCurve), true);
        out.mTier = tierFromRequirements(out.mSkillRequired, out.mAttributeRequired);
        out.mRating = static_cast<int>(std::floor(power * 100.f + 0.5f));
        out.mApplicable = true;
        out.mAutomatic = true;
        out.mSkill = rule.skill;
        out.mAttribute = rule.attr;
        out.mItemName = ref->mBase->mName.empty() ? item.getCellRef().getRefId() : ref->mBase->mName;
        fillActorValues(out, actor);
        return out;
    }

    MWMechanics::EquipmentRequirementResult legacyWeaponRequirement(const MWWorld::ConstPtr& item,
        const MWWorld::Ptr& actor)
    {
        MWMechanics::EquipmentRequirementResult out;
        const auto* ref = item.get<ESM::Weapon>();
        if (!ref || !ref->mBase || !getBool("weapon enabled", true))
            return out;

        const std::string id = Misc::StringUtils::lowerCase(item.getCellRef().getRefId());
        if (id == "sunder" || id == "keening")
            return out;
        if (ref->mBase->mData.mType == ESM::Weapon::Arrow || ref->mBase->mData.mType == ESM::Weapon::Bolt)
            return out;

        const bool bound = startsWithCi(id, "bound_") || startsWithCi(ref->mBase->mName, "bound ");
        if (bound && !getBool("bound weapon requirements", true))
            return out;

        WeaponRule rule;
        if (!weaponRule(ref->mBase->mData.mType, rule))
            return out;

        const std::string prefix(rule.group);
        const int d2 = getInt((prefix + " tier 2 damage").c_str(), rule.d2);
        const int d3 = getInt((prefix + " tier 3 damage").c_str(), rule.d3);
        const int d4 = getInt((prefix + " tier 4 damage").c_str(), rule.d4);
        float damage = 0.f;
        switch (rule.damage)
        {
            case WeaponRule::Chop: damage = avgPair(ref->mBase->mData.mChop); break;
            case WeaponRule::Slash: damage = avgPair(ref->mBase->mData.mSlash); break;
            case WeaponRule::Thrust: damage = avgPair(ref->mBase->mData.mThrust); break;
            case WeaponRule::Max:
                damage = std::max({avgPair(ref->mBase->mData.mChop), avgPair(ref->mBase->mData.mSlash), avgPair(ref->mBase->mData.mThrust)});
                break;
        }
        out.mTier = tierFromThresholds(damage, d2, d3, d4);
        out.mSkillRequired = getInt(("weapon tier " + std::to_string(out.mTier) + " skill").c_str(),
            out.mTier == 1 ? 0 : out.mTier == 2 ? 30 : out.mTier == 3 ? 60 : 80);
        out.mAttributeRequired = getInt(("weapon tier " + std::to_string(out.mTier) + " attribute").c_str(),
            out.mTier == 1 ? 0 : out.mTier == 2 ? 30 : out.mTier == 3 ? 60 : 80);
        if (out.mTier <= 1 || (out.mSkillRequired <= 0 && out.mAttributeRequired <= 0))
            return out;

        out.mApplicable = true;
        out.mSkill = rule.skill;
        out.mAttribute = rule.attr;
        out.mItemName = ref->mBase->mName.empty() ? item.getCellRef().getRefId() : ref->mBase->mName;
        fillActorValues(out, actor);
        return out;
    }

    MWMechanics::EquipmentRequirementResult armorRequirement(const MWWorld::ConstPtr& item,
        const MWWorld::Ptr& actor)
    {
        if (getBool("automatic calculation", true))
            return automaticArmorRequirement(item, actor);
        return legacyArmorRequirement(item, actor);
    }

    MWMechanics::EquipmentRequirementResult weaponRequirement(const MWWorld::ConstPtr& item,
        const MWWorld::Ptr& actor)
    {
        if (getBool("automatic calculation", true))
            return automaticWeaponRequirement(item, actor);
        return legacyWeaponRequirement(item, actor);
    }
}

namespace MWMechanics
{
    EquipmentRequirementResult getEquipmentRequirement(const MWWorld::ConstPtr& item,
        const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty() || item.isEmpty() || !getBool("enabled", true))
            return {};
        if (item.getTypeName() == typeid(ESM::Armor).name())
            return armorRequirement(item, actor);
        if (item.getTypeName() == typeid(ESM::Weapon).name())
            return weaponRequirement(item, actor);
        return {};
    }

    std::string formatEquipmentRequirementMessage(const EquipmentRequirementResult& r)
    {
        if (!r.mApplicable || r.mAllowed)
            return {};
        std::string msg = "\"" + r.mItemName + "\" " + tr("requirements.requires") + ": ";
        bool comma = false;
        if (r.mSkillValue < r.mSkillRequired)
        {
            msg += r.mSkillName + " " + std::to_string(r.mSkillValue) + "/" + std::to_string(r.mSkillRequired);
            comma = true;
        }
        if (r.mAttributeValue < r.mAttributeRequired)
        {
            if (comma) msg += ", ";
            msg += r.mAttributeName + " " + std::to_string(r.mAttributeValue) + "/" + std::to_string(r.mAttributeRequired);
        }
        return msg;
    }

    std::string formatEquipmentRequirementTooltip(const EquipmentRequirementResult& r)
    {
        if (!r.mApplicable || !getBool("tooltip enabled", true))
            return {};
        std::string text = "\n\n#{fontcolourhtml=header}" + tr("requirements.title")
            + " #{fontcolourhtml=normal}" + tr("requirements.tier") + " " + std::to_string(r.mTier);
        if (r.mAutomatic && r.mRating >= 0)
            text += "  " + tr("requirements.rating") + " " + std::to_string(r.mRating) + "/100";
        const char* ok = "#{fontcolourhtml=normal}";
        const char* bad = "#{fontcolourhtml=negative}";
        text += "\n" + std::string(r.mSkillValue >= r.mSkillRequired ? ok : bad)
            + r.mSkillName + " " + std::to_string(r.mSkillValue) + " / " + std::to_string(r.mSkillRequired);
        text += "\n" + std::string(r.mAttributeValue >= r.mAttributeRequired ? ok : bad)
            + r.mAttributeName + " " + std::to_string(r.mAttributeValue) + " / " + std::to_string(r.mAttributeRequired);
        return text;
    }
    void enforceEquipmentRequirements(const MWWorld::Ptr& actor, bool showMessages)
    {
        if (actor.isEmpty() || !actor.getClass().hasInventoryStore(actor) || !getBool("enabled", true))
            return;

        MWWorld::InventoryStore& inventory = actor.getClass().getInventoryStore(actor);
        // Copy the objects first. Unequipping mutates slot iterators and may also
        // restack inventory entries, so never keep slot iterators across removals.
        std::vector<MWWorld::Ptr> invalid;
        for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
        {
            MWWorld::ContainerStoreIterator equipped = inventory.getSlot(slot);
            if (equipped == inventory.end())
                continue;
            MWWorld::Ptr item = *equipped;
            const EquipmentRequirementResult requirement = getEquipmentRequirement(item, actor);
            if (requirement.mApplicable && !requirement.mAllowed)
                invalid.push_back(item);
        }

        // The same stack can occupy more than one legal slot in a few edge cases.
        // De-duplicate by Ptr equality before unequipping.
        for (std::size_t i = 0; i < invalid.size(); ++i)
        {
            bool duplicate = false;
            for (std::size_t j = 0; j < i; ++j)
                if (invalid[i] == invalid[j]) { duplicate = true; break; }
            if (duplicate || !inventory.isEquipped(invalid[i]))
                continue;

            const EquipmentRequirementResult requirement = getEquipmentRequirement(invalid[i], actor);
            if (!requirement.mApplicable || requirement.mAllowed)
                continue;
            if (showMessages)
                MWBase::Environment::get().getWindowManager()->messageBox(
                    formatEquipmentRequirementMessage(requirement));
            inventory.unequipItem(invalid[i], actor);
        }
    }

}
