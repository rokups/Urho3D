//
// Copyright (c) 2017-2020 the rbfx project.
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

#pragma once

#include "../RenderPipeline/RenderPipelineDefs.h"

namespace Urho3D
{

/// Built-in shader consts.
/// See _Uniforms.glsl for descriptions.
namespace ShaderConsts
{
    URHO3D_SHADER_CONST(Frame, DeltaTime);
    URHO3D_SHADER_CONST(Frame, ElapsedTime);

    URHO3D_SHADER_CONST(Camera, View);
    URHO3D_SHADER_CONST(Camera, ViewInv);
    URHO3D_SHADER_CONST(Camera, ViewProj);
    URHO3D_SHADER_CONST(Camera, ClipPlane);
    URHO3D_SHADER_CONST(Camera, CameraPos);
    URHO3D_SHADER_CONST(Camera, NearClip);
    URHO3D_SHADER_CONST(Camera, FarClip);
    URHO3D_SHADER_CONST(Camera, DepthMode);
    URHO3D_SHADER_CONST(Camera, FrustumSize);
    URHO3D_SHADER_CONST(Camera, GBufferOffsets);
    URHO3D_SHADER_CONST(Camera, DepthReconstruct);
    URHO3D_SHADER_CONST(Camera, GBufferInvSize);
    URHO3D_SHADER_CONST(Camera, AmbientColor);
    URHO3D_SHADER_CONST(Camera, FogParams);
    URHO3D_SHADER_CONST(Camera, FogColor);
    URHO3D_SHADER_CONST(Camera, NormalOffsetScale);

    URHO3D_SHADER_CONST(Zone, ReflectionAverageColor);
    URHO3D_SHADER_CONST(Zone, RoughnessToLODFactor);

    URHO3D_SHADER_CONST(Light, LightPos);
    URHO3D_SHADER_CONST(Light, LightDir);
    URHO3D_SHADER_CONST(Light, SpotAngle);
    URHO3D_SHADER_CONST(Light, LightShapeMatrix);
    URHO3D_SHADER_CONST(Light, VertexLights);
    URHO3D_SHADER_CONST(Light, LightMatrices);
    URHO3D_SHADER_CONST(Light, LightColor);
    URHO3D_SHADER_CONST(Light, ShadowCubeAdjust);
    URHO3D_SHADER_CONST(Light, ShadowCubeUVBias);
    URHO3D_SHADER_CONST(Light, ShadowDepthFade);
    URHO3D_SHADER_CONST(Light, ShadowIntensity);
    URHO3D_SHADER_CONST(Light, ShadowMapInvSize);
    URHO3D_SHADER_CONST(Light, ShadowSplits);
    URHO3D_SHADER_CONST(Light, VSMShadowParams);
    URHO3D_SHADER_CONST(Light, LightRad);
    URHO3D_SHADER_CONST(Light, LightLength);

    /// Serialized in Material, do not rename
    /// @{
    URHO3D_SHADER_CONST(Material, UOffset);
    URHO3D_SHADER_CONST(Material, VOffset);
    URHO3D_SHADER_CONST(Material, LMOffset);
    URHO3D_SHADER_CONST(Material, MatDiffColor);
    URHO3D_SHADER_CONST(Material, MatEmissiveColor);
    URHO3D_SHADER_CONST(Material, Roughness);
    URHO3D_SHADER_CONST(Material, MatEnvMapColor);
    URHO3D_SHADER_CONST(Material, Metallic);
    URHO3D_SHADER_CONST(Material, MatSpecColor);
    URHO3D_SHADER_CONST(Material, FadeOffsetScale);
    URHO3D_SHADER_CONST(Material, NormalScale);
    URHO3D_SHADER_CONST(Material, DielectricReflectance);
    /// @}

    URHO3D_SHADER_CONST(Object, Model);
    URHO3D_SHADER_CONST(Object, SHAr);
    URHO3D_SHADER_CONST(Object, SHAg);
    URHO3D_SHADER_CONST(Object, SHAb);
    URHO3D_SHADER_CONST(Object, SHBr);
    URHO3D_SHADER_CONST(Object, SHBg);
    URHO3D_SHADER_CONST(Object, SHBb);
    URHO3D_SHADER_CONST(Object, SHC);
    URHO3D_SHADER_CONST(Object, Ambient);
    URHO3D_SHADER_CONST(Object, BillboardRot);
    URHO3D_SHADER_CONST(Object, SkinMatrices);
};

}
