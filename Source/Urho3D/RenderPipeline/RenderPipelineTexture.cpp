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

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/Mutex.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/PipelineState.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/RenderSurface.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/TextureCube.h"
#include "../Graphics/Viewport.h"
#include "../RenderPipeline/RenderPipeline.h"
#include "../RenderPipeline/RenderPipelineTexture.h"
#include "../IO/Log.h"

#include <EASTL/unordered_set.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Return existing or allocate new default depth-stencil for given render target.
RenderSurface* GetDepthStencil(Renderer* renderer, RenderSurface* renderTarget)
{
    // If using the backbuffer, return the backbuffer depth-stencil
    if (!renderTarget)
        return nullptr;

    // Then check for linked depth-stencil
    if (RenderSurface* linkedDepthStencil = renderTarget->GetLinkedDepthStencil())
        return linkedDepthStencil;

    // Finally get one from Renderer
    return renderer->GetDepthStencil(renderTarget->GetWidth(), renderTarget->GetHeight(),
        renderTarget->GetMultiSample(), renderTarget->GetAutoResolve());
}

/// Calculate optimal size for render target.
IntVector2 CalculateRenderTargetSize(const IntRect& viewportRect, const Vector2& sizeMultiplier, const IntVector2& explicitSize)
{
    if (explicitSize != IntVector2::ZERO)
        return explicitSize;
    const IntVector2 viewportSize = viewportRect.Size();
    return VectorMax(IntVector2::ONE, VectorRoundToInt(static_cast<Vector2>(viewportSize) * sizeMultiplier));
}

/// Return render surface of texture.
RenderSurface* GetRenderSurfaceFromTexture(Texture* texture, CubeMapFace face = FACE_POSITIVE_X)
{
    if (!texture)
        return nullptr;

    if (texture->GetType() == Texture2D::GetTypeStatic())
        return static_cast<Texture2D*>(texture)->GetRenderSurface();
    else if (texture->GetType() == TextureCube::GetTypeStatic())
        return static_cast<TextureCube*>(texture)->GetRenderSurface(face);
    else
        return nullptr;
}

/// Create pipeline state for copying textures.
SharedPtr<PipelineState> CreateCopyTexturePipelineState(
    Graphics* graphics, Renderer* renderer, bool constantBuffersEnabled)
{
    Geometry* quadGeometry = renderer->GetQuadGeometry();

    PipelineStateDesc desc;
    desc.vertexElements_ = quadGeometry->GetVertexBuffer(0)->GetElements();
    desc.indexType_ = IndexBuffer::GetIndexBufferType(quadGeometry->GetIndexBuffer());
    desc.primitiveType_ = TRIANGLE_LIST;
    desc.colorWrite_ = true;

    static const char* shaderName = "v2/CopyFramebuffer";
    static const char* defines = constantBuffersEnabled ? "URHO3D_USE_CBUFFERS " : "";
    desc.vertexShader_ = graphics->GetShader(VS, shaderName, defines);
    desc.pixelShader_ = graphics->GetShader(PS, shaderName, defines);
    return renderer->GetOrCreatePipelineState(desc);
}

/// Calculate offset and scale of viewport within texture.
Vector4 CalculateViewportOffsetAndScale(const IntVector2& textureSize, const IntRect& viewportRect)
{
    const Vector2 halfViewportScale = 0.5f * static_cast<Vector2>(viewportRect.Size()) / static_cast<Vector2>(textureSize);
    const float xOffset = static_cast<float>(viewportRect.left_) / textureSize.x_ + halfViewportScale.x_;
    const float yOffset = static_cast<float>(viewportRect.top_) / textureSize.y_ + halfViewportScale.y_;
#ifdef URHO3D_OPENGL
    return { xOffset, 1.0f - yOffset, halfViewportScale.x_, halfViewportScale.y_ };
#else
    return { xOffset, yOffset, halfViewportScale.x_, halfViewportScale.y_ };
#endif
}

/// Thread safe provider of unique indexes.
class ThreadSafeIndex
{
public:
    /// Allocate index for object.
    unsigned Allocate()
    {
        MutexLock<Mutex> lock(mutex_);

        // Scroll next index until get a free one.
        while (allocated_.contains(nextIndex_) || nextIndex_ == 0)
            ++nextIndex_;

        allocated_.insert(nextIndex_);
        return nextIndex_++;
    }

    /// Release index.
    void Release(unsigned index)
    {
        MutexLock<Mutex> lock(mutex_);

        assert(allocated_.contains(index));
        allocated_.erase(index);
    }

private:
    /// Mutex that protects map and index.
    Mutex mutex_;
    /// Next index.
    unsigned nextIndex_{};
    /// Map from index to object.
    ea::unordered_set<unsigned> allocated_;
};

