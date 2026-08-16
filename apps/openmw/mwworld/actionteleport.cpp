#include "actionteleport.hpp"

#include <algorithm>
#include <list>
#include <vector>
#include <utility>

#include <components/misc/rng.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/aisequence.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/cellstore.hpp"

#include "player.hpp"

namespace
{
    struct QueuedTeleport
    {
        MWWorld::Ptr mActor;
        std::string mCellName;
        ESM::Position mPosition;
        float mDelay = 0.f;
        int mRequiredCombatTargetId = -1;
    };

    std::vector<QueuedTeleport> sQueuedTeleports;
}

namespace MWWorld
{
    ActionTeleport::ActionTeleport (const std::string& cellName,
        const ESM::Position& position, bool teleportFollowers)
    : Action (true), mCellName (cellName), mPosition (position), mTeleportFollowers(teleportFollowers)
    {
    }

    void ActionTeleport::executeImp (const Ptr& actor)
    {
        if (mTeleportFollowers)
        {
            std::set<MWWorld::Ptr> followers;
            const bool includeHostilePursuers
                = Settings::Manager::getBool("combat pursuit through doors", "Game");
            getFollowers(actor, followers, includeHostilePursuers);

            std::size_t delayedPursuerIndex = 0;
            for (std::set<MWWorld::Ptr>::iterator it = followers.begin(); it != followers.end(); ++it)
            {
                MWMechanics::AiSequence& sequence
                    = it->getClass().getCreatureStats(*it).getAiSequence();
                const bool hostilePursuer = sequence.isInCombat(actor);
                const bool delayedPursuit = hostilePursuer
                    && Settings::Manager::getBool("combat pursuit delayed door transition", "Game");

                if (!delayedPursuit)
                {
                    teleport(*it, actor);
                    continue;
                }

                MWBase::World* world = MWBase::Environment::get().getWorld();
                const ESM::CellId fromCellId = it->getCell()->getCell()->getCellId();
                const std::string fromCellName = it->getCell()->isExterior()
                    ? std::string() : it->getCell()->getCell()->mName;
                const ESM::Position fromPosition = actor.getRefData().getPosition();

                MWWorld::CellStore* destinationCell = nullptr;
                if (mCellName.empty())
                {
                    int cellX = 0;
                    int cellY = 0;
                    world->positionToIndex(mPosition.pos[0], mPosition.pos[1], cellX, cellY);
                    destinationCell = world->getExterior(cellX, cellY);
                }
                else
                    destinationCell = world->getInterior(mCellName);

                if (destinationCell)
                {
                    sequence.recordDoorTransition(fromCellId, fromCellName, fromPosition,
                        destinationCell->getCell()->getCellId(), mPosition);
                }

                const float minDelay = std::max(0.f,
                    Settings::Manager::getFloat("combat pursuit door delay min", "Game"));
                const float maxDelay = std::max(minDelay,
                    Settings::Manager::getFloat("combat pursuit door delay max", "Game"));
                const float randomDelay = minDelay
                    + (maxDelay - minDelay) * Misc::Rng::rollClosedProbability();
                const float stagger = static_cast<float>(delayedPursuerIndex++) * 0.12f;
                const int targetActorId = actor.getClass().getCreatureStats(actor).getActorId();
                queueDelayedTeleport(*it, mCellName, mPosition, randomDelay + stagger, targetActorId);
            }
        }

        teleport(actor);
    }

