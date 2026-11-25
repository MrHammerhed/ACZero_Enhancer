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

float2 Hash22(float2 p) {
    float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.xx + p3.yz) * p3.zy) * 2.0f - 1.0f;
}

// Smooth 2D Perlin Gradient Noise (zero blocky ValueNoise grid artifacts!)
float GradNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

    float2 g00 = normalize(Hash22(i + float2(0.0f, 0.0f)) + 1e-4f);
    float2 g10 = normalize(Hash22(i + float2(1.0f, 0.0f)) + 1e-4f);
    float2 g01 = normalize(Hash22(i + float2(0.0f, 1.0f)) + 1e-4f);
    float2 g11 = normalize(Hash22(i + float2(1.0f, 1.0f)) + 1e-4f);

    float n00 = dot(g00, f - float2(0.0f, 0.0f));
    float n10 = dot(g10, f - float2(1.0f, 0.0f));
    float n01 = dot(g01, f - float2(0.0f, 1.0f));
    float n11 = dot(g11, f - float2(1.0f, 1.0f));

    return lerp(lerp(n00, n10, u.x), lerp(n01, n11, u.x), u.y) * 0.5f + 0.5f;
}

float Fbm3(float2 p) {
    float v = 0.0f;
    float amp = 0.5f;
    float2x2 rot = float2x2(0.80f, 0.60f, -0.60f, 0.80f);
    [unroll]
    for (int i = 0; i < 3; ++i) {
        v += amp * GradNoise(p);
        p = mul(rot, p) * 2.06f + float2(17.3f, 9.2f);
        amp *= 0.5f;
    }
    return v;
}

float Fbm4(float2 p) {
    float v = 0.0f;
    float amp = 0.5f;
    float2x2 rot = float2x2(0.80f, 0.60f, -0.60f, 0.80f);
    [unroll]
    for (int i = 0; i < 4; ++i) {
        v += amp * GradNoise(p);
        p = mul(rot, p) * 2.06f + float2(17.3f, 9.2f);
        amp *= 0.5f;
    }
    return v;
}

float Fbm5(float2 p) {
    float v = 0.0f;
    float amp = 0.5f;
    float2x2 rot = float2x2(0.80f, 0.60f, -0.60f, 0.80f);
    [unroll]
    for (int i = 0; i < 5; ++i) {
        v += amp * GradNoise(p);
        p = mul(rot, p) * 2.04f + float2(13.7f, 21.1f);
        amp *= 0.5f;
    }
    return v;
}

