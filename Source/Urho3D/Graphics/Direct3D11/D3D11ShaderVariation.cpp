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

#include "../../Precompiled.h"

#include "../../Graphics/Graphics.h"
#include "../../Graphics/GraphicsImpl.h"
#include "../../Graphics/Shader.h"
#include "../../Graphics/ShaderDefineArray.h"
#include "../../Graphics/ShaderConverter.h"
#include "../../Graphics/VertexBuffer.h"
#include "../../IO/File.h"
#include "../../IO/FileSystem.h"
#include "../../IO/Log.h"
#include "../../Resource/ResourceCache.h"

#include <d3dcompiler.h>

#include "../../DebugNew.h"

namespace Urho3D
{

const char* ShaderVariation::elementSemanticNames[] =
{
    "POSITION",
    "NORMAL",
    "BINORMAL",
    "TANGENT",
    "TEXCOORD",
    "COLOR",
    "BLENDWEIGHT",
    "BLENDINDICES",
    "OBJECTINDEX"
};

void ShaderVariation::OnDeviceLost()
{
    // No-op on Direct3D11
}

bool ShaderVariation::Create()
{
    Release();

    if (!graphics_)
        return false;

    if (!owner_)
    {
        compilerOutput_ = "Owner shader has expired";
        return false;
    }

    // Check for up-to-date bytecode on disk
    ea::string path, name, extension;
    SplitPath(owner_->GetName(), path, name, extension);
    extension = type_ == VS ? ".vs4" : ".ps4";

    ea::string binaryShaderName = graphics_->GetShaderCacheDir() + name + "_" + StringHash(defines_).ToString() + extension;

    if (!LoadByteCode(binaryShaderName))
    {
        // Compile shader if don't have valid bytecode
        if (!Compile())
            return false;
        // Save the bytecode after successful compile, but not if the source is from a package
        if (owner_->GetTimeStamp())
            SaveByteCode(binaryShaderName);
    }

    // Then create shader from the bytecode
    ID3D11Device* device = graphics_->GetImpl()->GetDevice();
    if (type_ == VS)
    {
        if (device && byteCode_.size())
        {
            HRESULT hr = device->CreateVertexShader(&byteCode_[0], byteCode_.size(), nullptr, (ID3D11VertexShader**)&object_.ptr_);
            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(object_.ptr_);
                compilerOutput_ = "Could not create vertex shader (HRESULT " + ToStringHex((unsigned)hr) + ")";
            }
        }
        else
            compilerOutput_ = "Could not create vertex shader, empty bytecode";
    }
    else
    {
        if (device && byteCode_.size())
        {
            HRESULT hr = device->CreatePixelShader(&byteCode_[0], byteCode_.size(), nullptr, (ID3D11PixelShader**)&object_.ptr_);
            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(object_.ptr_);
                compilerOutput_ = "Could not create pixel shader (HRESULT " + ToStringHex((unsigned)hr) + ")";
            }
        }
        else
            compilerOutput_ = "Could not create pixel shader, empty bytecode";
    }

    return object_.ptr_ != nullptr;
}

void ShaderVariation::Release()
{
    if (object_.ptr_)
    {
        if (!graphics_)
            return;

        graphics_->CleanupShaderPrograms(this);

        if (type_ == VS)
        {
            if (graphics_->GetVertexShader() == this)
                graphics_->SetShaders(nullptr, nullptr);
        }
        else
        {
            if (graphics_->GetPixelShader() == this)
                graphics_->SetShaders(nullptr, nullptr);
        }

        URHO3D_SAFE_RELEASE(object_.ptr_);
    }

    compilerOutput_.clear();

    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        useTextureUnits_[i] = false;
    for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
        constantBufferSizes_[i] = 0;
    parameters_.clear();
    byteCode_.clear();
    elementHash_ = 0;
}

void ShaderVariation::SetDefines(const ea::string& defines)
{
    defines_ = defines;
}

