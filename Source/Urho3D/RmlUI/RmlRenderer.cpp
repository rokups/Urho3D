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
#include "../Core/Context.h"
#include "../IO/Log.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/VertexBuffer.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/GraphicsEvents.h"
#include "../Graphics/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../Math/Matrix4.h"
#include "../RmlUI/RmlRenderer.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace Detail
{

namespace
{

/// Internal vertex type used to render RmlUI geometry.
struct RmlVertex
{
    Vector3 position_;
    unsigned color_;
    Vector2 texCoord_;
};

/// Internal RmlUI texture holder.
struct CachedRmlTexture
{
    SharedPtr<Image> image_;
    SharedPtr<Texture2D> texture_;
};

/// Wrap CachedRmlTexture pointer to RmlUI handle.
Rml::TextureHandle WrapTextureHandle(CachedRmlTexture* texture) { return reinterpret_cast<Rml::TextureHandle>(texture); }

/// Unwrap RmlUI handle to CachedRmlTexture pointer.
CachedRmlTexture* UnwrapTextureHandle(Rml::TextureHandle texture) { return reinterpret_cast<CachedRmlTexture*>(texture); }

}

RmlRenderer::RmlRenderer(Context* context)
    : Object(context)
{
    InitializeGraphics();
    SubscribeToEvent(E_SCREENMODE, [this](StringHash, VariantMap&) { InitializeGraphics(); });
}

void RmlRenderer::BeginRendering()
{
    auto graphics = GetSubsystem<Graphics>();

    drawQueue_ = GetSubsystem<Renderer>()->GetDefaultDrawQueue();
    vertexBuffer_->Discard();
    indexBuffer_->Discard();
    drawQueue_->Reset(false);

    VertexBuffer* vertexBuffer = vertexBuffer_->GetVertexBuffer();
    IndexBuffer* indexBuffer = indexBuffer_->GetIndexBuffer();
    drawQueue_->SetBuffers({ { vertexBuffer }, indexBuffer, nullptr });

    batchStateCreateContext_.vertexBuffer_ = vertexBuffer;
    batchStateCreateContext_.indexBuffer_ = indexBuffer;

    renderSurface_ = graphics->GetRenderTarget(0);
    isRenderSurfaceSRGB_ = RenderSurface::GetSRGB(graphics, renderSurface_);
    viewportSize_ = graphics->GetViewport().Size();

    const Vector2 invScreenSize = Vector2::ONE / static_cast<Vector2>(viewportSize_);
    Vector2 scale(2.0f * invScreenSize.x_, -2.0f * invScreenSize.y_);
    Vector2 offset(-1.0f, 1.0f);
    if (renderSurface_)
    {
#ifdef URHO3D_OPENGL
        // On OpenGL, flip the projection if rendering to a texture so that the texture can be addressed in the
        // same way as a render texture produced on Direct3D.
        offset.y_ = -offset.y_;
        scale.y_ = -scale.y_;
#endif
    }

    const float farClip_ = 1000.0f;
    projection_ = Matrix4::IDENTITY;
    projection_.m00_ = scale.x_;
    projection_.m03_ = offset.x_;
    projection_.m11_ = scale.y_;
    projection_.m13_ = offset.y_;
    projection_.m22_ = 1.0f / farClip_;
    projection_.m23_ = 0.0f;
    projection_.m33_ = 1.0f;
}

void RmlRenderer::EndRendering()
{
    vertexBuffer_->Commit();
    indexBuffer_->Commit();
    drawQueue_->Execute();
    drawQueue_ = nullptr;
}

void RmlRenderer::InitializeGraphics()
{
    auto graphics = GetSubsystem<Graphics>();
    if (!graphics || !graphics->IsInitialized())
        return;

    batchStateCache_ = MakeShared<DefaultUIBatchStateCache>(context_);

    vertexBuffer_ = MakeShared<DynamicVertexBuffer>(context_);
    vertexBuffer_->Initialize(1024, VertexBuffer::GetElements(MASK_POSITION | MASK_COLOR | MASK_TEXCOORD1));
    indexBuffer_ = MakeShared<DynamicIndexBuffer>(context_);
    indexBuffer_->Initialize(1024, true);

    noTextureMaterial_ = Material::CreateBaseMaterial(context_, "Basic", "VERTEXCOLOR", "VERTEXCOLOR");
    alphaMapMaterial_ = Material::CreateBaseMaterial(context_, "Basic", "DIFFMAP VERTEXCOLOR", "ALPHAMAP VERTEXCOLOR");
    diffMapMaterial_ = Material::CreateBaseMaterial(context_, "Basic", "DIFFMAP VERTEXCOLOR", "DIFFMAP VERTEXCOLOR");
}

Material* RmlRenderer::GetBatchMaterial(Texture2D* texture)
{
    if (!texture)
        return noTextureMaterial_;
    else if (texture->GetFormat() == Graphics::GetAlphaFormat())
        return alphaMapMaterial_;
    else
        return diffMapMaterial_;
}

void RmlRenderer::RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices,
    Rml::TextureHandle textureHandle, const Rml::Vector2f& translation)
{
    if (scissorEnabled_ && transformEnabled_)
    {
        URHO3D_LOGERROR("Scissor test is not supported for transformed geometry");
        return;
    }

    const auto [firstVertex, vertexData] = vertexBuffer_->AddVertices(num_vertices);
    const auto [firstIndex, indexData] = indexBuffer_->AddIndices(num_indices);

    RmlVertex* destVertices = reinterpret_cast<RmlVertex*>(vertexData);
    for (unsigned i = 0; i < num_vertices; ++i)
    {
        destVertices[i].position_.x_ = vertices[i].position.x + translation.x;
        destVertices[i].position_.y_ = vertices[i].position.y + translation.y;
        destVertices[i].position_.z_ = 0.0f;
        const Rml::Colourb& color = vertices[i].colour;
        destVertices[i].color_ = (color.alpha << 24u) | (color.blue << 16u) | (color.green << 8u) | color.red;
        destVertices[i].texCoord_.x_ = vertices[i].tex_coord.x;
        destVertices[i].texCoord_.y_ = vertices[i].tex_coord.y;
    }

    unsigned* destIndices = reinterpret_cast<unsigned*>(indexData);
    for (unsigned i = 0; i < num_indices; ++i)
        destIndices[i] = indices[i] + firstVertex;

    // Restore texture data if lost
    CachedRmlTexture* cachedTexture = UnwrapTextureHandle(textureHandle);
    Texture2D* texture = cachedTexture ? cachedTexture->texture_ : nullptr;
    if (texture && texture->IsDataLost())
    {
        texture->SetData(cachedTexture->image_, true);
        texture->ClearDataLost();
    }

    Material* material = GetBatchMaterial(texture);
    Pass* pass = material->GetDefaultPass();
    const UIBatchStateKey batchStateKey{ isRenderSurfaceSRGB_, material, pass, BLEND_ALPHA };
    PipelineState* pipelineState = batchStateCache_->GetOrCreatePipelineState(batchStateKey, batchStateCreateContext_);

    const IntRect scissor = scissorEnabled_ ? scissor_ : IntRect{ IntVector2::ZERO, viewportSize_ };

    drawQueue_->SetScissorRect(scissor);
    drawQueue_->SetPipelineState(pipelineState);

    if (texture)
        drawQueue_->AddShaderResource(TU_DIFFUSE, texture);
    drawQueue_->CommitShaderResources();

    if (drawQueue_->BeginShaderParameterGroup(SP_CAMERA, false))
    {
        drawQueue_->AddShaderParameter(VSP_VIEWPROJ, projection_);
        drawQueue_->CommitShaderParameterGroup(SP_CAMERA);
    }

    if (drawQueue_->BeginShaderParameterGroup(SP_MATERIAL, false))
    {
        drawQueue_->AddShaderParameter(PSP_MATDIFFCOLOR, Color::WHITE.ToVector4());
        drawQueue_->CommitShaderParameterGroup(SP_MATERIAL);
    }

    if (drawQueue_->BeginShaderParameterGroup(SP_OBJECT, true))
    {
        drawQueue_->AddShaderParameter(VSP_MODEL, transform_);
        drawQueue_->CommitShaderParameterGroup(SP_OBJECT);
    }

    drawQueue_->DrawIndexedLegacy(firstIndex, num_indices, firstVertex, num_vertices);
}

