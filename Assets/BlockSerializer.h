// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Types.h"
#include "../Core/Exceptions.h"
#include "../Math/Vector.h"
#include "../Math/Matrix.h"
#include "../Utility/PtrUtils.h"
#include <vector>
#include <iterator>

namespace Serialization
{
    template<typename Type> class BlockSerializerAllocator;
    template<typename Type> class BlockSerializerDeleter;

        ////////////////////////////////////////////////////

    class NascentBlockSerializer
    {
    public:
        struct SpecialBuffer
        {
            enum Enum { Unknown, VertexBuffer, IndexBuffer, String, Vector, UniquePtr };
        };
        
        template<typename Type> void    SerializeSubBlock(const Type* type);
        template<typename Type> void    SerializeSubBlock(const Type* begin, const Type* end, SpecialBuffer::Enum specialBuffer = SpecialBuffer::Unknown);
        void                            SerializeSubBlock(NascentBlockSerializer& subBlock, SpecialBuffer::Enum specialBuffer = SpecialBuffer::Unknown);

        virtual void    SerializeSpecialBuffer( SpecialBuffer::Enum specialBuffer, 
                                                const void* begin, const void* end);
        
        virtual void    SerializeValue  ( uint8     value );
        virtual void    SerializeValue  ( uint16    value );
        virtual void    SerializeValue  ( uint32    value );
        virtual void    SerializeValue  ( uint64    value );
        virtual void    SerializeValue  ( float     value );
        virtual void    SerializeValue  ( const std::string& value );

        template<typename Type, typename Allocator>
            void    SerializeValue  ( const std::vector<Type, Allocator>& value );

        template<typename Type, typename Deletor>
            void    SerializeValue  ( const DynamicArray<Type, Deletor>& value );

        template<typename Type, typename Deletor>
            void    SerializeValue  ( const std::unique_ptr<Type, Deletor>& value, size_t count );

        template<typename Type>
            void    SerializeRaw    ( Type      type );

        std::unique_ptr<uint8[]>      AsMemoryBlock();

        NascentBlockSerializer();
        ~NascentBlockSerializer();

        class InternalPointer
        {
        public:
            size_t                  _pointerOffset;
            size_t                  _subBlockOffset;
            size_t                  _subBlockSize;
            SpecialBuffer::Enum     _specialBuffer;
        };

        static const size_t PtrFlagBit  = size_t(1)<<(size_t(sizeof(size_t)*8-1));
        static const size_t PtrMask     = ~PtrFlagBit;

    protected:
        std::vector<uint8>              _memory;
        std::vector<uint8>              _trailingSubBlocks;
        std::vector<InternalPointer>    _internalPointers;

        virtual void PushBackPointer(size_t value);
        virtual void PushBackRaw(const void* data, size_t size);
        virtual void PushBackRaw_SubBlock(const void* data, size_t size);
        virtual void PushBackInternalPointer(const InternalPointer& ptr);
        virtual void PushBackPlaceholder(SpecialBuffer::Enum specialBuffer);
    };

    void            Block_Initialize(void* block, const void* base=nullptr);
    const void*     Block_GetFirstObject(const void* blockStart);
    size_t          Block_GetSize(const void* block);
    std::unique_ptr<uint8[]>     Block_Duplicate(const void* block);

        ////////////////////////////////////////////////////

    template <typename Type>
        void Serialize(NascentBlockSerializer& serializer, const Type& object)
    {
        object.Serialize(serializer);
    }

    inline void Serialize(NascentBlockSerializer& serializer, uint8  value)                { serializer.SerializeValue(value); }
    inline void Serialize(NascentBlockSerializer& serializer, uint16 value)                { serializer.SerializeValue(value); }
    inline void Serialize(NascentBlockSerializer& serializer, uint32 value)                { serializer.SerializeValue(value); }
    inline void Serialize(NascentBlockSerializer& serializer, uint64 value)                { serializer.SerializeValue(value); }
    inline void Serialize(NascentBlockSerializer& serializer, float  value)                { serializer.SerializeValue(value); }
    inline void Serialize(NascentBlockSerializer& serializer, const std::string& value)    { serializer.SerializeValue(value); }

        ////////////////////////////////////////////////////

    template<typename Type>
        void    NascentBlockSerializer::SerializeSubBlock(const Type* begin, const Type* end, SpecialBuffer::Enum specialBuffer)
    {
        NascentBlockSerializer temporaryBlock;
        for (auto i=begin; i!=end; ++i) {
            Serialize(temporaryBlock, *i);
        }

        SerializeSubBlock(temporaryBlock, specialBuffer);
    }
        
    template<typename Type>
        void    NascentBlockSerializer::SerializeSubBlock(const Type* type)
    {
        NascentBlockSerializer temporaryBlock;
        Serialize(temporaryBlock, type);
        SerializeSubBlock(temporaryBlock, SpecialBuffer::Unknown);
    }

    template<typename Type>
        void    NascentBlockSerializer::SerializeRaw(Type      type)
    {
        PushBackRaw(&type, sizeof(Type));
    }

    template<typename Type, typename Allocator>
        void    NascentBlockSerializer::SerializeValue  ( const std::vector<Type, Allocator>& value )
    {
        SerializeSubBlock(AsPointer(value.cbegin()), AsPointer(value.cend()), SpecialBuffer::Vector);
    }

    template<typename Type, typename Deletor>
        void    NascentBlockSerializer::SerializeValue  ( const DynamicArray<Type, Deletor>& value )
    {
        SerializeSubBlock(value.begin(), value.end(), SpecialBuffer::UniquePtr);
        SerializeValue(value.size());
    }
        
