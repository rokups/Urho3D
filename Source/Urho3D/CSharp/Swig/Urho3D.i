%module(directors="1", dirprot="1", allprotected="1", naturalvar=1) Urho3D

namespace eastl { }
namespace ea = eastl;
namespace Urho3D { }
using namespace Urho3D;

#define final
#define URHO3D_DEPRECATED
#define static_assert(...)

%include "stl.i"
%include "stdint.i"
%include "typemaps.i"
%include "arrays_csharp.i"
%include "cmalloc.i"
%include "swiginterface.i"
%include "attribute.i"

%include "InstanceCache.i"

%apply bool* INOUT                  { bool&, bool* };
%apply signed char* INOUT           { signed char&/*, signed char**/ };
%apply unsigned char* INOUT         { unsigned char&/*, unsigned char**/ };
%apply short* INOUT                 { short&, short* };
%apply unsigned short* INOUT        { unsigned short&, unsigned short* };
%apply int* INOUT                   { int&, int* };
%apply unsigned int* INOUT          { unsigned int&, unsigned int*, unsigned&, unsigned* };
%apply long long* INOUT             { long long&, long long* };
%apply unsigned long long* INOUT    { unsigned long long&, unsigned long long* };
%apply float* INOUT                 { float&, float* };
%apply double* INOUT                { double&, double* };

%apply bool FIXED[]                 { bool[] };
%apply signed char FIXED[]          { signed char[] };
%apply unsigned char FIXED[]        { unsigned char[] };
%apply short FIXED[]                { short[] };
%apply unsigned short FIXED[]       { unsigned short[] };
%apply int FIXED[]                  { int[] };
%apply unsigned int FIXED[]         { unsigned int[] };
%apply long long FIXED[]            { long long[] };
%apply unsigned long long FIXED[]   { unsigned long long[] };
%apply float FIXED[]                { float[] };
%apply double FIXED[]               { double[] };

%typemap(csvarout) void* VOID_INT_PTR %{
  get {
    var ret = $imcall;$excode
    return ret;
  }
%}

%apply void* VOID_INT_PTR {
  void*,
  signed char*,
  unsigned char*
}

%typemap(csvarin, excode=SWIGEXCODE2) void* VOID_INT_PTR %{
  set {
    $imcall;$excode
  }
%}

%apply void* { std::uintptr_t, uintptr_t };
%apply unsigned { time_t };
%typemap(csvarout, excode=SWIGEXCODE2) float INOUT[] "get { var ret = $imcall;$excode return ret; }"
%typemap(ctype)   const char* INPUT[] "char**"
%typemap(cstype)  const char* INPUT[] "string[]"
%typemap(imtype, inattributes="[global::System.Runtime.InteropServices.In, global::System.Runtime.InteropServices.MarshalAs(global::Urho3DNet.Urho3DPINVOKE.LPStr)]") const char* INPUT[] "string[]"

%typemap(csin)    const char* INPUT[] "$csinput"
%typemap(in)      const char* INPUT[] "$1 = $input;"
%typemap(freearg) const char* INPUT[] ""
%typemap(argout)  const char* INPUT[] ""
%apply const char* INPUT[]   { char const *const items[] };

// ref global::System.IntPtr
%typemap(ctype, out="void *")                 void*& "void *"
%typemap(imtype, out="global::System.IntPtr") void*& "ref global::System.IntPtr"
%typemap(cstype, out="$csclassname")          void*& "ref global::System.IntPtr"
%typemap(csin)                                void*& "ref $csinput"
%typemap(in)                                  void*& %{ $1 = ($1_ltype)$input; %}
%typecheck(SWIG_TYPECHECK_CHAR_PTR)           void*& ""

// Speed boost
%pragma(csharp) imclassclassmodifiers="[System.Security.SuppressUnmanagedCodeSecurity]\ninternal unsafe class"
%pragma(csharp) moduleclassmodifiers="[System.Security.SuppressUnmanagedCodeSecurity]\npublic unsafe partial class"
%typemap(csclassmodifiers) SWIGTYPE "[global::Urho3DNet.Preserve(AllMembers=true)]\npublic unsafe partial class"

%{
#if _WIN32
#   include <Urho3D/WindowsSupport.h>
#endif
#include <Urho3D/Urho3DAll.h>   // If this include is missing please build with -DURHO3D_MONOLITHIC_HEADER=ON
#include <SDL/SDL_joystick.h>
#include <SDL/SDL_gamecontroller.h>
#include <SDL/SDL_keycode.h>
#include <Urho3D/CSharp/Native/SWIGHelpers.h>
%}

%typemap(check, canthrow=1) SWIGTYPE* self %{
  if (!$1) {
    SWIG_CSharpSetPendingExceptionArgument(SWIG_CSharpArgumentNullException, "$1_type is expired.", 0);
    return $null;
  }
%}

%include "Helpers.i"
%include "Operators.i"

#ifndef URHO3D_STATIC
#   define URHO3D_STATIC
#endif
#ifndef URHO3D_API
#   define URHO3D_API
#endif

#define URHO3D_TYPE_TRAIT(...)

%apply void* VOID_INT_PTR {
	SDL_Cursor*,
	SDL_Surface*,
	SDL_Window*,
	Urho3D::GraphicsImpl*,
    ImFont*,
    tracy::SourceLocationData*
}

%include "StringHash.i"
%include "eastl_string.i"

%rename("%(camelcase)s", %$isenumitem) "";
%rename("%(camelcase)s", %$isvariable, %$ispublic) "";
%rename("%(camelcase)s", %$isvariable, %$isprotected) "";

// --------------------------------------- Math ---------------------------------------
%include "Math.i"

%ignore Urho3D::M_PI;
%ignore Urho3D::M_HALF_PI;
%ignore Urho3D::M_MIN_INT;
%ignore Urho3D::M_MAX_INT;
%ignore Urho3D::M_MIN_UNSIGNED;
%ignore Urho3D::M_MAX_UNSIGNED;
%ignore Urho3D::M_EPSILON;
%ignore Urho3D::M_LARGE_EPSILON;
%ignore Urho3D::M_MIN_NEARCLIP;
%ignore Urho3D::M_MAX_FOV;
%ignore Urho3D::M_LARGE_VALUE;
%ignore Urho3D::M_INFINITY;
%ignore Urho3D::M_DEGTORAD;
%ignore Urho3D::M_DEGTORAD_2;
%ignore Urho3D::M_RADTODEG;

%ignore Urho3D::Frustum::planes_;
%ignore Urho3D::Frustum::vertices_;
// These should be implemented in C# anyway.
%ignore Urho3D::Polyhedron::Polyhedron(const Vector<eastl::vector<Vector3> >& faces);
%ignore Urho3D::Polyhedron::faces_;
%ignore Urho3D::Random;
%ignore Urho3D::RandomNormal;
%ignore Urho3D::SetRandomSeed;
%ignore Urho3D::GetRandomSeed;
%ignore Urho3D::Rand;
%ignore Urho3D::RandStandardNormal;
%ignore Urho3D::RandomEngine::GetStandardNormalFloatPair;

