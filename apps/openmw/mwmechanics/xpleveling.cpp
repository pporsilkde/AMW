#include "xpleveling.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <MyGUI_LanguageManager.h>
#include <set>
#include <sstream>

#include <components/esm/attr.hpp>
#include <components/esm/loadbook.hpp>
#include <components/esm/loadclas.hpp>
#include <components/esm/loaddial.hpp>
#include <components/esm/loadgmst.hpp>
#include <components/esm/loadinfo.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/esm/loadskil.hpp>
#include <components/misc/stringops.hpp>
#include <components/misc/rng.hpp>
#include <components/settings/settings.hpp>

#include "actorutil.hpp"
#include "difficultyscaling.hpp"
#include "npcstats.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/timestamp.hpp"

namespace
{
    float clampFloat(float value, float low, float high)
    {
        return std::max(low, std::min(high, value));
    }

    float positiveSetting(const char* name, float fallback)
    {
        const float value = Settings::Manager::getFloat(name, "XP Leveling");
        return value > 0.f ? value : fallback;
    }

    float nonNegativeSetting(const char* name)
    {
        return std::max(0.f, Settings::Manager::getFloat(name, "XP Leveling"));
    }

    bool showNotifications()
    {
        return Settings::Manager::getBool("show xp notifications", "XP Leveling");
    }