    void ActionTeleport::teleport(const Ptr& actor, const Ptr& teleportTarget)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        actor.getClass().getCreatureStats(actor).land(actor == world->getPlayerPtr());
        if(actor == world->getPlayerPtr())
        {
            world->getPlayer().setTeleported(true);
            if (mCellName.empty())
                world->changeToExteriorCell (mPosition, true);
            else
                world->changeToInteriorCell (mCellName, mPosition, true);
        }
        else
        {
            MWMechanics::AiSequence& sequence
                = actor.getClass().getCreatureStats(actor).getAiSequence();
            const bool isCombatPursuer = !teleportTarget.isEmpty()
                && sequence.isInCombat(teleportTarget);

            // Keep AiCombat for an attacker following its target through this door.
            if (sequence.isInCombat(world->getPlayerPtr()) && !isCombatPursuer)
                sequence.stopCombat();
            else if (mCellName.empty())
            {
                int cellX;
                int cellY;
                world->positionToIndex(mPosition.pos[0],mPosition.pos[1],cellX,cellY);
                world->moveObject(actor,world->getExterior(cellX,cellY),
                    mPosition.pos[0],mPosition.pos[1],mPosition.pos[2]);
            }
            else
                world->moveObject(actor,world->getInterior(mCellName),mPosition.pos[0],mPosition.pos[1],mPosition.pos[2]);
        }
    }

    void ActionTeleport::queueDelayedTeleport(const MWWorld::Ptr& actor, const std::string& cellName,
        const ESM::Position& position, float delay, int requiredCombatTargetId)
    {
        if (actor.isEmpty() || !actor.getClass().isActor())
            return;

        QueuedTeleport queued;
        queued.mActor = actor;
        queued.mCellName = cellName;
        queued.mPosition = position;
        queued.mDelay = std::max(0.f, delay);
        queued.mRequiredCombatTargetId = requiredCombatTargetId;
        sQueuedTeleports.push_back(std::move(queued));
    }

    void ActionTeleport::updateDelayedTeleports(float duration)
    {
        if (sQueuedTeleports.empty())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        for (auto it = sQueuedTeleports.begin(); it != sQueuedTeleports.end();)
        {
            it->mDelay -= duration;
            if (it->mDelay > 0.f)
            {
                ++it;
                continue;
            }

            MWWorld::Ptr actor = it->mActor;
            if (actor.isEmpty() || !actor.getRefData().getCount() || !actor.getRefData().isEnabled()
                || !actor.getClass().isActor() || actor.getClass().getCreatureStats(actor).isDead())
            {
                it = sQueuedTeleports.erase(it);
                continue;
            }

            MWWorld::Ptr combatTarget;
            if (it->mRequiredCombatTargetId >= 0)
            {
                combatTarget = world->searchPtrViaActorId(it->mRequiredCombatTargetId);
                if (combatTarget.isEmpty()
                    || !actor.getClass().getCreatureStats(actor).getAiSequence().isInCombat(combatTarget))
                {
                    it = sQueuedTeleports.erase(it);
                    continue;
                }
            }

            ActionTeleport action(it->mCellName, it->mPosition, false);
            action.teleport(actor, combatTarget);
            it = sQueuedTeleports.erase(it);
        }
    }

    void ActionTeleport::clearDelayedTeleports()
    {
        sQueuedTeleports.clear();
    }

    void ActionTeleport::getFollowers(const MWWorld::Ptr& actor, std::set<MWWorld::Ptr>& out, bool includeHostiles) {
        std::set<MWWorld::Ptr> followers;
        MWBase::MechanicsManager* mechanics
            = MWBase::Environment::get().getMechanicsManager();
        mechanics->getActorsFollowing(actor, followers);

        if (includeHostiles)
        {
            const std::list<MWWorld::Ptr> pursuers = mechanics->getActorsFighting(actor);
            followers.insert(pursuers.begin(), pursuers.end());
        }

        std::size_t hostilePursuerCount = 0;
        const int maxHostilePursuers = std::max(0,
            Settings::Manager::getInt("combat pursuit max actors", "Game"));
        const float guaranteedDistance = std::max(0.f,
            Settings::Manager::getFloat("combat pursuit guaranteed distance", "Game"));
        const float maximumDoorDistance = std::max(guaranteedDistance,
            Settings::Manager::getFloat("combat pursuit door max distance", "Game"));
        const float minimumChance = std::max(0.f, std::min(1.f,
            Settings::Manager::getFloat("combat pursuit minimum chance", "Game")));

        for(std::set<MWWorld::Ptr>::iterator it = followers.begin();it != followers.end();++it)
        {
            MWWorld::Ptr follower = *it;

            if (!follower.getRefData().getCount() || !follower.getRefData().isEnabled()
                || follower.getClass().getCreatureStats(follower).isDead())
                continue;

            std::string script = follower.getClass().getScript(follower);

            const bool isHostilePursuer
                = follower.getClass().getCreatureStats(follower).getAiSequence().isInCombat(actor);
            if (!includeHostiles && isHostilePursuer)
                continue;

            if (!script.empty() && follower.getRefData().getLocals().getIntVar(script, "stayoutside") == 1)
                continue;

            const float distance = (follower.getRefData().getPosition().asVec3()
                - actor.getRefData().getPosition().asVec3()).length();

            if (isHostilePursuer)
            {
                const MWMechanics::AiSequence& sequence
                    = follower.getClass().getCreatureStats(follower).getAiSequence();
                const int maxDoorTransitions = std::max(1,
                    Settings::Manager::getInt("combat pursuit max door transitions", "Game"));
                const bool humanoid
                    = follower.getClass().isNpc() || follower.getClass().isBipedal(follower);
                if (!sequence.hasPackage(MWMechanics::AiPackageTypeId::InternalTravel)
                    || sequence.getReturnHomeDoorTransitionCount() >= static_cast<std::size_t>(maxDoorTransitions)
                    || !humanoid || maxHostilePursuers == 0
                    || hostilePursuerCount >= static_cast<std::size_t>(maxHostilePursuers)
                    || distance > maximumDoorDistance)
                    continue;

                float pursuitChance = 1.f;
                if (distance > guaranteedDistance && maximumDoorDistance > guaranteedDistance)
                {
                    const float normalizedDistance = std::min(1.f,
                        (distance - guaranteedDistance) / (maximumDoorDistance - guaranteedDistance));
                    pursuitChance = 1.f - normalizedDistance * (1.f - minimumChance);
                }

                if (Misc::Rng::rollClosedProbability() > pursuitChance)
                    continue;

                ++hostilePursuerCount;
            }
            else if (distance > 800.f)
                continue;

            out.emplace(follower);
        }
    }
}