%include "Urho3D/Math/MathDefs.h"
%include "Urho3D/Math/Polyhedron.h"
%include "Urho3D/Math/Frustum.h"
%include "Urho3D/Math/RandomEngine.h"

CSHARP_ARRAYS_FIXED(Urho3D::Vector4, global::Urho3DNet.Vector4)
%apply Urho3D::Vector4 FIXED[] { Urho3D::Vector4[] };

// ---------------------------------------  ---------------------------------------
%ignore Urho3D::begin;
%ignore Urho3D::end;

%ignore Urho3D::textureFilterModeNames;
%ignore Urho3D::textureUnitNames;
%ignore Urho3D::cullModeNames;
%ignore Urho3D::fillModeNames;
%ignore Urho3D::blendModeNames;
%ignore Urho3D::compareModeNames;
%ignore Urho3D::lightingModeNames;

%include "RefCounted.i"

%apply size_t { eastl_size_t };
%apply const size_t& { const eastl_size_t& };
%include "eastl_vector.i"
%include "eastl_map.i"
%include "eastl_pair.i"
%include "eastl_unordered_map.i"

// Declare inheritable classes in this file
%include "Context.i"

%rename(VarVoidPtr) VAR_VOIDPTR;
%rename(VarResourceRef) VAR_RESOURCEREF;
%rename(VarResourceRefList) VAR_RESOURCEREFLIST;
%rename(VarVariantList) VAR_VARIANTVECTOR;
%rename(VarVariantMap) VAR_VARIANTMAP;
%rename(VarIntRect) VAR_INTRECT;
%rename(VarIntVector2) VAR_INTVECTOR2;
%rename(VarMatrix3x4) VAR_MATRIX3X4;
%rename(VarStringList) VAR_STRINGVECTOR;
%rename(VarIntVector3) VAR_INTVECTOR3;

AddEqualityOperators(Urho3D::ResourceRef);
AddEqualityOperators(Urho3D::ResourceRefList);
AddEqualityOperators(Urho3D::AttributeInfo);
AddEqualityOperators(Urho3D::Splite);
AddEqualityOperators(Urho3D::JSONValue);
AddEqualityOperators(Urho3D::PListValue);
AddEqualityOperators(Urho3D::VertexElement);
AddEqualityOperators(Urho3D::RenderTargetInfo);
AddEqualityOperators(Urho3D::RenderPathCommand);
AddEqualityOperators(Urho3D::Bone);
AddEqualityOperators(Urho3D::ModelMorph);
AddEqualityOperators(Urho3D::AnimationKeyFrame);
AddEqualityOperators(Urho3D::AnimationTrack);
AddEqualityOperators(Urho3D::AnimationTriggerPoint);
AddEqualityOperators(Urho3D::AnimationControl);
AddEqualityOperators(Urho3D::Billboard);
AddEqualityOperators(Urho3D::DecalVertex);
AddEqualityOperators(Urho3D::TechniqueEntry);
AddEqualityOperators(Urho3D::CustomGeometryVertex);
AddEqualityOperators(Urho3D::ColorFrame);
AddEqualityOperators(Urho3D::TextureFrame);
AddEqualityOperators(Urho3D::Variant);

%ignore Urho3D::GPUObject::OnDeviceLost;
%ignore Urho3D::GPUObject::OnDeviceReset;
%ignore Urho3D::GPUObject::Release;
%ignore Urho3D::VertexBuffer::OnDeviceLost;
%ignore Urho3D::VertexBuffer::OnDeviceReset;
%ignore Urho3D::VertexBuffer::Release;
%ignore Urho3D::IndexBuffer::OnDeviceLost;
%ignore Urho3D::IndexBuffer::OnDeviceReset;
%ignore Urho3D::IndexBuffer::Release;
%ignore Urho3D::ConstantBuffer::OnDeviceLost;
%ignore Urho3D::ConstantBuffer::OnDeviceReset;
%ignore Urho3D::ConstantBuffer::Release;
%ignore Urho3D::ShaderVariation::OnDeviceLost;
%ignore Urho3D::ShaderVariation::OnDeviceReset;
%ignore Urho3D::ShaderVariation::Release;
%ignore Urho3D::Texture::OnDeviceLost;
%ignore Urho3D::Texture::OnDeviceReset;
%ignore Urho3D::Texture::Release;

// --------------------------------------- SDL ---------------------------------------
namespace SDL
{
  #include "../ThirdParty/SDL/include/SDL/SDL_joystick.h"
  #include "../ThirdParty/SDL/include/SDL/SDL_gamecontroller.h"
  #include "../ThirdParty/SDL/include/SDL/SDL_keycode.h"
}
// --------------------------------------- Core ---------------------------------------
%ignore Urho3D::RegisterResourceLibrary;
%ignore Urho3D::RegisterSceneLibrary;
%ignore Urho3D::RegisterAudioLibrary;
%ignore Urho3D::RegisterIKLibrary;
%ignore Urho3D::RegisterGraphicsLibrary;
%ignore Urho3D::RegisterNavigationLibrary;
%ignore Urho3D::RegisterUILibrary;

%ignore Urho3D::GetEventNameRegister;
%ignore Urho3D::CustomVariantValue;
%ignore Urho3D::CustomVariantValueTraits;
%ignore Urho3D::CustomVariantValueImpl;
%ignore Urho3D::MakeCustomValue;
%ignore Urho3D::VariantValue;
%ignore Urho3D::Variant::Variant(const VectorBuffer&);
%ignore Urho3D::Variant::GetVectorBuffer;
%ignore Urho3D::Variant::SetCustomVariantValue;
%ignore Urho3D::Variant::GetCustomVariantValuePtr;
%ignore Urho3D::Variant::GetStringVector;
%ignore Urho3D::Variant::GetVariantVector;
%ignore Urho3D::Variant::GetVariantMap;
%ignore Urho3D::Variant::GetCustomPtr;
%ignore Urho3D::Variant::GetBuffer;
%ignore Urho3D::VARIANT_VALUE_SIZE;
%rename(GetVariantType) Urho3D::Variant::GetType;
%nocsattribute Urho3D::Variant::GetType;
%csmethodmodifiers ToString() "public override"
%ignore Urho3D::AttributeInfo::enumNamesStorage_;
%ignore Urho3D::AttributeInfo::enumNamesPointers_;
%ignore Urho3D::AttributeInfo::enumNames_;

