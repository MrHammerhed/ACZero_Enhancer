// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port) — Radial Fog Shader Override
// Shader: DrawMesh_g.hlsl (gs_6_0)
// 1:1 replica of extracted/a/shader/DrawMesh_g.cso with spherical (radial) fog
// computed from projection-invariant P00/P11 extracted from M0 + viewport.
// ============================================================================

struct SrtData {
    float4 m0[4];        // +0x000: ScreenProj * View
    float4 m1[4];        // +0x040: Clip * View
    float4 fogParams;    // +0x080: .x = fogA, .y = fogB
    float4 fogColor;     // +0x090: unused in GS (used in PS)
    float4 detailFade;   // +0x0A0: .x = morphStart, .y = morphEnd, .z = t1Far, .w = t1InvSpan
    float4 secFade;      // +0x0B0: .xy = t2 uv offset, .z = t2Far, .w = t2InvSpan
    float4 cloudShadow;  // +0x0C0: .x = originX, .z = originZ, .w = invSpan
    float4 basePos[10];  // +0x0D0: local tile vertex offsets
    float4 baseUv[10];   // +0x170: local tile UV coordinates
};

struct SrtDataB {
    int4   vcol[10];     // +0x000: 10 vertex RGBA colors (int32, 0..128)
    float2 tileOrigin;   // +0x0A0: world X, Z origin of sub-tile
    float  heights[10];  // +0x0A8: 10 vertex heights (scaled by 80.0f)
    int    vertCount;    // +0x0D0: 4 (low-LOD quad) or 10 (high-LOD fan)
    int    pad1;         // +0x0D4
    int    pad2;         // +0x0D8
    int4   neighbors;    // +0x0DC
};

cbuffer CbViewportSize : register(b10) {
    float2 g_VpSize;
};

cbuffer CbViewportCenter : register(b11) {
    float2 g_VpCenter;
};

StructuredBuffer<SrtData>  g_Srt  : register(t10);
StructuredBuffer<SrtDataB> g_Inst : register(t11);

struct GSIn {
    uint passIdx : PASSINDEX0;
};

struct GSOut {
    float4 pos   : SV_Position;
    float4 color : COLOR0;
    float4 uv0   : TEXCOORD0;
    float3 uv1   : TEXCOORD1;
    nointerpolation int instId : TEXCOORD3;
    float4 uv2   : TEXCOORD2;
};

