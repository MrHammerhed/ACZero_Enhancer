// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port) — Radial Fog Shader Override
// Shader: DrawACM_v.hlsl (vs_6_0)
// 1:1 replica of extracted/a/shader/DrawACM_v.cso with spherical (radial) fog
// computed from projection-invariant P00/P11 extracted from M0 + viewport.
// ============================================================================

struct SrtData {
    float4 m0[4];        // +0x000: ScreenProj * View
    float4 m1[4];        // +0x040: Clip * View
    float4 lightDir[4];  // +0x080: Light direction matrix
    float4 lightCol[4];  // +0x0C0: Light color matrix (row 3 = ambient)
    float4 worldMat[4];  // +0x100: Model-to-World matrix
    float4 camPos;       // +0x140: Camera world position
    float4 fogParams;    // +0x150: .x = fogA, .y = fogB
    float4 lightFlags;   // +0x160: .x = enableLight, .y = linearDiffuse, .z = zScale, .w = envReflect
    float4 fogFlags;     // +0x170: .w = fogEnable
    float4 extraFade;    // +0x180: .z = fadeFar, .w = fadeInvSpan
    float4 viewport;     // +0x190: .x = vp_w, .y = vp_h, .z = vp_cx, .w = vp_cy
};

cbuffer CbFlags : register(b10) {
    int   g_Flags;
    float g_Pad;
};

StructuredBuffer<SrtData> g_Srt : register(t10);

struct VSIn {
    float4 pos    : ATTRIBUTE0;
    float4 normal : ATTRIBUTE1;
    float4 color  : ATTRIBUTE2;
    float4 uv     : ATTRIBUTE3;
};

struct VSOut {
    float4 pos   : SV_Position;
    float4 color : COLOR0;
    float3 uvFog : TEXCOORD0;
};

