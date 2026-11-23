// ACZero Enhancer — 2D UI Vertex Shader (SM 6.0)
// Used by ui/acz_ui_renderer.h for all custom menus and future HUD rendering.

cbuffer UiConstants : register(b0) {
    float2 g_InvScreenSize; // (2.0 / width, 2.0 / height)
    float2 g_Reserved;
};

struct VSInput {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
    uint   mode  : TEXCOORD1;
};

struct VSOutput {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint mode : TEXCOORD1;
};

VSOutput main(VSInput input) {
    VSOutput o;
    o.pos = float4(
        input.pos.x * g_InvScreenSize.x - 1.0f,
        1.0f - input.pos.y * g_InvScreenSize.y,
        0.0f,
        1.0f
    );
    o.uv    = input.uv;
    o.color = input.color;
    o.mode  = input.mode;
    return o;
}
