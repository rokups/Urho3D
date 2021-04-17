/// _Config_Basic.glsl
/// Don't include!
/// Global shader configuration that is invariant to material properties.

/// =================================== Global Constants ===================================

#define M_PI 3.14159265358979323846
#define M_MEDIUMP_FLT_MAX 65504.0


/// =================================== Type Aliases ===================================

/// Disable precision modifiers if not GL ES
#ifndef GL_ES
    #define highp
    #define mediump
    #define lowp
#endif

/// Define shortcuts for precision-qualified types
#define half   mediump float
#define half2  mediump vec2
#define half3  mediump vec3
#define half4  mediump vec4
#define fixed  lowp float
#define fixed2 lowp vec2
#define fixed3 lowp vec3
#define fixed4 lowp vec4


/// =================================== Compiler features ===================================

/// Current shader stage:
/// - URHO3D_VERTEX_SHADER
/// - URHO3D_PIXEL_SHADER
#ifdef COMPILEVS
    #define URHO3D_VERTEX_SHADER
#endif
#ifdef COMPILEPS
    #define URHO3D_PIXEL_SHADER
#endif

/// URHO3D_FEATURE_HIGHP: Whether high precision is supported for all shader stages.
#if !defined(GL_ES) || defined(GL_FRAGMENT_PRECISION_HIGH)
    #define URHO3D_FEATURE_HIGHP
#endif

/// URHO3D_FEATURE_HIGHP_IN_STAGE: Whether high precision is supported for current shader stage.
#if !defined(GL_ES) || defined(GL_FRAGMENT_PRECISION_HIGH) || defined(URHO3D_VERTEX_SHADER)
    #define URHO3D_FEATURE_HIGHP_IN_STAGE
#endif

/// URHO3D_FEATURE_DERIVATIVES: Whether standard derivatives are supported.
#if !defined(GL_ES) || defined(GL_OES_standard_derivatives)
    #define URHO3D_FEATURE_DERIVATIVES
#endif

/// URHO3D_FEATURE_FRAMEBUFFER_Y_INVERTED: Whether Y axis of clip space is the opposite of Y axis of render target texture.
#ifdef D3D11
    #define URHO3D_FEATURE_FRAMEBUFFER_Y_INVERTED
#endif


/// =================================== Global functions ===================================

#define SaturateMediump(x) min(x, M_MEDIUMP_FLT_MAX)


/// =================================== Stage inputs and outputs ===================================

/// ivec4_attrib: Compatible ivec4 vertex attribite. Cast to ivec4 before use.
#ifdef D3D11
    #define ivec4_attrib ivec4
#else
    #define ivec4_attrib vec4
#endif

/// VERTEX_INPUT: Declare vertex input variable;
/// VERTEX_OUTPUT: Declare vertex output variable.
#if defined(URHO3D_VERTEX_SHADER)
    #ifdef GL3
        #define VERTEX_INPUT(decl) in decl;
        #define VERTEX_OUTPUT(decl) out decl;
    #else
        #define VERTEX_INPUT(decl) attribute decl;
        #define VERTEX_OUTPUT(decl) varying decl;
    #endif
#elif defined(URHO3D_PIXEL_SHADER)
    #ifdef GL3
        #define VERTEX_OUTPUT(decl) in decl;
    #else
        #define VERTEX_OUTPUT(decl) varying decl;
    #endif
#endif

/// VERTEX_OUTPUT_HIGHP: Declare vertex output that is preferred to be highp.
#ifdef URHO3D_FEATURE_HIGHP
    #define VERTEX_OUTPUT_HIGHP(decl) VERTEX_OUTPUT(highp decl)
#else
    #define VERTEX_OUTPUT_HIGHP(decl) VERTEX_OUTPUT(mediump decl)
#endif

/// Default to 1 render target if not defined.
#ifndef URHO3D_NUM_RENDER_TARGETS
    #define URHO3D_NUM_RENDER_TARGETS 1
#endif

/// gl_FragData and gl_FragColor: color outputs of pixel shader.
#if defined(URHO3D_PIXEL_SHADER) && URHO3D_NUM_RENDER_TARGETS > 0
    #ifdef GL3
        out vec4 _fragData[URHO3D_NUM_RENDER_TARGETS];

        #define gl_FragColor _fragData[0]
        #define gl_FragData _fragData
    #else
        #if URHO3D_NUM_RENDER_TARGETS > 1
            #define gl_FragColor gl_FragData[0]
        #endif
    #endif
#endif


/// =================================== Uniforms ===================================

/// UNIFORM_BUFFER_BEGIN: Begin of uniform buffer declaration;
/// UNIFORM_BUFFER_END: End of uniform buffer declaration;
/// UNIFORM: Declares scalar, vector or matrix uniform;
/// SAMPLER: Declares texture sampler.
#ifdef URHO3D_USE_CBUFFERS
    #ifdef GL_ARB_shading_language_420pack
        #define _URHO3D_LAYOUT(index) layout(binding=index)
    #else
        #define _URHO3D_LAYOUT(index)
    #endif

    #define UNIFORM_BUFFER_BEGIN(index, name) _URHO3D_LAYOUT(index) uniform name {
    #define UNIFORM(decl) decl;
    #define UNIFORM_BUFFER_END(index, name) };
    #define SAMPLER(index, decl) _URHO3D_LAYOUT(index) uniform decl;
#else
    #define UNIFORM_BUFFER_BEGIN(index, name)
    #define UNIFORM(decl) uniform decl;
    #define UNIFORM_BUFFER_END(index, name)
    #define SAMPLER(index, decl) uniform decl;
#endif

