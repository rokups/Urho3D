/// _Config.glsl
/// Global configuration management, platform compatibility utilities,
/// global constants and helper functions.
/// Should be included before any other shader code.
///
/// Compatibility notes:
/// - Don't use function-style macros with 0 arguments, they don't work on some Android devices. Example (bad):
///   #define GetModelMatrix() cModel
/// - Don't change uniform buffer content depending on shader type or on shader-type-specifiec defines:
///   DX11 expects same buffers and will not handle it well.
///   Consider disabling whole buffer for stage.
///   Example: Object uniform buffer is enabled only for vertex shader.
#ifndef _CONFIG_GLSL_
#define _CONFIG_GLSL_

#extension GL_ARB_shading_language_420pack: enable
#extension GL_EXT_shader_texture_lod: enable
#extension GL_OES_standard_derivatives : enable

/// =================================== Shader configuration controls ===================================

/// Whether to disable default uniform buffer in favor of custom one.
/// You may still need to declare default constants to use built-in includes.
// #define URHO3D_CUSTOM_MATERIAL_UNIFORMS

/// Configures what data vertex shader needs in VertexTransform:
// #define URHO3D_VERTEX_NEED_NORMAL
// #define URHO3D_VERTEX_NEED_TANGENT

/// Configures what data pixel shader needs from vertex shader:
// #define URHO3D_PIXEL_NEED_SCREEN_POSITION
// #define URHO3D_PIXEL_NEED_EYE_VECTOR
// #define URHO3D_PIXEL_NEED_NORMAL
// #define URHO3D_PIXEL_NEED_TANGENT
// #define URHO3D_PIXEL_NEED_VERTEX_COLOR

/// Configures what data pixel shader needs to prepare for user shader:
// #define URHO3D_SURFACE_NEED_AMBIENT
// #define URHO3D_SURFACE_NEED_NORMAL
// #define URHO3D_SURFACE_NEED_NORMAL_IN_TANGENT_SPACE
// #define URHO3D_SURFACE_NEED_BACKGROUND_DEPTH
// #define URHO3D_SURFACE_NEED_BACKGROUND_COLOR

/// Configures whether to disable automatic sampling of built-in inputs.
// #define URHO3D_DISABLE_DIFFUSE_SAMPLING
// #define URHO3D_DISABLE_NORMAL_SAMPLING
// #define URHO3D_DISABLE_SPECULAR_SAMPLING
// #define URHO3D_DISABLE_EMISSIVE_SAMPLING


/// =================================== Material defines ===================================

/// Whether to disable all lighting calculation.
// #define UNLIT

/// Whether to discard pixels with alpha less than 0.5 in diffuse texture. Material properties are ignored.
// #define ALPHAMASK

/// Whether to apply normal mapping.
// #define NORMALMAP

/// Whether to treat emission map as occlusion map.
// #define AO

/// Whether to sample reflections from environment cubemap.
// #define ENVCUBEMAP

/// Whether to use two-sided ligthing for geometries.
// #define TRANSLUCENT

/// Whether to use volumetric ligthing for geometries (forward lighting only).
// #define VOLUMETRIC

/// Whether to use physiclally based material.
// #define PBR

/// Whether to apply soft fade-out for particles.
// #define SOFTPARTICLES

/// Whether to render transparent object.
/// If defined, specular is not affected by alpha.
/// If not defined, object fades out completely when alpha approaches zero.
// #define TRANSPARENT

/// Whether to unpack normal from red and alpha channels of normal map.
// #define PACKEDNORMAL


/// =================================== Deprecated material defines ===================================

// TODO(legacy): Remove deprecated defines
// #define DIFFMAP
// #define SPECMAP
// #define EMISSIVEMAP
// #define METALLIC
// #define ROUGHNESS
// #define VERTEXCOLOR
// #define NOUV
// #define AMBIENT
// #define DEFERRED
// #define IBL

#include "_Config_Basic.glsl"
#include "_Config_Material.glsl"

#endif // _CONFIG_GLSL_
