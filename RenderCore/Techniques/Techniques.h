// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Metal/Forward.h"
#include "../Metal/InputLayout.h"        // required for _materialConstantsLayout in ResolvedShader
#include "../../Utility/ParameterBox.h"
#include "../../Core/Prefix.h"
#include "../../Core/Types.h"
#include <string>
#include <vector>

namespace Utility { class Data; }
using namespace Utility;
namespace Assets { class DependencyValidation; class DirectorySearchRules; }

namespace RenderCore { namespace Techniques
{
    class ShaderParameters
    {
    public:
        struct Source { enum Enum { Geometry, GlobalEnvironment, Runtime, Material, Max }; };
        ParameterBox    _parameters[Source::Max];

        uint64      CalculateFilteredHash(uint64 inputHash, const ParameterBox* globalState[Source::Max]);
        void        BuildStringTable(std::vector<std::pair<std::string, std::string>>& defines) const;

    private:
        uint64      CalculateFilteredState(const ParameterBox* globalState[Source::Max]);
        std::vector<std::pair<uint64, uint64>>  _globalToFilteredTable;

        friend class Technique;
    };

        //////////////////////////////////////////////////////////////////
            //      T E C H N I Q U E                               //
        //////////////////////////////////////////////////////////////////

            //      "technique" is a way to select a correct shader
            //      in a data-driven way. The code provides a technique
            //      index and a set of parameters in ParameterBoxes
            //          -- that is transformed into a concrete shader

    class ResolvedShader
    {
    public:
        uint64                          _variationHash;
        Metal::ShaderProgram*           _shaderProgram;
        Metal::BoundUniforms*           _boundUniforms;
        Metal::BoundInputLayout*        _boundLayout;
        Metal::ConstantBufferLayout*    _materialConstantsLayout;

        ResolvedShader();
    };

        //
        //  <summary>Vertex, constants and resources interface for a technique<summary>
        //
        //  Defines an input interface for a technique. Normally a client may only define
        //  a few "TechniqueInterface" objects, and reuse them with many techniques.
        //
        //  The interface consists of:
        //      * vertex input layout
        //          - input vertex format (position of elements in the vertex buffer)
        //      * constant buffers
        //          - constant buffer binding locations
        //      * shader resources
        //          - shader resources binding locations
        //
        //  The constants and shader resources bindings define how the "BoundUniforms" object in the ResolvedShader
        //  is created. For example, if you bind a constant buffer to slot 0, then you can set it by passing the constant
        //  buffer in slot 0 to ResolvedShader::_boundUniforms->Apply(...).
        //
        //  When calling BindConstantBuffer or BindShaderResource, the first parameter
        //  is a hash value of the name in the shader. For example, if the shader has:
        //      Texture2D<float> Depths;
        //
        //  Then you can pass:
        //      BindShaderResource(Hash64("Depth"), interfaceSlotIndex);
        //
    class TechniqueInterface
    {
    public:
        void    BindConstantBuffer(uint64 hashName, unsigned slot, unsigned streamIndex);
        void    BindShaderResource(uint64 hashName, unsigned slot, unsigned streamIndex);

        uint64  GetHashValue() const;

        TechniqueInterface();
        TechniqueInterface(const Metal::InputLayout& vertexInputLayout);
        TechniqueInterface(TechniqueInterface&& moveFrom);
        TechniqueInterface&operator=(TechniqueInterface&& moveFrom);
        ~TechniqueInterface();

    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        friend class Technique; // makes internal structure easier
    };

    // #if defined(_DEBUG)
    //     #define CHECK_TECHNIQUE_HASH_CONFLICTS
    // #endif

    class Technique
    {
    public:
        ResolvedShader      FindVariation(  const ParameterBox* globalState[ShaderParameters::Source::Max], 
                                            const TechniqueInterface& techniqueInterface);
        bool                IsValid() const { return !_vertexShaderName.empty(); }

        Technique(Data& source, ::Assets::DirectorySearchRules* searchRules = nullptr, std::vector<const ::Assets::DependencyValidation*>* inherited = nullptr);
        Technique(Technique&& moveFrom);
        Technique& operator=(Technique&& moveFrom);
    protected:
        std::string         _name;
        ShaderParameters    _baseParameters;
        std::vector<std::pair<uint64, ResolvedShader>>  _filteredToResolved;
        std::vector<std::pair<uint64, ResolvedShader>>  _globalToResolved;
        std::string         _vertexShaderName;
        std::string         _pixelShaderName;
        std::string         _geometryShaderName;

        #if defined(CHECK_TECHNIQUE_HASH_CONFLICTS)
            class HashConflictTest
            {
            public:
                ParameterBox _globalState[ShaderParameters::Source::Max];
                uint64 _rawHash; 
                uint64 _filteredHash; 
                uint64 _interfaceHash;

                HashConflictTest(const ParameterBox* globalState[ShaderParameters::Source::Max], uint64 rawHash, uint64 filteredHash, uint64 interfaceHash);
                HashConflictTest();
            };
            std::vector<std::pair<uint64, HashConflictTest>>  _localToResolvedTest;
            std::vector<std::pair<uint64, HashConflictTest>>  _globalToResolvedTest;

            void TestHashConflict(
                const ParameterBox* globalState[ShaderParameters::Source::Max], 
                const HashConflictTest& comparison);
        #endif

        void        ResolveAndBind( 
            ResolvedShader& shader, 
            const ParameterBox* globalState[ShaderParameters::Source::Max],
            const TechniqueInterface& techniqueInterface);

        std::vector<std::unique_ptr<Metal::ShaderProgram>> _resolvedShaderPrograms;
        std::vector<std::unique_ptr<Metal::BoundUniforms>> _resolvedBoundUniforms;
        std::vector<std::unique_ptr<Metal::BoundInputLayout>> _resolvedBoundInputLayouts;
        std::vector<std::unique_ptr<Metal::ConstantBufferLayout>> _resolvedMaterialConstantsLayouts;
    };

    class ShaderType
    {
    public:
        ResolvedShader      FindVariation(int techniqueIndex, const ParameterBox* globalState[ShaderParameters::Source::Max], const TechniqueInterface& techniqueInterface);
        const ::Assets::DependencyValidation&         GetDependencyValidation() const     { return *_validationCallback; }

        ShaderType(const char resourceName[]);
        ~ShaderType();
    private:
        std::vector<Technique>      _technique;
        std::shared_ptr<::Assets::DependencyValidation>   _validationCallback;
    };

        //////////////////////////////////////////////////////////////////
            //      C O N T E X T                                   //
        //////////////////////////////////////////////////////////////////
    
    class TechniqueContext
    {
    public:
        ParameterBox   _globalEnvironmentState;
        ParameterBox   _runtimeState;

        static void     BindGlobalUniforms(TechniqueInterface&);
        static void     BindGlobalUniforms(Metal::BoundUniforms&);
    };

}}

