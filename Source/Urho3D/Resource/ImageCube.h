//
// Copyright (c) 2020-2020 the Urho3D project.
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

#include "../Graphics/GraphicsDefs.h"
#include "../Resource/Image.h"
#include "../Resource/Resource.h"
#include "../Math/SphericalHarmonics.h"

#include <EASTL/vector.h>
#include <EASTL/utility.h>

namespace Urho3D
{

class Deserializer;
class Image;
class XMLFile;

/// Cube texture resource.
class URHO3D_API ImageCube : public Resource
{
    URHO3D_OBJECT(ImageCube, Resource);

public:
    /// Construct.
    explicit ImageCube(Context* context);
    /// Destruct.
    ~ImageCube() override;
    /// Register object factory.
    static void RegisterObject(Context* context);

    /// Load resource from stream. May be called from a worker thread. Return true if successful.
    bool BeginLoad(Deserializer& source) override;

    /// Return face images.
    const ea::vector<SharedPtr<Image>>& GetImages() const { return faceImages_; }
    /// Return parameters XML.
    XMLFile* GetParametersXML() const { return parametersXml_; }
    /// Return image data from a face's zero mip level.
    Image* GetImage(CubeMapFace face) const { return faceImages_[face]; }
    /// Return mip level used for SH calculation.
    unsigned GetSphericalHarmonicsMipLevel() const;
    /// Return decompressed cube image mip level.
    SharedPtr<ImageCube> GetDecompressedImageLevel(unsigned index) const;
    /// Return decompressed cube image.
    SharedPtr<ImageCube> GetDecompressedImage() const;

    /// Return nearest pixel color at given direction.
    Color SampleNearest(const Vector3& direction) const;
    /// Return offset from the center of the unit cube for given texel (assuming zero mip level).
    Vector3 ProjectTexelOnCube(CubeMapFace face, int x, int y) const;
    /// Return offset from the center of the unit cube for given texel.
    Vector3 ProjectTexelOnCubeLevel(CubeMapFace face, int x, int y, unsigned level) const;
    /// Project direction on texel of cubemap face.
    ea::pair<CubeMapFace, IntVector2> ProjectDirectionOnFaceTexel(const Vector3& direction) const;
    /// Calculate spherical harmonics for the cube map.
    SphericalHarmonicsColor9 CalculateSphericalHarmonics() const;

    /// Project UV onto cube.
    static Vector3 ProjectUVOnCube(CubeMapFace face, const Vector2& uv);
    /// Project direction onto cubemap.
    static ea::pair<CubeMapFace, Vector2> ProjectDirectionOnFace(const Vector3& direction);

private:
    /// Face images.
    ea::vector<SharedPtr<Image>> faceImages_;
    /// Parameter file.
    SharedPtr<XMLFile> parametersXml_;
    /// Cube width.
    int width_{};
};

}
