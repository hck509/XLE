// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../MainGeometry.h"
#include "../CommonResources.h"

struct GSOutput
{
	float	intersectionDepth : INTERSECTIONDEPTH;
	float4	triangleA : POINT0;
	float4	triangleB : POINT1;
	float4	triangleC : POINT2;
};

cbuffer RayDefinition
{
	float3	RayStart; 
	float	RayLength;
	float3	RayDirection;
}



float3 RayTriangleIntersection(float3 p, float3 d, float3 v0, float3 v1, float3 v2)
{
		//		basic alegrabic intersection method from--
		//	http://www.lighthouse3d.com/tutorials/maths/ray-triangle-intersection/
	float3 e1 = v1 - v0;
	float3 e2 = v2 - v0;

	float3 h = cross(d,e2);
	float a = dot(e1,h);

	if (a > -0.00001f && a < 0.00001f)
		return 0.0.xxx;

	float f = 1.f/a;
	float3 s = p-v0;
	float u = f * (dot(s,h));

	if (u < 0.f || u > 1.f)
		return 0.0.xxx;

	float3 q = cross(s,e1);
	float v = f * dot(d,q);

	if (v < 0.f || u + v > 1.f)
		return 0.0.xxx;

	// at this stage we can compute t to find out where
	// the intersection point is on the line
	float t = f * dot(e2,q);

	if (t > 0.00001f) // ray intersection
		return float3( t, u, v );
	else
		return 0.0.xxx;
}

#if OUTPUT_TEXCOORD==1
	float2 GetTexCoord(VSOutput input) { return input.texCoord; }
#else
	float2 GetTexCoord(VSOutput input) { return 1.0.xx; }
#endif

[maxvertexcount(1)]
	void triangles(triangle VSOutput input[3], inout PointStream<GSOutput> outputStream)
{
	// Test the triangle to see if there is an intersection with the given ray.
	// If we get an intersection, write the result to "outputStream"
	//
	// We're assuming that the ray is in world space. We'll use the worldSpace member
	// of the input vertices to compare against the ray.
	//
	// We're going to ignore the winding order. So callers will get both front-face
	// and back-face intersections.

	float3 intersectionResult = 
		RayTriangleIntersection( 
			RayStart, RayDirection, 
			input[0].worldPosition, input[1].worldPosition, input[2].worldPosition);
	if (intersectionResult.x > 0.f && intersectionResult.x < RayLength) {
		GSOutput result;
		result.intersectionDepth = intersectionResult.x;
		float3 barycentric = float3(1.f - intersectionResult.y - intersectionResult.z, intersectionResult.yz);

		bool isOpaquePart = true;

		// If this is alpha test geometry, we need to check the texture 
		// to see if this  is a transparent pixel. This method is sometimes
		// used for "picking" tests in tools. Without this alpha test check,
		// the alpha tested triangles will behave like opaque triangles, which
		// will give a confusing result for the user.
		#if (OUTPUT_TEXCOORD==1) && (MAT_ALPHA_TEST==1)
			const float alphaThreshold = 0.5f;
			float2 texCoord = 
				  barycentric.x * GetTexCoord(input[0])
				+ barycentric.y * GetTexCoord(input[1])
				+ barycentric.z * GetTexCoord(input[2])
				;
			isOpaquePart = DiffuseTexture.SampleLevel(DefaultSampler, texCoord, 0).a > alphaThreshold;
		#endif

		if (isOpaquePart) {
			result.triangleA = float4(input[0].worldPosition, barycentric.x);
			result.triangleB = float4(input[1].worldPosition, barycentric.y);
			result.triangleC = float4(input[2].worldPosition, barycentric.z);
			outputStream.Append(result);
		}
	}
}