// Subsystem properties.
%extend Urho3D::Context {
  %insert("proxycode") %{
    public global::Urho3DNet.Engine Engine => (global::Urho3DNet.Engine)Subsystems.Get(new StringHash("Engine"));
    public global::Urho3DNet.Time Time => (global::Urho3DNet.Time)Subsystems.Get(new StringHash("Time"));
    //public global::Urho3DNet.WorkQueue WorkQueue => (global::Urho3DNet.WorkQueue)Subsystems.Get(new StringHash("WorkQueue"));
    public global::Urho3DNet.FileSystem FileSystem => (global::Urho3DNet.FileSystem)Subsystems.Get(new StringHash("FileSystem"));
    public global::Urho3DNet.ResourceCache ResourceCache => (global::Urho3DNet.ResourceCache)Subsystems.Get(new StringHash("ResourceCache"));
    public global::Urho3DNet.Localization Localization => (global::Urho3DNet.Localization)Subsystems.Get(new StringHash("Localization"));
    public global::Urho3DNet.Input Input => (global::Urho3DNet.Input)Subsystems.Get(new StringHash("Input"));
    public global::Urho3DNet.Audio Audio => (global::Urho3DNet.Audio)Subsystems.Get(new StringHash("Audio"));
    public global::Urho3DNet.UI UI => (global::Urho3DNet.UI)Subsystems.Get(new StringHash("UI"));
    public global::Urho3DNet.Graphics Graphics => (global::Urho3DNet.Graphics)Subsystems.Get(new StringHash("Graphics"));
    public global::Urho3DNet.Renderer Renderer => (global::Urho3DNet.Renderer)Subsystems.Get(new StringHash("Renderer"));
    public global::Urho3DNet.Log Log => (global::Urho3DNet.Log)Subsystems.Get(new StringHash("Log"));
  %}
}
#if URHO3D_NETWORK
%extend Urho3D::Context {
  %insert("proxycode") %{
    public global::Urho3DNet.Network Network => (global::Urho3DNet.Network)Subsystems.Get(new StringHash("Network"));
  %}
}
#endif
#if URHO3D_SYSTEMUI
%extend Urho3D::Context {
  %insert("proxycode") %{
    public global::Urho3DNet.SystemUI SystemUI => (global::Urho3DNet.SystemUI)Subsystems.Get(new StringHash("SystemUI"));
  %}
}
#endif

%ignore Urho3D::Detail::CriticalSection;
%ignore Urho3D::MutexLock;

%include "Object.i"
%director Urho3D::AttributeAccessor;

%include "generated/Urho3D/_pre_core.i"
%include "Urho3D/Core/Variant.h"
%include "Urho3D/Core/Attribute.h"
%include "Urho3D/Core/SubsystemCache.h"
%include "Urho3D/Core/Context.h"
%include "Urho3D/Core/Object.h"
%include "Urho3D/Core/Timer.h"
%include "Urho3D/Core/Spline.h"
%include "Urho3D/Core/Mutex.h"

// --------------------------------------- Container ------------------------------------
%include "Urho3D/Container/ByteVector.h"

// --------------------------------------- Engine ---------------------------------------
%ignore Urho3D::Engine::DefineParameters;
%ignore Urho3D::Application::engine_;
%ignore Urho3D::Application::GetCommandLineParser;
%ignore Urho3D::PluginApplication::PluginApplicationMain;
%ignore Urho3D::PluginApplication::InitializeReloadablePlugin;
%ignore Urho3D::PluginApplication::UninitializeReloadablePlugin;

%include "generated/Urho3D/_pre_engine.i"
%include "Urho3D/Engine/EngineDefs.h"
%include "Urho3D/Engine/Engine.h"
%include "Urho3D/Engine/Application.h"
%include "Urho3D/Engine/PluginApplication.h"
%include "generated/Urho3D/_pre_script.i"
#if URHO3D_CSHARP
%include "Urho3D/Script/Script.h"
#endif

// --------------------------------------- Input ---------------------------------------
%typemap(csbase) Urho3D::MouseButton "uint"

%apply void* VOID_INT_PTR { SDL_GameController*, SDL_Joystick* }
%apply int { SDL_JoystickID };
%typemap(csvarout, excode=SWIGEXCODE2) SDL_GameController*, SDL_Joystick* %{
  get {
    var ret = $imcall;$excode
    return ret;
  }
%}

%ignore Urho3D::TouchState::GetTouchedElement;
%ignore Urho3D::Input::OnRawInput;

%include "generated/Urho3D/_pre_input.i"
%include "Urho3D/Input/InputConstants.h"
%include "Urho3D/Input/Controls.h"
%include "Urho3D/Input/Input.h"

// --------------------------------------- IO ---------------------------------------
%ignore Urho3D::GetWideNativePath;
%ignore Urho3D::logLevelNames;
%ignore Urho3D::LOG_LEVEL_COLORS;

%extend Urho3D::Log {
public:
    static void Trace(const char* message)   { Log::GetLogger().Write(LOG_TRACE, message); }
    static void Debug(const char* message)   { Log::GetLogger().Write(LOG_DEBUG, message); }
    static void Info(const char* message)    { Log::GetLogger().Write(LOG_INFO, message); }
    static void Warning(const char* message) { Log::GetLogger().Write(LOG_WARNING, message); }
    static void Error(const char* message)   { Log::GetLogger().Write(LOG_ERROR, message); }
}

%include "generated/Urho3D/_pre_io.i"
%interface_custom("%s", "I%s", Urho3D::Serializer);
%include "Urho3D/IO/Serializer.h"
%interface_custom("%s", "I%s", Urho3D::Deserializer);
%include "Urho3D/IO/Deserializer.h"
%interface_custom("%s", "I%s", Urho3D::AbstractFile);
%include "Urho3D/IO/AbstractFile.h"
%include "Urho3D/IO/Compression.h"
%include "Urho3D/IO/File.h"
%include "Urho3D/IO/Log.h"
%include "Urho3D/IO/MemoryBuffer.h"
%include "Urho3D/IO/PackageFile.h"
%include "Urho3D/IO/VectorBuffer.h"
%include "Urho3D/IO/FileSystem.h"

%ignore Urho3D::NonCopyable;
%ignore Urho3D::ArchiveBase;
%ignore Urho3D::Archive::OpenBlock;
%ignore Urho3D::Archive::OpenSequentialBlock;
%ignore Urho3D::Archive::OpenUnorderedBlock;
%ignore Urho3D::Archive::OpenArrayBlock;
%ignore Urho3D::Archive::OpenMapBlock;
%ignore Urho3D::Archive::OpenSafeSequentialBlock;
%ignore Urho3D::Archive::OpenSafeUnorderedBlock;
%include "Urho3D/IO/Archive.h"

// --------------------------------------- Resource ---------------------------------------
%ignore Urho3D::XMLFile::GetDocument;
%ignore Urho3D::XMLElement::XMLElement(XMLFile* file, pugi::xml_node_struct* node);
%ignore Urho3D::XMLElement::XMLElement(XMLFile* file, const XPathResultSet* resultSet, const pugi::xpath_node* xpathNode, unsigned xpathResultIndex);
%ignore Urho3D::XMLElement::GetNode;
%ignore Urho3D::XMLElement::GetXPathNode;
%ignore Urho3D::XMLElement::Select;
%ignore Urho3D::XMLElement::SelectSingle;
%ignore Urho3D::XPathResultSet::XPathResultSet(XMLFile* file, pugi::xpath_node_set* resultSet);
%ignore Urho3D::XPathResultSet::GetXPathNodeSet;
%ignore Urho3D::XPathQuery::GetXPathQuery;
%ignore Urho3D::XPathQuery::GetXPathVariableSet;

