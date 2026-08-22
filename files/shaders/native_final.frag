#version 120
uniform sampler2D sceneTexture;
uniform sampler2D weightTexture;
uniform sampler2D bloomTexture;
uniform sampler2D depthTexture;
uniform sampler2D overlayDepthTexture;
uniform vec2 inverseSceneSize;
uniform float smaaEnabled;
uniform float bloomEnabled;
uniform float bloomIntensity;

uniform float atmosphericFogEnabled;
uniform float atmosphericFogStrength;
uniform float godRaysEnabled;
uniform float godRaysStrength;
uniform float sharpeningEnabled;
uniform float sharpeningStrength;
uniform float ditheringEnabled;

uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
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
uniform float environmentWaterActive;
uniform float environmentWaterHeight;
uniform vec2 sunScreenPosition;
uniform float sunVisible;
uniform vec3 sunColor;
uniform float sunDayFactor;
uniform float firstPersonView;
uniform float frameTime;

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

vec3 reconstructWorldPosition(vec2 uv, float viewDepth)
{
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 ray = normalize(cameraForward
        + cameraRight * (ndc.x * cameraAspect * cameraTanHalfFovY)
        + cameraUp * (ndc.y * cameraTanHalfFovY));
    float forwardCos = max(dot(ray, cameraForward), 0.08);
    return cameraWorldPosition + ray * (viewDepth / forwardCos);
}

