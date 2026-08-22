#include "questmanager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <utility>

#include <MyGUI_Button.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_TabControl.h>
#include <MyGUI_TabItem.h>
#include <MyGUI_TextBox.h>

#include <components/esm/loaddial.hpp>
#include <components/esm/loadinfo.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/misc/stringops.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/journal.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwdialogue/quest.hpp"
#include "../mwdialogue/topic.hpp"
#include "../mwworld/esmstore.hpp"

namespace
{
    std::string trim(const std::string& value)
    {
        std::size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
            ++first;
        std::size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
            --last;
        return value.substr(first, last - first);
    }

    bool starts(const std::string& value, const std::string& prefix)
    {
        return value.compare(0, prefix.size(), prefix) == 0;
    }

    bool inSet(const std::string& value, std::initializer_list<const char*> values)
    {
        for (const char* candidate : values)
            if (value == candidate)
                return true;
        return false;
    }

    std::string firstUsefulName(const MWDialogue::Quest& quest, const std::string& fallback)
    {
        std::string name = trim(quest.getName());
        return name.empty() ? fallback : name;
    }

    std::string sanitizeJournalText(const std::string& value)
    {
        std::string out;
        out.reserve(value.size());
        for (char c : value)
            if (c != '@' && c != '#')
                out.push_back(c);
        return out;
    }

    bool titleLike(const std::string& value)
    {
        const std::string text = trim(sanitizeJournalText(value));
        if (text.empty() || text.size() > 72)
            return false;
        const char last = text.back();
        return last != '.' && last != '!' && last != '?' && last != ';' && last != ',';
    }

    std::string recordTitle(const std::string& id, const std::string& fallback)
    {
        try
        {
            const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
            const ESM::Dialogue* record = store.get<ESM::Dialogue>().search(id);
            if (record && record->mType == ESM::Dialogue::Journal)
            {
                const ESM::DialInfo* best = nullptr;
                for (const ESM::DialInfo& info : record->mInfo)
                    if ((!best || info.mData.mJournalIndex < best->mData.mJournalIndex)
                        && !info.mResponse.empty())
                        best = &info;
                if (best && titleLike(best->mResponse))
                    return trim(sanitizeJournalText(best->mResponse));
            }
        }
        catch (...) {}

        std::string text = trim(sanitizeJournalText(fallback));
        if (text.size() > 42)
            text = text.substr(0, 39) + "...";
        return text.empty() ? id : text;
    }
}

