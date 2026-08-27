#include "journalentry.hpp"

#include <stdexcept>

#include <components/esm/journalentry.hpp>
#include <components/misc/stringops.hpp>

#include <components/interpreter/defines.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/esmstore.hpp"

#include "../mwscript/interpretercontext.hpp"


namespace MWDialogue
{
    Entry::Entry() {}

    Entry::Entry (const std::string& topic, const std::string& infoId, const MWWorld::Ptr& actor)
    : mInfoId (infoId)
    {
        const ESM::Dialogue *dialogue =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::Dialogue>().find (topic);

        for (ESM::Dialogue::InfoContainer::const_iterator iter (dialogue->mInfo.begin());
            iter!=dialogue->mInfo.end(); ++iter)
            if (Misc::StringUtils::ciEqual(iter->mId, mInfoId))
            {
                if (actor.isEmpty())
                {
                    MWScript::InterpreterContext interpreterContext(nullptr, MWWorld::Ptr());
                    mText = Interpreter::fixDefinesDialog(iter->mResponse, interpreterContext);
                }
                else
                {
                    MWScript::InterpreterContext interpreterContext(&actor.getRefData().getLocals(),actor);
                    mText = Interpreter::fixDefinesDialog(iter->mResponse, interpreterContext);
                }

                return;
            }

        throw std::runtime_error ("unknown info ID " + mInfoId + " for topic " + topic);
    }

    Entry::Entry (const ESM::JournalEntry& record)
        : mInfoId(record.mInfo), mText(record.mText), mActorName(record.mActorName)
    {
        // Old and converted saves can contain a topic JOUR record whose cached
        // TEXT is empty. If the INFO still exists, rebuild the text from the
        // current dialogue database instead of exposing an empty topic page.
        if (!mText.empty() || record.mTopic.empty())
            return;

        try
        {
            const ESM::Dialogue* dialogue = MWBase::Environment::get().getWorld()->getStore()
                .get<ESM::Dialogue>().search(record.mTopic);
            if (!dialogue)
                return;

            const ESM::DialInfo* recovered = nullptr;
            if (!mInfoId.empty())
            {
                for (const ESM::DialInfo& info : dialogue->mInfo)
                {
                    if (Misc::StringUtils::ciEqual(info.mId, mInfoId))
                    {
                        recovered = &info;
                        break;
                    }
                }
            }

            // Legacy/imported topic records may have neither cached TEXT nor a
            // usable INFO id. In that case use only an unrestricted, generic
            // response from the current DIAL as a conservative recovery path.
            // We deliberately do not expose faction/actor/condition-specific
            // alternatives here, which would reveal dialogue the player may
            // never have heard.
            if (!recovered)
            {
                for (const ESM::DialInfo& info : dialogue->mInfo)
                {
                    if (info.mResponse.empty())
                        continue;
                    if (!info.mActor.empty() || !info.mRace.empty() || !info.mClass.empty()
                        || !info.mFaction.empty() || !info.mPcFaction.empty() || !info.mCell.empty()
                        || !info.mSelects.empty())
                        continue;
                    recovered = &info; // generic catch-all entries are usually last
                }
            }

            if (recovered)
            {
                MWScript::InterpreterContext interpreterContext(nullptr, MWWorld::Ptr());
                mText = Interpreter::fixDefinesDialog(recovered->mResponse, interpreterContext);
            }
        }
        catch (...)
        {
        }
    }

    std::string Entry::getText() const
    {
        return mText;
    }

    void Entry::write (ESM::JournalEntry& entry) const
    {
        entry.mInfo = mInfoId;
        entry.mText = mText;
        entry.mActorName = mActorName;
    }


    JournalEntry::JournalEntry() {}

    JournalEntry::JournalEntry (const std::string& topic, const std::string& infoId, const MWWorld::Ptr& actor)
        : Entry (topic, infoId, actor), mTopic (topic)
    {}

    JournalEntry::JournalEntry (const ESM::JournalEntry& record)
        : Entry (record), mTopic (record.mTopic)
    {}

    void JournalEntry::write (ESM::JournalEntry& entry) const
    {
        Entry::write (entry);
        entry.mTopic = mTopic;
    }

    JournalEntry JournalEntry::makeFromQuest (const std::string& topic, int index)
    {
        return JournalEntry (topic, idFromIndex (topic, index), MWWorld::Ptr());
    }

    std::string JournalEntry::idFromIndex (const std::string& topic, int index)
    {
        const ESM::Dialogue *dialogue =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::Dialogue>().find (topic);

        for (ESM::Dialogue::InfoContainer::const_iterator iter (dialogue->mInfo.begin());
            iter!=dialogue->mInfo.end(); ++iter)
            if (iter->mData.mJournalIndex==index)
            {
                return iter->mId;
            }

        throw std::runtime_error ("unknown journal index for topic " + topic);
    }


    StampedJournalEntry::StampedJournalEntry()
    : mDay (0), mMonth (0), mDayOfMonth (0)
    {}

    StampedJournalEntry::StampedJournalEntry (const std::string& topic, const std::string& infoId,
        int day, int month, int dayOfMonth, const MWWorld::Ptr& actor)
    : JournalEntry (topic, infoId, actor), mDay (day), mMonth (month), mDayOfMonth (dayOfMonth)
    {}

    StampedJournalEntry::StampedJournalEntry (const ESM::JournalEntry& record)
    : JournalEntry (record), mDay (record.mDay), mMonth (record.mMonth),
      mDayOfMonth (record.mDayOfMonth)
    {}

    void StampedJournalEntry::write (ESM::JournalEntry& entry) const
    {
        JournalEntry::write (entry);
        entry.mDay = mDay;
        entry.mMonth = mMonth;
        entry.mDayOfMonth = mDayOfMonth;
    }

    StampedJournalEntry StampedJournalEntry::makeFromQuest (const std::string& topic, int index, const MWWorld::Ptr& actor)
    {
        int day = MWBase::Environment::get().getWorld()->getGlobalInt ("dayspassed");
        int month = MWBase::Environment::get().getWorld()->getGlobalInt ("month");
        int dayOfMonth = MWBase::Environment::get().getWorld()->getGlobalInt ("day");

        return StampedJournalEntry (topic, idFromIndex (topic, index), day, month, dayOfMonth, actor);
    }
}
