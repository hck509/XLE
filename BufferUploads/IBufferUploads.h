// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Types.h"
#include "../Utility/Threading/ThreadingUtils.h"     // for RefCountedObject
#include "../Utility/IntrusivePtr.h"
#include "../Utility/Mixins.h"
#include "../Utility/StringUtils.h"
#include "../RenderCore/IDevice_Forward.h"
#include "../RenderCore/Metal/Resource.h"
#include "IBufferUploads_Forward.h"
#include <vector>
#include <memory>
#include <assert.h>

#if OUTPUT_DLL
    #define buffer_upload_dll_export       dll_export
#else
    #define buffer_upload_dll_export
#endif

namespace Utility { class DefragStep; }

namespace BufferUploads
{
    /// Container for CPUAccess::Enum
    namespace CPUAccess
    {
        /// <summary>Determines CPU access privileges</summary>
        /// Determines what access (if any) the CPU will have to the
        /// buffer. This can control how the object is stored in memory
        /// in into what memory it is stored.
        ///
        /// It is always a good idea to use as few flags as possible. Many
        /// buffers should be fine with a CPUAccess flag of "0".
        ///
        /// Note that this flag can change how the system performs upload
        /// operations. For example, in D3D11, when the CPUAccess::WriteDynamic flag
        /// is set, UpdateSubresource() can't be used to some types of buffers.
        /// In these cases, Map() is used instead.
        ///
        /// Try to avoid "WriteDynamic" unless the buffer will be Locked
        /// multiple times per frame. If a buffer only needs to be updated
        /// once per frame (or less), use CPUAccess::Write
        enum Enum
        {
            Read                = 1<<0,             ///< CPU can read from this resource (using IManager::Resource_Readback)
            Write               = 1<<1,             ///< CPU can write to this resource, but will only do so once (or less) per frame
            WriteDynamic        = (1<<2)|Write      ///< CPU can write to this resource, and will lock it multiple times during a single per frame
        };
        typedef unsigned BitField;
    }

    /// Container for GPUAccess::Enum
    namespace GPUAccess
    {
        /// <summary>Determines GPU access privileges</summary>
        /// Determines whether the GPU will read from or write to a resource (or both).
        /// As usual, try to limit the privileges were possible.
        enum Enum
        {
            Read                = 1<<0,     ///< GPU can read from a resource (eg, shader resource, texture, input structured buffer)
            Write               = 1<<1      ///< GPU can write to the resource (eg, render target, RWTexture, RWStructuredBuffer)
        };
        typedef unsigned BitField;
    }

    /// Container for BindFlag::Enum
    namespace BindFlag
    {
        /// <summary>Determines how the buffer will be bound to the pipeline</summary>
        /// Most buffers are just blocks of data on the GPU. They can be bound to the
        /// pipeline in multiple ways, for different purposes. 
        /// 
        /// This flag controls how the buffer can be used. Most buffer only have a single
        /// bind flag. But sometimes we need to combine input and output binding modes
        /// eg: 
        ///     <list>
        ///         <item> BindFlag::RenderTarget | BindFlag::ShaderResource
        ///         <item> BindFlag::DepthStencil | BindFlag::ShaderResource
        ///         <item> BindFlag::StructuredBuffer | BindFlag::VertexBuffer
        ///     </list>
        enum Enum
        {
            VertexBuffer        = 1<<0,     ///< Used as an vertex buffer (ie, IASetVertexBuffers)
            IndexBuffer         = 1<<1,     ///< Used as an index buffer (ie, IASetIndexBuffer)
            ShaderResource      = 1<<2,     ///< Used as a shader resource (ie, PSSetShaderResources)
            RenderTarget        = 1<<3,     ///< Used as a render target (ie, OMSetRenderTargets)
            DepthStencil        = 1<<4,     ///< Used as a depth buffer (ie, OMSetRenderTargets)
            UnorderedAccess     = 1<<5,     ///< Used as a unordered access buffer (ie, CSSetUnorderedAccessViews)
            StructuredBuffer    = 1<<6,     ///< Used as a structured buffer (ie, CSSetShaderResources)
            ConstantBuffer      = 1<<7,     ///< Used as a constant buffer (ie, VSSetConstantBuffers)
            StreamOutput        = 1<<8,     ///< Used as a stream-output buffer from the geomtry shader (ie, SOSetTargets)
            DrawIndirectArgs    = 1<<9,     ///< Used with DrawInstancedIndirect or DrawIndexedInstancedIndirect
            RawViews            = 1<<10     ///< Enables use of raw shader resource views
        };
        typedef unsigned BitField;
    }

