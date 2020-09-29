#ifndef D3D11

// Fallback to legacy shader for D3D9
#include "Basic.hlsl"

#else

cbuffer CameraVS : register(b1)
{
    float4x4 cViewProj;
}

Texture2D tDiffMap : register(t0);
SamplerState sDiffMap : register(s0);

#define Sample2D(tex, uv) t##tex.Sample(s##tex, uv)

float4 GetClipPos(float3 worldPos)
{
    return mul(float4(worldPos, 1.0), cViewProj);
}

#define OUTPOSITION SV_POSITION
#define OUTCOLOR0 SV_TARGET


void VS(float4 iPos : POSITION,
    #ifdef DIFFMAP
        float2 iTexCoord : TEXCOORD0,
    #endif
    #ifdef VERTEXCOLOR
        float4 iColor : COLOR0,
    #endif
    #ifdef DIFFMAP
        out float2 oTexCoord : TEXCOORD0,
    #endif
    #ifdef VERTEXCOLOR
        out float4 oColor : COLOR0,
    #endif
    out float4 oPos : OUTPOSITION)
{
    float3 worldPos = iPos.xyz;
    oPos = GetClipPos(worldPos);

    #ifdef VERTEXCOLOR
        oColor = iColor;
    #endif
    #ifdef DIFFMAP
        oTexCoord = iTexCoord;
    #endif
}

void PS(
    #if defined(DIFFMAP) || defined(ALPHAMAP)
        float2 iTexCoord : TEXCOORD0,
    #endif
    #ifdef VERTEXCOLOR
        float4 iColor : COLOR0,
    #endif
    out float4 oColor : OUTCOLOR0)
{
    float4 diffColor = 1.0;

    #ifdef VERTEXCOLOR
        diffColor *= iColor;
    #endif

    #if !defined(DIFFMAP) && !defined(ALPHAMAP)
        oColor = diffColor;
    #endif
    #ifdef DIFFMAP
        float4 diffInput = Sample2D(DiffMap, iTexCoord);
        #ifdef ALPHAMASK
            if (diffInput.a < 0.5)
                discard;
        #endif
        oColor = diffColor * diffInput;
    #endif
    #ifdef ALPHAMAP
        float alphaInput = Sample2D(DiffMap, iTexCoord).a;
        oColor = float4(diffColor.rgb, diffColor.a * alphaInput);
    #endif
}

#endif
