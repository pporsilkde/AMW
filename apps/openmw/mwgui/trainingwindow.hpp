#ifndef MWGUI_TRAININGWINDOW_H
#define MWGUI_TRAININGWINDOW_H

#include "windowbase.hpp"
#include "referenceinterface.hpp"
#include "timeadvancer.hpp"
#include "waitdialog.hpp"

namespace MWMechanics
{
    class NpcStats;
}

namespace MWGui
{

    class TrainingWindow : public WindowBase, public ReferenceInterface
    {
    public:
        TrainingWindow();

        void onOpen() override;

        bool exit() override;

        void setPtr(const MWWorld::Ptr& actor) override;

        void onFrame(float dt) override;

        WindowBase* getProgressBar() { return &mProgressBar; }

        void clear() override { resetReference(); }

    protected:
        void onReferenceUnavailable() override;

        void onCancelButtonClicked (MyGUI::Widget* sender);
        void onTrainingSelected(MyGUI::Widget* sender);

        void onTrainingProgressChanged(int cur, int total);
        void onTrainingFinished();

        // Training eligibility is always based on the permanent/base skill value.
        // Temporary Drain/Fortify effects must never alter trainer capability.
        float getSkillForTraining(const MWMechanics::NpcStats& stats, int skillId) const;

        MyGUI::Widget* mTrainingOptions;
        MyGUI::Button* mCancelButton;
        MyGUI::TextBox* mPlayerGold;

        WaitDialogProgressBar mProgressBar;
        TimeAdvancer mTimeAdvancer;
    };

}

#endif
