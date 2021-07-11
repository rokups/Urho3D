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

#include "../Graphics/ShaderConverter.h"

#ifdef URHO3D_SPIRV

#include "../IO/Log.h"
#include "../Graphics/ShaderVariation.h"
#include "../Graphics/Graphics.h"
#include "../Resource/ResourceCache.h"

#include <glslang/Public/ShaderLang.h>
#include <StandAlone/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_hlsl.hpp>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Utility to de/initialize glslang library.
struct GlslangGuardian
{
    GlslangGuardian() { glslang::InitializeProcess(); }
    ~GlslangGuardian() { glslang::FinalizeProcess(); }
};
static const GlslangGuardian glslangGuardian;

/// Vertex element semantics and index.
using VertexElementSemanticIndex = ea::pair<VertexElementSemantic, unsigned>;

/// Return vertex element semantics.
VertexElementSemanticIndex ParseVertexElement(ea::string_view name)
{
    const auto firstDigit = ea::find_if(name.begin(), name.end(), [](char ch) { return isdigit(ch); });
    const unsigned index = firstDigit != name.end() ? ToUInt(ea::string{ firstDigit, name.end() }) : 0;
    static const ea::unordered_map<ea::string, VertexElementSemantic> semanticsMapping = {
        { "iPos", SEM_POSITION },
        { "iNormal", SEM_NORMAL },
        { "iColor", SEM_COLOR },
        { "iTexCoord", SEM_TEXCOORD },
        { "iTangent", SEM_TANGENT },
        { "iBlendWeights", SEM_BLENDWEIGHTS },
        { "iBlendIndices", SEM_BLENDINDICES },
        { "iObjectIndex", SEM_OBJECTINDEX }
    };
    const ea::string_view sematicName = firstDigit != name.end() ? name.substr(0, firstDigit - name.begin()) : name;
    const auto semanticIter = semanticsMapping.find_as(sematicName);
    const VertexElementSemantic semantic = semanticIter != semanticsMapping.end()
        ? semanticIter->second
        : MAX_VERTEX_ELEMENT_SEMANTICS;
    return { semantic , index };
}

/// SpirV shader data.
struct SpirVShader
{
    /// Shader bytecode.
    std::vector<unsigned> bytecode_;
    /// Input layout.
    //ea::vector<VertexElementSemanticIndex> inputLayout_;
};

/// Compile SpirV shader.
bool CompileSpirV(EShLanguage stage, ea::string_view sourceCode, const ShaderDefineArray& shaderDefines,
    SpirVShader& outputShader, ea::string& errorMessage)
{
    outputShader = {};

    // Prepare defines
    static thread_local ea::string sourceHeader;
    sourceHeader.clear();
    sourceHeader += "#version 450\n";
    for (const auto& define : shaderDefines)
        sourceHeader += Format("#define {} {}\n", define.first, define.second);

    const char* inputStrings[] = { sourceHeader.data(), sourceCode.data() };
    const int inputLengths[] = { static_cast<int>(sourceHeader.length()), static_cast<int>(sourceCode.length()) };

    // Setup glslang shader
    glslang::TShader shader(stage);
    shader.setStringsWithLengths(inputStrings, inputLengths, static_cast<int>(std::size(inputStrings)));
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, 100);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EshTargetSpv, glslang::EShTargetSpv_1_0);
    shader.setAutoMapLocations(true);

    // Parse input shader
    if (!shader.parse(&glslang::DefaultTBuiltInResource, 100, false, EShMsgDefault))
    {
        errorMessage = shader.getInfoLog();
        return false;
    }

    // Link into fake program
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault))
    {
        errorMessage = program.getInfoLog();
        return false;
    }
    if (!program.mapIO())
    {
        errorMessage = program.getInfoLog();
        return false;
    }

    // Parse input layout
    /*program.buildReflection(EShReflectionAllBlockVariables);
    for (int i = 0; i < program.getNumLiveAttributes(); ++i)
    {
        const char* name = program.getAttributeName(i);
        const auto element = ParseVertexElement(name);
        if (element.first == MAX_VERTEX_ELEMENT_SEMANTICS)
        {
            errorMessage = Format("Unknown vertex element '{}'", name);
            return false;
        }
        outputShader.inputLayout_.push_back(element);
    }*/

    // Convert intermediate representation to SPIRV
    const glslang::TIntermediate* im = program.getIntermediate(stage);
    assert(im);
    spv::SpvBuildLogger spvLogger;
    glslang::SpvOptions spvOptions;
    spvOptions.generateDebugInfo = true;
    spvOptions.disableOptimizer = true;
    spvOptions.optimizeSize = false;
    glslang::GlslangToSpv(*im, outputShader.bytecode_, &spvLogger, &spvOptions);
    const std::string spirvLog = spvLogger.getAllMessages();
    if (!spirvLog.empty())
    {
        errorMessage = spirvLog.c_str();
    }

    return true;
}

