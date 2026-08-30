#include "terrainoccluder.hpp"
#include "storage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <osg/Array>

namespace Terrain
{
    TerrainOccluder::TerrainOccluder(Storage* storage, float cellWorldSize)
        : mStorage(storage)
        , mCellWorldSize(cellWorldSize)
        , mScratchPositions(new osg::Vec3Array)
    {
    }

    bool TerrainOccluder::hasTerrainData() const
    {
        return mStorage != nullptr;
    }

    void TerrainOccluder::setLodLevel(int lod)
    {
        lod = std::max(0, lod);
        if (mLodLevel == lod)
            return;

        mLodLevel = lod;
        invalidateCache();
    }

    void TerrainOccluder::setCellBuildBudget(int cellsPerBuild)
    {
        mCellBuildBudget = cellsPerBuild;
    }

    void TerrainOccluder::publishStats()
    {
        mPublishedCachedCellCount.store(static_cast<unsigned int>(mCellCache.size()), std::memory_order_relaxed);
        mPublishedLastBuiltCells.store(mLastBuiltCells, std::memory_order_relaxed);
        mPublishedLastVisibleCells.store(mLastVisibleCells, std::memory_order_relaxed);
        mPublishedRegionComplete.store(mRegionCacheValid, std::memory_order_relaxed);
    }

    void TerrainOccluder::invalidateCache()
    {
        mRegionCacheValid = false;
        mCachedRadius = -1;
        mCellCache.clear();
        mLastBuiltCells = 0;
        mLastVisibleCells = 0;
        if (mScratchPositions.valid())
            mScratchPositions->clear();
        publishStats();
    }

    TerrainOccluder::CachedCellMesh TerrainOccluder::buildCell(int cellX, int cellY) const
    {
        CachedCellMesh result;

        const osg::Vec2f center(cellX + 0.5f, cellY + 0.5f);
        if (!mScratchPositions.valid())
            mScratchPositions = new osg::Vec3Array;
        mScratchPositions->clear();
        osg::ref_ptr<osg::Vec3Array> normals(new osg::Vec3Array);
        osg::ref_ptr<osg::Vec4ubArray> colors(new osg::Vec4ubArray);
        colors->setNormalize(true);
        mStorage->fillVertexBuffers(0, 1.0f, center, mScratchPositions, normals, colors);
        if (mScratchPositions->empty())
            return result;

        const int fullPerSide = static_cast<int>(std::sqrt(static_cast<float>(mScratchPositions->size())));
        if (fullPerSide < 2)
            return result;

        const int safeLod = std::min(mLodLevel, 15);
        const int step = std::max(1, 1 << safeLod);
        const int coarsePerSide = (fullPerSide - 1) / step + 1;
        if (coarsePerSide < 2)
            return result;

        std::vector<float> quadMins((coarsePerSide - 1) * (coarsePerSide - 1));
        for (int qj = 0; qj < coarsePerSide - 1; ++qj)
        {
            for (int qi = 0; qi < coarsePerSide - 1; ++qi)
            {
                const int startI = qi * step;
                const int startJ = qj * step;
                const int endI = std::min((qi + 1) * step, fullPerSide - 1);
                const int endJ = std::min((qj + 1) * step, fullPerSide - 1);
                float minH = std::numeric_limits<float>::max();
                for (int fj = startJ; fj <= endJ; ++fj)
                    for (int fi = startI; fi <= endI; ++fi)
                        minH = std::min(minH, (*mScratchPositions)[fj * fullPerSide + fi].z());
                quadMins[qj * (coarsePerSide - 1) + qi] = minH;
            }
        }

        const osg::Vec3f worldOffset(center.x() * mCellWorldSize, center.y() * mCellWorldSize, 0.0f);
        result.mPositions.reserve(static_cast<std::size_t>(coarsePerSide * coarsePerSide));
        for (int cj = 0; cj < coarsePerSide; ++cj)
        {
            for (int ci = 0; ci < coarsePerSide; ++ci)
            {
                float minH = std::numeric_limits<float>::max();
                for (int dj = -1; dj <= 0; ++dj)
                    for (int di = -1; di <= 0; ++di)
                    {
                        const int qi = ci + di;
                        const int qj = cj + dj;
                        if (qi >= 0 && qi < coarsePerSide - 1 && qj >= 0 && qj < coarsePerSide - 1)
                            minH = std::min(minH, quadMins[qj * (coarsePerSide - 1) + qi]);
                    }

                const int srcI = std::min(ci * step, fullPerSide - 1);
                const int srcJ = std::min(cj * step, fullPerSide - 1);
                osg::Vec3f pos = (*mScratchPositions)[srcJ * fullPerSide + srcI];
                pos.z() = minH;
                pos += worldOffset;
                result.mPositions.push_back(pos);
                result.mBounds.expandBy(pos);
            }
        }

        result.mIndices.reserve(static_cast<std::size_t>((coarsePerSide - 1) * (coarsePerSide - 1) * 6));
        for (int row = 0; row < coarsePerSide - 1; ++row)
        {
            for (int col = 0; col < coarsePerSide - 1; ++col)
            {
                const unsigned int tl = static_cast<unsigned int>(row * coarsePerSide + col);
                const unsigned int tr = tl + 1;
                const unsigned int bl = tl + static_cast<unsigned int>(coarsePerSide);
                const unsigned int br = bl + 1;
                result.mIndices.push_back(tl);
                result.mIndices.push_back(bl);
                result.mIndices.push_back(tr);
                result.mIndices.push_back(tr);
                result.mIndices.push_back(bl);
                result.mIndices.push_back(br);
            }
        }

        return result;
    }

