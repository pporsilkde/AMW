#include "occlusionculling.hpp"

#include <algorithm>
#include <cmath>
#include <typeinfo>

#include <osg/BoundingSphere>
#include <osg/ComputeBoundsVisitor>
#include <osg/Drawable>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Matrix>
#include <osg/NodeVisitor>
#include <osg/Transform>
#include <osg/UserDataContainer>
#include <osgUtil/CullVisitor>

#include <components/esm/loadstat.hpp>
#include <components/sceneutil/occlusionculling.hpp>
#include <components/terrain/terrainoccluder.hpp>

#include "../mwworld/class.hpp"

#include "objects.hpp"

namespace
{
    class CollectMeshVisitor : public osg::NodeVisitor
    {
    public:
        CollectMeshVisitor() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}

        void apply(osg::Transform& transform) override
        {
            osg::Matrix matrix;
            if (!mMatrixStack.empty())
                matrix = mMatrixStack.back();
            transform.computeLocalToWorldMatrix(matrix, this);
            mMatrixStack.push_back(matrix);
            traverse(transform);
            mMatrixStack.pop_back();
        }

        void apply(osg::Drawable& drawable) override
        {
            osg::Geometry* geom = drawable.asGeometry();
            if (!geom)
                return;
            const osg::Vec3Array* verts = dynamic_cast<const osg::Vec3Array*>(geom->getVertexArray());
            if (!verts || verts->empty())
                return;

            osg::Matrix matrix;
            if (!mMatrixStack.empty())
                matrix = mMatrixStack.back();
            const unsigned int base = static_cast<unsigned int>(mVertices.size());
            for (osg::Vec3Array::const_iterator it = verts->begin(); it != verts->end(); ++it)
                mVertices.push_back((*it) * matrix);
            for (unsigned int p = 0; p < geom->getNumPrimitiveSets(); ++p)
                collectTriangles(geom->getPrimitiveSet(p), base);
        }

        std::vector<osg::Vec3f> mVertices;
        std::vector<unsigned int> mIndices;

    private:
        void collectTriangles(const osg::PrimitiveSet* pset, unsigned int base)
        {
            const unsigned int count = pset->getNumIndices();
            switch (pset->getMode())
            {
                case GL_TRIANGLES:
                    for (unsigned int i = 0; i + 2 < count; i += 3)
                    { mIndices.push_back(base + pset->index(i)); mIndices.push_back(base + pset->index(i+1)); mIndices.push_back(base + pset->index(i+2)); }
                    break;
                case GL_TRIANGLE_STRIP:
                    for (unsigned int i = 0; i + 2 < count; ++i)
                        if ((i & 1u) == 0)
                        { mIndices.push_back(base + pset->index(i)); mIndices.push_back(base + pset->index(i+1)); mIndices.push_back(base + pset->index(i+2)); }
                        else
                        { mIndices.push_back(base + pset->index(i+1)); mIndices.push_back(base + pset->index(i)); mIndices.push_back(base + pset->index(i+2)); }
                    break;
                case GL_TRIANGLE_FAN:
                    for (unsigned int i = 1; i + 1 < count; ++i)
                    { mIndices.push_back(base + pset->index(0)); mIndices.push_back(base + pset->index(i)); mIndices.push_back(base + pset->index(i+1)); }
                    break;
                default:
                    break;
            }
        }
        std::vector<osg::Matrix> mMatrixStack;
    };

    static osgUtil::CullVisitor* asCull(osg::NodeVisitor* nv)
    {
        return dynamic_cast<osgUtil::CullVisitor*>(nv);
    }

    static bool shouldUseOcclusionForCamera(osg::Camera* cam)
    {
        if (!cam)
            return false;

        const std::string name = cam->getName();
        if (name == "ShadowCamera" || name == "ReflectionCamera" || name == "RefractionCamera")
            return false;

        // PRE_RENDER cameras are commonly used for RTT passes such as water and shadows.
        // Keep occlusion culling restricted to the main scene camera.
        if (cam->getRenderOrder() != osg::Camera::NESTED_RENDER)
            return false;

        return true;
    }
}

