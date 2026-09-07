#include "spellicons.hpp"

#include <sstream>
#include <iomanip>
#include <set>

#include <MyGUI_ImageBox.h>
#include <MyGUI_LanguageManager.h>

#include <components/esm/loadmgef.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/world.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/classarchetype.hpp"

#include "tooltips.hpp"


namespace MWGui
{

    void EffectSourceVisitor::visit (MWMechanics::EffectKey key, int effectIndex,
                                     const std::string& sourceName, const std::string& sourceId, int casterActorId,
                                     float magnitude, float remainingTime, float totalTime)
    {
        MagicEffectInfo newEffectSource;
        newEffectSource.mKey = key;
        newEffectSource.mMagnitude = static_cast<int>(magnitude);
        newEffectSource.mPermanent = mIsPermanent;
        newEffectSource.mRemainingTime = remainingTime;
        newEffectSource.mSource = sourceName;
        newEffectSource.mTotalTime = totalTime;

        mEffectSources[key.mId].push_back(newEffectSource);
    }


    /*
        Start of AMP addition (Y044)

        mWidgetMap holds raw MyGUI pointers to children of the effect box. Those
        children can be destroyed without this class ever being told, which used
        to leave dangling pointers that were dereferenced on the next frame -
        reliably reproducible while casting, because casting is what adds a new
        effect and forces the strip to be rebuilt.
    */
    SpellIcons::~SpellIcons()
    {
        invalidate();
    }

    void SpellIcons::invalidate()
    {
        mWidgetMap.clear();
        mParent = nullptr;
    }

    void SpellIcons::pruneDeadWidgets(MyGUI::Widget* parent)
    {
        // A cache built for a different parent can never be reused.
        if (parent != mParent)
        {
            mWidgetMap.clear();
            mParent = parent;
            return;
        }

        if (mWidgetMap.empty())
            return;

        // Only widgets that are still live children of the parent may be touched.
        std::set<MyGUI::Widget*> liveChildren;
        const size_t childCount = parent->getChildCount();
        for (size_t i = 0; i < childCount; ++i)
            liveChildren.insert(parent->getChildAt(i));

        for (auto it = mWidgetMap.begin(); it != mWidgetMap.end();)
        {
            if (it->second == nullptr || liveChildren.find(it->second) == liveChildren.end())
                it = mWidgetMap.erase(it);
            else
                ++it;
        }
    }
    /*
        End of AMP addition (Y044)
    */

    void SpellIcons::updateWidgets(MyGUI::Widget *parent, bool adjustSize)
    {
        // TODO: Tracking add/remove/expire would be better than force updating every frame

        /*
            Start of AMP addition (Y044)

            Never work with a cache that outlived its widgets.
        */
        if (parent == nullptr)
        {
            invalidate();
            return;
        }

        pruneDeadWidgets(parent);
        /*
            End of AMP addition (Y044)
        */

        MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);


        EffectSourceVisitor visitor;

        // permanent item enchantments & permanent spells
        visitor.mIsPermanent = true;
        MWWorld::InventoryStore& store = player.getClass().getInventoryStore(player);
        store.visitEffectSources(visitor);
        stats.getSpells().visitEffectSources(visitor);

        // now add lasting effects
        visitor.mIsPermanent = false;
        stats.getActiveSpells().visitEffectSources(visitor);

