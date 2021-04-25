#ifndef D3D11

#include "Uniforms.hlsl"
#include "Samplers.hlsl"
#include "Transform.hlsl"
#include "ScreenPos.hlsl"

void VS(float4 iPos : POSITION,
    out float4 oPos : OUTPOSITION)
{
    float4x3 modelMatrix = iModelMatrix;
    float3 worldPos = GetWorldPos(modelMatrix);
    oPos = GetClipPos(worldPos);
}

void PS(out float4 oColor : OUTCOLOR0)
{
    oColor = cMatDiffColor;
}

#else

cbuffer FrameVS : register(b0)
{
    float4x3 cMatrix;
    float4 cColor;
}

void VS(float4 iPos : POSITION,
    out float4 oPos : SV_POSITION)
{
    oPos = float4(mul(iPos, cMatrix), 1.0);
}

void PS(out float4 oColor : SV_TARGET)
{
    oColor = cColor;
}

#endif
