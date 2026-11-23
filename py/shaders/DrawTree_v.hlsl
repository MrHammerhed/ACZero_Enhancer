// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port) — Radial Fog Shader Override
// Shader: DrawTree_v.hlsl (vs_6_0)
// 1:1 replica of extracted/a/shader/DrawTree_v.cso with spherical (radial) fog
// using the already-computed 3D camera distance instead of planar clip_w.
// ============================================================================

struct SrtData {
    float4 m0[4];       // +0x000: ScreenProj * View
    float4 m1[4];       // +0x040: Clip * View
    float4 scaleOff;    // +0x080: .z = billboard scale, .w = z-bias
    float4 fadeDist;    // +0x090: .x = maxDist, .y = fadeStart, .w = cullDist
    float4 camPos;      // +0x0A0: .xyz = camera world position
    float4 fogParams;   // +0x0B0: .x = fogA, .y = fogB
    float4 offCenter[3];// +0x0C0: tree center offsets (3 types)
    float4 uvRange[3];  // +0x0F0: .x = u0, .y = u1 (3 types)
    float4 halfSize[3]; // +0x120: billboard half-extents (3 types)
    float4 viewport;    // +0x150: .x = vp_w, .y = vp_h, .z = vp_cx, .w = vp_cy
};

struct SrtDataB {
    float4 pos;
    int    type;
};

StructuredBuffer<SrtData>  g_Srt  : register(t10);
StructuredBuffer<SrtDataB> g_Inst : register(t12);

struct VSOut {
    float4 pos  : SV_Position;
    float4 uv   : TEXCOORD0;
    nointerpolation int2 meta : TEXCOORD1;
};

VSOut main(uint vid : SV_VertexID) {
    SrtData  s = g_Srt[0];
    uint instIdx = vid / 6u;
    uint rem     = vid % 6u;
    uint corner  = (rem > 2u) ? (6u - rem) : rem;

    SrtDataB inst = g_Inst[instIdx];
    float4 p = inst.pos;
    int t = inst.type;

    // Clip frustum Z check using M1 columns 2 and 3
    float cz = p.x * s.m1[0].z + p.y * s.m1[1].z + p.z * s.m1[2].z + p.w * s.m1[3].z;
    float cw = p.x * s.m1[0].w + p.y * s.m1[1].w + p.z * s.m1[2].w + p.w * s.m1[3].w;
    float acw = abs(cw);
    int clipped = (cz < -acw || acw < cz) ? 1 : 0;

    // True 3D radial distance from camera
    float3 d = p.xyz - s.camPos.xyz;
    float dist3d = sqrt(d.x * d.x + d.y * d.y + d.z * d.z);

    float fade = 1.0f - ((dist3d - s.fadeDist.y) * 0.9921875f) / (s.fadeDist.x - s.fadeDist.y);
    int fullOpaque = 1;
    int culledFlag = clipped;
    float outAlpha = 1.0f;
    if (fade < 0.9921875f) {
        fullOpaque = 0;
        outAlpha = fade;
        if (!(fade > 0.0f)) {
            culledFlag = 1;
        }
    }
    bool outOfRange = (s.fadeDist.x < dist3d) || (s.fadeDist.w < dist3d);

    // Screen-space projection via M0
    float sx = p.x * s.m0[0].x + p.y * s.m0[1].x + p.z * s.m0[2].x + p.w * s.m0[3].x;
    float sy = p.x * s.m0[0].y + p.y * s.m0[1].y + p.z * s.m0[2].y + p.w * s.m0[3].y;
    float sz = p.x * s.m0[0].z + p.y * s.m0[1].z + p.z * s.m0[2].z + p.w * s.m0[3].z;
    float sw = p.x * s.m0[0].w + p.y * s.m0[1].w + p.z * s.m0[2].w + p.w * s.m0[3].w;

    float invW = 1.0f / sw;
    float szBias = sz + s.scaleOff.w;

    float2 ctr = float2(sx, sy) + s.offCenter[t].xy * s.scaleOff.z;
    float2 hs  = s.halfSize[t].xy * s.scaleOff.z;

    float2 qpos;
    float2 quv;
    if (corner == 0u) {
        qpos = ctr - hs;
        quv  = float2(s.uvRange[t].x, 0.0f);
    } else if (corner == 1u) {
        qpos = float2(ctr.x + hs.x, ctr.y - hs.y);
        quv  = float2(s.uvRange[t].y, 0.0f);
    } else if (corner == 2u) {
        qpos = float2(ctr.x - hs.x, ctr.y + hs.y);
        quv  = float2(s.uvRange[t].x, 1.0f);
    } else {
        qpos = ctr + hs;
        quv  = float2(s.uvRange[t].y, 1.0f);
    }

    float ndcX = ((qpos.x * invW) - s.viewport.z) * 2.0f / s.viewport.x;
    float ndcY = ((qpos.y * invW) - s.viewport.w) * 2.0f / s.viewport.y;
    float outX = ndcX * sw;
    float outY = ndcY * sw;
    float outZ = (szBias * (1.0f / 16777215.0f)) * sw * invW;

    // Radial (spherical) fog using true 3D distance dist3d instead of planar sw
    float fog = clamp((dist3d * s.fogParams.y + s.fogParams.x) * (1.0f / 255.0f), 0.0f, 1.0f);

    bool discardVert = outOfRange || (culledFlag != 0);

    VSOut o;
    o.pos  = discardVert ? float4(-2.0f, -2.0f, -2.0f, -2.0f) : float4(outX, outY, outZ, sw);
    o.uv   = discardVert ? float4(0.0f, 0.0f, 0.0f, 0.0f) : float4(quv.x, quv.y, outAlpha, fog);
    o.meta = int2(discardVert ? 0 : t, fullOpaque);
    return o;
}
