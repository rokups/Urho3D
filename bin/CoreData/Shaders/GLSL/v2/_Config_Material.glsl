/// _Config_Material.glsl
/// Don't include!
/// Material-specific configuration.

/// Lighting is applied in any form (direct light, ambient light, emission, lightmaps, etc).
// #define URHO3D_IS_LIT

/// Whether both sides of surface have exactly the same lighting.
// #define URHO3D_SURFACE_ONE_SIDED

/// Whether both sides of surface have indepentent lighting still based on the normal.
// #define URHO3D_SURFACE_TWO_SIDED

/// Whether the surface normal is ignored when calculating lighting. Not supported by deferred lighting.
// #define URHO3D_SURFACE_VOLUMETRIC

/// Adjust N dot L according to surface type for vertex lighting. Defined only in vertex shader.
// #define VERTEX_ADJUST_NoL

/// Whether to pre-multiply alpha into output color. Used for physically correct specular for transparent objects.
// #define URHO3D_PREMULTIPLY_ALPHA

/// Whether to apply non-PBR reflection mapping.
// #define URHO3D_REFLECTION_MAPPING

/// Whether to apply soft particles fade out.
// #define URHO3D_SOFT_PARTICLES

/// Whether to blur reflection according to surface roughness.
// #define URHO3D_BLUR_REFLECTION

/// =================================== Disable inputs ===================================

#ifdef URHO3D_DISABLE_DIFFUSE_SAMPLING
    #ifdef URHO3D_MATERIAL_HAS_DIFFUSE
        #undef URHO3D_MATERIAL_HAS_DIFFUSE
    #endif
#endif

#ifdef URHO3D_DISABLE_NORMAL_SAMPLING
    #ifdef URHO3D_MATERIAL_HAS_NORMAL
        #undef URHO3D_MATERIAL_HAS_NORMAL
    #endif
#endif

#ifdef URHO3D_DISABLE_SPECULAR_SAMPLING
    #ifdef URHO3D_MATERIAL_HAS_SPECULAR
        #undef URHO3D_MATERIAL_HAS_SPECULAR
    #endif
#endif

#ifdef URHO3D_DISABLE_EMISSIVE_SAMPLING
    #ifdef URHO3D_MATERIAL_HAS_EMISSIVE
        #undef URHO3D_MATERIAL_HAS_EMISSIVE
    #endif
#endif

/// =================================== Convert material defines ===================================

#if defined(PBR)
    #ifndef URHO3D_PHYSICAL_MATERIAL
        #define URHO3D_PHYSICAL_MATERIAL
    #endif
#endif

/// =================================== Configure material with color output ===================================

#if !defined(URHO3D_DEPTH_ONLY_PASS) && !defined(URHO3D_LIGHT_VOLUME_PASS)

    #if !defined(UNLIT)

        #define URHO3D_IS_LIT

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

        #if defined(TRANSPARENT)
            #define URHO3D_PREMULTIPLY_ALPHA
        #endif

        #if defined(URHO3D_AMBIENT_PASS)
            #ifndef URHO3D_SURFACE_NEED_AMBIENT
                #define URHO3D_SURFACE_NEED_AMBIENT
            #endif
        #endif

        #if defined(URHO3D_LIGHT_PASS)

            #if !defined(URHO3D_SURFACE_VOLUMETRIC)
                #ifndef URHO3D_SURFACE_NEED_NORMAL
                    #define URHO3D_SURFACE_NEED_NORMAL
                #endif
            #endif

            #ifndef URHO3D_PIXEL_NEED_LIGHT_VECTOR
                #define URHO3D_PIXEL_NEED_LIGHT_VECTOR
            #endif

            #if URHO3D_SPECULAR > 0
                #ifndef URHO3D_PIXEL_NEED_EYE_VECTOR
                    #define URHO3D_PIXEL_NEED_EYE_VECTOR
                #endif
            #endif

            #if defined(URHO3D_HAS_SHADOW)
                #ifndef URHO3D_PIXEL_NEED_SHADOW_POS
                    #define URHO3D_PIXEL_NEED_SHADOW_POS
                #endif
            #endif

            #if defined(URHO3D_LIGHT_CUSTOM_SHAPE)
                #ifndef URHO3D_PIXEL_NEED_LIGHT_SHAPE_POS
                    #define URHO3D_PIXEL_NEED_LIGHT_SHAPE_POS
                #endif
            #endif

        #endif // URHO3D_LIGHT_PASS

        #if defined(URHO3D_PHYSICAL_MATERIAL) || defined(URHO3D_GBUFFER_PASS)
            #ifndef URHO3D_SURFACE_NEED_NORMAL
                #define URHO3D_SURFACE_NEED_NORMAL
            #endif
        #endif

        #if defined(URHO3D_PHYSICAL_MATERIAL)
            #ifndef URHO3D_PIXEL_NEED_EYE_VECTOR
                #define URHO3D_PIXEL_NEED_EYE_VECTOR
            #endif
        #endif

    #endif // UNLIT

    #if defined(URHO3D_AMBIENT_PASS) && (defined(ENVCUBEMAP) || defined(URHO3D_PHYSICAL_MATERIAL))
        #ifndef URHO3D_SURFACE_NEED_REFLECTION_COLOR
            #define URHO3D_SURFACE_NEED_REFLECTION_COLOR
        #endif
    #endif

    #if defined(URHO3D_AMBIENT_PASS) && defined(ENVCUBEMAP)
        #define URHO3D_REFLECTION_MAPPING
        #ifndef URHO3D_SURFACE_NEED_NORMAL
            #define URHO3D_SURFACE_NEED_NORMAL
        #endif
    #endif

    #if defined(URHO3D_VERTEX_HAS_COLOR)
        #ifndef URHO3D_PIXEL_NEED_VERTEX_COLOR
            #define URHO3D_PIXEL_NEED_VERTEX_COLOR
        #endif
    #endif

    #if defined(SOFTPARTICLES) && defined(URHO3D_HAS_READABLE_DEPTH)
        #define URHO3D_SOFT_PARTICLES
    #endif