/// Remapping HLSL compiler.
class RemappingCompilerHLSL : public spirv_cross::CompilerHLSL
{
public:
    RemappingCompilerHLSL(std::vector<unsigned> spirv) : spirv_cross::CompilerHLSL(spirv)
    {
    }

    bool RemapInputLayout(ea::string& errorMessage)
    {
        unsigned location = 0;
        unsigned numErrors = 0;

        const auto& execution = get_entry_point();
        if (execution.model == spv::ExecutionModelVertex)
        {
            // Output Uniform Constants (values, samplers, images, etc).
            ir.for_each_typed_id<spirv_cross::SPIRVariable>([&](uint32_t, spirv_cross::SPIRVariable &var)
            {
                auto& type = this->get<spirv_cross::SPIRType>(var.basetype);
                auto& m = ir.meta[var.self].decoration;
                if (type.storage == spv::StorageClassInput && !is_builtin_variable(var))
                {
                    m.decoration_flags.set(spv::DecorationLocation);
                    m.location = location++;
                    const VertexElementSemanticIndex vertexElement = ParseVertexElement(m.alias.c_str());
                    if (vertexElement.first == MAX_VERTEX_ELEMENT_SEMANTICS)
                    {
                        ++numErrors;
                        errorMessage += Format("Unknown input vertex element: '{}'\n", m.alias.c_str());
                        return;
                    }

                    const VertexElementSemantic semantic = vertexElement.first;
                    const unsigned index = vertexElement.second;
                    const ea::string name = Format("{}{}", ShaderVariation::elementSemanticNames[semantic], index);
                    add_vertex_attribute_remap({ m.location, name.c_str() });
                }
            });
        }

        return numErrors == 0;
    }
};

/// Convert SpirV to HLSL5
bool ConvertToHLSL5(const SpirVShader& shader, ea::string& outputShader, ea::string& errorMessage)
{
    errorMessage.clear();
    outputShader.clear();

    RemappingCompilerHLSL compiler(shader.bytecode_);
    if (!compiler.RemapInputLayout(errorMessage))
        return false;

    spirv_cross::CompilerGLSL::Options commonOptions;
    commonOptions.emit_line_directives = true;
    compiler.set_common_options(commonOptions);
    spirv_cross::CompilerHLSL::Options hlslOptions;
    hlslOptions.shader_model = 50;
    hlslOptions.point_size_compat = true;
    compiler.set_hlsl_options(hlslOptions);
    const std::string src = compiler.compile();
    if (src.empty())
    {
        errorMessage = "Unknown error";
        return false;
    }

    outputShader = src.c_str();
    return true;
}

}

bool ConvertShaderToHLSL5(ShaderType shaderType, const ea::string& sourceCode, const ShaderDefineArray& shaderDefines,
    ea::string& outputShaderCode, ea::string& errorMessage)
{
    SpirVShader shader;
    if (!CompileSpirV(shaderType == VS ? EShLangVertex : EShLangFragment, sourceCode, shaderDefines, shader, errorMessage))
        return false;

    if (!ConvertToHLSL5(shader, outputShaderCode, errorMessage))
        return false;

    return true;
}

}

