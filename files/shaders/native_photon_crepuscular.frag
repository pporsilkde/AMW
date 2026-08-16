#version 120
/*
 * ArenaMW Photon-style crepuscular rays.
 *
 * The raymarch structure, cloud-shadowed single scattering, adaptive
 * horizon/zenith step count, Beer-Lambert extinction and the paired
 * Henyey-Greenstein phase lobes are adapted from Photon Shaders by
 * SixthSurge (include/sky/crepuscular_rays.glsl). See
 * PHOTON_SHADERS_LICENSE.txt included with this patch.
 *
 * Photon normally samples its own cloud-shadow map in shadow-view space.
 * ArenaMW V1 does not have that map, so this port evaluates the same
 * weather-driven cloud coverage field used by ArenaMW cloud shadows at
 * each atmospheric raymarch sample. The result remains world-space and
 * cloud-driven instead of the old screen-space radial blur.
 */

uniform sampler2D depthTexture;
uniform vec2 inverseCrepuscularSize;
uniform float cameraNear;
uniform float cameraFar;
uniform vec3 cameraWorldPosition;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform vec3 cameraForward;
uniform float cameraTanHalfFovY;
uniform float cameraAspect;
uniform float environmentExterior;
uniform float environmentUnderwater;
uniform vec3 sunColor;
uniform vec3 sunDirection;
uniform float frameTime;
uniform float godRaysEnabled;
uniform float godRaysStrength;
uniform float volumetricCloudsEnabled;
uniform float cloudCoverage;
uniform float cloudSpeed;
uniform float cloudBaseHeight;

const float PI = 3.14159265358979323846;

float linearDepth(float depth)
{
    float n = max(cameraNear, 0.001);
    float f = max(cameraFar, n + 1.0);
    float z = depth * 2.0 - 1.0;
    return (2.0 * n * f) / max(f + n - z * (f - n), 0.0001);
}

float hash12(vec2 p)
{
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float cloudLightAt(vec3 worldPos, vec3 lightDir)
{
    // Cloud shafts are produced below the cloud deck. Above it there is no
    // cloud between this atmospheric sample and the sun.
    if (lightDir.z <= 0.025 || worldPos.z >= cloudBaseHeight)
        return 1.0;

    float travel = (cloudBaseHeight - worldPos.z) / max(lightDir.z, 0.025);
    if (travel < 0.0 || travel > 52000.0)
        return 1.0;

    vec2 projected = worldPos.xy + lightDir.xy * travel;
    float wind = frameTime * cloudSpeed;
    vec2 q = projected * 0.000105 + vec2(wind * 0.014, wind * 0.008);

    float broad = valueNoise(q) * 0.62
        + valueNoise(q * 2.03 + vec2(13.7, 7.1)) * 0.28
        + valueNoise(q * 4.11 + vec2(3.2, 17.9)) * 0.10;
    float threshold = mix(0.76, 0.34, clamp(cloudCoverage, 0.0, 1.0));
    float cover = smoothstep(threshold - 0.07, threshold + 0.13, broad);

    // Photon cubically weights the sampled cloud-shadow transmittance.
    // Return transmittance here and apply the cubic response in the march.
    return clamp(1.0 - cover * 0.94, 0.04, 1.0);
}

float henyeyGreenstein(float cosTheta, float g)
{
    float gg = g * g;
    float denom = max(1.0 + gg - 2.0 * g * cosTheta, 0.0005);
    return (1.0 - gg) / (4.0 * PI * pow(denom, 1.5));
}

void main()
{
    if (godRaysEnabled < 0.5 || volumetricCloudsEnabled < 0.5
        || environmentExterior < 0.5 || environmentUnderwater > 0.5)
    {
        gl_FragColor = vec4(0.0);
        return;
    }

    vec3 lightDir = normalize(sunDirection);
    if (lightDir.z <= 0.015)
    {
        gl_FragColor = vec4(0.0);
        return;
    }

    vec2 uv = gl_FragCoord.xy * inverseCrepuscularSize;
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 rayDir = normalize(cameraForward
        + cameraRight * (ndc.x * cameraAspect * cameraTanHalfFovY)
        + cameraUp * (ndc.y * cameraTanHalfFovY));

    // Match Photon's idea: many samples along the horizon, only a few toward
    // the zenith. Fixed maximum loop count keeps this GLSL 1.20 friendly.
    float zenith = clamp(abs(rayDir.z), 0.0, 1.0);
    float stepCount = floor(mix(20.0, 4.0, zenith) + 0.5);

    float rawDepth = texture2D(depthTexture, uv).r;
    float maxRayLength = min(max(cameraFar, 2048.0), 14000.0);
    if (rawDepth < 0.999999)
    {
        float viewDepth = linearDepth(rawDepth);
        float forwardCos = max(dot(rayDir, cameraForward), 0.08);
        maxRayLength = min(maxRayLength, viewDepth / forwardCos);
    }

    if (maxRayLength <= 8.0)
    {
        gl_FragColor = vec4(0.0);
        return;
    }

    float stepLength = maxRayLength / max(stepCount, 1.0);
    float dither = hash12(gl_FragCoord.xy + vec2(mod(frameTime * 9.0, 53.0), 17.0));

    // Photon's extinction is atmosphere-scaled. These coefficients are
    // rescaled for Morrowind world units while retaining Beer-Lambert form.
    float amount = clamp(godRaysStrength, 0.0, 1.5);
    vec3 extinctionCoeff = vec3(0.000105) * mix(0.65, 1.20, clamp(amount, 0.0, 1.0));
    vec3 stepOpticalDepth = extinctionCoeff * stepLength;
    vec3 stepTransmittance = exp(-stepOpticalDepth);
    vec3 transmittedFraction = (vec3(1.0) - stepTransmittance)
        / max(stepOpticalDepth, vec3(0.000001));

    float LoV = dot(rayDir, lightDir);
    float forwards = henyeyGreenstein(LoV, 0.5);
    float backwards = henyeyGreenstein(LoV, -0.2);
    float phase = forwards + 0.5 * backwards;

    vec3 scattering = vec3(0.0);
    vec3 transmittance = vec3(1.0);

    for (int i = 0; i < 20; ++i)
    {
        if (float(i) >= stepCount)
            break;

        float d = stepLength * (float(i) + dither);
        vec3 worldPos = cameraWorldPosition + rayDir * d;
        float cloudLight = cloudLightAt(worldPos, lightDir);

        // Same visual shaping used by Photon: cubic cloud-light response and
        // a distance build-up so rays do not form a bright veil at the eye.
        float distanceBuild = 1.0 - exp2(-0.0018 * d);
        float heightFade = 1.0 - smoothstep(cloudBaseHeight * 0.92, cloudBaseHeight * 1.03, worldPos.z);
        scattering += vec3(cloudLight * cloudLight * cloudLight)
            * transmittance * distanceBuild * max(heightFade, 0.0);
        transmittance *= stepTransmittance;
    }

    vec3 lightColor = max(sunColor, vec3(0.0));
    float sunLuma = dot(lightColor, vec3(0.2126, 0.7152, 0.0722));
    if (sunLuma < 0.008)
    {
        gl_FragColor = vec4(0.0);
        return;
    }

    scattering *= extinctionCoeff * transmittedFraction * lightColor * stepLength;
    scattering *= (5.2 * amount) * phase;

    // Prevent pathological HDR spikes close to the solar direction while
    // still allowing bloom/HDR to shape the final appearance later.
    scattering = min(max(scattering, vec3(0.0)), vec3(1.75));
    gl_FragColor = vec4(scattering, 1.0);
}