%ignore Urho3D::Image::GetLevels(ea::vector<Image*>& levels);
%ignore Urho3D::Image::GetLevels(ea::vector<const Image*>& levels) const;

namespace Urho3D { class Image; }
%extend Urho3D::Image {
public:
	eastl::vector<Image*> GetLevels() {
		eastl::vector<Image*> result{};
		$self->GetLevels(result);
		return result;
	}
}

// These expose iterators of underlying collection. Iterate object through GetObject() instead.
%ignore Urho3D::BackgroundLoadItem;
%ignore Urho3D::BackgroundLoader::ThreadFunction;
%ignore Urho3D::ImageCube::CalculateSphericalHarmonics;
%rename(GetValueType) Urho3D::PListValue::GetType;

%include "generated/Urho3D/_pre_resource.i"
%include "Urho3D/Resource/Resource.h"
#if defined(URHO3D_THREADING)
%include "Urho3D/Resource/BackgroundLoader.h"
#endif
%include "Urho3D/Resource/Image.h"
%include "Urho3D/Resource/ImageCube.h"
%include "Urho3D/Resource/JSONValue.h"
%include "Urho3D/Resource/JSONFile.h"
%include "Urho3D/Resource/Localization.h"
%include "Urho3D/Resource/PListFile.h"
%include "Urho3D/Resource/XMLElement.h"
%include "Urho3D/Resource/XMLFile.h"
%include "Urho3D/Resource/ResourceCache.h"

%template(ImageVector)       eastl::vector<Urho3D::SharedPtr<Urho3D::Image>>;
%template(FaceVectorPair)    eastl::pair<Urho3D::CubeMapFace, Urho3D::Vector2>;
%template(FaceIntVectorPair) eastl::pair<Urho3D::CubeMapFace, Urho3D::IntVector2>;


// --------------------------------------- Scene ---------------------------------------
%ignore Urho3D::DirtyBits::data_;
%ignore Urho3D::SceneReplicationState::dirtyNodes_;		// Needs HashSet wrapped
%ignore Urho3D::NodeReplicationState::dirtyVars_;		// Needs HashSet wrapped
%ignore Urho3D::Animatable::animatedNetworkAttributes_; // Needs HashSet wrapped
%ignore Urho3D::AsyncProgress::resources_;
%ignore Urho3D::ValueAnimation::GetKeyFrames;
%ignore Urho3D::Serializable::networkState_;
%ignore Urho3D::Serializable::instanceDefaultValues_;
%ignore Urho3D::Serializable::temporary_;
%ignore Urho3D::ReplicationState::connection_;
%ignore Urho3D::Component::CleanupConnection;
%ignore Urho3D::Scene::CleanupConnection;
%ignore Urho3D::Node::CleanupConnection;
%ignore Urho3D::NodeImpl;
%ignore Urho3D::Node::GetEntity;
%ignore Urho3D::Node::SetEntity;
%ignore Urho3D::Scene::GetRegistry;
%ignore Urho3D::Scene::GetComponentIndex;
%ignore Urho3D::Animatable::animationEnabled_;
%ignore Urho3D::Animatable::objectAnimation_;
%ignore Urho3D::Component::node_;
%ignore Urho3D::Component::id_;
%ignore Urho3D::Component::enabled_;

%include "generated/Urho3D/_pre_scene.i"
%include "Urho3D/Scene/AnimationDefs.h"
%include "Urho3D/Scene/ValueAnimationInfo.h"
%include "Urho3D/Scene/Serializable.h"
%include "Urho3D/Scene/Animatable.h"
%include "Urho3D/Scene/Component.h"
%include "Urho3D/Scene/Node.h"
%include "Urho3D/Scene/ReplicationState.h"
%include "Urho3D/Scene/Scene.h"
%include "Urho3D/Scene/SplinePath.h"
%include "Urho3D/Scene/ValueAnimation.h"
%include "Urho3D/Scene/LogicComponent.h"
%include "Urho3D/Scene/ObjectAnimation.h"
%include "Urho3D/Scene/SceneResolver.h"
%include "Urho3D/Scene/SmoothedTransform.h"
%include "Urho3D/Scene/UnknownComponent.h"

// --------------------------------------- Audio ---------------------------------------
%ignore Urho3D::BufferedSoundStream::AddData(const ea::shared_array<signed char>& data, unsigned numBytes);
%ignore Urho3D::BufferedSoundStream::AddData(const ea::shared_array<signed short>& data, unsigned numBytes);
%ignore Urho3D::Sound::GetData;

%include "generated/Urho3D/_pre_audio.i"
%include "Urho3D/Audio/AudioDefs.h"
%include "Urho3D/Audio/Audio.h"
%include "Urho3D/Audio/Sound.h"
%include "Urho3D/Audio/SoundStream.h"
%include "Urho3D/Audio/BufferedSoundStream.h"
%include "Urho3D/Audio/OggVorbisSoundStream.h"
%include "Urho3D/Audio/SoundListener.h"
%include "Urho3D/Audio/SoundSource.h"
%include "Urho3D/Audio/SoundSource3D.h"

// --------------------------------------- IK ---------------------------------------
#if defined(URHO3D_IK)
%{ using Algorithm = Urho3D::IKSolver::Algorithm; %}

