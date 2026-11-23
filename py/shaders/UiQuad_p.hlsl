// ACZero Enhancer — 2D UI Pixel Shader (SM 6.0)
// mode 0: RGBA texture * vertex color (use 1x1 white texture for solid rects/lines)
// mode 1: R8_UNORM glyph alpha atlas * vertex color
// mode 2: Dual Premultiplied-Ash + Additive-Fire Ember Shader (requires Premultiplied PSO: ONE + INV_SRC_ALPHA):
//         color.r = heat [0..1]
//         color.g = bokeh blur [0..1]
//         color.b = floor(seed 1..30) + frac(crust 0.0..0.95)
//         color.a = alpha [0..1]

Texture2D<float4> g_Tex     : register(t0);
SamplerState      g_Sampler : register(s0);

struct PSInput {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint mode : TEXCOORD1;
};

float Hash21(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float3 BlackbodyEmber(float t) {
    t = saturate(t);
    float3 c0 = float3(0.32f, 0.015f, 0.00f);
    float3 c1 = float3(0.95f, 0.14f,  0.01f);
    float3 c2 = float3(1.00f, 0.56f,  0.05f);
    float3 c3 = float3(1.00f, 0.93f,  0.46f);
    float3 c4 = float3(1.00f, 0.99f,  0.90f);
    if (t < 0.25f) return lerp(c0, c1, t * 4.0f);
    if (t < 0.55f) return lerp(c1, c2, (t - 0.25f) / 0.30f);
    if (t < 0.82f) return lerp(c2, c3, (t - 0.55f) / 0.27f);
    return lerp(c3, c4, (t - 0.82f) / 0.18f);
}

float4 main(PSInput input) : SV_TARGET {
    if (input.mode == 2u) {
        float2 p     = input.uv * 2.0f - 1.0f;
        float  heat  = input.color.r;
        float  blur  = saturate(input.color.g);
        float  seed  = floor(input.color.b);
        float  crust = saturate(frac(input.color.b) * 1.08f); // 0 = fresh glowing, 1 = heavily charred black coal
        float  alpha = input.color.a;

        float r = length(p);
        if (r >= 1.0f) discard;

        // Layer 3 (blur > 0.85): pure soft giant out-of-focus bokeh light spot (zero sharp edges or crust!)
        if (blur > 0.85f) {
            float bokeh = exp(-r * r * 4.2f) * saturate((1.0f - r) * 3.0f);
            float3 bCol = BlackbodyEmber(0.38f + heat * 0.42f) * bokeh * (0.65f + 0.55f * heat) * alpha;
            return float4(bCol, 0.0f); // Pure additive soft bokeh light!
        }

        // Organic jagged coal flake silhouette
        float ang = atan2(p.y, p.x);
        float edgeNoise = 0.78f
                        + 0.18f * sin(ang * 3.0f + seed * 1.7f)
                        + 0.12f * cos(ang * 5.0f - seed * 2.3f)
                        + 0.09f * sin(ang * 2.0f + seed * 0.9f);
        float2 ep = float2(p.x * 1.15f, p.y * 1.55f);
        float  dFlake = length(ep) / max(0.35f, edgeNoise * 0.44f);

        // Internal ember cracks/veins (2 octaves of noise)
        float2 np = p * 4.4f + float2(seed * 1.37f, seed * 0.79f);
        float n1 = ValueNoise(np);
        float n2 = ValueNoise(np * 2.3f + 7.3f);
        float veins = saturate(n1 * 0.60f + n2 * 0.50f);

        // Growing black charred crust over time; bright oxygen flares (high heat) burst through cracks!
        float crustThreshold = saturate(0.18f + crust * 0.78f - max(0.0f, heat - 0.58f) * 1.10f);
        float openFire = smoothstep(crustThreshold - 0.10f, crustThreshold + 0.14f, veins);
        openFire = lerp(openFire, 0.58f, blur * 0.65f);

        // Smooth flake mask with clean Gaussian falloff for foreground bokeh (zero hard circle cutoff!)
        float edgeSoft = lerp(0.14f, 0.55f, blur);
        float flakeMask = 1.0f - smoothstep(1.0f - edgeSoft, 1.0f + edgeSoft, dFlake);
        float gaussEnv  = exp(-r * r * lerp(3.6f, 4.8f, blur)) * saturate((1.0f - r) * 3.5f);

        // 1. Opaque black charred coal body (premultiplied dark ash: darkens crimson background!)
        float blackCrustMask = flakeMask * (1.0f - openFire * 0.85f) * (1.0f - blur * 0.55f);
        float outAlpha = saturate(blackCrustMask * 0.92f * alpha);
        float3 ashRgb  = float3(0.015f, 0.005f, 0.004f) * outAlpha;

        // 2. Pure additive glowing fire inside open cracks + soft Gaussian fiery bloom halo
        float localTemp = saturate(heat * (0.45f + 0.85f * openFire) * (1.15f - 0.35f * saturate(dFlake)));
        float3 fireRgb  = BlackbodyEmber(localTemp) * flakeMask * openFire * (1.05f + 1.15f * heat);
        float3 haloRgb  = BlackbodyEmber(heat * 0.62f) * gaussEnv * (0.38f + 0.65f * heat) * (0.35f + 0.65f * openFire);

        // In Premultiplied PSO (ONE + INV_SRC_ALPHA):
        //   - Black crust has outAlpha > 0 -> genuinely occludes background with black coal!
        //   - Fire & bloom add pure light on top (outAlpha stays 0 outside crust -> pure additive bloom!)
        float3 finalRgb = ashRgb + (fireRgb + haloRgb) * alpha;
        return float4(finalRgb, outAlpha);
    }

    float4 s = g_Tex.Sample(g_Sampler, input.uv);
    if (input.mode == 1u) {
        return float4(input.color.rgb, input.color.a * s.r);
    }
    return s * input.color;
}
