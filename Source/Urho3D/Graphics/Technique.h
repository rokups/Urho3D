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

/// \file

#pragma once

#include "../Container/Hash.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/PipelineStateTracker.h"
#include "../Resource/Resource.h"

namespace Urho3D
{

class ShaderVariation;

static const char* blendModeNames[] =
{
    "replace",
    "add",
    "multiply",
    "alpha",
    "addalpha",
    "premulalpha",
    "invdestalpha",
    "subtract",
    "subtractalpha",
    nullptr
};

static const char* compareModeNames[] =
{
    "always",
    "equal",
    "notequal",
    "less",
    "lessequal",
    "greater",
    "greaterequal",
    nullptr
};

static const char* lightingModeNames[] =
{
    "unlit",
    "pervertex",
    "perpixel",
    nullptr
};

/// Lighting mode of a pass.
enum PassLightingMode
{
    LIGHTING_UNLIT = 0,
    LIGHTING_PERVERTEX,
    LIGHTING_PERPIXEL
};

/// %Material rendering pass, which defines shaders and render state.
class URHO3D_API Pass : public RefCounted, public PipelineStateTracker
{
public:
    /// Construct.
    explicit Pass(const ea::string& name);
    /// Destruct.
    ~Pass() override;

    /// Set blend mode.
    /// @property
    void SetBlendMode(BlendMode mode);
    /// Set culling mode override. By default culling mode is read from the material instead. Set the illegal culling mode MAX_CULLMODES to disable override again.
    /// @property
    void SetCullMode(CullMode mode);
    /// Set depth compare mode.
    /// @property
    void SetDepthTestMode(CompareMode mode);
    /// Set pass lighting mode, affects what shader variations will be attempted to be loaded.
    /// @property
    void SetLightingMode(PassLightingMode mode);
    /// Set depth write on/off.
    /// @property
    void SetDepthWrite(bool enable);
    /// Set alpha-to-coverage on/off.
    /// @property
    void SetAlphaToCoverage(bool enable);
    /// Set whether requires desktop level hardware.
    /// @property{set_desktop}
    void SetIsDesktop(bool enable);
    /// Set vertex shader name.
    /// @property
    void SetVertexShader(const ea::string& name);
    /// Set pixel shader name.
    /// @property
    void SetPixelShader(const ea::string& name);
    /// Set vertex shader defines. Separate multiple defines with spaces.
    /// @property
    void SetVertexShaderDefines(const ea::string& defines);
    /// Set pixel shader defines. Separate multiple defines with spaces.
    /// @property
    void SetPixelShaderDefines(const ea::string& defines);
    /// Set vertex shader define excludes. Use to mark defines that the shader code will not recognize, to prevent compiling redundant shader variations.
    /// @property
    void SetVertexShaderDefineExcludes(const ea::string& excludes);
    /// Set pixel shader define excludes. Use to mark defines that the shader code will not recognize, to prevent compiling redundant shader variations.
    /// @property
    void SetPixelShaderDefineExcludes(const ea::string& excludes);
    /// Reset shader pointers.
    void ReleaseShaders();
    /// Mark shaders loaded this frame.
    void MarkShadersLoaded(unsigned frameNumber);

    /// Return pass name.
    const ea::string& GetName() const { return name_; }

    /// Return pass index. This is used for optimal render-time pass queries that avoid map lookups.
    unsigned GetIndex() const { return index_; }

    /// Return blend mode.
    /// @property
    BlendMode GetBlendMode() const { return blendMode_; }

    /// Return culling mode override. If pass is not overriding culling mode (default), the illegal mode MAX_CULLMODES is returned.
    /// @property
    CullMode GetCullMode() const { return cullMode_; }

    /// Return depth compare mode.
    /// @property
    CompareMode GetDepthTestMode() const { return depthTestMode_; }

    /// Return pass lighting mode.
    /// @property
    PassLightingMode GetLightingMode() const { return lightingMode_; }

    /// Return last shaders loaded frame number.
    unsigned GetShadersLoadedFrameNumber() const { return shadersLoadedFrameNumber_; }

    /// Return depth write mode.
    /// @property
    bool GetDepthWrite() const { return depthWrite_; }

    /// Return alpha-to-coverage mode.
    /// @property
    bool GetAlphaToCoverage() const { return alphaToCoverage_; }