%include "generated/Urho3D/_pre_ik.i"
%include "Urho3D/IK/IKConstraint.h"
%include "Urho3D/IK/IKEffector.h"
%include "Urho3D/IK/IK.h"
%include "Urho3D/IK/IKSolver.h"
#endif
// --------------------------------------- Graphics ---------------------------------------
%ignore Urho3D::FrustumOctreeQuery::TestDrawables;
%ignore Urho3D::SphereOctreeQuery::TestDrawables;
%ignore Urho3D::AllContentOctreeQuery::TestDrawables;
%ignore Urho3D::PointOctreeQuery::TestDrawables;
%ignore Urho3D::BoxOctreeQuery::TestDrawables;
%ignore Urho3D::OctreeQuery::TestDrawables;
%ignore Urho3D::UpdateDrawablesWork;
%ignore Urho3D::ProcessLightWork;
%ignore Urho3D::CheckVisibilityWork;
%ignore Urho3D::CheckDrawableVisibilityWork;
%ignore Urho3D::ELEMENT_TYPESIZES;
%ignore Urho3D::ScratchBuffer;
%ignore Urho3D::Drawable::batches_;
%ignore Urho3D::Light::SetLightQueue;
%ignore Urho3D::Light::GetLightQueue;
%ignore Urho3D::Renderer::SetShadowMapFilter;
%ignore Urho3D::Renderer::SetBatchShaders;
%ignore Urho3D::Renderer::SetLightVolumeBatchShaders;
%ignore Urho3D::IndexBufferDesc;
%ignore Urho3D::VertexBufferDesc;
%ignore Urho3D::GPUObject::GetGraphics;
%ignore Urho3D::Terrain::GetHeightData; // eastl::shared_array<float>
%ignore Urho3D::Geometry::GetRawData;
%ignore Urho3D::Geometry::SetRawVertexData;
%ignore Urho3D::Geometry::SetRawIndexData;
%ignore Urho3D::Geometry::GetRawDataShared;
%ignore Urho3D::IndexBuffer::GetShadowDataShared;
%ignore Urho3D::VertexBuffer::GetShadowDataShared;
%ignore Urho3D::RenderPathCommand::outputs_;    // Needs Pair<String, CubeMapFace>
%ignore Urho3D::RenderPathCommand::textureNames_;    // Needs array of strings
%ignore Urho3D::VertexBufferMorph::morphData_;      // Needs SharedPtrArray
%ignore Urho3D::ShaderVariation::GetConstantBufferSizes;
%ignore Urho3D::DecalVertex::blendIndices_;
%ignore Urho3D::DecalVertex::blendWeights_;
%ignore Urho3D::ShaderVariation::elementSemanticNames;
%ignore Urho3D::CustomGeometry::DrawOcclusion;
%ignore Urho3D::CustomGeometry::MakeCircleGraph;
%ignore Urho3D::CustomGeometry::ProcessRayQuery;
%ignore Urho3D::OcclusionBufferData::dataWithSafety_;
%ignore Urho3D::ScenePassInfo::batchQueue_;
%ignore Urho3D::LightQueryResult;
%ignore Urho3D::View::GetLightQueues;
%ignore Urho3D::Drawable::GetMutableLightProbeTetrahedronHint;
%ignore Urho3D::Skybox::GetImage;   // Needs ImageCube
%ignore Urho3D::Drawable2D::layer_;
%ignore Urho3D::Drawable2D::orderInLayer_;
%ignore Urho3D::Drawable::worldBoundingBox_;
%ignore Urho3D::Drawable::boundingBox_;
%ignore Urho3D::Drawable::drawableFlags_;
%ignore Urho3D::Drawable::castShadows_;
%ignore Urho3D::Drawable::occluder_;
%ignore Urho3D::Drawable::occludee_;
%ignore Urho3D::Drawable::zoneDirty_;
%ignore Urho3D::Drawable::octant_;
%ignore Urho3D::Drawable::zone_;
%ignore Urho3D::Drawable::viewMask_;
%ignore Urho3D::Drawable::lightMask_;
%ignore Urho3D::Drawable::shadowMask_;
%ignore Urho3D::Drawable::zoneMask_;
%ignore Urho3D::Drawable::distance_;
%ignore Urho3D::Drawable::lodDistance_;
%ignore Urho3D::Drawable::drawDistance_;
%ignore Urho3D::Drawable::shadowDistance_;
%ignore Urho3D::Drawable::sortValue_;
%ignore Urho3D::Drawable::minZ_;
%ignore Urho3D::Drawable::maxZ_;
%ignore Urho3D::Drawable::lodBias_;
%ignore Urho3D::Drawable::maxLights_;
%ignore Urho3D::Drawable::firstLight_;
%ignore Urho3D::Drawable::lights_;
%ignore Urho3D::Drawable::vertexLights_;
%ignore Urho3D::GlobalIllumination::SampleAmbientSH;
%rename(DrawableFlags) Urho3D::DrawableFlag;

%apply void* VOID_INT_PTR {
    int *data_,
    int *Urho3D::OcclusionBuffer::GetBuffer
}
%include "generated/Urho3D/_pre_graphics.i"
%include "Urho3D/Graphics/GraphicsDefs.h"
%interface_custom("%s", "I%s", Urho3D::GPUObject);
%include "Urho3D/Graphics/GPUObject.h"
%include "Urho3D/Graphics/IndexBuffer.h"
%include "Urho3D/Graphics/VertexBuffer.h"
%include "Urho3D/Graphics/Geometry.h"
%include "Urho3D/Graphics/OcclusionBuffer.h"
%include "Urho3D/Graphics/Drawable.h"
%include "Urho3D/Graphics/OctreeQuery.h"
%interface_custom("%s", "I%s", Urho3D::Octant);
%include "Urho3D/Graphics/Octree.h"
%include "Urho3D/Graphics/RenderPath.h"
%include "Urho3D/Graphics/Viewport.h"
%include "Urho3D/Graphics/RenderSurface.h"
%include "Urho3D/Graphics/Texture.h"
%include "Urho3D/Graphics/Texture2D.h"
%include "Urho3D/Graphics/Texture2DArray.h"
%include "Urho3D/Graphics/Texture3D.h"
%include "Urho3D/Graphics/TextureCube.h"
//%include "Urho3D/Graphics/Batch.h"
%include "Urho3D/Graphics/Skeleton.h"
%include "Urho3D/Graphics/Model.h"
%include "Urho3D/Graphics/StaticModel.h"
%include "Urho3D/Graphics/StaticModelGroup.h"
%include "Urho3D/Graphics/Animation.h"
%include "Urho3D/Graphics/AnimationState.h"
%include "Urho3D/Graphics/AnimationController.h"
%include "Urho3D/Graphics/AnimatedModel.h"
%include "Urho3D/Graphics/BillboardSet.h"
%include "Urho3D/Graphics/DecalSet.h"
%include "Urho3D/Graphics/Light.h"
%include "Urho3D/Graphics/ConstantBuffer.h"
%include "Urho3D/Graphics/ShaderVariation.h"
%include "Urho3D/Graphics/ShaderPrecache.h"
%include "Urho3D/Graphics/Tangent.h"
//%include "Urho3D/Graphics/VertexDeclaration.h"
%include "Urho3D/Graphics/Camera.h"
%include "Urho3D/Graphics/GlobalIllumination.h"
%include "Urho3D/Graphics/View.h"
%include "Urho3D/Graphics/Material.h"
%include "Urho3D/Graphics/CustomGeometry.h"
%include "Urho3D/Graphics/ParticleEffect.h"
%include "Urho3D/Graphics/RibbonTrail.h"
%include "Urho3D/Graphics/Technique.h"
%include "Urho3D/Graphics/ParticleEmitter.h"
%include "Urho3D/Graphics/Shader.h"
%include "Urho3D/Graphics/Skybox.h"
%include "Urho3D/Graphics/TerrainPatch.h"
%include "Urho3D/Graphics/Terrain.h"
%include "Urho3D/Graphics/DebugRenderer.h"
%include "Urho3D/Graphics/Zone.h"
%include "Urho3D/Graphics/Renderer.h"
%include "Urho3D/Graphics/Graphics.h"

