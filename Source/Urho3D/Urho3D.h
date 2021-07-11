//
// Copyright (c) 2008-2017 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#ifndef URHO3D_API_H
#define URHO3D_API_H

#ifdef _WIN32

#ifdef _MSC_VER
#pragma warning(disable: 4251)
#pragma warning(disable: 4275)
#endif

#define URHO3D_EXPORT_API __declspec(dllexport)

#ifdef URHO3D_STATIC
#  define URHO3D_API
#  define URHO3D_NO_EXPORT
#else
#  ifndef URHO3D_API
#    ifdef URHO3D_EXPORTS
        /* We are building this library */
#      define URHO3D_API URHO3D_EXPORT_API
#    else
        /* We are using this library */
#      define URHO3D_API __declspec(dllimport)
#    endif
#  endif

#  ifndef URHO3D_NO_EXPORT
#    define URHO3D_NO_EXPORT
#  endif
#endif

#ifndef URHO3D_DEPRECATED
#  define URHO3D_DEPRECATED __declspec(deprecated)
#endif

#ifndef URHO3D_DEPRECATED_EXPORT
#  define URHO3D_DEPRECATED_EXPORT URHO3D_API URHO3D_DEPRECATED
#endif

#ifndef URHO3D_DEPRECATED_NO_EXPORT
#  define URHO3D_DEPRECATED_NO_EXPORT URHO3D_NO_EXPORT URHO3D_DEPRECATED
#endif

#define DEFINE_NO_DEPRECATED 0
#if DEFINE_NO_DEPRECATED
# define URHO3D_NO_DEPRECATED
#endif

#else

#define URHO3D_EXPORT_API __attribute__((visibility("default")))

#ifdef URHO3D_STATIC
#ifndef URHO3D_API
#  define URHO3D_API
#endif
#  define URHO3D_NO_EXPORT
#else
#  define URHO3D_API URHO3D_EXPORT_API
#  ifndef URHO3D_NO_EXPORT
#    define URHO3D_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef URHO3D_DEPRECATED
#  define URHO3D_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef URHO3D_DEPRECATED_EXPORT
#  define URHO3D_DEPRECATED_EXPORT URHO3D_API URHO3D_DEPRECATED
#endif

#ifndef URHO3D_DEPRECATED_NO_EXPORT
#  define URHO3D_DEPRECATED_NO_EXPORT URHO3D_NO_EXPORT URHO3D_DEPRECATED
#endif

#define DEFINE_NO_DEPRECATED 0
#if DEFINE_NO_DEPRECATED
# define URHO3D_NO_DEPRECATED
#endif

#endif

// @URHO3D_ENGINE_CONFIG@

// Disable SSE if compiler does not support it.
#if defined(URHO3D_SSE) && !defined(__SSE2__) && (!defined(_M_IX86_FP) || _M_IX86_FP < 2)
#   undef URHO3D_SSE
#endif

#endif