#endif // URHO3D_DEPTH_ONLY_PASS

/// =================================== Configure light volume pass ===================================

#ifdef URHO3D_LIGHT_VOLUME_PASS
    #define URHO3D_IS_LIT
#endif

/// =================================== Propagate implications ===================================

/// If per-pixel reflection is used for PBR material, blur reflection according to surface roughness.
#if defined(URHO3D_FEATURE_CUBEMAP_LOD) && defined(URHO3D_PHYSICAL_MATERIAL) && !defined(URHO3D_VERTEX_REFLECTION)
    #define URHO3D_BLUR_REFLECTION
#endif

/// If surface needs ambient, vertex shader needs normal for vertex and SH lighting.
#if defined(URHO3D_SURFACE_NEED_AMBIENT)
    #ifndef URHO3D_VERTEX_NEED_NORMAL
        #define URHO3D_VERTEX_NEED_NORMAL
    #endif
#endif

/// If surface needs ambient and lightmapping is enabled, pixel shader needs lightmap UVs.
#if defined(URHO3D_SURFACE_NEED_AMBIENT) && defined(URHO3D_HAS_LIGHTMAP)
    #ifndef URHO3D_PIXEL_NEED_LIGHTMAP_UV
        #define URHO3D_PIXEL_NEED_LIGHTMAP_UV
    #endif
#endif

/// If surface needs reflection, pixel shader needs eye or relfection vector.
#if defined(URHO3D_REFLECTION_MAPPING) || defined(URHO3D_PHYSICAL_MATERIAL)
    #if defined(URHO3D_VERTEX_REFLECTION)
        #ifndef URHO3D_PIXEL_NEED_REFLECTION_VECTOR
            #define URHO3D_PIXEL_NEED_REFLECTION_VECTOR
        #endif
    #else
        #ifndef URHO3D_PIXEL_NEED_EYE_VECTOR
            #define URHO3D_PIXEL_NEED_EYE_VECTOR
        #endif
    #endif
#endif

/// If surface needs normal, pixel shader needs normal and optionally tangent.
#if defined(URHO3D_SURFACE_NEED_NORMAL)
    #ifndef URHO3D_PIXEL_NEED_NORMAL
        #define URHO3D_PIXEL_NEED_NORMAL
    #endif

    #if defined(NORMALMAP)
        #ifndef URHO3D_PIXEL_NEED_TANGENT
            #define URHO3D_PIXEL_NEED_TANGENT
        #endif

        #ifndef URHO3D_NORMAL_MAPPING
            #define URHO3D_NORMAL_MAPPING
        #endif
    #endif
#endif

/// If pixel shader needs normal, vertex shader needs normal.
#ifdef URHO3D_PIXEL_NEED_NORMAL
    #ifndef URHO3D_VERTEX_NEED_NORMAL
        #define URHO3D_VERTEX_NEED_NORMAL
    #endif
#endif

/// If pixel shader needs tangent, vertex shader needs tangent.
#ifdef URHO3D_PIXEL_NEED_TANGENT
    #ifndef URHO3D_VERTEX_NEED_TANGENT
        #define URHO3D_VERTEX_NEED_TANGENT
    #endif
#endif

/// If soft particles are enabled, pixel shader needs background depth.
#ifdef URHO3D_SOFT_PARTICLES
    #ifndef URHO3D_SURFACE_NEED_BACKGROUND_DEPTH
        #define URHO3D_SURFACE_NEED_BACKGROUND_DEPTH
    #endif
#endif

/// If background color and/or depth is sampled, pixel shader needs screen position.
#if defined(URHO3D_SURFACE_NEED_BACKGROUND_DEPTH) || defined(URHO3D_SURFACE_NEED_BACKGROUND_COLOR) \
    || (defined(URHO3D_REFLECTION_MAPPING) && defined(URHO3D_MATERIAL_HAS_PLANAR_ENVIRONMENT))
    #ifndef URHO3D_PIXEL_NEED_SCREEN_POSITION
        #define URHO3D_PIXEL_NEED_SCREEN_POSITION
    #endif
#endif

/// If shadow normal offset is enabled, vertex shader needs normal.
#ifdef URHO3D_SHADOW_NORMAL_OFFSET
    #ifndef URHO3D_VERTEX_NEED_NORMAL
        #define URHO3D_VERTEX_NEED_NORMAL
    #endif
#endif