// --------------------------------------- Navigation ---------------------------------------
#if defined(URHO3D_NAVIGATION)
%apply void* VOID_INT_PTR {
	rcContext*,
	dtTileCacheContourSet*,
	dtTileCachePolyMesh*,
	dtTileCacheAlloc*,
	dtQueryFilter*,
	rcCompactHeightfield*,
	rcContourSet*,
	rcHeightfield*,
	rcHeightfieldLayerSet*,
	rcPolyMesh*,
	rcPolyMeshDetail*
}
%ignore Urho3D::CrowdManager::SetVelocityShader;
%ignore Urho3D::NavBuildData::navAreas_;
%ignore Urho3D::NavigationMesh::FindPath;
%include "generated/Urho3D/_pre_navigation.i"
%include "Urho3D/Navigation/CrowdAgent.h"
%include "Urho3D/Navigation/CrowdManager.h"
%include "Urho3D/Navigation/NavigationMesh.h"
%include "Urho3D/Navigation/DynamicNavigationMesh.h"
%include "Urho3D/Navigation/NavArea.h"
%include "Urho3D/Navigation/NavBuildData.h"
%include "Urho3D/Navigation/Navigable.h"
%include "Urho3D/Navigation/Obstacle.h"
%include "Urho3D/Navigation/OffMeshConnection.h"
%template(CrowdAgentArray)       eastl::vector<Urho3D::CrowdAgent*>;
#endif

// --------------------------------------- Network ---------------------------------------
#if defined(URHO3D_NETWORK)
%ignore Urho3D::Network::MakeHttpRequest;
%ignore Urho3D::PackageDownload;
%ignore Urho3D::PackageUpload;

// These methods use forward-declared types from SLikeNet.
%ignore Urho3D::Connection::Connection;
%ignore Urho3D::Connection::Initialize;
%ignore Urho3D::Connection::GetAddressOrGUID;
%ignore Urho3D::Connection::SetAddressOrGUID;
%ignore Urho3D::Network::HandleMessage;
%ignore Urho3D::Network::NewConnectionEstablished;
%ignore Urho3D::Network::ClientDisconnected;
%ignore Urho3D::Network::GetConnection;
%ignore Urho3D::Network::OnServerConnect;
%ignore Urho3D::Network::HandleIncomingPacket;

%include "generated/Urho3D/_pre_network.i"
%include "Urho3D/Network/Connection.h"
%include "Urho3D/Network/Network.h"
%include "Urho3D/Network/NetworkPriority.h"
%include "Urho3D/Network/Protocol.h"

%template(ConnectionVector) eastl::vector<Urho3D::SharedPtr<Urho3D::Connection>>;
#endif

//// --------------------------------------- Physics ---------------------------------------
#if defined(URHO3D_PHYSICS)
%ignore Urho3D::TriangleMeshData::meshInterface_;
%ignore Urho3D::TriangleMeshData::shape_;
%ignore Urho3D::TriangleMeshData::infoMap_;
%ignore Urho3D::GImpactMeshData::meshInterface_;
%ignore Urho3D::HeightfieldData::heightData_;
%ignore Urho3D::ConvexData::indexData_;
%ignore Urho3D::ConvexData::vertexData_;
%ignore Urho3D::PhysicsWorld::GetTriMeshCache;
%ignore Urho3D::PhysicsWorld::GetGImpactTrimeshCache;
%ignore Urho3D::PhysicsWorld::GetConvexCache;
%ignore Urho3D::RigidBody::getWorldTransform;
%ignore Urho3D::RigidBody::setWorldTransform;
%apply void* VOID_INT_PTR {
	btCollisionConfiguration*,
	btCollisionShape*,
	btCompoundShape*,
	btDiscreteDynamicsWorld*,
	btPersistentManifold*,
	btRigidBody*,
	btTypedConstraint*
}

#ifdef BT_USE_DOUBLE_PRECISION
#error Not supported.
#else
%apply float { btScalar }
#endif

%include "generated/Urho3D/_pre_physics.i"
%include "Urho3D/Physics/CollisionShape.h"
%include "Urho3D/Physics/Constraint.h"
%include "Urho3D/Physics/PhysicsWorld.h"
%include "Urho3D/Physics/RaycastVehicle.h"
%include "Urho3D/Physics/RigidBody.h"
%template(PhysicsRaucastResultVector)   eastl::vector<Urho3D::PhysicsRaycastResult>;
%template(RigidBodyVector)              eastl::vector<Urho3D::RigidBody*>;
#endif
// --------------------------------------- SystemUI ---------------------------------------
#if defined(URHO3D_SYSTEMUI)
%ignore SystemUI::GetValueCache;
using ImGuiConfigFlags = unsigned;
%ignore ToImGui;
%ignore ToIntVector2;
%ignore ToIntRect;
%ignore ImGui::IsMouseDown;
%ignore ImGui::IsMouseDoubleClicked;
%ignore ImGui::IsMouseDragging;
%ignore ImGui::IsMouseReleased;
%ignore ImGui::IsMouseClicked;
%ignore ImGui::IsItemClicked;
%ignore ImGui::SetDragDropVariant;
%ignore ImGui::AcceptDragDropVariant;
%ignore ImGui::dpx;
%ignore ImGui::dpy;
%ignore ImGui::dp;
%ignore ImGui::pdpx;
%ignore ImGui::pdpy;
%ignore ImGui::pdp;
%apply unsigned short INPUT[] { ImWchar* };

%include "generated/Urho3D/_pre_systemui.i"
%include "Urho3D/SystemUI/Console.h"
%include "Urho3D/SystemUI/DebugHud.h"
%include "Urho3D/SystemUI/SystemMessageBox.h"
%include "Urho3D/SystemUI/SystemUI.h"
#endif
// --------------------------------------- UI ---------------------------------------
%ignore Urho3D::UIElement::GetBatches;
%ignore Urho3D::UIElement::GetDebugDrawBatches;
%ignore Urho3D::UIElement::GetBatchesWithOffset;

%include "generated/Urho3D/_pre_ui.i"
%include "Urho3D/UI/UI.h"
//%include "Urho3D/UI/UIBatch.h"
%include "Urho3D/UI/UIElement.h"
%include "Urho3D/UI/BorderImage.h"
%include "Urho3D/UI/UISelectable.h"
%include "Urho3D/UI/CheckBox.h"
%include "Urho3D/UI/FontFace.h"
%include "Urho3D/UI/FontFaceBitmap.h"
%include "Urho3D/UI/FontFaceFreeType.h"
%include "Urho3D/UI/Font.h"
%include "Urho3D/UI/LineEdit.h"
%include "Urho3D/UI/ProgressBar.h"
%include "Urho3D/UI/ScrollView.h"
%include "Urho3D/UI/Sprite.h"
%include "Urho3D/UI/Text.h"
%include "Urho3D/UI/Button.h"
%include "Urho3D/UI/Menu.h"
%include "Urho3D/UI/DropDownList.h"
%include "Urho3D/UI/Cursor.h"
%include "Urho3D/UI/FileSelector.h"
%include "Urho3D/UI/ListView.h"
%include "Urho3D/UI/MessageBox.h"
%include "Urho3D/UI/ScrollBar.h"
%include "Urho3D/UI/Slider.h"
%include "Urho3D/UI/Text3D.h"
%include "Urho3D/UI/ToolTip.h"
%include "Urho3D/UI/UIComponent.h"
%include "Urho3D/UI/Window.h"
%include "Urho3D/UI/View3D.h"
%nocsattribute Urho3D::LineEdit::GetCursor;

// --------------------------------------- Urho2D ---------------------------------------
#if URHO3D_URHO2D
%rename(GetMapType) Urho3D::TileMapObject2D::GetType;
%rename(GetLayerType) Urho3D::TmxLayer2D::GetType;

