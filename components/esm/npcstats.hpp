#ifndef OPENMW_ESM_NPCSTATS_H
#define OPENMW_ESM_NPCSTATS_H

#include <string>
#include <vector>
#include <map>

#include "statstate.hpp"

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    // format 0, saved games only

    struct NpcStats
    {
        struct Faction
        {
            bool mExpelled;
            int mRank;
            int mReputation;

            Faction();
        };

        bool mIsWerewolf;

        bool mWerewolfDeprecatedData;

        std::map<std::string, Faction> mFactions; // lower case IDs
        int mDisposition;
        StatState<float> mSkills[27];
        int mBounty;
        int mReputation;
        int mWerewolfKills;
        int mLevelProgress;
        // ArenaMW native XP leveling state. Version 0 means a pre-XP save and
        // allows the runtime to perform a one-time migration from LPRO.
        int mXpVersion;
        float mExperience;
        int mSkillPoints;
        float mXpAttributeProgress[8];
        std::vector<std::string> mXpRewardKeys;
        int mSkillIncrease[8];
        int mSpecIncreases[3];
        std::vector<std::string> mUsedIds; // lower case IDs
        float mTimeToStartDrowning;
        int mCrimeId;

        /// Initialize to default state
        void blank();

        void load (ESMReader &esm);
        void save (ESMWriter &esm) const;
    };
}

#endif
