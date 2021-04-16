/// _Config_Material.glsl
/// Don't include!
/// Material-specific configuration.

/// URHO3D_IS_LIT: Whether there's lighting in any form applied to geometry.
/// URHO3D_HAS_PIXEL_LIGHT: Whether there's per-pixel lighting from directional, point or spot light source.
/// URHO3D_NORMAL_MAPPING: Whether to apply normal mapping.
/// URHO3D_PIXEL_NEED_AMBIENT: Whether pixel shader needs ambient lighting.
#if !defined(UNLIT) && !defined(URHO3D_DEPTH_ONLY_PASS)
    #define URHO3D_IS_LIT

    #if defined(URHO3D_LIGHT_DIRECTIONAL) || defined(URHO3D_LIGHT_POINT) || defined(URHO3D_LIGHT_SPOT)
        #define URHO3D_HAS_PIXEL_LIGHT
    #endif

    #ifdef NORMALMAP
        #define URHO3D_NORMAL_MAPPING
    #endif

    #ifdef URHO3D_AMBIENT_PASS
        #define URHO3D_PIXEL_NEED_AMBIENT
    #endif
#endif

/// URHO3D_PHYSICAL_MATERIAL: Whether to use physiclally based material.
/// Implies URHO3D_LIGHT_HAS_SPECULAR.
/// Implies URHO3D_REFLECTION_MAPPING.
/// Changes semantics of specular texture:
/// - sSpecMap.r: Roughness;
/// - sSpecMap.g: Metallic factor;
/// - sSpecMap.a: Occlusion.
#if defined(URHO3D_IS_LIT) && defined(PBR)
    #define URHO3D_PHYSICAL_MATERIAL
    #ifndef URHO3D_LIGHT_HAS_SPECULAR
        #define URHO3D_LIGHT_HAS_SPECULAR
    #endif
#endif

/// URHO3D_PREMULTIPLY_ALPHA: Whether to pre-multiply alpha into output color
#if defined(URHO3D_IS_LIT) && defined(TRANSPARENT)
    #define URHO3D_PREMULTIPLY_ALPHA
#endif

/// URHO3D_REFLECTION_MAPPING: Whether to apply reflections from environment cubemap.
#if !defined(URHO3D_ADDITIVE_LIGHT_PASS) && (defined(ENVCUBEMAP) || defined(URHO3D_PHYSICAL_MATERIAL))
    #define URHO3D_REFLECTION_MAPPING
#endif

/// URHO3D_SOFT_PARTICLES: Whether to apply soft particles fade out.
#if defined(PARTICLE) && defined(URHO3D_SOFT_PARTICLES_ENABLED)
    #define URHO3D_SOFT_PARTICLES
#endif

/// URHO3D_SPECULAR_ANTIALIASING is disabled if derivatives are not supported.
#if defined(GL_ES) && !defined(GL_OES_standard_derivatives)
    #undef URHO3D_SPECULAR_ANTIALIASING
#endif

/// URHO3D_PIXEL_NEED_BACKGROUND_DEPTH is implied by soft particles
#ifdef URHO3D_SOFT_PARTICLES
    #ifndef URHO3D_PIXEL_NEED_BACKGROUND_DEPTH
        #define URHO3D_PIXEL_NEED_BACKGROUND_DEPTH
    #endif
#endif

#ifdef URHO3D_IGNORE_MATERIAL_DIFFUSE
    #ifdef URHO3D_MATERIAL_HAS_DIFFUSE
        #undef URHO3D_MATERIAL_HAS_DIFFUSE
    #endif
#endif

#ifdef URHO3D_IGNORE_MATERIAL_NORMAL
    #ifdef URHO3D_MATERIAL_HAS_NORMAL
        #undef URHO3D_MATERIAL_HAS_NORMAL
    #endif
#endif

#ifdef URHO3D_IGNORE_MATERIAL_SPECULAR
    #ifdef URHO3D_MATERIAL_HAS_SPECULAR
        #undef URHO3D_MATERIAL_HAS_SPECULAR
    #endif
#endif

#ifdef URHO3D_IGNORE_MATERIAL_EMISSIVE
    #ifdef URHO3D_MATERIAL_HAS_EMISSIVE
        #undef URHO3D_MATERIAL_HAS_EMISSIVE
    #endif
#endif


// =================================== Vertex output configuration ===================================

/// URHO3D_PIXEL_NEED_EYE_VECTOR is implied by:
/// - Specular lighting;
/// - Reflection mapping.
#if defined(URHO3D_REFLECTION_MAPPING) || defined(URHO3D_LIGHT_HAS_SPECULAR)
    #ifndef URHO3D_PIXEL_NEED_EYE_VECTOR
        #define URHO3D_PIXEL_NEED_EYE_VECTOR
    #endif