GSOut BuildMeshVertex(
    SrtData s,
    SrtDataB inst,
    uint instId,
    int vertSlot,
    int heightIdx,
    int colIdx,
    bool isHighLod,
    float invP00Sq,
    float invP11Sq
) {
    float4 bp = s.basePos[vertSlot];
    float wx = inst.tileOrigin.x + bp.x;
    float wz = inst.tileOrigin.y + bp.z;
    float wy = inst.heights[heightIdx] * 80.0f;

    float viewZ = wx * s.m1[0].w + wy * s.m1[1].w + wz * s.m1[2].w + bp.w * s.m1[3].w;

    if (isHighLod && ((vertSlot & 1) == 0)) {
        float morphDelta = viewZ - s.detailFade.x;
        if (morphDelta > 0.0f) {
            int prevIdx = (vertSlot < 1) ? 5 : (vertSlot - 1);
            int nextIdx = vertSlot + 1;
            float avgH = (inst.heights[prevIdx] + inst.heights[nextIdx]) * 40.0f;
            if (s.detailFade.y > viewZ) {
                float t = morphDelta / (s.detailFade.y - s.detailFade.x);
                wy = wy + t * (avgH - wy);
            } else {
                wy = avgH;
            }
        }
    }

    float sx = wx * s.m0[0].x + wy * s.m0[1].x + wz * s.m0[2].x + bp.w * s.m0[3].x;
    float sy = wx * s.m0[0].y + wy * s.m0[1].y + wz * s.m0[2].y + bp.w * s.m0[3].y;
    float sz = wx * s.m0[0].z + wy * s.m0[1].z + wz * s.m0[2].z + bp.w * s.m0[3].z;
    float sw = wx * s.m0[0].w + wy * s.m0[1].w + wz * s.m0[2].w + bp.w * s.m0[3].w;

    float px = sx / sw;
    float py = sy / sw;
    float pz = sz / sw;

    float ndcX = ((px - g_VpCenter.x) * 2.0f) / g_VpSize.x;
    float ndcY = ((py - g_VpCenter.y) * 2.0f) / g_VpSize.y;

    float outX = ndcX * sw;
    float outY = ndcY * sw;
    float outZ = (sw * (1.0f / 16777215.0f)) * pz;

    // Radial (spherical) fog distance using exact P00/P11 from M0 + viewport
    float radScale = sqrt(1.0f + (ndcX * ndcX) * invP00Sq + (ndcY * ndcY) * invP11Sq);
    float radDist  = sw * radScale;
    float fog = clamp((radDist * s.fogParams.y + s.fogParams.x) * (1.0f / 255.0f), 0.0f, 1.0f);

    float2 uvBase = s.baseUv[vertSlot].xy;
    float4 col    = float4(inst.vcol[colIdx]) * 0.0078125f;

    float t2Fade = clamp((s.secFade.z - viewZ) * s.secFade.w, 0.0f, 1.0f);
    float2 uv2Scaled = uvBase * 2.0f;
    float2 uv1Pos    = uv2Scaled + s.secFade.xy;
    float t1Fade = clamp((s.detailFade.z - viewZ) * s.detailFade.w, 0.0f, 1.0f);

    float2 cloudUv = float2(wx - s.cloudShadow.x, wz - s.cloudShadow.z) * s.cloudShadow.w;

    GSOut o;
    o.pos    = float4(outX, outY, outZ, sw);
    o.color  = col;
    o.uv0    = float4(uvBase.x, uvBase.y, cloudUv.x, cloudUv.y);
    o.uv1    = float3(uv1Pos.x, uv1Pos.y, t2Fade);
    o.instId = (int)instId;
    o.uv2    = float4(uv2Scaled.x, uv2Scaled.y, fog, t1Fade);
    return o;
}

[maxvertexcount(24)]
void main(point GSIn input[1], inout TriangleStream<GSOut> triStream) {
    SrtData  s      = g_Srt[0];
    uint     instId = input[0].passIdx;
    SrtDataB inst   = g_Inst[instId];

    // Extract scale-invariant 1/P00^2 and 1/P11^2 from M0 rotation columns + viewport
    float3 col0 = float3(s.m0[0].x, s.m0[1].x, s.m0[2].x);
    float3 col1 = float3(s.m0[0].y, s.m0[1].y, s.m0[2].y);
    float3 col3 = float3(s.m0[0].w, s.m0[1].w, s.m0[2].w);
    float3 ux   = (col0 - g_VpCenter.x * col3) * (2.0f / g_VpSize.x);
    float3 uy   = (col1 - g_VpCenter.y * col3) * (2.0f / g_VpSize.y);
    float  w2   = dot(col3, col3);
    float  invP00Sq = w2 / max(dot(ux, ux), 1e-6f);
    float  invP11Sq = w2 / max(dot(uy, uy), 1e-6f);

    int  vertCount = inst.vertCount;
    bool isLowLod  = (vertCount == 4);
    bool isHighLod = (vertCount == 10);
    int  step      = isLowLod ? 2 : 1;
    int  offset    = isLowLod ? 1 : 0;

    if (vertCount <= 2) {
        return;
    }

    GSOut v0 = BuildMeshVertex(s, inst, instId, offset, 0, 0, isHighLod, invP00Sq, invP11Sq);
    GSOut vPrev = BuildMeshVertex(s, inst, instId, offset + step, 1, 1, isHighLod, invP00Sq, invP11Sq);

    for (int i = 2; i < vertCount; ++i) {
        int slot = i * step + offset;
        GSOut vCur = BuildMeshVertex(s, inst, instId, slot, i, i, isHighLod, invP00Sq, invP11Sq);

        triStream.Append(v0);
        triStream.Append(vPrev);
        triStream.Append(vCur);
        triStream.RestartStrip();

        vPrev = vCur;
    }
}
