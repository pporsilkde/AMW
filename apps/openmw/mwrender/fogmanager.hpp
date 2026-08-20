#ifndef OPENMW_MWRENDER_FOGMANAGER_H
#define OPENMW_MWRENDER_FOGMANAGER_H

#include <osg/Vec4f>

namespace ESM
{
    struct Cell;
}

namespace MWRender
{
    class FogManager
    {
    public:
        FogManager();

        void configure(float viewDistance, const ESM::Cell *cell);
        void configure(float viewDistance, float fogDepth, float underwaterFog, float dlFactor, float dlOffset, const osg::Vec4f &color);
        void setViewDistance(float viewDistance);

        osg::Vec4f getFogColor(bool isUnderwater) const;
        float getFogStart(bool isUnderwater) const;
        float getFogEnd(bool isUnderwater) const;

    private:
        // Underwater fog has to saturate before the far plane clips geometry,
        // otherwise terrain just ends mid-water and the cut edge is visible.
        // These compress the configured envelope against the *current* view
        // distance without disturbing the stable configured values.
        float underwaterFogStart() const;
        float underwaterFogEnd() const;

        float mViewDistance;
        float mLandFogStart;
        float mLandFogEnd;
        float mUnderwaterFogStart;
        float mUnderwaterFogEnd;
        osg::Vec4f mFogColor;

        osg::Vec4f mUnderwaterColor;
        float mUnderwaterWeight;
        float mUnderwaterIndoorFog;
    };
}

#endif