    /// Return whether requires desktop level hardware.
    /// @property
    bool IsDesktop() const { return isDesktop_; }

    /// Return whether the pass uses cutout transparency via ALPHAMASK.
    bool IsAlphaMask() const { return isAlphaMask_; }

    /// Return vertex shader name.
    /// @property
    const ea::string& GetVertexShader() const { return vertexShaderName_; }

    /// Return pixel shader name.
    /// @property
    const ea::string& GetPixelShader() const { return pixelShaderName_; }

    /// Return vertex shader defines.
    /// @property
    const ea::string& GetVertexShaderDefines() const { return vertexShaderDefines_; }

    /// Return pixel shader defines.
    /// @property
    const ea::string& GetPixelShaderDefines() const { return pixelShaderDefines_; }

    /// Return vertex shader define excludes.
    /// @property
    const ea::string& GetVertexShaderDefineExcludes() const { return vertexShaderDefineExcludes_; }

    /// Return pixel shader define excludes.
    /// @property
    const ea::string& GetPixelShaderDefineExcludes() const { return pixelShaderDefineExcludes_; }

    /// Return vertex shaders.
    ea::vector<SharedPtr<ShaderVariation> >& GetVertexShaders() { return vertexShaders_; }

    /// Return pixel shaders.
    ea::vector<SharedPtr<ShaderVariation> >& GetPixelShaders() { return pixelShaders_; }

    /// Return vertex shaders with extra defines from the renderpath.
    ea::vector<SharedPtr<ShaderVariation> >& GetVertexShaders(const StringHash& extraDefinesHash);
    /// Return pixel shaders with extra defines from the renderpath.
    ea::vector<SharedPtr<ShaderVariation> >& GetPixelShaders(const StringHash& extraDefinesHash);
    /// Return the effective vertex shader defines, accounting for excludes. Called internally by Renderer.
    ea::string GetEffectiveVertexShaderDefines() const;
    /// Return the effective pixel shader defines, accounting for excludes. Called internally by Renderer.
    ea::string GetEffectivePixelShaderDefines() const;

private:
    /// Recalculate hash of pipeline state configuration.
    unsigned RecalculatePipelineStateHash() const override;

    /// Pass index.
    unsigned index_;
    /// Blend mode.
    BlendMode blendMode_;
    /// Culling mode.
    CullMode cullMode_;
    /// Depth compare mode.
    CompareMode depthTestMode_;
    /// Lighting mode.
    PassLightingMode lightingMode_;
    /// Last shaders loaded frame number.
    unsigned shadersLoadedFrameNumber_;
    /// Depth write mode.
    bool depthWrite_;
    /// Alpha-to-coverage mode.
    bool alphaToCoverage_;
    /// Require desktop level hardware flag.
    bool isDesktop_;
    /// Whether the pass uses cutout transparency via ALPHAMASK.
    bool isAlphaMask_{};
    /// Vertex shader name.
    ea::string vertexShaderName_;
    /// Pixel shader name.
    ea::string pixelShaderName_;
    /// Vertex shader defines.
    ea::string vertexShaderDefines_;
    /// Pixel shader defines.
    ea::string pixelShaderDefines_;
    /// Vertex shader define excludes.
    ea::string vertexShaderDefineExcludes_;
    /// Pixel shader define excludes.
    ea::string pixelShaderDefineExcludes_;
    /// Vertex shaders.
    ea::vector<SharedPtr<ShaderVariation> > vertexShaders_;
    /// Pixel shaders.
    ea::vector<SharedPtr<ShaderVariation> > pixelShaders_;
    /// Vertex shaders with extra defines from the renderpath.
    ea::unordered_map<StringHash, ea::vector<SharedPtr<ShaderVariation> > > extraVertexShaders_;
    /// Pixel shaders with extra defines from the renderpath.
    ea::unordered_map<StringHash, ea::vector<SharedPtr<ShaderVariation> > > extraPixelShaders_;
    /// Pass name.
    ea::string name_;
};

/// %Material technique. Consists of several passes.
class URHO3D_API Technique : public Resource
{
    URHO3D_OBJECT(Technique, Resource);

