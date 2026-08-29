#include "hud.hpp"

#include <osg/Math>

#include <MyGUI_RenderManager.h>
#include <MyGUI_ProgressBar.h>
#include <MyGUI_Button.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <utility>



#include <components/settings/settings.hpp>
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "inventorywindow.hpp"
#include "spellicons.hpp"
#include "itemmodel.hpp"
#include "draganddrop.hpp"

#include "itemwidget.hpp"

namespace
{
    // The location name temporarily replaces the clock below the compass.
    // Only the time animates: fade it out, show the location steadily, then fade time back in.
    constexpr float sCompassInfoFadeDuration = 0.35f;
    constexpr float sCompassLocationHoldDuration = 4.f;
    constexpr float sCompassInfoSequenceDuration
        = sCompassInfoFadeDuration * 2.f + sCompassLocationHoldDuration;

    std::string getWeaponSpellBoxMode()
    {
        const auto modeKey = std::make_pair(std::string("GUI"), std::string("weapon spell box mode"));
        const auto legacyKey = std::make_pair(std::string("GUI"), std::string("persistent weapon spell boxes"));
        if (Settings::Manager::mUserSettings.find(modeKey) == Settings::Manager::mUserSettings.end())
        {
            const auto legacyIt = Settings::Manager::mUserSettings.find(legacyKey);
            if (legacyIt != Settings::Manager::mUserSettings.end())
                return (legacyIt->second == "false" || legacyIt->second == "0") ? "hidden" : "transparent";
        }

        const std::string mode = Settings::Manager::getString("weapon spell box mode", "GUI");
        if (mode == "hidden" || mode == "transparent" || mode == "visible")
            return mode;
        return Settings::Manager::getBool("persistent weapon spell boxes", "GUI")
            ? "transparent" : "hidden";
    }

    std::string getResourceBarMode()
    {
        const std::string mode = Settings::Manager::getString("resource bars mode", "HUD");
        if (mode == "always" || mode == "automatic" || mode == "hidden")
            return mode;
        return Settings::Manager::getBool("auto hide resource bars", "GUI")
            ? "automatic" : "always";
    }

    std::string getNpcBarMode()
    {
        // Settings::Manager throws for keys that are absent from settings-default.cfg,
        // and this runs once per frame. Never let a missing key take the HUD down.
        std::string mode;
        try
        {
            mode = Settings::Manager::getString("npc bar mode", "HUD");
        }
        catch (const std::exception&)
        {
            mode.clear();
        }

        if (mode == "off" || mode == "combat" || mode == "hover" || mode == "both")
            return mode;

        try
        {
            return Settings::Manager::getBool("target info panel", "GUI") ? "both" : "combat";
        }
        catch (const std::exception&)
        {
            return "both";
        }
    }

    /// Exterior cells stream their neighbours in, so a fight legitimately spans more
    /// than one cell. Only interiors are a hard boundary for overhead bars.
    bool sharesCombatSpace(const MWWorld::Ptr& actor, const MWWorld::Ptr& player)
    {
        if (!actor.isInCell() || !player.isInCell())
            return false;
        if (actor.getCell() == player.getCell())
            return true;
        return actor.getCell()->isExterior() && player.getCell()->isExterior();
    }

    // X024/X025 combat bar presentation constants.
    //
    // X025 replaced the X024 "lowered panel" behaviour with a docked stack above
    // the stamina bar; the sLowered*/sFront* constants are gone with it.
    //
    // The bar used to be placed straight from getObjectScreenBounds every frame.
    // That box is animated, so a running NPC bobbed the bar by several pixels per
    // stride, and every "not visible right now" branch hid the widget outright,
    // which read as a hard blink whenever line of sight or the 10 Hz participant
    // scan flickered. Both are now filtered.
    namespace CombatBar
    {
        // Horizontal tracking has to stay tight or the bar lags behind a strafing
        // target; vertical is filtered much harder because that is where the run
        // animation bob lives.
        constexpr float sSmoothTauX = 0.055f;
        constexpr float sSmoothTauY = 0.140f;
        constexpr float sSmoothTauSize = 0.200f;
        // The head/stack transition is deliberately slow so it reads as the bar
        // travelling into place rather than snapping to a second position.
        constexpr float sSmoothTauDock = 0.280f;
        constexpr float sSmoothTauHealth = 0.220f;

        constexpr float sFadeInTime = 0.12f;
        constexpr float sFadeOutTime = 0.30f;
        // How long a bar keeps its last good position after the actor stops being
        // resolvable. Covers the 100 ms scan period plus a short LOS flicker.
        constexpr float sLingerTime = 0.35f;

        // Beyond this jump the smoothing is skipped: a camera cut or a teleport
        // must not drag the bar across the whole screen.
        constexpr float sSnapFraction = 0.35f;

        // ------------------------------------------------------------------
        // X025 distances. Everything is expressed in paces and converted once,
        // so the thresholds below can be read straight off the design.
        // A pace is taken as one metre; OpenMW's world scale is ~70 units/metre.
        // ------------------------------------------------------------------
        constexpr float sUnitsPerStep = 70.f;

        constexpr float sDockEnterSteps = 15.f;  // closer than this -> docked stack
        constexpr float sDockExitSteps = 18.f;   // hysteresis, must exceed the above
        constexpr float sVanishSteps = 40.f;     // beyond this the bar is gone
        constexpr float sFullSizeSteps = 3.5f;   // no shrinking closer than this

        constexpr float sDockEnterDistance = sDockEnterSteps * sUnitsPerStep;
        constexpr float sDockExitDistance = sDockExitSteps * sUnitsPerStep;
        constexpr float sVanishDistance = sVanishSteps * sUnitsPerStep;
        constexpr float sFullSizeDistance = sFullSizeSteps * sUnitsPerStep;
        // The last stretch before vanishing is spent fading, so a bar never pops
        // out of existence at exactly 40 paces.
        constexpr float sFadeOutStartDistance = sVanishDistance * 0.90f;

        // Overhead bar geometry at full size, and how small it is allowed to get
        // just before it disappears. Deliberately small: the overhead bar is a
        // glance-value readout, the docked stack is where the detail lives.
        constexpr float sHeadWidthMax = 92.f;
        constexpr float sHeadHeightMax = 7.f;
        constexpr float sHeadScaleMin = 0.28f;
        // Gap between the top of the actor's hit box and the bar. Kept tight so
        // the bar reads as belonging to that actor and not as floating above the
        // scene; the second term shrinks the gap along with the bar itself.
        constexpr float sHeadClearance = 2.f;
        // Smallest the widget is ever drawn at. Has to stay below the size the
        // distance ramp produces at the vanishing distance, otherwise the bar
        // would stop shrinking early and the falloff would look truncated.
        constexpr int sHeadMinWidthPixels = 22;
        constexpr int sHeadMinHeightPixels = 2;
        constexpr int sScreenMargin = 6;

        // ------------------------------------------------------------------
        // X025 docked stack: a fixed-width list that grows upwards from just
        // above the stamina bar. One row is a name with its bar underneath.
        // ------------------------------------------------------------------
        constexpr int sDockWidth = 196;
        constexpr int sDockBarHeight = 9;
        constexpr int sDockNameHeight = 18;
        constexpr int sDockNameFontHeight = 16;
        constexpr int sDockNameGap = 2;      // between the caption and its bar
        constexpr int sDockRowStride = 31;   // name + gap + bar + breathing room
        constexpr int sDockGap = 10;         // between the stack and the stamina bar
        constexpr std::size_t sDockMaxRows = 5;

        // An actor has to hold the new side of the threshold this long before the
        // bar actually moves. Without it an enemy circling at ~15 paces would make
        // its bar hop between the head and the stack.
        constexpr float sDockSwitchDelay = 0.25f;
        // The name only becomes readable once the bar is essentially in the stack.
        constexpr float sNameFadeStart = 0.55f;

        // Progress bars are driven at a fixed resolution so a smoothed value is
        // still visible on an actor with very few hit points.
        constexpr int sProgressResolution = 1000;

        inline float clamp01(float value)
        {
            return std::max(0.f, std::min(1.f, value));
        }

        /// Framerate-independent exponential approach.
        inline float approach(float current, float target, float tau, float dt)
        {
            if (tau <= 0.f || dt <= 0.f)
                return target;
            return current + (target - current) * (1.f - std::exp(-dt / tau));
        }

        inline float lerp(float from, float to, float t)
        {
            return from + (to - from) * t;
        }
    }

    bool npcBarShowsHover(const std::string& mode)
    {
        return mode == "hover" || mode == "both";
    }

    bool npcBarShowsCombat(const std::string& mode)
    {
        return mode == "combat" || mode == "both";
    }

    enum class HorizontalCompassMarkerKind
    {
        Enemy = 0,
        Ally = 1,
        Door = 2,
        DetectKey = 3,
        DetectEnchantment = 4,
        DetectCreature = 5
    };

    struct HorizontalCompassMarkerCandidate
    {
        MWWorld::Ptr mObject;
        std::string mIdentity;
        int mLeft = 0;
        float mDistanceSquared = 0.f;
        HorizontalCompassMarkerKind mKind = HorizontalCompassMarkerKind::Ally;
    };

    MyGUI::Colour getHorizontalCompassMarkerColour(HorizontalCompassMarkerKind kind)
    {
        switch (kind)
        {
            case HorizontalCompassMarkerKind::Enemy:
                return MyGUI::Colour(1.00f, 0.18f, 0.16f);
            case HorizontalCompassMarkerKind::Ally:
                return MyGUI::Colour(0.14f, 0.74f, 0.22f);
            case HorizontalCompassMarkerKind::Door:
            case HorizontalCompassMarkerKind::DetectKey:
            case HorizontalCompassMarkerKind::DetectEnchantment:
            case HorizontalCompassMarkerKind::DetectCreature:
                return MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}"));
        }
        return MyGUI::Colour(1.f, 1.f, 1.f);
    }

    std::string getHorizontalCompassMarkerCaption(HorizontalCompassMarkerKind kind)
    {
        switch (kind)
        {
            case HorizontalCompassMarkerKind::Door:
                return "⌂";
            case HorizontalCompassMarkerKind::DetectKey:
                return "○─";
            case HorizontalCompassMarkerKind::DetectEnchantment:
                return "✦";
            case HorizontalCompassMarkerKind::DetectCreature:
                return "△";
            case HorizontalCompassMarkerKind::Enemy:
            case HorizontalCompassMarkerKind::Ally:
                return "●";
        }
        return "●";
    }
}

namespace MWGui
{

    /**
     * Makes it possible to use ItemModel::moveItem to move an item from an inventory to the world.
     */
    class WorldItemModel : public ItemModel
    {
    public:
        WorldItemModel(float left, float top) : mLeft(left), mTop(top) {}
        virtual ~WorldItemModel() override {}
        MWWorld::Ptr copyItem (const ItemStack& item, size_t count, bool /*allowAutoEquip*/) override
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();

            MWWorld::Ptr dropped;
            if (world->canPlaceObject(mLeft, mTop))
                dropped = world->placeObject(item.mBase, mLeft, mTop, count);
            else
                dropped = world->dropObjectOnGround(world->getPlayerPtr(), item.mBase, count);
            dropped.getCellRef().setOwner("");





            return dropped;
        }

        void removeItem (const ItemStack& item, size_t count) override { throw std::runtime_error("removeItem not implemented"); }
        ModelIndex getIndex (ItemStack item) override { throw std::runtime_error("getIndex not implemented"); }
        void update() override {}
        size_t getItemCount() override { return 0; }
        ItemStack getItem (ModelIndex index) override { throw std::runtime_error("getItem not implemented"); }

