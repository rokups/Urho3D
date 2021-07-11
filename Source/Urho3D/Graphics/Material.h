//
// Copyright (c) 2008-2020 the Urho3D project.
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

#include "../Container/IndexAllocator.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/Light.h"
#include "../Graphics/Technique.h"
#include "../Math/Vector4.h"
#include "../Resource/Resource.h"
#include "../Scene/ValueAnimationInfo.h"

#include <atomic>

namespace Urho3D
{

class Material;
class Pass;
class Scene;
class Texture;
class Texture2D;
class TextureCube;
class ValueAnimationInfo;
class JSONFile;

static const unsigned char DEFAULT_RENDER_ORDER = 128;

static const char* textureUnitNames[] =
{
    "diffuse",
    "normal",
    "specular",
    "emissive",
    "environment",
#ifdef DESKTOP_GRAPHICS
    "volume",
    "custom1",
    "custom2",
    "lightramp",
    "lightshape",
    "shadowmap",
    "faceselect",
    "indirection",
    "depth",
    "light",
    "zone",
    nullptr
#else
    "lightramp",
    "lightshape",
    "shadowmap",
    nullptr
#endif
};

static const char* cullModeNames[] =
{
    "none",
    "ccw",
    "cw",
    nullptr
};

static const char* fillModeNames[] =
{
    "solid",
    "wireframe",
    "point",
    nullptr
};

/// %Material's shader parameter definition.
struct MaterialShaderParameter
{
    /// Name.
    ea::string name_;
    /// Value.
    Variant value_;
};

/// %Material's technique list entry.
struct URHO3D_API TechniqueEntry
{
    /// Construct with defaults.
    TechniqueEntry() noexcept;
    /// Construct with parameters.
    TechniqueEntry(Technique* tech, MaterialQuality qualityLevel, float lodDistance) noexcept;
    /// Destruct.
    ~TechniqueEntry() noexcept = default;

    /// Instance equality operator.
    bool operator ==(const TechniqueEntry& rhs) const
    {
        return this == &rhs;
    }

    /// Instance inequality operator.
    bool operator !=(const TechniqueEntry& rhs) const
    {
        return this != &rhs;
    }

    /// Technique.
    SharedPtr<Technique> technique_;
    /// Original technique, in case the material adds shader compilation defines. The modified clones are requested from it.
    SharedPtr<Technique> original_;
    /// Quality level.
    MaterialQuality qualityLevel_;
    /// LOD distance.
    float lodDistance_;
};

/// Material's shader parameter animation instance.
class URHO3D_API ShaderParameterAnimationInfo : public ValueAnimationInfo
{
public:
    /// Construct.
    ShaderParameterAnimationInfo
        (Material* material, const ea::string& name, ValueAnimation* attributeAnimation, WrapMode wrapMode, float speed);
    /// Copy construct.
    ShaderParameterAnimationInfo(const ShaderParameterAnimationInfo& other);
    /// Destruct.
    ~ShaderParameterAnimationInfo() override;

    /// Return shader parameter name.
    const ea::string& GetName() const { return name_; }

protected:
    /// Apply new animation value to the target object. Called by Update().
    void ApplyValue(const Variant& newValue) override;

private:
    /// Shader parameter name.
    ea::string name_;
};

/// Describes how to render 3D geometries.
class URHO3D_API Material : public Resource, public PipelineStateTracker, public IDFamily<Material>
{
    URHO3D_OBJECT(Material, Resource);

public:
    /// Construct.
    explicit Material(Context* context);
    /// Destruct.
    ~Material() override;
    /// Register object factory.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Create simple material with only base pass. Used by UI renderers.
    static SharedPtr<Material> CreateBaseMaterial(Context* context,
        const ea::string& shaderName, const ea::string& vsDefines, const ea::string& psDefines);

    /// Load resource from stream. May be called from a worker thread. Return true if successful.
    bool BeginLoad(Deserializer& source) override;
    /// Finish resource loading. Always called from the main thread. Return true if successful.
    bool EndLoad() override;
    /// Save resource. Return true if successful.
    bool Save(Serializer& dest) const override;

    using Resource::Load;

    /// Load from an XML element. Return true if successful.
    bool Load(const XMLElement& source);
    /// Save to an XML element. Return true if successful.
    bool Save(XMLElement& dest) const;

    /// Load from a JSON value. Return true if successful.
    bool Load(const JSONValue& source);
    /// Save to a JSON value. Return true if successful.
    bool Save(JSONValue& dest) const;

