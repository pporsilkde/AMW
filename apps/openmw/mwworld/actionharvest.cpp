#include "actionharvest.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "class.hpp"
#include "containerstore.hpp"

namespace MWWorld
{
    ActionHarvest::ActionHarvest (const MWWorld::Ptr& container)
        : Action (true, container)
    {
        setSound("Item Ingredient Up");
    }

    void ActionHarvest::executeImp (const MWWorld::Ptr& actor)
    {
        if (!MWBase::Environment::get().getWindowManager()->isAllowed(MWGui::GW_Inventory))
            return;

        MWWorld::Ptr target = getTarget();
        MWWorld::ContainerStore& store = target.getClass().getContainerStore (target);
        store.resolve();
        MWWorld::ContainerStore& actorStore = actor.getClass().getContainerStore(actor);
        for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
        {
            if (!it->getClass().showsInInventory(*it))
                continue;

            int itemCount = it->getRefData().getCount();
            // Note: it is important to check for crime before move an item from container. Otherwise owner check will not work
            // for a last item in the container - empty harvested containers are considered as "allowed to use".
            MWBase::Environment::get().getMechanicsManager()->itemTaken(actor, *it, target, itemCount);
            actorStore.add(*it, itemCount, actor);
            store.remove(*it, itemCount, getTarget());
        }

        // Arena Y013: the right HUD item feed is authoritative for harvested items.
        // Do not emit the legacy centered sNotifyMessage60/61 MessageBox as well.

        // Update animation object
        MWBase::Environment::get().getWorld()->disable(target);
        MWBase::Environment::get().getWorld()->enable(target);
    }
}
