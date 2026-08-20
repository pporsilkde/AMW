#include "fogmanager.hpp"

#include <algorithm>
#include <limits>

#include <components/esm/loadcell.hpp>
#include <components/fallback/fallback.hpp>
#include <components/sceneutil/util.hpp>

namespace MWRender
{
namespace
{
    // Underwater fog must be fully opaque slightly *before* the far plane, so
    // that geometry being clipped there is already hidden by the water colour.
    constexpr float sUnderwaterFogFarMargin = 0.92f;
}

    FogManager::FogManager()
        : mViewDistance(std::numeric_limits<float>::max())
        , mLandFogStart(0.f)
        , mLandFogEnd(std::numeric_limits<float>::max())
        , mUnderwaterFogStart(0.f)
        , mUnderwaterFogEnd(std::numeric_limits<float>::max())
        , mFogColor(osg::Vec4f())
        , mUnderwaterColor(Fallback::Map::getColour("Water_UnderwaterColor"))
        , mUnderwaterWeight(Fallback::Map::getFloat("Water_UnderwaterColorWeight"))
        , mUnderwaterIndoorFog(Fallback::Map::getFloat("Water_UnderwaterIndoorFog"))
    {
    }

    void FogManager::configure(float viewDistance, const ESM::Cell *cell)
    {
        const osg::Vec4f color = SceneUtil::colourFromRGB(cell->mAmbi.mFog);
        configure(viewDistance, cell->mAmbi.mFogDensity, mUnderwaterIndoorFog, 1.f, 0.f, color);
    }

    void FogManager::configure(float viewDistance, float fogDepth, float underwaterFog,
        float /*dlFactor*/, float /*dlOffset*/, const osg::Vec4f &color)
    {
        mViewDistance = std::max(1.f, viewDistance);

        if (fogDepth == 0.f)
        {
            mLandFogStart = 0.f;
            mLandFogEnd = std::numeric_limits<float>::max();
        }
        else
        {
            mLandFogStart = viewDistance * (1.f - fogDepth);
            mLandFogEnd = viewDistance;
        }

        mUnderwaterFogStart = std::min(viewDistance, 7168.f) * (1.f - underwaterFog);
        mUnderwaterFogEnd = std::min(viewDistance, 7168.f);
        mFogColor = color;
    }

    void FogManager::setViewDistance(float viewDistance)
    {
        viewDistance = std::max(1.f, viewDistance);
        const float noFog = std::numeric_limits<float>::max();

        // Track the live far plane. The underwater envelope below is *not*
        // recomputed from it (that caused pulsing), but it is clamped against
        // it when queried, which is what keeps the cut edge hidden.
        mViewDistance = viewDistance;

        // Preserve the currently configured fog density while moving its start/end
        // together with the adaptive view distance. The max-float sentinel means
        // that this cell/weather intentionally has no distance fog.
        if (mLandFogEnd != noFog && mLandFogEnd > 0.f)
        {
            const float fogDepth = std::clamp(1.f - mLandFogStart / mLandFogEnd, 0.f, 1.f);
            mLandFogEnd = viewDistance;
            mLandFogStart = mLandFogEnd * (1.f - fogDepth);
        }

        // Keep underwater fog independent from adaptive land distance. Updating it
        // every frame made the underwater colour/fog envelope pulse whenever the
        // terrain optimiser adjusted the exterior far plane. Underwater values are
        // recalculated only when the cell, weather or water configuration changes.
    }

    float FogManager::underwaterFogEnd() const
    {
        if (mViewDistance >= std::numeric_limits<float>::max())
            return mUnderwaterFogEnd;
        return std::min(mUnderwaterFogEnd, mViewDistance * sUnderwaterFogFarMargin);
    }

    float FogManager::underwaterFogStart() const
    {
        const float end = underwaterFogEnd();
        if (mUnderwaterFogEnd <= 0.f || end >= mUnderwaterFogEnd)
            return std::min(mUnderwaterFogStart, end);

        // Compress start and end together so the configured fog density (the
        // start/end ratio) survives being squeezed against a nearer far plane.
        return end * (mUnderwaterFogStart / mUnderwaterFogEnd);
    }

    float FogManager::getFogStart(bool isUnderwater) const
    {
        return isUnderwater ? underwaterFogStart() : mLandFogStart;
    }

    float FogManager::getFogEnd(bool isUnderwater) const
    {
        return isUnderwater ? underwaterFogEnd() : mLandFogEnd;
    }

    osg::Vec4f FogManager::getFogColor(bool isUnderwater) const
    {
        if (isUnderwater)
            return mUnderwaterColor * mUnderwaterWeight + mFogColor * (1.f - mUnderwaterWeight);
        return mFogColor;
    }
}