void RmlRenderer::EnableScissorRegion(bool enable)
{
    scissorEnabled_ = enable;
}

void RmlRenderer::SetScissorRegion(int x, int y, int width, int height)
{
    scissor_.left_ = x;
    scissor_.top_ = y;
    scissor_.bottom_ = y + height;
    scissor_.right_ = x + width;

#ifdef URHO3D_OPENGL
    // Flip scissor vertically if using OpenGL texture rendering
    if (renderSurface_)
    {
        int top = scissor_.top_;
        int bottom = scissor_.bottom_;
        scissor_.top_ = viewportSize_.y_ - bottom;
        scissor_.bottom_ = viewportSize_.y_ - top;
    }
#endif

    // TODO: Support transformed scissors by doing scissor test on CPU
}

bool RmlRenderer::LoadTexture(Rml::TextureHandle& textureOut, Rml::Vector2i& sizeOut, const Rml::String& source)
{
    ResourceCache* cache = context_->GetSubsystem<ResourceCache>();
    Texture2D* texture = cache->GetResource<Texture2D>(source.c_str());
    if (texture)
    {
        sizeOut.x = texture->GetWidth();
        sizeOut.y = texture->GetHeight();
        texture->AddRef();
    }
    auto cachedTexture = new CachedRmlTexture{ nullptr, SharedPtr(texture) };
    textureOut = WrapTextureHandle(cachedTexture);
    return true;
}

bool RmlRenderer::GenerateTexture(Rml::TextureHandle& handleOut, const Rml::byte* source, const Rml::Vector2i& size)
{
    auto image = MakeShared<Image>(context_);
    image->SetSize(size.x, size.y, 4);
    image->SetData(source);

    auto texture = MakeShared<Texture2D>(context_);
    texture->SetData(image, true);

    auto cachedTexture = new CachedRmlTexture{ image, texture };
    handleOut = WrapTextureHandle(cachedTexture);
    return true;
}

void RmlRenderer::ReleaseTexture(Rml::TextureHandle textureHandle)
{
    CachedRmlTexture* cachedTexture = UnwrapTextureHandle(textureHandle);
    delete cachedTexture;
}

void RmlRenderer::SetTransform(const Rml::Matrix4f* transform)
{
    transformEnabled_ = transform != nullptr;
    transform_ = transform ? Matrix3x4(Matrix4(transform->data())) : Matrix3x4::IDENTITY;
}

}   // namespace Detail

}   // namespace Urho3D