VSOut main(VSIn inp) {
    SrtData s = g_Srt[0];

    // M0 * WorldMat (row-major matrix multiplication matching DrawACM_v.ll)
    float4 mvp[4];
    [unroll]
    for (int r = 0; r < 4; ++r) {
        mvp[r] = s.worldMat[r].x * s.m0[0]
               + s.worldMat[r].y * s.m0[1]
               + s.worldMat[r].z * s.m0[2]
               + s.worldMat[r].w * s.m0[3];
    }

    float3 p = inp.pos.xyz;
    float sx = p.x * mvp[0].x + p.y * mvp[1].x + p.z * mvp[2].x + mvp[3].x;
    float sy = p.x * mvp[0].y + p.y * mvp[1].y + p.z * mvp[2].y + mvp[3].y;
    float sz = p.x * mvp[0].z + p.y * mvp[1].z + p.z * mvp[2].z + mvp[3].z;
    float sw = p.x * mvp[0].w + p.y * mvp[1].w + p.z * mvp[2].w + mvp[3].w;

    float px = sx / sw;
    float py = sy / sw;
    float pz = sz / sw;

    float ndcX = ((px - s.viewport.z) * 2.0f) / s.viewport.x;
    float ndcY = ((py - s.viewport.w) * 2.0f) / s.viewport.y;

    bool isZ16 = (s.lightFlags.z == 16.0f);
    float zScale = isZ16 ? (1.0f / 16777215.0f) : (s.lightFlags.z * (1.0f / 16777215.0f));
    float ndcZ = pz * zScale;
    if (isZ16 && sw > 0.0f) {
        ndcZ = clamp(ndcZ, -1.0f, 1.0f);
    }

    float4 outPos = float4(ndcX * sw, ndcY * sw, ndcZ * sw, sw);

    // Radial (spherical) fog using scale-invariant P00/P11 from M0 + viewport
    float fog = 1.0f;
    if (s.fogFlags.w != 0.0f && ((g_Flags & 4) == 0)) {
        float3 col0 = float3(s.m0[0].x, s.m0[1].x, s.m0[2].x);
        float3 col1 = float3(s.m0[0].y, s.m0[1].y, s.m0[2].y);
        float3 col3 = float3(s.m0[0].w, s.m0[1].w, s.m0[2].w);
        float3 ux   = (col0 - s.viewport.z * col3) * (2.0f / s.viewport.x);
        float3 uy   = (col1 - s.viewport.w * col3) * (2.0f / s.viewport.y);
        float  w2   = dot(col3, col3);
        float  invP00Sq = w2 / max(dot(ux, ux), 1e-6f);
        float  invP11Sq = w2 / max(dot(uy, uy), 1e-6f);
        float  radDist  = sw * sqrt(1.0f + (ndcX * ndcX) * invP00Sq + (ndcY * ndcY) * invP11Sq);
        fog = clamp((radDist * s.fogParams.y + s.fogParams.x) * (1.0f / 255.0f), 0.0f, 1.0f);
    }

    // Vertex color unpacking (flag bit 1: int32 bits reinterpreted as float)
    bool isIntPacked = ((g_Flags & 2) != 0);
    float4 vcol = inp.color;
    if (isIntPacked) {
        vcol = float4(asint(inp.color.x), asint(inp.color.y), asint(inp.color.z), asint(inp.color.w));
    }
    vcol *= 0.0078125f;

    float4 litCol = vcol;
    if (s.lightFlags.x != 0.0f) {
        // LightDirMat * WorldMat (3x3)
        float3 ld0 = s.worldMat[0].x * s.lightDir[0].xyz + s.worldMat[0].y * s.lightDir[1].xyz + s.worldMat[0].z * s.lightDir[2].xyz + s.worldMat[0].w * s.lightDir[3].xyz;
        float3 ld1 = s.worldMat[1].x * s.lightDir[0].xyz + s.worldMat[1].y * s.lightDir[1].xyz + s.worldMat[1].z * s.lightDir[2].xyz + s.worldMat[1].w * s.lightDir[3].xyz;
        float3 ld2 = s.worldMat[2].x * s.lightDir[0].xyz + s.worldMat[2].y * s.lightDir[1].xyz + s.worldMat[2].z * s.lightDir[2].xyz + s.worldMat[2].w * s.lightDir[3].xyz;

        float3 n = inp.normal.xyz;
        if (isIntPacked) {
            n = float3(asint(inp.normal.x), asint(inp.normal.y), asint(inp.normal.z)) * (1.0f / 4096.0f);
        }

        float3 ndl = float3(
            n.x * ld0.x + n.y * ld1.x + n.z * ld2.x,
            n.x * ld0.y + n.y * ld1.y + n.z * ld2.y,
            n.x * ld0.z + n.y * ld1.z + n.z * ld2.z
        );
        if (s.lightFlags.y == 0.0f) {
            ndl = 2.0f * ndl * ndl * ndl;
        }
        ndl = max(ndl, 0.0f);

        float3 lrgb = ndl.x * s.lightCol[0].xyz + ndl.y * s.lightCol[1].xyz + ndl.z * s.lightCol[2].xyz + s.lightCol[3].xyz;
        lrgb = max(lrgb, 0.0f) * vcol.rgb;
        litCol = float4(min(lrgb, 1.9921875f), 1.0f);
    }

    float2 outUv = inp.uv.xy;
    float4 finalCol = litCol;

    // Environment reflection mapping (flag bit 0)
    if ((g_Flags & 1) != 0) {
        float3 n = inp.normal.xyz;
        if (isIntPacked) {
            n = float3(asint(inp.normal.x), asint(inp.normal.y), asint(inp.normal.z)) * (1.0f / 4096.0f);
        }
        float3 wn = n.x * s.worldMat[0].xyz + n.y * s.worldMat[1].xyz + n.z * s.worldMat[2].xyz;
        float3 rv = wn;
        if (s.lightFlags.w != 0.0f) {
            float3 wp = p.x * s.worldMat[0].xyz + p.y * s.worldMat[1].xyz + p.z * s.worldMat[2].xyz + (s.worldMat[3].xyz - s.camPos.xyz);
            float3 vn = wp * rsqrt(dot(wp, wp));
            rv = vn - 2.0f * dot(vn, wn) * wn;
        }
        float envI = clamp(rv.y * 10.0f, 0.0f, 1.0f);
        finalCol = float4(envI, envI, envI, 0.9921875f);
        outUv = clamp((float2(rv.x, rv.z) + 1.0f) * 0.5f, 0.0f, 1.0f);
    }

    // Distance detail fade override (flag bit 3)
    if ((g_Flags & 8) != 0) {
        float w0 = dot(s.worldMat[0], float4(s.m1[0].w, s.m1[1].w, s.m1[2].w, s.m1[3].w));
        float w1 = dot(s.worldMat[1], float4(s.m1[0].w, s.m1[1].w, s.m1[2].w, s.m1[3].w));
        float w2 = dot(s.worldMat[2], float4(s.m1[0].w, s.m1[1].w, s.m1[2].w, s.m1[3].w));
        float w3 = dot(s.worldMat[3], float4(s.m1[0].w, s.m1[1].w, s.m1[2].w, s.m1[3].w));
        float vz = p.x * w0 + p.y * w1 + p.z * w2 + w3;
        float f  = clamp((s.extraFade.z - vz) * s.extraFade.w, 0.0f, 1.0f);
        finalCol = float4(f, f, f, 1.0f);
        outUv   *= 2.0f;
    }

    VSOut o;
    o.pos   = outPos;
    o.color = finalCol;
    o.uvFog = float3(outUv.x, outUv.y, fog);
    return o;
}