namespace MWGui
{
    QuestManagerWindow::QuestManagerWindow(std::function<void()> returnToJournal)
        : WindowModal("openmw_questmanager.layout")
        , mReturnToJournal(std::move(returnToJournal))
    {
        getWidget(mTitle, "QuestmanTitle");
        getWidget(mTabs, "QuestmanTabs");
        getWidget(mQuestSearch, "QuestSearch");
        getWidget(mQuestFilter, "QuestFilter");
        getWidget(mShowCompletedHidden, "ShowCompletedHidden");
        getWidget(mQuestList, "QuestList");
        getWidget(mQuestIcon, "QuestIcon");
        getWidget(mQuestHeading, "QuestHeading");
        getWidget(mQuestDetail, "QuestDetail");
        getWidget(mPinButton, "PinButton");
        getWidget(mHideButton, "HideButton");
        getWidget(mTopicSearch, "TopicSearch");
        getWidget(mTopicList, "TopicList");
        getWidget(mTopicHeading, "TopicHeading");
        getWidget(mTopicDetail, "TopicDetail");
        getWidget(mRecordSearch, "RecordSearch");
        getWidget(mRecordList, "RecordList");
        getWidget(mRecordHeading, "RecordHeading");
        getWidget(mRecordDetail, "RecordDetail");
        getWidget(mStatsDetail, "StatsDetail");
        getWidget(mCloseButton, "CloseButton");

        mTitle->setCaption(tr("questman.title"));
        mTabs->getItemAt(0)->setCaption(tr("questman.tab.quests"));
        mTabs->getItemAt(1)->setCaption(tr("questman.tab.topics"));
        mTabs->getItemAt(2)->setCaption(tr("questman.tab.records"));
        mTabs->getItemAt(3)->setCaption(tr("questman.tab.stats"));
        mQuestSearch->setCaption("");
        mTopicSearch->setCaption("");
        mRecordSearch->setCaption("");
        mShowCompletedHidden->setCaption(tr("questman.show_completed_hidden"));
        mCloseButton->setCaption(tr("questman.close"));

        mQuestDetail->setEditStatic(true);
        mQuestDetail->setEditReadOnly(true);
        mQuestDetail->setEditMultiLine(true);
        mQuestDetail->setEditWordWrap(true);
        mTopicDetail->setEditStatic(true);
        mTopicDetail->setEditReadOnly(true);
        mTopicDetail->setEditMultiLine(true);
        mTopicDetail->setEditWordWrap(true);
        mRecordDetail->setEditStatic(true);
        mRecordDetail->setEditReadOnly(true);
        mRecordDetail->setEditMultiLine(true);
        mRecordDetail->setEditWordWrap(true);
        mStatsDetail->setEditStatic(true);
        mStatsDetail->setEditReadOnly(true);
        mStatsDetail->setEditMultiLine(true);
        mStatsDetail->setEditWordWrap(true);

        mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyClose);
        mTabs->eventTabChangeSelect += MyGUI::newDelegate(this, &QuestManagerWindow::notifyTabChanged);
        mQuestSearch->eventEditTextChange += MyGUI::newDelegate(this, &QuestManagerWindow::notifySearchChanged);
        mTopicSearch->eventEditTextChange += MyGUI::newDelegate(this, &QuestManagerWindow::notifySearchChanged);
        mRecordSearch->eventEditTextChange += MyGUI::newDelegate(this, &QuestManagerWindow::notifySearchChanged);
        mQuestFilter->eventComboChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyFilterChanged);
        mShowCompletedHidden->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyShowCompletedHidden);
        mQuestList->eventListChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyQuestSelected);
        mTopicList->eventListChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyTopicSelected);
        mRecordList->eventListChangePosition += MyGUI::newDelegate(this, &QuestManagerWindow::notifyRecordSelected);
        mPinButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyPin);
        mHideButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuestManagerWindow::notifyHide);

        center();
    }

    void QuestManagerWindow::onOpen()
    {
        WindowModal::onOpen();
        center();
        rebuildData();
        refreshCurrentTab();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);
    }

    bool QuestManagerWindow::exit()
    {
        notifyClose(nullptr);
        return false;
    }

    void QuestManagerWindow::clear()
    {
        mQuests.clear();
        mTopics.clear();
        mRecords.clear();
        mQuestGivers.clear();
        mQuestGiverStages.clear();
        mVisibleQuests.clear();
        mVisibleTopics.clear();
        mVisibleRecords.clear();
    }

    std::string QuestManagerWindow::tr(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    std::string QuestManagerWindow::lower(const std::string& value)
    {
        return Misc::StringUtils::lowerCaseUtf8(value);
    }

    bool QuestManagerWindow::containsSearch(const std::string& haystack, const std::string& needle)
    {
        if (needle.empty())
            return true;
        return lower(haystack).find(needle) != std::string::npos;
    }

    bool QuestManagerWindow::getBool(const char* setting, bool fallback)
    {
        try { return Settings::Manager::getBool(setting, "Questman"); }
        catch (...) { return fallback; }
    }

    std::set<std::string> QuestManagerWindow::readIdSet(const char* setting)
    {
        std::set<std::string> result;
        std::string data;
        try { data = Settings::Manager::getString(setting, "Questman"); }
        catch (...) { return result; }
        std::stringstream stream(data);
        std::string item;
        while (std::getline(stream, item, ';'))
        {
            item = trim(item);
            if (!item.empty())
                result.insert(lower(item));
        }
        return result;
    }

    void QuestManagerWindow::writeIdSet(const char* setting, const std::set<std::string>& values)
    {
        std::string data;
        for (const std::string& value : values)
        {
            if (!data.empty())
                data += ';';
            data += value;
        }
        Settings::Manager::setString(setting, "Questman", data);
    }

    std::string QuestManagerWindow::truncate(const std::string& value, std::size_t maxChars)
    {
        if (value.size() <= maxChars)
            return value;
        return value.substr(0, maxChars > 3 ? maxChars - 3 : maxChars) + (maxChars > 3 ? "..." : "");
    }

    std::string QuestManagerWindow::join(const std::vector<std::string>& values, const std::string& separator)
    {
        std::string result;
        for (const std::string& value : values)
        {
            if (!result.empty())
                result += separator;
            result += value;
        }
        return result;
    }

    std::string QuestManagerWindow::labelForAddon(const std::string& value)
    {
        return tr("questman.addon." + value);
    }

    std::string QuestManagerWindow::labelForCategory(const std::string& value)
    {
        return tr("questman.category." + value);
    }

    std::string QuestManagerWindow::labelForFaction(const std::string& value)
    {
        if (value.empty())
            return tr("questman.none");
        return tr("questman.faction." + value);
    }

    std::string QuestManagerWindow::questIconFor(const Metadata& meta)
    {
        if (!meta.mIcon.empty())
            return "Icons/questman/" + meta.mIcon;
        if (!meta.mFaction.empty())
            return "Icons/questman/fa_shared.dds";
        return "Icons/questman/cat_misc.dds";
    }

    QuestManagerWindow::Metadata QuestManagerWindow::classifyQuest(const std::string& rawId)
    {
        const std::string id = lower(rawId);
        Metadata out;
        out.mAddon = "unknown";
        out.mCategory = "unknown";

        const auto isDigitAt = [&id](std::size_t pos)
        {
            return pos < id.size() && std::isdigit(static_cast<unsigned char>(id[pos]));
        };
        const auto hasToken = [&id](const std::string& token)
        {
            std::size_t pos = id.find(token);
            while (pos != std::string::npos)
            {
                const std::size_t after = pos + token.size();
                if (after < id.size() && (id[after] == '_' || std::isdigit(static_cast<unsigned char>(id[after]))))
                    return true;
                pos = id.find(token, pos + 1);
            }
            return false;
        };
        const auto hasTagNumber = [&id](const std::string& tag)
        {
            std::size_t pos = id.find(tag);
            while (pos != std::string::npos)
            {
                const std::size_t after = pos + tag.size();
                if (after < id.size() && std::isdigit(static_cast<unsigned char>(id[after])))
                    return true;
                pos = id.find(tag, pos + 1);
            }
            return false;
        };
        const auto setFaction = [&out](const char* faction)
        {
            out.mFaction = faction;
            out.mCategory = inSet(faction, {"hlaalu", "redoran", "telvanni"}) ? "great_house" : "faction";
        };

        // Stage 1: identify the province/add-on exactly like Questman's gated
        // rule sets. This deliberately does NOT treat every TR_ id as Tamriel
        // Rebuilt, because vanilla Tribunal also owns TR_ records.
        const bool isTamrielRebuilt = (starts(id, "tr_m") && isDigitAt(4))
            || starts(id, "tr_necmq") || id == "tr_tt_q1";
        const bool isProjectCyrodiil = starts(id, "pc_m") && isDigitAt(4);
        const bool isProvinceSkyrim = starts(id, "sky_");

        if (isTamrielRebuilt)
        {
            out.mAddon = "tamriel_rebuilt";
            out.mCategory = "misc";
            if (id.find("necmq") != std::string::npos) out.mCategory = "main";
            else if (hasToken("_hh")) setFaction("hlaalu");
            else if (hasToken("_hr")) setFaction("redoran");
            else if (hasToken("_ht")) setFaction("telvanni");
            else if (hasToken("_eec")) setFaction("east_empire");
            else if (hasToken("_fg")) setFaction("fighters");
            else if (hasToken("_ic")) setFaction("imperial_cult");
            else if (hasToken("_il")) setFaction("imperial_legion");
            else if (hasToken("_jns")) setFaction("janatta");
            else if (hasToken("_mg")) setFaction("mages");
            else if (hasToken("_mt")) setFaction("morag_tong");
            else if (hasToken("_tg")) setFaction("thieves");
            else if (hasToken("_tt")) setFaction("temple");
            else if (hasToken("_da")) out.mCategory = "daedric";
            else if (hasToken("_va") || hasToken("_bal")) out.mCategory = "vampire";
            else if (hasToken("_arena")) out.mCategory = "narsis_arena";
            else if (id.find("_and_bounty_") != std::string::npos
                || hasToken("_b") || id.find("_uman_b") != std::string::npos)
                out.mCategory = "bounty";

            // Questman's explicit TR outliers.
            if (starts(id, "tr_m1_rr_mq_") || id == "tr_m1_rr_mq_2.5" || id == "tr_m1_rr_mq_6.5")
                setFaction("telvanni");
            else if (id == "tr_m4_and_missingmerchant") setFaction("hlaalu");
            else if (id == "tr_m3_at_mgbonus") setFaction("mages");
            else if (id == "tr_m0_siegeatfiremoth") setFaction("imperial_legion");
            else if (id == "tr_tt_q1") setFaction("temple");
        }
        else if (isProjectCyrodiil)
        {
            out.mAddon = "project_cyrodiil";
            out.mCategory = "misc";
            if (hasToken("_mg")) setFaction("mages");
            else if (hasToken("_tg")) setFaction("thieves");
            else if (hasToken("_fg")) setFaction("fighters");
            else if (hasToken("_k1")) setFaction("kingdom_anvil");
            else if (hasToken("_ip")) setFaction("itinerant_priests");
            else if (hasToken("_bounty")) out.mCategory = "bounty";
            else if (id.find("_afp") != std::string::npos) out.mCategory = "abecette";

            if (id == "pc_m0_fw_colstalker") setFaction("fighters");
            else if (id == "pc_m1_anv_goldenrod") setFaction("kingdom_anvil");
        }
        else if (isProvinceSkyrim)
        {
            out.mAddon = "province_skyrim";
            out.mCategory = "misc";
            if (hasTagNumber("tg")) setFaction("thieves");
            else if (hasTagNumber("mg")) setFaction("mages");
            else if (hasTagNumber("fg")) setFaction("fighters");
            else if (hasTagNumber("b")) out.mCategory = "bounty";
        }
        else
        {
            // Vanilla mainland fallback. Order matters: faction/house prefixes
            // precede broad Tribunal/Bloodmoon main-quest prefixes.
            out.mAddon = "unknown";
            out.mCategory = "unknown";
            if (starts(id, "fg_")) { out.mAddon = "morrowind"; setFaction("fighters"); }
            else if (starts(id, "mg_")) { out.mAddon = "morrowind"; setFaction("mages"); }
            else if (starts(id, "tg_")) { out.mAddon = "morrowind"; setFaction("thieves"); }
            else if (starts(id, "mt_")) { out.mAddon = "morrowind"; setFaction("morag_tong"); }
            else if (starts(id, "il_")) { out.mAddon = "morrowind"; setFaction("imperial_legion"); }
            else if (starts(id, "ic")) { out.mAddon = "morrowind"; setFaction("imperial_cult"); }
            else if (starts(id, "tt_")) { out.mAddon = "morrowind"; setFaction("temple"); }
            else if (starts(id, "co_")) { out.mAddon = "bloodmoon"; setFaction("east_empire"); }
            else if (starts(id, "hh_")) { out.mAddon = "morrowind"; setFaction("hlaalu"); }
            else if (starts(id, "hr_")) { out.mAddon = "morrowind"; setFaction("redoran"); }
            else if (starts(id, "ht_")) { out.mAddon = "morrowind"; setFaction("telvanni"); }
            else if (starts(id, "da_")) { out.mAddon = "morrowind"; out.mCategory = "daedric"; }
            else if (starts(id, "va_")) { out.mAddon = "morrowind"; out.mCategory = "vampire"; }
            else if (id.size() >= 2 && (id[0] == 'a' || id[0] == 'b' || id[0] == 'c') && isDigitAt(1))
                { out.mAddon = "morrowind"; out.mCategory = "main"; }
            else if (starts(id, "cx_")) { out.mAddon = "morrowind"; out.mCategory = "main"; }
            else if (starts(id, "tr")) { out.mAddon = "tribunal"; out.mCategory = "main"; }
            else if (starts(id, "bm_")) { out.mAddon = "bloodmoon"; out.mCategory = "main"; }
            else if (starts(id, "ms_")) { out.mAddon = "tribunal"; out.mCategory = "misc"; }
            else if (starts(id, "mv_") || starts(id, "eb") || starts(id, "blades_")
                || starts(id, "town_") || starts(id, "romance_"))
                { out.mAddon = "morrowind"; out.mCategory = "misc"; }

            // Exact vanilla exceptions from Questman 1.4.
            if (id == "a1_sleeperdreamer02" || id == "a2_1_kurapli_zallay")
                out.mCategory = "misc";
            if (inSet(id, {"bm_airship", "bm_airship_a", "bm_airship_c", "bm_brodirgrove",
                    "bm_cursedcaptain", "bm_cursedcaptain_a", "bm_falmer", "bm_ingmar",
                    "bm_meadhall", "bm_meadhall_a", "bm_meadhall_b", "bm_meadhall_c",
                    "bm_missionary", "bm_moonsugar", "bm_retribution", "bm_sadseer",
                    "bm_tymvaul", "bm_womanscorned"}))
                out.mCategory = "misc";
            if (inSet(id, {"ms_apologies", "ms_arenimtomb", "ms_fargothring", "ms_firemoth",
                    "ms_gold_kanet_flower", "ms_hannat", "ms_hatandskirt", "ms_hentuspants",
                    "ms_jobashaabolitionist", "ms_lookout", "ms_master_index", "ms_nord_burial",
                    "ms_nuccius", "ms_piernette", "ms_trerayna_bounty", "ms_umbra",
                    "ms_vampirecure", "ms_vassirdidanat", "ms_whiteguar"}))
                out.mAddon = "morrowind";
        }

        if (out.mFaction == "hlaalu") out.mIcon = "gh_hlalu.dds";
        else if (out.mFaction == "redoran") out.mIcon = "gh_redoran.dds";
        else if (out.mFaction == "telvanni") out.mIcon = "gh_telvanni.dds";
        else if (out.mFaction == "fighters") out.mIcon = "fa_fighters.dds";
        else if (out.mFaction == "mages") out.mIcon = "fa_mages.dds";
        else if (out.mFaction == "thieves") out.mIcon = "fa_thieves.dds";
        else if (out.mFaction == "east_empire") out.mIcon = "fa_eec.dds";
        else if (out.mFaction == "temple") out.mIcon = "fa_temple.dds";
        else if (out.mFaction == "imperial_legion") out.mIcon = "fa_imp_legion.dds";
        else if (out.mFaction == "imperial_cult") out.mIcon = "fa_imp_cult.dds";
        else if (out.mFaction == "morag_tong") out.mIcon = "fa_morag_tong.dds";
        else if (out.mFaction == "janatta") out.mIcon = "fa_janatta.dds";
        else if (out.mFaction == "kingdom_anvil") out.mIcon = "fa_anvil.dds";
        else if (out.mFaction == "itinerant_priests") out.mIcon = "fa_priests.dds";
        else if (out.mCategory == "vampire") out.mIcon = "cat_vampires.dds";
        else if (out.mCategory == "daedric") out.mIcon = "cat_daedric.dds";
        else if (out.mCategory == "bounty") out.mIcon = "cat_bounty.dds";
        else if (out.mCategory == "narsis_arena") out.mIcon = "cat_narsis_arena.dds";
        else if (out.mCategory == "abecette") out.mIcon = "cat_abecette.dds";
        else if (out.mCategory == "main")
        {
            if (out.mAddon == "tribunal") out.mIcon = "addon_tribunal.dds";
            else if (out.mAddon == "bloodmoon") out.mIcon = "addon_bloodmoon.dds";
            else if (out.mAddon == "tamriel_rebuilt") out.mIcon = "addon_tamriel.dds";
            else if (out.mAddon == "project_cyrodiil") out.mIcon = "addon_cyrodiil.dds";
            else if (out.mAddon == "province_skyrim") out.mIcon = "addon_skyrim.dds";
            else out.mIcon = "addon_morrowind.dds";
        }
        else if (out.mCategory == "great_house") out.mIcon = "gh_shared.dds";
        else if (out.mCategory == "faction") out.mIcon = "fa_shared.dds";
        else out.mIcon = "cat_misc.dds";

        return out;
    }

    void QuestManagerWindow::rebuildQuestGivers()
    {
        mQuestGivers.clear();
        mQuestGiverStages.clear();
        std::map<std::string, int> stages;
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();
        const MWWorld::Store<ESM::Dialogue>& dialogues = store.get<ESM::Dialogue>();
        const std::regex quoted("journal[\\t ,]+\\\"([^\\\"]+)\\\"[\\t ,]+(-?[0-9]+)", std::regex_constants::icase);
        const std::regex unquoted("journal[\\t ,]+([A-Za-z][A-Za-z0-9_]+)[\\t ,]+(-?[0-9]+)", std::regex_constants::icase);

        for (auto dialIt = dialogues.begin(); dialIt != dialogues.end(); ++dialIt)
        {
            for (const ESM::DialInfo& info : dialIt->mInfo)
            {
                if (info.mResultScript.empty() || info.mActor.empty())
                    continue;
                std::smatch match;
                std::string::const_iterator begin = info.mResultScript.begin();
                const std::string::const_iterator end = info.mResultScript.end();
                while (begin != end)
                {
                    std::match_results<std::string::const_iterator> result;
                    bool found = std::regex_search(begin, end, result, quoted);
                    if (!found)
                        found = std::regex_search(begin, end, result, unquoted);
                    if (!found)
                        break;
                    const std::string id = lower(result[1].str());
                    const int stage = std::atoi(result[2].str().c_str());
                    const auto current = stages.find(id);
                    if (current == stages.end() || stage < current->second)
                    {
                        stages[id] = stage;
                        const ESM::NPC* npc = store.get<ESM::NPC>().search(info.mActor);
                        mQuestGivers[id] = npc && !npc->mName.empty() ? npc->mName : info.mActor;
                        mQuestGiverStages[id] = stage;
                    }
                    begin = result[0].second;
                }
            }
        }
    }

    void QuestManagerWindow::rebuildData()
    {
        // Rebuild the save-dependent lists on every open, but keep the giver
        // catalog cached: it depends on loaded content, not on journal state,
        // and scanning every INFO record in TR/PC/SHOTN can be expensive.
        mQuests.clear();
        mTopics.clear();
        mRecords.clear();
        mVisibleQuests.clear();
        mVisibleTopics.clear();
        mVisibleRecords.clear();
        mPinned = readIdSet("pinned quests");
        mHidden = readIdSet("hidden quests");
        mShowCompletedHiddenState = getBool("show completed hidden", false);
        try { mLastQuestId = lower(Settings::Manager::getString("last quest", "Questman")); }
        catch (...) { mLastQuestId.clear(); }
        try { mLastTopicId = lower(Settings::Manager::getString("last topic", "Questman")); }
        catch (...) { mLastTopicId.clear(); }
        if (mQuestGivers.empty())
            rebuildQuestGivers();

        MWBase::Journal* journal = MWBase::Environment::get().getJournal();
        if (!journal)
            return;

        struct Group
        {
            QuestData mQuest;
            std::set<std::string> mIdKeys;
        };
        std::map<std::string, Group> groups;
        std::map<std::string, std::string> idToGroup;

        for (auto it = journal->questBegin(); it != journal->questEnd(); ++it)
        {
            const std::string id = it->first;
            const std::string name = firstUsefulName(it->second, id);
            const std::string groupKey = lower(name);
            Group& group = groups[groupKey];
            if (group.mQuest.mId.empty())
            {
                group.mQuest.mId = id;
                group.mQuest.mName = name;
                const Metadata meta = classifyQuest(id);
                group.mQuest.mAddon = meta.mAddon;
                group.mQuest.mCategory = meta.mCategory;
                group.mQuest.mFaction = meta.mFaction;
                group.mQuest.mIcon = questIconFor(meta);
            }
            group.mQuest.mIds.push_back(id);
            group.mIdKeys.insert(lower(id));
            idToGroup[lower(id)] = groupKey;
            group.mQuest.mCompleted = group.mQuest.mCompleted || it->second.isFinished();
            group.mQuest.mStage = std::max(group.mQuest.mStage, it->second.getIndex());
        }

        int order = 0;
        for (auto it = journal->begin(); it != journal->end(); ++it, ++order)
        {
            const std::string topicKey = lower(it->mTopic);
            const auto groupName = idToGroup.find(topicKey);
            std::ostringstream dateStream;
            dateStream << it->mDayOfMonth << " "
                << MWBase::Environment::get().getWorld()->getMonthName(it->mMonth)
                << " " << it->mDay;
            const std::string date = dateStream.str();
            EntryData entry;
            entry.mText = it->getText();
            entry.mActor = it->mActorName;
            entry.mDate = date;
            entry.mOrder = order;

            if (groupName != idToGroup.end())
            {
                Group& group = groups[groupName->second];
                group.mQuest.mEntries.push_back(entry);
                group.mQuest.mLastOrder = std::max(group.mQuest.mLastOrder, order);
            }
            else
            {
                // Questman's Records tab is specifically the accumulated JOUR
                // records that are not real/name-grouped quests.
                RecordData record;
                record.mTopic = it->mTopic;
                record.mEntry = entry;
                record.mTitle = recordTitle(it->mTopic, entry.mText);
                mRecords.push_back(record);
            }
        }

        for (auto& groupPair : groups)
        {
            QuestData& quest = groupPair.second.mQuest;
            // A translated quest can be represented by multiple JOUR ids. Keep a
            // deterministic identity so pin/hide/last-viewed state does not jump
            // when the journal progresses into another alias.
            std::sort(quest.mIds.begin(), quest.mIds.end(), [](const std::string& a, const std::string& b)
                { return QuestManagerWindow::lower(a) < QuestManagerWindow::lower(b); });
            if (!quest.mIds.empty())
                quest.mId = quest.mIds.front();

            const std::string key = lower(quest.mId);
            quest.mPinned = mPinned.count(key) != 0;
            quest.mHidden = mHidden.count(key) != 0;
            int bestStage = 0x7fffffff;
            for (const std::string& id : quest.mIds)
            {
                const std::string giverKey = lower(id);
                const auto giver = mQuestGivers.find(giverKey);
                const auto stage = mQuestGiverStages.find(giverKey);
                if (giver == mQuestGivers.end() || stage == mQuestGiverStages.end())
                    continue;
                if (stage->second < bestStage)
                {
                    bestStage = stage->second;
                    quest.mGiver = giver->second;
                }
            }
            mQuests.push_back(quest);
        }

        std::stable_sort(mQuests.begin(), mQuests.end(), [](const QuestData& a, const QuestData& b)
        {
            if (a.mPinned != b.mPinned) return a.mPinned > b.mPinned;
            if (a.mCompleted != b.mCompleted) return a.mCompleted < b.mCompleted;
            if (a.mLastOrder != b.mLastOrder) return a.mLastOrder > b.mLastOrder;
            return QuestManagerWindow::lower(a.mName) < QuestManagerWindow::lower(b.mName);
        });

        for (auto it = journal->topicBegin(); it != journal->topicEnd(); ++it)
        {
            TopicData topic;
            topic.mId = it->first;
            topic.mName = it->second.getName().empty() ? it->first : it->second.getName();
            int topicOrder = 0;
            for (auto entry = it->second.begin(); entry != it->second.end(); ++entry, ++topicOrder)
            {
                EntryData data;
                data.mText = entry->getText();
                data.mActor = entry->mActorName;
                data.mOrder = topicOrder;
                topic.mEntries.push_back(data);
            }
            mTopics.push_back(topic);
        }
        std::sort(mTopics.begin(), mTopics.end(), [](const TopicData& a, const TopicData& b)
            { return QuestManagerWindow::lower(a.mName) < QuestManagerWindow::lower(b.mName); });

        std::reverse(mRecords.begin(), mRecords.end());
        rebuildFilters();
    }

    void QuestManagerWindow::rebuildFilters()
    {
        mFilters.clear();
        mQuestFilter->removeAllItems();
        FilterData all;
        all.mAxis = FilterData::All;
        all.mLabel = tr("questman.filter.all");
        mFilters.push_back(all);
        mQuestFilter->addItem(all.mLabel);

        std::set<std::string> addons, categories, factions;
        for (const QuestData& quest : mQuests)
        {
            addons.insert(quest.mAddon);
            categories.insert(quest.mCategory);
            if (!quest.mFaction.empty()) factions.insert(quest.mFaction);
        }
        const auto addFilters = [this](const std::set<std::string>& values, FilterData::Axis axis,
            const std::string& prefix)
        {
            for (const std::string& value : values)
            {
                FilterData filter;
                filter.mAxis = axis;
                filter.mValue = value;
                std::string label;
                if (axis == FilterData::Addon) label = labelForAddon(value);
                else if (axis == FilterData::Category) label = labelForCategory(value);
                else label = labelForFaction(value);
                filter.mLabel = prefix + ": " + label;
                mFilters.push_back(filter);
                mQuestFilter->addItem(filter.mLabel);
            }
        };
        addFilters(addons, FilterData::Addon, tr("questman.filter.addon"));
        addFilters(categories, FilterData::Category, tr("questman.filter.category"));
        addFilters(factions, FilterData::Faction, tr("questman.filter.faction"));
        mQuestFilter->setIndexSelected(0);
    }

    void QuestManagerWindow::refreshCurrentTab()
    {
        switch (mTabs->getIndexSelected())
        {
            case 0: refreshQuests(); break;
            case 1: refreshTopics(); break;
            case 2: refreshRecords(); break;
            default: refreshStats(); break;
        }
    }

    void QuestManagerWindow::refreshQuests()
    {
        const std::string needle = lower(mQuestSearch->getCaption());
        const std::size_t filterIndex = mQuestFilter->getIndexSelected();
        const FilterData* filter = filterIndex < mFilters.size() ? &mFilters[filterIndex] : nullptr;
        mVisibleQuests.clear();
        mQuestList->removeAllItems();

        for (std::size_t i = 0; i < mQuests.size(); ++i)
        {
            const QuestData& quest = mQuests[i];
            if (!mShowCompletedHiddenState && (quest.mCompleted || quest.mHidden))
                continue;
            if (filter && filter->mAxis != FilterData::All)
            {
                const std::string* field = filter->mAxis == FilterData::Addon ? &quest.mAddon
                    : filter->mAxis == FilterData::Category ? &quest.mCategory : &quest.mFaction;
                if (*field != filter->mValue)
                    continue;
            }
            bool matches = containsSearch(quest.mName, needle) || containsSearch(quest.mId, needle);
            if (!matches)
                for (const EntryData& entry : quest.mEntries)
                    if (containsSearch(entry.mText, needle)) { matches = true; break; }
            if (!matches)
                continue;

            std::string prefix;
            if (quest.mPinned) prefix += "* ";
            if (quest.mCompleted) prefix += "[" + tr("questman.completed_short") + "] ";
            if (quest.mHidden) prefix += "[" + tr("questman.hidden_short") + "] ";
            mQuestList->addItem(prefix + quest.mName);
            mVisibleQuests.push_back(i);
        }

        if (!mVisibleQuests.empty())
        {
            std::size_t select = 0;
            if (!mLastQuestId.empty())
            {
                for (std::size_t row = 0; row < mVisibleQuests.size(); ++row)
                {
                    const QuestData& candidate = mQuests[mVisibleQuests[row]];
                    bool match = lower(candidate.mId) == mLastQuestId;
                    if (!match)
                        for (const std::string& id : candidate.mIds)
                            if (lower(id) == mLastQuestId) { match = true; break; }
                    if (match) { select = row; break; }
                }
            }
            mQuestList->setIndexSelected(select);
        }
        updateQuestDetail();
    }

    void QuestManagerWindow::refreshTopics()
    {
        const std::string needle = lower(mTopicSearch->getCaption());
        mVisibleTopics.clear();
        mTopicList->removeAllItems();
        for (std::size_t i = 0; i < mTopics.size(); ++i)
        {
            const TopicData& topic = mTopics[i];
            bool matches = containsSearch(topic.mName, needle);
            if (!matches)
                for (const EntryData& entry : topic.mEntries)
                    if (containsSearch(entry.mText, needle)) { matches = true; break; }
            if (!matches) continue;
            mTopicList->addItem(topic.mName);
            mVisibleTopics.push_back(i);
        }
        if (!mVisibleTopics.empty())
        {
            std::size_t select = 0;
            if (!mLastTopicId.empty())
                for (std::size_t row = 0; row < mVisibleTopics.size(); ++row)
                    if (lower(mTopics[mVisibleTopics[row]].mId) == mLastTopicId)
                    { select = row; break; }
            mTopicList->setIndexSelected(select);
        }
        updateTopicDetail();
    }

    void QuestManagerWindow::refreshRecords()
    {
        const std::string needle = lower(mRecordSearch->getCaption());
        mVisibleRecords.clear();
        mRecordList->removeAllItems();
        for (std::size_t i = 0; i < mRecords.size(); ++i)
        {
            const RecordData& record = mRecords[i];
            if (!containsSearch(record.mTitle, needle) && !containsSearch(record.mEntry.mText, needle)
                && !containsSearch(record.mEntry.mActor, needle))
                continue;
            std::string label = record.mEntry.mDate;
            if (!label.empty()) label += " — ";
            label += truncate(record.mTitle, 46);
            mRecordList->addItem(label);
            mVisibleRecords.push_back(i);
        }
        if (!mVisibleRecords.empty()) mRecordList->setIndexSelected(0);
        updateRecordDetail();
    }

    void QuestManagerWindow::refreshStats()
    {
        int active = 0, completed = 0, hidden = 0, pinned = 0;
        std::map<std::string, int> addons, categories, factions;
        for (const QuestData& quest : mQuests)
        {
            if (quest.mCompleted) ++completed; else ++active;
            if (quest.mHidden) ++hidden;
            if (quest.mPinned) ++pinned;
            ++addons[quest.mAddon];
            ++categories[quest.mCategory];
            if (!quest.mFaction.empty()) ++factions[quest.mFaction];
        }
        std::ostringstream text;
        text << tr("questman.stats.active") << ": " << active << "\n";
        text << tr("questman.stats.completed") << ": " << completed << "\n";
        text << tr("questman.stats.pinned") << ": " << pinned << "\n";
        text << tr("questman.stats.hidden") << ": " << hidden << "\n";
        text << tr("questman.stats.topics") << ": " << mTopics.size() << "\n";
        text << tr("questman.stats.records") << ": " << mRecords.size() << "\n\n";
        text << tr("questman.filter.addon") << "\n";
        for (const auto& value : addons) text << "  " << labelForAddon(value.first) << ": " << value.second << "\n";
        text << "\n" << tr("questman.filter.category") << "\n";
        for (const auto& value : categories) text << "  " << labelForCategory(value.first) << ": " << value.second << "\n";
        if (!factions.empty())
        {
            text << "\n" << tr("questman.filter.faction") << "\n";
            for (const auto& value : factions) text << "  " << labelForFaction(value.first) << ": " << value.second << "\n";
        }
        mStatsDetail->setCaption(text.str());
    }

    void QuestManagerWindow::updateQuestDetail()
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleQuests.size())
        {
            mQuestHeading->setCaption(tr("questman.no_selection"));
            mQuestDetail->setCaption("");
            mQuestIcon->setImageTexture("");
            mPinButton->setEnabled(false);
            mHideButton->setEnabled(false);
            return;
        }
        QuestData& quest = mQuests[mVisibleQuests[selected]];
        mQuestHeading->setCaption(quest.mName);
        mQuestIcon->setImageTexture(quest.mIcon);
        std::ostringstream text;
        text << tr("questman.status") << ": " << (quest.mCompleted ? tr("questman.completed") : tr("questman.active")) << "\n";
        text << tr("questman.addon") << ": " << labelForAddon(quest.mAddon) << "\n";
        text << tr("questman.category") << ": " << labelForCategory(quest.mCategory) << "\n";
        if (!quest.mFaction.empty()) text << tr("questman.faction") << ": " << labelForFaction(quest.mFaction) << "\n";
        if (!quest.mGiver.empty()) text << tr("questman.giver") << ": " << quest.mGiver << "\n";
        text << "\n" << tr("questman.journal_entries") << "\n";
        if (quest.mEntries.empty()) text << tr("questman.no_entries") << "\n";
        for (const EntryData& entry : quest.mEntries)
        {
            if (!entry.mDate.empty()) text << "\n[" << entry.mDate << "]";
            if (!entry.mActor.empty()) text << "  " << entry.mActor;
            text << "\n" << sanitizeJournalText(entry.mText) << "\n";
        }
        if (getBool("technical info", false))
        {
            text << "\n" << tr("questman.technical") << "\n";
            text << "ID: " << join(quest.mIds, ", ") << "\n";
            text << "Stage: " << quest.mStage << "\n";
        }
        mQuestDetail->setCaption(text.str());
        updateQuestButtons();
    }

    void QuestManagerWindow::updateQuestButtons()
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleQuests.size()) return;
        const QuestData& quest = mQuests[mVisibleQuests[selected]];
        mPinButton->setEnabled(true);
        mHideButton->setEnabled(true);
        mPinButton->setCaption(quest.mPinned ? tr("questman.unpin") : tr("questman.pin"));
        mHideButton->setCaption(quest.mHidden ? tr("questman.unhide") : tr("questman.hide"));
    }

    void QuestManagerWindow::updateTopicDetail()
    {
        const std::size_t selected = mTopicList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleTopics.size())
        {
            mTopicHeading->setCaption(tr("questman.no_selection"));
            mTopicDetail->setCaption("");
            return;
        }
        const TopicData& topic = mTopics[mVisibleTopics[selected]];
        mTopicHeading->setCaption(topic.mName);
        std::ostringstream text;
        for (const EntryData& entry : topic.mEntries)
        {
            if (!entry.mActor.empty()) text << entry.mActor << ":\n";
            text << sanitizeJournalText(entry.mText) << "\n\n";
        }
        mTopicDetail->setCaption(text.str());
    }

    void QuestManagerWindow::updateRecordDetail()
    {
        const std::size_t selected = mRecordList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleRecords.size())
        {
            mRecordHeading->setCaption(tr("questman.no_selection"));
            mRecordDetail->setCaption("");
            return;
        }
        const RecordData& record = mRecords[mVisibleRecords[selected]];
        mRecordHeading->setCaption(record.mTitle);
        std::ostringstream text;
        if (!record.mEntry.mDate.empty()) text << record.mEntry.mDate << "\n";
        if (!record.mEntry.mActor.empty()) text << record.mEntry.mActor << "\n";
        if (!record.mEntry.mDate.empty() || !record.mEntry.mActor.empty()) text << "\n";
        text << sanitizeJournalText(record.mEntry.mText);
        if (getBool("technical info", false)) text << "\n\n" << tr("questman.technical") << "\nTopic: " << record.mTopic;
        mRecordDetail->setCaption(text.str());
    }

    void QuestManagerWindow::notifyClose(MyGUI::Widget*)
    {
        setVisible(false);
        if (mReturnToJournal)
            mReturnToJournal();
    }

    void QuestManagerWindow::notifyTabChanged(MyGUI::TabControl*, std::size_t)
    {
        refreshCurrentTab();
    }

    void QuestManagerWindow::notifySearchChanged(MyGUI::EditBox*)
    {
        refreshCurrentTab();
    }

    void QuestManagerWindow::notifyFilterChanged(MyGUI::ComboBox*, std::size_t)
    {
        refreshQuests();
    }

    void QuestManagerWindow::notifyShowCompletedHidden(MyGUI::Widget*)
    {
        mShowCompletedHiddenState = !mShowCompletedHiddenState;
        Settings::Manager::setBool("show completed hidden", "Questman", mShowCompletedHiddenState);
        mShowCompletedHidden->setStateSelected(mShowCompletedHiddenState);
        refreshQuests();
    }

    void QuestManagerWindow::notifyQuestSelected(MyGUI::ListBox*, std::size_t)
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected != MyGUI::ITEM_NONE && selected < mVisibleQuests.size())
        {
            mLastQuestId = lower(mQuests[mVisibleQuests[selected]].mId);
            Settings::Manager::setString("last quest", "Questman", mLastQuestId);
        }
        updateQuestDetail();
    }

    void QuestManagerWindow::notifyTopicSelected(MyGUI::ListBox*, std::size_t)
    {
        const std::size_t selected = mTopicList->getIndexSelected();
        if (selected != MyGUI::ITEM_NONE && selected < mVisibleTopics.size())
        {
            mLastTopicId = lower(mTopics[mVisibleTopics[selected]].mId);
            Settings::Manager::setString("last topic", "Questman", mLastTopicId);
        }
        updateTopicDetail();
    }

    void QuestManagerWindow::notifyRecordSelected(MyGUI::ListBox*, std::size_t)
    {
        updateRecordDetail();
    }

    void QuestManagerWindow::notifyPin(MyGUI::Widget*)
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleQuests.size()) return;
        QuestData& quest = mQuests[mVisibleQuests[selected]];
        const std::string key = lower(quest.mId);
        quest.mPinned = !quest.mPinned;
        if (quest.mPinned) mPinned.insert(key); else mPinned.erase(key);
        writeIdSet("pinned quests", mPinned);
        refreshQuests();
    }

    void QuestManagerWindow::notifyHide(MyGUI::Widget*)
    {
        const std::size_t selected = mQuestList->getIndexSelected();
        if (selected == MyGUI::ITEM_NONE || selected >= mVisibleQuests.size()) return;
        QuestData& quest = mQuests[mVisibleQuests[selected]];
        const std::string key = lower(quest.mId);
        quest.mHidden = !quest.mHidden;
        if (quest.mHidden) mHidden.insert(key); else mHidden.erase(key);
        writeIdSet("hidden quests", mHidden);
        refreshQuests();
    }
}
