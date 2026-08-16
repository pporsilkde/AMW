#version 120
/*
 * ArenaMW experimental volumetric clouds.
 * Layer shaping, low-resolution raymarching and light-transmittance concepts
 * are adapted from Photon Shaders by SixthSurge. See
 * PHOTON_SHADERS_LICENSE.txt included with this patch.
 *
 * V1 intentionally omits Photon's temporal reprojection/history pass. It uses
 * a dedicated low-resolution render target so the expensive raymarch remains
 * isolated and can be validated before temporal accumulation is introduced.
 */

uniform vec2 inverseCloudSize;
uniform vec3 cameraWorldPosition;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform vec3 cameraForward;
uniform float cameraTanHalfFovY;
uniform float cameraAspect;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 fogColor;
uniform vec3 skyColor;
uniform float frameTime;
uniform float environmentExterior;
uniform float environmentUnderwater;
uniform float cloudCoverage;
uniform float cloudDensity;
uniform float cloudSpeed;
uniform float cloudStepCount;
uniform float cloudBaseHeight;
uniform float cloudTopHeight;
uniform float cloudDetail;

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float noise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
}

float fbm3(vec3 p)
{
    float v = noise3(p) * 0.62;
    v += noise3(p * 2.03 + vec3(13.7, 7.1, 19.3)) * 0.28;
    v += noise3(p * 4.11 + vec3(3.2, 17.9, 5.6)) * 0.10;
    return v;
}

float cloudHeightShape(float h)
{
    float bottom = smoothstep(0.00, 0.13, h);
    float top = 1.0 - smoothstep(0.68, 1.00, h);
    float middle = mix(0.78, 1.0, smoothstep(0.10, 0.45, h));
    return bottom * top * middle;
}

float cloudDensityAt(vec3 worldPos)
{
    float layerHeight = max(cloudTopHeight - cloudBaseHeight, 1.0);
    float h = (worldPos.z - cloudBaseHeight) / layerHeight;
    if (h <= 0.0 || h >= 1.0)
        return 0.0;

    float wind = frameTime * cloudSpeed;
    vec3 q = vec3(
        worldPos.x * 0.000105 + wind * 0.014,
        worldPos.y * 0.000105 + wind * 0.008,
        h * 1.65);

    float broad = fbm3(q);
    float detail = noise3(q * 3.7 + vec3(h * 2.1, -h * 1.3, 11.0));
    float coverageThreshold = mix(0.76, 0.34, clamp(cloudCoverage, 0.0, 1.0));
    float body = smoothstep(coverageThreshold - 0.08, coverageThreshold + 0.12, broad);
    body -= (1.0 - detail) * 0.24 * clamp(cloudDetail, 0.0, 1.0);
    return max(body, 0.0) * cloudHeightShape(h) * max(cloudDensity, 0.0);
}

bool intersectCloudLayer(vec3 ro, vec3 rd, out float tNear, out float tFar)
{
    if (abs(rd.z) < 0.0005)
        return false;

    float a = (cloudBaseHeight - ro.z) / rd.z;
    float b = (cloudTopHeight - ro.z) / rd.z;
    tNear = max(min(a, b), 0.0);
    tFar = min(max(a, b), 52000.0);
    return tFar > tNear + 1.0;
}

float phaseApprox(float mu)
{
    float forward = pow(clamp(mu * 0.5 + 0.5, 0.0, 1.0), 10.0);
    float backward = pow(clamp(-mu * 0.5 + 0.5, 0.0, 1.0), 3.0);
    return 0.62 + forward * 1.65 + backward * 0.16;
}

void main()
{
    if (environmentExterior < 0.5 || environmentUnderwater > 0.5)
    {
        gl_FragColor = vec4(0.0);
        return;
    }

    vec2 uv = gl_FragCoord.xy * inverseCloudSize;
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 rayDir = normalize(cameraForward
        + cameraRight * (ndc.x * cameraAspect * cameraTanHalfFovY)
        + cameraUp * (ndc.y * cameraTanHalfFovY));

    float tNear, tFar;
    if (!intersectCloudLayer(cameraWorldPosition, rayDir, tNear, tFar))
    {
        gl_FragColor = vec4(0.0);
        return;
    }

    float steps = clamp(floor(cloudStepCount + 0.5), 8.0, 32.0);
    float stepLength = (tFar - tNear) / steps;
    float jitter = hash13(vec3(gl_FragCoord.xy, mod(frameTime * 0.37, 127.0)));
    float t = tNear + stepLength * jitter;

    vec3 lightDir = normalize(sunDirection);
    float sunHeight = clamp(lightDir.z * 4.0 + 0.2, 0.0, 1.0);
    float mu = dot(rayDir, lightDir);
    float phase = phaseApprox(mu);
    vec3 dayLight = max(sunColor, vec3(0.0));
    vec3 ambientLight = mix(max(fogColor, vec3(0.0)), max(skyColor, vec3(0.0)) + vec3(0.04), 0.55);

    vec3 scattering = vec3(0.0);
    float transmittance = 1.0;
    float firstDistance = tFar;

    for (int i = 0; i < 32; ++i)
    {
        if (float(i) >= steps || transmittance < 0.018)
            break;

        vec3 p = cameraWorldPosition + rayDir * t;
        float density = cloudDensityAt(p);
        if (density > 0.008)
        {
            if (firstDistance >= tFar)
                firstDistance = t;

            // Two cheap lighting probes approximate Photon's in-cloud light
            // transmittance while keeping the prototype suitable for old GL.
            float ld0 = cloudDensityAt(p + lightDir * 650.0);
            float ld1 = cloudDensityAt(p + lightDir * 1450.0);
            float lightTrans = exp(-(ld0 * 0.80 + ld1 * 0.55));

            float extinction = density * stepLength * 0.00072;
            float sampleOpacity = 1.0 - exp(-extinction);
            float powder = 1.0 - exp(-density * 2.6);
            vec3 direct = dayLight * (0.30 + lightTrans * 0.92) * phase * mix(0.78, 1.14, powder);
            vec3 localLight = ambientLight * (0.68 + 0.30 * sunHeight) + direct;

            scattering += transmittance * sampleOpacity * localLight;
            transmittance *= (1.0 - sampleOpacity);
        }
        t += stepLength;
    }

    float opacity = clamp(1.0 - transmittance, 0.0, 0.985);
    if (opacity < 0.002)
    {
        gl_FragColor = vec4(0.0);
        return;
    }

    // Aerial perspective keeps far/horizon clouds from looking pasted onto
    // the sky and mirrors Photon's idea of atmosphere-aware cloud scattering.
    float distanceHaze = smoothstep(9000.0, 42000.0, firstDistance);
    vec3 cloudColor = scattering / max(opacity, 0.04);
    cloudColor = mix(cloudColor, max(fogColor, vec3(0.0)), distanceHaze * 0.58);
    cloudColor = max(cloudColor, vec3(0.0));

    gl_FragColor = vec4(cloudColor, opacity);
}