    std::string arenaText(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    float difficultyXpMultiplier()
    {
        if (!Settings::Manager::getBool("difficulty xp scaling", "XP Leveling"))
            return 1.f;

        const int tier = std::max(1, std::min(6, difficultyTier()));
        static const char* settingNames[] =
        {
            "difficulty xp tier 1 multiplier",
            "difficulty xp tier 2 multiplier",
            "difficulty xp tier 3 multiplier",
            "difficulty xp tier 4 multiplier",
            "difficulty xp tier 5 multiplier",
            "difficulty xp tier 6 multiplier"
        };
        static const float fallbacks[] = { 1.f, 1.15f, 1.30f, 1.45f, 1.60f, 1.75f };
        return positiveSetting(settingNames[tier - 1], fallbacks[tier - 1]);
    }

    float totalXpMultiplier()
    {
        return positiveSetting("xp gain multiplier", 1.f) * difficultyXpMultiplier();
    }

    float chanceSetting(const char* name, float fallback)
    {
        const float value = Settings::Manager::getFloat(name, "XP Leveling");
        return std::isfinite(value) ? clampFloat(value, 0.f, 1.f) : fallback;
    }

    bool randomRewardRoll(const char* chanceName, float fallback)
    {
        return Misc::Rng::rollProbability() < chanceSetting(chanceName, fallback);
    }

    float finalXpAmount(float rawAmount)
    {
        return rawAmount * totalXpMultiplier();
    }

    std::string formatXp(float amount)
    {
        std::ostringstream stream;
        if (std::fabs(amount - std::round(amount)) < 0.05f)
            stream << static_cast<int>(std::round(amount));
        else
            stream << std::fixed << std::setprecision(1) << amount;
        return stream.str();
    }

    const ESM::Class& getPlayerClass(const MWWorld::Ptr& player)
    {
        const ESM::NPC* npc = player.get<ESM::NPC>()->mBase;
        return *MWBase::Environment::get().getWorld()->getStore().get<ESM::Class>().find(npc->mClass);
    }

    float xpRequirementForLevel(int level)
    {
        const float base = positiveSetting("base xp to level", 1000.f);
        const int targetLevel = std::max(1, level);

        // C24: progressive level curve. The amount added to the next-level
        // requirement grows as 10, 15, 20, ... 50, then 60, 70, ... by
        // default. This makes high character levels progressively more
        // expensive without the abrupt linear +250 jump used by the old
        // prototype. The legacy linear rule remains available as a fallback.
        if (!Settings::Manager::getBool("progressive xp curve", "XP Leveling"))
        {
            const float perLevel = nonNegativeSetting("xp per level");
            return std::max(1.f, base + std::max(0, targetLevel - 1) * perLevel);
        }

        float requirement = base;
        float increment = nonNegativeSetting("xp level increment start");
        const float lowStep = nonNegativeSetting("xp level increment step");
        const float threshold = nonNegativeSetting("xp level increment threshold");
        const float highStep = nonNegativeSetting("xp level increment high step");

        for (int currentLevel = 1; currentLevel < targetLevel; ++currentLevel)
        {
            requirement += increment;

            if (increment < threshold)
                increment = std::min(threshold, increment + lowStep);
            else
                increment += highStep;
        }

        return std::max(1.f, requirement);
    }

    void notifyXp(const std::string& text)
    {
        if (!showNotifications() || text.empty())
            return;

        // Arena Y011: gameplay XP-system notifications use the shared right-side
        // feed. If an XP action is initiated from a GUI (for example spending
        // skill points), keep the old MessageBox route so the feedback cannot
        // expire invisibly behind a menu.
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        if (windowManager->isGuiMode())
            windowManager->messageBox(text);
        else
            windowManager->hudNotification(
                text, std::string(), "icons\\k\\tx_attribute_luck.dds");
    }

    void notifyXpGain(float amount, const std::string& legacyText)
    {
        if (!showNotifications() || legacyText.empty() || !std::isfinite(amount))
            return;

        // Existing XP call sites already build "+N XP - reason" strings. Keep
        // their localized reason but let the HUD own amount formatting and
        // coalescing so multiple rewards from the same source merge cleanly.
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        if (windowManager->isGuiMode())
        {
            windowManager->messageBox(legacyText);
            return;
        }

        std::string reason;
        const std::size_t separator = legacyText.find(" - ");
        if (separator != std::string::npos && separator + 3 < legacyText.size())
            reason = legacyText.substr(separator + 3);
        windowManager->hudExperienceNotification(amount, reason);
    }

    void completeLevelUp(const MWWorld::Ptr& player)
    {
        MWMechanics::NpcStats& stats = player.getClass().getNpcStats(player);

        const MWWorld::Store<ESM::GameSetting>& gmst
            = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();

        const float endurance = stats.getAttribute(ESM::Attribute::Endurance).getBase();
        const float healthGain = endurance * gmst.find("fLevelUpHealthEndMult")->mValue.getFloat();

        MWMechanics::DynamicStat<float> health(stats.getHealth());
        health.setBase(stats.getHealth().getBase() + healthGain);
        health.setCurrent(std::max(1.f, stats.getHealth().getCurrent() + healthGain));
        stats.setHealth(health);

        stats.setLevel(stats.getLevel() + 1);

        // Arena XP: Luck is not governed by any skill, so it advances from
        // character levels instead. Grant +1 Luck on every even character
        // level (2, 4, 6, ...), capped at the normal attribute maximum of 100.
        if ((stats.getLevel() % 2) == 0)
        {
            const float luck = stats.getAttribute(ESM::Attribute::Luck).getBase();
            if (luck < 100.f)
                stats.setAttribute(ESM::Attribute::Luck, std::min(100.f, luck + 1.f));
        }

        const int points = std::max(0, Settings::Manager::getInt("skill points per level", "XP Leveling"));
        stats.addSkillPoints(points);

        std::ostringstream message;
        message << arenaText("xp.msg.level_reached") << ": " << stats.getLevel();
        if (points > 0)
            message << ". +" << points << " " << arenaText("xp.msg.skill_points");
        notifyXp(message.str());
    }

    void addExperience(const MWWorld::Ptr& player, float amount, const std::string& notification)
    {
        if (!MWMechanics::XPLeveling::isEnabled() || player.isEmpty() || !player.getClass().isNpc())
            return;

        amount *= totalXpMultiplier();
        if (!(amount > 0.f) || !std::isfinite(amount))
            return;

        MWMechanics::NpcStats& stats = player.getClass().getNpcStats(player);
        stats.setExperience(std::max(0.f, stats.getExperience()) + amount);

        if (!notification.empty())
            notifyXpGain(amount, notification);

        // XP is banked immediately. No vanilla sleep gate or attribute picker is
        // involved; each completed level grants spendable skill points instead.
        for (int guard = 0; guard < 100; ++guard)
        {
            const float required = xpRequirementForLevel(stats.getLevel());
            if (stats.getExperience() + 0.0001f < required)
                break;

            stats.setExperience(std::max(0.f, stats.getExperience() - required));
            completeLevelUp(player);
        }
    }

    bool awardOnce(const MWWorld::Ptr& player, const std::string& key, float amount,
        const std::string& notification)
    {
        if (!MWMechanics::XPLeveling::isEnabled() || key.empty())
            return false;

        MWMechanics::NpcStats& stats = player.getClass().getNpcStats(player);
        if (stats.hasXpRewardKey(key))
            return false;

        if (!(amount > 0.f))
            return false;

        stats.addXpRewardKey(key);
        addExperience(player, amount, notification);
        return true;
    }

    float classAttributeProgress(const ESM::Class& class_, const ESM::Skill& skill, int skillId)
    {
        float progress = nonNegativeSetting("attribute progress misc");
        for (int i = 0; i < 5; ++i)
        {
            if (class_.mData.mSkills[i][0] == skillId)
            {
                progress = nonNegativeSetting("attribute progress minor");
                break;
            }
            if (class_.mData.mSkills[i][1] == skillId)
            {
                progress = nonNegativeSetting("attribute progress major");
                break;
            }
        }

        if (skill.mData.mSpecialization == class_.mData.mSpecialization)
            progress *= positiveSetting("attribute specialization multiplier", 1.25f);

        progress *= positiveSetting("attribute progress multiplier", 1.f);
        return progress;
    }

    void addAttributeProgress(const MWWorld::Ptr& player, int attribute, float amount)
    {
        if (attribute < 0 || attribute >= ESM::Attribute::Length || amount <= 0.f)
            return;

        MWMechanics::NpcStats& stats = player.getClass().getNpcStats(player);
        float progress = stats.getXpAttributeProgress(attribute) + amount;
        float base = stats.getAttribute(attribute).getBase();
        int gained = 0;

        while (progress >= 1.f && base < 100.f)
        {
            progress -= 1.f;
            base += 1.f;
            stats.setAttribute(attribute, std::min(100.f, base));
            ++gained;
        }

        if (base >= 100.f)
            progress = 0.f;
        stats.setXpAttributeProgress(attribute, progress);

        if (gained > 0)
        {
            const std::string& gmstId = ESM::Attribute::sGmstAttributeIds[attribute];
            const std::string name = MWBase::Environment::get().getWindowManager()
                ->getGameSettingString(gmstId, gmstId);
            std::ostringstream message;
            message << name << " " << arenaText("xp.msg.increased_to") << " "
                    << static_cast<int>(stats.getAttribute(attribute).getBase());
            notifyXp(message.str());
        }
    }

    bool playerResponsibleForKill(const MWWorld::Ptr& attacker)
    {
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (attacker == player)
            return true;

        if (attacker.isEmpty())
            return false;

        std::set<MWWorld::Ptr> followers;
        MWBase::Environment::get().getMechanicsManager()->getActorsSidingWith(player, followers);
        return followers.find(attacker) != followers.end();
    }
}

namespace MWMechanics
{
    namespace XPLeveling
    {
        bool isEnabled()
        {
            return Settings::Manager::getBool("enabled", "XP Leveling");
        }

