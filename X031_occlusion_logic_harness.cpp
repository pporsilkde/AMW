#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <vector>

struct Vec4 { double v[4]; };
struct Mat  { double m[4][4]; };

static Mat mul(const Mat& a, const Mat& b)
{
    Mat r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}

static Vec4 transformPlane(const Vec4& p, const Mat& M)
{
    Vec4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.v[i] += p.v[j] * M.m[i][j];
    return r;
}

static double distance(const Vec4& plane, double x, double y, double z)
{
    return plane.v[0]*x + plane.v[1]*y + plane.v[2]*z + plane.v[3];
}

struct OccluderSim
{
    std::map<std::pair<int,int>, bool> cache;
    bool regionValid = false;
    std::pair<int,int> cachedPos{0,0};
    int cachedRadius = -1;
    int budgetPerBuild = 24;
    unsigned int lastBuilt = 0;

    void prune(std::pair<int,int> c, int r)
    {
        const int keep = std::max(1, r) + 2;
        for (auto it = cache.begin(); it != cache.end();)
        {
            if (std::abs(it->first.first-c.first)>keep || std::abs(it->first.second-c.second)>keep)
                it=cache.erase(it); else ++it;
        }
    }
    void collect(int cx0,int cy0,int radius)
    {
        lastBuilt=0;
        int budget=budgetPerBuild>0?budgetPerBuild:std::numeric_limits<int>::max();
        bool complete=true;
        for(int cy=cy0-radius;cy<=cy0+radius;++cy)
            for(int cx=cx0-radius;cx<=cx0+radius;++cx)
            {
                if(cache.count({cx,cy})) continue;
                if(budget<=0){complete=false;continue;}
                --budget; ++lastBuilt; cache[{cx,cy}]=true;
            }
        cachedPos={cx0,cy0}; cachedRadius=radius; regionValid=complete; prune(cachedPos,radius);
    }
};

static bool testBudget()
{
    const int radius=8, region=(2*radius+1)*(2*radius+1);
    OccluderSim sim; int frames=0;
    while(!sim.regionValid && frames<1000){sim.collect(0,0,radius);++frames;}
    const int expect=(region+sim.budgetPerBuild-1)/sim.budgetPerBuild;
    bool ok=frames==expect;
    std::printf("cold: %d frames expected %d => %s\n",frames,expect,ok?"ok":"FAIL");
    sim.collect(1,0,radius); bool step=sim.lastBuilt==unsigned(2*radius+1);
    std::printf("step: %u built expected %d => %s\n",sim.lastBuilt,2*radius+1,step?"ok":"FAIL");
    OccluderSim off; off.budgetPerBuild=0; off.collect(0,0,radius); bool unlimited=off.lastBuilt==unsigned(region);
    std::printf("unlimited: %u expected %d => %s\n",off.lastBuilt,region,unlimited?"ok":"FAIL");
    return ok&&step&&unlimited;
}

static Mat perspective(double fovyDeg,double aspect,double zn,double zf)
{
    const double f=1.0/std::tan(fovyDeg*3.14159265358979/360.0);
    Mat P{}; P.m[0][0]=f/aspect; P.m[1][1]=f; P.m[2][2]=(zf+zn)/(zn-zf); P.m[2][3]=-1.0; P.m[3][2]=2.0*zf*zn/(zn-zf); return P;
}

static bool testFrustum(const char* label,const Mat& proj)
{
    Mat view{}; for(int i=0;i<4;++i)view.m[i][i]=1.0;
    Mat vp=mul(view,proj);
    const Vec4 clip[4]={{{1,0,0,1}},{{-1,0,0,1}},{{0,1,0,1}},{{0,-1,0,1}}};
    Vec4 planes[4]; for(int i=0;i<4;++i) planes[i]=transformPlane(clip[i],vp);
    auto inside=[&](double x,double y,double z){for(auto &p:planes)if(distance(p,x,y,z)<0)return false;return true;};
    bool ahead=true,behind=true,side=true;
    for(double d=1;d<20000;d*=2){if(!inside(0,0,-d))ahead=false;if(inside(0,0,d))behind=false;if(inside(10*d,0,-d))side=false;}
    std::printf("%s ahead=%s behind-reject=%s side-reject=%s\n",label,ahead?"ok":"FAIL",behind?"ok":"FAIL",side?"ok":"FAIL");
    return ahead&&behind&&side;
}

int main()
{
    bool ok=testBudget();
    ok &= testFrustum("standard", perspective(75.0,16.0/9.0,1.0,100000.0));
    ok &= testFrustum("reversed", perspective(75.0,16.0/9.0,100000.0,1.0));
    return ok?0:1;
}
