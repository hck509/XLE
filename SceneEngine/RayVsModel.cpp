// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RayVsModel.h"
#include "SceneEngineUtility.h"
#include "LightingParser.h"
#include "../RenderCore/Techniques/ResourceBox.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../BufferUploads/DataPacket.h"

#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/DX11/Metal/DX11Utils.h"

namespace SceneEngine
{
    class RayVsModelResources
    {
    public:
        class Desc 
        {
        public:
            unsigned _elementSize;
            unsigned _elementCount;
            Desc(unsigned elementSize, unsigned elementCount) : _elementSize(elementSize), _elementCount(elementCount) {}
        };
        intrusive_ptr<ID3D::Buffer> _streamOutputBuffer;
        intrusive_ptr<ID3D::Resource> _clearedBuffer;
        intrusive_ptr<ID3D::Resource> _cpuAccessBuffer;
        RayVsModelResources(const Desc&);
    };

    RayVsModelResources::RayVsModelResources(const Desc& desc)
    {
        using namespace BufferUploads;
        using namespace RenderCore::Metal;
        auto& uploads = *SceneEngine::GetBufferUploads();

        LinearBufferDesc lbDesc;
        lbDesc._structureByteSize = desc._elementSize;
        lbDesc._sizeInBytes = desc._elementSize * desc._elementCount;

        BufferDesc bufferDesc = CreateDesc(
            BindFlag::StreamOutput, 0, GPUAccess::Read | GPUAccess::Write,
            lbDesc, "RayVsModelBuffer");
        
        auto soRes = uploads.Transaction_Immediate(bufferDesc, nullptr)->AdoptUnderlying();
        _streamOutputBuffer = QueryInterfaceCast<ID3D::Buffer>(soRes);

        _cpuAccessBuffer = uploads.Transaction_Immediate(
            CreateDesc(0, CPUAccess::Read, 0, lbDesc, "RayVsModelCopyBuffer"), nullptr)->AdoptUnderlying();

        auto pkt = CreateEmptyPacket(bufferDesc);
        XlSetMemory(pkt->GetData(), 0, pkt->GetDataSize());
        _clearedBuffer = uploads.Transaction_Immediate(
            CreateDesc(
                BindFlag::StreamOutput, 0, GPUAccess::Read | GPUAccess::Write,
                lbDesc, "RayVsModelClearingBuffer"), 
            pkt.get())->AdoptUnderlying();
    }

    auto RayVsModelStateContext::GetResults() -> std::vector<ResultEntry>
    {
        std::vector<ResultEntry> result;

            // We must lock the stream output buffer, and look for results within it
            // it seems that this kind of thing wasn't part of the original intentions
            // for stream output. So the results can appear anywhere within the buffer.
            // We have to search for non-zero entries. Results that haven't been written
            // to will appear zeroed out.
        _devContext->GetUnderlying()->CopyResource(_res->_cpuAccessBuffer.get(), _res->_streamOutputBuffer.get());

        D3D11_MAPPED_SUBRESOURCE mappedSub;
        auto hresult = _devContext->GetUnderlying()->Map(
            _res->_cpuAccessBuffer.get(), 0, D3D11_MAP_READ, 0, &mappedSub);
        if (SUCCEEDED(hresult)) {

            const auto* mappedData = (const ResultEntry*)mappedSub.pData;
            unsigned resultCount = 0;
            for (unsigned c=0; c<s_maxResultCount; ++c) {
                if (mappedData[c]._depthAsInt) { ++resultCount; }
            }
            result.reserve(resultCount);
            for (unsigned c=0; c<s_maxResultCount; ++c) {
                if (mappedData[c]._depthAsInt) { 
                    result.push_back(mappedData[c]);
                }
            }

            _devContext->GetUnderlying()->Unmap(_res->_cpuAccessBuffer.get(), 0);
        }

        return result;
    }