        float getXpForNextLevel(const MWWorld::Ptr& player)
        {
            if (player.isEmpty() || !player.getClass().isActor())
                return xpRequirementForLevel(1);
            return xpRequirementForLevel(std::max(1, player.getClass().getCreatureStats(player).getLevel()));
        }

        int getSkillPointCost(float skillBase)
        {
            if (skillBase < 50.f)
                return 1;
            if (skillBase < 75.f)
                return 2;
            if (skillBase < 90.f)
                return 3;
            return 4;
        }

        void awardSkillUse(const MWWorld::Ptr& player, int skillId, int usageType,
            float extraFactor, const ESM::Class& class_)
        {
            if (!isEnabled() || player.isEmpty() || player != MWMechanics::getPlayer())
                return;
            if (skillId < 0 || skillId >= ESM::Skill::Length || usageType >= 4)
                return;

            const ESM::Skill* skill = MWBase::Environment::get().getWorld()->getStore().get<ESM::Skill>().find(skillId);
            float organicGain = 1.f;
            if (usageType >= 0)
                organicGain = skill->mData.mUseValue[usageType];

            organicGain *= std::max(0.f, extraFactor);
            if (!(organicGain > 0.f))
                return;

            const NpcStats& stats = player.getClass().getNpcStats(player);
            const float requirement = std::max(1.f, stats.getSkillProgressRequirement(skillId, class_));
            const float equivalentXp = positiveSetting("xp per skill level equivalent", 50.f);

            // The use-value already contains the action-specific difficulty signal
            // OpenMW uses for progression (and many ArenaMW systems pass a richer
            // extraFactor). Normalising by the current skill requirement makes the
            // pipeline generic across all 27 skills without any skill-ID tables.
            float xp = equivalentXp * organicGain / requirement;

            const float globalSkillXp = Settings::Manager::getFloat("global XP gain multiplier", "Game");
            if (globalSkillXp > 0.f)
                xp *= globalSkillXp;

            addExperience(player, xp, std::string());
        }

