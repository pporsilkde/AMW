#ifndef GAME_MWDIALOGUE_TOPICRECOVERY_H
#define GAME_MWDIALOGUE_TOPICRECOVERY_H

#include <string>

#include <components/esm/loaddial.hpp>
#include <components/esm/loadinfo.hpp>
#include <components/interpreter/defines.hpp>
#include <components/misc/stringops.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/npcstats.hpp"
#include "../mwscript/interpretercontext.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/ptr.hpp"

namespace MWDialogue
{
    /// Recovery of topic responses out of the locally loaded dialogue database.
    ///
    /// The multiplayer server only replicates *which* topics a player knows - it
    /// never transfers the responses themselves. Old and converted saves have the
    /// same problem: a topic record can arrive with an empty TEXT field and no
    /// usable INFO id. In both cases the journal ends up holding a Topic without a
    /// single entry, which is why such a topic used to open as a blank page and
    /// was reported as "no entries" by the quest manager.
    ///
    /// Every client has the complete DIAL/INFO data loaded anyway, so the missing
    /// text is looked up here instead of being sent over the network.
    namespace TopicRecovery
    {
        /// Player-side membership check, mirroring the dialogue filter.
        inline bool playerMeetsPcRequirement(const ESM::DialInfo& info)
        {
            if (info.mPcFaction.empty())
                return true;

            try
            {
                const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
                const int rank = player.getClass().getNpcStats(player).getFactionRank(info.mPcFaction);
                if (rank < 0)
                    return false;
                return static_cast<int>(info.mData.mPCrank) <= rank;
            }
            catch (...)
            {
                return false;
            }
        }

        /// True when the response is tied to one particular speaker. We no longer
        /// know who was talked to, so such responses must never be presented as
        /// the generic answer for a topic.
        inline bool isSpeakerBound(const ESM::DialInfo& info)
        {
            return !info.mActor.empty() || !info.mRace.empty() || !info.mClass.empty()
                || !info.mFaction.empty() || !info.mCell.empty() || info.mData.mGender >= 0;
        }

        /// Pick the INFO a player would have been given for this topic.
        ///
        /// An exact INFO id always wins. Otherwise the candidates are ranked the
        /// way the real dialogue filter resolves them: the first unconditional
        /// response in file order is what every actor falls through to, so it is
        /// preferred over player-gated and condition-gated variants, and anything
        /// bound to a specific speaker is only used as a last resort.
        inline const ESM::DialInfo* selectInfo(const ESM::Dialogue& dialogue, const std::string& infoId,
            bool allowGeneric)
        {
            const ESM::DialInfo* exact = nullptr;
            const ESM::DialInfo* unrestricted = nullptr;
            const ESM::DialInfo* pcGated = nullptr;
            const ESM::DialInfo* selectGated = nullptr;
            const ESM::DialInfo* anyResponse = nullptr;

            for (const ESM::DialInfo& info : dialogue.mInfo)
            {
                if (info.mResponse.empty())
                    continue;

                if (!exact && !infoId.empty() && Misc::StringUtils::ciEqual(info.mId, infoId))
                    exact = &info;

                if (!allowGeneric)
                    continue;

                if (!anyResponse)
                    anyResponse = &info;

                if (isSpeakerBound(info))
                    continue;

                if (!info.mPcFaction.empty())
                {
                    if (!pcGated && playerMeetsPcRequirement(info))
                        pcGated = &info;
                    continue;
                }

                if (!info.mSelects.empty())
                {
                    if (!selectGated)
                        selectGated = &info;
                    continue;
                }

                if (!unrestricted)
                    unrestricted = &info;
            }

            if (exact)
                return exact;
            if (unrestricted)
                return unrestricted;
            if (pcGated)
                return pcGated;
            if (selectGated)
                return selectGated;
            return anyResponse;
        }

        /// Resolve %-defines in a response.
        ///
        /// An empty implicit reference already covers every player-side define
        /// (%PCName, %PCRace, %PCRank, ...). Speaker-side defines throw without a
        /// reference, so those fall back to the player rather than dropping an
        /// otherwise perfectly readable entry.
        inline std::string expandDefines(const std::string& response)
        {
            if (response.find('%') == std::string::npos && response.find('^') == std::string::npos)
                return response;

            try
            {
                MWScript::InterpreterContext context(nullptr, MWWorld::Ptr());
                return Interpreter::fixDefinesDialog(response, context);
            }
            catch (...)
            {
            }

            try
            {
                MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
                MWScript::InterpreterContext context(&player.getRefData().getLocals(), player);
                return Interpreter::fixDefinesDialog(response, context);
            }
            catch (...)
            {
            }

            return response;
        }

        /// Text to show for a known topic whose stored entry carries none.
        /// Returns an empty string when nothing can be resolved, so callers keep
        /// their own "no entries" handling for genuinely unknown records.
        inline std::string responseFor(const std::string& topicId, const std::string& infoId,
            std::string* resolvedInfoId = nullptr)
        {
            if (topicId.empty())
                return std::string();

            try
            {
                const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
                const ESM::Dialogue* dialogue = store.get<ESM::Dialogue>().search(topicId);
                if (!dialogue)
                    return std::string();

                // Quest journals are addressed by journal index. Substituting a
                // generic response there would invent quest progress, so only an
                // exact INFO match is allowed to be recovered for them.
                const bool allowGeneric = dialogue->mType != ESM::Dialogue::Journal;

                const ESM::DialInfo* info = selectInfo(*dialogue, infoId, allowGeneric);
                if (!info)
                    return std::string();

                if (resolvedInfoId)
                    *resolvedInfoId = info->mId;

                return expandDefines(info->mResponse);
            }
            catch (...)
            {
            }

            return std::string();
        }
    }
}

#endif
