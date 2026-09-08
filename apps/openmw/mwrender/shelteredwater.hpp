#ifndef OPENMW_MWRENDER_SHELTEREDWATER_H
#define OPENMW_MWRENDER_SHELTEREDWATER_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <osg/FrameStamp>
#include <osg/NodeVisitor>
#include "shelteredwatertransition.hpp"

#include <osg/Vec3f>

#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/cellstore.hpp"

namespace MWRender
{
    // ArenaMP Y049: estimate whether the water around the local player is a
    // sheltered/landlocked body of water. A direction counts as blocked when
    // terrain rises to the water line within the configured fetch radius. This
    // deliberately uses geometry instead of cell names so modded lakes, canals,
    // rivers and coves work without per-world lists.
    inline float sampleShelteredWaterFactor(float waterLevel, bool interior)
    {
        if (!Settings::Manager::getBool("auto sheltered water", "Water"))
            return 0.f;
        if (interior)
            return 1.f;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (!world)
            return 0.f;

        MWWorld::Ptr player = world->getPlayerPtr();
        if (player.isEmpty() || !player.isInCell() || !player.getCell()->isExterior())
            return 0.f;

        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        const float radius = std::clamp(Settings::Manager::getFloat("sheltered detection radius", "Water"), 256.f, 8192.f);
        const float landMargin = std::clamp(Settings::Manager::getFloat("sheltered land margin", "Water"), -64.f, 256.f);
        const float threshold = std::clamp(Settings::Manager::getFloat("sheltered enclosure threshold", "Water"), 0.25f, 0.95f);

        // Recompute only after a meaningful move/water-level change. Two callers
        // (world-state caustics and water material) share this inline cache.
        static osg::Vec3f lastPos(std::numeric_limits<float>::max(), 0.f, 0.f);
        static float lastWater = std::numeric_limits<float>::max();
        static float lastRadius = -1.f;
        static float lastThreshold = -1.f;
        static float lastMargin = std::numeric_limits<float>::max();
        static float cached = 0.f;
        const osg::Vec2f move(playerPos.x() - lastPos.x(), playerPos.y() - lastPos.y());
        if (move.length2() < 256.f * 256.f && std::abs(lastWater - waterLevel) < 1.f
            && std::abs(lastRadius - radius) < 1.f && std::abs(lastThreshold - threshold) < 0.001f
            && std::abs(lastMargin - landMargin) < 0.001f)
            return cached;

        constexpr int directions = 16;
        constexpr int radialSamples = 3;
        int blocked = 0;
        for (int i = 0; i < directions; ++i)
        {
            const float angle = static_cast<float>(i) * 6.28318530718f / static_cast<float>(directions);
            const float dx = std::cos(angle);
            const float dy = std::sin(angle);
            bool hitLand = false;
            for (int r = 1; r <= radialSamples; ++r)
            {
                const float dist = radius * static_cast<float>(r) / static_cast<float>(radialSamples);
                osg::Vec3f sample(playerPos.x() + dx * dist, playerPos.y() + dy * dist, waterLevel);
                const float terrain = world->getTerrainHeightAt(sample);
                if (std::isfinite(terrain) && terrain >= waterLevel + landMargin)
                {
                    hitLand = true;
                    break;
                }
            }
            if (hitLand)
                ++blocked;
        }

        const float enclosure = static_cast<float>(blocked) / static_cast<float>(directions);
        const float denom = std::max(0.001f, 1.f - threshold);
        float factor = std::clamp((enclosure - threshold) / denom, 0.f, 1.f);
        // Smooth the transition between open sea, coves and fully landlocked water.
        factor = factor * factor * (3.f - 2.f * factor);

        lastPos = playerPos;
        lastWater = waterLevel;
        lastRadius = radius;
        lastThreshold = threshold;
        lastMargin = landMargin;
        cached = factor;
        return factor;
    }
    inline float getShelteredWaterFactor(float waterLevel, bool interior, osg::NodeVisitor* visitor)
    {
        const float target = sampleShelteredWaterFactor(waterLevel, interior);
        static ShelteredWaterTransition transition;
        if (!visitor || !visitor->getFrameStamp())
            return target;
        return transition.update(target, visitor->getFrameStamp()->getSimulationTime());
    }
}

#endif