        // Y038: archetype signature effects are runtime-derived, so they do not
        // exist in Spells/ActiveSpells as ordinary sources. Mirror them into the
        // Active Effects strip for UI only, using the exact same effect list that
        // Actors::adjustMagicEffects injects into CreatureStats.
        MWMechanics::ClassArchetype::DisplayInfo archetypeInfo;
        if (MWMechanics::ClassArchetype::getDisplayInfo(player, false, archetypeInfo))
        {
            std::vector<MWMechanics::ClassArchetype::PassiveEffect> passiveEffects;
            const bool sneaking = MWBase::Environment::get().getMechanicsManager()->isSneaking(player);
            MWMechanics::ClassArchetype::getPassiveMagicEffects(player, sneaking, passiveEffects);
            const std::string sourceName = MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=archetype.label}")
                + ": " + MyGUI::LanguageManager::getInstance().replaceTags(
                    "#{arenamp=archetype." + archetypeInfo.id + ".name}");
            for (const MWMechanics::ClassArchetype::PassiveEffect& passive : passiveEffects)
            {
                MagicEffectInfo info;
                info.mKey = MWMechanics::EffectKey(passive.effectId);
                info.mMagnitude = static_cast<int>(passive.magnitude);
                info.mPermanent = true;
                info.mRemainingTime = -1.f;
                info.mTotalTime = -1.f;
                info.mSource = sourceName;
                visitor.mEffectSources[passive.effectId].push_back(info);
            }
        }

        std::map <int, std::vector<MagicEffectInfo> >& effects = visitor.mEffectSources;

        int w=2;

        for (auto& effectInfoPair : effects)
        {
            const int effectId = effectInfoPair.first;
            const ESM::MagicEffect* effect =
                MWBase::Environment::get().getWorld ()->getStore ().get<ESM::MagicEffect>().find(effectId);

            float remainingDuration = 0;
            float totalDuration = 0;

            std::string sourcesDescription;

            static const float fadeTime = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fMagicStartIconBlink")->mValue.getFloat();

            std::vector<MagicEffectInfo>& effectInfos = effectInfoPair.second;
            bool addNewLine = false;
            for (const MagicEffectInfo& effectInfo : effectInfos)
            {
                if (addNewLine)
                    sourcesDescription += "\n";

                // if at least one of the effect sources is permanent, the effect will never wear off
                if (effectInfo.mPermanent)
                {
                    remainingDuration = fadeTime;
                    totalDuration = fadeTime;
                }
                else
                {
                    remainingDuration = std::max(remainingDuration, effectInfo.mRemainingTime);
                    totalDuration = std::max(totalDuration, effectInfo.mTotalTime);
                }

                sourcesDescription += effectInfo.mSource;

                if (effect->mData.mFlags & ESM::MagicEffect::TargetSkill)
                    sourcesDescription += " (" +
                            MWBase::Environment::get().getWindowManager()->getGameSettingString(
                                ESM::Skill::sSkillNameIds[effectInfo.mKey.mArg], "") + ")";
                if (effect->mData.mFlags & ESM::MagicEffect::TargetAttribute)
                    sourcesDescription += " (" +
                            MWBase::Environment::get().getWindowManager()->getGameSettingString(
                                ESM::Attribute::sGmstAttributeIds[effectInfo.mKey.mArg], "") + ")";

                ESM::MagicEffect::MagnitudeDisplayType displayType = effect->getMagnitudeDisplayType();
                if (displayType == ESM::MagicEffect::MDT_TimesInt)
                {
                    std::string timesInt =  MWBase::Environment::get().getWindowManager()->getGameSettingString("sXTimesINT", "");
                    std::stringstream formatter;
                    formatter << std::fixed << std::setprecision(1) << " " << (effectInfo.mMagnitude / 10.0f) << timesInt;
                    sourcesDescription += formatter.str();
                }
                else if ( displayType != ESM::MagicEffect::MDT_None )
                {
                    sourcesDescription += ": " + MyGUI::utility::toString(effectInfo.mMagnitude);

                    if ( displayType == ESM::MagicEffect::MDT_Percentage )
                        sourcesDescription += MWBase::Environment::get().getWindowManager()->getGameSettingString("spercent", "");
                    else if ( displayType == ESM::MagicEffect::MDT_Feet )
                        sourcesDescription += " " + MWBase::Environment::get().getWindowManager()->getGameSettingString("sfeet", "");
                    else if ( displayType == ESM::MagicEffect::MDT_Level )
                    {
                        sourcesDescription += " " + ((effectInfo.mMagnitude > 1) ?
                            MWBase::Environment::get().getWindowManager()->getGameSettingString("sLevels", "") :
                            MWBase::Environment::get().getWindowManager()->getGameSettingString("sLevel", "") );
                    }
                    else // ESM::MagicEffect::MDT_Points
                    {
                        sourcesDescription += " " + ((effectInfo.mMagnitude > 1) ?
                            MWBase::Environment::get().getWindowManager()->getGameSettingString("spoints", "") :
                            MWBase::Environment::get().getWindowManager()->getGameSettingString("spoint", "") );
                    }
                }
                if (effectInfo.mRemainingTime > -1 && Settings::Manager::getBool("show effect duration","Game"))
                    sourcesDescription += MWGui::ToolTips::getDurationString(effectInfo.mRemainingTime, " #{sDuration}");

                addNewLine = true;
            }

            if (remainingDuration > 0.f)
            {
                MyGUI::ImageBox* image;
                if (mWidgetMap.find(effectId) == mWidgetMap.end())
                {
                    image = parent->createWidget<MyGUI::ImageBox>
                        ("ImageBox", MyGUI::IntCoord(w,2,16,16), MyGUI::Align::Default);
                    mWidgetMap[effectId] = image;

                    image->setImageTexture(MWBase::Environment::get().getWindowManager()->correctIconPath(effect->mIcon));

                    std::string name = ESM::MagicEffect::effectIdToString (effectId);

                    ToolTipInfo tooltipInfo;
                    tooltipInfo.caption = "#{" + name + "}";
                    tooltipInfo.icon = effect->mIcon;
                    tooltipInfo.imageSize = 16;
                    tooltipInfo.wordWrap = false;

                    image->setUserData(tooltipInfo);
                    image->setUserString("ToolTipType", "ToolTipInfo");
                }
                else
                    image = mWidgetMap[effectId];

                image->setPosition(w,2);
                image->setVisible(true);
                w += 16;

                ToolTipInfo* tooltipInfo = image->getUserData<ToolTipInfo>();
                tooltipInfo->text = sourcesDescription;

                // Fade out
                if (totalDuration >= fadeTime && fadeTime > 0.f)
                    image->setAlpha(std::min(remainingDuration/fadeTime, 1.f));
            }
            else if (mWidgetMap.find(effectId) != mWidgetMap.end())
            {
                MyGUI::ImageBox* image = mWidgetMap[effectId];
                image->setVisible(false);
                image->setAlpha(1.f);
            }
        }

        if (adjustSize)
        {
            int s = w + 2;
            if (effects.empty())
                s = 0;
            int diff = parent->getWidth() - s;
            parent->setSize(s, parent->getHeight());
            parent->setPosition(parent->getLeft()+diff, parent->getTop());
        }

        // hide inactive effects
        for (auto& widgetPair : mWidgetMap)
        {
            if (effects.find(widgetPair.first) == effects.end())
                widgetPair.second->setVisible(false);
        }
    }

}