    template<typename Type, typename Deletor>
        void    NascentBlockSerializer::SerializeValue  ( const std::unique_ptr<Type, Deletor>& value, size_t count )
    {
        SerializeSubBlock(value.get(), &value[count], SpecialBuffer::UniquePtr);
    }

        ////////////////////////////////////////////////////

    #pragma warning(push)
    #pragma warning(disable:4702)

    template<typename Type>
        class BlockSerializerAllocator : public std::allocator<Type>
    {
    public:
        pointer allocate(size_type n, std::allocator<void>::const_pointer ptr= 0)
        {
            if (_fromFixedStorage) {
                ThrowException(std::invalid_argument("Cannot allocate from a BlockSerializerAllocator than has been serialized in from a fixed block"));
                return nullptr;
            }
            return std::allocator<Type>::allocate(n, ptr);
        }

        void deallocate(pointer p, size_type n)
        {
            if (!_fromFixedStorage) {
                std::allocator<Type>::deallocate(p, n);
            }
        }

        template<class _Other>
		    struct rebind
		    {
		        typedef BlockSerializerAllocator<_Other> other;
		    };

        BlockSerializerAllocator()                                                  : _fromFixedStorage(0) {}
        explicit BlockSerializerAllocator(unsigned fromFixedStorage)                : _fromFixedStorage(fromFixedStorage) {}
        BlockSerializerAllocator(const std::allocator<Type>& copyFrom)              : _fromFixedStorage(0) {}
        BlockSerializerAllocator(std::allocator<Type>&& moveFrom)                   : _fromFixedStorage(0) {}
        BlockSerializerAllocator(const BlockSerializerAllocator<Type>& copyFrom)    : _fromFixedStorage(0) {}
        BlockSerializerAllocator(BlockSerializerAllocator<Type>&& moveFrom)         : _fromFixedStorage(moveFrom._fromFixedStorage) {}
    private:
        unsigned    _fromFixedStorage;
    };

    #pragma warning(pop)

        ////////////////////////////////////////////////////

    template<typename Type>
        class BlockSerializerDeleter : public std::default_delete<Type>
    {
    public:
        void operator()(Type *_Ptr) const
        {
            if (!_fromFixedStorage) {
                std::default_delete<Type>::operator()(_Ptr);
            }
        }

        BlockSerializerDeleter()                                                : _fromFixedStorage(0) {}
        explicit BlockSerializerDeleter(unsigned fromFixedStorage)              : _fromFixedStorage(fromFixedStorage) {}
        BlockSerializerDeleter(const std::default_delete<Type>& copyFrom)       : _fromFixedStorage(0) {}
        BlockSerializerDeleter(std::default_delete<Type>&& moveFrom)            : _fromFixedStorage(0) {}
        BlockSerializerDeleter(const BlockSerializerDeleter<Type>& copyFrom)    : _fromFixedStorage(copyFrom._fromFixedStorage) {}
        BlockSerializerDeleter(BlockSerializerDeleter<Type>&& moveFrom)         : _fromFixedStorage(moveFrom._fromFixedStorage) {}
    private:
        unsigned    _fromFixedStorage;
    };

        ////////////////////////////////////////////////////

    template<typename Type>
        class BlockSerializerDeleter<Type[]> : public std::default_delete<Type[]>
    {
    public:
        void operator()(Type *_Ptr) const
        {
            if (!_fromFixedStorage) {
                std::default_delete<Type[]>::operator()(_Ptr);
            }
        }

        BlockSerializerDeleter()                                                : _fromFixedStorage(0) {}
        explicit BlockSerializerDeleter(unsigned fromFixedStorage)              : _fromFixedStorage(fromFixedStorage) {}
        BlockSerializerDeleter(const std::default_delete<Type[]>& copyFrom)     : _fromFixedStorage(0) {}
        BlockSerializerDeleter(std::default_delete<Type[]>&& moveFrom)          : _fromFixedStorage(0) {}
        BlockSerializerDeleter(const BlockSerializerDeleter<Type[]>& copyFrom)  : _fromFixedStorage(copyFrom._fromFixedStorage) {}
        BlockSerializerDeleter(BlockSerializerDeleter<Type[]>&& moveFrom)       : _fromFixedStorage(moveFrom._fromFixedStorage) {}
    private:
        unsigned    _fromFixedStorage;
    };

        ////////////////////////////////////////////////////

    template<typename Type>
        class Vector
    {
    public:
        typedef std::vector<Type, BlockSerializerAllocator<Type>> Type;
    };

        ////////////////////////////////////////////////////
        

    template<typename Type, typename Allocator>
        void Serialize(NascentBlockSerializer& serializer, const std::vector<Type, Allocator>& value)
            { serializer.SerializeValue(value); }

    template<typename Type, typename Deletor>
        void Serialize(NascentBlockSerializer& serializer, const DynamicArray<Type, Deletor>& value)
            { serializer.SerializeValue(value); }
        
    template<typename Type, typename Deletor>
        void Serialize(NascentBlockSerializer& serializer, const std::unique_ptr<Type, Deletor>& value, size_t count)
            { serializer.SerializeValue(value, count); }

    template<int Dimen, typename Primitive>
        inline void Serialize(  NascentBlockSerializer& serializer,
                                const cml::vector< Primitive, cml::fixed<Dimen> >& vec)
    {
        for (unsigned j=0; j<Dimen; ++j) {
            serializer.SerializeValue(vec[j]);
        }
    }
    
    inline void Serialize(   NascentBlockSerializer&     serializer, 
                             const Math::Float4x4&       float4x4)
    {
        for (unsigned i=0; i<4; ++i)
            for (unsigned j=0; j<4; ++j) {
                serializer.SerializeValue(float4x4(i,j));
            }
    }

}