    /// Container for AllocationRules::Enum
    namespace AllocationRules
    {
        /// <summary>Determines how BufferUploads will allocate a resource</summary>
        /// Special flags that determine how the system will allocate a resource.
        enum Enum
        {
            Pooled              = 1<<0,     ///< If a compatible resource has been recently released, reuse it
            Batched             = 1<<1,     ///< Batch together similar uploads, so they become a single low level operation per frame
            Staging             = 1<<2,     ///< Staging memory only (ie, don't send to GPU)
            NonVolatile         = 1<<3      ///< Allow the underlying API to manage memory so that it will survive device resets (ie, create a managed resource in D3D9)
        };
        typedef unsigned BitField;
    }
        
        /////////////////////////////////////////////////

    struct LinearBufferDesc
    {
        unsigned _sizeInBytes;
        unsigned _structureByteSize;
    };

    struct TextureSamples
    {
        uint8 _sampleCount;
        uint8 _samplingQuality;
        static TextureSamples Create(uint8 sampleCount=1, uint8 samplingQuality=0)
        {
            TextureSamples result;
            result._sampleCount = sampleCount;
            result._samplingQuality = samplingQuality;
            return result;
        }
    };

    struct TextureDesc
    {
        uint32 _width, _height, _depth;
        unsigned _nativePixelFormat;
        struct Dimensionality { enum Enum { T1D, T2D, T3D, CubeMap }; };
        Dimensionality::Enum _dimensionality;
        uint8 _mipCount, _arrayCount;
        TextureSamples _samples;

        static TextureDesc Plain1D(
            uint32 width, unsigned nativePixelFormat, 
            uint8 mipCount=1, uint8 arrayCount=0);
        static TextureDesc Plain2D(
            uint32 width, uint32 height, unsigned nativePixelFormat, 
            uint8 mipCount=1, uint8 arrayCount=0, const TextureSamples& samples = TextureSamples::Create());
        static TextureDesc Plain3D(
            uint32 width, uint32 height, uint32 depth, unsigned nativePixelFormat, uint8 mipCount=1);
    };

    /// <summary>Description of a buffer</summary>
    /// Description of a buffer, used during creation operations.
    /// Usually, BufferDesc is filled with a description of a new buffer to create,
    /// and passed to IManager::Transaction_Begin.
    struct BufferDesc
    {
    public:
            // following the D3D11 style; let's use a "type" member, with a union
        struct Type { enum Enum { LinearBuffer, Texture, Unknown, Max }; };
        Type::Enum _type;
        BindFlag::BitField _bindFlags;
        CPUAccess::BitField _cpuAccess; 
        GPUAccess::BitField _gpuAccess;
        AllocationRules::BitField _allocationRules;
        union {
            LinearBufferDesc _linearBufferDesc;
            TextureDesc _textureDesc;
        };
        char _name[48];

        buffer_upload_dll_export BufferDesc();
    };

    inline BufferDesc CreateDesc(
        BindFlag::BitField bindFlags,
        CPUAccess::BitField cpuAccess, 
        GPUAccess::BitField gpuAccess,
        const TextureDesc& textureDesc,
        const char name[])
    {
        BufferDesc desc;
        desc._type = BufferDesc::Type::Texture;
        desc._bindFlags = bindFlags;
        desc._cpuAccess = cpuAccess;
        desc._gpuAccess = gpuAccess;
        desc._allocationRules = 0;
        desc._textureDesc = textureDesc;
        XlCopyString(desc._name, dimof(desc._name), name);
        return desc;
    }

