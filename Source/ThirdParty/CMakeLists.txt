#
# Copyright (c) 2008-2017 the Urho3D project.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

# Third party warnings are irrelevant to us.
if (MSVC)
    ucm_replace_flag("/W[0-3]" "/W0" REGEX)
else ()
    ucm_add_flags("-w")
endif ()

if (UWP)
    add_definitions (-DUWP=1)
endif ()

add_subdirectory(EASTL)
add_subdirectory(LZ4)
add_subdirectory(PugiXml)
add_subdirectory(rapidjson)
add_subdirectory(STB)
add_subdirectory(IconFontCppHeaders)
add_subdirectory(CLI11)
add_subdirectory(fmt)
add_subdirectory(spdlog)
set (SDL_CMAKE_DEBUG_POSTFIX "" CACHE STRING "" FORCE)
set (SDL_RENDER OFF CACHE BOOL "" FORCE)
add_subdirectory(SDL)
add_subdirectory(ETCPACK)

if (NOT MINI_URHO)
    if (URHO3D_TOOLS)
        add_subdirectory(crunch)
    endif ()
    add_subdirectory(FreeType)
    if (URHO3D_RMLUI)
        add_subdirectory(RmlUi)
    endif ()

    if (URHO3D_GLOW AND DESKTOP)
        add_subdirectory(xatlas)
        set (EMBREE_ISPC_SUPPORT OFF CACHE BOOL "")
        set (EMBREE_TUTORIALS OFF CACHE BOOL "")
        set (EMBREE_STATIC_LIB ON CACHE BOOL "")
        set (EMBREE_TASKING_SYSTEM "INTERNAL" CACHE STRING "")
        set (EMBREE_LIBRARY_NAME "embree" CACHE STRING "")
        set (EMBREE_STATIC_RUNTIME ${URHO3D_STATIC_RUNTIME} CACHE BOOL "")
        set (EMBREE_MAX_ISA "NONE" CACHE STRING "")
        set (EMBREE_RAY_MASK ON CACHE BOOL "")
        set (EMBREE_ISA_SSE2 ON CACHE BOOL "")
        set (EMBREE_ISA_SSE42 OFF CACHE BOOL "")
        set (EMBREE_ISA_AVX OFF CACHE BOOL "")
        set (EMBREE_ISA_AVX2 OFF CACHE BOOL "")
        set (EMBREE_ISA_AVX512KNL OFF CACHE BOOL "")
        set (EMBREE_ISA_AVX512SKX OFF CACHE BOOL "")
        set (EMBREE_GEOMETRY_CURVE OFF CACHE BOOL "")
        set (EMBREE_GEOMETRY_GRID OFF CACHE BOOL "")
        set (EMBREE_GEOMETRY_INSTANCE OFF CACHE BOOL "")
        set (EMBREE_GEOMETRY_POINT OFF CACHE BOOL "")
        set (EMBREE_GEOMETRY_QUAD OFF CACHE BOOL "")
        set (EMBREE_GEOMETRY_SUBDIVISION OFF CACHE BOOL "")
        set (EMBREE_GEOMETRY_USER OFF CACHE BOOL "")
        set (EMBREE_API_NAMESPACE "embree3" CACHE STRING "")
        add_subdirectory(embree)
    endif ()

    if (URHO3D_SPIRV)
        set (ENABLE_HLSL OFF CACHE BOOL "")
        set (ENABLE_CTEST OFF CACHE BOOL "")
        set (ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "")
        set (BUILD_EXTERNAL OFF CACHE BOOL "")
        set (SKIP_GLSLANG_INSTALL ON CACHE BOOL "")
        add_subdirectory(glslang)

        set (SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "")
        set (SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "")
        add_subdirectory(SPIRV-Cross)
    endif ()

    if (URHO3D_SYSTEMUI)
        add_subdirectory(imgui)
    endif ()

    if (URHO3D_NETWORK)
        add_subdirectory (Civetweb)
        add_subdirectory (SLikeNet)
    endif ()

    if (URHO3D_IK)
        add_subdirectory (ik)
    endif ()

    if (URHO3D_NAVIGATION)
        add_subdirectory (Detour)
        add_subdirectory (DetourCrowd)
        add_subdirectory (DetourTileCache)
        add_subdirectory (Recast)
    endif ()

    if (URHO3D_URHO2D)
        add_subdirectory (Box2D)
    endif ()

    if (URHO3D_WEBP)
        add_subdirectory (WebP)
    endif ()

    if (URHO3D_PHYSICS)
        add_subdirectory (Bullet)
        add_subdirectory (StanHull)
    endif ()

    if (URHO3D_TOOLS)
        add_subdirectory(nativefiledialog)
    endif ()

    if (NOT ANDROID AND NOT ARM AND NOT WEB)
        if (URHO3D_OPENGL)
            add_subdirectory (GLEW)
        elseif (URHO3D_D3D9)
            add_subdirectory (MojoShader)
        endif ()
    endif ()

    if (URHO3D_PROFILING)
        if (NOT TARGET GLEW AND URHO3D_TOOLS)
            add_subdirectory (GLEW)
        endif ()
        set (CAPSTONE_BUILD_STATIC_RUNTIME ${URHO3D_STATIC_RUNTIME})
        set (CAPSTONE_BUILD_STATIC ON)
        set (CAPSTONE_BUILD_SHARED OFF)
        set (CAPSTONE_BUILD_TESTS OFF)
        set (CAPSTONE_BUILD_CSTOOL OFF)
        set (CAPSTONE_ARCHITECTURE_DEFAULT OFF)
        set (CAPSTONE_X86_SUPPORT ON)
        set (CAPSTONE_ARM_SUPPORT ON)
        set (CAPSTONE_ARM64_SUPPORT ON)
        set (CAPSTONE_X86_ATT_DISABLE ON)
        add_subdirectory (capstone)
    endif ()
    # Tracy's headers are included even in non-profiled builds in order to provide stub profiler macros.
    add_subdirectory (tracy)

    if (URHO3D_TOOLS)
        add_subdirectory(Assimp)
    endif ()
endif ()

if (URHO3D_CSHARP_TOOLS AND DESKTOP)
    # When crosscompiling, swig will be built as part of native tools build. See Source/CMakeLists.txt.
    add_subdirectory(swig)
endif ()

if (DESKTOP AND NOT LINUX)
    add_subdirectory (LibCpuId)
endif ()

vs_group_subdirectory_targets(${CMAKE_CURRENT_SOURCE_DIR} ThirdParty)
