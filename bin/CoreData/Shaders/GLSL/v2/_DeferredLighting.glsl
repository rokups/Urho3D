#ifndef _DEFERRED_LIGHTING_GLSL_
#define _DEFERRED_LIGHTING_GLSL_

#ifndef _DIRECT_LIGHTING_GLSL_
    #error Include "_DirectLighting.glsl" before "_DeferredLighting.glsl"
#endif

#ifdef URHO3D_HAS_PIXEL_LIGHT
#ifdef URHO3D_PIXEL_SHADER
    /// Return pixel lighting data for deferred rendering.
    PixelLightData GetDeferredPixelLightData(vec4 worldPos, float depth)
    {
        PixelLightData result;
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
#endif // URHO3D_PIXEL_SHADER
#endif // URHO3D_HAS_PIXEL_LIGHT

#endif // _DEFERRED_LIGHTING_GLSL_