    const TerrainOccluder::CachedCellMesh* TerrainOccluder::getCell(int cellX, int cellY, int& budget)
    {
        const CellKey key(cellX, cellY);
        const std::map<CellKey, CachedCellMesh>::iterator found = mCellCache.find(key);
        if (found != mCellCache.end())
            return &found->second;

        if (budget <= 0)
            return nullptr;

        --budget;
        ++mLastBuiltCells;
        return &mCellCache.emplace(key, buildCell(cellX, cellY)).first->second;
    }

    void TerrainOccluder::pruneCellCache(const osg::Vec2i& center, int radiusCells)
    {
        const int keepRadius = std::max(1, radiusCells) + 2;
        for (std::map<CellKey, CachedCellMesh>::iterator it = mCellCache.begin(); it != mCellCache.end();)
        {
            const int dx = std::abs(it->first.first - center.x());
            const int dy = std::abs(it->first.second - center.y());
            if (dx > keepRadius || dy > keepRadius)
                it = mCellCache.erase(it);
            else
                ++it;
        }
    }

    void TerrainOccluder::collectVisibleCells(const osg::Vec3f& eyePoint, int radiusCells,
        const osg::Polytope& frustum, std::vector<OccluderCellMesh>& out)
    {
        mLastBuiltCells = 0;
        mLastVisibleCells = 0;
        out.clear();

        if (!hasTerrainData())
        {
            mRegionCacheValid = false;
            publishStats();
            return;
        }

        radiusCells = std::max(1, radiusCells);
        const int cellX = static_cast<int>(std::floor(eyePoint.x() / mCellWorldSize));
        const int cellY = static_cast<int>(std::floor(eyePoint.y() / mCellWorldSize));
        const osg::Vec2i cellPos(cellX, cellY);

        // Keep the old X029 budget semantics: cached cells are free, newly decoded
        // cells consume the budget, and an incomplete region simply contributes
        // fewer occluders until later frames finish it.
        int budget = mCellBuildBudget > 0 ? mCellBuildBudget : std::numeric_limits<int>::max();
        bool complete = true;

        // Reuse enough storage for the full square while keeping each entry tiny
        // (two pointers). No vertex/index copying or index rebasing happens here.
        const int side = radiusCells * 2 + 1;
        out.reserve(static_cast<std::size_t>(side * side));

        for (int cy = cellY - radiusCells; cy <= cellY + radiusCells; ++cy)
        {
            for (int cx = cellX - radiusCells; cx <= cellX + radiusCells; ++cx)
            {
                const CachedCellMesh* cell = getCell(cx, cy, budget);
                if (!cell)
                {
                    complete = false;
                    continue;
                }

                if (cell->mPositions.empty() || cell->mIndices.empty())
                    continue;

                if (mFrustumCulling && cell->mBounds.valid())
                {
                    bool outside = false;
                    const osg::Polytope::PlaneList& planes = frustum.getPlaneList();
                    for (osg::Polytope::PlaneList::const_iterator plane = planes.begin(); plane != planes.end(); ++plane)
                    {
                        if (plane->intersect(cell->mBounds) < 0)
                        {
                            outside = true;
                            break;
                        }
                    }
                    if (outside)
                        continue;
                }

                OccluderCellMesh mesh;
                mesh.mPositions = &cell->mPositions;
                mesh.mIndices = &cell->mIndices;
                out.push_back(mesh);
                ++mLastVisibleCells;
            }
        }

        mCachedCellPos = cellPos;
        mCachedRadius = radiusCells;
        mRegionCacheValid = complete;

        // Safe even while the region is incomplete: current cells lie within
        // radius, while prune keeps radius+2 rings.
        pruneCellCache(cellPos, radiusCells);
        publishStats();
    }
}
