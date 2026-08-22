#include "poison.hpp"

#include <algorithm>

#include <components/esm/loadalch.hpp>
#include <components/esm/loadmgef.hpp>
#include <components/esm/loadskil.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"

#include "spellcasting.hpp"
#include "npcstats.hpp"

namespace MWMechanics
{
    bool isPurePoison(const MWWorld::ConstPtr& potion)
    {
        if (potion.isEmpty() || potion.getTypeName() != typeid(ESM::Potion).name())
            return false;
        const auto* ref = potion.get<ESM::Potion>();
        if (!ref || !ref->mBase || ref->mBase->mEffects.mList.empty())
            return false;

        bool harmful = false;
        for (const ESM::ENAMstruct& effect : ref->mBase->mEffects.mList)
        {
            const ESM::MagicEffect* magic = MWBase::Environment::get().getWorld()->getStore()
                .get<ESM::MagicEffect>().find(effect.mEffectID);
            if ((magic->mData.mFlags & ESM::MagicEffect::Harmful) == 0)
                return false;
            harmful = true;
        }
        return harmful;
    }

    int poisonChargesFor(const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty())
            return 1;
        const int maxCharges = std::max(1, Settings::Manager::getInt("max poison charges", "ArenaMW Alchemy"));
        const float skill = actor.getClass().getNpcStats(actor).getSkill(ESM::Skill::Alchemy).getBase();
        return std::min(maxCharges, 1 + static_cast<int>(std::max(0.f, skill) / 20.f));
    }

    bool coatEquippedWeapon(const MWWorld::Ptr& actor, const MWWorld::Ptr& potion, std::string* message)
    {
        if (actor.isEmpty() || potion.isEmpty() || !isPurePoison(potion) || !actor.getClass().hasInventoryStore(actor))
            return false;

        MWWorld::InventoryStore& inventory = actor.getClass().getInventoryStore(actor);
        MWWorld::ContainerStoreIterator weaponIt = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weaponIt == inventory.end() || weaponIt->getTypeName() != typeid(ESM::Weapon).name())
            return false;

        MWWorld::Ptr weapon = *weaponIt;
        const int charges = poisonChargesFor(actor);
        weapon.getRefData().setPoison(potion.getCellRef().getRefId(), charges);

        if (message)
        {
            const auto* potionRef = potion.get<ESM::Potion>();
            *message = potionRef->mBase->mName + " -> " + weapon.getClass().getName(weapon)
                + " (" + std::to_string(charges) + ")";
        }
        return true;
    }

    bool applyWeaponPoison(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& weapon,
        const osg::Vec3f& hitPosition, bool fromProjectile)
    {
        if (attacker.isEmpty() || victim.isEmpty() || weapon.isEmpty() || !victim.getClass().isActor())
            return false;

        MWWorld::RefData& data = weapon.getRefData();
        if (!data.hasPoison())
            return false;

        const ESM::Potion* potion = MWBase::Environment::get().getWorld()->getStore().get<ESM::Potion>()
            .search(data.getPoisonId());
        if (!potion)
        {
            data.clearPoison();
            return false;
        }

        ESM::EffectList targetEffects = potion->mEffects;
        for (ESM::ENAMstruct& effect : targetEffects.mList)
            effect.mRange = ESM::RT_Touch;

        MWMechanics::CastSpell cast(attacker, victim, fromProjectile);
        cast.mId = potion->mId;
        cast.mSourceName = potion->mName;
        cast.mSourceType = MWMechanics::CastSpell::SourceType::Potion;
        cast.mHitPosition = hitPosition;
        cast.mStack = false;
        cast.inflict(victim, attacker, targetEffects, ESM::RT_Touch);

        data.consumePoisonCharge();
        return true;
    }

    std::string describeWeaponPoison(const MWWorld::ConstPtr& weapon)
    {
        if (weapon.isEmpty() || !weapon.getRefData().hasPoison())
            return {};
        const ESM::Potion* potion = MWBase::Environment::get().getWorld()->getStore().get<ESM::Potion>()
            .search(weapon.getRefData().getPoisonId());
        const std::string name = potion && !potion->mName.empty() ? potion->mName : weapon.getRefData().getPoisonId();
        return name + " (" + std::to_string(weapon.getRefData().getPoisonCharges()) + ")";
    }
}
