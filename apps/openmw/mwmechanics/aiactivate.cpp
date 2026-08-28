#include "aiactivate.hpp"

#include <algorithm>
#include <cstddef>

#include <components/esm/aisequence.hpp>
#include <components/misc/rng.hpp>
#include <components/misc/stringops.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/world.hpp"
#include "../mwbase/environment.hpp"

#include "../mwworld/class.hpp"

#include "../mwrender/animation.hpp"

#include "creaturestats.hpp"
#include "movement.hpp"
#include "steering.hpp"
#include "animationenhancements.hpp"

namespace
{
    bool isWorshipTarget(const MWWorld::Ptr& target)
    {
        if (target.isEmpty())
            return false;

        std::string signature = target.getCellRef().getRefId();
        signature += " ";
        signature += target.getClass().getName(target);
        signature += " ";
        signature += target.getClass().getModel(target);
        Misc::StringUtils::lowerCaseInPlace(signature);

        return signature.find("altar") != std::string::npos
            || signature.find("shrine") != std::string::npos
            || signature.find("prayer") != std::string::npos;
    }

    bool startWorshipAnimation(const MWWorld::Ptr& actor, std::string& group, int& blendMask)
    {
        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(actor);
        if (!animation)
            return false;

        struct Candidate
        {
            const char* mGroup;
            int mBlendMask;
            float mSpeed;
        };
        static const Candidate sCandidates[] = {
            { "prayer1", MWRender::Animation::BlendMask_All, 0.82f },
            { "prayer2", MWRender::Animation::BlendMask_All, 0.82f },
            { "armsAlmaPray", MWRender::Animation::BlendMask_UpperBody, 0.76f },
            { "PoseAlma3", MWRender::Animation::BlendMask_UpperBody, 0.82f },
        };

        const int first = Misc::Rng::rollDice(static_cast<int>(sizeof(sCandidates) / sizeof(sCandidates[0])));
        for (std::size_t offset = 0; offset < sizeof(sCandidates) / sizeof(sCandidates[0]); ++offset)
        {
            const Candidate& candidate
                = sCandidates[(static_cast<std::size_t>(first) + offset)
                    % (sizeof(sCandidates) / sizeof(sCandidates[0]))];
            if (!animation->hasAnimation(candidate.mGroup))
                continue;

            ArenaMW::InteractionAnimationData data;
            data.group = candidate.mGroup;
            data.blendMask = candidate.mBlendMask;
            data.speed = candidate.mSpeed;
            data.loops = 1;
            if (ArenaMW::playInteractionAnimation(actor, data))
            {
                group = candidate.mGroup;
                blendMask = candidate.mBlendMask;
                return true;
            }
        }
        return false;
    }

    void stopWorshipAnimation(const MWWorld::Ptr& actor, const std::string& group, int blendMask)
    {
        if (group.empty() || blendMask == 0)
            return;
        ArenaMW::InteractionAnimationData data;
        data.group = group;
        data.blendMask = blendMask;
        ArenaMW::stopInteractionAnimation(actor, data);
    }
}

namespace MWMechanics
{
    AiActivate::AiActivate(const std::string &objectId)
        : mObjectId(objectId)
    {
    }

    AiActivate::AiActivate(MWWorld::Ptr object)
        : mObjectId("")
        , mObjectPtr(object)
    {
    }

    bool AiActivate::execute(const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration)
    {
        const MWWorld::Ptr target = mObjectId.empty() ? mObjectPtr
            : MWBase::Environment::get().getWorld()->searchPtr(mObjectId, false);

        actor.getClass().getCreatureStats(actor).setDrawState(DrawState_Nothing);

        // Stop if the target doesn't exist. Also release a contextual pose if the
        // activatable vanished while the actor was in the middle of the prayer.
        // Really we should be checking whether the target is currently registered with the MechanicsManager.
        if (target == MWWorld::Ptr() || !target.getRefData().getCount() || !target.getRefData().isEnabled())
        {
            if (mContextActionStarted)
                stopWorshipAnimation(actor, mContextAnimation, mContextBlendMask);
            mContextActionStarted = false;
            mContextAnimation.clear();
            mContextBlendMask = 0;
            return true;
        }

        // Turn to target and move to it directly, without pathfinding.
        const osg::Vec3f targetDir = target.getRefData().getPosition().asVec3() - actor.getRefData().getPosition().asVec3();

        zTurn(actor, std::atan2(targetDir.x(), targetDir.y()), 0.f);
        actor.getClass().getMovementSettings(actor).mPosition[1] = 1;
        actor.getClass().getMovementSettings(actor).mPosition[0] = 0;

        const float activationDistance = MWBase::Environment::get().getWorld()->getMaxActivationDistance();
        if (mContextActionStarted && targetDir.length() > activationDistance)
        {
            // The actor can be pushed away while praying. Release the full-body pose and
            // let AiActivate approach again; it may retry the contextual action once close.
            stopWorshipAnimation(actor, mContextAnimation, mContextBlendMask);
            mContextActionStarted = false;
            mContextActionTimer = 0.f;
            mContextAnimation.clear();
            mContextBlendMask = 0;
        }

        if (activationDistance >= targetDir.length())
        {
            actor.getClass().getMovementSettings(actor).mPosition[1] = 0;
            actor.getClass().getMovementSettings(actor).mPosition[0] = 0;

            // Contextual altar/shrine use: pause briefly for a prayer animation before the
            // actual activation. This only runs once for this runtime AiActivate package.
            if (!mContextActionCompleted && actor.getClass().isNpc()
                && Settings::Manager::getBool("contextual npc animations", "GUI")
                && isWorshipTarget(target))
            {
                if (!mContextActionStarted)
                {
                    mContextAnimation.clear();
                    mContextBlendMask = 0;
                    if (startWorshipAnimation(actor, mContextAnimation, mContextBlendMask))
                    {
                        mContextActionStarted = true;
                        mContextActionTimer = 2.4f;
                        return false;
                    }
                    mContextActionCompleted = true;
                }
                else
                {
                    mContextActionTimer -= std::max(0.f, duration);
                    if (mContextActionTimer > 0.f)
                        return false;

                    stopWorshipAnimation(actor, mContextAnimation, mContextBlendMask);
                    mContextAnimation.clear();
                    mContextBlendMask = 0;
                    mContextActionStarted = false;
                    mContextActionCompleted = true;
                }
            }

            // Note: we intentionally do not cancel package after activation here for backward compatibility with original engine.
            MWBase::Environment::get().getWorld()->activate(target, actor);
        }
        return false;
    }

    void AiActivate::writeState(ESM::AiSequence::AiSequence &sequence) const
    {
        std::unique_ptr<ESM::AiSequence::AiActivate> activate(new ESM::AiSequence::AiActivate());
        activate->mTargetId = mObjectId;

        ESM::AiSequence::AiPackageContainer package;
        package.mType = ESM::AiSequence::Ai_Activate;
        package.mPackage = activate.release();
        sequence.mPackages.push_back(package);
    }

    AiActivate::AiActivate(const ESM::AiSequence::AiActivate *activate)
        : mObjectId(activate->mTargetId)
    {
    }
}
