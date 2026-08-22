#include "alchemyknowledge.hpp"

#include "npcstats.hpp"

#include <algorithm>
#include <limits>

#include <components/esm/esmreader.hpp>
#include <components/esm/esmwriter.hpp>
#include <components/esm/loadingr.hpp>
#include <components/esm/loadskil.hpp>
#include <components/misc/stringops.hpp>
#include <components/settings/settings.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    std::map<std::string, AlchemyKnowledge::IngredientKnowledge> AlchemyKnowledge::sKnowledge;
    std::string AlchemyKnowledge::sLastBrew;
    std::vector<std::string> AlchemyKnowledge::sLastRecipe;
    int AlchemyKnowledge::sLastMode = 0;

    std::string AlchemyKnowledge::normalise(const std::string& id)
    {
        return Misc::StringUtils::lowerCase(id);
    }

    AlchemyKnowledge::IngredientKnowledge& AlchemyKnowledge::get(const std::string& ingredientId)
    {
        return sKnowledge[normalise(ingredientId)];
    }

    bool AlchemyKnowledge::isKnown(const std::string& ingredientId, int effectIndex)
    {
        if (effectIndex < 0 || effectIndex >= 4)
            return false;
        const auto found = sKnowledge.find(normalise(ingredientId));
        return found != sKnowledge.end() && (found->second.mKnownMask & (1u << effectIndex)) != 0;
    }

    void AlchemyKnowledge::confirm(const std::string& ingredientId, int effectIndex, unsigned int amount)
    {
        if (effectIndex < 0 || effectIndex >= 4)
            return;

        IngredientKnowledge& knowledge = get(ingredientId);
        const unsigned int old = knowledge.mConfirmations[effectIndex];
        const unsigned int value = std::min<unsigned int>(
            std::numeric_limits<std::uint16_t>::max(), old + amount);
        knowledge.mConfirmations[effectIndex] = static_cast<std::uint16_t>(value);

        const int threshold = std::max(1, Settings::Manager::getInt("learning confirmations", "ArenaMW Alchemy"));
        if (value >= static_cast<unsigned int>(threshold))
            knowledge.mKnownMask |= static_cast<std::uint8_t>(1u << effectIndex);
    }

    void AlchemyKnowledge::learnFromEating(const MWWorld::ConstPtr& ingredient)
    {
        if (!Settings::Manager::getBool("learn from eating", "ArenaMW Alchemy") || ingredient.isEmpty())
            return;

        const auto* ref = ingredient.get<ESM::Ingredient>();
        if (!ref || !ref->mBase || ref->mBase->mData.mEffectID[0] < 0)
            return;

        IngredientKnowledge& knowledge = get(ref->mBase->mId);
        knowledge.mKnownMask |= 1u;
        knowledge.mConfirmations[0] = std::max<std::uint16_t>(knowledge.mConfirmations[0], 1);
    }

    void AlchemyKnowledge::learnFromBrewing(const MWWorld::ConstPtr& ingredient, short effectId,
        signed char skill, signed char attribute)
    {
        if (!Settings::Manager::getBool("learn from brewing", "ArenaMW Alchemy") || ingredient.isEmpty())
            return;

        const auto* ref = ingredient.get<ESM::Ingredient>();
        if (!ref || !ref->mBase)
            return;

        for (int i = 0; i < 4; ++i)
        {
            if (ref->mBase->mData.mEffectID[i] != effectId)
                continue;
            if (ref->mBase->mData.mSkills[i] != skill || ref->mBase->mData.mAttributes[i] != attribute)
                continue;
            confirm(ref->mBase->mId, i);
        }
    }

    void AlchemyKnowledge::revealBySkill(const MWWorld::ConstPtr& ingredient, float alchemySkill)
    {
        if (!Settings::Manager::getBool("learn from skill", "ArenaMW Alchemy") || ingredient.isEmpty())
            return;

        const auto* ref = ingredient.get<ESM::Ingredient>();
        if (!ref || !ref->mBase)
            return;

        const int thresholds[4] = {
            Settings::Manager::getInt("skill reveal effect 1", "ArenaMW Alchemy"),
            Settings::Manager::getInt("skill reveal effect 2", "ArenaMW Alchemy"),
            Settings::Manager::getInt("skill reveal effect 3", "ArenaMW Alchemy"),
            Settings::Manager::getInt("skill reveal effect 4", "ArenaMW Alchemy")
        };
        IngredientKnowledge& knowledge = get(ref->mBase->mId);
        for (int i = 0; i < 4; ++i)
        {
            if (ref->mBase->mData.mEffectID[i] >= 0 && alchemySkill >= thresholds[i])
                knowledge.mKnownMask |= static_cast<std::uint8_t>(1u << i);
        }
    }

    void AlchemyKnowledge::revealInventoryBySkill(const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty())
            return;
        const float skill = actor.getClass().getNpcStats(actor).getSkill(ESM::Skill::Alchemy).getBase();
        MWWorld::ContainerStore& store = actor.getClass().getContainerStore(actor);
        for (MWWorld::ContainerStoreIterator it = store.begin(MWWorld::ContainerStore::Type_Ingredient);
             it != store.end(); ++it)
            revealBySkill(*it, skill);
    }

    void AlchemyKnowledge::setLastBrew(const std::string& name)
    {
        sLastBrew = name;
    }

    const std::string& AlchemyKnowledge::getLastBrew()
    {
        return sLastBrew;
    }

    void AlchemyKnowledge::setLastRecipe(const std::vector<std::string>& ingredientIds, int mode)
    {
        sLastRecipe = ingredientIds;
        sLastMode = mode;
        if (sLastRecipe.size() > 4)
            sLastRecipe.resize(4);
    }

    const std::vector<std::string>& AlchemyKnowledge::getLastRecipe()
    {
        return sLastRecipe;
    }

    int AlchemyKnowledge::getLastMode()
    {
        return sLastMode;
    }

    void AlchemyKnowledge::write(ESM::ESMWriter& writer)
    {
        writer.writeHNT("AVER", static_cast<std::uint32_t>(1));
        if (!sLastBrew.empty())
            writer.writeHNString("LAST", sLastBrew);
        writer.writeHNT("LRMD", sLastMode);
        static const char* recipeTags[4] = {"LR00", "LR01", "LR02", "LR03"};
        for (std::size_t i = 0; i < sLastRecipe.size() && i < 4; ++i)
            writer.writeHNString(recipeTags[i], sLastRecipe[i]);

        for (const auto& entry : sKnowledge)
        {
            writer.writeHNString("INGR", entry.first);
            writer.writeHNT("KNWN", entry.second.mKnownMask);
            writer.writeHNT("CNF0", entry.second.mConfirmations[0]);
            writer.writeHNT("CNF1", entry.second.mConfirmations[1]);
            writer.writeHNT("CNF2", entry.second.mConfirmations[2]);
            writer.writeHNT("CNF3", entry.second.mConfirmations[3]);
        }
    }

    void AlchemyKnowledge::read(ESM::ESMReader& reader)
    {
        clear();
        std::uint32_t version = 0;
        reader.getHNOT(version, "AVER");
        (void)version;
        sLastBrew = reader.getHNOString("LAST");
        reader.getHNOT(sLastMode, "LRMD");
        static const char* recipeTags[4] = {"LR00", "LR01", "LR02", "LR03"};
        for (const char* tag : recipeTags)
        {
            const std::string id = reader.getHNOString(tag);
            if (!id.empty())
                sLastRecipe.push_back(id);
        }

        while (reader.isNextSub("INGR"))
        {
            const std::string id = reader.getHString();
            IngredientKnowledge knowledge;
            reader.getHNT(knowledge.mKnownMask, "KNWN");
            reader.getHNT(knowledge.mConfirmations[0], "CNF0");
            reader.getHNT(knowledge.mConfirmations[1], "CNF1");
            reader.getHNT(knowledge.mConfirmations[2], "CNF2");
            reader.getHNT(knowledge.mConfirmations[3], "CNF3");
            sKnowledge[normalise(id)] = knowledge;
        }
    }

    void AlchemyKnowledge::clear()
    {
        sKnowledge.clear();
        sLastBrew.clear();
        sLastRecipe.clear();
        sLastMode = 0;
    }
}