    friend class Renderer;

public:
    /// Construct.
    explicit Technique(Context* context);
    /// Destruct.
    ~Technique() override;
    /// Register object factory.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Load resource from stream. May be called from a worker thread. Return true if successful.
    bool BeginLoad(Deserializer& source) override;

    /// Set whether requires desktop level hardware.
    /// @property{set_desktop}
    void SetIsDesktop(bool enable);
    /// Create a new pass.
    Pass* CreatePass(const ea::string& name);
    /// Remove a pass.
    void RemovePass(const ea::string& name);
    /// Reset shader pointers in all passes.
    void ReleaseShaders();
    /// Clone the technique. Passes will be deep copied to allow independent modification.
    SharedPtr<Technique> Clone(const ea::string& cloneName = EMPTY_STRING) const;

    /// Return whether requires desktop level hardware.
    /// @property
    bool IsDesktop() const { return isDesktop_; }

    /// Return whether technique is supported by the current hardware.
    /// @property
    bool IsSupported() const { return !isDesktop_ || desktopSupport_; }

    /// Return whether has a pass.
    bool HasPass(unsigned passIndex) const { return passIndex < passes_.size() && passes_[passIndex] != nullptr; }

    /// Return whether has a pass by name. This overload should not be called in time-critical rendering loops; use a pre-acquired pass index instead.
    bool HasPass(const ea::string& name) const;

    /// Return a pass, or null if not found.
    Pass* GetPass(unsigned passIndex) const { return passIndex < passes_.size() ? passes_[passIndex].Get() : nullptr; }

    /// Return a pass by name, or null if not found. This overload should not be called in time-critical rendering loops; use a pre-acquired pass index instead.
    Pass* GetPass(const ea::string& name) const;

    /// Return a pass that is supported for rendering, or null if not found.
    Pass* GetSupportedPass(unsigned passIndex) const
    {
        Pass* pass = passIndex < passes_.size() ? passes_[passIndex] : nullptr;
        return pass && (!pass->IsDesktop() || desktopSupport_) ? pass : nullptr;
    }

    /// Return a supported pass by name. This overload should not be called in time-critical rendering loops; use a pre-acquired pass index instead.
    Pass* GetSupportedPass(const ea::string& name) const;

    /// Return number of passes.
    /// @property
    unsigned GetNumPasses() const;
    /// Return all pass names.
    /// @property
    ea::vector<ea::string> GetPassNames() const;
    /// Return all passes.
    /// @property
    ea::vector<Pass*> GetPasses() const;

    /// Return a clone with added shader compilation defines. Called internally by Material.
    SharedPtr<Technique> CloneWithDefines(const ea::string& vsDefines, const ea::string& psDefines);

    /// Return a pass type index by name. Allocate new if not used yet.
    static unsigned GetPassIndex(const ea::string& passName);

    /// Index for base pass. Initialized once GetPassIndex() has been called for the first time.
    static unsigned basePassIndex;
    /// Index for alpha pass. Initialized once GetPassIndex() has been called for the first time.
    static unsigned alphaPassIndex;
    /// Index for prepass material pass. Initialized once GetPassIndex() has been called for the first time.
    static unsigned materialPassIndex;
    /// Index for deferred G-buffer pass. Initialized once GetPassIndex() has been called for the first time.
    static unsigned deferredPassIndex;
    /// Index for per-pixel light pass. Initialized once GetPassIndex() has been called for the first time.
    static unsigned lightPassIndex;
    /// Index for lit base pass. Initialized once GetPassIndex() has been called for the first time.
    static unsigned litBasePassIndex;
    /// Index for lit alpha pass. Initialized once GetPassIndex() has been called for the first time.
    static unsigned litAlphaPassIndex;
    /// Index for shadow pass. Initialized once GetPassIndex() has been called for the first time.
    static unsigned shadowPassIndex;

private:
    /// Require desktop GPU flag.
    bool isDesktop_;
    /// Cached desktop GPU support flag.
    bool desktopSupport_;
    /// Passes.
    ea::vector<SharedPtr<Pass> > passes_;
    /// Cached clones with added shader compilation defines.
    ea::unordered_map<ea::pair<StringHash, StringHash>, SharedPtr<Technique> > cloneTechniques_;

    /// Pass index assignments.
    static ea::unordered_map<ea::string, unsigned> passIndices;
};

}
