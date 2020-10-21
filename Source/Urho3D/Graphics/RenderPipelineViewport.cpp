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
#include "../Graphics/Graphics.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/RenderSurface.h"
#include "../Graphics/RenderPipelineViewport.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/TextureCube.h"
#include "../IO/Log.h"

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

/// Return size of render target.
IntVector2 GetRenderTargetSize(const IntRect& viewportRect, const Vector2& sizeMultiplier, const IntVector2& explicitSize)
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

/// Return common size or null if not applicable.
IntVector2 GetCommonSize(RenderSurface* depthStencilSurface, ea::span<RenderSurface* const> colorSurfaces)
{
    IntVector2 result;
    if (depthStencilSurface)
        result = { depthStencilSurface->GetWidth(), depthStencilSurface->GetHeight() };
    for (RenderSurface* colorSurface : colorSurfaces)
    {
        if (!colorSurface)
            continue;
        if (result == IntVector2::ZERO)
            result = { colorSurface->GetWidth(), colorSurface->GetHeight() };
        else if (result.x_ != colorSurface->GetWidth() || result.y_ != colorSurface->GetHeight())
            return IntVector2::ZERO;
    }
    return result;
}

}

/// Return GBuffer offsets.
Vector4 RenderPipelineViewport::GetGBufferOffsets(const IntVector2& textureSize, const IntRect& viewportRect)
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

/// Return GBuffer inverted size.
Vector2 RenderPipelineViewport::GetGBufferInvSize(const IntVector2& textureSize)
{
    return { 1.0f / textureSize.x_, 1.0f / textureSize.y_ };
}

RenderPipelineViewport::RenderPipelineViewport(Context* context)
    : Object(context)
    , graphics_(context_->GetSubsystem<Graphics>())
    , renderer_(context_->GetSubsystem<Renderer>())
{
    {
        Geometry* quadGeometry = renderer_->GetQuadGeometry();

        PipelineStateDesc desc;
        desc.vertexElements_ = quadGeometry->GetVertexBuffer(0)->GetElements();
        desc.indexType_ = IndexBuffer::GetIndexBufferType(quadGeometry->GetIndexBuffer());
        desc.primitiveType_ = TRIANGLE_LIST;
        desc.colorWrite_ = true;

        static const char* shaderName = "CopyFramebuffer";
        desc.vertexShader_ = graphics_->GetShader(VS, shaderName);
        desc.pixelShader_ = graphics_->GetShader(PS, shaderName);
        copyRenderTargetPipelineState_ = renderer_->GetOrCreatePipelineState(desc);
    }
}

void RenderPipelineViewport::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    viewport_ = viewport;
    viewportRenderTarget_ = renderTarget;
}

void RenderPipelineViewport::ResetRenderTargets()
{
    renderTargets_.clear();
}

void RenderPipelineViewport::AddRenderTarget(const RenderTargetDesc& renderTarget)
{
    renderTargets_.push_back(renderTarget);
}

void RenderPipelineViewport::AddRenderTarget(const ea::string& name, const ea::string& format)
{
    RenderTargetDesc renderTarget;
    renderTarget.name_ = name;
    renderTarget.format_ = Graphics::GetFormat(format);
    AddRenderTarget(renderTarget);
}

void RenderPipelineViewport::BeginFrame()
{
    // Update viewport rect
    if (viewport_->GetRect() != IntRect::ZERO)
        viewportRect_ = viewport_->GetRect();
    else
        viewportRect_ = { IntVector2::ZERO, graphics_->GetRenderTargetDimensions() };

    // Update pipeline state inputs
    // TODO(renderer): Support multiple cameras
    cullCamera_ = viewport_->GetCamera();
    constantBuffersEnabled_ = graphics_->GetConstantBuffersEnabled();
    MarkPipelineStateHashDirty();

#ifdef URHO3D_OPENGL
    // On OpenGL, flip the projection if rendering to a texture so that the texture can be addressed in the same way
    // as a render texture produced on Direct3D
    if (viewportRenderTarget_)
    {
        if (cullCamera_)
            cullCamera_->SetFlipVertical(!cullCamera_->GetFlipVertical());
    }
#endif

    // Prepare all screen buffers
    viewportDepth_ = GetDepthStencil(renderer_, viewportRenderTarget_);
    renderTargetTextures_.clear();
    for (const RenderTargetDesc& desc : renderTargets_)
    {
        const IntVector2 size = GetRenderTargetSize(viewportRect_, desc.sizeMultiplier_, desc.fixedSize_);

        Texture* screenBuffer = renderer_->GetScreenBuffer(size.x_, size.y_, desc.format_, desc.multiSample_, desc.autoResolve_,
            desc.cubemap_, desc.filtered_, desc.sRGB_, desc.persistenceKey_);

        renderTargetTextures_[desc.name_] = screenBuffer;
    }
}

