#ifndef OPENMW_MWRENDER_SHELTEREDWATERTRANSITION_H
#define OPENMW_MWRENDER_SHELTEREDWATERTRANSITION_H

#include <algorithm>
#include <cmath>

namespace MWRender
{
    class ShelteredWaterTransition
    {
        bool mInitialized = false;
        double mTime = 0.0;
        float mValue = 0.f;

    public:
        float update(float target, double time)
        {
            target = std::clamp(target, 0.f, 1.f);
            if (!mInitialized || time < mTime)
            {
                mInitialized = true;
                mTime = time;
                mValue = target;
                return mValue;
            }
            // Limit jumps after a loading stall. Normal convergence is independent of FPS.
            const float dt = static_cast<float>(std::clamp(time - mTime, 0.0, 0.25));
            mTime = time;
            mValue += (target - mValue) * (1.f - std::exp(-dt / 0.8f));
            return mValue;
        }
    };
}
#endif
