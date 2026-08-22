#ifndef OPENMW_MWMECHANICS_ALCHEMYKNOWLEDGE_H
#define OPENMW_MWMECHANICS_ALCHEMYKNOWLEDGE_H

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace MWWorld
{
    class ConstPtr;
    class Ptr;
}

namespace MWMechanics
{
    class AlchemyKnowledge
    {
    public:
        struct IngredientKnowledge
        {
            std::uint8_t mKnownMask = 0;
            std::array<std::uint16_t, 4> mConfirmations{{0, 0, 0, 0}};
        };

        static bool isKnown(const std::string& ingredientId, int effectIndex);
        static void learnFromEating(const MWWorld::ConstPtr& ingredient);
        static void learnFromBrewing(const MWWorld::ConstPtr& ingredient, short effectId, signed char skill, signed char attribute);
        static void revealBySkill(const MWWorld::ConstPtr& ingredient, float alchemySkill);
        static void revealInventoryBySkill(const MWWorld::Ptr& actor);

        static void setLastBrew(const std::string& name);
        static const std::string& getLastBrew();
        static void setLastRecipe(const std::vector<std::string>& ingredientIds, int mode);
        static const std::vector<std::string>& getLastRecipe();
        static int getLastMode();

        static void write(ESM::ESMWriter& writer);
        static void read(ESM::ESMReader& reader);
        static void clear();

    private:
        static IngredientKnowledge& get(const std::string& ingredientId);
        static std::string normalise(const std::string& id);
        static void confirm(const std::string& ingredientId, int effectIndex, unsigned int amount = 1);

        static std::map<std::string, IngredientKnowledge> sKnowledge;
        static std::string sLastBrew;
        static std::vector<std::string> sLastRecipe;
        static int sLastMode;
    };
}

#endif