bool ShaderVariation::LoadByteCode(const ea::string& binaryShaderName)
{
    ResourceCache* cache = owner_->GetSubsystem<ResourceCache>();
    if (!cache->Exists(binaryShaderName))
        return false;

    FileSystem* fileSystem = owner_->GetSubsystem<FileSystem>();
    unsigned sourceTimeStamp = owner_->GetTimeStamp();
    // If source code is loaded from a package, its timestamp will be zero. Else check that binary is not older
    // than source
    if (sourceTimeStamp && fileSystem->GetLastModifiedTime(cache->GetResourceFileName(binaryShaderName)) < sourceTimeStamp)
        return false;

    SharedPtr<File> file = cache->GetFile(binaryShaderName);
    if (!file || file->ReadFileID() != "USHD")
    {
        URHO3D_LOGERROR(binaryShaderName + " is not a valid shader bytecode file");
        return false;
    }

    /// \todo Check that shader type and model match
    /*unsigned short shaderType = */file->ReadUShort();
    /*unsigned short shaderModel = */file->ReadUShort();
    elementHash_ = file->ReadUInt();
    elementHash_ <<= 32;

    unsigned numParameters = file->ReadUInt();
    for (unsigned i = 0; i < numParameters; ++i)
    {
        ea::string name = file->ReadString();
        unsigned buffer = file->ReadUByte();
        unsigned offset = file->ReadUInt();
        unsigned size = file->ReadUInt();

        parameters_[StringHash(name)] = ShaderParameter{type_, name, offset, size, buffer};
    }

    unsigned numTextureUnits = file->ReadUInt();
    for (unsigned i = 0; i < numTextureUnits; ++i)
    {
        /*String unitName = */file->ReadString();
        unsigned reg = file->ReadUByte();

        if (reg < MAX_TEXTURE_UNITS)
            useTextureUnits_[reg] = true;
    }

    unsigned byteCodeSize = file->ReadUInt();
    if (byteCodeSize)
    {
        byteCode_.resize(byteCodeSize);
        file->Read(&byteCode_[0], byteCodeSize);

        if (type_ == VS)
            URHO3D_LOGDEBUG("Loaded cached vertex shader " + GetFullName());
        else
            URHO3D_LOGDEBUG("Loaded cached pixel shader " + GetFullName());

        CalculateConstantBufferSizes();
        return true;
    }
    else
    {
        URHO3D_LOGERROR(binaryShaderName + " has zero length bytecode");
        return false;
    }
}