Texture* RenderPipelineViewport::GetRenderTarget(StringHash name) const
{
    auto iter = renderTargetTextures_.find(name);
    if (iter != renderTargetTextures_.end())
        return iter->second;
    return nullptr;
}

void RenderPipelineViewport::SetRenderTargets(ea::string_view depthStencil, ea::span<const ea::string_view> renderTargets)
{
    SetRenderTargets(depthStencil, renderTargets, IntRect::ZERO, FACE_POSITIVE_X);
}

void RenderPipelineViewport::SetRenderTargets(ea::string_view depthStencil, ea::span<const ea::string_view> renderTargets,
    const IntRect& viewportRect, CubeMapFace face)
{
    if (renderTargets.size() > MAX_RENDERTARGETS)
    {
        URHO3D_LOGERROR("Too many render targets set");
        return;
    }

    RenderSurface* depthStencilSurface = GetRenderSurfaceFromTexture(GetRenderTarget(depthStencil), face);
    if (!depthStencilSurface && !depthStencil.empty())
    {
        URHO3D_LOGERROR("Cannot find depth-stencil render target '{}'", depthStencil);
        return;
    }

    RenderSurface* colorSurfaces[MAX_RENDERTARGETS]{};
    for (unsigned i = 0; i < ea::min<unsigned>(renderTargets.size(), MAX_RENDERTARGETS); ++i)
    {
        colorSurfaces[i] = GetRenderSurfaceFromTexture(GetRenderTarget(renderTargets[i]), face);
        if (!colorSurfaces[i])
        {
            URHO3D_LOGERROR("Cannot find color render target '{}'", renderTargets[i]);
            return;
        }
    }

    const IntVector2 commonSize = GetCommonSize(depthStencilSurface, colorSurfaces);
    if (commonSize == IntVector2::ZERO && viewportRect == IntRect::ZERO)
    {
        URHO3D_LOGERROR("Cannot automatically determine viewport size");
        return;
    }

    const IntRect actualViewportRect = viewportRect == IntRect::ZERO ? IntRect{ IntVector2::ZERO, commonSize } : viewportRect;

    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
        graphics_->SetRenderTarget(i, colorSurfaces[i]);
    graphics_->SetDepthStencil(depthStencilSurface);
    graphics_->SetViewport(actualViewportRect);
}

void RenderPipelineViewport::ClearRenderTarget(ea::string_view renderTarget, const Color& color, CubeMapFace face)
{
    RenderSurface* colorSurface = GetRenderSurfaceFromTexture(GetRenderTarget(renderTarget), face);
    if (!colorSurface || colorSurface->GetUsage() != TEXTURE_RENDERTARGET)
    {
        URHO3D_LOGERROR("Cannot find render target '{}' to clear", renderTarget);
        return;
    }

    graphics_->SetRenderTarget(0, colorSurface);
    for (unsigned i = 1; i < MAX_RENDERTARGETS; ++i)
        graphics_->ResetRenderTarget(i);
    graphics_->ResetDepthStencil();
    graphics_->SetViewport({ 0, 0, colorSurface->GetWidth(), colorSurface->GetHeight() });
    graphics_->Clear(CLEAR_COLOR, color);
}

void RenderPipelineViewport::ClearDepthStencil(ea::string_view depthStencil, float depth, unsigned stencil)
{
    RenderSurface* depthStencilSurface = GetRenderSurfaceFromTexture(GetRenderTarget(depthStencil));
    if (!depthStencilSurface || depthStencilSurface->GetUsage() != TEXTURE_DEPTHSTENCIL)
    {
        URHO3D_LOGERROR("Cannot find depth-stencil '{}' to clear", depthStencil);
        return;
    }

    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
        graphics_->ResetRenderTarget(i);
    graphics_->SetDepthStencil(depthStencilSurface);
    graphics_->SetViewport({ 0, 0, depthStencilSurface->GetWidth(), depthStencilSurface->GetHeight() });
    graphics_->Clear(CLEAR_DEPTH | CLEAR_STENCIL, Color::TRANSPARENT_BLACK, depth, stencil);
}

void RenderPipelineViewport::SetViewportRenderTargets(ClearTargetFlags clear, const Color& color, float depth, unsigned stencil)
{
    graphics_->SetRenderTarget(0, viewportRenderTarget_);
    for (unsigned i = 1; i < MAX_RENDERTARGETS; ++i)
        graphics_->ResetRenderTarget(i);
    graphics_->SetDepthStencil(viewportDepth_);
    graphics_->SetViewport(viewportRect_);
    if (clear != CLEAR_NONE)
        graphics_->Clear(clear, color, depth, stencil);
}

