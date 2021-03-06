// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Metal/DeviceContext.h"
#include "../Core/Types.h"
#include "../Math/Vector.h"
#include <memory>

namespace RenderCore { namespace Techniques { class CameraDesc; class TechniqueContext; }}

namespace SceneEngine
{
    class ISceneParser;
    class TerrainManager;
    class PlacementsEditor;

    /// <summary>Context for doing ray & box intersection test<summary>
    /// This context is intended for performing ray intersections for tools.
    /// Frequently we need to do "hit tests" and various projection and 
    /// unprojection operations. This context contains the minimal references
    /// to do this.
    /// <seealso cref="IntersectionResolver" />
    class IntersectionTestContext
    {
    public:
        std::pair<Float3, Float3> CalculateWorldSpaceRay(Int2 screenCoord) const;
        Float2 ProjectToScreenSpace(const Float3& worldSpaceCoord) const;
        RenderCore::Techniques::CameraDesc GetCameraDesc() const;
        const RenderCore::Techniques::TechniqueContext& GetTechniqueContext() const { return *_techniqueContext.get(); }
        ISceneParser* GetSceneParser() const { return _sceneParser.get(); }
        Int2 GetViewportSize() const;
        intrusive_ptr<RenderCore::Metal::DeviceContext> GetImmediateContext() const;

        IntersectionTestContext(
            std::shared_ptr<ISceneParser> sceneParser,
            std::shared_ptr<RenderCore::Techniques::TechniqueContext> techniqueContext);
        ~IntersectionTestContext();
    protected:
        std::shared_ptr<ISceneParser>      _sceneParser;
        std::shared_ptr<RenderCore::Techniques::TechniqueContext>  _techniqueContext;
    };

    /// <summary>Resolves ray and box intersections for tools</summary>
    /// This object can calculate intersections of basic primitives against
    /// the scene. This is intended for tools to perform interactive operations
    /// (like selecting objects in the scene).
    /// Note that much of the intersection math is performed on the GPU. This means
    /// that any intersection operation will probably involve a GPU synchronisation.
    /// This isn't intended to be used at runtime in a game, because it may cause
    /// frame-rate hitches. But for tools, it should not be an issue.
    class IntersectionTestScene
    {
    public:
        struct Type 
        {
            enum Enum 
            {
                Terrain = 1<<0, 
                Placement = 1<<1
            };
            typedef unsigned BitField;
        };
        
        class Result
        {
        public:
            Type::Enum                  _type;
            Float3                      _worldSpaceCollision;
            std::pair<uint64, uint64>   _objectGuid;
            float                       _distance;

            Result() 
            : _type(Type::Enum(0))
            , _worldSpaceCollision(0.f, 0.f, 0.f)
            , _objectGuid(0ull, 0ull)
            , _distance(FLT_MAX) {}
        };

        Result FirstRayIntersection(
            RenderCore::Metal::DeviceContext* devContext,
            const IntersectionTestContext& context,
            std::pair<Float3, Float3> worldSpaceRay,
            Type::BitField filter = ~Type::BitField(0)) const;

        Result UnderCursor(
            const IntersectionTestContext& context,
            Int2 cursorPosition,
            Type::BitField filter = ~Type::BitField(0)) const;

        IntersectionTestScene(
            std::shared_ptr<TerrainManager> terrainManager = std::shared_ptr<TerrainManager>(),
            std::shared_ptr<PlacementsEditor> placements = std::shared_ptr<PlacementsEditor>());
        ~IntersectionTestScene();
    protected:
        std::shared_ptr<TerrainManager> _terrainManager;
        std::shared_ptr<PlacementsEditor> _placements;
    };
}