namespace MWRender
{
    // X029: the simplification itself, split out so both the per-instance and the
    // shared model-space builders can use it. boundsFallback may be null, which is
    // what the model-space builder wants: a degenerate model must not report a
    // bounding box measured in the wrong space.
    static OccluderMesh simplifyCollected(const CollectMeshVisitor& v, osg::Node* boundsFallback,
        int gridRes, float shrinkFactor)
    {
        OccluderMesh mesh;
        if (v.mIndices.empty() || v.mVertices.size() < 3)
        {
            if (boundsFallback)
            {
                osg::ComputeBoundsVisitor cbv;
                boundsFallback->accept(cbv);
                mesh.aabb = cbv.getBoundingBox();
            }
            return mesh;
        }

        for (std::vector<osg::Vec3f>::const_iterator it = v.mVertices.begin(); it != v.mVertices.end(); ++it)
            mesh.aabb.expandBy(*it);

        const unsigned int res = static_cast<unsigned int>(std::max(1, gridRes));
        const float dx = mesh.aabb.xMax() - mesh.aabb.xMin();
        const float dy = mesh.aabb.yMax() - mesh.aabb.yMin();
        const float dz = mesh.aabb.zMax() - mesh.aabb.zMin();
        const float maxDim = std::max(dx, std::max(dy, dz));
        const float cellSize = maxDim / res;
        if (cellSize > 0.f)
        {
            const unsigned int resX = std::max(1u, static_cast<unsigned int>(std::ceil(dx / cellSize)));
            const unsigned int resY = std::max(1u, static_cast<unsigned int>(std::ceil(dy / cellSize)));
            struct CellData { osg::Vec3f sum; unsigned int count = 0; unsigned int newIndex = 0; };
            std::unordered_map<unsigned int, CellData> cells;
            std::vector<unsigned int> remap(v.mVertices.size());
            for (std::size_t i = 0; i < v.mVertices.size(); ++i)
            {
                const osg::Vec3f& p = v.mVertices[i];
                const unsigned int gx = std::min(static_cast<unsigned int>(std::max((p.x()-mesh.aabb.xMin())/cellSize, 0.f)), resX-1);
                const unsigned int gy = std::min(static_cast<unsigned int>(std::max((p.y()-mesh.aabb.yMin())/cellSize, 0.f)), resY-1);
                const unsigned int gz = std::min(static_cast<unsigned int>(std::max((p.z()-mesh.aabb.zMin())/cellSize, 0.f)), res-1);
                const unsigned int cellId = gx + gy * resX + gz * resX * resY;
                CellData& cell = cells[cellId];
                cell.sum += p;
                cell.count++;
                remap[i] = cellId;
            }
            unsigned int next = 0;
            for (std::unordered_map<unsigned int, CellData>::iterator it = cells.begin(); it != cells.end(); ++it)
            {
                it->second.newIndex = next++;
                mesh.vertices.push_back(it->second.sum / static_cast<float>(it->second.count));
            }
            for (std::size_t i = 0; i + 2 < v.mIndices.size(); i += 3)
            {
                const unsigned int a = cells[remap[v.mIndices[i]]].newIndex;
                const unsigned int b = cells[remap[v.mIndices[i+1]]].newIndex;
                const unsigned int c = cells[remap[v.mIndices[i+2]]].newIndex;
                if (a != b && b != c && a != c)
                { mesh.indices.push_back(a); mesh.indices.push_back(b); mesh.indices.push_back(c); }
            }
        }
        if (!mesh.vertices.empty())
        {
            osg::Vec3f center(0,0,0);
            for (std::vector<osg::Vec3f>::const_iterator it = mesh.vertices.begin(); it != mesh.vertices.end(); ++it)
                center += *it;
            center /= static_cast<float>(mesh.vertices.size());
            for (std::vector<osg::Vec3f>::iterator it = mesh.vertices.begin(); it != mesh.vertices.end(); ++it)
                *it = center + (*it - center) * shrinkFactor;
        }
        return mesh;
    }

    OccluderMesh buildSimplifiedMesh(osg::Node* node, int gridRes, float shrinkFactor)
    {
        CollectMeshVisitor v;
        node->accept(v);
        return simplifyCollected(v, node, gridRes, shrinkFactor);
    }

    OccluderMesh buildSimplifiedMeshLocal(osg::Node* node, int gridRes, float shrinkFactor)
    {
        CollectMeshVisitor v;
        if (osg::Group* group = node->asGroup())
        {
            // Skip the node itself so its own placement transform stays out of the
            // hull. Everything below it is the model.
            const unsigned int count = group->getNumChildren();
            for (unsigned int i = 0; i < count; ++i)
                group->getChild(i)->accept(v);
        }
        else
        {
            node->accept(v);
        }
        return simplifyCollected(v, nullptr, gridRes, shrinkFactor);
    }

    bool OccluderTemplateCache::getOrCreate(const std::string& key, osg::Node* node,
        int gridRes, float shrinkFactor, OccluderMesh& out)
    {
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            const std::map<std::string, OccluderMesh>::const_iterator found = mTemplates.find(key);
            if (found != mTemplates.end())
            {
                mHits.fetch_add(1, std::memory_order_relaxed);
                if (found->second.indices.empty())
                    return false;
                out = found->second;
                return true;
            }
        }