bool ShaderVariation::Compile()
{
    // Set the code, defines, entrypoint, profile and flags according to the shader being compiled
    const ea::string* sourceCode = nullptr;
    const char* entryPoint = nullptr;
    const char* profile = nullptr;
    unsigned flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    ShaderDefineArray defines{ defines_ };
    ea::vector<D3D_SHADER_MACRO> macros;

    if (type_ == VS)
    {
        entryPoint = "VS";
        defines.Append("COMPILEVS");
        profile = "vs_4_0";
    }
    else
    {
        entryPoint = "PS";
        defines.Append("COMPILEPS");
        profile = "ps_4_0";
        flags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
    }

    defines.Append("MAXBONES", ea::to_string(Graphics::GetMaxBones()));
    defines.Append("D3D11");

    // Convert shader source code if GLSL
    static thread_local ea::string convertedShaderSourceCode;
    if (owner_->IsGLSL())
    {
        defines.Append("DESKTOP_GRAPHICS");
        defines.Append("GL3");

        const ea::string& universalSourceCode = owner_->GetSourceCode(type_);
        ea::string errorMessage;
        if (!ConvertShaderToHLSL5(type_, universalSourceCode, defines, convertedShaderSourceCode, errorMessage))
        {
            URHO3D_LOGERROR("Failed to convert shader {} from GLSL:\n{}", GetFullName(), errorMessage);
            return false;
        }

        // In debug mode, check that all defines are referenced by the shader code
#ifdef _DEBUG
        const auto& unusedDefines = defines.FindUnused(universalSourceCode);
        if (!unusedDefines.empty())
            URHO3D_LOGWARNING("Shader {} does not use the define(s): {}", GetFullName(), ea::string::joined(unusedDefines, ", "));
#endif

        sourceCode = &convertedShaderSourceCode;
        entryPoint = "main";
    }
    else
    {
        const ea::string& nativeSourceCode = owner_->GetSourceCode(type_);
        sourceCode = &nativeSourceCode;

        macros.reserve(defines.Size());
        for (const auto& define : defines)
        {
            macros.push_back({ define.first.c_str(), define.second.c_str() });

            // In debug mode, check that all defines are referenced by the shader code
#ifdef _DEBUG
            const auto& unusedDefines = defines.FindUnused(nativeSourceCode);
            if (!unusedDefines.empty())
                URHO3D_LOGWARNING("Shader {} does not use the define(s): {}", GetFullName(), ea::string::joined(unusedDefines, ", "));
#endif
        }
    }

    D3D_SHADER_MACRO endMacro;
    endMacro.Name = nullptr;
    endMacro.Definition = nullptr;
    macros.emplace_back(endMacro);

    // Compile using D3DCompile
    ID3DBlob* shaderCode = nullptr;
    ID3DBlob* errorMsgs = nullptr;

    HRESULT hr = D3DCompile(sourceCode->c_str(), sourceCode->length(), owner_->GetName().c_str(), &macros.front(), nullptr,
        entryPoint, profile, flags, 0, &shaderCode, &errorMsgs);
    if (FAILED(hr))
    {
        // Do not include end zero unnecessarily
        compilerOutput_ = ea::string((const char*)errorMsgs->GetBufferPointer(), (unsigned)errorMsgs->GetBufferSize() - 1);
    }
    else
    {
        if (type_ == VS)
            URHO3D_LOGDEBUG("Compiled vertex shader " + GetFullName());
        else
            URHO3D_LOGDEBUG("Compiled pixel shader " + GetFullName());

        unsigned char* bufData = (unsigned char*)shaderCode->GetBufferPointer();
        unsigned bufSize = (unsigned)shaderCode->GetBufferSize();
        // Use the original bytecode to reflect the parameters
        ParseParameters(bufData, bufSize);
        CalculateConstantBufferSizes();

        // Then strip everything not necessary to use the shader
        ID3DBlob* strippedCode = nullptr;
        D3DStripShader(bufData, bufSize,
            D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO | D3DCOMPILER_STRIP_TEST_BLOBS, &strippedCode);
        byteCode_.resize((unsigned)strippedCode->GetBufferSize());
        memcpy(&byteCode_[0], strippedCode->GetBufferPointer(), byteCode_.size());
        strippedCode->Release();
    }

    URHO3D_SAFE_RELEASE(shaderCode);
    URHO3D_SAFE_RELEASE(errorMsgs);

    return !byteCode_.empty();
}

void ShaderVariation::ParseParameters(unsigned char* bufData, unsigned bufSize)
{
    ID3D11ShaderReflection* reflection = nullptr;
    D3D11_SHADER_DESC shaderDesc;

    HRESULT hr = D3DReflect(bufData, bufSize, IID_ID3D11ShaderReflection, (void**)&reflection);
    if (FAILED(hr) || !reflection)
    {
        URHO3D_SAFE_RELEASE(reflection);
        URHO3D_LOGD3DERROR("Failed to reflect vertex shader's input signature", hr);
        return;
    }

    reflection->GetDesc(&shaderDesc);

    if (type_ == VS)
    {
        unsigned elementHash = 0;
        for (unsigned i = 0; i < shaderDesc.InputParameters; ++i)
        {
            D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
            reflection->GetInputParameterDesc((UINT)i, &paramDesc);
            VertexElementSemantic semantic = (VertexElementSemantic)GetStringListIndex(paramDesc.SemanticName, elementSemanticNames, MAX_VERTEX_ELEMENT_SEMANTICS, true);
            if (semantic != MAX_VERTEX_ELEMENT_SEMANTICS)
            {
                CombineHash(elementHash, semantic);
                CombineHash(elementHash, paramDesc.SemanticIndex);
            }
        }
        elementHash_ = elementHash;
        elementHash_ <<= 32;
    }

    ea::unordered_map<ea::string, unsigned> cbRegisterMap;

    for (unsigned i = 0; i < shaderDesc.BoundResources; ++i)
    {
        D3D11_SHADER_INPUT_BIND_DESC resourceDesc;
        reflection->GetResourceBindingDesc(i, &resourceDesc);
        ea::string resourceName(resourceDesc.Name);
        if (resourceDesc.Type == D3D_SIT_CBUFFER)
            cbRegisterMap[resourceName] = resourceDesc.BindPoint;
        else if (resourceDesc.Type == D3D_SIT_SAMPLER && resourceDesc.BindPoint < MAX_TEXTURE_UNITS)
            useTextureUnits_[resourceDesc.BindPoint] = true;
    }

    for (unsigned i = 0; i < shaderDesc.ConstantBuffers; ++i)
    {
        ID3D11ShaderReflectionConstantBuffer* cb = reflection->GetConstantBufferByIndex(i);
        D3D11_SHADER_BUFFER_DESC cbDesc;
        cb->GetDesc(&cbDesc);
        unsigned cbRegister = cbRegisterMap[ea::string(cbDesc.Name)];

        for (unsigned j = 0; j < cbDesc.Variables; ++j)
        {
            ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(j);
            D3D11_SHADER_VARIABLE_DESC varDesc;
            var->GetDesc(&varDesc);
            ea::string varName(varDesc.Name);
            const auto nameStart = varName.find('c');
            if (nameStart != ea::string::npos)
            {
                varName = varName.substr(nameStart + 1); // Strip the c to follow Urho3D constant naming convention
                parameters_[varName] = ShaderParameter{type_, varName, varDesc.StartOffset, varDesc.Size, cbRegister};
            }
        }
    }

    reflection->Release();
}

