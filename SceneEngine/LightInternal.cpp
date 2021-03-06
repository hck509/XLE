#include "LightInternal.h"
#include "LightDesc.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../Math/Transformations.h"
#include "../Math/ProjectionMath.h"
#include "../Utility/MemoryUtils.h"

namespace SceneEngine
{
    void BuildShadowConstantBuffers(
        CB_ArbitraryShadowProjection& arbitraryCBSource,
        CB_OrthoShadowProjection& orthoCBSource,
        const MultiProjection<MaxShadowTexturesPerLight>& desc)
    {
        auto frustumCount = desc._count;

        XlZeroMemory(arbitraryCBSource);   // unused array elements must be zeroed out
        XlZeroMemory(orthoCBSource);       // unused array elements must be zeroed out

        typedef MultiProjection<MaxShadowTexturesPerLight>::Mode Mode;
        if (desc._mode == Mode::Arbitrary) {

            arbitraryCBSource._projectionCount = frustumCount;
            for (unsigned c=0; c<frustumCount; ++c) {
                arbitraryCBSource._worldToProj[c] = 
                    Combine(  
                        desc._fullProj[c]._viewMatrix, 
                        desc._fullProj[c]._projectionMatrix);
                arbitraryCBSource._minimalProj[c] = desc._minimalProjection[c];
            }

        } else if (desc._mode == Mode::Ortho) {

            using namespace RenderCore;

                //  We can still fill in the constant buffer
                //  for arbitrary projection. If we use the right
                //  shader, we shouldn't need this. But we can have it
                //  prepared for comparisons.
            arbitraryCBSource._projectionCount = frustumCount;
            auto baseWorldToProj = desc._definitionViewMatrix;

            float p22 = 1.f, p23 = 0.f;

            for (unsigned c=0; c<frustumCount; ++c) {
                const auto& mins = desc._orthoSub[c]._projMins;
                const auto& maxs = desc._orthoSub[c]._projMaxs;
                using namespace RenderCore::Techniques;
                Float4x4 projMatrix = OrthogonalProjection(
                    mins[0], mins[1], maxs[0], maxs[1], mins[2], maxs[2],
                    GeometricCoordinateSpace::RightHanded, GetDefaultClipSpaceType());

                arbitraryCBSource._worldToProj[c] = Combine(baseWorldToProj, projMatrix);
                arbitraryCBSource._minimalProj[c] = ExtractMinimalProjection(projMatrix);

                orthoCBSource._cascadeScale[c][0] = projMatrix(0,0);
                orthoCBSource._cascadeScale[c][1] = projMatrix(1,1);
                orthoCBSource._cascadeTrans[c][0] = projMatrix(0,3);
                orthoCBSource._cascadeTrans[c][1] = projMatrix(1,3);

                if (c==0) {
                    p22 = projMatrix(2,2);
                    p23 = projMatrix(2,3);
                }

                    // (unused parts)
                orthoCBSource._cascadeScale[c][2] = 1.f;
                orthoCBSource._cascadeTrans[c][2] = 0.f;
                orthoCBSource._cascadeScale[c][3] = 1.f;
                orthoCBSource._cascadeTrans[c][3] = 0.f;
            }

                //  Also fill in the constants for ortho projection mode
            orthoCBSource._projectionCount = frustumCount;
            orthoCBSource._minimalProjection = desc._minimalProjection[0];

                //  We merge in the transform for the z component
                //  Every cascade uses the same depth range, which means we only
                //  have to adjust the X and Y components for each cascade
            auto zComponentMerge = Identity<Float4x4>();
            zComponentMerge(2,2) = p22;
            zComponentMerge(2,3) = p23;
            orthoCBSource._worldToProj = AsFloat3x4(Combine(baseWorldToProj, zComponentMerge));
        }
    }

