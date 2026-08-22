#ifndef OPENMW_MWGUI_QUESTMANAGER_H
#define OPENMW_MWGUI_QUESTMANAGER_H

#include "windowbase.hpp"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace MyGUI
{
    class Button;
    class ComboBox;
    class EditBox;
    class ImageBox;
    class ListBox;
    class TabControl;
    class TextBox;
}

namespace MWGui
{
    /// Native C++ interpretation of Questman for ArenaMW.
    /// It reads the engine journal directly and therefore does not require Lua/OpenMW 0.50 APIs.
    class QuestManagerWindow final : public WindowModal
    {
    public:
        explicit QuestManagerWindow(std::function<void()> returnToJournal);

        void onOpen() override;
        bool exit() override;
        void clear() override;

    private:
        struct EntryData
        {
            std::string mText;
            std::string mActor;
            std::string mDate;
            int mOrder = -1;
        };

        struct QuestData
        {
            std::string mId;
            std::string mName;
            std::vector<std::string> mIds;
            std::vector<EntryData> mEntries;
            bool mCompleted = false;
            bool mPinned = false;
            bool mHidden = false;
            int mStage = 0;
            int mLastOrder = -1;
            std::string mAddon;
            std::string mCategory;
            std::string mFaction;
            std::string mGiver;
            std::string mIcon;
        };

        struct TopicData
        {
            std::string mId;
            std::string mName;
            std::vector<EntryData> mEntries;
        };

        struct RecordData
        {
            std::string mTopic;
            std::string mTitle;
            EntryData mEntry;
        };

        struct FilterData
        {
            enum Axis { All, Addon, Category, Faction } mAxis = All;
            std::string mValue;
            std::string mLabel;
        };

        struct Metadata
        {
            std::string mAddon;
            std::string mCategory;
            std::string mFaction;
            std::string mIcon;
        };

        void rebuildData();
        void rebuildQuestGivers();
        void rebuildFilters();
        void refreshCurrentTab();
        void refreshQuests();
        void refreshTopics();
        void refreshRecords();
        void refreshStats();
        void updateQuestDetail();
        void updateTopicDetail();
        void updateRecordDetail();
        void updateQuestButtons();

        void notifyClose(MyGUI::Widget* sender);
        void notifyTabChanged(MyGUI::TabControl* sender, std::size_t index);
        void notifySearchChanged(MyGUI::EditBox* sender);
        void notifyFilterChanged(MyGUI::ComboBox* sender, std::size_t index);
        void notifyShowCompletedHidden(MyGUI::Widget* sender);
        void notifyQuestSelected(MyGUI::ListBox* sender, std::size_t index);
        void notifyTopicSelected(MyGUI::ListBox* sender, std::size_t index);
        void notifyRecordSelected(MyGUI::ListBox* sender, std::size_t index);
        void notifyPin(MyGUI::Widget* sender);
        void notifyHide(MyGUI::Widget* sender);

        static Metadata classifyQuest(const std::string& id);
        static std::string lower(const std::string& value);
        static bool containsSearch(const std::string& haystack, const std::string& needle);
        static std::set<std::string> readIdSet(const char* setting);
        static void writeIdSet(const char* setting, const std::set<std::string>& values);
        static bool getBool(const char* setting, bool fallback);
        static std::string tr(const std::string& key);
        static std::string truncate(const std::string& value, std::size_t maxChars);
        static std::string join(const std::vector<std::string>& values, const std::string& separator);
        static std::string questIconFor(const Metadata& meta);
        static std::string labelForAddon(const std::string& value);
        static std::string labelForCategory(const std::string& value);
        static std::string labelForFaction(const std::string& value);

        std::function<void()> mReturnToJournal;

        MyGUI::TextBox* mTitle = nullptr;
        MyGUI::TabControl* mTabs = nullptr;
        MyGUI::EditBox* mQuestSearch = nullptr;
        MyGUI::ComboBox* mQuestFilter = nullptr;
        MyGUI::Button* mShowCompletedHidden = nullptr;
        MyGUI::ListBox* mQuestList = nullptr;
        MyGUI::ImageBox* mQuestIcon = nullptr;
        MyGUI::TextBox* mQuestHeading = nullptr;
        MyGUI::EditBox* mQuestDetail = nullptr;
        MyGUI::Button* mPinButton = nullptr;
        MyGUI::Button* mHideButton = nullptr;
        MyGUI::EditBox* mTopicSearch = nullptr;
        MyGUI::ListBox* mTopicList = nullptr;
        MyGUI::TextBox* mTopicHeading = nullptr;
        MyGUI::EditBox* mTopicDetail = nullptr;
        MyGUI::EditBox* mRecordSearch = nullptr;
        MyGUI::ListBox* mRecordList = nullptr;
        MyGUI::TextBox* mRecordHeading = nullptr;
        MyGUI::EditBox* mRecordDetail = nullptr;
        MyGUI::EditBox* mStatsDetail = nullptr;
        MyGUI::Button* mCloseButton = nullptr;

        std::vector<QuestData> mQuests;
        std::vector<TopicData> mTopics;
        std::vector<RecordData> mRecords;
        std::vector<FilterData> mFilters;
        std::map<std::string, std::string> mQuestGivers;
        std::map<std::string, int> mQuestGiverStages;
        std::vector<std::size_t> mVisibleQuests;
        std::vector<std::size_t> mVisibleTopics;
        std::vector<std::size_t> mVisibleRecords;
        std::set<std::string> mPinned;
        std::set<std::string> mHidden;
        bool mShowCompletedHiddenState = false;
        std::string mLastQuestId;
        std::string mLastTopicId;
    };
}

#endif
