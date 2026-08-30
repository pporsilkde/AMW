// X029 logic harness. Reproduces the arithmetic of the patched code with the
// OSG types replaced by minimal stand-ins, so the parts that can be checked
// without a full engine build are actually checked.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------- stubs
struct Vec3
{
    float v[3];
    float& x() { return v[0]; } float& y() { return v[1]; } float& z() { return v[2]; }
    float x() const { return v[0]; } float y() const { return v[1]; } float z() const { return v[2]; }
    Vec3 operator+(const Vec3& o) const { return {{v[0]+o.v[0], v[1]+o.v[1], v[2]+o.v[2]}}; }
    Vec3 operator-(const Vec3& o) const { return {{v[0]-o.v[0], v[1]-o.v[1], v[2]-o.v[2]}}; }
    Vec3 operator*(float s) const { return {{v[0]*s, v[1]*s, v[2]*s}}; }
    Vec3& operator+=(const Vec3& o) { v[0]+=o.v[0]; v[1]+=o.v[1]; v[2]+=o.v[2]; return *this; }
    Vec3& operator/=(float s) { v[0]/=s; v[1]/=s; v[2]/=s; return *this; }
};

// Row-vector convention, same as osg: p * M.
struct Mat
{
    float m[4][4];
    static Mat identity() { Mat r{}; for (int i=0;i<4;++i) r.m[i][i]=1.f; return r; }
};

static Vec3 mul(const Vec3& p, const Mat& M)
{
    Vec3 r{};
    for (int c = 0; c < 3; ++c)
        r.v[c] = p.v[0]*M.m[0][c] + p.v[1]*M.m[1][c] + p.v[2]*M.m[2][c] + M.m[3][c];
    return r;
}

// ================================================================
// 1. Terrain occluder build budget
// ================================================================
struct OccluderSim
{
    std::map<std::pair<int,int>, bool> cache;   // value unused, presence is what matters
    bool regionValid = false;
    std::pair<int,int> cachedPos{0,0};
    int cachedRadius = -1;
    int budgetPerBuild = 24;

    unsigned long totalBuilt = 0;
    unsigned int lastBuilt = 0;

    void prune(std::pair<int,int> center, int radius)
    {
        const int keep = std::max(1, radius) + 2;
        for (auto it = cache.begin(); it != cache.end();)
        {
            if (std::abs(it->first.first - center.first) > keep || std::abs(it->first.second - center.second) > keep)
                it = cache.erase(it);
            else
                ++it;
        }
    }

    // Returns true when the region was (re)assembled this call.
    bool build(int cellX, int cellY, int radius)
    {
        const std::pair<int,int> pos(cellX, cellY);
        if (regionValid && pos == cachedPos && radius == cachedRadius)
            return false;

        int budget = budgetPerBuild > 0 ? budgetPerBuild : std::numeric_limits<int>::max();
        bool complete = true;
        lastBuilt = 0;

        for (int cy = cellY - radius; cy <= cellY + radius; ++cy)
            for (int cx = cellX - radius; cx <= cellX + radius; ++cx)
            {
                if (cache.count({cx,cy}))
                    continue;
                if (budget <= 0) { complete = false; continue; }
                --budget; ++lastBuilt; ++totalBuilt;
                cache[{cx,cy}] = true;
            }

        cachedPos = pos;
        cachedRadius = radius;
        regionValid = complete;
        if (complete)
            prune(pos, radius);
        return true;
    }
};

static void testBudget()
{
    const int radius = 8;
    const int region = (2*radius+1)*(2*radius+1);
    printf("region at radius %d = %d cells\n", radius, region);

    // cold start, stationary
    OccluderSim sim;
    int frames = 0;
    while (!sim.regionValid && frames < 1000) { sim.build(0, 0, radius); ++frames; }
    printf("test1 cold start: complete after %d frames, %lu cells built, max %u per frame\n",
        frames, sim.totalBuilt, sim.budgetPerBuild);
    printf("       expected frames = ceil(%d/%d) = %d  -> %s\n",
        region, sim.budgetPerBuild, (region + sim.budgetPerBuild - 1)/sim.budgetPerBuild,
        frames == (region + sim.budgetPerBuild - 1)/sim.budgetPerBuild ? "ok" : "MISMATCH");

    // one-cell step after warm-up: only the new edge
    unsigned long before = sim.totalBuilt;
    sim.build(1, 0, radius);
    printf("test2 one-cell step: built %u cells (edge is 2r+1 = %d) -> %s\n",
        sim.lastBuilt, 2*radius+1, sim.lastBuilt == static_cast<unsigned>(2*radius+1) ? "ok" : "MISMATCH");
    printf("       region complete immediately: %s\n", sim.regionValid ? "yes" : "no");
    (void)before;

    // unlimited budget reproduces X028 exactly
    OccluderSim old; old.budgetPerBuild = 0;
    old.build(0,0,radius);
    printf("test3 budget<=0 (X028 behaviour): %u cells in one call, complete=%s\n",
        old.lastBuilt, old.regionValid ? "yes" : "no");

    // walking a straight line, 40 cells, warm cache
    OccluderSim walk;
    while (!walk.regionValid) walk.build(0,0,radius);
    unsigned long start = walk.totalBuilt;
    unsigned int worst = 0;
    for (int i = 1; i <= 40; ++i)
    {
        walk.build(i, 0, radius);
        worst = std::max(worst, walk.lastBuilt);
    }
    printf("test4 walk 40 cells: %lu cells built total, worst frame %u, cache holds %zu cells\n",
        walk.totalBuilt - start, worst, walk.cache.size());
    printf("       cache bound at keepRadius=r+2 is (2*(r+2)+1)^2 = %d -> %s\n",
        (2*(radius+2)+1)*(2*(radius+2)+1),
        walk.cache.size() <= static_cast<size_t>((2*(radius+2)+1)*(2*(radius+2)+1)) ? "within bound" : "EXCEEDED");
}