#endif

/// TODO(renderer): Don't do it for particles?
/// URHO3D_PIXEL_NEED_NORMAL is implied by:
/// - Per-pixel lighting;
/// - Geometry buffer rendering for lit geometry;
/// - Reflection mapping.
#if defined(URHO3D_HAS_PIXEL_LIGHT) || (defined(URHO3D_IS_LIT) && defined(URHO3D_GBUFFER_PASS)) || defined(URHO3D_REFLECTION_MAPPING)
    #ifndef URHO3D_PIXEL_NEED_NORMAL
        #define URHO3D_PIXEL_NEED_NORMAL
    #endif
#endif

/// URHO3D_PIXEL_NEED_TANGENT is implied by normal mapping.
#if defined(URHO3D_NORMAL_MAPPING)
    #ifndef URHO3D_PIXEL_NEED_TANGENT
        #define URHO3D_PIXEL_NEED_TANGENT
    #endif
#endif

/// URHO3D_PIXEL_NEED_VERTEX_COLOR is implied by URHO3D_VERTEX_HAS_COLOR.
#if defined(URHO3D_VERTEX_HAS_COLOR) && !defined(URHO3D_SHADOW_PASS)
    #ifndef URHO3D_PIXEL_NEED_VERTEX_COLOR
        #define URHO3D_PIXEL_NEED_VERTEX_COLOR
    #endif
#endif

/// URHO3D_PIXEL_NEED_SCREEN_POSITION is implied by background color or depth.
#if defined(URHO3D_PIXEL_NEED_BACKGROUND_DEPTH) || defined(URHO3D_PIXEL_NEED_BACKGROUND_COLOR)
    #ifndef URHO3D_PIXEL_NEED_SCREEN_POSITION
        #define URHO3D_PIXEL_NEED_SCREEN_POSITION
    #endif
#endif

/// =================================== Vertex input configuration ===================================

#ifdef URHO3D_VERTEX_SHADER
    /// URHO3D_VERTEX_NEED_NORMAL is implied by:
    /// - URHO3D_PIXEL_NEED_NORMAL;
    /// - Any kind of lighting;
    /// - Shadow normal bias.
    #ifdef URHO3D_VERTEX_NORMAL_AVAILABLE
        #if defined(URHO3D_PIXEL_NEED_NORMAL) || defined(URHO3D_IS_LIT) || defined(URHO3D_SHADOW_NORMAL_OFFSET)
            #ifndef URHO3D_VERTEX_NEED_NORMAL
                #define URHO3D_VERTEX_NEED_NORMAL
            #endif
        #endif
    #endif

    /// URHO3D_VERTEX_NEED_TANGENT is implied by URHO3D_PIXEL_NEED_TANGENT.
    #ifdef URHO3D_VERTEX_TANGENT_AVAILABLE
        #if defined(URHO3D_PIXEL_NEED_TANGENT)
            #ifndef URHO3D_VERTEX_NEED_TANGENT
                #define URHO3D_VERTEX_NEED_TANGENT
            #endif
        #endif
    #endif
#endif // URHO3D_VERTEX_SHADER

/// =================================== Light configuration ===================================

#if defined(URHO3D_IS_LIT)
    // TODO(renderer): Handle this for pixel lights too
    /// URHO3D_SURFACE_ONE_SIDED: Normal is clamped when calculating lighting. Ignored in deferred rendering.
    /// URHO3D_SURFACE_TWO_SIDED: Normal is mirrored when calculating lighting. Ignored in deferred rendering.
    /// URHO3D_SURFACE_VOLUMETRIC: Normal is ignored when calculating lighting. Ignored in deferred rendering.
    /// VERTEX_ADJUST_NoL: Adjust N dot L for lighting in vertex shader.
    /// PIXEL_ADJUST_NoL: Adjust N dot L for lighting in pixel shader.
    #if defined(VOLUMETRIC)
        #define URHO3D_SURFACE_VOLUMETRIC
        #define VERTEX_ADJUST_NoL(NoL) 1.0
    #elif defined(TRANSLUCENT)
        #define URHO3D_SURFACE_TWO_SIDED
        #define VERTEX_ADJUST_NoL(NoL) abs(NoL)
    #else
        #define URHO3D_SURFACE_ONE_SIDED
        #define VERTEX_ADJUST_NoL(NoL) max(0.0, NoL)
    #endif
#endif