void ShaderVariation::SaveByteCode(const ea::string& binaryShaderName)
{
    ResourceCache* cache = owner_->GetSubsystem<ResourceCache>();
    FileSystem* fileSystem = owner_->GetSubsystem<FileSystem>();

    // Filename may or may not be inside the resource system
    ea::string fullName = binaryShaderName;
    if (!IsAbsolutePath(fullName))
    {
        // If not absolute, use the resource dir of the shader
        ea::string shaderFileName = cache->GetResourceFileName(owner_->GetName());
        if (shaderFileName.empty())
            return;
        fullName = shaderFileName.substr(0, shaderFileName.find(owner_->GetName())) + binaryShaderName;
    }
    ea::string path = GetPath(fullName);
    if (!fileSystem->DirExists(path))
        fileSystem->CreateDir(path);

    SharedPtr<File> file(new File(owner_->GetContext(), fullName, FILE_WRITE));
    if (!file->IsOpen())
        return;

    file->WriteFileID("USHD");
    file->WriteShort((unsigned short)type_);
    file->WriteShort(4);
    file->WriteUInt(elementHash_ >> 32);

    file->WriteUInt(parameters_.size());
    for (auto i = parameters_.begin(); i != parameters_.end(); ++i)
    {
        file->WriteString(i->second.name_);
        file->WriteUByte((unsigned char)i->second.buffer_);
        file->WriteUInt(i->second.offset_);
        file->WriteUInt(i->second.size_);
    }

    unsigned usedTextureUnits = 0;
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        if (useTextureUnits_[i])
            ++usedTextureUnits;
    }
    file->WriteUInt(usedTextureUnits);
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        if (useTextureUnits_[i])
        {
            file->WriteString(graphics_->GetTextureUnitName((TextureUnit)i));
            file->WriteUByte((unsigned char)i);
        }
    }

    file->WriteUInt(byteCode_.size());
    if (byteCode_.size())
        file->Write(&byteCode_[0], byteCode_.size());
}

void ShaderVariation::CalculateConstantBufferSizes()
{
    for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
        constantBufferSizes_[i] = 0;

    for (auto i = parameters_.begin(); i != parameters_.end(); ++i)
    {
        if (i->second.buffer_ < MAX_SHADER_PARAMETER_GROUPS)
        {
            unsigned oldSize = constantBufferSizes_[i->second.buffer_];
            unsigned paramEnd = i->second.offset_ + i->second.size_;
            if (paramEnd > oldSize)
                constantBufferSizes_[i->second.buffer_] = (paramEnd + 15) / 16 * 16;
        }
    }
}

}