    void RayVsModelStateContext::SetRay(const std::pair<Float3, Float3> worldSpaceRay)
    {
        float rayLength = Magnitude(worldSpaceRay.second - worldSpaceRay.first);
        struct RayDefinitionCBuffer
        {
            Float3 _rayStart;
            float _rayLength;
            Float3 _rayDirection;
            unsigned _dummy;
        } rayDefinitionCBuffer = 
        {
            worldSpaceRay.first, rayLength,
            (worldSpaceRay.second - worldSpaceRay.first) / rayLength, 0
        };

        _devContext->BindGS(RenderCore::MakeResourceList(
            RenderCore::Metal::ConstantBuffer(&rayDefinitionCBuffer, sizeof(rayDefinitionCBuffer))));
    }

    RayVsModelStateContext::RayVsModelStateContext(
        RenderCore::Metal::DeviceContext* devContext,
        const RenderCore::Techniques::TechniqueContext& techniqueContext,
        const RenderCore::Techniques::CameraDesc* cameraForLOD)
    : _devContext(devContext)
    , _parserContext(nullptr, techniqueContext)
    {
        assert(devContext);
        using namespace RenderCore::Metal;

            // We're doing the intersection test in the geometry shader. This means
            // we have to setup a projection transform to avoid removing any potential
            // intersection results during screen-edge clipping.
            // Also, if we want to know the triangle pts and barycentric coordinates,
            // we need to make sure that no clipping occurs.
            // The easiest way to prevent clipping would be use a projection matrix that
            // would transform all points into a single point in the center of the view
            // frustum.
        ViewportDesc newViewport(0.f, 0.f, float(255.f), float(255.f), 0.f, 1.f);
        devContext->Bind(newViewport);

        RenderingQualitySettings qualitySettings(UInt2(256, 256));

        Float4x4 specialProjMatrix = MakeFloat4x4(
            0.f, 0.f, 0.f, 0.5f,
            0.f, 0.f, 0.f, 0.5f,
            0.f, 0.f, 0.f, 0.5f,
            0.f, 0.f, 0.f, 1.f);

            // The camera settings can affect the LOD that objects a rendered with.
            // So, in some cases we need to initialise the camera to the same state
            // used in rendering. This will ensure that we get the right LOD behaviour.
        RenderCore::Techniques::CameraDesc camera;
        if (cameraForLOD) { camera = *cameraForLOD; }

        LightingParser_SetupScene(
            devContext, _parserContext, camera, qualitySettings);
        LightingParser_SetGlobalTransform(
            devContext, _parserContext, camera, qualitySettings._dimensions[0], qualitySettings._dimensions[1],
            &specialProjMatrix);

        _oldSO = RenderCore::Metal::GeometryShader::GetDefaultStreamOutputInitializers();

        static const InputElementDesc eles[] = {
            InputElementDesc("INTERSECTIONDEPTH",   0, NativeFormat::R32_FLOAT),
            InputElementDesc("POINT",               0, NativeFormat::R32G32B32A32_FLOAT),
            InputElementDesc("POINT",               1, NativeFormat::R32G32B32A32_FLOAT),
            InputElementDesc("POINT",               2, NativeFormat::R32G32B32A32_FLOAT)
        };

        static const unsigned strides[] = { sizeof(ResultEntry) };
        GeometryShader::SetDefaultStreamOutputInitializers(
            GeometryShader::StreamOutputInitializers(eles, dimof(eles), strides, dimof(strides)));

        _res = &RenderCore::Techniques::FindCachedBox<RayVsModelResources>(
            RayVsModelResources::Desc(sizeof(ResultEntry), s_maxResultCount));

            // the only way to clear these things is copy from another buffer...
        devContext->GetUnderlying()->CopyResource(_res->_streamOutputBuffer.get(), _res->_clearedBuffer.get());

        ID3D::Buffer* targets[] = { _res->_streamOutputBuffer.get() };
        unsigned offsets[] = { 0 };
        devContext->GetUnderlying()->SOSetTargets(dimof(targets), targets, offsets);

        devContext->BindGS(RenderCore::MakeResourceList(RenderCore::Techniques::CommonResources()._defaultSampler));
    }

    RayVsModelStateContext::~RayVsModelStateContext()
    {
        assert(_devContext);
        _devContext->GetUnderlying()->SOSetTargets(0, nullptr, nullptr);
        RenderCore::Metal::GeometryShader::SetDefaultStreamOutputInitializers(_oldSO);
    }


}