void RenderPipelineViewport::SetViewportRenderTargets()
{
    SetViewportRenderTargets(CLEAR_NONE, Color::TRANSPARENT_BLACK, 1.0f, 0);
}

void RenderPipelineViewport::CopyToRenderTarget(Texture* sourceTexture, RenderSurface* destinationSurface,
    const IntRect& sourceViewportRect, const IntRect& destinationViewportRect, bool flipVertical)
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

    drawQueue_.Reset(graphics_, false);
    drawQueue_.SetPipelineState(copyRenderTargetPipelineState_);
    if (drawQueue_.BeginShaderParameterGroup(SP_CAMERA))
    {
        drawQueue_.AddShaderParameter(VSP_GBUFFEROFFSETS, GetGBufferOffsets(sourceTexture->GetSize(), sourceViewportRect));
        drawQueue_.AddShaderParameter(PSP_GBUFFERINVSIZE, GetGBufferInvSize(sourceTexture->GetSize()));
        drawQueue_.AddShaderParameter(VSP_VIEWPROJ, projection);
        drawQueue_.CommitShaderParameterGroup(SP_CAMERA);
    }
    if (drawQueue_.BeginShaderParameterGroup(SP_OBJECT))
    {
        drawQueue_.AddShaderParameter(VSP_MODEL, modelMatrix);
        drawQueue_.CommitShaderParameterGroup(SP_OBJECT);
    }

    drawQueue_.AddShaderResource(TU_DIFFUSE, sourceTexture);
    drawQueue_.CommitShaderResources();
    drawQueue_.SetBuffers(quadGeometry->GetVertexBuffer(0), quadGeometry->GetIndexBuffer());
    drawQueue_.DrawIndexed(quadGeometry->GetIndexStart(), quadGeometry->GetIndexCount());

    graphics_->SetRenderTarget(0, destinationSurface);
    for (unsigned i = 1; i < MAX_RENDERTARGETS; ++i)
        graphics_->ResetRenderTarget(i);
    graphics_->ResetDepthStencil();
    graphics_->SetViewport(destinationViewportRect);
    drawQueue_.Execute(graphics_);
}

void RenderPipelineViewport::CopyToRenderTarget(ea::string_view sourceRenderTarget, ea::string_view destinationRenderTarget,
    CubeMapFace face)
{
    Texture* sourceTexture = GetRenderTarget(sourceRenderTarget);
    Texture* destinationTexture = GetRenderTarget(destinationRenderTarget);
    RenderSurface* destinationSurface = GetRenderSurfaceFromTexture(destinationTexture, face);
    if (!sourceTexture)
    {
        URHO3D_LOGERROR("Cannot find source render target '{}' to copy from", sourceRenderTarget);
        return;
    }
    if (!destinationSurface)
    {
        URHO3D_LOGERROR("Cannot find destination render target '{}' to copy to", destinationRenderTarget);
        return;
    }

    const IntRect sourceRect{ IntVector2::ZERO, sourceTexture->GetSize() };
    const IntRect destinationRect{ IntVector2::ZERO, destinationTexture->GetSize() };
    CopyToRenderTarget(sourceTexture, destinationSurface, sourceRect, destinationRect, false);
}

void RenderPipelineViewport::CopyToViewportRenderTarget(ea::string_view sourceRenderTarget)
{
    Texture* sourceTexture = GetRenderTarget(sourceRenderTarget);
    if (!sourceTexture)
    {
        URHO3D_LOGERROR("Cannot find source render target '{}' to copy from", sourceRenderTarget);
        return;
    }

    const IntRect sourceRect{ IntVector2::ZERO, sourceTexture->GetSize() };
    CopyToRenderTarget(sourceTexture, viewportRenderTarget_, sourceRect, viewportRect_, cullCamera_->GetFlipVertical());
}

void RenderPipelineViewport::EndFrame()
{
    cachedPipelineStateHash_ = GetPipelineStateHash();

#ifdef URHO3D_OPENGL
    // Undo flip
    if (viewportRenderTarget_)
    {
        if (cullCamera_)
            cullCamera_->SetFlipVertical(!cullCamera_->GetFlipVertical());
    }
#endif

    // Reset transient pointers just in case
    viewportDepth_ = nullptr;
    renderTargetTextures_.clear();
}

unsigned RenderPipelineViewport::RecalculatePipelineStateHash() const
{
    unsigned hash = 0;
    CombineHash(hash, cullCamera_->GetFlipVertical());
    CombineHash(hash, constantBuffersEnabled_);
    return hash;
}

}