    /// Set number of techniques.
    /// @property
    void SetNumTechniques(unsigned num);
    /// Set technique.
    void SetTechnique(unsigned index, Technique* tech, MaterialQuality qualityLevel = QUALITY_LOW, float lodDistance = 0.0f);
    /// Set additional vertex shader defines. Separate multiple defines with spaces. Setting defines at the material level causes technique(s) to be cloned as necessary.
    /// @property
    void SetVertexShaderDefines(const ea::string& defines);
    /// Set additional pixel shader defines. Separate multiple defines with spaces. Setting defines at the material level causes technique(s) to be cloned as necessary.
    /// @property
    void SetPixelShaderDefines(const ea::string& defines);
    /// Set shader parameter.
    /// @property{set_shaderParameters}
    void SetShaderParameter(const ea::string& name, const Variant& value);
    /// Set shader parameter animation.
    void
        SetShaderParameterAnimation(const ea::string& name, ValueAnimation* animation, WrapMode wrapMode = WM_LOOP, float speed = 1.0f);
    /// Set shader parameter animation wrap mode.
    void SetShaderParameterAnimationWrapMode(const ea::string& name, WrapMode wrapMode);
    /// Set shader parameter animation speed.
    void SetShaderParameterAnimationSpeed(const ea::string& name, float speed);
    /// Set texture.
    /// @property{set_textures}
    void SetTexture(TextureUnit unit, Texture* texture);
    /// Set texture coordinate transform.
    void SetUVTransform(const Vector2& offset, float rotation, const Vector2& repeat);
    /// Set texture coordinate transform.
    void SetUVTransform(const Vector2& offset, float rotation, float repeat);
    /// Set culling mode.
    /// @property
    void SetCullMode(CullMode mode);
    /// Set culling mode for shadows.
    /// @property
    void SetShadowCullMode(CullMode mode);
    /// Set polygon fill mode. Interacts with the camera's fill mode setting so that the "least filled" mode will be used.
    /// @property
    void SetFillMode(FillMode mode);
    /// Set depth bias parameters for depth write and compare. Note that the normal offset parameter is not used and will not be saved, as it affects only shadow map sampling during light rendering.
    /// @property
    void SetDepthBias(const BiasParameters& parameters);
    /// Set alpha-to-coverage mode on all passes.
    /// @property
    void SetAlphaToCoverage(bool enable);
    /// Set line antialiasing on/off. Has effect only on models that consist of line lists.
    /// @property
    void SetLineAntiAlias(bool enable);
    /// Set 8-bit render order within pass. Default 128. Lower values will render earlier and higher values later, taking precedence over e.g. state and distance sorting.
    /// @property
    void SetRenderOrder(unsigned char order);
    /// Set whether to use in occlusion rendering. Default true.
    /// @property
    void SetOcclusion(bool enable);
    /// Associate the material with a scene to ensure that shader parameter animation happens in sync with scene update, respecting the scene time scale. If no scene is set, the global update events will be used.
    /// @property
    void SetScene(Scene* scene);
    /// Remove shader parameter.
    void RemoveShaderParameter(const ea::string& name);
    /// Reset all shader pointers.
    void ReleaseShaders();
    /// Clone the material.
    SharedPtr<Material> Clone(const ea::string& cloneName = EMPTY_STRING) const;
    /// Ensure that material techniques are listed in correct order.
    void SortTechniques();
    /// Mark material for auxiliary view rendering.
    void MarkForAuxView(unsigned frameNumber);

    /// Return number of techniques.
    /// @property
    unsigned GetNumTechniques() const { return techniques_.size(); }

    /// Return all techniques.
    const ea::vector<TechniqueEntry>& GetTechniques() const { return techniques_; }

    /// Return technique entry by index.
    const TechniqueEntry& GetTechniqueEntry(unsigned index) const;
    /// Return technique by index.
    /// @property{get_techniques}
    Technique* GetTechnique(unsigned index) const;
    /// Find best technique for given drawable and quality settings.
    Technique* FindTechnique(Drawable* drawable, MaterialQuality materialQuality) const;
    /// Return pass by technique index and pass name.
    Pass* GetPass(unsigned index, const ea::string& passName) const;
    /// Return default pass. Used by UI materials. It's base pass of 0-th technique assigned to Material.
    Pass* GetDefaultPass() const;
    /// Return texture by unit.
    /// @property{get_textures}
    Texture* GetTexture(TextureUnit unit) const;

    /// Return all textures.
    const ea::unordered_map<TextureUnit, SharedPtr<Texture> >& GetTextures() const { return textures_; }

    /// Return additional vertex shader defines.
    /// @property
    const ea::string& GetVertexShaderDefines() const { return vertexShaderDefines_; }
    /// Return additional pixel shader defines.
    /// @property
    const ea::string& GetPixelShaderDefines() const { return pixelShaderDefines_; }

    /// Return shader parameter.
    /// @property{get_shaderParameters}
    const Variant& GetShaderParameter(const ea::string& name) const;
    /// Return shader parameter animation.
    ValueAnimation* GetShaderParameterAnimation(const ea::string& name) const;
    /// Return shader parameter animation wrap mode.
    WrapMode GetShaderParameterAnimationWrapMode(const ea::string& name) const;
    /// Return shader parameter animation speed.
    float GetShaderParameterAnimationSpeed(const ea::string& name) const;

    /// Return all shader parameters.
    const ea::unordered_map<StringHash, MaterialShaderParameter>& GetShaderParameters() const { return shaderParameters_; }