    inline BufferDesc CreateDesc(
        BindFlag::BitField bindFlags,
        CPUAccess::BitField cpuAccess, 
        GPUAccess::BitField gpuAccess,
        const LinearBufferDesc& linearBufferDesc,
        const char name[])
    {
        BufferDesc desc;
        desc._type = BufferDesc::Type::LinearBuffer;
        desc._bindFlags = bindFlags;
        desc._cpuAccess = cpuAccess;
        desc._gpuAccess = gpuAccess;
        desc._allocationRules = 0;
        desc._linearBufferDesc = linearBufferDesc;
        XlCopyString(desc._name, dimof(desc._name), name);
        return desc;
    }

    struct BufferMetrics : public BufferDesc
    {
    public:
        unsigned        _systemMemorySize;
        unsigned        _videoMemorySize;
        const char*     _pixelFormatName;
    };

        /////////////////////////////////////////////////

    class RawDataPacket : public RefCountedObject
    {
    public:
            //  
            //      Note RawDataPacket will be initialised with a ref count of 1 
            //      after the constructor.
            //      so:
            //          RawDataPacket* p = new RawDataPacket_Type();
            //          p->AddRef();
            //          p->Release();
            //          p->Release();
            //
            //      will not destroy the object until the second Release();
            //
        virtual void*                           GetData             (unsigned mipIndex=0, unsigned arrayIndex=0) = 0;
        virtual size_t                          GetDataSize         (unsigned mipIndex=0, unsigned arrayIndex=0) const = 0;
        virtual std::pair<unsigned,unsigned>    GetRowAndSlicePitch (unsigned mipIndex=0, unsigned arrayIndex=0) const = 0;
    };

        /////////////////////////////////////////////////

    typedef int64 TimeMarker;

    namespace UploadDataType
    {
        enum Enum
        {
            Texture, Vertex, Index,
            Max
        };
    }

    struct AssemblyLineMetrics
    {
        unsigned _transactionCount, _temporaryTransactionsAllocated, _longTermTransactionsAllocated;
        unsigned _queuedCreates, _queuedUploads, _queuedStagingCreates;
        unsigned _queuedPeakCreates, _queuedPeakUploads, _queuedPeakStagingCreates;
        unsigned _queuedBytes[UploadDataType::Max];
        AssemblyLineMetrics();
    };

    struct AssemblyLineRetirement
    {
        BufferDesc _desc;
        TimeMarker _requestTime, _retirementTime;
    };

    struct CommandListMetrics
    {
        unsigned _bytesUploaded[UploadDataType::Max];
        unsigned _bytesCreated[UploadDataType::Max];
        unsigned _bytesUploadedDuringCreation[UploadDataType::Max];
        unsigned _bytesUploadTotal;
        unsigned _countCreations[UploadDataType::Max];
        unsigned _countDeviceCreations[UploadDataType::Max];
        unsigned _countUploaded[UploadDataType::Max];
        unsigned _contextOperations, _nonContextOperations, _deviceCreateOperations;
        AssemblyLineMetrics _assemblyLineMetrics;
        AssemblyLineRetirement _retirements[16];
        unsigned _retirementCount;
        std::vector<AssemblyLineRetirement> _retirementsOverflow;
        TimeMarker _resolveTime, _commitTime;
        TimeMarker _waitTime, _processingStart, _processingEnd;
        TimeMarker _framePriorityStallTime;
        unsigned _batchedCopyBytes, _batchedCopyCount;
        unsigned _wakeCount, _frameId;

        buffer_upload_dll_export CommandListMetrics();
        buffer_upload_dll_export CommandListMetrics(const CommandListMetrics& cloneFrom);
        buffer_upload_dll_export const CommandListMetrics& operator=(const CommandListMetrics& cloneFrom);

