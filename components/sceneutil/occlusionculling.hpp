#ifndef OPENMW_COMPONENTS_SCENEUTIL_OCCLUSIONCULLING_H
#define OPENMW_COMPONENTS_SCENEUTIL_OCCLUSIONCULLING_H

#include <osg/BoundingBox>
#include <osg/Matrixd>
#include <osg/Referenced>
#include <osg/Vec3f>

#include <atomic>
#include <vector>

class MaskedOcclusionCulling;

namespace SceneUtil
{
    /// Wraps Intel's Masked Software Occlusion Culling library.
    /// Provides a CPU-based hierarchical depth buffer for occlusion testing during cull traversal.
    class OcclusionCuller : public osg::Referenced
    {
    public:
        OcclusionCuller(unsigned int bufferWidth, unsigned int bufferHeight);
        ~OcclusionCuller();

        /// Call at the start of each frame's cull traversal.
        /// Clears the depth buffer and stores the view-projection matrix.
        void beginFrame(const osg::Matrixd& viewMatrix, const osg::Matrixd& projectionMatrix);

        /// Rasterize world-space triangles as occluders into the depth buffer.
        void rasterizeOccluder(const std::vector<osg::Vec3f>& worldPositions, const std::vector<unsigned int>& indices);

        /// Rasterize a world-space AABB as an occluder (12 triangles for 6 faces).
        void rasterizeAABBOccluder(const osg::BoundingBox& worldBB);

        /// Test if a world-space AABB is visible (not fully occluded).
        /// Returns true if the box may be visible, false if definitely occluded.
        bool testVisibleAABB(const osg::BoundingBox& worldBB) const;

        bool isActive() const { return mMOC != nullptr; }
        bool isFrameActive() const { return mFrameActive; }

        /// Cached once per main-camera cull frame to avoid repeated matrix inversion in node callbacks.
        const osg::Matrixd& getInverseViewMatrix() const { return mInverseView; }
        const osg::Vec3f& getEyeWorld() const { return mEyeWorld; }

        // X029-safe: stats are published once per cull frame. This avoids data
        // races with the update-thread stats reader without adding atomics to the
        // per-AABB hot path. Values intentionally describe the previous completed
        // cull frame.
        unsigned int getNumOccluded() const { return mPublishedNumOccluded.load(std::memory_order_relaxed); }
        unsigned int getNumTested() const { return mPublishedNumTested.load(std::memory_order_relaxed); }
        unsigned int getNumBuildingOccluders() const { return mPublishedNumBuildingOccluders.load(std::memory_order_relaxed); }
        unsigned int getNumBuildingTris() const { return mPublishedNumBuildingTris.load(std::memory_order_relaxed); }
        unsigned int getNumBuildingVerts() const { return mPublishedNumBuildingVerts.load(std::memory_order_relaxed); }
        void incrementBuildingOccluders(unsigned int tris, unsigned int verts)
        {
            ++mNumBuildingOccluders;
            mNumBuildingTris += tris;
            mNumBuildingVerts += verts;
        }

        /// Write the per-pixel depth buffer to depthData (width*height floats, bottom-to-top).
        void computePixelDepthBuffer(float* depthData) const;

        void getResolution(unsigned int& width, unsigned int& height) const;

    private:
        MaskedOcclusionCulling* mMOC;
        osg::Matrixd mViewProjection;
        osg::Matrixd mInverseView;
        osg::Vec3f mEyeWorld;
        float mVPFloat[16] = {};
        bool mFrameActive = false;

        mutable unsigned int mNumOccluded = 0;
        mutable unsigned int mNumTested = 0;
        unsigned int mNumBuildingOccluders = 0;
        unsigned int mNumBuildingTris = 0;
        unsigned int mNumBuildingVerts = 0;

        std::atomic<unsigned int> mPublishedNumOccluded{0};
        std::atomic<unsigned int> mPublishedNumTested{0};
        std::atomic<unsigned int> mPublishedNumBuildingOccluders{0};
        std::atomic<unsigned int> mPublishedNumBuildingTris{0};
        std::atomic<unsigned int> mPublishedNumBuildingVerts{0};
    };
}

#endif