float worldDepthAt(vec2 uv)
{
    return texture2D(depthTexture, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

float overlayDepthAt(vec2 uv)
{
    return texture2D(overlayDepthTexture, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

float skyVisibilityAt(vec2 uv)
{
    // World-depth texture is captured before the first-person depth clear, so
    // this test is valid in both camera modes. Values very close to 1 are sky.
    return smoothstep(0.99998, 0.9999995, worldDepthAt(uv));
}

bool firstPersonForeground(vec2 uv)
{
    if (firstPersonView < 0.5)
        return false;

    // After OpenMW clears depth for the first-person bin only hands/weapons
    // write depth. Protect them from world-space fog/light shafts.
    return overlayDepthAt(uv) < 0.9996;
}

vec3 viewRayForUv(vec2 uv)
{
    vec2 ndc = uv * 2.0 - 1.0;
    return normalize(cameraForward
        + cameraRight * (ndc.x * cameraAspect * cameraTanHalfFovY)
        + cameraUp * (ndc.y * cameraTanHalfFovY));
}

float atmosphericSkyVeil(vec3 ray, float strength)
{
    // Keep the horizon visibly denser, but avoid a hard ridge where one sky
    // shaping term takes over from another. A weighted blend of broad-horizon,
    // arched horizon and upper-haze terms is smoother than a max() switch and
    // removes the noticeable boundary line in foggy skies.
    float elevation = clamp(ray.z, -0.45, 0.98);
    float broadHorizon = 1.0 - smoothstep(-0.16, 0.92, elevation);
    float horizonArch = pow(clamp(1.0 - abs(ray.z) * 0.82, 0.0, 1.0), 1.85);
    float upperHaze = 1.0 - smoothstep(0.58, 0.96, elevation);
    float shape = clamp(broadHorizon * 0.56 + horizonArch * 0.32 + upperHaze * 0.12, 0.0, 1.0);
    float skyDensity = mix(0.30, 0.98, shape);
    float skyOptical = strength * skyDensity * 0.68;
    return clamp(1.0 - exp(-skyOptical), 0.0, 0.29);
}

vec3 applyAtmosphericFog(vec3 color, vec2 uv, float rawDepth)
{
    if (environmentExterior < 0.5 || environmentUnderwater > 0.5 || firstPersonForeground(uv))
        return color;

    vec3 ray = viewRayForUv(uv);
    float strength = clamp(atmosphericFogStrength, 0.0, 1.0);
    if (strength <= 0.0001)
        return color;

    // Reuse the same sky-veil model for both empty sky and very distant
    // geometry so mountains/buildings and the neighbouring sky do not split on
    // a visible seam. This especially helps the skyline in heavy fog.
    float skyAmount = atmosphericSkyVeil(ray, strength);
    if (rawDepth >= 0.99997)
        return mix(color, max(fogColor, vec3(0.0)), skyAmount);

    float d = linearDepth(rawDepth);
    float start = max(fogStart * 0.24, cameraNear * 3.0);
    float end = max(fogEnd, start + 1.0);
    float distanceFog = smoothstep(start, end, d);
    if (distanceFog <= 0.0001)
        return color;

    vec3 worldPos = reconstructWorldPosition(uv, d);
    float relativeHeight = worldPos.z - cameraWorldPosition.z;

    // The native atmospheric pass is an air-volume effect. When the camera is
    // above water, depth can still belong to the water surface or submerged
    // terrain. Applying the air fog to those pixels creates a hard-looking
    // horizontal/vertical cut at the water boundary. Fade the extra atmospheric
    // contribution out at and below the current water plane, while leaving the
    // engine's normal underwater fog/refraction pipeline untouched.
    float aboveWater = 1.0;
    if (environmentWaterActive > 0.5)
        aboveWater = smoothstep(environmentWaterHeight + 24.0,
            environmentWaterHeight + 220.0, worldPos.z);

    // Dense near the ground / low valleys, thinner above the eye line. The
    // transition is deliberately broad and retains a non-zero upper-air floor
    // so tall geometry does not reveal a visible horizontal fog ceiling.
    float heightWeight = mix(1.28, 0.62, smoothstep(-650.0, 2600.0, relativeHeight));

    // Two very low-frequency world-space noise octaves. The field is anchored
    // to the world, not the screen, so it drifts like fog instead of swimming
    // with camera rotation.
    vec2 noiseCoord = worldPos.xy * 0.00105 + vec2(frameTime * 0.010, -frameTime * 0.0065);
    float n0 = valueNoise(noiseCoord);
    float n1 = valueNoise(noiseCoord * 0.43 + vec2(13.7, -9.2));
    float density = mix(0.68, 1.28, n0 * 0.68 + n1 * 0.32) * heightWeight;

    // Exponential response gives a visibly volumetric build-up while staying
    // bounded and preserving the game's native distance fog underneath.
    float optical = distanceFog * strength * density * aboveWater * 0.72;
    float amount = clamp(1.0 - exp(-optical), 0.0, 0.52);

    // As geometry approaches the far-fog limit, softly inherit part of the sky
    // veil. This avoids a hard separation band between the tops of distant
    // buildings/terrain and the fogged sky behind them.
    float skylineBlend = smoothstep(end * 0.58, end * 0.96, d)
        * smoothstep(-260.0, 900.0, relativeHeight + 120.0)
        * aboveWater;
    amount = max(amount, skyAmount * skylineBlend * 0.92);

    return mix(color, max(fogColor, vec3(0.0)), amount);
}

float sunSurroundVisibility()
{
    if (sunScreenPosition.x <= 0.0 || sunScreenPosition.x >= 1.0
        || sunScreenPosition.y <= 0.0 || sunScreenPosition.y >= 1.0)
        return 0.0;

    // Sample a ring outside the apparent disc. A thin branch, pole or small
    // foreground prop can cover every sample on the disc itself, but it should
    // not remove the entire surrounding light volume. Large occluders still
    // cover this ring and therefore suppress the shafts normally.
    vec2 aspectScale = vec2(1.0 / max(cameraAspect, 0.01), 1.0);
    vec2 r0 = aspectScale * 0.026;

    float v = 0.0;
    float w = 0.0;

    v += skyVisibilityAt(sunScreenPosition + vec2( r0.x, 0.0)); w += 1.0;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r0.x, 0.0)); w += 1.0;
    v += skyVisibilityAt(sunScreenPosition + vec2(0.0,  r0.y)); w += 1.0;
    v += skyVisibilityAt(sunScreenPosition + vec2(0.0, -r0.y)); w += 1.0;
    v += skyVisibilityAt(sunScreenPosition + vec2( r0.x,  r0.y)); w += 1.0;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r0.x,  r0.y)); w += 1.0;
    v += skyVisibilityAt(sunScreenPosition + vec2( r0.x, -r0.y)); w += 1.0;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r0.x, -r0.y)); w += 1.0;

    return clamp(v / max(w, 0.0001), 0.0, 1.0);
}