        unsigned RetirementCount() const                                { return unsigned(_retirementCount + _retirementsOverflow.size()); }
        const AssemblyLineRetirement& Retirement(unsigned index) const  { if (index<_retirementCount) {return _retirements[index];} return _retirementsOverflow[index-_retirementCount]; }
    };

        /////////////////////////////////////////////////

    struct PoolMetrics
    {
        BufferDesc _desc;
        unsigned _currentSize, _peakSize;
        unsigned _topMostAge;
        unsigned _recentDeviceCreateCount;
        unsigned _recentPoolCreateCount;
        unsigned _recentReleaseCount;
        unsigned _totalRealSize, _totalCreateSize, _totalCreateCount;
    };

    struct BatchedHeapMetrics
    {
        std::vector<unsigned> _markers;
        unsigned _allocatedSpace, _unallocatedSpace;
        unsigned _heapSize;
        unsigned _largestFreeBlock;
        unsigned _spaceInReferencedCountedBlocks;
        unsigned _referencedCountedBlockCount;
    };

    struct BatchingSystemMetrics
    {
        std::vector<BatchedHeapMetrics> _heaps;
    };

    struct PoolSystemMetrics
    {
        std::vector<PoolMetrics> _resourcePools;
        std::vector<PoolMetrics> _stagingPools;
        BatchingSystemMetrics _batchingSystemMetrics;
    };

        /////////////////////////////////////////////////

    typedef uint64 TransactionID;
    typedef RenderCore::Metal::Underlying::Resource UnderlyingResource;

    class IResourcePool
    {
    public:
        virtual void AddRef(
            uint64 resourceMarker, UnderlyingResource* resource, 
            unsigned offset, unsigned size) = 0;
        virtual void ReturnToPool(
            uint64 resourceMarker, intrusive_ptr<UnderlyingResource>&& resource, 
            unsigned offset, unsigned size) = 0;
        virtual ~IResourcePool() {}
    };

    class ResourceLocator : public RefCountedObject
    {
    public:
        intrusive_ptr<UnderlyingResource> AdoptUnderlying();

        bool IsEmpty() const { return !_resource; }
        unsigned Offset() const { return _offset; }
        unsigned Size() const { return _size; }
        UnderlyingResource* GetUnderlying() const { return _resource.get(); }
        std::shared_ptr<IResourcePool> Pool() { return _pool; }
        uint64 PoolMarker() { return _poolMarker; }

        ResourceLocator(
            intrusive_ptr<UnderlyingResource>&& moveFrom, unsigned offset=~unsigned(0x0), unsigned size=~unsigned(0x0), 
            std::shared_ptr<IResourcePool> pool = nullptr, uint64 poolMarker = 0);
        ResourceLocator(ResourceLocator&& moveFrom);
        ResourceLocator& operator=(ResourceLocator&& moveFrom);
        ResourceLocator();
        ~ResourceLocator();

        void swap(ResourceLocator& other);
    protected:
        intrusive_ptr<UnderlyingResource> _resource;
        unsigned _offset, _size;
        std::shared_ptr<IResourcePool> _pool;
        uint64 _poolMarker;

        ResourceLocator(const ResourceLocator& resource);
        ResourceLocator& operator=(const ResourceLocator& resource);
    };

        /////////////////////////////////////////////////

    class Event_ResourceReposition
    {
    public:
        UnderlyingResource* _originalResource;
        UnderlyingResource* _newResource;
        std::shared_ptr<IResourcePool> _pool;
        uint64 _poolMarker;
        std::vector<Utility::DefragStep> _defragSteps;
    };

    class Box2D
    {
    public:
        signed _left, _top, _right, _bottom;
        Box2D() : _left(0), _top(0), _right(0), _bottom(0) {}
        Box2D(signed left, signed top, signed right, signed bottom) : _left(left), _top(top), _right(right), _bottom(bottom) {}
    };

    bool operator==(const Box2D& lhs, const Box2D& rhs);

        /////////////////////////////////////////////////

    namespace TransactionOptions
    {
        enum {
            LongTerm         = 1<<0,
            FramePriority    = 1<<1,
            ForceCreate      = 1<<2
        };
        typedef unsigned BitField;
    }

