#ifndef _PIXEL_OUTPUT_GLSL_
#define _PIXEL_OUTPUT_GLSL_

#ifdef STAGE_PIXEL_SHADER

#ifdef GL3
    #ifndef NUM_RENDER_TARGETS
        #if defined(DEFERRED)
            #define NUM_RENDER_TARGETS 4
        #elif defined(PREPASS)
            #define NUM_RENDER_TARGETS 2
        #else
            #define NUM_RENDER_TARGETS 1
        #endif
    #endif

    out vec4 fragData[NUM_RENDER_TARGETS];

    #define gl_FragColor fragData[0]
    #define gl_FragData fragData
#endif

#endif

#endif
