#ifndef _DEFERRED_LIGHTING_GLSL_
#define _DEFERRED_LIGHTING_GLSL_

#ifndef _DIRECT_LIGHTING_GLSL_
    #error Include _DirectLighting.glsl before _DeferredLighting.glsl
#endif

#ifndef _VERTEX_SCREEN_POS_GLSL_
    #error Include _VertexScreenPos.glsl before _DeferredLighting.glsl
#endif

#ifdef URHO3D_VERTEX_SHADER
    /// Vertex shader utilities for deferred rendering
    #ifdef URHO3D_LIGHT_DIRECTIONAL
        #define GetDeferredScreenPos(clipPos)   vec4(GetScreenPosPreDiv(clipPos), 0.0, 1.0)
        #define GetDeferredNearRay(clipPos)     GetNearRay(clipPos)
        #define GetDeferredFarRay(clipPos)      GetFarRay(clipPos)
    #else
        #define GetDeferredScreenPos(clipPos)   GetScreenPos(clipPos)
        #define GetDeferredNearRay(clipPos)     (GetNearRay(clipPos) * clipPos.w)
        #define GetDeferredFarRay(clipPos)      (GetFarRay(clipPos) * clipPos.w)
    #endif
#endif

#ifdef URHO3D_PIXEL_SHADER
#ifdef URHO3D_LIGHT_PASS
    /// Sample geometry buffer at position.
    #ifdef URHO3D_LIGHT_DIRECTIONAL
        #define SampleGeometryBuffer(texture, screenPos) texture2D(texture, screenPos.xy)
    #else
        #define SampleGeometryBuffer(texture, screenPos) texture2DProj(texture, screenPos)
    #endif

    /// Return world position from depth.
    #ifdef URHO3D_ORTHOGRAPHIC_DEPTH
        #define GetDeferredWorldPosTemp(depth) mix(vNearRay, vFarRay, depth)
    #else
        #define GetDeferredWorldPosTemp(depth) (vFarRay * depth)
    #endif
    #ifdef URHO3D_LIGHT_DIRECTIONAL
        #define GetDeferredWorldPos(screenPos, depth) vec4(GetDeferredWorldPosTemp(depth), 1.0)
    #else
        #define GetDeferredWorldPos(screenPos, depth) vec4((GetDeferredWorldPosTemp(depth) / screenPos.w), 1.0)
    #endif

    /// Return pixel lighting data for deferred rendering.
    DirectLightData GetDeferredDirectLightData(const vec4 worldPos, const float depth)
    {
        DirectLightData result;
        result.lightVec = NormalizeLightVector(GetLightVector(worldPos.xyz));
    #ifdef URHO3D_LIGHT_CUSTOM_SHAPE
        vec4 shapePos = worldPos * cLightShapeMatrix;
        result.lightColor = GetLightColorFromShape(shapePos);
    #else
        result.lightColor = GetLightColor(result.lightVec.xyz);
    #endif
    #ifdef URHO3D_HAS_SHADOW
        vec4 shadowUV = WorldSpaceToShadowUV(worldPos, depth);
        result.shadow = FadeShadow(SampleShadowFiltered(shadowUV), depth);
    #endif
        return result;
    }

#endif // URHO3D_LIGHT_PASS
#endif // URHO3D_PIXEL_SHADER

#endif // _DEFERRED_LIGHTING_GLSL_