    /// <summary>Specifies a limited part of a resource</summary>
    /// When we want to upload new data for only part of a resource
    /// (for example, just one mip map), we can use PartialResource
    /// to define a limited area within a resource
    class PartialResource
    {
    public:
        Box2D _box;
        unsigned _lodLevelMin, _lodLevelMax;
        unsigned _arrayIndex;
        
        PartialResource(const Box2D& box = Box2D(), 
                        unsigned lodLevelMin = 0, unsigned lodLevelMax = 0,
                        unsigned arrayIndex = 0)
        : _box(box), _lodLevelMin(lodLevelMin), _lodLevelMax(lodLevelMax), _arrayIndex(arrayIndex) {}
    };

        /////////////////////////////////////////////////

#define FLEX_INTERFACE Manager
/*-----------------*/ #include "../RenderCore/FlexBegin.h" /*-----------------*/

        /// <summary>Main interface for BufferUploads</summary>
        /// BufferUploads::IManager is used as the main interface for uploading
        /// data to the GPU.
        /// Normal usage involves creating a transaction, waiting for the transaction
        /// to complete, and then ending the transaction.
        ///
        /// Use BufferUploads::CreateManager() to create a new manager object.
        ///
        /// Buffer uploads can be used from a separate dll, or statically linked in.
        ///
        /// <example>
        ///     Typical usage:
        ///     <code>\code
        ///         BufferUploads::IManager& manager = ...;
        ///         BufferUploads::BufferDesc desc = ...;
        ///         intrusive_ptr<DataPacket> pkt = ...;
        ///         auto uploadTransaction = manager.Transaction_Begin(desc, initialisationData);
        ///
        ///             // later....
        ///         if (manager.IsCompleted(uploadTransaction)) {
        ///             _myResource = manager.GetResource(uploadTransaction);
        ///             manager.Transaction_End(uploadTransaction);
        ///         }
        ///     \endcode</code>
        /// </example>
    class ICLASSNAME(Manager) : noncopyable
    {
    public:
            /// \name Upload Data to an existing transaction
            /// @{

            /// <summary>Use UpdateData to change the data within an existing object</summary>
            /// Upload data for buffer uploads can be provided either to the Transaction_Begin
            /// call, or to UploadData. Use UploadData when you want to update an existing resource,
            /// or change the data that's already present.
        IMETHOD void            UpdateData  (TransactionID id, RawDataPacket* rawData, const PartialResource& = PartialResource()) IPURE;
            /// @}

            /// \name Begin and End transactions
            /// @{

            /// <summary>Begin a new transaction</summary>
            /// Begin a new transaction, either by creating a new resource, or by attaching
            /// to an existing resource.
        IMETHOD TransactionID   Transaction_Begin    (const BufferDesc& desc, RawDataPacket* initialisationData = nullptr, TransactionOptions::BitField flags=0) IPURE;
        IMETHOD TransactionID   Transaction_Begin    (intrusive_ptr<ResourceLocator> & locator, TransactionOptions::BitField flags=0) IPURE;

            /// <summary>Ends a transaction</summary>
            /// Ends a transaction started with Transaction_Begin. Internally, this updates
            /// a reference count. So every call to Transaction_Begin must be balanced with
            /// a call to Transaction_End.
            /// Be sure to end all transactions before destroying the buffer uploads manager.
        IMETHOD void            Transaction_End      (TransactionID id) IPURE;

            /// <summary>Validates a transaction</summary>
            /// This is a tool for debugging. Checks a transaction for common problems.
            /// Only implemented in _DEBUG builds. Errors will invoke an assert.
        IMETHOD void            Transaction_Validate (TransactionID id) IPURE;
            /// @}

            /// \name Immediate creation
            /// @{

            /// <summary>Create a new buffer synchronously</summary>
            /// Creates a new resource synchronously. All creating objects will
            /// execute in the current thread, and a new resource will be returned from
            /// the call. Use these methods when uploads can't be delayed.
        IMETHOD intrusive_ptr<ResourceLocator>
            Transaction_Immediate(  const BufferDesc& desc, RawDataPacket* initialisationData, 
                                    const PartialResource& = PartialResource()) IPURE;
            /// @}