/// Persistent screen buffers.
static ThreadSafeIndex persistentScreenBuffers;

}

RenderPipelineTexture::RenderPipelineTexture(RenderPipeline* renderPipeline)
    : Object(renderPipeline->GetContext())
    , renderer_(GetSubsystem<Renderer>())
    , graphics_(GetSubsystem<Graphics>())
    , drawQueue_(renderPipeline->GetDefaultDrawQueue())
{
    renderPipeline->OnRenderBegin.Subscribe(this, &RenderPipelineTexture::OnRenderBegin);
}

RenderPipelineTexture::~RenderPipelineTexture()
{
}

bool RenderPipelineTexture::IsCompatibleWith(RenderPipelineTexture* otherTexture) const
{
    RenderSurface* thisSurface = GetRenderSurface();
    RenderSurface* otherSurface = otherTexture->GetRenderSurface();

#ifdef URHO3D_OPENGL
    // Due to FBO limitations, in OpenGL default color and depth surfaces cannot be mixed with custom ones
    if (!!thisSurface != !!otherSurface)
        return false;
#endif

    // If multisampling levels are different, textures are incompatible
    if (RenderSurface::GetMultiSample(graphics_, thisSurface) != RenderSurface::GetMultiSample(graphics_, otherSurface))
        return false;

    // If sizes are different, textures are incompatible
    // TODO(renderer): This limitation may be lifted
    if (GetViewportRect() != otherTexture->GetViewportRect())
        return false;

    return true;
}

void RenderPipelineTexture::ClearColor(const Color& color, CubeMapFace face)
{
    RenderSurface* renderSurface = GetRenderSurface(face);
    if (renderSurface && renderSurface->GetUsage() == TEXTURE_DEPTHSTENCIL)
    {
        URHO3D_LOGERROR("Cannot clear color for depth-stencil texture");
        return;
    }

    SetRenderTarget(renderSurface);
    graphics_->SetViewport(GetViewportRect());
    graphics_->Clear(CLEAR_COLOR, color);
}

void RenderPipelineTexture::ClearDepthStencil(float depth, unsigned stencil, CubeMapFace face)
{
    RenderSurface* renderSurface = GetRenderSurface(face);
    if (renderSurface && renderSurface->GetUsage() == TEXTURE_RENDERTARGET)
    {
        URHO3D_LOGERROR("Cannot clear depth-stencil for color texture");
        return;
    }

    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
        graphics_->ResetRenderTarget(i);
    graphics_->SetDepthStencil(renderSurface);
    graphics_->SetViewport(GetViewportRect());
    graphics_->Clear(CLEAR_DEPTH | CLEAR_STENCIL, Color::TRANSPARENT_BLACK, depth, stencil);
}

void RenderPipelineTexture::SetRenderTargetsRegion(const IntRect& viewportRect,
    ea::span<RenderPipelineTexture* const> colorTextures, CubeMapFace face)
{
    if (colorTextures.size() > MAX_RENDERTARGETS)
    {
        URHO3D_LOGERROR("Too many render targets set");
        return;
    }

    // Check texture compatibility
    if (!colorTextures.empty())
    {
        if (!colorTextures[0]->IsCompatibleWith(this))
        {
            URHO3D_LOGERROR("Color texture #0 is incompatible with depth buffer");
            return;
        }

        for (unsigned i = 1; i < colorTextures.size(); ++i)
        {
            if (!colorTextures[0]->IsCompatibleWith(colorTextures[i]))
            {
                URHO3D_LOGERROR("Color texture #0 is incompatible with color texture #{}", i);
                return;
            }
        }
    }

    RenderSurface* depthStencilSurface = GetRenderSurface(face);
    RenderSurface* colorSurfaces[MAX_RENDERTARGETS]{};
    for (unsigned i = 0; i < colorTextures.size(); ++i)
    {
        colorSurfaces[i] = colorTextures[i]->GetRenderSurface(face);
        if (!colorSurfaces[i] && i != 0)
        {
            URHO3D_LOGERROR("Default color texture can be bound only to slot #0");
            return;
        }
    }

    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
        graphics_->SetRenderTarget(i, colorSurfaces[i]);
    graphics_->SetDepthStencil(depthStencilSurface);
    const IntRect effectiveViewportRect = viewportRect == IntRect::ZERO ? GetViewportRect() : viewportRect;
    graphics_->SetViewport(effectiveViewportRect);

}