    private:
        // Where to drop the item
        float mLeft;
        float mTop;
    };


    HUD::HUD(CustomMarkerCollection &customMarkers, DragAndDrop* dragAndDrop, MWRender::LocalMap* localMapRender)
        : WindowBase("openmw_hud.layout")
        , LocalMapBase(customMarkers, localMapRender, Settings::Manager::getBool("local map hud fog of war", "Map"))
        , mGameplayHud(nullptr)
        , mHealth(nullptr)
        , mMagicka(nullptr)
        , mStamina(nullptr)
        , mDrowning(nullptr)
        , mHealthText(nullptr)
        , mMagickaText(nullptr)
        , mStaminaText(nullptr)
        , mFpsBox(nullptr)
        , mEnemyName(nullptr)
        , mEnemySummary(nullptr)
        , mWeapImage(nullptr)
        , mSpellImage(nullptr)
        , mWeapStatus(nullptr)
        , mSpellStatus(nullptr)
        , mEffectBox(nullptr)
        , mMinimap(nullptr)
        , mCrosshair(nullptr)
        , mCellNameClip(nullptr)
        , mCellNameBox(nullptr)
        , mGameTimeBox(nullptr)
        , mHorizontalCompass(nullptr)
        , mHorizontalCompassCenter(nullptr)
        , mDrowningFrame(nullptr)
        , mDrowningFlash(nullptr)
        , mHealthManaStaminaBaseLeft(0)
        , mWeapBoxBaseLeft(0)
        , mSpellBoxBaseLeft(0)
        , mMinimapBoxBaseRight(0)
        , mEffectBoxBaseRight(0)
        , mDragAndDrop(dragAndDrop)
        , mCellNameTimer(0.0f)
        , mCellNameScrollOffset(0.f)
        , mCellNameScrollPause(0.f)
        , mCellNameScrollDirection(-1)
        , mCellNameScrolling(false)
        , mWeaponSpellTimer(0.f)
        , mGameTimeUpdateTimer(0.f)
        , mGameTimeCaption("00:00")
        , mGameTimeShowingCellName(false)
        , mMapVisible(true)
        , mMinimapBaseVisible(true)
        , mEffectBaseVisible(true)
        , mCrosshairBaseVisible(true)
        , mWeaponVisible(true)
        , mSpellVisible(true)
        , mWorldMouseOver(false)
        , mHorizontalCompassAngle(0.f)
        , mHorizontalCompassDirty(true)
        , mHorizontalCompassMarkerTimer(0.f)
        , mEnemyActorId(-1)
        , mEnemyHealthTimer(-1)
        , mFocusActorScreenX(0.5f)
        , mFocusActorScreenY(0.f)
        , mFocusActorDistance(-1.f)
        , mFocusActorPanelAlpha(0.f)
        , mTargetPanelCenterX(0.f)
        , mFocusActorCurrentlyFaced(false)
        , mTargetPanelPositionInitialized(false)
        , mFpsUpdateTimer(0.f)
        , mFpsAccumulatedTime(0.f)
        , mFpsFrameCount(0)
        , mIsDrowning(false)
        , mDrowningFlashTheta(0.f)
        , mHmsBaseVisible(true)
    {
        mMainWidget->setSize(MyGUI::RenderManager::getInstance().getViewSize());
        getWidget(mGameplayHud, "GameplayHud");

        // Do not expose the gameplay HUD while a new single-player character is
        // still being registered. Character-generation windows are separate widgets.
        mGameplayHud->setVisible(false);

        // Energy bars
        getWidget(mHealthFrame, "HealthFrame");
        getWidget(mMagickaFrame, "MagickaFrame");
        getWidget(mFatigueFrame, "FatigueFrame");
        getWidget(mHealth, "Health");
        getWidget(mMagicka, "Magicka");
        getWidget(mStamina, "Stamina");
        getWidget(mEnemyHealth, "EnemyHealth");
        getWidget(mEnemyName, "EnemyName");
        getWidget(mEnemySummary, "EnemySummary");

        // X014: fixed widget pool for world-space combat health bars. Reusing
        // widgets avoids GUI allocations while fights are running.
        constexpr std::size_t combatHealthBarPoolSize = 24;
        mCombatHealthBars.reserve(combatHealthBarPoolSize);
        for (std::size_t i = 0; i < combatHealthBarPoolSize; ++i)
        {
            CombatHealthBarState state;
            state.mWidget = mGameplayHud->createWidget<MyGUI::ProgressBar>(
                "MW_Progress_Red", MyGUI::IntCoord(0, 0, 118, 9),
                MyGUI::Align::Left | MyGUI::Align::Top,
                "CombatHealthBar" + MyGUI::utility::toString(i));
            state.mWidget->setNeedMouseFocus(false);
            state.mWidget->setVisible(false);

            // X025: one caption per slot, created here so no layout file has to
            // change. It is only shown while the bar sits in the docked stack.
            state.mName = mGameplayHud->createWidget<MyGUI::TextBox>(
                "SandBrightText",
                MyGUI::IntCoord(0, 0, CombatBar::sDockWidth, CombatBar::sDockNameHeight),
                MyGUI::Align::Left | MyGUI::Align::Top,
                "CombatHealthBarName" + MyGUI::utility::toString(i));
            state.mName->setNeedMouseFocus(false);
            state.mName->setFontHeight(CombatBar::sDockNameFontHeight);
            state.mName->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
            state.mName->setVisible(false);

            mCombatHealthBars.push_back(state);
        }

        getWidget(mHealthText, "HealthText");
        getWidget(mMagickaText, "MagickaText");
        getWidget(mStaminaText, "StaminaText");
        getWidget(mFpsBox, "FpsText");
        mHealthManaStaminaBaseLeft = mHealthFrame->getLeft();

        mHealthFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);
        mMagickaFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);
        mFatigueFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);

        //Drowning bar
        getWidget(mDrowningFrame, "DrowningFrame");
        getWidget(mDrowning, "Drowning");
        getWidget(mDrowningFlash, "Flash");
        mDrowning->setProgressRange(200);

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();

        // Item and spell images and status bars
        getWidget(mWeapBox, "WeapBox");
        getWidget(mWeapImage, "WeapImage");
        getWidget(mWeapStatus, "WeapStatus");
        mWeapBoxBaseLeft = mWeapBox->getLeft();
        mWeapBox->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWeaponClicked);

        getWidget(mSpellBox, "SpellBox");
        getWidget(mSpellImage, "SpellImage");
        getWidget(mSpellStatus, "SpellStatus");
        mSpellBoxBaseLeft = mSpellBox->getLeft();
        mSpellBox->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMagicClicked);

        getWidget(mSneakBox, "SneakBox");
        mSneakBoxBaseLeft = mSneakBox->getLeft();

        getWidget(mEffectBox, "EffectBox");
        mEffectBoxBaseRight = viewSize.width - mEffectBox->getRight();

        getWidget(mMinimapBox, "MiniMapBox");
        mMinimapBoxBaseRight = viewSize.width - mMinimapBox->getRight();
        getWidget(mMinimap, "MiniMap");
        getWidget(mCompass, "Compass");
        getWidget(mMinimapButton, "MiniMapButton");
        mMinimapButton->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);

        getWidget(mCellNameClip, "CellNameClip");
        getWidget(mCellNameBox, "CellName");
        getWidget(mWeaponSpellBox, "WeaponSpellName");
        getWidget(mGameTimeBox, "GameTime");
        if (mGameTimeBox)
        {
            mGameTimeBox->setFontHeight(18);
            mGameTimeBox->setTextAlign(MyGUI::Align::Center);
            mGameTimeBox->setCaption(mGameTimeCaption);
        }
        // Location now shares the clock line, so retire the old standalone caption.
        if (mCellNameClip)
            mCellNameClip->setVisible(false);
        if (mCellNameBox)
            mCellNameBox->setVisible(false);

        getWidget(mHorizontalCompass, "HorizontalCompass");
        constexpr int horizontalCompassTickCount = 11;
        for (int i = 0; i < horizontalCompassTickCount; ++i)
        {
            MyGUI::TextBox* tick = mHorizontalCompass->createWidget<MyGUI::TextBox>("SandBrightText",
                MyGUI::IntCoord(0, 1, 48, 20), MyGUI::Align::Default);
            tick->setTextAlign(MyGUI::Align::Center);
            tick->setTextShadow(true);
            tick->setTextShadowColour(MyGUI::Colour::Black);
            tick->setNeedMouseFocus(false);
            mHorizontalCompassTicks.push_back(tick);
        }
        mHorizontalCompassCenter = mHorizontalCompass->createWidget<MyGUI::TextBox>("SandBrightText",
            MyGUI::IntCoord(mHorizontalCompass->getWidth() / 2 - 12, 14, 24, 10), MyGUI::Align::Default);
        mHorizontalCompassCenter->setCaption("^");
        mHorizontalCompassCenter->setTextAlign(MyGUI::Align::Center);
        mHorizontalCompassCenter->setTextShadow(true);
        mHorizontalCompassCenter->setTextShadowColour(MyGUI::Colour::Black);
        mHorizontalCompassCenter->setNeedMouseFocus(false);

        // Reuse a fixed pool so actors entering/leaving range never allocate GUI widgets mid-frame.
        constexpr int horizontalCompassMarkerCount = 48;
        for (int i = 0; i < horizontalCompassMarkerCount; ++i)
        {
            MyGUI::TextBox* marker = mHorizontalCompass->createWidget<MyGUI::TextBox>("NormalText",
                MyGUI::IntCoord(0, 3, 32, 22), MyGUI::Align::Default);
            marker->setCaption("●");
            marker->setFontName("CompassMarkerFont");
            marker->setFontHeight(18);
            marker->setTextAlign(MyGUI::Align::Center);
            marker->setTextShadow(true);
            marker->setTextShadowColour(MyGUI::Colour::Black);
            marker->setNeedMouseFocus(false);
            marker->setAlpha(0.f);
            marker->setVisible(false);

            HorizontalCompassMarkerState state;
            state.mWidget = marker;
            mHorizontalCompassMarkers.push_back(state);
        }

        getWidget(mCrosshair, "Crosshair");
        // The crosshair lives outside the chargen-hidden GameplayHud container,
        // so it is available as soon as the camera enters game mode.
        mCrosshair->setVisible(Settings::Manager::getBool("crosshair", "HUD"));

        LocalMapBase::init(mMinimap, mCompass);

        mMainWidget->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWorldClicked);
        mMainWidget->eventMouseMove += MyGUI::newDelegate(this, &HUD::onWorldMouseOver);
        mMainWidget->eventMouseLostFocus += MyGUI::newDelegate(this, &HUD::onWorldMouseLostFocus);

        updatePositions();
        mSpellIcons = new SpellIcons();
    }

    HUD::~HUD()
    {
        mMainWidget->eventMouseLostFocus.clear();
        mMainWidget->eventMouseMove.clear();
        mMainWidget->eventMouseButtonClick.clear();

        delete mSpellIcons;
    }

    void HUD::setValue(const std::string& id, const MWMechanics::DynamicStat<float>& value)
    {
        int current = static_cast<int>(value.getCurrent());
        int modified = static_cast<int>(value.getModified());

        // Fatigue can be negative
        if (id != "FBar")
            current = std::max(0, current);

        std::string valStr = MyGUI::utility::toString(current) + " / " + MyGUI::utility::toString(modified);
        if (id == "HBar")
        {
            mHealth->setProgressRange(std::max(0, modified));
            mHealth->setProgressPosition(std::max(0, current));
            if (mHealthText)
                mHealthText->setCaption(valStr);
            mHealthFrame->setUserString("Caption_HealthDescription", "#{sHealthDesc}\n" + valStr);
            registerBarChange(mHealthBarState, current, modified);
        }
        else if (id == "MBar")
        {
            mMagicka->setProgressRange(std::max(0, modified));
            mMagicka->setProgressPosition(std::max(0, current));
            if (mMagickaText)
                mMagickaText->setCaption(valStr);
            mMagickaFrame->setUserString("Caption_HealthDescription", "#{sMagDesc}\n" + valStr);
            registerBarChange(mMagickaBarState, current, modified);
        }
        else if (id == "FBar")
        {
            mStamina->setProgressRange(std::max(0, modified));
            mStamina->setProgressPosition(std::max(0, current));
            if (mStaminaText)
                mStaminaText->setCaption(valStr);
            mFatigueFrame->setUserString("Caption_HealthDescription", "#{sFatDesc}\n" + valStr);
            registerBarChange(mStaminaBarState, current, modified);
        }
    }

    void HUD::setDrowningTimeLeft(float time, float maxTime)
    {
        size_t progress = static_cast<size_t>(time / maxTime * 200);
        mDrowning->setProgressPosition(progress);

        bool isDrowning = (progress == 0);
        if (isDrowning && !mIsDrowning) // Just started drowning
            mDrowningFlashTheta = 0.0f; // Start out on bright red every time.

        mDrowningFlash->setVisible(isDrowning);
        mIsDrowning = isDrowning;
    }

    void HUD::setDrowningBarVisible(bool visible)
    {
        mDrowningFrame->setVisible(visible);
    }

    void HUD::onWorldClicked(MyGUI::Widget* _sender)
    {
        if (!MWBase::Environment::get().getWindowManager ()->isGuiMode ())
            return;

        MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            // drop item into the gameworld
            MWBase::Environment::get().getWorld()->breakInvisibility(
                        MWMechanics::getPlayer());

            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            MyGUI::IntPoint cursorPosition = MyGUI::InputManager::getInstance().getMousePosition();
            float mouseX = cursorPosition.left / float(viewSize.width);
            float mouseY = cursorPosition.top / float(viewSize.height);

            WorldItemModel drop (mouseX, mouseY);
            mDragAndDrop->drop(&drop, nullptr);

            winMgr->changePointer("arrow");
        }
        else
        {
            GuiMode mode = winMgr->getMode();

            if (!winMgr->isConsoleMode() && (mode != GM_Container) && (mode != GM_Inventory))
                return;

            MWWorld::Ptr object = MWBase::Environment::get().getWorld()->getFacedObject();

            if (winMgr->isConsoleMode())
                winMgr->setConsoleSelectedObject(object);
            else //if ((mode == GM_Container) || (mode == GM_Inventory))
            {
                // pick up object
                if (!object.isEmpty())
                    winMgr->getInventoryWindow()->pickUpObject(object);
            }
        }
    }

    void HUD::onWorldMouseOver(MyGUI::Widget* _sender, int x, int y)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            mWorldMouseOver = false;

            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            MyGUI::IntPoint cursorPosition = MyGUI::InputManager::getInstance().getMousePosition();
            float mouseX = cursorPosition.left / float(viewSize.width);
            float mouseY = cursorPosition.top / float(viewSize.height);

            MWBase::World* world = MWBase::Environment::get().getWorld();

            // if we can't drop the object at the wanted position, show the "drop on ground" cursor.
            bool canDrop = world->canPlaceObject(mouseX, mouseY);

            if (!canDrop)
                MWBase::Environment::get().getWindowManager()->changePointer("drop_ground");
            else
                MWBase::Environment::get().getWindowManager()->changePointer("arrow");

        }
        else
        {
            MWBase::Environment::get().getWindowManager()->changePointer("arrow");
            mWorldMouseOver = true;
        }
    }

    void HUD::onWorldMouseLostFocus(MyGUI::Widget* _sender, MyGUI::Widget* _new)
    {
        MWBase::Environment::get().getWindowManager()->changePointer("arrow");
        mWorldMouseOver = false;
    }

    void HUD::onHMSClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Stats);
    }

    void HUD::onMapClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Map);
    }

    void HUD::onWeaponClicked(MyGUI::Widget* _sender)
    {
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Inventory);
    }

    void HUD::onMagicClicked(MyGUI::Widget* _sender)
    {
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Magic);
    }


    void HUD::onResChange(int width, int height)
    {
        mMainWidget->setSize(width, height);
        updatePositions();
    }

    void HUD::setCellName(const std::string& cellName)
    {
        if (mCellName != cellName)
        {
            mCellName = cellName;
            mCellNameTimer = Settings::Manager::getBool("show cell name", "HUD")
                ? sCompassInfoSequenceDuration : 0.f;

            if (mCellNameClip)
                mCellNameClip->setVisible(false);
            if (mCellNameBox)
                mCellNameBox->setVisible(false);
        }
    }

    void HUD::onFrame(float dt)
    {
        // In single-player, chargenstate becomes -1 only after the complete
        // character creation/registration sequence has finished. Toggling one
        // parent preserves every child's own map/autohide/setting visibility.
        const bool characterRegistrationFinished
            = MWBase::Environment::get().getWorld()->getGlobalInt("chargenstate") == -1;
        if (mGameplayHud && mGameplayHud->getVisible() != characterRegistrationFinished)
            mGameplayHud->setVisible(characterRegistrationFinished);

        LocalMapBase::onFrame(dt);

        // Apply HUD preferences live so changes made in the dedicated HUD tab do
        // not require reopening the game or rebuilding the HUD layout.
        const bool showMinimap = mMinimapBaseVisible && Settings::Manager::getBool("show minimap", "HUD");
        if (mMinimapBox->getVisible() != showMinimap)
        {
            mMinimapBox->setVisible(showMinimap);
            updatePositions();
        }
        if (mCompass)
            mCompass->setVisible(showMinimap);
        // Clock/location visibility and timed replacement are handled together below.
        if (mEffectBox)
            mEffectBox->setVisible(mEffectBaseVisible && Settings::Manager::getBool("show status effects", "HUD"));
        if (mFpsBox)
            mFpsBox->setVisible(Settings::Manager::getBool("show fps", "HUD"));
        if (mCrosshair)
            mCrosshair->setVisible(mCrosshairBaseVisible && Settings::Manager::getBool("crosshair", "HUD"));

        updateHorizontalCompass();
        updateHorizontalCompassMarkers(dt);

        updateGameTimeAndCellName(dt);

        mWeaponSpellTimer -= dt;
        if (mWeaponSpellTimer < 0)
            mWeaponSpellBox->setVisible(false);

        mFpsAccumulatedTime += dt;
        ++mFpsFrameCount;
        mFpsUpdateTimer -= dt;
        if (mFpsBox && mFpsUpdateTimer <= 0.f)
        {
            const float safeTime = std::max(0.0001f, mFpsAccumulatedTime);
            const int fps = static_cast<int>(std::lround(
                static_cast<double>(mFpsFrameCount) / safeTime));
            mFpsBox->setCaption(MyGUI::utility::toString(fps));
            mFpsUpdateTimer = 0.25f;
            mFpsAccumulatedTime = 0.f;
            mFpsFrameCount = 0;
        }

        updateCombatHealthBars(dt);
        updateFocusedTargetPanel(dt);

        const std::string npcBarMode = getNpcBarMode();
        const bool showHoverNpcBar = npcBarShowsHover(npcBarMode);
        const bool focusedTargetAlive = !mFocusActor.isEmpty()
            && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead();
        const bool dialogueOpen = MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue);
        const bool focusedTargetPanel = showHoverNpcBar && focusedTargetAlive && !dialogueOpen
            && mFocusActorPanelAlpha >= 0.5f;
        mEnemyHealthTimer -= dt;
        if (mEnemyHealth->getVisible() && !focusedTargetPanel)
            mEnemyHealth->setVisible(false);

        if (mIsDrowning)
            mDrowningFlashTheta += dt * osg::PI*2;

        mSpellIcons->updateWidgets(mEffectBox, true);
        if (mEffectBox)
            mEffectBox->setVisible(mEffectBaseVisible
                && Settings::Manager::getBool("show status effects", "HUD")
                && mEffectBox->getChildCount() > 0);

        if (focusedTargetPanel && mEnemyHealth->getVisible())
            updateEnemyHealthBar();

        if (focusedTargetPanel)
        {
            mEnemyHealth->setVisible(true);
            if (mEnemyName)
                mEnemyName->setVisible(true);
            if (mEnemySummary)
                mEnemySummary->setVisible(true);
            updateEnemyHealthBar();
        }

        if (mIsDrowning)
        {
            float intensity = (cos(mDrowningFlashTheta) + 2.0f) / 3.0f;

            mDrowningFlash->setAlpha(intensity);
        }

        MWMechanics::DrawState_ drawState = MWMechanics::DrawState_Nothing;
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (!player.isEmpty())
            drawState = player.getClass().getCreatureStats(player).getDrawState();

        if (!showHoverNpcBar || dialogueOpen)
            mEnemyHealth->setVisible(false);

        const bool showFocusedTargetInfo = focusedTargetPanel && mEnemyHealth->getVisible();
        if (mEnemyName)
            mEnemyName->setVisible(showFocusedTargetInfo);
        if (mEnemySummary)
            mEnemySummary->setVisible(showFocusedTargetInfo);

        updateAutoHideBar(mHealthFrame, mHealthBarState, dt, false);
        updateAutoHideBar(mMagickaFrame, mMagickaBarState, dt,
            drawState == MWMechanics::DrawState_Spell, mSpellBox);
        updateAutoHideBar(mFatigueFrame, mStaminaBarState, dt,
            drawState == MWMechanics::DrawState_Weapon, mWeapBox);
    }


    void HUD::updateGameTimeAndCellName(float dt)
    {
        if (!mGameTimeBox)
            return;

        dt = std::max(0.f, dt);
        const bool showTime = Settings::Manager::getBool("show game time", "HUD");
        const bool showLocation = Settings::Manager::getBool("show cell name", "HUD");

        mGameTimeUpdateTimer -= dt;
        if (mGameTimeUpdateTimer <= 0.f)
        {
            const float gameHour = MWBase::Environment::get().getWorld()->getTimeStamp().getHour();
            int hours = static_cast<int>(std::floor(gameHour)) % 24;
            int minutes = static_cast<int>(std::floor((gameHour - std::floor(gameHour)) * 60.f + 0.5f));
            if (minutes >= 60)
            {
                minutes = 0;
                hours = (hours + 1) % 24;
            }

            std::ostringstream stream;
            stream << std::setfill('0') << std::setw(2) << hours << ':'
                   << std::setfill('0') << std::setw(2) << minutes;
            mGameTimeCaption = stream.str();
            mGameTimeUpdateTimer = 0.2f;
        }

        if (!showLocation)
            mCellNameTimer = 0.f;
        else if (mCellNameTimer > 0.f)
            mCellNameTimer = std::max(0.f, mCellNameTimer - dt);

        bool displayLocation = false;
        float alpha = 1.f;
        const bool locationSequence = showLocation && !mCellName.empty() && mCellNameTimer > 0.f;

        if (locationSequence)
        {
            if (!showTime)
            {
                displayLocation = true;
                alpha = 1.f;
            }
            else
            {
                const float elapsed = sCompassInfoSequenceDuration - mCellNameTimer;
                const float fade = sCompassInfoFadeDuration;
                const float locationEnd = fade + sCompassLocationHoldDuration;

                if (elapsed < fade)
                {
                    displayLocation = false;
                    alpha = 1.f - elapsed / fade;
                }
                else if (elapsed < locationEnd)
                {
                    displayLocation = true;
                    alpha = 1.f;
                }
                else
                {
                    displayLocation = false;
                    alpha = (elapsed - locationEnd) / fade;
                }
            }
        }

        alpha = std::max(0.f, std::min(1.f, alpha));
        const bool visible = displayLocation ? showLocation : showTime;
        mGameTimeBox->setVisible(visible);
        mGameTimeBox->setAlpha(visible ? alpha : 0.f);

        if (!visible)
            return;

        if (displayLocation)
        {
            if (!mGameTimeShowingCellName)
            {
                mGameTimeBox->setCaptionWithReplacing("#{sCell=" + mCellName + "}");
                mGameTimeShowingCellName = true;
            }
        }
        else
        {
            mGameTimeBox->setCaption(mGameTimeCaption);
            mGameTimeShowingCellName = false;
        }
    }

    void HUD::setPlayerDir(float x, float y)
    {
        LocalMapBase::setPlayerDir(x, y);
        const float angle = std::atan2(x, y);
        if (angle != mHorizontalCompassAngle)
        {
            mHorizontalCompassAngle = angle;
            mHorizontalCompassDirty = true;
        }
    }

    void HUD::updateHorizontalCompass()
    {
        if (!mHorizontalCompass)
            return;

        const bool enabled = Settings::Manager::getBool("horizontal compass", "HUD");
        if (mHorizontalCompass->getVisible() != enabled)
            mHorizontalCompass->setVisible(enabled);
        if (!enabled || !mHorizontalCompassDirty)
            return;

        static const char* directions[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        constexpr float stepDegrees = 15.f;
        constexpr float pixelsPerDegree = 4.f;
        constexpr int labelWidth = 48;

        float heading = osg::RadiansToDegrees(mHorizontalCompassAngle);
        while (heading < 0.f)
            heading += 360.f;
        while (heading >= 360.f)
            heading -= 360.f;

        const int nearestStep = static_cast<int>(std::floor(heading / stepDegrees + 0.5f));
        const int halfCount = static_cast<int>(mHorizontalCompassTicks.size()) / 2;
        for (size_t i = 0; i < mHorizontalCompassTicks.size(); ++i)
        {
            const int markerStep = nearestStep + static_cast<int>(i) - halfCount;
            const float markerDegrees = markerStep * stepDegrees;
            const float delta = markerDegrees - heading;
            const int left = static_cast<int>(std::lround(
                mHorizontalCompass->getWidth() * 0.5f + delta * pixelsPerDegree - labelWidth * 0.5f));
            MyGUI::TextBox* tick = mHorizontalCompassTicks[i];
            tick->setPosition(left, 1);

            int wrappedStep = markerStep % 24;
            if (wrappedStep < 0)
                wrappedStep += 24;
            if (wrappedStep % 3 == 0)
                tick->setCaption(directions[(wrappedStep / 3) % 8]);
            else
                tick->setCaption("|");
        }

        mHorizontalCompassDirty = false;
    }

    void HUD::updateHorizontalCompassMarkers(float dt)
    {
        if (!mHorizontalCompass)
            return;

        dt = std::max(0.f, dt);
        const bool enabled = Settings::Manager::getBool("horizontal compass", "HUD")
            && mGameplayHud && mGameplayHud->getVisible();
        if (!enabled)
        {
            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
            {
                state.mObject = MWWorld::Ptr();
                state.mIdentity.clear();
                state.mAlpha = 0.f;
                state.mTargetAlpha = 0.f;
                state.mSeen = false;
                state.mWidget->setAlpha(0.f);
                state.mWidget->setVisible(false);
            }
            mHorizontalCompassMarkerTimer = 0.f;
            return;
        }

        mHorizontalCompassMarkerTimer -= dt;
        if (mHorizontalCompassMarkerTimer <= 0.f)
        {
            mHorizontalCompassMarkerTimer = 0.075f;

            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                state.mSeen = false;

            MWBase::World* world = MWBase::Environment::get().getWorld();
            MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
            if (world && mechanics)
            {
                const MWWorld::Ptr player = world->getPlayerPtr();
                if (!player.isEmpty() && player.isInCell())
                {
                    // Lower enum value has visual priority if a reference belongs to several groups.
                    std::map<MWWorld::Ptr, HorizontalCompassMarkerKind> objects;
                    const auto addObject = [&objects](const MWWorld::Ptr& object, HorizontalCompassMarkerKind kind)
                    {
                        if (object.isEmpty())
                            return;
                        const auto result = objects.emplace(object, kind);
                        if (!result.second && static_cast<int>(kind) < static_cast<int>(result.first->second))
                            result.first->second = kind;
                    };

                    for (const MWWorld::Ptr& enemy : mechanics->getActorsFighting(player))
                        addObject(enemy, HorizontalCompassMarkerKind::Enemy);

                    std::set<MWWorld::Ptr> allies;
                    mechanics->getActorsSidingWith(player, allies);
                    for (const MWWorld::Ptr& ally : allies)
                        addObject(ally, HorizontalCompassMarkerKind::Ally);

                    // Native detection logic already includes spells, abilities, potions and enchanted items.
                    const auto addDetected = [world, &player, &addObject](MWBase::World::DetectionType type,
                        HorizontalCompassMarkerKind kind)
                    {
                        std::vector<MWWorld::Ptr> detected;
                        world->listDetectedReferences(player, detected, type);
                        for (const MWWorld::Ptr& object : detected)
                            addObject(object, kind);
                    };
                    addDetected(MWBase::World::Detect_Key, HorizontalCompassMarkerKind::DetectKey);
                    addDetected(MWBase::World::Detect_Enchantment, HorizontalCompassMarkerKind::DetectEnchantment);
                    addDetected(MWBase::World::Detect_Creature, HorizontalCompassMarkerKind::DetectCreature);

                    constexpr float markerRange = 8192.f;
                    constexpr float markerRangeSquared = markerRange * markerRange;
                    constexpr float doorMarkerRange = 1250.f;
                    constexpr float doorMarkerRangeSquared = doorMarkerRange * doorMarkerRange;
                    constexpr float pixelsPerDegree = 4.f;
                    constexpr int compassInnerPadding = 10;
                    const int markerWidth = mHorizontalCompassMarkers.empty()
                        ? 32
                        : mHorizontalCompassMarkers.front().mWidget->getWidth();
                    const osg::Vec3f playerPosition = player.getRefData().getPosition().asVec3();
                    const int compassWidth = mHorizontalCompass->getWidth();
                    const float compassCenter = compassWidth * 0.5f;
                    const float maximumMarkerOffset = std::max(1.f,
                        compassCenter - compassInnerPadding - markerWidth * 0.5f);
                    const float maximumVisibleDegrees = maximumMarkerOffset / pixelsPerDegree;

                    std::vector<HorizontalCompassMarkerCandidate> candidates;
                    candidates.reserve(objects.size() + 24);

                    const auto appendCandidate = [&](const MWWorld::Ptr& object, const std::string& identity,
                        HorizontalCompassMarkerKind kind, float worldX, float worldY, float distanceSquared)
                    {
                        if (distanceSquared > markerRangeSquared || distanceSquared < 1.f)
                            return;

                        const float bearing = std::atan2(worldX - playerPosition.x(), worldY - playerPosition.y());
                        float relativeAngle = bearing - mHorizontalCompassAngle;
                        while (relativeAngle > osg::PI)
                            relativeAngle -= static_cast<float>(osg::PI) * 2.f;
                        while (relativeAngle < -osg::PI)
                            relativeAngle += static_cast<float>(osg::PI) * 2.f;

                        const float relativeDegrees = osg::RadiansToDegrees(relativeAngle);
                        const float displayedDegrees = std::max(-maximumVisibleDegrees,
                            std::min(maximumVisibleDegrees, relativeDegrees));
                        const int left = static_cast<int>(std::lround(
                            compassCenter + displayedDegrees * pixelsPerDegree - markerWidth * 0.5f));

                        HorizontalCompassMarkerCandidate candidate;
                        candidate.mObject = object;
                        candidate.mIdentity = identity;
                        candidate.mLeft = left;
                        candidate.mDistanceSquared = distanceSquared;
                        candidate.mKind = kind;
                        candidates.push_back(candidate);
                    };

                    for (const auto& objectEntry : objects)
                    {
                        const MWWorld::Ptr& object = objectEntry.first;
                        const HorizontalCompassMarkerKind kind = objectEntry.second;
                        if (object == player || !object.isInCell())
                            continue;
                        if (object.getRefData().getCount() <= 0 || !object.getRefData().isEnabled()
                            || object.getRefData().isDeleted())
                            continue;

                        const bool actorMarker = kind == HorizontalCompassMarkerKind::Enemy
                            || kind == HorizontalCompassMarkerKind::Ally
                            || kind == HorizontalCompassMarkerKind::DetectCreature;
                        if (actorMarker)
                        {
                            if (!object.getClass().isActor() || object.getClass().getCreatureStats(object).isDead())
                                continue;
                            if (kind != HorizontalCompassMarkerKind::DetectCreature
                                && object.getCell() != player.getCell())
                                continue;
                        }

                        const osg::Vec3f position = object.getRefData().getPosition().asVec3();
                        const float dx = position.x() - playerPosition.x();
                        const float dy = position.y() - playerPosition.y();
                        appendCandidate(object, std::string(), kind, position.x(), position.y(), dx * dx + dy * dy);
                    }

                    // Outdoors only: show nearby teleport doors that lead into interiors.
                    if (player.getCell()->getCell()->isExterior())
                    {
                        std::vector<MWBase::World::DoorMarker> doors;
                        world->getDoorMarkers(player.getCell(), doors);
                        for (const MWBase::World::DoorMarker& door : doors)
                        {
                            if (door.dest.mPaged)
                                continue;

                            const float dx = door.x - playerPosition.x();
                            const float dy = door.y - playerPosition.y();
                            const float distanceSquared = dx * dx + dy * dy;
                            if (distanceSquared > doorMarkerRangeSquared)
                                continue;

                            std::ostringstream identity;
                            identity << "door:" << door.name << ':'
                                << static_cast<int>(std::lround(door.x)) << ':'
                                << static_cast<int>(std::lround(door.y));
                            appendCandidate(MWWorld::Ptr(), identity.str(), HorizontalCompassMarkerKind::Door,
                                door.x, door.y, distanceSquared);
                        }
                    }

                    std::sort(candidates.begin(), candidates.end(),
                        [](const HorizontalCompassMarkerCandidate& left,
                            const HorizontalCompassMarkerCandidate& right)
                        {
                            if (left.mDistanceSquared != right.mDistanceSquared)
                                return left.mDistanceSquared < right.mDistanceSquared;
                            return static_cast<int>(left.mKind) < static_cast<int>(right.mKind);
                        });

                    const size_t candidateCount = std::min(candidates.size(), mHorizontalCompassMarkers.size());
                    for (size_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
                    {
                        const HorizontalCompassMarkerCandidate& candidate = candidates[candidateIndex];
                        HorizontalCompassMarkerState* selectedState = nullptr;

                        for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                        {
                            const bool sameObject = !candidate.mObject.isEmpty()
                                && !state.mObject.isEmpty() && state.mObject == candidate.mObject;
                            const bool sameIdentity = candidate.mObject.isEmpty()
                                && state.mObject.isEmpty() && !candidate.mIdentity.empty()
                                && state.mIdentity == candidate.mIdentity;
                            if (sameObject || sameIdentity)
                            {
                                selectedState = &state;
                                break;
                            }
                        }

                        if (!selectedState)
                        {
                            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                            {
                                if (!state.mSeen && state.mObject.isEmpty() && state.mIdentity.empty())
                                {
                                    selectedState = &state;
                                    break;
                                }
                            }
                        }
                        if (!selectedState)
                        {
                            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                            {
                                if (state.mSeen)
                                    continue;
                                if (!selectedState || state.mAlpha < selectedState->mAlpha)
                                    selectedState = &state;
                            }
                        }
                        if (!selectedState)
                            continue;

                        const bool newMarker = (!candidate.mObject.isEmpty()
                                && (selectedState->mObject.isEmpty() || selectedState->mObject != candidate.mObject))
                            || (candidate.mObject.isEmpty()
                                && (selectedState->mIdentity != candidate.mIdentity || !selectedState->mObject.isEmpty()));
                        const float oldTargetLeft = selectedState->mTargetLeft;
                        selectedState->mObject = candidate.mObject;
                        selectedState->mIdentity = candidate.mIdentity;
                        selectedState->mSeen = true;
                        selectedState->mTargetLeft = static_cast<float>(candidate.mLeft);
                        selectedState->mTargetAlpha = 1.f;
                        selectedState->mWidget->setCaption(getHorizontalCompassMarkerCaption(candidate.mKind));
                        selectedState->mWidget->setTextColour(getHorizontalCompassMarkerColour(candidate.mKind));

                        if (newMarker)
                        {
                            selectedState->mCurrentLeft = selectedState->mTargetLeft;
                            selectedState->mAlpha = 0.f;
                            selectedState->mWidget->setAlpha(0.f);
                        }
                        else if (std::abs(selectedState->mTargetLeft - oldTargetLeft) > compassWidth * 0.65f)
                        {
                            selectedState->mCurrentLeft = selectedState->mTargetLeft;
                        }
                    }
                }
            }

            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
            {
                if (!state.mSeen)
                    state.mTargetAlpha = 0.f;
            }
        }

        const float positionBlend = 1.f - std::exp(-14.f * dt);
        constexpr float fadeInSpeed = 7.5f;
        constexpr float fadeOutSpeed = 4.5f;
        for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
        {
            state.mCurrentLeft += (state.mTargetLeft - state.mCurrentLeft) * positionBlend;
            if (state.mTargetAlpha > state.mAlpha)
                state.mAlpha = std::min(state.mTargetAlpha, state.mAlpha + fadeInSpeed * dt);
            else
                state.mAlpha = std::max(state.mTargetAlpha, state.mAlpha - fadeOutSpeed * dt);

            const bool visible = state.mAlpha > 0.01f || state.mTargetAlpha > 0.f;
            state.mWidget->setVisible(visible);
            if (visible)
            {
                constexpr int compassInnerPadding = 10;
                const int markerWidth = state.mWidget->getWidth();
                const float minimumLeft = static_cast<float>(compassInnerPadding);
                const float maximumLeft = static_cast<float>(std::max(
                    compassInnerPadding, mHorizontalCompass->getWidth() - compassInnerPadding - markerWidth));
                state.mCurrentLeft = std::max(minimumLeft, std::min(maximumLeft, state.mCurrentLeft));
                state.mWidget->setPosition(static_cast<int>(std::lround(state.mCurrentLeft)), 3);
                state.mWidget->setAlpha(state.mAlpha);
            }
            else
            {
                state.mObject = MWWorld::Ptr();
                state.mIdentity.clear();
                state.mAlpha = 0.f;
                state.mWidget->setAlpha(0.f);
            }
        }
    }

    void HUD::registerBarChange(AutoHideBarState& state, int current, int modified)
    {
        const bool firstUpdate = !state.initialized;
        const bool maximumChanged = state.initialized && state.modified != modified;
        const bool valueDecreased = state.initialized && current < state.current;

        state.current = current;
        state.modified = modified;
        state.initialized = true;

        // Show a bar when the resource is actually spent/damaged or its maximum changes.
        // Passive regeneration must not continuously restart the auto-hide timer.
        if (firstUpdate || maximumChanged || valueDecreased)
        {
            state.idleTimer = 0.f;
            state.alpha = 1.f;
        }
    }

    void HUD::applyBarAlpha(MyGUI::Widget* widget, float alpha)
    {
        if (!widget)
            return;

        widget->setAlpha(std::max(0.f, std::min(1.f, alpha)));
    }

    void HUD::updateAutoHideBar(MyGUI::Widget* frame, AutoHideBarState& state, float dt,
        bool forceVisible, MyGUI::Widget* persistentIcon)
    {
        if (!frame || !state.initialized)
            return;

        const bool barEnabled = frame == mHealthFrame
            ? Settings::Manager::getBool("show health bar", "HUD")
            : (frame == mMagickaFrame
                ? Settings::Manager::getBool("show magicka bar", "HUD")
                : Settings::Manager::getBool("show stamina bar", "HUD"));

        if (!mHmsBaseVisible)
        {
            frame->setVisible(false);
            return;
        }

        const auto applyResourceState = [&](float alpha)
        {
            alpha = std::max(0.f, std::min(1.f, alpha));
            const std::string boxMode = persistentIcon ? getWeaponSpellBoxMode() : "hidden";
            const bool keepIcon = persistentIcon && boxMode != "hidden";

            if (!keepIcon)
            {
                frame->setVisible(alpha > 0.f);
                applyBarAlpha(frame, alpha);
                if (alpha > 0.f)
                {
                    for (unsigned int i = 0; i < frame->getChildCount(); ++i)
                    {
                        MyGUI::Widget* child = frame->getChildAt(i);
                        const bool iconAllowed = child != persistentIcon
                            || (persistentIcon == mWeapBox ? mWeaponVisible : mSpellVisible);
                        child->setVisible(iconAllowed);
                        applyBarAlpha(child, 1.f);
                    }
                }
                return;
            }

            // Weapon and spell boxes live inside the stamina/magicka frame. Keep the
            // parent alive, fade only the bar children, and apply the selected box mode.
            frame->setVisible(true);
            applyBarAlpha(frame, 1.f);
            for (unsigned int i = 0; i < frame->getChildCount(); ++i)
            {
                MyGUI::Widget* child = frame->getChildAt(i);
                if (child == persistentIcon)
                    continue;
                child->setVisible(alpha > 0.f);
                applyBarAlpha(child, alpha);
            }

            const bool iconAllowed = persistentIcon == mWeapBox ? mWeaponVisible : mSpellVisible;
            persistentIcon->setVisible(iconAllowed);
            const float persistentAlpha = boxMode == "visible" ? 1.f : 0.4f;
            applyBarAlpha(persistentIcon, std::max(persistentAlpha, alpha));
        };

        const std::string resourceMode = getResourceBarMode();
        if (!barEnabled || resourceMode == "hidden")
        {
            applyResourceState(0.f);
            return;
        }

        if (resourceMode == "always")
        {
            state.alpha = 1.f;
            applyResourceState(1.f);
            return;
        }

        // Keep the relevant resource bar visible for as long as the player is
        // actively holding a weapon or has magic readied. Start the normal
        // auto-hide delay only after the weapon/spell is put away.
        if (forceVisible)
        {
            state.idleTimer = 0.f;
            state.alpha = 1.f;
            applyResourceState(1.f);
            return;
        }

        state.idleTimer += dt;

        const bool isFull = state.modified <= 0 || state.current >= state.modified;
        const float hideDelay = isFull ? 7.f : 20.f;
        const float fadeDuration = 0.35f;

        float targetAlpha = 1.f;
        if (state.idleTimer > hideDelay)
            targetAlpha = std::max(0.f, 1.f - (state.idleTimer - hideDelay) / fadeDuration);

        state.alpha = targetAlpha;
        applyResourceState(state.alpha);
    }

    void HUD::setSelectedSpell(const std::string& spellId, int successChancePercent)
    {
        const ESM::Spell* spell =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().find(spellId);

        std::string spellName = spell->mName;
        if (spellName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = spellName;
            mWeaponSpellBox->setCaption(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(successChancePercent);

        mSpellBox->setUserString("ToolTipType", "Spell");
        mSpellBox->setUserString("Spell", spellId);

        // use the icon of the first effect
        const ESM::MagicEffect* effect =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(spell->mEffects.mList.front().mEffectID);

        std::string icon = effect->mIcon;
        int slashPos = icon.rfind('\\');
        icon.insert(slashPos+1, "b_");
        icon = MWBase::Environment::get().getWindowManager()->correctIconPath(icon);

        mSpellImage->setSpellIcon(icon);
    }

    void HUD::setSelectedEnchantItem(const MWWorld::Ptr& item, int chargePercent)
    {
        std::string itemName = item.getClass().getName(item);
        if (itemName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = itemName;
            mWeaponSpellBox->setCaption(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(chargePercent);

        mSpellBox->setUserString("ToolTipType", "ItemPtr");
        mSpellBox->setUserData(MWWorld::Ptr(item));

        mSpellImage->setItem(item);
    }

    void HUD::setSelectedWeapon(const MWWorld::Ptr& item, int durabilityPercent)
    {
        std::string itemName = item.getClass().getName(item);
        if (itemName != mWeaponName && mWeaponVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mWeaponName = itemName;
            mWeaponSpellBox->setCaption(mWeaponName);
            mWeaponSpellBox->setVisible(true);
        }

        mWeapBox->clearUserStrings();
        mWeapBox->setUserString("ToolTipType", "ItemPtr");
        mWeapBox->setUserData(MWWorld::Ptr(item));

        mWeapStatus->setProgressRange(100);
        mWeapStatus->setProgressPosition(durabilityPercent);

        mWeapImage->setItem(item);
    }

    void HUD::unsetSelectedSpell()
    {
        std::string spellName = "#{sNone}";
        if (spellName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = spellName;
            mWeaponSpellBox->setCaptionWithReplacing(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(0);
        mSpellImage->setItem(MWWorld::Ptr());
        mSpellBox->clearUserStrings();
    }

    void HUD::unsetSelectedWeapon()
    {
        std::string itemName = "#{sSkillHandtohand}";
        if (itemName != mWeaponName && mWeaponVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mWeaponName = itemName;
            mWeaponSpellBox->setCaptionWithReplacing(mWeaponName);
            mWeaponSpellBox->setVisible(true);
        }

        mWeapStatus->setProgressRange(100);
        mWeapStatus->setProgressPosition(0);

        MWBase::World *world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();

        mWeapImage->setItem(MWWorld::Ptr());
        std::string icon = (player.getClass().getNpcStats(player).isWerewolf()) ? "icons\\k\\tx_werewolf_hand.dds" : "icons\\k\\stealth_handtohand.dds";
        mWeapImage->setIcon(icon);

        mWeapBox->clearUserStrings();
        mWeapBox->setUserString("ToolTipType", "Layout");
        mWeapBox->setUserString("ToolTipLayout", "HandToHandToolTip");
        mWeapBox->setUserString("Caption_HandToHandText", itemName);
        mWeapBox->setUserString("ImageTexture_HandToHandImage", icon);
    }

    void HUD::setCrosshairVisible(bool visible)
    {
        mCrosshairBaseVisible = visible;
        mCrosshair->setVisible(visible && Settings::Manager::getBool("crosshair", "HUD"));
    }

    void HUD::setCrosshairOwned(bool owned)
    {
        const int size = owned ? 32 : 64;
        mCrosshair->changeWidgetSkin(owned ? "HUD_Crosshair_Owned" : "HUD_Crosshair");

        // Keep both reticles exactly centred. The ownership hand is intentionally
        // half the size of the normal crosshair and must not inherit its 64x64 box.
        mCrosshair->setCoord(
            (mMainWidget->getWidth() - size) / 2,
            (mMainWidget->getHeight() - size) / 2,
            size, size);
    }

    void HUD::setHmsVisible(bool visible)
    {
        mHmsBaseVisible = visible;

        mHealth->setVisible(visible);
        mMagicka->setVisible(visible);
        mStamina->setVisible(visible);

        if (!visible)
        {
            mHealthFrame->setVisible(false);
            mMagickaFrame->setVisible(false);
            mFatigueFrame->setVisible(false);
        }
        else
        {
            registerBarChange(mHealthBarState, mHealthBarState.current, mHealthBarState.modified);
            registerBarChange(mMagickaBarState, mMagickaBarState.current, mMagickaBarState.modified);
            registerBarChange(mStaminaBarState, mStaminaBarState.current, mStaminaBarState.modified);

            mHealthFrame->setVisible(true);
            mMagickaFrame->setVisible(true);
            mFatigueFrame->setVisible(true);
            applyBarAlpha(mHealthFrame, 1.f);
            applyBarAlpha(mMagickaFrame, 1.f);
            applyBarAlpha(mFatigueFrame, 1.f);
        }

        updatePositions();
    }

    void HUD::setWeapVisible(bool visible)
    {
        mWeapBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setSpellVisible(bool visible)
    {
        mSpellBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setSneakVisible(bool visible)
    {
        mSneakBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setEffectVisible(bool visible)
    {
        mEffectBaseVisible = visible;
        mEffectBox->setVisible(visible && Settings::Manager::getBool("show status effects", "HUD"));
        updatePositions();
    }

    void HUD::setMinimapVisible(bool visible)
    {
        mMinimapBaseVisible = visible;
        mMinimapBox->setVisible(visible && Settings::Manager::getBool("show minimap", "HUD"));
        updatePositions();
    }

    void HUD::updatePositions()
    {
        int weapDx = 0, spellDx = 0;
        if (!mHealth->getVisible())
            spellDx = weapDx = mWeapBoxBaseLeft - mHealthManaStaminaBaseLeft;

        if (!mWeapBox->getVisible())
            spellDx += mSpellBoxBaseLeft - mWeapBoxBaseLeft;

        mWeaponVisible = mWeapBox->getVisible();
        mSpellVisible = mSpellBox->getVisible();
        if (!mWeaponVisible && !mSpellVisible)
            mWeaponSpellBox->setVisible(false);

        mWeapBox->setPosition(mWeapBoxBaseLeft - weapDx, mWeapBox->getTop());
        mSpellBox->setPosition(mSpellBoxBaseLeft - spellDx, mSpellBox->getTop());

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        mSneakBox->setPosition((viewSize.width - mSneakBox->getWidth()) / 2,
                               (viewSize.height - mSneakBox->getHeight()) / 2);

        if (mHorizontalCompass && mGameTimeBox)
        {
            const int infoWidth = std::max(360, mHorizontalCompass->getWidth());
            constexpr int infoHeight = 28;
            constexpr int infoGap = 1;
            const int infoLeft = (viewSize.width - infoWidth) / 2;
            const int infoTop = mHorizontalCompass->getTop() + mHorizontalCompass->getHeight() + infoGap;
            mGameTimeBox->setCoord(infoLeft, infoTop, infoWidth, infoHeight);
            mGameTimeBox->setFontHeight(18);
            mGameTimeBox->setTextAlign(MyGUI::Align::Center);
        }
        if (mCellNameClip)
            mCellNameClip->setVisible(false);
        if (mCellNameBox)
            mCellNameBox->setVisible(false);

        // effect box can have variable width -> variable left coordinate
        int effectsDx = 0;
        if (!mMinimapBox->getVisible ())
            effectsDx = mEffectBoxBaseRight - mMinimapBoxBaseRight;

        mMapVisible = mMinimapBox->getVisible ();
        mEffectBox->setPosition((viewSize.width - mEffectBoxBaseRight) - mEffectBox->getWidth() + effectsDx, mEffectBox->getTop());
    }

    void HUD::setFocusObject(const MWWorld::Ptr& focus)
    {
        const bool validActor = !focus.isEmpty() && focus.getClass().isActor()
            && focus != MWMechanics::getPlayer()
            && !focus.getClass().getCreatureStats(focus).isDead();

        if (validActor)
        {
            if (mFocusActor.isEmpty() || mFocusActor != focus)
            {
                mFocusActor = focus;
                mFocusActorPanelAlpha = 0.f;
                mTargetPanelPositionInitialized = false;
            }
            mFocusActorCurrentlyFaced = true;
            mFocusActorDistance = MWBase::Environment::get().getWorld()->getDistanceToFacedObject();
        }
        else
        {
            // Keep the previous actor long enough to fade the panel out. Clearing it
            // immediately made name/health/level blink whenever the ray briefly left
            // the actor between two frames.
            mFocusActorCurrentlyFaced = false;
            mFocusActorDistance = -1.f;
        }
    }

    void HUD::setFocusObjectScreenCoords(float min_x, float min_y, float max_x, float max_y)
    {
        mFocusActorScreenX = (min_x + max_x) * 0.5f;
        mFocusActorScreenY = min_y;
    }

    void HUD::updateFocusedTargetPanel(float dt)
    {
        dt = std::max(0.f, dt);

        bool actorAlive = !mFocusActor.isEmpty();
        if (actorAlive)
        {
            actorAlive = mFocusActor.isInCell()
                && mFocusActor.getRefData().getCount() > 0
                && mFocusActor.getRefData().isEnabled()
                && !mFocusActor.getRefData().isDeleted()
                && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead();
        }

        const bool dialogueOpen = MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue);
        const bool hoverEnabled = npcBarShowsHover(getNpcBarMode());
        float desiredAlpha = 0.f;
        bool fastPeacefulPointBlankHide = false;

        bool focusActorInCombat = false;
        if (actorAlive)
        {
            for (const CombatHealthBarState& state : mCombatHealthBars)
            {
                if (!state.mActor.isEmpty() && state.mActor == mFocusActor)
                {
                    focusActorInCombat = true;
                    break;
                }
            }
        }

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        if (actorAlive && !focusActorInCombat
            && mFocusActorCurrentlyFaced && hoverEnabled && !dialogueOpen)
        {
            // Binary visibility: the panel is either fully visible or completely
            // hidden. Keep a generous centre band for large animated actors.
            const float horizontalOffset = std::abs(mFocusActorScreenX - 0.5f);
            constexpr float centreVisibleEnd = 0.46f;
            const bool nearScreenCentre = horizontalOffset <= centreVisibleEnd;

            bool withinActivationDistance = true;
            bool hidePeacefulAtPointBlank = false;
            if (mFocusActorDistance >= 0.f)
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();
                const float maximumDistance = std::max(1.f, world->getMaxActivationDistance());
                withinActivationDistance = mFocusActorDistance <= maximumDistance * 1.05f;

                const MWWorld::Ptr player = MWMechanics::getPlayer();
                const MWMechanics::CreatureStats& targetStats
                    = mFocusActor.getClass().getCreatureStats(mFocusActor);
                const bool aggressiveTarget = targetStats.getAiSequence().isInCombat(player)
                    || MWBase::Environment::get().getMechanicsManager()->isAggressive(mFocusActor, player);

                // Only first-person view hides a peaceful target panel, and only
                // almost face-to-face. Third-person view and hostile targets keep
                // the panel visible at any close distance.
                constexpr float pointBlankDistance = 48.f;
                const float pointBlankLimit = std::min(pointBlankDistance, maximumDistance * 0.25f);
                hidePeacefulAtPointBlank = world->isFirstPerson()
                    && !aggressiveTarget
                    && mFocusActorDistance <= pointBlankLimit;
                fastPeacefulPointBlankHide = hidePeacefulAtPointBlank;
            }

            desiredAlpha = nearScreenCentre && withinActivationDistance
                && !hidePeacefulAtPointBlank ? 1.f : 0.f;

            // The panel remains in the upper centre and follows left/right only
            // slightly. This keeps it readable and prevents jitter from the actor's
            // animated bounding box.
            const float screenCentre = viewSize.width * 0.5f;
            const float maximumTravel = viewSize.width * 0.12f;
            const float actorOffset = (mFocusActorScreenX - 0.5f) * viewSize.width * 0.35f;
            const float desiredCentre = screenCentre
                + std::max(-maximumTravel, std::min(maximumTravel, actorOffset));

            if (!mTargetPanelPositionInitialized)
            {
                mTargetPanelCenterX = desiredCentre;
                mTargetPanelPositionInitialized = true;
            }
            else
            {
                const float positionBlend = 1.f - std::exp(-7.f * dt);
                mTargetPanelCenterX += (desiredCentre - mTargetPanelCenterX) * positionBlend;
            }
        }

        // Appearance is three times faster than before (7 -> 21). The accumulator
        // provides a tiny anti-flicker delay, but rendering below uses a hard 0/1
        // threshold, so no semi-transparent panel is ever shown.
        const float transitionSpeed = desiredAlpha > mFocusActorPanelAlpha
            ? 21.f
            : (fastPeacefulPointBlankHide ? 18.f : 2.4f);
        const float alphaBlend = 1.f - std::exp(-transitionSpeed * dt);
        mFocusActorPanelAlpha += (desiredAlpha - mFocusActorPanelAlpha) * alphaBlend;
        mFocusActorPanelAlpha = std::max(0.f, std::min(1.f, mFocusActorPanelAlpha));

        if ((!actorAlive || !mFocusActorCurrentlyFaced) && mFocusActorPanelAlpha < 0.5f)
        {
            mFocusActor = MWWorld::Ptr();
            mFocusActorPanelAlpha = 0.f;
            mTargetPanelPositionInitialized = false;
        }
    }

    void HUD::hideCombatHealthBars()
    {
        mCombatHealthBarScanTimer = 0.f;
        for (CombatHealthBarState& state : mCombatHealthBars)
        {
            state.mActor = MWWorld::Ptr();
            state.mAlly = false;
            // X024: clear the smoothed geometry too. Keeping it would make the next
            // actor to land in this slot start its fade-in at the previous actor's
            // screen position and slide across the view.
            state.mHasScreenState = false;
            state.mAlpha = 0.f;
            state.mTargetAlpha = 0.f;
            state.mDisplayHealth = -1.f;
            state.mLingerTimer = 0.f;
            state.mDocked = false;
            state.mDockBlend = 0.f;
            state.mDockSwitchTimer = 0.f;
            state.mDockRow = -1;
            state.mNameCaption.clear();
            if (state.mWidget)
                state.mWidget->setVisible(false);
            if (state.mName)
                state.mName->setVisible(false);
        }
    }

    void HUD::applyCombatHealthBar(CombatHealthBarState& state, float dt)
    {
        MyGUI::ProgressBar* bar = state.mWidget;
        if (!bar)
            return;

        const float fadeTau = state.mTargetAlpha > state.mAlpha
            ? CombatBar::sFadeInTime : CombatBar::sFadeOutTime;
        state.mAlpha = CombatBar::approach(state.mAlpha, state.mTargetAlpha, fadeTau, dt);

        if (state.mAlpha <= 0.004f || !state.mHasScreenState)
        {
            state.mAlpha = std::max(0.f, state.mAlpha);
            if (state.mTargetAlpha <= 0.f)
            {
                state.mAlpha = 0.f;
                state.mHasScreenState = false;
                // A fully faded slot must not keep a place in the stack, otherwise
                // it would hold a row open for an actor that is no longer there.
                state.mDocked = false;
                state.mDockBlend = 0.f;
                state.mDockRow = -1;
            }
            bar->setVisible(false);
            if (state.mName)
                state.mName->setVisible(false);
            return;
        }

        const int width = std::max(CombatBar::sHeadMinWidthPixels,
            static_cast<int>(std::lround(state.mWidth)));
        const int height = std::max(CombatBar::sHeadMinHeightPixels,
            static_cast<int>(std::lround(state.mHeight)));
        const int left = static_cast<int>(std::lround(state.mCentreX - width * 0.5f));
        const int top = static_cast<int>(std::lround(state.mCentreY - height * 0.5f));

        const float alpha = std::min(1.f, state.mAlpha);
        bar->setCoord(left, top, width, height);
        bar->setAlpha(alpha);
        bar->setVisible(true);

        // X025: the caption rides directly on top of the bar for the whole trip, so
        // it never detaches or slides in from somewhere else. It is invisible over
        // the head (unreadable at range, and it would clutter a busy fight) and
        // fades in over the last part of the dock transition.
        if (state.mName)
        {
            const float nameReveal = CombatBar::clamp01(
                (state.mDockBlend - CombatBar::sNameFadeStart)
                / std::max(0.001f, 1.f - CombatBar::sNameFadeStart));
            const float nameAlpha = alpha * nameReveal;

            if (nameAlpha <= 0.01f || state.mNameCaption.empty())
            {
                state.mName->setVisible(false);
            }
            else
            {
                const int nameWidth = std::max(width, CombatBar::sDockWidth);
                state.mName->setCoord(left,
                    top - CombatBar::sDockNameGap - CombatBar::sDockNameHeight,
                    nameWidth, CombatBar::sDockNameHeight);
                state.mName->setAlpha(nameAlpha);
                state.mName->setVisible(true);
            }
        }
    }

    void HUD::updateCombatHealthBars(float dt)
    {
        dt = std::max(0.f, dt);

        const bool enabled = npcBarShowsCombat(getNpcBarMode())
            && mGameplayHud && mGameplayHud->getVisible()
            && !MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue);
        if (!enabled)
        {
            hideCombatHealthBars();
            return;
        }

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
        if (!world || !mechanics)
        {
            hideCombatHealthBars();
            return;
        }

        const MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty() || !player.isInCell())
        {
            hideCombatHealthBars();
            return;
        }

        // Rebuild only the participant list at 10 Hz. Projection, distance scaling
        // and health values still update every frame, so bars stay attached to heads.
        mCombatHealthBarScanTimer -= dt;
        if (mCombatHealthBarScanTimer <= 0.f)
        {
            mCombatHealthBarScanTimer = 0.10f;

            struct Candidate
            {
                MWWorld::Ptr mActor;
                bool mAlly = false;
                float mDistanceSquared = 0.f;
            };

            std::map<MWWorld::Ptr, bool> participants; // false = enemy, true = ally
            for (const MWWorld::Ptr& enemy : mechanics->getActorsFighting(player))
            {
                if (!enemy.isEmpty())
                    participants[enemy] = false;
            }

            std::set<MWWorld::Ptr> allies;
            mechanics->getActorsSidingWith(player, allies);
            for (const MWWorld::Ptr& ally : allies)
            {
                if (ally.isEmpty() || participants.count(ally) || !ally.getClass().isActor())
                    continue;

                const MWMechanics::CreatureStats& stats = ally.getClass().getCreatureStats(ally);
                if (!stats.isDead() && stats.getAiSequence().isInCombat())
                    participants[ally] = true;
            }

            const osg::Vec3f playerPosition = player.getRefData().getPosition().asVec3();
            constexpr float maximumBarDistanceSquared
                = CombatBar::sVanishDistance * CombatBar::sVanishDistance;

            std::vector<Candidate> candidates;
            candidates.reserve(participants.size());
            for (const auto& participant : participants)
            {
                const MWWorld::Ptr& actor = participant.first;
                if (actor == player || !sharesCombatSpace(actor, player))
                    continue;
                if (actor.getRefData().getCount() <= 0 || !actor.getRefData().isEnabled()
                    || actor.getRefData().isDeleted() || !actor.getClass().isActor())
                    continue;

                const MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
                if (stats.isDead() || stats.getHealth().getCurrent() <= 0.f)
                    continue;
                if (!world->getLOS(player, actor))
                    continue;

                const osg::Vec3f delta = actor.getRefData().getPosition().asVec3() - playerPosition;
                const float distanceSquared = delta.length2();
                if (distanceSquared > maximumBarDistanceSquared)
                    continue;

                Candidate candidate;
                candidate.mActor = actor;
                candidate.mAlly = participant.second;
                candidate.mDistanceSquared = distanceSquared;
                candidates.push_back(candidate);
            }

            std::sort(candidates.begin(), candidates.end(),
                [](const Candidate& left, const Candidate& right)
                {
                    return left.mDistanceSquared < right.mDistanceSquared;
                });

            const std::size_t capacity = mCombatHealthBars.size();
            if (candidates.size() > capacity)
                candidates.resize(capacity);

            // Keep every actor on the widget it already owns. Re-filling the pool in
            // distance order made two participants swap widgets whenever they traded
            // places, which swapped their colours for a frame and reset the progress
            // bar in the middle of a fight.
            std::vector<bool> slotTaken(capacity, false);
            std::vector<bool> candidatePlaced(candidates.size(), false);

            for (std::size_t i = 0; i < capacity; ++i)
            {
                CombatHealthBarState& state = mCombatHealthBars[i];
                if (state.mActor.isEmpty())
                    continue;

                bool stillFighting = false;
                for (std::size_t c = 0; c < candidates.size(); ++c)
                {
                    if (candidatePlaced[c] || candidates[c].mActor != state.mActor)
                        continue;

                    state.mAlly = candidates[c].mAlly;
                    candidatePlaced[c] = true;
                    slotTaken[i] = true;
                    stillFighting = true;
                    break;
                }

                if (!stillFighting)
                {
                    // X025: the widget is no longer switched off here. Leaving combat
                    // goes through the same fade as everything else, and the row this
                    // actor held in the stack is released at once so the entries above
                    // it start sliding down immediately.
                    state.mActor = MWWorld::Ptr();
                    state.mDocked = false;
                    state.mDockRow = -1;
                }
            }

            // X025: prefer whichever free slot is furthest through its fade-out.
            // Dropping a new actor onto a slot that is still visibly fading would
            // otherwise make the bar slide in from the previous owner's position.
            std::vector<std::size_t> freeSlots;
            freeSlots.reserve(capacity);
            for (std::size_t i = 0; i < capacity; ++i)
            {
                if (!slotTaken[i])
                    freeSlots.push_back(i);
            }

            std::sort(freeSlots.begin(), freeSlots.end(),
                [this](std::size_t left, std::size_t right)
                {
                    return mCombatHealthBars[left].mAlpha < mCombatHealthBars[right].mAlpha;
                });

            std::size_t nextFree = 0;
            for (std::size_t c = 0; c < candidates.size(); ++c)
            {
                if (candidatePlaced[c])
                    continue;
                if (nextFree >= freeSlots.size())
                    break;

                CombatHealthBarState& state = mCombatHealthBars[freeSlots[nextFree]];
                state.mActor = candidates[c].mActor;
                state.mAlly = candidates[c].mAlly;
                // A fresh occupant starts from scratch: no inherited screen position,
                // no inherited row in the stack, no inherited health reading.
                state.mHasScreenState = false;
                state.mAlpha = 0.f;
                state.mTargetAlpha = 0.f;
                state.mDisplayHealth = -1.f;
                state.mLingerTimer = 0.f;
                state.mDocked = false;
                state.mDockBlend = 0.f;
                state.mDockSwitchTimer = 0.f;
                state.mDockRow = -1;
                state.mNameCaption.clear();
                slotTaken[freeSlots[nextFree]] = true;
                ++nextFree;
            }

            // Re-skin against what the widget is actually wearing, not against the
            // previous ally flag: a slot cleared by hideCombatHealthBars resets that
            // flag to "enemy" while the widget still carries the green skin.
            for (CombatHealthBarState& state : mCombatHealthBars)
            {
                if (state.mActor.isEmpty() || !state.mWidget || state.mSkinAlly == state.mAlly)
                    continue;

                state.mWidget->changeWidgetSkin(state.mAlly ? "MW_Progress_Green" : "MW_Progress_Red");
                state.mWidget->setNeedMouseFocus(false);
                state.mSkinAlly = state.mAlly;
            }
        }

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        const osg::Vec3f playerPosition = player.getRefData().getPosition().asVec3();
        const float viewWidth = static_cast<float>(std::max(1, viewSize.width));
        const float viewHeight = static_cast<float>(std::max(1, viewSize.height));

        // ------------------------------------------------------------------
        // X025 pass 1 - resolve every slot and work out its overhead anchor.
        //
        // Nothing is pushed into a widget yet: the docked placement of one bar
        // depends on how many other bars are docked, so the geometry can only be
        // finished once every slot has stated what it wants.
        // ------------------------------------------------------------------
        std::size_t dockedCount = 0;
        for (const CombatHealthBarState& state : mCombatHealthBars)
        {
            if (state.mDocked)
                ++dockedCount;
        }

        for (CombatHealthBarState& state : mCombatHealthBars)
        {
            state.mFrameResolved = false;
            state.mFrameDrop = false;
            state.mFrameAlpha = 1.f;
            state.mFrameDistance = 0.f;

            MyGUI::ProgressBar* bar = state.mWidget;
            if (!bar)
                continue;

            // A slot that cannot be drawn right now is not switched off on the spot.
            // It keeps its last smoothed placement for a short grace period and then
            // fades, so a momentary LOS break, a scan boundary or a participant
            // leaving combat never produces a blink.
            const MWWorld::Ptr& actor = state.mActor;
            if (actor.isEmpty()
                || !sharesCombatSpace(actor, player)
                || actor.getRefData().getCount() <= 0 || !actor.getRefData().isEnabled()
                || actor.getRefData().isDeleted() || !actor.getClass().isActor())
            {
                state.mFrameDrop = true;
                continue;
            }

            MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
            const float maximumHealth = stats.getHealth().getModified();
            const float currentHealth = stats.getHealth().getCurrent();
            if (stats.isDead() || maximumHealth <= 0.f || currentHealth <= 0.f)
            {
                // Death still fades rather than blinking, but without the grace
                // period: there is nothing left to track.
                state.mLingerTimer = 0.f;
                continue;
            }

            const float distance
                = (actor.getRefData().getPosition().asVec3() - playerPosition).length();
            if (distance >= CombatBar::sVanishDistance)
                continue;

            // Size falls off with distance: full size inside the melee radius,
            // shrinking towards sHeadScaleMin as the actor walks away, with the
            // alpha ramp below finishing the job at the vanishing distance.
            const float distanceT = CombatBar::clamp01(
                (distance - CombatBar::sFullSizeDistance)
                / (CombatBar::sVanishDistance - CombatBar::sFullSizeDistance));
            const float scale = std::max(CombatBar::sHeadScaleMin,
                1.f - (1.f - CombatBar::sHeadScaleMin) * distanceT);

            state.mHeadWidth = CombatBar::sHeadWidthMax * scale;
            state.mHeadHeight = CombatBar::sHeadHeightMax * scale;

            float minX = 0.f;
            float minY = 0.f;
            float maxX = 0.f;
            float maxY = 0.f;
            bool anchorUsable = false;

            if (world->getObjectScreenBounds(actor, minX, minY, maxX, maxY))
            {
                // Sit on top of the hit box, and stay there: instead of culling the
                // bar once the head leaves the top of the view, clamp it to the
                // edge. That is exactly the case of a tall or very close actor,
                // where losing the bar would be worst.
                const float headCentreX = (minX + maxX) * 0.5f * viewWidth;
                float headCentreY = minY * viewHeight - state.mHeadHeight * 0.5f
                    - (CombatBar::sHeadClearance + CombatBar::sHeadClearance * scale);
                headCentreY = std::max(headCentreY,
                    state.mHeadHeight * 0.5f + CombatBar::sScreenMargin);

                anchorUsable = headCentreX > -state.mHeadWidth
                    && headCentreX < viewWidth + state.mHeadWidth
                    && headCentreY < viewHeight + state.mHeadHeight;

                if (anchorUsable)
                {
                    state.mHeadCentreX = headCentreX;
                    state.mHeadCentreY = headCentreY;
                }
            }

            // A docked bar survives without a usable anchor. Turning your back on
            // the enemy you are trading blows with must not empty the stack.
            if (!anchorUsable && !state.mDocked)
                continue;

            state.mFrameDistance = distance;
            state.mFrameResolved = true;

            if (distance > CombatBar::sFadeOutStartDistance)
                state.mFrameAlpha = CombatBar::clamp01(
                    (CombatBar::sVanishDistance - distance)
                    / (CombatBar::sVanishDistance - CombatBar::sFadeOutStartDistance));

            // Dock decision: two thresholds plus a dwell timer. An actor pacing
            // around the 15 pace mark holds the new side for sDockSwitchDelay before
            // anything moves, and only gives the row back at 18 paces.
            const bool wantsDock = state.mDocked
                ? distance < CombatBar::sDockExitDistance
                : distance < CombatBar::sDockEnterDistance;

            if (wantsDock == state.mDocked)
            {
                state.mDockSwitchTimer = 0.f;
            }
            else
            {
                state.mDockSwitchTimer += dt;
                if (state.mDockSwitchTimer >= CombatBar::sDockSwitchDelay)
                {
                    state.mDockSwitchTimer = 0.f;
                    if (!wantsDock)
                    {
                        state.mDocked = false;
                        state.mDockRow = -1;
                        if (dockedCount > 0)
                            --dockedCount;
                    }
                    else if (dockedCount < CombatBar::sDockMaxRows)
                    {
                        // The stack is capped on purpose. Anything that does not fit
                        // simply keeps its overhead bar instead of covering the view.
                        state.mDocked = true;
                        state.mDockSequence = ++mCombatDockSequenceCounter;
                        ++dockedCount;
                    }
                }
            }

            // Health is smoothed too, so a hit slides the bar down instead of
            // snapping it. A near-full swing is treated as a heal or a respawn and
            // is applied at once.
            if (state.mDisplayHealth < 0.f
                || std::abs(state.mDisplayHealth - currentHealth) > maximumHealth * 0.9f)
                state.mDisplayHealth = currentHealth;
            else
                state.mDisplayHealth = CombatBar::approach(
                    state.mDisplayHealth, currentHealth, CombatBar::sSmoothTauHealth, dt);

            const float healthFraction = CombatBar::clamp01(state.mDisplayHealth / maximumHealth);
            bar->setProgressRange(static_cast<std::size_t>(CombatBar::sProgressResolution));
            bar->setProgressPosition(static_cast<std::size_t>(
                std::lround(healthFraction * CombatBar::sProgressResolution)));

            if (state.mName)
            {
                const std::string caption = actor.getClass().getName(actor);
                if (caption != state.mNameCaption)
                {
                    state.mNameCaption = caption;
                    state.mName->setCaption(caption);
                }
            }
        }

        // ------------------------------------------------------------------
        // X025 pass 2 - order the stack and find where it starts.
        //
        // Rows are handed out by the order in which actors joined, never by
        // distance: sorting a live list by distance makes entries swap places every
        // time two enemies trade positions, which is the single most unstable thing
        // such a panel can do. When an entry leaves, the ones above it slide down
        // under the ordinary position smoothing.
        // ------------------------------------------------------------------
        std::vector<std::size_t> dockedSlots;
        dockedSlots.reserve(CombatBar::sDockMaxRows);
        for (std::size_t i = 0; i < mCombatHealthBars.size(); ++i)
        {
            if (mCombatHealthBars[i].mDocked)
                dockedSlots.push_back(i);
        }

        std::sort(dockedSlots.begin(), dockedSlots.end(),
            [this](std::size_t left, std::size_t right)
            {
                return mCombatHealthBars[left].mDockSequence
                    < mCombatHealthBars[right].mDockSequence;
            });

        for (std::size_t row = 0; row < dockedSlots.size(); ++row)
            mCombatHealthBars[dockedSlots[row]].mDockRow = static_cast<int>(row);

        // Anchor: the bottom left corner of the stack, sitting directly above the
        // stamina bar and growing upwards. Read from the widget rather than
        // hardcoded, so it follows the HUD layout and any GUI scaling.
        float dockLeft = static_cast<float>(CombatBar::sScreenMargin);
        float dockBottom = viewHeight * 0.5f;
        {
            MyGUI::IntPoint origin;
            if (mGameplayHud)
                origin = mGameplayHud->getAbsolutePosition();
            if (mFatigueFrame)
            {
                const MyGUI::IntCoord frame = mFatigueFrame->getAbsoluteCoord();
                dockLeft = static_cast<float>(frame.left - origin.left);
                dockBottom = static_cast<float>(frame.top - origin.top) - CombatBar::sDockGap;
            }
        }
        dockLeft = std::max(static_cast<float>(CombatBar::sScreenMargin),
            std::min(dockLeft, viewWidth - CombatBar::sDockWidth - CombatBar::sScreenMargin));

        // ------------------------------------------------------------------
        // X025 pass 3 - blend head anchor against stack row, smooth, draw.
        // ------------------------------------------------------------------
        for (CombatHealthBarState& state : mCombatHealthBars)
        {
            if (!state.mWidget)
                continue;

            if (state.mFrameResolved)
            {
                const float dockTarget = state.mDocked ? 1.f : 0.f;
                state.mDockBlend = CombatBar::approach(
                    state.mDockBlend, dockTarget, CombatBar::sSmoothTauDock, dt);
                if (state.mDockBlend < 0.002f)
                    state.mDockBlend = 0.f;
                else if (state.mDockBlend > 0.998f)
                    state.mDockBlend = 1.f;

                float targetCentreX = state.mHeadCentreX;
                float targetCentreY = state.mHeadCentreY;
                float targetWidth = state.mHeadWidth;
                float targetHeight = state.mHeadHeight;

                if (state.mDockBlend > 0.f)
                {
                    const int row = std::max(0, state.mDockRow);
                    const float rowBottom
                        = dockBottom - row * static_cast<float>(CombatBar::sDockRowStride);
                    const float dockCentreX = dockLeft + CombatBar::sDockWidth * 0.5f;
                    const float dockCentreY = rowBottom - CombatBar::sDockBarHeight * 0.5f;

                    targetCentreX = CombatBar::lerp(targetCentreX, dockCentreX, state.mDockBlend);
                    targetCentreY = CombatBar::lerp(targetCentreY, dockCentreY, state.mDockBlend);
                    targetWidth = CombatBar::lerp(targetWidth,
                        static_cast<float>(CombatBar::sDockWidth), state.mDockBlend);
                    targetHeight = CombatBar::lerp(targetHeight,
                        static_cast<float>(CombatBar::sDockBarHeight), state.mDockBlend);
                }

                state.mLingerTimer = CombatBar::sLingerTime;
                state.mTargetAlpha = state.mFrameAlpha;

                if (!state.mHasScreenState)
                {
                    // First frame for this actor: start where it belongs and fade in.
                    state.mCentreX = targetCentreX;
                    state.mCentreY = targetCentreY;
                    state.mWidth = targetWidth;
                    state.mHeight = targetHeight;
                    state.mHasScreenState = true;
                }
                else
                {
                    // The teleport guard is suspended mid-transition: the dock target
                    // is already an interpolation, and letting it snap would defeat
                    // the whole point of the travel animation.
                    const float snapDistance = viewWidth * CombatBar::sSnapFraction;
                    const bool travelling = state.mDockBlend > 0.f && state.mDockBlend < 1.f;

                    if (!travelling
                        && (std::abs(targetCentreX - state.mCentreX) > snapDistance
                            || std::abs(targetCentreY - state.mCentreY) > snapDistance))
                    {
                        // Camera cut or teleport: never drag the bar across the view.
                        state.mCentreX = targetCentreX;
                        state.mCentreY = targetCentreY;
                    }
                    else
                    {
                        state.mCentreX = CombatBar::approach(
                            state.mCentreX, targetCentreX, CombatBar::sSmoothTauX, dt);
                        state.mCentreY = CombatBar::approach(
                            state.mCentreY, targetCentreY, CombatBar::sSmoothTauY, dt);
                    }

                    state.mWidth = CombatBar::approach(
                        state.mWidth, targetWidth, CombatBar::sSmoothTauSize, dt);
                    state.mHeight = CombatBar::approach(
                        state.mHeight, targetHeight, CombatBar::sSmoothTauSize, dt);
                }
            }
            else
            {
                if (state.mFrameDrop)
                    state.mLingerTimer = 0.f;
                else
                    state.mLingerTimer = std::max(0.f, state.mLingerTimer - dt);

                // Hold the last good placement while the grace period runs, then fade.
                if (state.mLingerTimer <= 0.f)
                {
                    state.mTargetAlpha = 0.f;
                    state.mDisplayHealth = -1.f;
                }
            }

            applyCombatHealthBar(state, dt);
        }
    }

    void HUD::updateEnemyHealthBar()
    {
        const std::string npcBarMode = getNpcBarMode();
        const bool usingFocusActor = npcBarShowsHover(npcBarMode)
            && !mFocusActor.isEmpty()
            && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead()
            && !MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue)
            && mFocusActorPanelAlpha >= 0.5f;

        MWWorld::Ptr enemy;
        if (usingFocusActor)
            enemy = mFocusActor;

        if (enemy.isEmpty())
        {
            mEnemyHealth->setVisible(false);
            if (mEnemyName)
                mEnemyName->setVisible(false);
            if (mEnemySummary)
                mEnemySummary->setVisible(false);
            return;
        }

        MWMechanics::CreatureStats& stats = enemy.getClass().getCreatureStats(enemy);
        if (stats.isDead() || stats.getHealth().getCurrent() <= 0.f)
        {
            mEnemyHealth->setVisible(false);
            if (mEnemyName)
                mEnemyName->setVisible(false);
            if (mEnemySummary)
                mEnemySummary->setVisible(false);
            return;
        }

        const float maximumHealth = stats.getHealth().getModified();
        const float currentHealth = stats.getHealth().getCurrent();
        const int maximumHealthPoints = std::max(1, static_cast<int>(std::lround(maximumHealth)));
        const int currentHealthPoints = std::max(0, std::min(maximumHealthPoints,
            static_cast<int>(std::lround(currentHealth))));

        mEnemyHealth->setProgressRange(static_cast<size_t>(maximumHealthPoints));
        mEnemyHealth->setProgressPosition(static_cast<size_t>(currentHealthPoints));

        const float alpha = 1.f;
        mEnemyHealth->setAlpha(alpha);

        if (usingFocusActor)
        {
            // Faced actor: stable name/level/health panel in the upper HUD.
            mEnemyHealth->setSize(190, 16);
            if (mEnemyName)
            {
                mEnemyName->setSize(240, 20);
                mEnemyName->setCaption(enemy.getClass().getName(enemy) + "  -  Уровень "
                    + MyGUI::utility::toString(stats.getLevel()));
                mEnemyName->setAlpha(alpha);
            }
            if (mEnemySummary)
            {
                mEnemySummary->setSize(190, 16);
                mEnemySummary->setCaption(MyGUI::utility::toString(currentHealthPoints) + " / "
                    + MyGUI::utility::toString(maximumHealthPoints));
                mEnemySummary->setAlpha(alpha);
            }

            const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            const int nameWidth = mEnemyName ? mEnemyName->getWidth() : 0;
            const int nameHeight = mEnemyName ? mEnemyName->getHeight() : 0;
            const int barWidth = mEnemyHealth->getWidth();
            const int totalWidth = std::max(nameWidth, barWidth);
            const int totalHeight = nameHeight + 2 + mEnemyHealth->getHeight();

            constexpr int targetPanelSafeMargin = 14;
            const int horizontalMargin = std::min(targetPanelSafeMargin,
                std::max(0, (viewSize.width - totalWidth) / 2));
            const int verticalMargin = std::min(targetPanelSafeMargin,
                std::max(0, (viewSize.height - totalHeight) / 2));
            const int maximumLeft = std::max(horizontalMargin,
                viewSize.width - horizontalMargin - totalWidth);
            const int maximumTop = std::max(verticalMargin,
                viewSize.height - verticalMargin - totalHeight);

            const int stableCentreX = mTargetPanelPositionInitialized
                ? static_cast<int>(std::lround(mTargetPanelCenterX))
                : viewSize.width / 2;
            const int panelLeft = std::max(horizontalMargin,
                std::min(stableCentreX - totalWidth / 2, maximumLeft));

            // Reserve the compass and its shared clock/location line so the panel stays stable.
            int baseY = verticalMargin;
            if (mHorizontalCompass && mHorizontalCompass->getVisible())
            {
                const MyGUI::IntCoord compassCoord = mHorizontalCompass->getAbsoluteCoord();
                constexpr int targetPanelCompassGap = 7;
                baseY = std::max(baseY,
                    compassCoord.top + compassCoord.height + targetPanelCompassGap);
            }
            if (mGameTimeBox)
            {
                const MyGUI::IntCoord infoCoord = mGameTimeBox->getAbsoluteCoord();
                constexpr int targetPanelInfoGap = 5;
                baseY = std::max(baseY, infoCoord.top + infoCoord.height + targetPanelInfoGap);
            }
            baseY = std::min(baseY, maximumTop);

            if (mEnemyName)
                mEnemyName->setPosition(panelLeft + (totalWidth - nameWidth) / 2, baseY);
            const int barLeft = panelLeft + (totalWidth - barWidth) / 2;
            mEnemyHealth->setPosition(barLeft, baseY + nameHeight + 2);
            if (mEnemySummary)
                mEnemySummary->setPosition(barLeft, baseY + nameHeight + 2);
        }
    }

    void HUD::setEnemy(const MWWorld::Ptr &enemy)
    {
        mEnemyActorId = enemy.getClass().getCreatureStats(enemy).getActorId();
        if (mEnemyName)
            mEnemyName->setVisible(false);
        if (mEnemySummary)
            mEnemySummary->setVisible(false);
        mEnemyHealthTimer = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fNPCHealthBarTime")->mValue.getFloat();
        // X014: persistent combat health is rendered in world space above
        // the actual actor. Keep the actor id only for compatibility with callbacks.
    }

    void HUD::resetEnemy()
    {
        mEnemyActorId = -1;
        mEnemyHealthTimer = -1;
        if (mEnemyName) mEnemyName->setVisible(false);
        if (mEnemySummary) mEnemySummary->setVisible(false);
    }

    void HUD::clear()
    {
        unsetSelectedSpell();
        unsetSelectedWeapon();
        resetEnemy();
        mFocusActor = MWWorld::Ptr();
        mFocusActorCurrentlyFaced = false;
        mFocusActorDistance = -1.f;
        mFocusActorPanelAlpha = 0.f;
        mTargetPanelPositionInitialized = false;
        hideCombatHealthBars();
        if (mEnemyName)
            mEnemyName->setVisible(false);
        if (mEnemySummary)
            mEnemySummary->setVisible(false);
    }

    void HUD::customMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);
    }

    void HUD::doorMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);
    }

}
