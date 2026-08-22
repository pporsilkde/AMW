#include "alchemywindow.hpp"

#include <MyGUI_Gui.h>
#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_ControllerManager.h>
#include <MyGUI_ControllerRepeatClick.h>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/magiceffects.hpp"
#include "../mwmechanics/alchemy.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/alchemyknowledge.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include <MyGUI_Macros.h>
#include <components/esm/records.hpp>
#include <components/misc/stringops.hpp>

#include "inventoryitemmodel.hpp"
#include "sortfilteritemmodel.hpp"
#include "itemview.hpp"
#include "itemwidget.hpp"
#include "widgets.hpp"

namespace MWGui
{
    AlchemyWindow::AlchemyWindow()
        : WindowBase("openmw_alchemy_window.layout")
        , mCurrentFilter(FilterType::ByName)
        , mModel(nullptr)
        , mSortModel(nullptr)
        , mAlchemy(new MWMechanics::Alchemy())
        , mApparatus (4)
        , mIngredients (4)
    {
        getWidget(mCreateButton, "CreateButton");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mPotionModeButton, "PotionMode");
        getWidget(mPoisonModeButton, "PoisonMode");
        getWidget(mLastBrewText, "LastBrew");
        getWidget(mRepeatLastButton, "RepeatLast");
        getWidget(mIngredients[0], "Ingredient1");
        getWidget(mIngredients[1], "Ingredient2");
        getWidget(mIngredients[2], "Ingredient3");
        getWidget(mIngredients[3], "Ingredient4");
        getWidget(mApparatus[0], "Apparatus1");
        getWidget(mApparatus[1], "Apparatus2");
        getWidget(mApparatus[2], "Apparatus3");
        getWidget(mApparatus[3], "Apparatus4");
        getWidget(mEffectsBox, "CreatedEffects");
        getWidget(mBrewCountEdit, "BrewCount");
        getWidget(mIncreaseButton, "IncreaseButton");
        getWidget(mDecreaseButton, "DecreaseButton");
        getWidget(mNameEdit, "NameEdit");
        getWidget(mItemView, "ItemView");
        getWidget(mFilterValue, "FilterValue");
        getWidget(mFilterType, "FilterType");

        mBrewCountEdit->eventValueChanged += MyGUI::newDelegate(this, &AlchemyWindow::onCountValueChanged);
        mBrewCountEdit->eventEditSelectAccept += MyGUI::newDelegate(this, &AlchemyWindow::onAccept);
        mBrewCountEdit->setMinValue(1);
        mBrewCountEdit->setValue(1);

        mIncreaseButton->eventMouseButtonPressed += MyGUI::newDelegate(this, &AlchemyWindow::onIncreaseButtonPressed);
        mIncreaseButton->eventMouseButtonReleased += MyGUI::newDelegate(this, &AlchemyWindow::onCountButtonReleased);
        mDecreaseButton->eventMouseButtonPressed += MyGUI::newDelegate(this, &AlchemyWindow::onDecreaseButtonPressed);
        mDecreaseButton->eventMouseButtonReleased += MyGUI::newDelegate(this, &AlchemyWindow::onCountButtonReleased);

        mItemView->eventItemClicked += MyGUI::newDelegate(this, &AlchemyWindow::onSelectedItem);