    void PreparedShadowFrustum::InitialiseConstants(
        RenderCore::Metal::DeviceContext* devContext,
        const MultiProjection<MaxShadowTexturesPerLight>& desc)
    {
        _frustumCount = desc._count;
        _mode = desc._mode;

        BuildShadowConstantBuffers(_arbitraryCBSource, _orthoCBSource, desc);

        using RenderCore::Metal::ConstantBuffer;
            // arbitrary constants
        if (!_arbitraryCB.GetUnderlying()) {
            _arbitraryCB = ConstantBuffer(&_arbitraryCBSource, sizeof(_arbitraryCBSource));
        } else {
            _arbitraryCB.Update(*devContext, &_arbitraryCBSource, sizeof(_arbitraryCBSource));
        }

            // ortho constants
        if (!_orthoCB.GetUnderlying()) {
            _orthoCB = ConstantBuffer(&_orthoCBSource, sizeof(_orthoCBSource));
        } else {
            _orthoCB.Update(*devContext, &_orthoCBSource, sizeof(_orthoCBSource));
        }
    }

    bool PreparedShadowFrustum::IsReady() const
    {
        return _shadowTextureResource.GetUnderlying() && (_arbitraryCB.GetUnderlying() || _orthoCB.GetUnderlying());
    }

    PreparedShadowFrustum::PreparedShadowFrustum()
    : _frustumCount(0) 
    , _mode(ShadowProjectionDesc::Projections::Mode::Arbitrary)
    {}

    PreparedShadowFrustum::PreparedShadowFrustum(PreparedShadowFrustum&& moveFrom)
    : _shadowTextureResource(std::move(moveFrom._shadowTextureResource))
    , _arbitraryCBSource(std::move(moveFrom._arbitraryCBSource))
    , _orthoCBSource(std::move(moveFrom._orthoCBSource))
    , _arbitraryCB(std::move(moveFrom._arbitraryCB))
    , _orthoCB(std::move(moveFrom._orthoCB))
    , _frustumCount(moveFrom._frustumCount)
    , _mode(moveFrom._mode)
    {}

    PreparedShadowFrustum& PreparedShadowFrustum::operator=(PreparedShadowFrustum&& moveFrom)
    {
        _shadowTextureResource = std::move(moveFrom._shadowTextureResource);
        _arbitraryCBSource = std::move(moveFrom._arbitraryCBSource);
        _orthoCBSource = std::move(moveFrom._orthoCBSource);
        _arbitraryCB = std::move(moveFrom._arbitraryCB);
        _orthoCB = std::move(moveFrom._orthoCB);
        _frustumCount = moveFrom._frustumCount;
        _mode = moveFrom._mode;
        return *this;
    }

    GlobalLightingDesc::GlobalLightingDesc() 
    : _ambientLight(0.f, 0.f, 0.f), _skyTexture(nullptr)
    , _doAtmosphereBlur(false), _doOcean(false), _doToneMap(false), _doVegetationSpawn(false) 
    {}

    ShadowProjectionDesc::ShadowProjectionDesc()
    {
        _width = _height = 0;
        _typelessFormat = _writeFormat = _readFormat = RenderCore::Metal::NativeFormat::Unknown;
        _worldToClip = Identity<Float4x4>();
    }


    RenderCore::SharedPkt BuildScreenToShadowConstants(
        unsigned frustumCount,
        const CB_ArbitraryShadowProjection& arbitraryCB,
        const CB_OrthoShadowProjection& orthoCB,
        const Float4x4& cameraToWorld)
    {
        struct Basis
        {
            Float4x4    _cameraToShadow[6];
            Float4x4    _orthoCameraToShadow;
        } basis;
        XlZeroMemory(basis);    // unused array elements must be zeroed out

        for (unsigned c=0; c<unsigned(frustumCount); ++c) {
            auto& worldToShadowProj = arbitraryCB._worldToProj[c];
            auto cameraToShadowProj = Combine(cameraToWorld, worldToShadowProj);
            basis._cameraToShadow[c] = cameraToShadowProj;
        }
        auto& worldToShadowProj = orthoCB._worldToProj;
        basis._orthoCameraToShadow = Combine(cameraToWorld, worldToShadowProj);
        return RenderCore::MakeSharedPkt(basis);
    }

}