void RenderPipelineTexture::SetRenderTargets(
    ea::span<RenderPipelineTexture* const> colorTextures, CubeMapFace face)
{
    SetRenderTargetsRegion(IntRect::ZERO, colorTextures, face);
}

void RenderPipelineTexture::SetRenderTargetsRegion(const IntRect& viewportRect,
    std::initializer_list<RenderPipelineTexture*> colorTextures, CubeMapFace face)
{
    SetRenderTargetsRegion(viewportRect, { colorTextures.begin(), static_cast<unsigned>(colorTextures.size()) }, face);
}

void RenderPipelineTexture::SetRenderTargets(
    std::initializer_list<RenderPipelineTexture*> colorTextures, CubeMapFace face)
{
    SetRenderTargets({ colorTextures.begin(), static_cast<unsigned>(colorTextures.size()) }, face);
}

void RenderPipelineTexture::CopyRegionFrom(Texture* sourceTexture, const IntRect& sourceViewportRect,
    CubeMapFace destinationFace, const IntRect& destinationViewportRect, bool flipVertical)
{
    Geometry* quadGeometry = renderer_->GetQuadGeometry();
    Matrix3x4 modelMatrix = Matrix3x4::IDENTITY;
    Matrix4 projection = Matrix4::IDENTITY;
    if (flipVertical)
        projection.m11_ = -1.0f;
#ifdef URHO3D_OPENGL
    modelMatrix.m23_ = 0.0f;
#else
    modelMatrix.m23_ = 0.5f;
#endif

    drawQueue_->Reset();
    drawQueue_->SetPipelineState(copyPipelineState_);
    if (drawQueue_->BeginShaderParameterGroup(SP_CAMERA))
    {
        const Vector4 offsetAndScale = CalculateViewportOffsetAndScale(sourceTexture->GetSize(), sourceViewportRect);
        const auto invSize = Vector2::ONE / static_cast<Vector2>(sourceTexture->GetSize());
        drawQueue_->AddShaderParameter(VSP_GBUFFEROFFSETS, offsetAndScale);
        drawQueue_->AddShaderParameter(PSP_GBUFFERINVSIZE, invSize);
        drawQueue_->AddShaderParameter(VSP_VIEWPROJ, projection);
        drawQueue_->CommitShaderParameterGroup(SP_CAMERA);
    }
    if (drawQueue_->BeginShaderParameterGroup(SP_OBJECT))
    {
        drawQueue_->AddShaderParameter(VSP_MODEL, modelMatrix);
        drawQueue_->CommitShaderParameterGroup(SP_OBJECT);
    }

    drawQueue_->AddShaderResource(TU_DIFFUSE, sourceTexture);
    drawQueue_->CommitShaderResources();
    drawQueue_->SetBuffers(quadGeometry->GetVertexBuffer(0), quadGeometry->GetIndexBuffer());
    drawQueue_->DrawIndexed(quadGeometry->GetIndexStart(), quadGeometry->GetIndexCount());

    SetRenderTarget(GetRenderSurface(destinationFace));
    graphics_->SetViewport(destinationViewportRect);
    drawQueue_->Execute();
}

void RenderPipelineTexture::CopyFrom(RenderPipelineTexture* texture,
    CubeMapFace destinationFace, const IntRect& destinationViewportRect, bool flipVertical)
{
    Texture* sourceTexture = texture->GetTexture();
    const IntRect sourceRect = texture->GetViewportRect();
    CopyRegionFrom(sourceTexture, sourceRect, destinationFace, destinationViewportRect, flipVertical);
}

void RenderPipelineTexture::CopyFrom(RenderPipelineTexture* texture, bool flipVertical)
{
    CopyFrom(texture, FACE_POSITIVE_X, GetViewportRect(), flipVertical);
}

IntVector2 RenderPipelineTexture::GetSize() const
{
    return RenderSurface::GetSize(graphics_, GetRenderSurface());
}

Vector4 RenderPipelineTexture::GetViewportOffsetAndScale(const IntRect& viewportRect) const
{
    const IntVector2 size = GetSize();
    return CalculateViewportOffsetAndScale(size,
        viewportRect != IntRect::ZERO ? viewportRect : IntRect{ IntVector2::ZERO, size });
}

Vector2 RenderPipelineTexture::GetInvSize() const
{
    return Vector2::ONE / static_cast<Vector2>(GetSize());
}

void RenderPipelineTexture::OnRenderBegin(const FrameInfo& frameInfo)
{
    active_ = true;
    const bool constantBuffersEnabled = graphics_->GetConstantBuffersEnabled();
    if (constantBuffersEnabled != currentConstantBuffersEnabled_ || !copyPipelineState_)
    {
        currentConstantBuffersEnabled_ = constantBuffersEnabled;
        copyPipelineState_ = CreateCopyTexturePipelineState(
            graphics_, renderer_, currentConstantBuffersEnabled_);
    }
}