        mIngredients[0]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onIngredientSelected);
        mIngredients[1]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onIngredientSelected);
        mIngredients[2]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onIngredientSelected);
        mIngredients[3]->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onIngredientSelected);

        mCreateButton->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onCreateButtonClicked);
        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onCancelButtonClicked);
        mPotionModeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onModeButtonClicked);
        mPoisonModeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onModeButtonClicked);
        mRepeatLastButton->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::onRepeatLastClicked);

        mNameEdit->eventEditSelectAccept += MyGUI::newDelegate(this, &AlchemyWindow::onAccept);
        mFilterValue->eventComboChangePosition += MyGUI::newDelegate(this, &AlchemyWindow::onFilterChanged);
        mFilterValue->eventEditTextChange += MyGUI::newDelegate(this, &AlchemyWindow::onFilterEdited);
        mFilterType->eventMouseButtonClick += MyGUI::newDelegate(this, &AlchemyWindow::switchFilterType);

        center();
    }

    void AlchemyWindow::onAccept(MyGUI::EditBox* sender)
    {
        onCreateButtonClicked(sender);

        // To do not spam onAccept() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    void AlchemyWindow::onCancelButtonClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Alchemy);
    }

    void AlchemyWindow::onModeButtonClicked(MyGUI::Widget* sender)
    {
        mAlchemy->setMode(sender == mPoisonModeButton
            ? MWMechanics::Alchemy::Mode_Poison : MWMechanics::Alchemy::Mode_Potion);
        update();
    }

    void AlchemyWindow::onRepeatLastClicked(MyGUI::Widget*)
    {
        const std::vector<std::string>& recipe = MWMechanics::AlchemyKnowledge::getLastRecipe();
        if (recipe.empty())
            return;

        for (int i = 0; i < 4; ++i)
            mAlchemy->removeIngredient(i);

        mAlchemy->setMode(MWMechanics::AlchemyKnowledge::getLastMode() == static_cast<int>(MWMechanics::Alchemy::Mode_Poison)
            ? MWMechanics::Alchemy::Mode_Poison : MWMechanics::Alchemy::Mode_Potion);

        for (const std::string& id : recipe)
        {
            for (std::size_t i = 0; i < mModel->getItemCount(); ++i)
            {
                MWWorld::Ptr item = mModel->getItem(i).mBase;
                if (item.getTypeName() == typeid(ESM::Ingredient).name()
                    && Misc::StringUtils::ciEqual(item.getCellRef().getRefId(), id))
                {
                    mAlchemy->addIngredient(item);
                    break;
                }
            }
        }
        update();
    }

    void AlchemyWindow::onCreateButtonClicked(MyGUI::Widget* _sender)
    {
        mAlchemy->setPotionName(mNameEdit->getCaption());
        int count = mAlchemy->countPotionsToBrew();
        count = std::min(count, mBrewCountEdit->getValue());
        createPotions(count);
    }

    void AlchemyWindow::createPotions(int count)
    {
        MWMechanics::Alchemy::Result result = mAlchemy->create(mNameEdit->getCaption(), count);
        MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();

        switch (result)
        {
        case MWMechanics::Alchemy::Result_NoName:
            winMgr->messageBox("#{sNotifyMessage37}");
            break;
        case MWMechanics::Alchemy::Result_NoMortarAndPestle:
            winMgr->messageBox("#{sNotifyMessage45}");
            break;
        case MWMechanics::Alchemy::Result_LessThanTwoIngredients:
            winMgr->messageBox("#{sNotifyMessage6a}");
            break;
        case MWMechanics::Alchemy::Result_Success:
            winMgr->playSound("potion success");
            if (count == 1)
                winMgr->messageBox("#{sPotionSuccess}");
            else
                winMgr->messageBox("#{sPotionSuccess} "+mNameEdit->getCaption()+" ("+std::to_string(count)+")");
            break;
        case MWMechanics::Alchemy::Result_NoEffects:
        case MWMechanics::Alchemy::Result_RandomFailure:
            winMgr->messageBox("#{sNotifyMessage8}");
            winMgr->playSound("potion fail");
            break;
        }

        // remove ingredient slots that have been fully used up
        for (int i=0; i<4; ++i)
            if (mIngredients[i]->isUserString("ToolTipType"))
            {
                MWWorld::Ptr ingred = *mIngredients[i]->getUserData<MWWorld::Ptr>();
                if (ingred.getRefData().getCount() == 0)
                    removeIngredient(mIngredients[i]);
            }

        updateFilters();
        update();
    }

    void AlchemyWindow::initFilter()
    {
        auto const& wm = MWBase::Environment::get().getWindowManager();
        auto const ingredient  = wm->getGameSettingString("sIngredients", "Ingredients");
        auto const effect = wm->getGameSettingString("sMagicEffects", "Magic Effects");

        if (mFilterType->getCaption() == ingredient)
            mCurrentFilter = FilterType::ByName;
        else
            mCurrentFilter = FilterType::ByEffect;
        updateFilters();
        mFilterValue->clearIndexSelected();
        updateFilters();
    }

    void AlchemyWindow::switchFilterType(MyGUI::Widget* _sender)
    {
        auto const& wm = MWBase::Environment::get().getWindowManager();
        auto const ingredient  = wm->getGameSettingString("sIngredients", "Ingredients");
        auto const effect = wm->getGameSettingString("sMagicEffects", "Magic Effects");
        auto *button = _sender->castType<MyGUI::Button>();

        if (button->getCaption() == ingredient)
        {
            button->setCaption(effect);
            mCurrentFilter = FilterType::ByEffect;
        }
        else
        {
            button->setCaption(ingredient);
            mCurrentFilter = FilterType::ByName;
        }
        mSortModel->setNameFilter({});
        mSortModel->setEffectFilter({});
        mFilterValue->clearIndexSelected();
        updateFilters();
        mItemView->update();
    }

    void AlchemyWindow::updateFilters()
    {
        std::set<std::string> itemNames, itemEffects;
        for (size_t i = 0; i < mModel->getItemCount(); ++i)
        {
            MWWorld::Ptr item = mModel->getItem(i).mBase;
            if (item.getTypeName() != typeid(ESM::Ingredient).name())
                continue;

            itemNames.insert(item.getClass().getName(item));

            MWWorld::Ptr player = MWBase::Environment::get().getWorld ()->getPlayerPtr();
            auto const alchemySkill = player.getClass().getNpcStats(player).getSkill(ESM::Skill::Alchemy).getBase();

            auto const effects = MWMechanics::Alchemy::effectsDescription(item, alchemySkill);
            itemEffects.insert(effects.begin(), effects.end());
        }

        mFilterValue->removeAllItems();
        auto const addItems = [&](auto const& container)
        {
            for (auto const& item : container)
                mFilterValue->addItem(item);
        };
        switch (mCurrentFilter)
        {
            case FilterType::ByName: addItems(itemNames); break;
            case FilterType::ByEffect: addItems(itemEffects); break;
        }
    }

    void AlchemyWindow::applyFilter(const std::string& filter)
    {
        switch (mCurrentFilter)
        {
            case FilterType::ByName: mSortModel->setNameFilter(filter); break;
            case FilterType::ByEffect: mSortModel->setEffectFilter(filter); break;
        }
        mItemView->update();
    }

    void AlchemyWindow::onFilterChanged(MyGUI::ComboBox* _sender, size_t _index)
    {
        // ignore spurious event fired when one edit the content after selection.
        // onFilterEdited will handle it.
        if (_index != MyGUI::ITEM_NONE)
            applyFilter(_sender->getItemNameAt(_index));
    }

    void AlchemyWindow::onFilterEdited(MyGUI::EditBox* _sender)
    {
        applyFilter(_sender->getCaption());
    }

    void AlchemyWindow::onOpen()
    {
        mAlchemy->clear();
        mAlchemy->setAlchemist (MWMechanics::getPlayer());
        mAlchemy->setMode(MWMechanics::Alchemy::Mode_Potion);

        mModel = new InventoryItemModel(MWMechanics::getPlayer());
        mSortModel = new SortFilterItemModel(mModel);
        mSortModel->setFilter(SortFilterItemModel::Filter_OnlyIngredients);
        mItemView->setModel (mSortModel);
        mItemView->resetScrollBars();

        mNameEdit->setCaption("");
        mBrewCountEdit->setValue(1);

        int index = 0;
        for (MWMechanics::Alchemy::TToolsIterator iter (mAlchemy->beginTools());
            iter!=mAlchemy->endTools() && index<static_cast<int> (mApparatus.size()); ++iter, ++index)
        {
            mApparatus.at (index)->setItem(*iter);
            mApparatus.at (index)->clearUserStrings();
            if (!iter->isEmpty())
            {
                mApparatus.at (index)->setUserString ("ToolTipType", "ItemPtr");
                mApparatus.at (index)->setUserData (MWWorld::Ptr(*iter));
            }
        }

        update();
        initFilter();

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mNameEdit);
    }

    void AlchemyWindow::onIngredientSelected(MyGUI::Widget* _sender)
    {
        removeIngredient(_sender);
        update();
    }

    void AlchemyWindow::onSelectedItem(int index)
    {
        MWWorld::Ptr item = mSortModel->getItem(index).mBase;
        int res = mAlchemy->addIngredient(item);

        if (res != -1)
        {
            update();

            std::string sound = item.getClass().getUpSoundId(item);
            MWBase::Environment::get().getWindowManager()->playSound(sound);
        }
    }

    void AlchemyWindow::update()
    {
        std::string suggestedName = mAlchemy->suggestPotionName();
        if (suggestedName != mSuggestedPotionName)
            mNameEdit->setCaptionWithReplacing(suggestedName);
        mSuggestedPotionName = suggestedName;

        mSortModel->clearDragItems();

        MWMechanics::Alchemy::TIngredientsIterator it = mAlchemy->beginIngredients ();
        for (int i=0; i<4; ++i)
        {
            ItemWidget* ingredient = mIngredients[i];

            MWWorld::Ptr item;
            if (it != mAlchemy->endIngredients ())
            {
                item = *it;
                ++it;
            }

            if (!item.isEmpty())
                mSortModel->addDragItem(item, item.getRefData().getCount());

            if (ingredient->getChildCount())
                MyGUI::Gui::getInstance().destroyWidget(ingredient->getChildAt(0));

            ingredient->clearUserStrings ();

            ingredient->setItem(item);

            if (item.isEmpty ())
                continue;

            ingredient->setUserString("ToolTipType", "ItemPtr");
            ingredient->setUserData(MWWorld::Ptr(item));

            ingredient->setCount(item.getRefData().getCount());
        }

        mItemView->update();

        mPotionModeButton->setStateSelected(mAlchemy->getMode() == MWMechanics::Alchemy::Mode_Potion);
        mPoisonModeButton->setStateSelected(mAlchemy->getMode() == MWMechanics::Alchemy::Mode_Poison);

        // countPotionsToBrew() validates the current name as well as the ingredients.
        // Keep the mechanics-side name synchronized with the edit box so the live
        // batch limit remains correct before the Create button is pressed.
        mAlchemy->setPotionName(mNameEdit->getCaption());
        const int maxBrew = mAlchemy->countPotionsToBrew();
        mBrewCountEdit->setMaxValue(std::max(1, maxBrew));
        if (maxBrew > 0 && mBrewCountEdit->getValue() > maxBrew)
            mBrewCountEdit->setValue(maxBrew);

        const std::string& lastBrew = MWMechanics::AlchemyKnowledge::getLastBrew();
        mLastBrewText->setCaptionWithReplacing("#{arenamp=alchemy.last_brew}: "
            + (lastBrew.empty() ? std::string("-") : lastBrew));
        mRepeatLastButton->setEnabled(!MWMechanics::AlchemyKnowledge::getLastRecipe().empty());

        Widgets::SpellEffectList list;
        for (const ESM::ENAMstruct& effect : mAlchemy->getEffects())
        {
            Widgets::SpellEffectParams params;
            params.mEffectID = effect.mEffectID;
            params.mSkill = effect.mSkill;
            params.mAttribute = effect.mAttribute;
            params.mMagnMin = effect.mMagnMin;
            params.mMagnMax = effect.mMagnMax;
            params.mDuration = effect.mDuration;
            params.mRange = effect.mRange;
            params.mArea = effect.mArea;
            params.mNoTarget = true;
            params.mKnown = true;

            const ESM::MagicEffect* magicEffect = MWBase::Environment::get().getWorld()->getStore()
                .get<ESM::MagicEffect>().find(effect.mEffectID);
            params.mNoMagnitude = (magicEffect->mData.mFlags & ESM::MagicEffect::NoMagnitude) != 0;
            params.mIsConstant = (magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration) != 0;
            list.push_back(params);
        }

        while (mEffectsBox->getChildCount())
            MyGUI::Gui::getInstance().destroyWidget(mEffectsBox->getChildAt(0));

        MyGUI::IntCoord coord(0, 0, mEffectsBox->getWidth(), 24);
        Widgets::MWEffectListPtr effectsWidget = mEffectsBox->createWidget<Widgets::MWEffectList>
            ("MW_StatName", coord, MyGUI::Align::Left | MyGUI::Align::Top);

        effectsWidget->setEffectList(list);

        std::vector<MyGUI::Widget*> effectItems;
        effectsWidget->createEffectWidgets(effectItems, mEffectsBox, coord, false, 0);
        effectsWidget->setCoord(coord);
    }

    void AlchemyWindow::removeIngredient(MyGUI::Widget* ingredient)
    {
        for (int i=0; i<4; ++i)
            if (mIngredients[i] == ingredient)
                mAlchemy->removeIngredient (i);

        update();
    }

    void AlchemyWindow::addRepeatController(MyGUI::Widget *widget)
    {
        MyGUI::ControllerItem* item = MyGUI::ControllerManager::getInstance().createItem(MyGUI::ControllerRepeatClick::getClassTypeName());
        MyGUI::ControllerRepeatClick* controller = static_cast<MyGUI::ControllerRepeatClick*>(item);
        controller->eventRepeatClick += newDelegate(this, &AlchemyWindow::onRepeatClick);
        MyGUI::ControllerManager::getInstance().addItem(widget, controller);
    }

    void AlchemyWindow::onIncreaseButtonPressed(MyGUI::Widget* _sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        addRepeatController(_sender);
        onIncreaseButtonTriggered();
    }

    void AlchemyWindow::onDecreaseButtonPressed(MyGUI::Widget* _sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        addRepeatController(_sender);
        onDecreaseButtonTriggered();
    }

    void AlchemyWindow::onRepeatClick(MyGUI::Widget* widget, MyGUI::ControllerItem* controller)
    {
        if (widget == mIncreaseButton)
            onIncreaseButtonTriggered();
        else if (widget == mDecreaseButton)
            onDecreaseButtonTriggered();
    }

    void AlchemyWindow::onCountButtonReleased(MyGUI::Widget *_sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        MyGUI::ControllerManager::getInstance().removeItem(_sender);
    }

    void AlchemyWindow::onCountValueChanged(int value)
    {
        mBrewCountEdit->setValue(std::abs(value));
    }

    void AlchemyWindow::onIncreaseButtonTriggered()
    {
        int currentCount = mBrewCountEdit->getValue();

        // prevent overflows
        if (currentCount == std::numeric_limits<int>::max())
            return;

        mBrewCountEdit->setValue(currentCount+1);
    }

    void AlchemyWindow::onDecreaseButtonTriggered()
    {
        int currentCount = mBrewCountEdit->getValue();
        if (currentCount > 1)
            mBrewCountEdit->setValue(currentCount-1);
    }
}
