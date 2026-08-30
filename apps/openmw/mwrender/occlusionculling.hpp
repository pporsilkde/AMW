#ifndef OPENMW_MWRENDER_OCCLUSIONCULLING_H
#define OPENMW_MWRENDER_OCCLUSIONCULLING_H

#include <osg/BoundingBox>
#include <osg/Object>
#include <osg/NodeCallback>
#include <osg/Polytope>
#include <osg/Referenced>
#include <osg/Vec3f>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

#include <atomic>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <components/terrain/terrainoccluder.hpp>

namespace osg
{
    class Group;
    class Node;
    class NodeVisitor;
}

namespace SceneUtil
{
    class OcclusionCuller;
}


namespace MWRender
{
    struct OccluderMesh
    {
        osg::BoundingBox aabb;
        std::vector<osg::Vec3f> vertices;
        std::vector<unsigned int> indices;
    };

    /// Collapses a node's geometry into a coarse, shrunken occluder hull.
    /// Vertices are produced in the node's own parent space, i.e. the node's own
    /// transform IS applied.
    OccluderMesh buildSimplifiedMesh(osg::Node* node, int gridRes, float shrinkFactor);

    /// X029: same simplification, but the node's own transform is NOT applied, so
    /// the result is a model-space hull that every instance of the same model can
    /// share. Returns a mesh with no indices when the node has no usable geometry.
    OccluderMesh buildSimplifiedMeshLocal(osg::Node* node, int gridRes, float shrinkFactor);

    /// X029: shared model-space occluder hulls.
    ///
    /// Before this, every large static built its own hull from a full subtree walk
    /// the first time it was culled, in a per-cell cache. Twenty houses of the same
    /// model meant twenty walks, and all of them again on the next cell reload.
    /// Here the expensive part happens once per (model, grid resolution) and each
    /// instance only pays a vertex transform.
    ///
    /// Shared between cells, so it is accessed from the cull traversal; the mutex
    /// guards the map and the counters are atomic for the stats reader.
    class OccluderTemplateCache : public osg::Referenced
    {
    public:
        /// Copies the shared model-space hull for \a key into \a out, building it
        /// from \a node on a miss. Returns false when the model yields no usable
        /// geometry, in which case the caller falls back to the per-instance path.
        /// A copy rather than a pointer: nothing then depends on how long the entry
        /// stays in the map.
        bool getOrCreate(const std::string& key, osg::Node* node, int gridRes, float shrinkFactor,
            OccluderMesh& out);

        unsigned int getSize() const;
        unsigned int getHits() const { return mHits.load(std::memory_order_relaxed); }
        unsigned int getMisses() const { return mMisses.load(std::memory_order_relaxed); }

    protected:
        ~OccluderTemplateCache() override = default;

    private:
        // Bounded so a pathological load order cannot grow it without limit. The
        // ceiling is far above any realistic number of distinct large statics.
        static constexpr std::size_t sMaxEntries = 4096;

        mutable std::mutex mMutex;
        std::map<std::string, OccluderMesh> mTemplates;
        std::atomic<unsigned int> mHits{0};
        std::atomic<unsigned int> mMisses{0};
    };

    class PagedOccluderData : public osg::Object
    {
    public:
        PagedOccluderData() = default;
        PagedOccluderData(const PagedOccluderData& copy, const osg::CopyOp& copyop = osg::CopyOp())
            : osg::Object(copy, copyop), mOccluderMeshes(copy.mOccluderMeshes) {}
        META_Object(MWRender, PagedOccluderData)
        std::vector<OccluderMesh> mOccluderMeshes;
    };

    class SceneOcclusionCallback : public osg::NodeCallback
    {
    public:
        SceneOcclusionCallback(SceneUtil::OcclusionCuller* culler, Terrain::TerrainOccluder* occluder,
            int radiusCells, bool enableTerrainOccluder);
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);
    private:
        osg::ref_ptr<SceneUtil::OcclusionCuller> mCuller;
        Terrain::TerrainOccluder* mTerrainOccluder;
        int mRadiusCells;
        bool mEnableTerrainOccluder;
        unsigned int mLastFrameNumber;
        // X031: rasterize cached cells directly. The frustum and the tiny list of
        // cell references are reused every cull frame.
        osg::Polytope mFrustum;
        std::vector<Terrain::OccluderCellMesh> mVisibleCells;
    };

    class PagedOccluderCallback : public osg::NodeCallback
    {
    public:
        PagedOccluderCallback(SceneUtil::OcclusionCuller* culler, float maxDistance);
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);
    private:
        osg::ref_ptr<SceneUtil::OcclusionCuller> mCuller;
        float mMaxDistanceSq;
    };

    class CellOcclusionCallback : public osg::NodeCallback
    {
    public:
        CellOcclusionCallback(SceneUtil::OcclusionCuller* culler, float occluderMinRadius,
            float occluderMaxRadius, float occluderShrinkFactor, int occluderMeshResolution,
            int occluderMaxMeshResolution, float occluderInsideThreshold,
            float occluderMaxDistance, bool enableStaticOccluders,
            OccluderTemplateCache* templateCache = nullptr);
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);
    private:
        // X029: the per-instance entry keeps a weak reference to the node it was
        // built for. The map is keyed by raw pointer, and a node removed from the
        // cell can be replaced by a new one at the same address; without this check
        // that new object would silently inherit the old world-space hull.
        struct InstanceEntry
        {
            osg::observer_ptr<osg::Node> mNode;
            OccluderMesh mMesh;
        };

        const OccluderMesh& getOccluderMesh(osg::Node* node);
        std::string templateKey(osg::Node* node, int gridRes) const;
        osg::ref_ptr<SceneUtil::OcclusionCuller> mCuller;
        float mOccluderMinRadius;
        float mOccluderMaxRadius;
        float mOccluderShrinkFactor;
        int mOccluderMeshResolution;
        int mOccluderMaxMeshResolution;
        float mOccluderInsideThreshold;
        float mOccluderMaxDistanceSq;
        bool mEnableStaticOccluders;
        osg::ref_ptr<OccluderTemplateCache> mTemplateCache;
        std::unordered_map<osg::Node*, InstanceEntry> mMeshCache;
    };
}

#endif
