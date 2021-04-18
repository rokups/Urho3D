/// _Config_Material.glsl
/// Don't include!
/// Material-specific configuration.

/// =================================== Disable inputs ===================================

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

/// =================================== Convert material defines ===================================

#if defined(PBR)
    #ifndef URHO3D_PHYSICAL_MATERIAL
        #define URHO3D_PHYSICAL_MATERIAL
    #endif
#endif

/// =================================== Configure material with color output ===================================

#if !defined(URHO3D_DEPTH_ONLY_PASS) && !defined(URHO3D_LIGHT_VOLUME_PASS)

    /// Implied inputs for lit passes.
    #if !defined(UNLIT)

        /// URHO3D_IS_LIT: Lighting is applied.
        #define URHO3D_IS_LIT

        /// URHO3D_SURFACE_ONE_SIDED: Normal is clamped when calculating lighting.
        /// URHO3D_SURFACE_TWO_SIDED: Normal is mirrored when calculating lighting.
        /// URHO3D_SURFACE_VOLUMETRIC: Normal is ignored when calculating lighting. Ignored in deferred rendering.
        /// VERTEX_ADJUST_NoL: Adjust N dot L for lighting in vertex shader.
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

        /// URHO3D_PREMULTIPLY_ALPHA: Whether to pre-multiply alpha into output color
        #if defined(TRANSPARENT)
            #define URHO3D_PREMULTIPLY_ALPHA
        #endif

        /// Implied inputs for ambient lit pass.
        #ifdef URHO3D_AMBIENT_PASS

            /// Pixel shader needs ambient lighting color.
            #ifndef URHO3D_SURFACE_NEED_AMBIENT
                #define URHO3D_SURFACE_NEED_AMBIENT
            #endif

            /// Vertex shader needs normal for vertex and SH lighting.
            #ifndef URHO3D_VERTEX_NEED_NORMAL
                #define URHO3D_VERTEX_NEED_NORMAL
            #endif

            /// Pixel shader needs lightmap UVs if enabled.
            #if defined(URHO3D_HAS_LIGHTMAP)
                #ifndef URHO3D_PIXEL_NEED_LIGHTMAP_UV
                    #define URHO3D_PIXEL_NEED_LIGHTMAP_UV
                #endif
            #endif

            /// Pixel shader needs emission.
            #ifndef URHO3D_SURFACE_NEED_EMISSION
                #define URHO3D_SURFACE_NEED_EMISSION
            #endif

        #endif

        /// Implied inputs for per-pixel light passes.
        #if defined(URHO3D_LIGHT_PASS)

            /// Pixel shader needs normal for lighting calculation.
            #if !defined(URHO3D_SURFACE_VOLUMETRIC)
                #ifndef URHO3D_SURFACE_NEED_NORMAL
                    #define URHO3D_SURFACE_NEED_NORMAL
                #endif
            #endif

            /// Pixel shader needs vector to light source.
            #ifndef URHO3D_PIXEL_NEED_LIGHT_VECTOR
                #define URHO3D_PIXEL_NEED_LIGHT_VECTOR
            #endif

            /// Pixel shader needs eye vector for specular if enabled.
            #if URHO3D_SPECULAR > 0
                #ifndef URHO3D_PIXEL_NEED_EYE_VECTOR
                    #define URHO3D_PIXEL_NEED_EYE_VECTOR
                #endif
            #endif

            /// Pixel shader needs shadow coordinate(s) if enabled.
            #if defined(URHO3D_HAS_SHADOW)
                #ifndef URHO3D_PIXEL_NEED_SHADOW_POS
                    #define URHO3D_PIXEL_NEED_SHADOW_POS
                #endif
            #endif

            /// Pixel shader needs light shape coordinate if enabled.
            #if defined(URHO3D_LIGHT_CUSTOM_SHAPE)
                #ifndef URHO3D_PIXEL_NEED_LIGHT_SHAPE_POS
                    #define URHO3D_PIXEL_NEED_LIGHT_SHAPE_POS
                #endif
            #endif

        #endif // URHO3D_LIGHT_PASS

        /// PBR materials and Geometry buffer pass always need normal.
        #if defined(URHO3D_PHYSICAL_MATERIAL) || defined(URHO3D_GBUFFER_PASS)
            #ifndef URHO3D_SURFACE_NEED_NORMAL
                #define URHO3D_SURFACE_NEED_NORMAL
            #endif
        #endif

        /// PBR materials always need eye vector.
        #if defined(URHO3D_PHYSICAL_MATERIAL)
            #ifndef URHO3D_PIXEL_NEED_EYE_VECTOR
                #define URHO3D_PIXEL_NEED_EYE_VECTOR
            #endif
        #endif

    #endif // UNLIT

    /// Reflective or PBR materials need reflection color.
    #if defined(URHO3D_AMBIENT_PASS) && (defined(ENVCUBEMAP) || defined(URHO3D_PHYSICAL_MATERIAL))
        #ifndef URHO3D_SURFACE_NEED_REFLECTION_COLOR
            #define URHO3D_SURFACE_NEED_REFLECTION_COLOR
        #endif
    #endif

    /// URHO3D_REFLECTION_MAPPING: Material has non-PBR reflection.
    #if defined(URHO3D_AMBIENT_PASS) && defined(ENVCUBEMAP)
        #define URHO3D_REFLECTION_MAPPING
    #endif

    /// If vertex color is present, it's propagated to pixel shader.
    #if defined(URHO3D_VERTEX_HAS_COLOR)
        #ifndef URHO3D_PIXEL_NEED_VERTEX_COLOR
            #define URHO3D_PIXEL_NEED_VERTEX_COLOR
        #endif
    #endif

    /// URHO3D_SOFT_PARTICLES: Whether to apply soft particles fade out.
    #if defined(SOFTPARTICLES) && defined(URHO3D_HAS_READABLE_DEPTH)
        #define URHO3D_SOFT_PARTICLES
    #endif
#endif // URHO3D_DEPTH_ONLY_PASS

/// =================================== Configure light volume pass ===================================

#ifdef URHO3D_LIGHT_VOLUME_PASS
    #define URHO3D_IS_LIT
#endif

/// =================================== Propagate implications upwards ===================================

/// If surface needs reflection, pixel shader needs eye or relfection vector.
#if defined(URHO3D_REFLECTION_MAPPING) || defined(URHO3D_PHYSICAL_MATERIAL)
    #if defined(URHO3D_VERTEX_SHADER_REFLECTION)
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
#if defined(URHO3D_SURFACE_NEED_BACKGROUND_DEPTH) || defined(URHO3D_SURFACE_NEED_BACKGROUND_COLOR)
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
