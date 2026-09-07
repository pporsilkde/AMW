#ifndef MWGUI_SPELLICONS_H
#define MWGUI_SPELLICONS_H

#include <map>
#include <string>
#include <vector>

#include "../mwmechanics/magiceffects.hpp"

namespace MyGUI
{
    class Widget;
    class ImageBox;
}
namespace ESM
{
    struct ENAMstruct;
    struct EffectList;
}

namespace MWGui
{

    // information about a single magic effect source as required for display in the tooltip
    struct MagicEffectInfo
    {
        MagicEffectInfo()
            : mMagnitude(0)
            , mRemainingTime(0.f)
            , mTotalTime(0.f)
            , mPermanent(false)
        {}
        std::string mSource; // display name for effect source (e.g. potion name)
        MWMechanics::EffectKey mKey;
        int mMagnitude;
        float mRemainingTime;
        float mTotalTime;
        bool mPermanent; // the effect is permanent
    };

    class EffectSourceVisitor : public MWMechanics::EffectSourceVisitor
    {
    public:
        bool mIsPermanent;

        std::map <int, std::vector<MagicEffectInfo> > mEffectSources;

        virtual ~EffectSourceVisitor() {}

        void visit (MWMechanics::EffectKey key, int effectIndex,
                            const std::string& sourceName, const std::string& sourceId, int casterActorId,
                            float magnitude, float remainingTime = -1, float totalTime = -1) override;
    };

    class SpellIcons
    {
    public:
        /*
            Start of AMP change (Y044)

            The icon cache used to be a bare map of raw MyGUI pointers that outlived
            the widgets it pointed at. Whenever the effect box was rebuilt or its
            children were destroyed elsewhere, the next update called a virtual
            method on freed memory. Track the parent the cache belongs to and drop
            entries that are no longer live children of it.
        */
        ~SpellIcons();

        void updateWidgets(MyGUI::Widget* parent, bool adjustSize);

        // Forget every cached widget without touching it. Safe to call after the
        // parent or its children have already been destroyed.
        void invalidate();

    private:

        void pruneDeadWidgets(MyGUI::Widget* parent);

        MyGUI::Widget* mParent = nullptr;
        std::map<int, MyGUI::ImageBox*> mWidgetMap;
        /*
            End of AMP change (Y044)
        */
    };

}

#endif
