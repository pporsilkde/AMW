#ifndef OPENMW_MAGNUS_WATER_GLSL
#define OPENMW_MAGNUS_WATER_GLSL

#if @lightingMethodClustered

#ifndef POINT_LIGHT_COVERAGE_MULTIPLIER
#define POINT_LIGHT_COVERAGE_MULTIPLIER 1.35
#endif

struct MagnusWaterPointLight
{
    vec4 position;
    vec4 diffuse;
    vec4 ambient;
    vec4 specular;
    float constant;
    float linear;
    float quadratic;
    float radius;
};

struct MagnusWaterLightGrid
{
    uint offset;
    uint count;
};

layout(std430, binding = 2) restrict buffer MagnusWaterPointLightSSBO { MagnusWaterPointLight magnusWaterPointLights[]; };
layout(std430, binding = 3) restrict buffer MagnusWaterLightGridSSBO { MagnusWaterLightGrid magnusWaterLightGrid[]; };
layout(std430, binding = 4) restrict buffer MagnusWaterLightIndexListSSBO { uint magnusWaterLightIndexList[]; };

uniform float magnusNear;
uniform float magnusClusterFar;
uniform vec3 magnusGridSize;
uniform vec2 magnusScreenRes;

float magnusWaterQuickstep(float x)
{
    x = clamp(x, 0.0, 1.0);
    x = 1.0 - x * x;
    x = 1.0 - x * x;
    return x;
}

int magnusWaterClusterIndex(vec2 screenCoord, vec3 viewPos)
{
    float safeNear = max(magnusNear, 0.001);
    float safeFar = max(magnusClusterFar, safeNear + 0.001);
    float z = max(abs(viewPos.z), safeNear);
    int zTile = int((log(z / safeNear) * magnusGridSize.z) / log(safeFar / safeNear));
    zTile = clamp(zTile, 0, int(magnusGridSize.z) - 1);

    vec2 tileSize = magnusScreenRes / magnusGridSize.xy;
    ivec2 tileXY = ivec2(screenCoord / max(tileSize, vec2(1.0)));
    tileXY.x = clamp(tileXY.x, 0, int(magnusGridSize.x) - 1);
    tileXY.y = clamp(tileXY.y, 0, int(magnusGridSize.y) - 1);

    return tileXY.x + tileXY.y * int(magnusGridSize.x)
        + zTile * int(magnusGridSize.x) * int(magnusGridSize.y);
}

float magnusWaterAttenuation(MagnusWaterPointLight light, float lightDistance)
{
    float attenuation = clamp(1.0 / (light.constant + light.linear * lightDistance
        + light.quadratic * lightDistance * lightDistance), 0.0, 1.0);
    float effectiveRadius = max(light.radius * POINT_LIGHT_COVERAGE_MULTIPLIER, 0.001);
    return attenuation * (1.0 - magnusWaterQuickstep((lightDistance / effectiveRadius) - 1.0));
}

// Point-light reflection for water.  Point lights are stored in view space by
// LightManager, so convert the water surface point/normal before evaluating the
// same cluster used by regular fragment lighting.
vec3 magnusWaterPointSpecular(vec3 worldPosition, vec3 worldNormal, float hardness, float intensity)
{
    vec3 viewPos = (gl_ModelViewMatrix * vec4(worldPosition, 1.0)).xyz;
    vec3 viewNormal = normalize(gl_NormalMatrix * worldNormal);
    vec3 viewDirection = normalize(-viewPos);

    int clusterIndex = magnusWaterClusterIndex(gl_FragCoord.xy, viewPos);
    MagnusWaterLightGrid grid = magnusWaterLightGrid[clusterIndex];
    vec3 result = vec3(0.0);

    for (uint i = 0u; i < grid.count; ++i)
    {
        MagnusWaterPointLight light = magnusWaterPointLights[magnusWaterLightIndexList[grid.offset + i]];
        vec3 lightVector = light.position.xyz - viewPos;
        float lightDistance = length(lightVector);
        if (lightDistance <= 0.0001)
            continue;

        vec3 lightDirection = lightVector / lightDistance;
        float NdotL = max(dot(viewNormal, lightDirection), 0.0);
        if (NdotL <= 0.0)
            continue;

        vec3 halfVector = normalize(lightDirection + viewDirection);
        float highlight = pow(max(dot(viewNormal, halfVector), 0.0), max(hardness, 1.0));
        float attenuation = magnusWaterAttenuation(light, lightDistance);
        result += light.specular.rgb * highlight * attenuation * intensity;
    }

    return result;
}

#else

vec3 magnusWaterPointSpecular(vec3 worldPosition, vec3 worldNormal, float hardness, float intensity)
{
    return vec3(0.0);
}

#endif

#endif
