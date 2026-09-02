#ifndef OPENMW_GAME_MWGUI_HUD_H
#define OPENMW_GAME_MWGUI_HUD_H

#include "mapwindow.hpp"
#include "statswatcher.hpp"

#include "../mwworld/ptr.hpp"

#include <vector>

namespace MWWorld
{
    class Ptr;
}

namespace MWGui
{
    class DragAndDrop;
    class SpellIcons;
    class ItemWidget;
    class SpellWidget;

    class HUD : public WindowBase, public LocalMapBase, public StatsListener
    {
    public:
        HUD(CustomMarkerCollection& customMarkers, DragAndDrop* dragAndDrop, MWRender::LocalMap* localMapRender);
        virtual ~HUD();
        void setValue (const std::string& id, const MWMechanics::DynamicStat<float>& value) override;

        /// Set time left for the player to start drowning
        /// @param time time left to start drowning
        /// @param maxTime how long we can be underwater (in total) until drowning starts
        void setDrowningTimeLeft(float time, float maxTime);
        void setDrowningBarVisible(bool visible);

        void setHmsVisible(bool visible);
        void setWeapVisible(bool visible);
        void setSpellVisible(bool visible);
        void setSneakVisible(bool visible);

        void setEffectVisible(bool visible);
        void setMinimapVisible(bool visible);

        void setSelectedSpell(const std::string& spellId, int successChancePercent);
        void setSelectedEnchantItem(const MWWorld::Ptr& item, int chargePercent);
        const MWWorld::Ptr& getSelectedEnchantItem();
        void setSelectedWeapon(const MWWorld::Ptr& item, int durabilityPercent);
        void unsetSelectedSpell();
        void unsetSelectedWeapon();

        void setCrosshairVisible(bool visible);
        void setCrosshairOwned(bool owned);

        void onFrame(float dt) override;
        void onResChange(int width, int height) override;

        void setCellName(const std::string& cellName);

        /// Update both the rotating minimap marker and the native horizontal compass.
        void setPlayerDir(float x, float y);

        bool getWorldMouseOver() { return mWorldMouseOver; }

        MyGUI::Widget* getEffectBox() { return mEffectBox; }

        void setEnemy(const MWWorld::Ptr& enemy);
        void resetEnemy();
        void setFocusObject(const MWWorld::Ptr& focus);
        void setFocusObjectScreenCoords(float min_x, float min_y, float max_x, float max_y);

        void clear() override;

    private:
        MyGUI::Widget* mGameplayHud;
        MyGUI::ProgressBar *mHealth, *mMagicka, *mStamina, *mEnemyHealth, *mDrowning;
        MyGUI::TextBox *mHealthText, *mMagickaText, *mStaminaText, *mFpsBox;
        MyGUI::TextBox *mEnemyName, *mEnemySummary;
        MyGUI::Widget *mHealthFrame, *mMagickaFrame, *mFatigueFrame;
        MyGUI::Widget *mWeapBox, *mSpellBox, *mSneakBox;
        ItemWidget *mWeapImage;
        SpellWidget *mSpellImage;
        MyGUI::ProgressBar *mWeapStatus, *mSpellStatus;
        MyGUI::Widget *mEffectBox, *mMinimapBox;
        MyGUI::Button* mMinimapButton;
        MyGUI::ScrollView* mMinimap;
        MyGUI::ImageBox* mCrosshair;
        MyGUI::Widget* mCellNameClip;
        MyGUI::TextBox* mCellNameBox;
        MyGUI::TextBox* mWeaponSpellBox;
        MyGUI::TextBox* mGameTimeBox;
        MyGUI::Widget* mHorizontalCompass;
        MyGUI::TextBox* mHorizontalCompassCenter;
        std::vector<MyGUI::TextBox*> mHorizontalCompassTicks;

        struct HorizontalCompassMarkerState
        {
            MyGUI::TextBox* mWidget = nullptr;
            MWWorld::Ptr mObject;
            std::string mIdentity;
            float mCurrentLeft = 0.f;
            float mTargetLeft = 0.f;
            float mAlpha = 0.f;
            float mTargetAlpha = 0.f;
            bool mSeen = false;
        };

        std::vector<HorizontalCompassMarkerState> mHorizontalCompassMarkers;

        struct CombatHealthBarState
        {
            MyGUI::ProgressBar* mWidget = nullptr;
            // X025: name caption. It belongs to the docked presentation only, so it
            // lives next to the bar rather than inside it and is faded in separately.
            MyGUI::TextBox* mName = nullptr;
            MWWorld::Ptr mActor;
            bool mAlly = false;
            // Skin the widget is actually wearing right now. Kept apart from mAlly:
            // clearing a slot resets mAlly but must not claim the widget went back to
            // the red skin, otherwise the next enemy inherits a green bar.
            bool mSkinAlly = false;

            // X024: presentation state. The raw per-frame anchor produced by
            // getObjectScreenBounds follows the animated bounding box, so a running
            // NPC made the bar jump up and down every stride. Everything below is
            // smoothed in screen space and faded in time, so a bar never appears,
            // moves or disappears instantly.
            bool mHasScreenState = false;
            float mCentreX = 0.f;       // smoothed bar centre, pixels
            float mCentreY = 0.f;
            float mWidth = 0.f;         // smoothed bar size, pixels
            float mHeight = 0.f;
            float mAlpha = 0.f;
            float mTargetAlpha = 0.f;
            float mDisplayHealth = -1.f;
            float mLingerTimer = 0.f;   // grace before a lost bar starts fading

