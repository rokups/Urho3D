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

#include "../Core/Context.h"
#include "../Graphics/Material.h"
#include "../Graphics/RenderPath.h"
#include "../Graphics/StaticModel.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Technique.h"
#include "../Graphics/Terrain.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/XMLFile.h"

#include <EASTL/string.h>

#include <future>

namespace Urho3D
{

/// Parallel loop.
template <class T>
void ParallelFor(unsigned count, unsigned numTasks, const T& callback)
{
    // Post async tasks
    ea::vector<std::future<void>> tasks;
    const unsigned chunkSize = (count + numTasks - 1) / numTasks;
    for (unsigned i = 0; i < numTasks; ++i)
    {
        tasks.push_back(std::async([&, i]()
        {
            const unsigned fromIndex = i * chunkSize;
            const unsigned toIndex = ea::min(fromIndex + chunkSize, count);
            callback(fromIndex, toIndex);
        }));
    }

    // Wait for async tasks
    for (auto& task : tasks)
        task.wait();
}

/// Return whether the material is opaque.
inline bool IsMaterialOpaque(const Material* material)
{
    const Technique* technique = material->GetTechnique(0);
    if (technique && technique->GetName().contains("alpha", false))
        return false;

    if (material->GetPixelShaderDefines().contains("ALPHAMASK"))
        return false;

    if (material->GetShaderParameter("MatDiffColor").GetVector4().w_ < 1.0f - M_LARGE_EPSILON)
        return false;

    return true;
}

/// Return material diffuse color.
inline Color GetMaterialDiffuseColor(const Material* material)
{
    return static_cast<Color>(material->GetShaderParameter("MatDiffColor").GetVector4());
}

/// Return material diffuse texture and UV offsets.
inline Texture* GetMaterialDiffuseTexture(const Material* material, Vector4& uOffset, Vector4& vOffset)
{
    Texture* texture = material->GetTexture(TU_DIFFUSE);
    if (!texture)
        return {};

    uOffset = material->GetShaderParameter("UOffset").GetVector4();
    vOffset = material->GetShaderParameter("VOffset").GetVector4();
    return texture;
}

/// Set lightmap index for component.
inline void SetLightmapIndex(Component* component, unsigned lightmapIndex)
{
    if (auto staticModel = dynamic_cast<StaticModel*>(component))
        staticModel->SetLightmapIndex(lightmapIndex);
    else if (auto terrain = dynamic_cast<Terrain*>(component))
        terrain->SetLightmapIndex(lightmapIndex);
    else
        assert(0);
}

/// Return lightmap index for component.
inline unsigned GetLightmapIndex(const Component* component)
{
    if (auto staticModel = dynamic_cast<const StaticModel*>(component))
        return staticModel->GetLightmapIndex();
    else if (auto terrain = dynamic_cast<const Terrain*>(component))
        return terrain->GetLightmapIndex();
    else
    {
        assert(0);
        return 0;
    }
}

/// Set lightmap scale and offset for component.
inline void SetLightmapScaleOffset(Component* component, const Vector4& scaleOffset)
{
    if (auto staticModel = dynamic_cast<StaticModel*>(component))
        staticModel->SetLightmapScaleOffset(scaleOffset);
    else if (auto terrain = dynamic_cast<Terrain*>(component))
        terrain->SetLightmapScaleOffset(scaleOffset);
    else
        assert(0);
}

/// Return lightmap scale and offset for component.
inline Vector4 GetLightmapScaleOffset(const Component* component)
{
    if (auto staticModel = dynamic_cast<const StaticModel*>(component))
        return staticModel->GetLightmapScaleOffset();
    else if (auto terrain = dynamic_cast<const Terrain*>(component))
        return terrain->GetLightmapScaleOffset();
    else
    {
        assert(0);
        return Vector4::ZERO;
    }
}

/// Create material for geometry buffer baking.
inline SharedPtr<Material> CreateBakingMaterial(Material* bakingMaterial, Material* sourceMaterial,
    const Vector4& scaleOffset, unsigned tapIndex, unsigned numTaps, const Vector2& tapOffset,
    unsigned geometryId, const Vector2& scaledAndConstBias)
{
    auto renderer = bakingMaterial->GetContext()->GetSubsystem<Renderer>();
    if (!sourceMaterial)
        sourceMaterial = renderer->GetDefaultMaterial();

    const Vector4 tapOffset4{ 0.0f, 0.0f, tapOffset.x_, tapOffset.y_ };
    const float tapDepth = 1.0f - static_cast<float>(tapIndex + 1) / (numTaps + 1);

    auto material = bakingMaterial->Clone();
    material->SetShaderParameter("LMOffset", scaleOffset + tapOffset4);
    material->SetShaderParameter("LightmapLayer", tapDepth);
    material->SetShaderParameter("LightmapGeometry", static_cast<float>(geometryId));
    material->SetShaderParameter("LightmapPositionBias", scaledAndConstBias);
    material->SetShaderParameter("MatDiffColor", sourceMaterial->GetShaderParameter("MatDiffColor").GetVector4());
    material->SetShaderParameter("MatEmissiveColor", sourceMaterial->GetShaderParameter("MatEmissiveColor").GetVector3());
    material->SetShaderParameter("UOffset", sourceMaterial->GetShaderParameter("UOffset").GetVector4());
    material->SetShaderParameter("VOffset", sourceMaterial->GetShaderParameter("VOffset").GetVector4());

    ea::string shaderDefines;

    if (Texture* diffuseMap = sourceMaterial->GetTexture(TU_DIFFUSE))
    {
        material->SetTexture(TU_DIFFUSE, diffuseMap);
        shaderDefines += "DIFFMAP ";
    }
    if (Texture* emissiveMap = sourceMaterial->GetTexture(TU_EMISSIVE))
    {
        material->SetTexture(TU_EMISSIVE, emissiveMap);
        shaderDefines += "EMISSIVEMAP ";
    }

    material->SetVertexShaderDefines(shaderDefines);
    material->SetPixelShaderDefines(shaderDefines);

    return material;
}

}
