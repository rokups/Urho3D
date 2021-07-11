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
#include "../../Graphics/ShaderProgram.h"
#include "../../Graphics/ShaderVariation.h"
#include "../../IO/Log.h"

#include <cctype>

#include "../../DebugNew.h"

namespace Urho3D
{

const char* ShaderVariation::elementSemanticNames[] =
{
    "POS",
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
    if (object_.name_ && !graphics_->IsDeviceLost())
        glDeleteShader(object_.name_);

    GPUObject::OnDeviceLost();

    compilerOutput_.clear();
}

void ShaderVariation::Release()
{
    if (object_.name_)
    {
        if (!graphics_)
            return;

        if (!graphics_->IsDeviceLost())
        {
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

            glDeleteShader(object_.name_);
        }

        object_.name_ = 0;
        graphics_->CleanupShaderPrograms(this);
    }

    compilerOutput_.clear();
}

bool ShaderVariation::Create()
{
    Release();

    if (!owner_)
    {
        compilerOutput_ = "Owner shader has expired";
        return false;
    }

    object_.name_ = glCreateShader(type_ == VS ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    if (!object_.name_)
    {
        compilerOutput_ = "Could not create shader object";
        return false;
    }

    const ea::string& originalShaderCode = owner_->GetSourceCode(type_);
    ea::string shaderCode;

    // Check if the shader code contains a version define
    unsigned verStart = originalShaderCode.find('#');
    unsigned verEnd = 0;
    if (verStart != ea::string::npos)
    {
        if (originalShaderCode.substr(verStart + 1, 7) == "version")
        {
            verEnd = verStart + 9;
            while (verEnd < originalShaderCode.length())
            {
                if (IsDigit((unsigned)originalShaderCode[verEnd]))
                    ++verEnd;
                else
                    break;
            }
            // If version define found, insert it first
            ea::string versionDefine = originalShaderCode.substr(verStart, verEnd - verStart);
            shaderCode += versionDefine + "\n";
        }
    }
    // Force GLSL version 150 if no version define and GL3 is being used
    if (!verEnd && Graphics::GetGL3Support())
    {
#ifdef MOBILE_GRAPHICS
        shaderCode += "#version 300 es\n";
#else
        shaderCode += "#version 150\n";
#endif
    }
#if defined(DESKTOP_GRAPHICS)
    shaderCode += "#define DESKTOP_GRAPHICS\n";
#elif defined(MOBILE_GRAPHICS)
    shaderCode += "#define MOBILE_GRAPHICS\n";
#endif

    // Distinguish between VS and PS compile in case the shader code wants to include/omit different things
    shaderCode += type_ == VS ? "#define COMPILEVS\n" : "#define COMPILEPS\n";

    // Add define for the maximum number of supported bones
    shaderCode += "#define MAXBONES " + ea::to_string(Graphics::GetMaxBones()) + "\n";

    // Prepend the defines to the shader code
    ea::vector<ea::string> defineVec = defines_.split(' ');
    for (unsigned i = 0; i < defineVec.size(); ++i)
    {
        // Add extra space for the checking code below
        ea::string defineString = "#define " + defineVec[i].replaced('=', ' ') + " \n";
        shaderCode += defineString;

        // In debug mode, check that all defines are referenced by the shader code
#ifdef _DEBUG
        ea::string defineCheck = defineString.substr(8, defineString.find(' ', 8) - 8);
        if (originalShaderCode.find(defineCheck) == ea::string::npos)
            URHO3D_LOGWARNING("Shader " + GetFullName() + " does not use the define " + defineCheck);
#endif
    }

#ifdef RPI
    if (type_ == VS)
        shaderCode += "#define RPI\n";
#endif
#ifdef __EMSCRIPTEN__
    shaderCode += "#define WEBGL\n";
#endif
    if (Graphics::GetGL3Support())
        shaderCode += "#define GL3\n";

    // When version define found, do not insert it a second time
    if (verEnd > 0)
        shaderCode += (originalShaderCode.c_str() + verEnd);
    else
        shaderCode += originalShaderCode;

    const char* shaderCStr = shaderCode.c_str();
    glShaderSource(object_.name_, 1, &shaderCStr, nullptr);
    glCompileShader(object_.name_);

    int compiled, length;
    glGetShaderiv(object_.name_, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        glGetShaderiv(object_.name_, GL_INFO_LOG_LENGTH, &length);
        compilerOutput_.resize((unsigned) length);
        int outLength;
        glGetShaderInfoLog(object_.name_, length, &outLength, &compilerOutput_[0]);
        glDeleteShader(object_.name_);
        object_.name_ = 0;

        compilerOutput_ = Shader::GetShaderFileList() + compilerOutput_;
    }
    else
        compilerOutput_.clear();

    return object_.name_ != 0;
}

void ShaderVariation::SetDefines(const ea::string& defines)
{
    defines_ = defines;
}

// These methods are no-ops for OpenGL
bool ShaderVariation::LoadByteCode(const ea::string& binaryShaderName) { return false; }
bool ShaderVariation::Compile() { return false; }
void ShaderVariation::ParseParameters(unsigned char* bufData, unsigned bufSize) {}
void ShaderVariation::SaveByteCode(const ea::string& binaryShaderName) {}
void ShaderVariation::CalculateConstantBufferSizes() {}

}