%ignore Urho3D::AnimationSet2D::GetSpriterData;
%ignore Urho3D::PhysicsWorld2D::DrawTransform;

// SWIG applies `override new` modifier by mistake.
%csmethodmodifiers Urho3D::Drawable2D::OnSceneSet "protected override";
%csmethodmodifiers Urho3D::Drawable2D::OnMarkedDirty "protected override";

%apply float { float32 };
%apply int { int32 };
%apply void* VOID_INT_PTR {
	b2Body*,
	b2Contact*,
	b2Fixture*,
	b2Joint*,
	b2Manifold*,
	b2World*
}

// b2Draw implementation
%ignore Urho3D::PhysicsWorld2D::DrawPolygon;
%ignore Urho3D::PhysicsWorld2D::DrawSolidPolygon;
%ignore Urho3D::PhysicsWorld2D::DrawCircle;
%ignore Urho3D::PhysicsWorld2D::DrawSolidCircle;
%ignore Urho3D::PhysicsWorld2D::DrawSegment;
%ignore Urho3D::PhysicsWorld2D::DrawTransform;
%ignore Urho3D::PhysicsWorld2D::DrawPoint;

%ignore Urho3D::ViewBatchInfo2D;
%ignore Urho3D::SourceBatch2D;
%ignore Urho3D::Vertex2D;
%ignore Urho3D::Drawable2D::GetSourceBatches;
%ignore Urho3D::TileMap2D::SetTmxFile;
%ignore Urho3D::TileMapLayer2D::Initialize;
%ignore Urho3D::TileMapLayer2D::GetTmxLayer;
%ignore Urho3D::Drawable2D::sourceBatches_;
%ignore Urho3D::TileMap2D::GetTmxFile;

%include "generated/Urho3D/_pre_urho2d.i"
%include "Urho3D/Urho2D/Drawable2D.h"
%include "Urho3D/Urho2D/StaticSprite2D.h"
%include "Urho3D/Urho2D/AnimatedSprite2D.h"
%include "Urho3D/Urho2D/TileMapDefs2D.h"
%include "Urho3D/Urho2D/AnimationSet2D.h"
%include "Urho3D/Urho2D/ParticleEffect2D.h"
%include "Urho3D/Urho2D/Renderer2D.h"
%include "Urho3D/Urho2D/SpriteSheet2D.h"
%include "Urho3D/Urho2D/TileMapLayer2D.h"
%include "Urho3D/Urho2D/ParticleEmitter2D.h"
%include "Urho3D/Urho2D/Sprite2D.h"
%include "Urho3D/Urho2D/StretchableSprite2D.h"
%include "Urho3D/Urho2D/TileMap2D.h"

%include "Urho3D/Urho2D/CollisionShape2D.h"
%include "Urho3D/Urho2D/CollisionPolygon2D.h"
%include "Urho3D/Urho2D/CollisionEdge2D.h"
%include "Urho3D/Urho2D/CollisionChain2D.h"
%include "Urho3D/Urho2D/CollisionCircle2D.h"
%include "Urho3D/Urho2D/CollisionBox2D.h"
%include "Urho3D/Urho2D/Constraint2D.h"
%include "Urho3D/Urho2D/ConstraintFriction2D.h"
%include "Urho3D/Urho2D/ConstraintPulley2D.h"
%include "Urho3D/Urho2D/ConstraintGear2D.h"
%include "Urho3D/Urho2D/ConstraintRevolute2D.h"
%include "Urho3D/Urho2D/ConstraintMotor2D.h"
%include "Urho3D/Urho2D/ConstraintRope2D.h"
%include "Urho3D/Urho2D/ConstraintMouse2D.h"
%include "Urho3D/Urho2D/ConstraintWeld2D.h"
%include "Urho3D/Urho2D/ConstraintDistance2D.h"
%include "Urho3D/Urho2D/ConstraintPrismatic2D.h"
%include "Urho3D/Urho2D/ConstraintWheel2D.h"
%include "Urho3D/Urho2D/RigidBody2D.h"
%include "Urho3D/Urho2D/PhysicsWorld2D.h"

%template(Sprite2DMap) eastl::unordered_map<eastl::string, Urho3D::SharedPtr<Urho3D::Sprite2D>>;
%template(PhysicsRaycastResult2DArray) eastl::vector<Urho3D::PhysicsRaycastResult2D>;
%template(RigitBody2DArray) eastl::vector<Urho3D::RigidBody2D*>;
%template(MaterialVector) eastl::vector<Urho3D::SharedPtr<Urho3D::Material>>;
%template(TileMapObject2DVector) eastl::vector<Urho3D::SharedPtr<Urho3D::TileMapObject2D>>;
#endif

%template(VariantMap)                   eastl::unordered_map<Urho3D::StringHash, Urho3D::Variant>;
%template(AttributeMap)                 eastl::unordered_map<Urho3D::StringHash, eastl::vector<Urho3D::AttributeInfo>>;
%template(PackageMap)                   eastl::unordered_map<eastl::string, Urho3D::PackageEntry>;
%template(JSONObject)                   eastl::map<eastl::string, Urho3D::JSONValue>;
%template(ResourceGroupMap)             eastl::unordered_map<Urho3D::StringHash, Urho3D::ResourceGroup>;
%template(ResourceMap)                  eastl::unordered_map<Urho3D::StringHash, Urho3D::SharedPtr<Urho3D::Resource>>;
%template(PListValueMap)                eastl::unordered_map<eastl::string, Urho3D::PListValue>;
%template(ComponentReplicationStateMap) eastl::unordered_map<unsigned int, Urho3D::ComponentReplicationState>;
%template(NodeReplicationStateMap)      eastl::unordered_map<unsigned int, Urho3D::NodeReplicationState>;
%template(ValueAnimationInfoMap)        eastl::unordered_map<eastl::string, Urho3D::SharedPtr<Urho3D::ValueAnimationInfo>>;
%template(AnimationTrackMap)            eastl::unordered_map<Urho3D::StringHash, Urho3D::AnimationTrack>;
%template(MaterialShaderParameterMap)   eastl::unordered_map<Urho3D::StringHash, Urho3D::MaterialShaderParameter>;
%template(TextureMap)                   eastl::unordered_map<Urho3D::TextureUnit, Urho3D::SharedPtr<Urho3D::Texture>>;
%template(AttributeAnimationInfos)      eastl::unordered_map<eastl::string, Urho3D::SharedPtr<Urho3D::AttributeAnimationInfo>>;
%template(VertexBufferMorphMap)         eastl::unordered_map<unsigned, Urho3D::VertexBufferMorph>;
%template(ShaderParameterMap)           eastl::unordered_map<Urho3D::StringHash, Urho3D::ShaderParameter>;
%template(ObjectMap)                    eastl::unordered_map<Urho3D::StringHash, Urho3D::SharedPtr<Urho3D::Object>>;
%template(TextureMap)                   eastl::unordered_map<Urho3D::TextureUnit, Urho3D::SharedPtr<Urho3D::Texture>>;

