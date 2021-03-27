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

#include "../Precompiled.h"

#include <EASTL/sort.h>

#include "../Core/Context.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Shader.h"
#include "../Graphics/ShaderVariation.h"
#include "../IO/Deserializer.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"
#include "../Resource/ResourceCache.h"

#include "../DebugNew.h"

namespace Urho3D
{

void CommentOutFunction(ea::string& code, const ea::string& signature)
{
    unsigned startPos = code.find(signature);
    unsigned braceLevel = 0;
    if (startPos == ea::string::npos)
        return;

    code.insert(startPos, "/*");

    for (unsigned i = startPos + 2 + signature.length(); i < code.length(); ++i)
    {
        if (code[i] == '{')
            ++braceLevel;
        else if (code[i] == '}')
        {
            --braceLevel;
            if (braceLevel == 0)
            {
                code.insert(i + 1, "*/");
                return;
            }
        }
    }
}

ea::string FormatLineDirective(bool isGLSL, const ea::string& fileName, unsigned fileIndex, unsigned line)
{
    if (isGLSL)
        return Format("#line {} {}\n", line, fileIndex);
    else
        return Format("#line {} \"{}\"\n", line, fileName);
}

ea::unordered_map<ea::string, unsigned> Shader::fileToIndexMapping;

Shader::Shader(Context* context) :
    Resource(context),
    timeStamp_(0),
    numVariations_(0)
{
    RefreshMemoryUse();
}

Shader::~Shader()
{
    auto* cache = GetSubsystem<ResourceCache>();
    if (cache)
        cache->ResetDependencies(this);
}

void Shader::RegisterObject(Context* context)
{
    context->RegisterFactory<Shader>();
}

bool Shader::BeginLoad(Deserializer& source)
{
    auto* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    // Load the shader source code and resolve any includes
    timeStamp_ = 0;
    ea::string shaderCode;
    ProcessSource(shaderCode, source);

    // Comment out the unneeded shader function
    vsSourceCode_ = shaderCode;
    psSourceCode_ = shaderCode;
    CommentOutFunction(vsSourceCode_, "void PS(");
    CommentOutFunction(psSourceCode_, "void VS(");

    // OpenGL: rename either VS() or PS() to main()
#ifdef URHO3D_OPENGL
    vsSourceCode_.replace("void VS(", "void main(");
    psSourceCode_.replace("void PS(", "void main(");
#endif

    RefreshMemoryUse();
    return true;
}

bool Shader::EndLoad()
{
    // If variations had already been created, release them and require recompile
    for (auto i = vsVariations_.begin(); i !=
        vsVariations_.end(); ++i)
        i->second->Release();
    for (auto i = psVariations_.begin(); i !=
        psVariations_.end(); ++i)
        i->second->Release();

    return true;
}

ShaderVariation* Shader::GetVariation(ShaderType type, const ea::string& defines)
{
    return GetVariation(type, defines.c_str());
}

ShaderVariation* Shader::GetVariation(ShaderType type, const char* defines)
{
    const unsigned definesHash = GetShaderDefinesHash(defines);

    ea::unordered_map<unsigned, SharedPtr<ShaderVariation> >& variations(type == VS ? vsVariations_ : psVariations_);
    auto i = variations.find(definesHash);
    if (i == variations.end())
    {
        // If shader not found, normalize the defines (to prevent duplicates) and check again. In that case make an alias
        // so that further queries are faster
        const ea::string normalizedDefines = NormalizeDefines(defines);
        const unsigned normalizedHash = GetShaderDefinesHash(normalizedDefines.c_str());

        i = variations.find(normalizedHash);
        if (i != variations.end())
            variations.insert(ea::make_pair(definesHash, i->second));
        else
        {
            // No shader variation found. Create new
            i = variations.insert(ea::make_pair(normalizedHash, SharedPtr<ShaderVariation>(new ShaderVariation(this, type)))).first;
            if (definesHash != normalizedHash)
                variations.insert(ea::make_pair(definesHash, i->second));

            Graphics* graphics = context_->GetSubsystem<Graphics>();
            i->second->SetName(GetFileName(GetName()));
            i->second->SetDefines(graphics->GetGlobalShaderDefines() + " " + normalizedDefines);
            ++numVariations_;
            RefreshMemoryUse();
        }
    }

    return i->second;
}

unsigned Shader::GetShaderDefinesHash(const char* defines) const
{
    Graphics* graphics = context_->GetSubsystem<Graphics>();
    unsigned definesHash = StringHash(defines).Value();
    CombineHash(definesHash, graphics->GetGlobalShaderDefinesHash().Value());
    return definesHash;
}

void Shader::ProcessSource(ea::string& code, Deserializer& source)
{
    auto* cache = GetSubsystem<ResourceCache>();
    const ea::string& fileName = source.GetName();
    const bool isGLSL = IsGLSL();

    // Add file to index
    unsigned& fileIndex = fileToIndexMapping[fileName];
    if (!fileIndex)
        fileIndex = fileToIndexMapping.size();

    // If the source if a non-packaged file, store the timestamp
    auto* file = dynamic_cast<File*>(&source);
    if (file && !file->IsPackaged())
    {
        auto* fileSystem = GetSubsystem<FileSystem>();
        ea::string fullName = cache->GetResourceFileName(file->GetName());
        unsigned fileTimeStamp = fileSystem->GetLastModifiedTime(fullName);
        if (fileTimeStamp > timeStamp_)
            timeStamp_ = fileTimeStamp;
    }

    // Store resource dependencies for includes so that we know to reload if any of them changes
    if (source.GetName() != GetName())
        cache->StoreResourceDependency(this, source.GetName());

    unsigned currentLine = 1;
    code += FormatLineDirective(isGLSL, fileName, fileIndex, currentLine);
    while (!source.IsEof())
    {
        ea::string line = source.ReadLine();
        line.ltrim();

        if (line.starts_with("#include"))
        {
            ea::string includeFileName = GetPath(source.GetName()) + line.substr(9).replaced("\"", "").trimmed();

            // Add included code or error directive
            SharedPtr<File> includeFile = cache->GetFile(includeFileName);
            if (includeFile)
                ProcessSource(code, *includeFile);
            else
                code += Format("#error Missing include file <{}>\n", includeFileName);

            code += FormatLineDirective(isGLSL, fileName, fileIndex, currentLine);
        }
        else
        {
            const bool isComment = line.length() >= 2 && line[0] == '/' && line[1] == '/';
            const bool isLineContinuation = line.length() >= 1 && line.back() == '\\';
            if (isLineContinuation)
                line.erase(line.end() - 1);
            if (!isComment)
                code += line;
            if (!isLineContinuation)
                code += "\n";
        }
        ++currentLine;
    }

    // Finally insert an empty line to mark the space between files
    code += "\n";
}

ea::string Shader::NormalizeDefines(const ea::string& defines)
{
    ea::vector<ea::string> definesVec = defines.to_upper().split(' ');
    ea::quick_sort(definesVec.begin(), definesVec.end());
    return ea::string::joined(definesVec, " ");
}

void Shader::RefreshMemoryUse()
{
    SetMemoryUse(
        (unsigned)(sizeof(Shader) + vsSourceCode_.length() + psSourceCode_.length() + numVariations_ * sizeof(ShaderVariation)));
}

ea::string Shader::GetShaderFileList()
{
    ea::vector<ea::pair<ea::string, unsigned>> fileList(fileToIndexMapping.begin(), fileToIndexMapping.end());
    ea::sort(fileList.begin(), fileList.end(),
        [](const ea::pair<ea::string, unsigned>& lhs, const ea::pair<ea::string, unsigned>& rhs)
    {
        return lhs.second < rhs.second;
    });

    ea::string result;
    result += "Shader Files:\n";
    for (const auto& item : fileList)
        result += Format("{}: {}\n", item.second, item.first);
    result += "\n";
    return result;
}

}
