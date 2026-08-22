#include "actioneat.hpp"

#include <components/esm/loadskil.hpp>

#include "../mwworld/containerstore.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/alchemyknowledge.hpp"

#include "class.hpp"

namespace MWWorld
{
    void ActionEat::executeImp (const Ptr& actor)
    {
        if (actor == MWMechanics::getPlayer())
            MWMechanics::AlchemyKnowledge::learnFromEating(getTarget());

        // remove used item (assume the item is present in inventory)
        getTarget().getContainerStore()->remove(getTarget(), 1, actor);

        // apply to actor
        std::string id = getTarget().getCellRef().getRefId();

        if (actor.getClass().apply (actor, id, actor) && actor == MWMechanics::getPlayer())
            actor.getClass().skillUsageSucceeded (actor, ESM::Skill::Alchemy, 1);
    }

    ActionEat::ActionEat (const MWWorld::Ptr& object) : Action (false, object) {}
}