    /// Return normal culling mode.
    /// @property
    CullMode GetCullMode() const { return cullMode_; }

    /// Return culling mode for shadows.
    /// @property
    CullMode GetShadowCullMode() const { return shadowCullMode_; }

    /// Return polygon fill mode.
    /// @property
    FillMode GetFillMode() const { return fillMode_; }

    /// Return depth bias.
    /// @property
    const BiasParameters& GetDepthBias() const { return depthBias_; }

    /// Return alpha-to-coverage mode.
    /// @property
    bool GetAlphaToCoverage() const { return alphaToCoverage_; }

    /// Return whether line antialiasing is enabled.
    /// @property
    bool GetLineAntiAlias() const { return lineAntiAlias_; }

    /// Return render order.
    /// @property
    unsigned char GetRenderOrder() const { return renderOrder_; }

    /// Return last auxiliary view rendered frame number.
    unsigned GetAuxViewFrameNumber() const { return auxViewFrameNumber_.load(std::memory_order_relaxed); }

    /// Return whether should render occlusion.
    /// @property
    bool GetOcclusion() const { return occlusion_; }

    /// Return whether should render specular.
    bool GetSpecular() const { return specular_; }

    /// Return the scene associated with the material for shader parameter animation updates.
    /// @property
    Scene* GetScene() const;

    /// Return shader parameter hash value. Used as an optimization to avoid setting shader parameters unnecessarily.
    unsigned GetShaderParameterHash() const { return shaderParameterHash_; }

    /// Return name for texture unit.
    static ea::string GetTextureUnitName(TextureUnit unit);
    /// Parse a shader parameter value from a string. Retunrs either a bool, a float, or a 2 to 4-component vector.
    static Variant ParseShaderParameterValue(const ea::string& value);

private:
    /// Helper function for loading JSON files.
    bool BeginLoadJSON(Deserializer& source);
    /// Helper function for loading XML files.
    bool BeginLoadXML(Deserializer& source);

    /// Reset to defaults.
    void ResetToDefaults();
    /// Recalculate shader parameter hash.
    void RefreshShaderParameterHash();
    /// Recalculate the memory used by the material.
    void RefreshMemoryUse();
    /// Reapply shader defines to technique index. By default reapply all.
    void ApplyShaderDefines(unsigned index = M_MAX_UNSIGNED);
    /// Return shader parameter animation info.
    ShaderParameterAnimationInfo* GetShaderParameterAnimationInfo(const ea::string& name) const;
    /// Update whether should be subscribed to scene or global update events for shader parameter animation.
    void UpdateEventSubscription();
    /// Update shader parameter animations.
    void HandleAttributeAnimationUpdate(StringHash eventType, VariantMap& eventData);
    /// Refresh subscriptions to texture events.
    void RefreshTextureEventSubscriptions();
    /// Set texture without event resubscription.
    void SetTextureInternal(TextureUnit unit, Texture* texture);
    /// Recalculate hash of pipeline state configuration.
    unsigned RecalculatePipelineStateHash() const override;

    /// Techniques.
    ea::vector<TechniqueEntry> techniques_;
    /// Textures.
    ea::unordered_map<TextureUnit, SharedPtr<Texture> > textures_;
    /// %Shader parameters.
    ea::unordered_map<StringHash, MaterialShaderParameter> shaderParameters_;
    /// %Shader parameters animation infos.
    ea::unordered_map<StringHash, SharedPtr<ShaderParameterAnimationInfo> > shaderParameterAnimationInfos_;
    /// Vertex shader defines.
    ea::string vertexShaderDefines_;
    /// Pixel shader defines.
    ea::string pixelShaderDefines_;
    /// Normal culling mode.
    CullMode cullMode_{};
    /// Culling mode for shadow rendering.
    CullMode shadowCullMode_{};
    /// Polygon fill mode.
    FillMode fillMode_{};
    /// Depth bias parameters.
    BiasParameters depthBias_{};
    /// Render order value.
    unsigned char renderOrder_{};
    /// Last auxiliary view rendered frame number.
    std::atomic_uint32_t auxViewFrameNumber_{ 0 };
    /// Shader parameter hash value.
    unsigned shaderParameterHash_{};
    /// Alpha-to-coverage flag.
    bool alphaToCoverage_{};
    /// Line antialiasing flag.
    bool lineAntiAlias_{};
    /// Render occlusion flag.
    bool occlusion_{true};
    /// Specular lighting flag.
    bool specular_{};
    /// Flag for whether is subscribed to animation updates.
    bool subscribed_{};
    /// Flag to suppress parameter hash and memory use recalculation when setting multiple shader parameters (loading or resetting the material).
    bool batchedParameterUpdate_{};
    /// XML file used while loading.
    SharedPtr<XMLFile> loadXMLFile_;
    /// JSON file used while loading.
    SharedPtr<JSONFile> loadJSONFile_;
    /// Associated scene for shader parameter animation updates.
    WeakPtr<Scene> scene_;
};

}