using Vector3 = Urho3D::Vector3;
%template(StringHashList)                   eastl::vector<Urho3D::StringHash>;
%template(Vector2List)                      eastl::vector<Urho3D::Vector2>;
%template(Vector3List)                      eastl::vector<Urho3D::Vector3>;
%template(Vector3Matrix)                    eastl::vector<eastl::vector<Urho3D::Vector3>>;
%template(Vector4List)                      eastl::vector<Urho3D::Vector4>;
%template(IntVector2List)                   eastl::vector<Urho3D::IntVector2>;
%template(IntVector3List)                   eastl::vector<Urho3D::IntVector3>;
%template(QuaternionList)                   eastl::vector<Urho3D::Quaternion>;
%template(RectList)                         eastl::vector<Urho3D::Rect>;
%template(IntRectList)                      eastl::vector<Urho3D::IntRect>;
%template(Matrix3x4List)                    eastl::vector<Urho3D::Matrix3x4>;
%template(BoolArray)                        eastl::vector<bool>;
%template(CharArray)                        eastl::vector<char>;
%template(ShortArray)                       eastl::vector<short>;
%template(IntArray)                         eastl::vector<int>;
%template(ByteVector)                       eastl::vector<unsigned char>;
%template(UShortArray)                      eastl::vector<unsigned short>;
%template(UIntArray)                        eastl::vector<unsigned int>;
%template(FloatArray)                       eastl::vector<float>;
%template(DoubleArray)                      eastl::vector<double>;

%template(ObjectList)                       eastl::vector<Urho3D::Object*>;
%template(SoundSourceList)                  eastl::vector<Urho3D::SoundSource*>;
//%template(BatchList)                      eastl::vector<Urho3D::Batch*>;
//%template(BatchGroupList)                 eastl::vector<Urho3D::BatchGroup*>;
%template(ComponentList)                    eastl::vector<Urho3D::Component*>;
%template(ComponentRefList)                 eastl::vector<Urho3D::SharedPtr<Urho3D::Component>>;
%template(DrawableList)                     eastl::vector<Urho3D::Drawable*>;
%template(ImageList)                        eastl::vector<Urho3D::Image*>;
%template(LightList)                        eastl::vector<Urho3D::Light*>;
%template(NodeList)                         eastl::vector<Urho3D::Node*>;
%template(NodeRefList)                      eastl::vector<Urho3D::SharedPtr<Urho3D::Node>>;
%template(PassList)                         eastl::vector<Urho3D::Pass*>;
%template(ReplicationStateList)             eastl::vector<Urho3D::ReplicationState*>;
%template(ResourceList)                     eastl::vector<Urho3D::Resource*>;
//%template(RigidBodyList)                  eastl::vector<Urho3D::RigidBody*>;
%template(UIElementList)                    eastl::vector<Urho3D::UIElement*>;
%template(UIElementRefList)                 eastl::vector<Urho3D::SharedPtr<Urho3D::UIElement>>;
%template(VAnimEventFrameList)              eastl::vector<const Urho3D::VAnimEventFrame*>;
%template(VertexElementList)                eastl::vector<Urho3D::VertexElement>;
%template(VertexBufferList)                 eastl::vector<Urho3D::VertexBuffer*>;
%template(VertexBufferRefList)              eastl::vector<Urho3D::SharedPtr<Urho3D::VertexBuffer>>;
%template(IndexBufferList)                  eastl::vector<Urho3D::IndexBuffer*>;
%template(IndexBufferRefList)               eastl::vector<Urho3D::SharedPtr<Urho3D::IndexBuffer>>;
%template(BillboardList)                    eastl::vector<Urho3D::Billboard>;
%template(DecalVertexList)                  eastl::vector<Urho3D::DecalVertex>;
%template(CustomGeometryVerticesList)       eastl::vector<Urho3D::CustomGeometryVertex>;
%template(CustomGeometryVerticesMatrix)     eastl::vector<eastl::vector<Urho3D::CustomGeometryVertex>>;
%template(RayQueryResultList)               eastl::vector<Urho3D::RayQueryResult>;
%template(SourceBatchList)                  eastl::vector<Urho3D::SourceBatch>;
%template(CameraList)                       eastl::vector<Urho3D::Camera*>;

%template(StringList)                       eastl::vector<eastl::string>;
%template(VariantList)                      eastl::vector<Urho3D::Variant>;
%template(AttributeInfoList)                eastl::vector<Urho3D::AttributeInfo>;
%template(JSONList)                         eastl::vector<Urho3D::JSONValue>;
%template(PListValueList)                   eastl::vector<Urho3D::PListValue>;
%template(PackageFileList)                  eastl::vector<Urho3D::SharedPtr<Urho3D::PackageFile>>;
%template(Texture2DList)                    eastl::vector<Urho3D::SharedPtr<Urho3D::Texture2D>>;
//%template(VAnimKeyFrameList)              eastl::vector<Urho3D::VAnimKeyFrame>; // some issue with const
%template(GeometryList)                     eastl::vector<Urho3D::SharedPtr<Urho3D::Geometry>>;
//%template(ConnectionList)                 eastl::vector<Urho3D::SharedPtr<Urho3D::Connection>>;
%template(GeometriesList)                   eastl::vector<eastl::vector<Urho3D::SharedPtr<Urho3D::Geometry>>>;
%template(RenderPathCommandList)            eastl::vector<Urho3D::RenderPathCommand>;
%template(RenderTargetInfoList)             eastl::vector<Urho3D::RenderTargetInfo>;
%template(BonesList)                        eastl::vector<Urho3D::Bone>;
%template(AnimationControlList)             eastl::vector<Urho3D::AnimationControl>;
%template(ModelMorphList)                   eastl::vector<Urho3D::ModelMorph>;
%template(AnimationStateList)               eastl::vector<Urho3D::SharedPtr<Urho3D::AnimationState>>;
%template(UIntArrayList)                    eastl::vector<eastl::vector<unsigned int>>;
%template(Matrix3x4ArrayList)               eastl::vector<eastl::vector<Urho3D::Matrix3x4>>;
%template(AnimationKeyFrameList)            eastl::vector<Urho3D::AnimationKeyFrame>;
%template(AnimationTrackList)               eastl::vector<Urho3D::AnimationTrack>;
%template(AnimationTriggerPointList)        eastl::vector<Urho3D::AnimationTriggerPoint>;
%template(ShaderVariationList)              eastl::vector<Urho3D::SharedPtr<Urho3D::ShaderVariation>>;
%template(ColorFrameList)                   eastl::vector<Urho3D::ColorFrame>;
%template(TextureFrameList)                 eastl::vector<Urho3D::TextureFrame>;
%template(TechniqueEntryList)               eastl::vector<Urho3D::TechniqueEntry>;
%template(CustomGeometryVerticesList)       eastl::vector<eastl::vector<Urho3D::CustomGeometryVertex>>;
%template(ComponentVector2)                 eastl::vector<Urho3D::WeakPtr<Urho3D::Component>>;