        void awardKill(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker)
        {
            if (!isEnabled() || victim.isEmpty() || attacker.isEmpty() || victim == attacker
                || victim == MWMechanics::getPlayer())
                return;
            if (!victim.getClass().isActor() || !playerResponsibleForKill(attacker))
                return;

            const MWWorld::Ptr player = MWMechanics::getPlayer();
            const int victimLevel = std::max(1, victim.getClass().getCreatureStats(victim).getLevel());
            const int playerLevel = std::max(1, player.getClass().getCreatureStats(player).getLevel());

            const float base = nonNegativeSetting("kill base xp");
            const float perLevel = nonNegativeSetting("kill xp per victim level");
            const float relativeDanger = clampFloat(
                std::sqrt(static_cast<float>(victimLevel) / static_cast<float>(playerLevel)), 0.35f, 2.25f);
            const float xp = (base + perLevel * victimLevel) * relativeDanger;

            std::ostringstream message;
            message << "+" << formatXp(finalXpAmount(xp)) << " XP - " << arenaText("xp.msg.defeated") << " "
                    << victim.getClass().getName(victim);
            addExperience(player, xp, message.str());
        }

        void awardQuestProgress(const std::string& questId, int journalIndex, bool completed)
        {
            if (!isEnabled() || questId.empty() || journalIndex <= 0)
                return;

            const MWWorld::Ptr player = MWMechanics::getPlayer();
            if (player.isEmpty() || !player.getClass().isNpc())
                return;

            const ESM::Dialogue* dialogue = MWBase::Environment::get().getWorld()
                ->getStore().get<ESM::Dialogue>().search(questId);
            if (!dialogue)
                return;

            std::string questName;
            for (const ESM::DialInfo& info : dialogue->mInfo)
            {
                if (info.mQuestStatus == ESM::DialInfo::QS_Name && !info.mResponse.empty())
                {
                    questName = info.mResponse;
                    break;
                }
            }
            if (questName.empty())
                questName = questId;

            NpcStats& stats = player.getClass().getNpcStats(player);
            const std::string lowerId = Misc::StringUtils::lowerCase(questId);
            const std::string stageKey = "quest-stage:" + lowerId + ":" + std::to_string(journalIndex);
            const std::string completionKey = "quest-complete:" + lowerId;

            float xp = 0.f;
            if (!stats.hasXpRewardKey(stageKey))
            {
                stats.addXpRewardKey(stageKey);
                xp += nonNegativeSetting("quest xp per stage");
            }

            if (completed && !stats.hasXpRewardKey(completionKey))
            {
                stats.addXpRewardKey(completionKey);
                xp += nonNegativeSetting("quest base xp");
            }

            if (!(xp > 0.f))
                return;

            std::ostringstream message;
            message << "+" << formatXp(finalXpAmount(xp))
                    << " XP - " << arenaText(completed ? "xp.msg.quest_completed" : "xp.msg.quest_progress")
                    << ": " << questName;
            addExperience(player, xp, message.str());
        }