            /// \name Transaction management
            /// @{

            /// <summary>Add extra ref count to transaction</summary>
            /// Adds another reference count to a transaction. Useful when a 
            /// resource is getting cloned.
            /// Should be balanced with a call to Transaction_End
            /// <seealso cref="Transaction_end"/>
        IMETHOD void            AddRef      (TransactionID id) IPURE;

            /// <summary>Checks for completion</summary>
            /// Returns true iff the given transaction has been completed.
        IMETHOD bool            IsCompleted (TransactionID id) IPURE;

            /// <summary>Gets the resource from a completed transaction</summary>
            /// After a transaction has been completed, get the resource with
            /// this method.
            /// Note that the reference count for the returned resource is 
            /// incremented by one in this method. The caller must balance 
            /// that with a call to Resource_Release().
        IMETHOD intrusive_ptr<ResourceLocator>         GetResource (TransactionID id) IPURE;
            /// @}

            /// \name Event queue
            /// @{

        typedef uint32 EventListID;
        IMETHOD EventListID     EventList_GetLatestID   () IPURE;
        IMETHOD void            EventList_Get           (EventListID id, Event_ResourceReposition*&begin, Event_ResourceReposition*&end) IPURE;
        IMETHOD void            EventList_Release       (EventListID id) IPURE;
            /// @}

            /// \name Resource references
            /// @{
        IMETHOD void            Resource_Validate           (const ResourceLocator& locator) IPURE;
            /// <summary>Read back data from a resource</summary>
            /// Read data back from a resource. Sometimes this may require copying data from
            /// the GPU onto the CPU. Note that this can have a significant effect on performance!
            /// If the resource is currently in use by the GPU, it can result in a store.
            /// Whenever possible, it's recommended to avoid using this method. It's provided for
            /// compatibility and debugging.
        IMETHOD intrusive_ptr<RawDataPacket>  Resource_ReadBack           (const ResourceLocator& locator) IPURE;
            /// @}

            /// \name Frame management
            /// @{

            /// <summary>Called every frame to update uploads</summary>
            /// Performs once-per-frame tasks. Normally called by the render device once per frame.
        IMETHOD void                    Update                  () IPURE;
            /// @}

            /// \name Utilities, profiling & debugging
            /// @{

            /// <summary>Stalls until all work queues are empty</summary>
            /// Normally should only be used during shutdown and loading.
        IMETHOD void                    Flush                   () IPURE;
            /// <summary>Gets performance metrics</summary>
            /// Gets the latest performance metrics. Internally the system
            /// maintains a queue of performance metrics. Every frame, a new
            /// set of metrics is pushed onto the queue (until the stack reaches
            /// it's maximum size).
            /// PopMetrics() will remove the next item from the queue. If there
            /// no more items, "_commitTime" will be 0.
        IMETHOD CommandListMetrics      PopMetrics              () IPURE;
            /// <summary>Returns the size of a buffer</summary>
            /// Calculates the size of a buffer from a description. This can be
            /// used to estimate the amount of GPU memory that will be used.
        IMETHOD size_t                  ByteCount               (const BufferDesc& desc) const IPURE;
            /// <summary>Returns metrics about pool memory</summary>
            /// Returns some profiling metrics related to the resource pooling
            /// buffers maintained by the system. Used by the BufferUploadDisplay
            /// for presenting profiling information.
        IMETHOD PoolSystemMetrics       CalculatePoolMetrics    () const IPURE;
            /// <summary>Sets a barrier for frame priority operations<summary>
            /// Sets a barrier, which determines the "end of frame" point for
            /// frame priority operations. This will normally be called from the same
            /// thread that begins most upload operations.
        IMETHOD void                    FramePriority_Barrier   () IPURE;
            /// @}

        IDESTRUCTOR
    };