/// INSTANCE_BUFFER_BEGIN: Begin of uniform buffer or group of per-instance attributes
/// INSTANCE_BUFFER_END: End of uniform buffer or instance attributes
#ifdef URHO3D_VERTEX_SHADER
    #ifdef URHO3D_INSTANCING
        #define INSTANCE_BUFFER_BEGIN(index, name)
        #define INSTANCE_BUFFER_END(index, name)
    #else
        #define INSTANCE_BUFFER_BEGIN(index, name) UNIFORM_BUFFER_BEGIN(index, name)
        #define INSTANCE_BUFFER_END(index, name) UNIFORM_BUFFER_END(index, name)
    #endif
#endif

/// UNIFORM_HIGHP: Uniform with max precision, undefined if not supported.
/// SAMPLER_HIGHP: Sampler with max precision, mediump if not supported.
#ifndef GL_ES
    #define UNIFORM_HIGHP(decl) UNIFORM(decl)
    #define SAMPLER_HIGHP(index, decl) SAMPLER(index, decl)
#else
    /// Use max available precision by default
    #if defined(GL_FRAGMENT_PRECISION_HIGH) || !defined(URHO3D_PIXEL_SHADER)
        precision highp float;
        #define UNIFORM_HIGHP(decl) UNIFORM(decl)
        #define SAMPLER_HIGHP(index, decl) SAMPLER(index, highp decl)
    #else
        precision mediump float;
        #define UNIFORM_HIGHP(decl)
        #define SAMPLER_HIGHP(index, decl) SAMPLER(index, mediump decl)
    #endif
#endif

// Compatible texture samplers for GL3
#ifdef GL3
    #define texture2D texture
    #define texture2DProj textureProj
    #define texture3D texture
    #define textureCube texture
    #define textureCubeLod textureLod
    #define texture2DLod textureLod
    #define texture2DLodOffset textureLodOffset
#endif


/// =================================== Consolidate ShaderProgramCompositor defines ===================================

/// Default to 1 cascade if not defined.
#ifndef URHO3D_MAX_SHADOW_CASCADES
    #define URHO3D_MAX_SHADOW_CASCADES 1
#endif

/// URHO3D_DEPTH_ONLY_PASS: Whether there's no color output and caller needs only depth.
#if defined(URHO3D_SHADOW_PASS)
    #define URHO3D_DEPTH_ONLY_PASS
#endif

/// URHO3D_LIGHT_PASS: Whether there's active per-pixel light source in this pass (litbase, light and lightvolume passes).
#if defined(URHO3D_LIGHT_DIRECTIONAL) || defined(URHO3D_LIGHT_POINT) || defined(URHO3D_LIGHT_SPOT)
    #define URHO3D_LIGHT_PASS
#endif

#ifdef URHO3D_VERTEX_SHADER
    /// Some geometries require certain vertex attributes.
    #if defined(URHO3D_GEOMETRY_SKINNED)
        #ifndef URHO3D_VERTEX_HAS_BONE_WEIGHTS_AND_INDICES
            #define URHO3D_VERTEX_HAS_BONE_WEIGHTS_AND_INDICES
        #endif
    #endif

    #if defined(URHO3D_GEOMETRY_BILLBOARD) || defined(URHO3D_GEOMETRY_DIRBILLBOARD)
        #ifndef URHO3D_VERTEX_HAS_TEXCOORD1
            #define URHO3D_VERTEX_HAS_TEXCOORD1
        #endif
    #endif

    #if defined(URHO3D_GEOMETRY_DIRBILLBOARD)
        #ifndef URHO3D_VERTEX_HAS_NORMAL
            #define URHO3D_VERTEX_HAS_NORMAL
        #endif
    #endif

    #if defined(URHO3D_GEOMETRY_TRAIL_FACE_CAMERA) || defined(URHO3D_GEOMETRY_TRAIL_BONE)
        #ifndef URHO3D_VERTEX_HAS_TANGENT
            #define URHO3D_VERTEX_HAS_TANGENT
        #endif
    #endif

    /// URHO3D_VERTEX_NORMAL_AVAILABLE: Whether vertex normal in object space is available.
    /// URHO3D_VERTEX_TANGENT_AVAILABLE: Whether vertex tangent in object space is available.
    /// Some geometry types may deduce vertex tangent or normal and don't need them present in actual vertex layout.
    /// Don't rely on URHO3D_VERTEX_HAS_NORMAL and URHO3D_VERTEX_HAS_TANGENT.
    #if defined(URHO3D_GEOMETRY_BILLBOARD) || defined(URHO3D_GEOMETRY_DIRBILLBOARD) || defined(URHO3D_GEOMETRY_TRAIL_FACE_CAMERA) || defined(URHO3D_GEOMETRY_TRAIL_BONE)
        #define URHO3D_VERTEX_NORMAL_AVAILABLE
        #define URHO3D_VERTEX_TANGENT_AVAILABLE
    #else
        #ifdef URHO3D_VERTEX_HAS_NORMAL
            #define URHO3D_VERTEX_NORMAL_AVAILABLE
        #endif
        #ifdef URHO3D_VERTEX_HAS_TANGENT
            #define URHO3D_VERTEX_TANGENT_AVAILABLE
        #endif
    #endif

    /// URHO3D_SHADOW_NORMAL_OFFSET is disabled if vertex normal is not available.
    #if defined(URHO3D_SHADOW_NORMAL_OFFSET) && !defined(URHO3D_VERTEX_NORMAL_AVAILABLE)
        #undef URHO3D_SHADOW_NORMAL_OFFSET
    #endif
#endif // URHO3D_VERTEX_SHADER