float sunDiscCoverage()
{
    if (sunScreenPosition.x <= 0.0 || sunScreenPosition.x >= 1.0
        || sunScreenPosition.y <= 0.0 || sunScreenPosition.y >= 1.0)
        return 0.0;

    // Estimate how much of the *whole apparent sun disc* is visible instead of
    // treating one depth sample as a global on/off switch. This is deliberately
    // wider than a 3x3 kernel: a thin branch/leaf should only remove the part of
    // the disc that it actually covers, while a wall/rock covering the complete
    // disc still drives coverage to zero.
    vec2 r1 = inverseSceneSize * 4.5;
    vec2 r2 = inverseSceneSize * 10.0;
    float v = 0.0;
    float w = 0.0;

    v += skyVisibilityAt(sunScreenPosition) * 0.08; w += 0.08;

    v += skyVisibilityAt(sunScreenPosition + vec2( r1.x, 0.0)) * 0.07; w += 0.07;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r1.x, 0.0)) * 0.07; w += 0.07;
    v += skyVisibilityAt(sunScreenPosition + vec2(0.0,  r1.y)) * 0.07; w += 0.07;
    v += skyVisibilityAt(sunScreenPosition + vec2(0.0, -r1.y)) * 0.07; w += 0.07;
    v += skyVisibilityAt(sunScreenPosition + vec2( r1.x,  r1.y)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r1.x,  r1.y)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2( r1.x, -r1.y)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r1.x, -r1.y)) * 0.055; w += 0.055;

    v += skyVisibilityAt(sunScreenPosition + vec2( r2.x, 0.0)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r2.x, 0.0)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2(0.0,  r2.y)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2(0.0, -r2.y)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2( r2.x,  r2.y)) * 0.035; w += 0.035;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r2.x,  r2.y)) * 0.035; w += 0.035;
    v += skyVisibilityAt(sunScreenPosition + vec2( r2.x, -r2.y)) * 0.035; w += 0.035;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r2.x, -r2.y)) * 0.035; w += 0.035;

    return clamp(v / max(w, 0.0001), 0.0, 1.0);
}