        void awardTravel(const MWWorld::Ptr& player)
        {
            if (!isEnabled() || player.isEmpty() || player != MWMechanics::getPlayer())
                return;

            // Persist the roll window in XP reward keys. This prevents door/cell
            // bouncing and, in ArenaMP, prevents a server restart/relog from
            // resetting the travel reward opportunity. Only the current slot is kept.
            const float cooldownHours = std::max(0.25f, positiveSetting("travel xp cooldown hours", 2.f));
            const MWWorld::TimeStamp now = MWBase::Environment::get().getWorld()->getTimeStamp();
            const double absoluteHours = static_cast<double>(now.getDay()) * 24.0 + now.getHour();
            const long long slot = static_cast<long long>(std::floor(absoluteHours / cooldownHours));
            const std::string prefix = "travel-roll-slot:";
            const std::string slotKey = prefix + std::to_string(slot);

            NpcStats& stats = player.getClass().getNpcStats(player);
            if (stats.hasXpRewardKey(slotKey))
                return;
            stats.removeXpRewardKeysWithPrefix(prefix);
            stats.addXpRewardKey(slotKey); // consume this roll even when RNG fails

            if (!randomRewardRoll("travel xp chance", 0.20f))
                return;
            const float xp = nonNegativeSetting("travel xp");
            if (!(xp > 0.f))
                return;
            std::ostringstream message;
            message << "+" << formatXp(finalXpAmount(xp)) << " XP - " << arenaText("xp.msg.travel");
            addExperience(player, xp, message.str());
        }

        void awardSuccessfulTrade(const MWWorld::Ptr& player)
        {
            if (!isEnabled() || player.isEmpty() || player != MWMechanics::getPlayer()
                || !randomRewardRoll("trade bonus xp chance", 0.20f))
                return;
            const float xp = nonNegativeSetting("trade bonus xp");
            if (!(xp > 0.f))
                return;
            std::ostringstream message;
            message << "+" << formatXp(finalXpAmount(xp)) << " XP - " << arenaText("xp.msg.trade_success");
            addExperience(player, xp, message.str());
        }

        void awardCriticalHit(const MWWorld::Ptr& player, const MWWorld::Ptr& victim)
        {
            if (!isEnabled() || player.isEmpty() || victim.isEmpty() || player != MWMechanics::getPlayer()
                || !victim.getClass().isActor() || !randomRewardRoll("critical bonus xp chance", 0.35f))
                return;
            const int victimLevel = std::max(1, victim.getClass().getCreatureStats(victim).getLevel());
            const int playerLevel = std::max(1, player.getClass().getCreatureStats(player).getLevel());
            const float danger = clampFloat(std::sqrt(static_cast<float>(victimLevel) / playerLevel), 0.5f, 2.f);
            const float xp = nonNegativeSetting("critical bonus xp") * danger;
            if (!(xp > 0.f))
                return;
            std::ostringstream message;
            message << "+" << formatXp(finalXpAmount(xp)) << " XP - " << arenaText("xp.msg.critical");
            addExperience(player, xp, message.str());
        }