        // Built outside the lock: this is the expensive subtree walk, and holding
        // the mutex across it would serialise unrelated first-sightings.
        OccluderMesh built = buildSimplifiedMeshLocal(node, gridRes, shrinkFactor);

        const std::lock_guard<std::mutex> lock(mMutex);
        if (mTemplates.size() >= sMaxEntries)
            mTemplates.clear();

        mMisses.fetch_add(1, std::memory_order_relaxed);
        // A racing thread may have inserted the same key meanwhile; emplace keeps
        // whichever landed first, and both are equivalent by construction.
        const OccluderMesh& stored = mTemplates.emplace(key, std::move(built)).first->second;
        if (stored.indices.empty())
            return false;

        out = stored;
        return true;
    }

    unsigned int OccluderTemplateCache::getSize() const
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<unsigned int>(mTemplates.size());
    }

    SceneOcclusionCallback::SceneOcclusionCallback(SceneUtil::OcclusionCuller* culler, Terrain::TerrainOccluder* occluder,
        int radiusCells, bool enableTerrainOccluder)
        : mCuller(culler), mTerrainOccluder(occluder), mRadiusCells(radiusCells), mEnableTerrainOccluder(enableTerrainOccluder), mLastFrameNumber(~0u)
    {
    }

    void SceneOcclusionCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        osgUtil::CullVisitor* cv = asCull(nv);
        if (!cv || !mCuller.valid() || !mTerrainOccluder)
        { traverse(node, nv); return; }
        osg::Camera* cam = cv->getCurrentCamera();
        if (!shouldUseOcclusionForCamera(cam))
        { traverse(node, nv); return; }
        const osg::FrameStamp* fs = cv->getFrameStamp();
        const unsigned int frame = fs ? fs->getFrameNumber() : 0u;
        if (frame == mLastFrameNumber)
        { traverse(node, nv); return; }
        mLastFrameNumber = frame;

        mCuller->beginFrame(cam->getViewMatrix(), cam->getProjectionMatrix());
        if (mEnableTerrainOccluder && mTerrainOccluder->hasTerrainData())
        {
            // X031: side planes only. The desired transform is inverse(view*proj),
            // so transformProvidingInverse receives view*proj directly. Near/far
            // are intentionally omitted: this rejects cells behind/off-axis while
            // remaining independent of normal/reversed depth conventions.
            mFrustum.setToUnitFrustum(false, false);
            mFrustum.transformProvidingInverse(cam->getViewMatrix() * cam->getProjectionMatrix());

            mTerrainOccluder->collectVisibleCells(cv->getEyePoint(), mRadiusCells, mFrustum, mVisibleCells);
            for (std::vector<Terrain::OccluderCellMesh>::const_iterator it = mVisibleCells.begin();
                 it != mVisibleCells.end(); ++it)
            {
                if (it->mPositions && it->mIndices && !it->mPositions->empty() && !it->mIndices->empty())
                    mCuller->rasterizeOccluder(*it->mPositions, *it->mIndices);
            }
        }
        traverse(node, nv);
    }

    PagedOccluderCallback::PagedOccluderCallback(SceneUtil::OcclusionCuller* culler, float maxDistance)
        : mCuller(culler), mMaxDistanceSq(maxDistance * maxDistance)
    {
    }

    void PagedOccluderCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        osgUtil::CullVisitor* cv = asCull(nv);
        if (!cv || !mCuller.valid() || !mCuller->isFrameActive() || !shouldUseOcclusionForCamera(cv->getCurrentCamera()))
        { traverse(node, nv); return; }
        const osg::BoundingSphere& bs = node->getBound();
        if (bs.valid())
        {
            const osg::Matrixd& inv = mCuller->getInverseViewMatrix();
            const osg::Matrixd modelToWorld = (*cv->getModelViewMatrix()) * inv;
            const osg::Vec3f worldCenter = bs.center() * modelToWorld;
            const float r = bs.radius();
            osg::BoundingBox bb(worldCenter.x()-r, worldCenter.y()-r, worldCenter.z()-r, worldCenter.x()+r, worldCenter.y()+r, worldCenter.z()+r);
            if (!mCuller->testVisibleAABB(bb))
                return;
            const osg::Vec3f& eyeWorld = mCuller->getEyeWorld();
            osg::UserDataContainer* udc = node->getUserDataContainer();
            if (udc)
                for (unsigned int i = 0; i < udc->getNumUserObjects(); ++i)
                    if (PagedOccluderData* pod = dynamic_cast<PagedOccluderData*>(udc->getUserObject(i)))
                    {
                        for (std::vector<OccluderMesh>::const_iterator it = pod->mOccluderMeshes.begin(); it != pod->mOccluderMeshes.end(); ++it)
                        {
                            if (it->indices.empty())
                                continue;
                            if ((it->aabb.center() - eyeWorld).length2() > mMaxDistanceSq)
                                continue;
                            mCuller->rasterizeOccluder(it->vertices, it->indices);
                            mCuller->incrementBuildingOccluders(static_cast<unsigned int>(it->indices.size()/3), static_cast<unsigned int>(it->vertices.size()));
                        }
                        break;
                    }
        }
        traverse(node, nv);
    }

    CellOcclusionCallback::CellOcclusionCallback(SceneUtil::OcclusionCuller* culler, float occluderMinRadius,
        float occluderMaxRadius, float occluderShrinkFactor, int occluderMeshResolution,
        int occluderMaxMeshResolution, float occluderInsideThreshold,
        float occluderMaxDistance, bool enableStaticOccluders, OccluderTemplateCache* templateCache)
        : mCuller(culler), mOccluderMinRadius(occluderMinRadius), mOccluderMaxRadius(occluderMaxRadius),
          mOccluderShrinkFactor(occluderShrinkFactor), mOccluderMeshResolution(occluderMeshResolution),
          mOccluderMaxMeshResolution(occluderMaxMeshResolution), mOccluderInsideThreshold(occluderInsideThreshold),
          mOccluderMaxDistanceSq(occluderMaxDistance * occluderMaxDistance), mEnableStaticOccluders(enableStaticOccluders),
          mTemplateCache(templateCache)
    {
    }

    std::string CellOcclusionCallback::templateKey(osg::Node* node, int gridRes) const
    {
        std::string key;
        osg::UserDataContainer* udc = node->getUserDataContainer();
        if (!udc)
            return key;

        for (unsigned int i = 0; i < udc->getNumUserObjects(); ++i)
        {
            PtrHolder* holder = dynamic_cast<PtrHolder*>(udc->getUserObject(i));
            if (!holder || holder->mPtr.isEmpty())
                continue;

            // X029-safe: share templates only for true ESM::Static records. Doors,
            // activators, containers and other non-actor objects may contain
            // per-instance animated child transforms; sharing a hull captured from
            // another animation state could become an oversized occluder.
            if (holder->mPtr.getTypeName() != typeid(ESM::Static).name())
                return key;

            const std::string model = holder->mPtr.getClass().getModel(holder->mPtr);
            if (model.empty())
                return key;

            key = model;
            key += '|';
            key += std::to_string(gridRes);
            key += '|';
            key += std::to_string(mOccluderShrinkFactor);
            return key;
        }

        return key;
    }

    const OccluderMesh& CellOcclusionCallback::getOccluderMesh(osg::Node* node)
    {
        std::unordered_map<osg::Node*, InstanceEntry>::iterator it = mMeshCache.find(node);
        if (it != mMeshCache.end())
        {
            if (it->second.mNode.get() == node)
                return it->second.mMesh;

            // The node this entry was built for is gone and the address has been
            // handed out again. Drop it rather than occlude with a stale hull.
            mMeshCache.erase(it);
        }

        int meshRes = mOccluderMeshResolution;
        const float radius = node->getBound().radius();
        if (radius > mOccluderMinRadius && mOccluderMinRadius > 0.f)
        {
            const float scale = radius / mOccluderMinRadius;
            meshRes = std::max(mOccluderMeshResolution, std::min(mOccluderMaxMeshResolution, static_cast<int>(mOccluderMeshResolution * scale)));
        }

        InstanceEntry entry;
        entry.mNode = node;

        bool fromTemplate = false;
        if (mTemplateCache.valid())
        {
            const std::string key = templateKey(node, meshRes);
            if (!key.empty())
            {
                OccluderMesh tmpl;
                if (mTemplateCache->getOrCreate(key, node, meshRes, mOccluderShrinkFactor, tmpl))
                {
                    // Placing the shared hull is a plain affine transform of its
                    // vertices. Shrinking about the centroid commutes with an affine
                    // map, so this is identical to shrinking after placement; only
                    // the clustering grid differs, and clustering can only pull
                    // vertices inside the original hull, never outside it.
                    osg::Matrix localToWorld;
                    localToWorld.makeIdentity();
                    if (osg::Transform* transform = node->asTransform())
                        transform->computeLocalToWorldMatrix(localToWorld, nullptr);

                    entry.mMesh.indices = std::move(tmpl.indices);
                    entry.mMesh.vertices.reserve(tmpl.vertices.size());
                    for (std::vector<osg::Vec3f>::const_iterator vit = tmpl.vertices.begin(); vit != tmpl.vertices.end(); ++vit)
                        entry.mMesh.vertices.push_back((*vit) * localToWorld);

                    // X029-safe: the old per-instance path tests the unsqueezed
                    // geometry bounds, then uses the shrunken mesh only as an
                    // occluder. Preserve that conservative rule here. Building the
                    // AABB from the already-shrunken template could let the object
                    // itself be rejected while visible geometry lies outside it.
                    if (tmpl.aabb.valid())
                    {
                        for (unsigned int corner = 0; corner < 8; ++corner)
                        {
                            const osg::Vec3f local(
                                (corner & 1u) ? tmpl.aabb.xMax() : tmpl.aabb.xMin(),
                                (corner & 2u) ? tmpl.aabb.yMax() : tmpl.aabb.yMin(),
                                (corner & 4u) ? tmpl.aabb.zMax() : tmpl.aabb.zMin());
                            entry.mMesh.aabb.expandBy(local * localToWorld);
                        }
                    }
                    fromTemplate = true;
                }
            }
        }

        if (!fromTemplate)
            entry.mMesh = buildSimplifiedMesh(node, meshRes, mOccluderShrinkFactor);

        return mMeshCache.emplace(node, std::move(entry)).first->second.mMesh;
    }

    void CellOcclusionCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        osgUtil::CullVisitor* cv = asCull(nv);
        osg::Group* group = node ? node->asGroup() : nullptr;
        if (!cv || !group || !mCuller.valid() || !mCuller->isFrameActive() || !shouldUseOcclusionForCamera(cv->getCurrentCamera()))
        { traverse(node, nv); return; }

        const osg::BoundingSphere& cellBS = group->getBound();
        if (cellBS.valid())
        {
            osg::BoundingBox cellBB; cellBB.expandBy(cellBS);
            if (!mCuller->testVisibleAABB(cellBB))
                return;
        }

        const unsigned int count = group->getNumChildren();
        for (unsigned int i = 0; i < count; ++i)
        {
            osg::Node* child = group->getChild(i);
            const osg::BoundingSphere& bs = child->getBound();
            if (!bs.valid() || bs.radius() < mOccluderMinRadius)
                continue;
            if (bs.radius() > mOccluderMaxRadius)
            {
                osg::BoundingBox bb; bb.expandBy(bs);
                if (mCuller->testVisibleAABB(bb)) child->accept(*cv);
                continue;
            }
            const OccluderMesh& mesh = getOccluderMesh(child);
            if (!mesh.aabb.valid())
                continue;
            if (mCuller->testVisibleAABB(mesh.aabb))
            {
                if (mEnableStaticOccluders && !mesh.indices.empty() && (bs.center() - cv->getEyePoint()).length2() < mOccluderMaxDistanceSq)
                {
                    const osg::Vec3f center = mesh.aabb.center();
                    const osg::Vec3f halfExtent = (osg::Vec3f(mesh.aabb.xMax(), mesh.aabb.yMax(), mesh.aabb.zMax()) - center) * mOccluderInsideThreshold;
                    osg::BoundingBox scaledBB; scaledBB.expandBy(center - halfExtent); scaledBB.expandBy(center + halfExtent);
                    if (!scaledBB.contains(cv->getEyePoint()))
                    {
                        mCuller->rasterizeOccluder(mesh.vertices, mesh.indices);
                        mCuller->incrementBuildingOccluders(static_cast<unsigned int>(mesh.indices.size()/3), static_cast<unsigned int>(mesh.vertices.size()));
                    }
                }
                child->accept(*cv);
            }
        }

        for (unsigned int i = 0; i < count; ++i)
        {
            osg::Node* child = group->getChild(i);
            const osg::BoundingSphere& bs = child->getBound();
            if (!bs.valid())
            { child->accept(*cv); continue; }
            if (bs.radius() >= mOccluderMinRadius)
                continue;
            // X031: most children are visible, so avoid the named user-data
            // lookup on that hot path. Only a child that software occlusion would
            // reject needs the skipOcclusion escape-hatch lookup (doors, etc.).
            osg::BoundingBox bb; bb.expandBy(bs);
            if (mCuller->testVisibleAABB(bb))
            {
                child->accept(*cv);
                continue;
            }

            bool skipOcclusion = false;
            child->getUserValue("skipOcclusion", skipOcclusion);
            if (skipOcclusion)
                child->accept(*cv);
        }
    }
}