namespace glslang
{

const TBuiltInResource DefaultTBuiltInResource = {
    /* .MaxLights = */ 32,
    /* .MaxClipPlanes = */ 6,
    /* .MaxTextureUnits = */ 32,
    /* .MaxTextureCoords = */ 32,
    /* .MaxVertexAttribs = */ 64,
    /* .MaxVertexUniformComponents = */ 4096,
    /* .MaxVaryingFloats = */ 64,
    /* .MaxVertexTextureImageUnits = */ 32,
    /* .MaxCombinedTextureImageUnits = */ 80,
    /* .MaxTextureImageUnits = */ 32,
    /* .MaxFragmentUniformComponents = */ 4096,
    /* .MaxDrawBuffers = */ 32,
    /* .MaxVertexUniformVectors = */ 128,
    /* .MaxVaryingVectors = */ 8,
    /* .MaxFragmentUniformVectors = */ 16,
    /* .MaxVertexOutputVectors = */ 16,
    /* .MaxFragmentInputVectors = */ 15,
    /* .MinProgramTexelOffset = */ -8,
    /* .MaxProgramTexelOffset = */ 7,
    /* .MaxClipDistances = */ 8,
    /* .MaxComputeWorkGroupCountX = */ 65535,
    /* .MaxComputeWorkGroupCountY = */ 65535,
    /* .MaxComputeWorkGroupCountZ = */ 65535,
    /* .MaxComputeWorkGroupSizeX = */ 1024,
    /* .MaxComputeWorkGroupSizeY = */ 1024,
    /* .MaxComputeWorkGroupSizeZ = */ 64,
    /* .MaxComputeUniformComponents = */ 1024,
    /* .MaxComputeTextureImageUnits = */ 16,
    /* .MaxComputeImageUniforms = */ 8,
    /* .MaxComputeAtomicCounters = */ 8,
    /* .MaxComputeAtomicCounterBuffers = */ 1,
    /* .MaxVaryingComponents = */ 60,
    /* .MaxVertexOutputComponents = */ 64,
    /* .MaxGeometryInputComponents = */ 64,
    /* .MaxGeometryOutputComponents = */ 128,
    /* .MaxFragmentInputComponents = */ 128,
    /* .MaxImageUnits = */ 8,
    /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
    /* .MaxCombinedShaderOutputResources = */ 8,
    /* .MaxImageSamples = */ 0,
    /* .MaxVertexImageUniforms = */ 0,
    /* .MaxTessControlImageUniforms = */ 0,
    /* .MaxTessEvaluationImageUniforms = */ 0,
    /* .MaxGeometryImageUniforms = */ 0,
    /* .MaxFragmentImageUniforms = */ 8,
    /* .MaxCombinedImageUniforms = */ 8,
    /* .MaxGeometryTextureImageUnits = */ 16,
    /* .MaxGeometryOutputVertices = */ 256,
    /* .MaxGeometryTotalOutputComponents = */ 1024,
    /* .MaxGeometryUniformComponents = */ 1024,
    /* .MaxGeometryVaryingComponents = */ 64,
    /* .MaxTessControlInputComponents = */ 128,
    /* .MaxTessControlOutputComponents = */ 128,
    /* .MaxTessControlTextureImageUnits = */ 16,
    /* .MaxTessControlUniformComponents = */ 1024,
    /* .MaxTessControlTotalOutputComponents = */ 4096,
    /* .MaxTessEvaluationInputComponents = */ 128,
    /* .MaxTessEvaluationOutputComponents = */ 128,
    /* .MaxTessEvaluationTextureImageUnits = */ 16,
    /* .MaxTessEvaluationUniformComponents = */ 1024,
    /* .MaxTessPatchComponents = */ 120,
    /* .MaxPatchVertices = */ 32,
    /* .MaxTessGenLevel = */ 64,
    /* .MaxViewports = */ 16,
    /* .MaxVertexAtomicCounters = */ 0,
    /* .MaxTessControlAtomicCounters = */ 0,
    /* .MaxTessEvaluationAtomicCounters = */ 0,
    /* .MaxGeometryAtomicCounters = */ 0,
    /* .MaxFragmentAtomicCounters = */ 8,
    /* .MaxCombinedAtomicCounters = */ 8,
    /* .MaxAtomicCounterBindings = */ 1,
    /* .MaxVertexAtomicCounterBuffers = */ 0,
    /* .MaxTessControlAtomicCounterBuffers = */ 0,
    /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
    /* .MaxGeometryAtomicCounterBuffers = */ 0,
    /* .MaxFragmentAtomicCounterBuffers = */ 1,
    /* .MaxCombinedAtomicCounterBuffers = */ 1,
    /* .MaxAtomicCounterBufferSize = */ 16384,
    /* .MaxTransformFeedbackBuffers = */ 4,
    /* .MaxTransformFeedbackInterleavedComponents = */ 64,
    /* .MaxCullDistances = */ 8,
    /* .MaxCombinedClipAndCullDistances = */ 8,
    /* .MaxSamples = */ 4,
    /* .maxMeshOutputVerticesNV = */ 256,
    /* .maxMeshOutputPrimitivesNV = */ 512,
    /* .maxMeshWorkGroupSizeX_NV = */ 32,
    /* .maxMeshWorkGroupSizeY_NV = */ 1,
    /* .maxMeshWorkGroupSizeZ_NV = */ 1,
    /* .maxTaskWorkGroupSizeX_NV = */ 32,
    /* .maxTaskWorkGroupSizeY_NV = */ 1,
    /* .maxTaskWorkGroupSizeZ_NV = */ 1,
    /* .maxMeshViewCountNV = */ 4,

    /* .limits = */ {
        /* .nonInductiveForLoops = */ 1,
        /* .whileLoops = */ 1,
        /* .doWhileLoops = */ 1,
        /* .generalUniformIndexing = */ 1,
        /* .generalAttributeMatrixVectorIndexing = */ 1,
        /* .generalVaryingIndexing = */ 1,
        /* .generalSamplerIndexing = */ 1,
        /* .generalVariableIndexing = */ 1,
        /* .generalConstantMatrixVectorIndexing = */ 1,
    }};
}
#endif
