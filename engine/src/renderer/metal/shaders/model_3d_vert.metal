#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct PushConstants
{
    float4x4 worldToClip;
    float4 normalToWorld0;
    float4 normalToWorld1;
    float4 normalToWorld2;
    float4 materialColor;
};

struct model_3d_vert_out
{
    float3 baseColor [[user(locn0)]];
    float3 worldNormal [[user(locn1)]];
    float3 lightDirection [[user(locn2)]];
    float2 texCoord [[user(locn3)]];
    float4 worldTangent [[user(locn4)]];
    float4 materialFactors [[user(locn5)]];
    float4 materialFactors2 [[user(locn6)]];
    float4 materialFactors3 [[user(locn7)]];
    float4 gl_Position [[position]];
};

struct model_3d_vert_in
{
    float3 inPosition [[attribute(0)]];
    float3 inColor [[attribute(1)]];
    float3 inNormal [[attribute(2)]];
    float2 inTexCoord [[attribute(3)]];
    float4 inTangent [[attribute(4)]];
    float4 inMaterial [[attribute(5)]];
    float4 inMaterial2 [[attribute(6)]];
    float4 inMaterial3 [[attribute(7)]];
};

vertex model_3d_vert_out model_3d_vert(model_3d_vert_in in [[stage_in]], constant PushConstants& pc [[buffer(0)]])
{
    model_3d_vert_out out = {};
    float3x3 normalToWorld = float3x3(float3(pc.normalToWorld0.xyz), float3(pc.normalToWorld1.xyz), float3(pc.normalToWorld2.xyz));
    float3 light = float3(pc.normalToWorld0.w, pc.normalToWorld1.w, pc.normalToWorld2.w);
    out.gl_Position = pc.worldToClip * float4(in.inPosition, 1.0);
    out.baseColor = fast::clamp(in.inColor * pc.materialColor.xyz, float3(0.0), float3(1.0));
    out.worldNormal = fast::normalize(normalToWorld * in.inNormal);
    float3 _100;
    if (length(light) > 9.9999997473787516355514526367188e-05)
    {
        _100 = fast::normalize(light);
    }
    else
    {
        _100 = float3(-0.348712146282196044921875, 0.647608280181884765625, 0.67749786376953125);
    }
    out.lightDirection = _100;
    out.texCoord = in.inTexCoord;
    out.worldTangent = float4(fast::normalize(normalToWorld * in.inTangent.xyz), in.inTangent.w);
    out.materialFactors = in.inMaterial;
    out.materialFactors2 = in.inMaterial2;
    out.materialFactors3 = in.inMaterial3;
    return out;
}