        void awardSuccessfulTheft(const MWWorld::Ptr& player, const MWWorld::Ptr& item, int count)
        {
            if (!isEnabled() || player.isEmpty() || item.isEmpty() || count <= 0 || player != MWMechanics::getPlayer()
                || !randomRewardRoll("theft bonus xp chance", 0.30f))
                return;
            const float xp = nonNegativeSetting("theft bonus xp");
            if (!(xp > 0.f))
                return;
            std::ostringstream message;
            message << "+" << formatXp(finalXpAmount(xp)) << " XP - " << arenaText("xp.msg.theft");
            addExperience(player, xp, message.str());
        }

        void awardBookRead(const MWWorld::Ptr& player, const ESM::Book& book)
        {
            if (!isEnabled() || player.isEmpty() || player != MWMechanics::getPlayer()
                || book.mData.mIsScroll)
                return;

            const bool skillBook = book.mData.mSkillId >= 0 && book.mData.mSkillId < ESM::Skill::Length;
            const float xp = nonNegativeSetting(skillBook ? "skill book xp" : "lore book xp");
            if (!(xp > 0.f))
                return;

            std::ostringstream message;
            message << "+" << formatXp(finalXpAmount(xp)) << " XP - " << arenaText("xp.msg.read") << " "
                    << (book.mName.empty() ? book.mId : book.mName);
            addExperience(player, xp, message.str());
        }

        void applyDeathPenalty(const MWWorld::Ptr& player)
        {
            if (!isEnabled() || player.isEmpty() || !player.getClass().isNpc())
                return;

            NpcStats& stats = player.getClass().getNpcStats(player);
            const float fraction = clampFloat(Settings::Manager::getFloat(
                "death xp loss fraction", "XP Leveling"), 0.f, 1.f);
            const float loss = std::min(stats.getExperience(), std::max(0.f, stats.getExperience() * fraction));
            if (!(loss > 0.f))
                return;

            stats.setExperience(std::max(0.f, stats.getExperience() - loss));
            std::ostringstream message;
            message << arenaText("xp.msg.death") << ": -" << formatXp(loss) << " XP";
            notifyXp(message.str());
        }

        bool spendSkillPoints(const MWWorld::Ptr& player, int skillId)
        {
            if (!isEnabled() || player.isEmpty() || !player.getClass().isNpc())
                return false;
            if (skillId < 0 || skillId >= ESM::Skill::Length)
                return false;

            NpcStats& stats = player.getClass().getNpcStats(player);
            const float before = stats.getSkill(skillId).getBase();
            if (before >= 100.f)
            {
                notifyXp(arenaText("xp.msg.skill_at_100"));
                return false;
            }

            const int cost = getSkillPointCost(before);
            if (stats.getSkillPoints() < cost)
            {
                std::ostringstream message;
                message << arenaText("xp.msg.not_enough_sp") << " (" << arenaText("xp.msg.need")
                        << " " << cost << ")";
                notifyXp(message.str());
                return false;
            }

            const ESM::Class& class_ = getPlayerClass(player);
            stats.increaseSkill(skillId, class_, false, false);
            const float after = stats.getSkill(skillId).getBase();
            if (after <= before)
                return false;

            if (!stats.spendSkillPoints(cost))
            {
                // Defensive rollback: this should be unreachable because the
                // balance check occurs immediately above.
                stats.getSkill(skillId).setBase(before);
                return false;
            }

            const ESM::Skill* skill = MWBase::Environment::get().getWorld()
                ->getStore().get<ESM::Skill>().find(skillId);
            addAttributeProgress(player, skill->mData.mAttribute,
                classAttributeProgress(class_, *skill, skillId));

            std::ostringstream message;
            message << arenaText("xp.msg.skill_points") << ": " << stats.getSkillPoints()
                    << " " << arenaText("xp.msg.remaining");
            notifyXp(message.str());
            return true;
        }
    }
}