vec3 computeGodRays(vec2 uv, float sunCoverage, float surroundVisibility)
{
    if (sunVisible < 0.5 || environmentExterior < 0.5 || environmentUnderwater > 0.5
        || firstPersonForeground(uv))
        return vec3(0.0);

    vec2 toSun = sunScreenPosition - uv;
    float radial = length(toSun);
    if (radial > 1.28)
        return vec3(0.0);

    float sunLuma = dot(max(sunColor, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
    if (sunLuma < 0.015)
        return vec3(0.0);

    // A small object may cover the complete sampled disc for a few frames even
    // though most of the sky around the sun is open. Preserve a reduced shaft
    // source in that case. A wall, cliff or roof covers both the disc and the
    // wider ring, so full occlusion still reaches zero.
    float effectiveCoverage = max(sunCoverage, surroundVisibility * 0.34);
    float fullOcclusionGate = smoothstep(0.012, 0.14, effectiveCoverage);
    if (fullOcclusionGate <= 0.001)
        return vec3(0.0);

    float shaftSource = fullOcclusionGate
        * mix(0.44, 1.0, smoothstep(0.10, 0.86, sunCoverage));
    float coreSource = fullOcclusionGate * smoothstep(0.05, 0.82, sunCoverage);

    // Volumetric march from the fragment toward the sun, accumulating
    // transmittance instead of averaging raw sky samples. Light physically
    // travels sun -> eye, so an occluder encountered early on this walk must
    // extinguish everything sampled behind it. That is what makes a trunk or
    // a rock carve a correctly shaped shadow *through* the shaft, rather than
    // merely dimming the whole radial average as the previous version did.
    const float sampleCount = 24.0;
    vec2 stepUv = toSun / sampleCount;

    // Per-pixel jitter of the first sample position. Without it the fixed step
    // length produces concentric banding rings around the sun; the offset is
    // averaged out by neighbouring pixels and re-rolled each frame.
    float jitter = hash12(gl_FragCoord.xy * 1.37 + vec2(mod(frameTime * 13.0, 57.0)));
    vec2 p = uv + stepUv * (0.25 + jitter * 0.75);

    // Extinction per occluded step, and the classic radial decay. Both are
    // scaled by the step length so the look stays stable regardless of how far
    // the fragment sits from the sun.
    float stepLength = radial / sampleCount;
    float extinction = 26.0 * stepLength;
    float decayRate = exp(-1.35 * stepLength);

    float transmittance = 1.0;
    float accumulated = 0.0;
    float norm = 0.0;
    float decay = 1.0;

    for (int i = 0; i < 24; ++i)
    {
        vec2 sp = clamp(p, vec2(0.0), vec2(1.0));
        float sky = skyVisibilityAt(sp);

        // Sky contributes light; geometry contributes extinction. Accumulate
        // the lit part *through* the transmittance already lost closer to the
        // camera, so partial occluders soften the shaft instead of clipping it.
        accumulated += sky * transmittance * decay;
        norm += decay;

        transmittance *= exp(-(1.0 - sky) * extinction);

        p += stepUv;
        decay *= decayRate;

        // Once the path is essentially blocked there is nothing left to gather.
        if (transmittance < 0.004)
            break;
    }

    float integrated = accumulated / max(norm, 0.0001);
    float localSky = skyVisibilityAt(uv);

    // Suppress the broad "whole sky glows" component while keeping shafts on
    // geometry/fog where the ray path opens toward the sun.
    float shaft = clamp(integrated - localSky * 0.42, 0.0, 1.0);
    shaft = pow(shaft, 1.35);

    // Attenuation. The radial term is now exponential rather than a plain
    // power curve, which keeps the near-sun region bright while letting distant
    // shafts fade out smoothly instead of ending on a visible ring.
    float sunFalloff = exp(-radial * 2.15) * pow(clamp(1.0 - radial / 1.28, 0.0, 1.0), 0.85);
    float coreGlow = pow(clamp(1.0 - radial / 0.22, 0.0, 1.0), 5.0) * 0.10;

    // Fade as the sun approaches (and leaves) the screen edge. Without this the
    // whole shaft field switches off in one frame when sunVisible flips.
    vec2 edge = min(sunScreenPosition, vec2(1.0) - sunScreenPosition);
    float edgeFade = smoothstep(-0.22, 0.06, min(edge.x, edge.y));

    vec3 warmSun = mix(max(sunColor, vec3(0.0)),
        vec3(1.0, 0.76, 0.48) * min(1.0, sunLuma + 0.20), 0.20);

    // Keep a faint remnant at night rather than reusing daytime intensity.
    // Sun diffuse luminance already follows the weather/time-of-day lighting,
    // so this also gives sensible attenuation in heavy overcast conditions.
    float daylight = smoothstep(0.02, 0.85, sunDayFactor);
    float timeOfDayAttenuation = mix(0.18, 1.0, daylight);
    // Keep the broad shaft field stable under thin occluders. Only the sun
    // core follows partial disc coverage strongly; the shaft field mostly
    // responds to the local radial samples above. Full occlusion still fades
    // both terms to zero.
    float intensity = (shaft * 1.05 * shaftSource + coreGlow * coreSource) * sunFalloff * edgeFade;
    return warmSun * intensity * max(godRaysStrength, 0.0) * 0.70 * timeOfDayAttenuation;
}

vec3 computeSunGlare(vec2 uv, float coverage, float surroundVisibility)
{
    if (sunVisible < 0.5 || environmentExterior < 0.5 || environmentUnderwater > 0.5)
        return vec3(0.0);

    // The broad halo uses the same surrounding-sky test as the shafts: a small
    // object can kill the bright core, but it should only attenuate the halo.
    // Large occluders cover both regions and still fade the full effect away.
    float effectiveCoverage = max(coverage, surroundVisibility * 0.30);
    float visibility = smoothstep(0.01, 0.22, effectiveCoverage);
    if (visibility <= 0.0001)
        return vec3(0.0);

    vec2 d = uv - sunScreenPosition;
    d.x *= max(cameraAspect, 0.01);
    float radius = length(d);

    vec2 centerDelta = sunScreenPosition - vec2(0.5);
    centerDelta.x *= max(cameraAspect, 0.01);
    float facing = clamp(1.0 - length(centerDelta) / 0.92, 0.0, 1.0);
    facing = facing * facing;

    float sunLuma = dot(max(sunColor, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
    if (sunLuma < 0.015)
        return vec3(0.0);

    vec3 warmSun = mix(max(sunColor, vec3(0.0)),
        vec3(1.0, 0.76, 0.48) * min(1.0, sunLuma + 0.20), 0.22);
    float daylight = smoothstep(0.02, 0.85, sunDayFactor);
    float timeOfDayAttenuation = mix(0.18, 1.0, daylight);

    // Local flash/halo plus a weak full-frame veiling glare when the player
    // looks toward the sun.  All terms are positive-only.
    float core = exp(-radius * radius * 190.0) * 0.16;
    float halo = exp(-radius * radius * 24.0) * 0.055;
    float wash = facing * 0.032;

    // Thin occluders affect the small core more strongly than the broad wash,
    // avoiding the old whole-screen pop when a branch crosses the sun.
    float softCoverage = mix(0.48, 1.0, smoothstep(0.12, 0.88, effectiveCoverage));
    float local = (core * smoothstep(0.04, 0.80, coverage) + halo * softCoverage) * visibility;
    float veil = wash * visibility * softCoverage;

    // Do not paint a bright local blob over first-person hands/weapons, but
    // retain the subtle full-screen veiling glare.
    if (firstPersonForeground(uv))
        local *= 0.12;

    return warmSun * (local + veil) * timeOfDayAttenuation;
}

vec3 sharpenCAS(vec2 uv, vec3 center)
{
    vec2 dx = vec2(inverseSceneSize.x, 0.0);
    vec2 dy = vec2(0.0, inverseSceneSize.y);
    vec3 b = texture2D(sceneTexture, clamp(uv - dy, vec2(0.0), vec2(1.0))).rgb;
    vec3 d = texture2D(sceneTexture, clamp(uv - dx, vec2(0.0), vec2(1.0))).rgb;
    vec3 f = texture2D(sceneTexture, clamp(uv + dx, vec2(0.0), vec2(1.0))).rgb;
    vec3 h = texture2D(sceneTexture, clamp(uv + dy, vec2(0.0), vec2(1.0))).rgb;

    vec3 mn = min(center, min(min(b, d), min(f, h)));
    vec3 mx = max(center, max(max(b, d), max(f, h)));
    vec3 amp = clamp(min(mn, 2.0 - mx) / max(mx, vec3(0.001)), 0.0, 1.0);
    amp = inversesqrt(max(amp, vec3(0.01)));
    float peak = mix(8.0, 5.0, clamp(sharpeningStrength, 0.0, 1.0));
    vec3 w = -1.0 / (amp * peak);
    vec3 outColor = ((b + d + f + h) * w + center) / max(1.0 + 4.0 * w, vec3(0.05));
    return clamp(outColor, mn * 0.92, mx * 1.08);
}

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseSceneSize;
    float rawDepth = texture2D(depthTexture, uv).r;
    vec3 color = texture2D(sceneTexture, uv).rgb;

    if (smaaEnabled >= 0.5)
    {
        vec4 w = texture2D(weightTexture, uv);
        float hw = clamp(w.x + w.y, 0.0, 0.92);
        float vw = clamp(w.z + w.w, 0.0, 0.92);
        vec3 h = texture2D(sceneTexture, clamp(uv - vec2(inverseSceneSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb * w.x
               + texture2D(sceneTexture, clamp(uv + vec2(inverseSceneSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb * w.y;
        vec3 v = texture2D(sceneTexture, clamp(uv - vec2(0.0, inverseSceneSize.y), vec2(0.0), vec2(1.0))).rgb * w.z
               + texture2D(sceneTexture, clamp(uv + vec2(0.0, inverseSceneSize.y), vec2(0.0), vec2(1.0))).rgb * w.w;
        float total = hw + vw;
        if (total > 0.0001)
        {
            vec3 blended = (h + v) / max(total, 0.0001);
            color = mix(color, blended, clamp(total * 0.68, 0.0, 0.80));
        }
    }

    if (sharpeningEnabled >= 0.5)
        color = mix(color, sharpenCAS(uv, color), clamp(sharpeningStrength, 0.0, 1.0));

    if (bloomEnabled >= 0.5)
        color += max(texture2D(bloomTexture, uv).rgb, vec3(0.0)) * max(bloomIntensity, 0.0);

    if (atmosphericFogEnabled >= 0.5)
        color = applyAtmosphericFog(color, uv, rawDepth);

    // Replacement for the legacy query-driven SunFlash/SunGlare. Sample the
    // sun visibility once and share it between glare and god rays; this avoids
    // duplicating a relatively expensive depth kernel in the full-screen pass.
    float sunCoverage = 0.0;
    float sunSurround = 0.0;
    if (sunVisible >= 0.5 && environmentExterior >= 0.5 && environmentUnderwater < 0.5)
    {
        sunCoverage = sunDiscCoverage();
        sunSurround = sunSurroundVisibility();
    }
    color += computeSunGlare(uv, sunCoverage, sunSurround);

    if (godRaysEnabled >= 0.5)
        color += computeGodRays(uv, sunCoverage, sunSurround);

    if (ditheringEnabled >= 0.5)
    {
        // Tiny ordered/noise dither (sub-LSB at 8-bit output) suppresses visible
        // bands in fog, sky gradients and HDR highlights without adding grain.
        float d = hash12(gl_FragCoord.xy + vec2(mod(frameTime * 17.0, 31.0))) - 0.5;
        color += vec3(d / 255.0);
    }

    gl_FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
