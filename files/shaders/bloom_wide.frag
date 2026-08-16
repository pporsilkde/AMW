#version 120

uniform sampler2D bloomTexture;
uniform vec2 inverseWideSize;
uniform vec2 inverseNearSize;
uniform float bloomRadius;

vec3 tap(vec2 uv)
{
    return max(texture2D(bloomTexture, clamp(uv, vec2(0.0), vec2(1.0))).rgb, vec3(0.0));
}

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseWideSize;
    vec2 p = inverseNearSize * max(bloomRadius, 0.5) * 4.0;

    // Cheap wide halo derived from the already-filtered half-resolution bloom.
    // The asymmetric rings avoid a boxy look while approximating the broad BSL
    // bloom scales without a seven-level packed pyramid.
    vec3 c = tap(uv) * 0.20;
    c += (tap(uv + vec2( p.x, 0.0)) + tap(uv - vec2( p.x, 0.0))
        + tap(uv + vec2(0.0,  p.y)) + tap(uv - vec2(0.0,  p.y))) * 0.085;
    c += (tap(uv + p * vec2( 1.4,  1.4)) + tap(uv + p * vec2(-1.4,  1.4))
        + tap(uv + p * vec2( 1.4, -1.4)) + tap(uv + p * vec2(-1.4, -1.4))) * 0.060;
    c += (tap(uv + vec2( p.x * 2.8, p.y * 0.5)) + tap(uv - vec2( p.x * 2.8, p.y * 0.5))
        + tap(uv + vec2( p.x * 0.5, p.y * 2.8)) + tap(uv - vec2( p.x * 0.5, p.y * 2.8))) * 0.035;

    gl_FragColor = vec4(c, 1.0);
}