    #if !defined(FLEX_CONTEXT_Manager)
        #define FLEX_CONTEXT_Manager     FLEX_CONTEXT_INTERFACE
    #endif

    #if defined(DOXYGEN)
        typedef IManager Base_Manager;
    #endif

    buffer_upload_dll_export std::unique_ptr<IManager>       CreateManager(RenderCore::IDevice* renderDevice);

/*-----------------*/ #include "../RenderCore/FlexEnd.h" /*-----------------*/

    inline TextureDesc TextureDesc::Plain1D(
                uint32 width, unsigned nativePixelFormat, 
                uint8 mipCount, uint8 arrayCount)
    {
        TextureDesc result;
        result._width = width;
        result._height = 1;
        result._depth = 1;
        result._nativePixelFormat = nativePixelFormat;
        result._dimensionality = Dimensionality::T1D;
        result._mipCount = mipCount;
        result._arrayCount = arrayCount;
        result._samples = TextureSamples::Create();
        return result;
    }
        
    inline TextureDesc TextureDesc::Plain2D(
                uint32 width, uint32 height, unsigned nativePixelFormat, 
                uint8 mipCount, uint8 arrayCount,
                const TextureSamples& samples)
    {
        TextureDesc result;
        result._width = width;
        result._height = height;
        result._depth = 1;
        result._nativePixelFormat = nativePixelFormat;
        result._dimensionality = Dimensionality::T2D;
        result._mipCount = mipCount;
        result._arrayCount = arrayCount;
        result._samples = samples;
        return result;
    }

    inline TextureDesc TextureDesc::Plain3D(
            uint32 width, uint32 height, uint32 depth,
            unsigned nativePixelFormat, uint8 mipCount)
    {
        TextureDesc result;
        result._width = width;
        result._height = height;
        result._depth = depth;
        result._nativePixelFormat = nativePixelFormat;
        result._dimensionality = Dimensionality::T3D;
        result._mipCount = mipCount;
        result._arrayCount = 0;
        result._samples = TextureSamples::Create();
        return result;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////

    inline intrusive_ptr<RenderCore::Metal::Underlying::Resource> ResourceLocator::AdoptUnderlying()
    {
        _offset = _size = 0;
        _pool.reset();
        _poolMarker = 0;
        return std::move(_resource);
    }

    inline ResourceLocator::ResourceLocator(
        intrusive_ptr<RenderCore::Metal::Underlying::Resource>&& moveFrom, 
        unsigned offset, unsigned size,
        std::shared_ptr<IResourcePool> pool, uint64 poolMarker)
    : _resource(std::forward<intrusive_ptr<RenderCore::Metal::Underlying::Resource>>(moveFrom))
    , _offset(offset), _size(size), _pool(pool), _poolMarker(poolMarker)
    {
        if (_pool) {
            _pool->AddRef(_poolMarker, _resource.get(), _offset, _size);
        }
    }

    inline ResourceLocator::ResourceLocator(ResourceLocator&& moveFrom) 
        : _resource(std::move(moveFrom._resource))
        , _offset(moveFrom._offset), _size(moveFrom._size)
        , _pool(std::move(moveFrom._pool)), _poolMarker(moveFrom._poolMarker)
    {}

    inline ResourceLocator& ResourceLocator::operator=(ResourceLocator&& moveFrom)
    {
        ResourceLocator(moveFrom).swap(*this);
        return *this;
    }

    inline ResourceLocator::ResourceLocator() : _pool(nullptr), _poolMarker(0), _offset(0), _size(0) {}
    inline ResourceLocator::~ResourceLocator()
    {
            // attempt to return our resource to the pool
        if (_pool) {
            _pool->ReturnToPool(_poolMarker, std::move(_resource), _offset, _size);
        }
    }

    inline void ResourceLocator::swap(ResourceLocator& other)
    {
        std::swap(_resource, other._resource);
        std::swap(_size, other._size);
        std::swap(_offset, other._offset);
        std::swap(_pool, other._pool);
        std::swap(_poolMarker, other._poolMarker);
    }
}

