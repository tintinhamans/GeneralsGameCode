// Ronin @feature 18/11/2026 DX9: THE GLOW ROUND A LAMP, its blur (R3DTaa buildGlow), as the resolve's local-contrast surround but in
// colour: c0.x = 0: the source's 3D view (c30) at a quarter of its size, 4x4 texels each; 1 / 2 = a Gaussian across / down, sigma
// 5 texels x c0.w. c0.yz = a texel of what is read.
//
// Compile with: fxc /T ps_3_0 /Fo GlowBlur.pso GlowBlur_ps.hlsl   (pairs with ScreenQuad.vso)

sampler2D g_Src  : register(s0);
float4    g_Pass : register(c0);
float4    g_Rect : register(c30);   // pass 0: the source's 3D view, 0..1 of it (c30: the resolve never reads it)

static const float OFS[8] = { 1.4850f, 3.4651f, 5.4452f, 7.4256f, 9.4061f, 11.3870f, 13.3682f, 15.3498f };
static const float WTS[8] = { 0.152007f, 0.124701f, 0.087314f, 0.052179f, 0.026613f, 0.011585f, 0.004304f, 0.001364f };

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    if (g_Pass.x < 0.5f)
    {
        float2 px = g_Pass.yz;
        float2 at = g_Rect.xy + uv * g_Rect.zw;
        float2 lo = g_Rect.xy + 0.5f * px;
        float2 hi = g_Rect.xy + g_Rect.zw - 0.5f * px;
        float3 c  = tex2Dlod(g_Src, float4(clamp(at + float2(-1.0f, -1.0f) * px, lo, hi), 0.0f, 0.0f)).rgb +
                    tex2Dlod(g_Src, float4(clamp(at + float2( 1.0f, -1.0f) * px, lo, hi), 0.0f, 0.0f)).rgb +
                    tex2Dlod(g_Src, float4(clamp(at + float2(-1.0f,  1.0f) * px, lo, hi), 0.0f, 0.0f)).rgb +
                    tex2Dlod(g_Src, float4(clamp(at + float2( 1.0f,  1.0f) * px, lo, hi), 0.0f, 0.0f)).rgb;
        return float4(c * 0.25f, 1.0f);
    }
    float2 dir = ((g_Pass.x < 1.5f) ? float2(g_Pass.y, 0.0f) : float2(0.0f, g_Pass.z)) * g_Pass.w;
    float3 sum = 0.079864f * tex2Dlod(g_Src, float4(uv, 0.0f, 0.0f)).rgb;
    [unroll]
    for (int k = 0; k < 8; ++k)
        sum += WTS[k] * (tex2Dlod(g_Src, float4(uv + dir * OFS[k], 0.0f, 0.0f)).rgb + tex2Dlod(g_Src, float4(uv - dir * OFS[k], 0.0f, 0.0f)).rgb);
    return float4(sum, 1.0f);
}