void RenderPipelineTexture::OnRenderEnd(const FrameInfo& frameInfo)
{
    active_ = false;
}

void RenderPipelineTexture::SetRenderTarget(RenderSurface* renderSurface)
{
    graphics_->SetRenderTarget(0, renderSurface);
    for (unsigned i = 1; i < MAX_RENDERTARGETS; ++i)
        graphics_->ResetRenderTarget(i);
    graphics_->SetDepthStencil(GetDepthStencil(renderer_, renderSurface));
}

bool RenderPipelineTexture::CheckRendering() const
{
    if (!active_)
    {
        URHO3D_LOGERROR("Cannot access RenderPipelineTexture outside of RenderPipeline::Render");
        return false;
    }
    return true;
}

ScreenBufferTexture::ScreenBufferTexture(
    RenderPipeline* renderPipeline, const ScreenBufferParams& params,
    const Vector2& sizeMultiplier, const IntVector2& fixedSize, bool persistent)
    : RenderPipelineTexture(renderPipeline)
    , params_(params)
    , sizeMultiplier_(sizeMultiplier)
    , fixedSize_(fixedSize)
{
    if (persistent)
        persistenceKey_ = persistentScreenBuffers.Allocate();
}

ScreenBufferTexture::~ScreenBufferTexture()
{
    if (persistenceKey_)
        persistentScreenBuffers.Release(persistenceKey_);
}

void ScreenBufferTexture::OnRenderBegin(const FrameInfo& frameInfo)
{
    BaseClassName::OnRenderBegin(frameInfo);

    currentSize_ = CalculateRenderTargetSize(frameInfo.viewRect_, sizeMultiplier_, fixedSize_);
    currentTexture_ = renderer_->GetScreenBuffer(currentSize_.x_, currentSize_.y_,
        params_.format_, params_.multiSample_, params_.autoResolve_,
        params_.cubemap_, params_.filtered_, params_.sRGB_,
        persistenceKey_);
}

Texture* ScreenBufferTexture::GetTexture() const
{
    return CheckRendering() ? currentTexture_ : nullptr;
}

RenderSurface* ScreenBufferTexture::GetRenderSurface(CubeMapFace face) const
{
    return CheckRendering() ? GetRenderSurfaceFromTexture(currentTexture_, face) : nullptr;
}

IntRect ScreenBufferTexture::GetViewportRect() const
{
    return CheckRendering() ? IntRect{ IntVector2::ZERO, currentSize_ } : IntRect::ZERO;
}

ViewportColorTexture::ViewportColorTexture(RenderPipeline* renderPipeline)
    : RenderPipelineTexture(renderPipeline)
{
}

void ViewportColorTexture::ClearDepthStencil(float depth, unsigned stencil, CubeMapFace face)
{
    URHO3D_LOGERROR("Cannot clear depth-stencil for color texture");
}

Texture* ViewportColorTexture::GetTexture() const
{
    return CheckRendering() ? renderTarget_->GetParentTexture() : nullptr;
}

RenderSurface* ViewportColorTexture::GetRenderSurface(CubeMapFace face) const
{
    return CheckRendering() ? renderTarget_ : nullptr;
}

void ViewportColorTexture::OnRenderBegin(const FrameInfo& frameInfo)
{
    BaseClassName::OnRenderBegin(frameInfo);
    renderTarget_ = frameInfo.renderTarget_;
    viewportRect_ = frameInfo.viewRect_;
}

ViewportDepthStencilTexture::ViewportDepthStencilTexture(RenderPipeline* renderPipeline)
    : RenderPipelineTexture(renderPipeline)
{
}

void ViewportDepthStencilTexture::ClearColor(const Color& color, CubeMapFace face)
{
    URHO3D_LOGERROR("Cannot clear color for depth-stencil texture");
}

Texture* ViewportDepthStencilTexture::GetTexture() const
{
    return CheckRendering() ? renderTarget_->GetParentTexture() : nullptr;
}

RenderSurface* ViewportDepthStencilTexture::GetRenderSurface(CubeMapFace face) const
{
    return CheckRendering() ? renderTarget_ : nullptr;
}

void ViewportDepthStencilTexture::OnRenderBegin(const FrameInfo& frameInfo)
{
    BaseClassName::OnRenderBegin(frameInfo);
    renderTarget_ = GetDepthStencil(renderer_, frameInfo.renderTarget_);
    viewportRect_ = frameInfo.viewRect_;
}

}
