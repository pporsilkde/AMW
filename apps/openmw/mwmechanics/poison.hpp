#ifndef OPENMW_MWMECHANICS_POISON_H
#define OPENMW_MWMECHANICS_POISON_H

#include <string>

namespace osg
{
    class Vec3f;
}

namespace MWWorld
{
    class Ptr;
    class ConstPtr;
}

namespace MWMechanics
{
    bool isPurePoison(const MWWorld::ConstPtr& potion);
    int poisonChargesFor(const MWWorld::Ptr& actor);
    bool coatEquippedWeapon(const MWWorld::Ptr& actor, const MWWorld::Ptr& potion, std::string* message = nullptr);
    bool applyWeaponPoison(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& weapon,
        const osg::Vec3f& hitPosition, bool fromProjectile = false);
    std::string describeWeaponPoison(const MWWorld::ConstPtr& weapon);
}

#endif
