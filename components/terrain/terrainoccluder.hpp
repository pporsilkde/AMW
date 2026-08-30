#ifndef OPENMW_COMPONENTS_TERRAIN_TERRAINOCCLUDER_H
#define OPENMW_COMPONENTS_TERRAIN_TERRAINOCCLUDER_H

#include <atomic>
#include <map>
#include <utility>
#include <vector>

#include <osg/Array>
#include <osg/BoundingBox>
#include <osg/Polytope>
#include <osg/Vec2i>
#include <osg/Vec3f>
#include <osg/ref_ptr>

namespace Terrain
{
    class Storage;

    /// X031: one cached LAND cell's coarse occluder mesh, handed out by reference.
    /// References stay valid until the next collectVisibleCells() on the same
    /// occluder; the caller consumes them immediately in the cull traversal.
    struct OccluderCellMesh
    {
        const std::vector<osg::Vec3f>* mPositions = nullptr;
        const std::vector<unsigned int>* mIndices = nullptr;
    };

    class TerrainOccluder
    {
    public:
        TerrainOccluder(Storage* storage, float cellWorldSize);

        void setLodLevel(int lod);
        void setCellBuildBudget(int cellsPerBuild);

        // X031: reject cached terrain occluder cells outside the camera side
        // planes before they reach the software rasterizer.
        void setFrustumCulling(bool enabled) { mFrustumCulling = enabled; }

        /// X031: return cached cells to rasterize this frame without assembling
        /// one giant rebased mesh. Missing cells are decoded within the X029
        /// budget. Skipped/missing cells only remove occluders, so the path is
        /// fail-open: the worst case is drawing more geometry.
        void collectVisibleCells(const osg::Vec3f& eyePoint, int radiusCells,
            const osg::Polytope& frustum, std::vector<OccluderCellMesh>& out);

        bool hasTerrainData() const;

        // Published snapshots are safe to read from the update/stats thread while
        // the cull traversal owns the mutable cache.
        unsigned int getCachedCellCount() const { return mPublishedCachedCellCount.load(std::memory_order_relaxed); }
        unsigned int getLastBuiltCellCount() const { return mPublishedLastBuiltCells.load(std::memory_order_relaxed); }
        unsigned int getLastVisibleCellCount() const { return mPublishedLastVisibleCells.load(std::memory_order_relaxed); }
        bool isRegionComplete() const { return mPublishedRegionComplete.load(std::memory_order_relaxed); }

    private:
        struct CachedCellMesh
        {
            std::vector<osg::Vec3f> mPositions;
            std::vector<unsigned int> mIndices;
            // Bounds of the actual coarse occluder mesh. Frustum rejection never
            // hides scene geometry; it only decides whether this occluder is worth
            // rasterizing for the current camera.
            osg::BoundingBox mBounds;
        };

        typedef std::pair<int, int> CellKey;

        CachedCellMesh buildCell(int cellX, int cellY) const;
        const CachedCellMesh* getCell(int cellX, int cellY, int& budget);
        void pruneCellCache(const osg::Vec2i& center, int radiusCells);
        void publishStats();
        void invalidateCache();

        Storage* mStorage;
        float mCellWorldSize;
        int mLodLevel = 3;

        bool mRegionCacheValid = false;
        osg::Vec2i mCachedCellPos;
        int mCachedRadius = -1;

        int mCellBuildBudget = 24;
        bool mFrustumCulling = true;
        unsigned int mLastBuiltCells = 0;
        unsigned int mLastVisibleCells = 0;

        std::atomic<unsigned int> mPublishedCachedCellCount{0};
        std::atomic<unsigned int> mPublishedLastBuiltCells{0};
        std::atomic<unsigned int> mPublishedLastVisibleCells{0};
        std::atomic<bool> mPublishedRegionComplete{false};

        // X031: reuse the largest temporary LAND position array instead of
        // allocating/freeing it once for every decoded cell.
        mutable osg::ref_ptr<osg::Vec3Array> mScratchPositions;

        std::map<CellKey, CachedCellMesh> mCellCache;
    };
}

#endif