// ================================================================
// 2. Shrink-about-centroid commutes with an affine transform
//    (this is what lets the hull be built once in model space)
// ================================================================
static void testCommutation()
{
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> d(-500.f, 500.f);
    std::uniform_real_distribution<float> ds(0.2f, 4.f);

    double worst = 0.0;
    for (int trial = 0; trial < 200; ++trial)
    {
        std::vector<Vec3> pts;
        for (int i = 0; i < 64; ++i) pts.push_back({{d(rng), d(rng), d(rng)}});

        // random affine: rotation about Z, non-uniform scale, translation
        const float a = d(rng) * 0.01f;
        const float sx = ds(rng), sy = ds(rng), sz = ds(rng);
        Mat M = Mat::identity();
        M.m[0][0] =  std::cos(a) * sx; M.m[0][1] = std::sin(a) * sx;
        M.m[1][0] = -std::sin(a) * sy; M.m[1][1] = std::cos(a) * sy;
        M.m[2][2] = sz;
        M.m[3][0] = d(rng); M.m[3][1] = d(rng); M.m[3][2] = d(rng);

        const float shrink = 0.76f;

        auto centroid = [](const std::vector<Vec3>& v) {
            Vec3 c{}; for (auto& p : v) c += p; c /= static_cast<float>(v.size()); return c; };

        // A: shrink in local space, then transform  (what the patch does)
        std::vector<Vec3> a1 = pts;
        { Vec3 c = centroid(a1); for (auto& p : a1) p = c + (p - c) * shrink; }
        for (auto& p : a1) p = mul(p, M);

        // B: transform, then shrink in world space  (what the old code did)
        std::vector<Vec3> b1 = pts;
        for (auto& p : b1) p = mul(p, M);
        { Vec3 c = centroid(b1); for (auto& p : b1) p = c + (p - c) * shrink; }

        for (size_t i = 0; i < a1.size(); ++i)
            for (int k = 0; k < 3; ++k)
                worst = std::max(worst, static_cast<double>(std::fabs(a1[i].v[k] - b1[i].v[k])));
    }
    printf("test5 shrink/transform commutation: worst component error over 200 random cases = %.6g\n", worst);
    printf("       (identical up to float rounding -> %s)\n", worst < 1e-2 ? "ok" : "MISMATCH");
}

// ================================================================
// 3. Grid clustering can only pull vertices inward
// ================================================================
static void testClusterConservative()
{
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> d(-1000.f, 1000.f);
    bool allInside = true;
    double worstOutside = 0.0;

    for (int trial = 0; trial < 100; ++trial)
    {
        std::vector<Vec3> pts;
        for (int i = 0; i < 400; ++i) pts.push_back({{d(rng), d(rng), d(rng)}});

        float mn[3] = {1e30f,1e30f,1e30f}, mx[3] = {-1e30f,-1e30f,-1e30f};
        for (auto& p : pts) for (int k=0;k<3;++k) { mn[k]=std::min(mn[k],p.v[k]); mx[k]=std::max(mx[k],p.v[k]); }

        const unsigned res = 10;
        const float dx = mx[0]-mn[0], dy = mx[1]-mn[1], dz = mx[2]-mn[2];
        const float cellSize = std::max(dx, std::max(dy, dz)) / res;
        const unsigned resX = std::max(1u, (unsigned)std::ceil(dx/cellSize));
        const unsigned resY = std::max(1u, (unsigned)std::ceil(dy/cellSize));

        struct CellData { Vec3 sum{}; unsigned count = 0; };
        std::unordered_map<unsigned, CellData> cells;
        for (auto& p : pts)
        {
            const unsigned gx = std::min((unsigned)std::max((p.x()-mn[0])/cellSize, 0.f), resX-1);
            const unsigned gy = std::min((unsigned)std::max((p.y()-mn[1])/cellSize, 0.f), resY-1);
            const unsigned gz = std::min((unsigned)std::max((p.z()-mn[2])/cellSize, 0.f), res-1);
            CellData& c = cells[gx + gy*resX + gz*resX*resY];
            c.sum += p; c.count++;
        }
        for (auto& kv : cells)
        {
            Vec3 avg = kv.second.sum; avg /= static_cast<float>(kv.second.count);
            for (int k = 0; k < 3; ++k)
            {
                const double over = std::max(mn[k] - avg.v[k], avg.v[k] - mx[k]);
                if (over > 1e-3) { allInside = false; worstOutside = std::max(worstOutside, over); }
            }
        }
    }
    printf("test6 clustering conservativeness: every merged vertex inside the source bounds = %s (worst overshoot %.6g)\n",
        allInside ? "yes" : "NO", worstOutside);
}

int main()
{
    testBudget();
    printf("\n");
    testCommutation();
    printf("\n");
    testClusterConservative();
    return 0;
}