float Fbm6(float2 p) {
    float v = 0.0f;
    float amp = 0.5f;
    float2x2 rot = float2x2(0.80f, 0.60f, -0.60f, 0.80f);
    [unroll]
    for (int i = 0; i < 6; ++i) {
        v += amp * GradNoise(p);
        p = mul(rot, p) * 2.04f + float2(13.7f, 21.4f);
        amp *= 0.5f;
    }
    return v;
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

    if (input.mode == 3u) {
        // 3 separate soft-blurred procedural ACZ tactical grids (uv in virtual 1080p coordinates)
        float2 p = input.uv;

        // Layer 1: Major square grid (step 120 virtual px, soft Gaussian-blurred line)
        float2 m1 = abs(frac(p / 120.0f + 0.5f) - 0.5f) * 120.0f;
        float  gMajor = max(exp(-(m1.x * m1.x) / 9.2f), exp(-(m1.y * m1.y) / 9.2f)) * input.color.r;

        // Layer 2: Minor 6x6 square sub-grid (step 20 virtual px, soft Gaussian-blurred line)
        float2 m2 = abs(frac(p / 20.0f + 0.5f) - 0.5f) * 20.0f;
        float  gMinor = max(exp(-(m2.x * m2.x) / 4.3f), exp(-(m2.y * m2.y) / 4.3f)) * input.color.g;

        // Layer 3: Soft blurred 45-degree '/' diagonal hatch (step 7 virtual px, smooth wave)
        float  diagWave = 0.5f + 0.5f * cos((p.x + p.y) * (6.2831853f / 7.0f));
        float  gDiag = pow(diagWave, 2.2f) * input.color.b;

        // Combine softly into dark crimson-black tactical screen lines
        float a = saturate(max(gMajor, max(gMinor, gDiag)) + gDiag * 0.25f) * input.color.a;
        return float4(0.03f, 0.002f, 0.001f, a);
    }

    if (input.mode == 4u) {
        // Cinematic Procedural Belkan Storm Clouds (2D Perlin Gradient + Double Domain Warp)
        // + Vanilla-style Forward-Zooming Mist Clouds (Title.swa Dict[17])
        // input.uv      = aspect-corrected centered coords: x in [-aspect/2..aspect/2], y in [-0.5..0.5]
        // input.color.r = elapsed time (seconds)
        // input.color.g = cloudIntro [0..1]
        // input.color.b = logoFlash  [0..1]
        // input.color.a = masterFade [0..1]
        float2 p          = input.uv;
        float  t          = input.color.r;
        float  cloudIntro = saturate(input.color.g);
        float  logoFlash  = saturate(input.color.b);
        float  masterFade = saturate(input.color.a);

        // --------------------------------------------------------------------
        // 1. Endless camera fly-through into deep, high-contrast dark crimson clouds
        //    Double-warped 6-octave gradient FBM with wide dark voids (Op Us-1 / entry_005)
        // --------------------------------------------------------------------
        float redCloud = 0.0f;
        float hotCore  = 0.0f;
        float wSum     = 0.0f;

        [unroll]
        for (int i = 0; i < 3; ++i) {
            float fi = (float)i;
            float ph = frac(t * 0.036f + fi * 0.3333333f); // ~27.8s monumental seamless zoom octave
            float zScale = exp(-ph * 1.18f);               // continuous ~3.25x forward fly-through
            float env = sin(ph * 3.14159265f);
            env = env * env;

            // Slightly stretched horizontally for sweeping atmospheric cloud banks (optimized single-warp 5-oct)
            float2 cp = float2(p.x * 1.90f, p.y * 2.65f) * zScale + float2(fi * 7.1f - 1.8f, fi * 4.7f + 1.2f);
            float2 q = float2(
                Fbm3(cp + float2(0.0f, 0.0f) + t * 0.008f),
                Fbm3(cp + float2(5.2f, 1.3f) - t * 0.006f)
            );
            float n = Fbm5(cp + 1.85f * (q - 0.5f) + float2(1.7f, 9.2f) + float2(t * 0.010f, -t * 0.007f));

            // Deep volumetric crimson cloud banks with dark smoky chasms between glowing ridges
            float wisps = smoothstep(0.32f, 0.78f, n);
            wisps = pow(wisps, 1.32f);
            float core  = smoothstep(0.56f, 0.89f, n);

            // Depth-weighted parallax: nearer phase layers feel slightly richer & more dramatic
            float depthWeight = 0.78f + 0.44f * ph;
            redCloud += wisps * env * depthWeight;
            hotCore  += core  * env * depthWeight;
            wSum     += env;
        }
        redCloud /= max(0.001f, wSum);
        hotCore  /= max(0.001f, wSum);

        // Deep crimson atmospheric cavity with fiery underglow rising from lower-left/center
        float lowerDepth = saturate(1.0f - length((p - float2(-0.14f, 0.32f)) * float2(0.78f, 1.18f)));
        float centerGlow = saturate(1.0f - length(p * float2(0.82f, 1.28f)));

        float3 colVoid   = float3(0.018f, 0.0010f, 0.0025f);
        float3 colDeep   = float3(0.300f, 0.0090f, 0.0150f);
        float3 colMid    = float3(0.760f, 0.0320f, 0.0220f);
        float3 colEmber  = float3(1.000f, 0.2100f, 0.0300f);

        float density = saturate(redCloud * (0.74f + 0.56f * lowerDepth));
        float3 bgCol = lerp(colVoid, colDeep, smoothstep(0.05f, 0.40f, density));
        bgCol = lerp(bgCol, colMid, smoothstep(0.32f, 0.74f, density));
        bgCol += colEmber * hotCore * (0.24f + 0.52f * lowerDepth);

        // Warm atmospheric reflection from the logo lock-in flash
        bgCol += float3(0.76f, 0.34f, 0.08f) * logoFlash * (0.18f + 0.46f * centerGlow) * (0.30f + 0.70f * redCloud);

        // --------------------------------------------------------------------
        // 2. Vanilla-style forward-zooming light clouds (Title.swa Dict[17])
        //    Textured warm pale-gold smoky cloud veils gliding toward camera
        // --------------------------------------------------------------------
        float mistAccum  = 0.0f;
        float mistDetail = 0.0f;
        [unroll]
        for (int j = 0; j < 3; ++j) {
            float fj = (float)j;
            float ph = frac(t * (1.0f / 6.2f) + fj * 0.3333333f);
            float zScale = exp(-ph * 0.80f); // ~2.22x forward zoom from center
            float env = smoothstep(0.0f, 0.34f, ph) * (1.0f - smoothstep(0.66f, 1.0f, ph));

            float2 mp = float2(p.x * 2.15f, p.y * 2.75f) * zScale + float2(fj * 11.3f + 2.1f, fj * 7.7f - 3.4f);
            float2 mw = float2(
                Fbm3(mp * 0.85f + float2(3.1f, 1.7f)),
                Fbm3(mp * 0.85f + float2(-2.4f, 5.2f))
            );
            float mn   = Fbm4(mp + (mw - 0.5f) * 1.65f);
            float fine = Fbm3(mp * 2.85f + (mw - 0.5f) * 2.2f);

            // Crisp, billowy fibrous smoke silhouette with internal cloud texture
            float cloudMask = smoothstep(0.36f, 0.78f, mn) * (0.62f + 0.58f * smoothstep(0.28f, 0.76f, fine));
            float ridge     = smoothstep(0.54f, 0.86f, mn * 0.72f + fine * 0.38f);
            mistAccum  += cloudMask * env;
            mistDetail += ridge * env;
        }

        // Original Title.swa Dict[17] warm yellowish-gold textured smoke clouds
        float3 mistBase  = float3(0.96f, 0.76f, 0.42f);
        float3 mistHi    = float3(1.00f, 0.90f, 0.64f);
        bgCol += (mistBase * mistAccum * 0.225f + mistHi * mistDetail * 0.105f) * cloudIntro;

        // Cinematic depth vignette pulling the viewer into the crimson storm
        float vig = saturate(1.0f - dot(p * float2(0.58f, 0.96f), p * float2(0.58f, 0.96f)) * 0.54f);
        bgCol *= (0.54f + 0.46f * vig);

        return float4(bgCol * masterFade, 1.0f);
    }

    if (input.mode == 5u) {
        // Even wide rounded-capsule optical bloom across the ENTIRE logo width (zero center hotspot!)
        // input.uv in [0..1]x[0..1], centered at (0.5, 0.5)
        float2 p = input.uv * 2.0f - 1.0f;
        float2 q = float2(max(0.0f, abs(p.x) - 0.52f) / 0.48f, p.y);
        float d2 = dot(q, q);
        if (d2 >= 1.0f) discard;

        float edgeFade = saturate((1.0f - sqrt(d2)) * 2.0f);
        float glow     = exp(-d2 * 3.1f) * edgeFade;
        return float4(input.color.rgb, glow * input.color.a);
    }

    float4 s = g_Tex.Sample(g_Sampler, input.uv);
    if (input.mode == 1u) {
        return float4(input.color.rgb, input.color.a * s.r);
    }
    return s * input.color;
}
