// Ronin @feature 18/11/2026 DX9: THE GLOW ROUND A LAMP, its source (R3DTaa buildGlow): a lit lamp's art (R3DLocalLights' G2ART copy)
// drawn again in its lamp's colour where nothing nearer hides it - the scene depth, tested as TaaDecalMark_ps tests the ground.
//
// Compile with: fxc /T ps_3_0 /Fo GlowArt.pso GlowArt_ps.hlsl   (pairs with TaaDecalMark.vso)

sampler2D g_Art   : register(s0);   // the art's mask texture: its texels white
sampler2D g_Depth : register(s2);   // INTZ scene depth, this frame

// Ronin @bugfix 18/11/2026 DX9: c30-c32, which the resolve never reads - TAA's screen copy runs after this with c10-c12 as it left them.
float4 g_Colour : register(c0);     // rgb = the lamp's colour as its art shows it, at its level, x `lights modelglowgain`
float4 g_VpPx   : register(c30);    // z = zNear, w = zFar
float4 g_VpUv   : register(c31);    // xy = the 3D view's origin in frame-buffer UV, zw = its size
float4 g_Hide   : register(c32);    // x = how far behind the scene's surface the art may lie (units), y = the same per unit of depth

float linearise(float raw)
{
    return (g_VpPx.z * g_VpPx.w) / max(g_VpPx.w - raw * (g_VpPx.w - g_VpPx.z), 0.0001f);
}

float4 main(float2 uv : TEXCOORD0, float4 cur : TEXCOORD1) : COLOR0
{
    float2 nc     = cur.xy / cur.w;
    float2 suv    = g_VpUv.xy + float2(nc.x * 0.5f + 0.5f, 0.5f - nc.y * 0.5f) * g_VpUv.zw;
    float  sceneZ = linearise(tex2Dlod(g_Depth, float4(suv, 0.0f, 0.0f)).r);
    float  fragZ  = linearise(cur.z / cur.w);
    clip(sceneZ + max(sceneZ * g_Hide.y, g_Hide.x) - fragZ);
    return float4(g_Colour.rgb * tex2D(g_Art, uv).rgb, 0.0f);
}