            // X025: docking. Inside the melee radius the bar leaves the actor's head
            // and joins a stack above the stamina bar; outside it, it goes back over
            // the head. mDockBlend is the animated 0..1 position between the two.
            bool mDocked = false;
            float mDockBlend = 0.f;
            float mDockSwitchTimer = 0.f;   // dwell before a dock/undock is honoured
            unsigned int mDockSequence = 0; // join order, decides the row
            int mDockRow = -1;
            std::string mNameCaption;

            // X025: per-frame scratch filled by pass 1 of updateCombatHealthBars and
            // consumed by pass 3. Meaningless outside that function.
            bool mFrameResolved = false;
            bool mFrameDrop = false;
            float mFrameDistance = 0.f;
            float mFrameAlpha = 1.f;
            float mHeadCentreX = 0.f;
            float mHeadCentreY = 0.f;
            float mHeadWidth = 0.f;
            float mHeadHeight = 0.f;
        };

        std::vector<CombatHealthBarState> mCombatHealthBars;
        float mCombatHealthBarScanTimer = 0.f;
        // Monotonic counter handing out mDockSequence, so stack rows keep their order.
        unsigned int mCombatDockSequenceCounter = 0;

        MyGUI::Widget *mDrowningFrame, *mDrowningFlash;

        // bottom left elements
        int mHealthManaStaminaBaseLeft, mWeapBoxBaseLeft, mSpellBoxBaseLeft, mSneakBoxBaseLeft;
        // bottom right elements
        int mMinimapBoxBaseRight, mEffectBoxBaseRight;

        DragAndDrop* mDragAndDrop;

        std::string mCellName;
        float mCellNameTimer;
        float mCellNameScrollOffset;
        float mCellNameScrollPause;
        int mCellNameScrollDirection;
        bool mCellNameScrolling;

        std::string mWeaponName;
        std::string mSpellName;
        float mWeaponSpellTimer;
        float mGameTimeUpdateTimer;
        std::string mGameTimeCaption;
        bool mGameTimeShowingCellName;

        bool mMapVisible;
        bool mMinimapBaseVisible;
        bool mEffectBaseVisible;
        bool mCrosshairBaseVisible;
        bool mWeaponVisible;
        bool mSpellVisible;

        bool mWorldMouseOver;
        float mHorizontalCompassAngle;
        bool mHorizontalCompassDirty;
        float mHorizontalCompassMarkerTimer;

        SpellIcons* mSpellIcons;

        int mEnemyActorId;
        float mEnemyHealthTimer;
        MWWorld::Ptr mFocusActor;
        float mFocusActorScreenX;
        float mFocusActorScreenY;
        float mFocusActorDistance;
        float mFocusActorPanelAlpha;
        float mTargetPanelCenterX;
        bool mFocusActorCurrentlyFaced;
        bool mTargetPanelPositionInitialized;

        float mFpsUpdateTimer;
        float mFpsAccumulatedTime;
        int mFpsFrameCount;

        bool  mIsDrowning;
        float mDrowningFlashTheta;


        struct AutoHideBarState
        {
            int current = 0;
            int modified = 0;
            float idleTimer = 0.f;
            float alpha = 1.f;
            bool initialized = false;
        };

        AutoHideBarState mHealthBarState;
        AutoHideBarState mMagickaBarState;
        AutoHideBarState mStaminaBarState;
        bool mHmsBaseVisible;

        void registerBarChange(AutoHideBarState& state, int current, int modified);
        void updateAutoHideBar(MyGUI::Widget* frame, AutoHideBarState& state, float dt, bool forceVisible,
            MyGUI::Widget* persistentIcon = nullptr);
        void applyBarAlpha(MyGUI::Widget* widget, float alpha);

        void onWorldClicked(MyGUI::Widget* _sender);
        void onWorldMouseOver(MyGUI::Widget* _sender, int x, int y);
        void onWorldMouseLostFocus(MyGUI::Widget* _sender, MyGUI::Widget* _new);
        void onHMSClicked(MyGUI::Widget* _sender);
        void onWeaponClicked(MyGUI::Widget* _sender);
        void onMagicClicked(MyGUI::Widget* _sender);
        void onMapClicked(MyGUI::Widget* _sender);

        // LocalMapBase
        void customMarkerCreated(MyGUI::Widget* marker) override;
        void doorMarkerCreated(MyGUI::Widget* marker) override;

        void updateEnemyHealthBar();
        void updateFocusedTargetPanel(float dt);
        void updateCombatHealthBars(float dt);
        void hideCombatHealthBars();
        // X024: drive one slot's opacity/geometry towards its target and push the
        // result into the widget. Called for both live and fading-out slots.
        // X025: also places and fades the name caption that rides on the bar.
        void applyCombatHealthBar(CombatHealthBarState& state, float dt);

        void updatePositions();
        void updateGameTimeAndCellName(float dt);
        void updateHorizontalCompass();
        void updateHorizontalCompassMarkers(float dt);
    };
}

#endif
