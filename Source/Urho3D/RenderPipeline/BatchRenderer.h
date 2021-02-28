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

#include "../Core/Object.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/GraphicsDefs.h"

#include <EASTL/span.h>

namespace Urho3D
{

class Camera;
class DrawableProcessor;
class InstancingBufferCompositor;
class Texture;
struct PipelineBatchBackToFront;
struct PipelineBatchByState;

/// Ambient lighting mode.
enum class AmbientMode
{
    Constant,
    Flat,
    Directional,
};

/// Batch renderer settings.
struct BatchRendererSettings
{
    /// Whether to apply gamma correction.
    bool gammaCorrection_{};

    /// Ambient mode.
    AmbientMode ambientMode_{ AmbientMode::Directional };
    /// Variance shadow map parameters.
    Vector2 vsmShadowParams_{ 0.0000001f, 0.9f };

    /// Calculate pipeline state hash.
    unsigned CalculatePipelineStateHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, gammaCorrection_);
        CombineHash(hash, MakeHash(ambientMode_));
        return hash;
    }

    /// Compare settings.
    bool operator==(const BatchRendererSettings& rhs) const
    {
        return gammaCorrection_ == rhs.gammaCorrection_
            && ambientMode_ == rhs.ambientMode_
            && vsmShadowParams_ == rhs.vsmShadowParams_;
    }

    /// Compare settings.
    bool operator!=(const BatchRendererSettings& rhs) const { return !(*this == rhs); }
};

/// Batch rendering flags.
enum class BatchRenderFlag
{
    /// Default null flag.
    None = 0,
    /// Export ambient light.
    AmbientLight = 1 << 0,
    /// Export vertex lights.
    VertexLights = 1 << 1,
    /// Export pixel light.
    PixelLight = 1 << 2,
    /// Use instancing for static geometry.
    InstantiateStaticGeometry = 1 << 3,
};

URHO3D_FLAGSET(BatchRenderFlag, BatchRenderFlags);

/// Light volume batch rendering context.
struct LightVolumeRenderContext
{
    /// Geometry buffer resources.
    ea::span<const ShaderResourceDesc> geometryBuffer_;
    /// Geometry buffer offset and scale.
    Vector4 geometryBufferOffsetAndScale_;
    /// Geometry buffer inverse scale.
    Vector2 geometryBufferInvSize_;
};

/// Utility class to convert pipeline batches into sequence of draw commands.
class URHO3D_API BatchRenderer : public Object
{
    URHO3D_OBJECT(BatchRenderer, Object);

public:
    /// Construct.
    BatchRenderer(Context* context, const DrawableProcessor* drawableProcessor,
        InstancingBufferCompositor* instancingBuffer);

    /// Set settings.
    void SetSettings(const BatchRendererSettings& settings);

    /// Render batches (sorted by state).
    void RenderBatches(DrawCommandQueue& drawQueue, const Camera* camera, BatchRenderFlags flags,
        ea::span<const PipelineBatchByState> batches);
    /// Render batches (sorted by distance).
    void RenderBatches(DrawCommandQueue& drawQueue, const Camera* camera, BatchRenderFlags flags,
        ea::span<const PipelineBatchBackToFront> batches);

    /// Render light volume batches for deferred rendering.
    void RenderLightVolumeBatches(DrawCommandQueue& drawQueue, Camera* camera,
        const LightVolumeRenderContext& ctx, ea::span<const PipelineBatchByState> batches);

private:
    /// Renderer subsystem.
    Renderer* renderer_{};
    /// Drawable processor.
    const DrawableProcessor* drawableProcessor_{};
    /// Instancing buffer.
    InstancingBufferCompositor* instancingBuffer_{};

    /// Settings.
    BatchRendererSettings settings_;
};

}
