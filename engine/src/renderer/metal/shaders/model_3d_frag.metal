#pragma clang diagnostic ignored "-Wmissing-prototypes"

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

struct model_3d_frag_out
{
    float4 outColor [[color(0)]];
};

struct model_3d_frag_in
{
    float3 baseColor [[user(locn0)]];
    float3 worldNormal [[user(locn1)]];
    float3 lightDirection [[user(locn2)]];
    float2 texCoord [[user(locn3)]];
    float4 worldTangent [[user(locn4)]];
    float4 materialFactors [[user(locn5)]];
    float4 materialFactors2 [[user(locn6)]];
    float4 materialFactors3 [[user(locn7)]];
};

static inline __attribute__((always_inline))
float3 linear_to_srgb(thread float3& color)
{
    color = fast::max(color, float3(0.0));
    float3 lower = color * 12.9200000762939453125;
    float3 higher = (powr(color, float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875);
    return select(higher, lower, color <= float3(0.003130800090730190277099609375));
}

fragment model_3d_frag_out model_3d_frag(model_3d_frag_in in [[stage_in]], constant PushConstants& pc [[buffer(0)]], texture2d<float> baseColorTexture [[texture(0)]], texture2d<float> normalTexture [[texture(1)]], texture2d<float> metallicRoughnessTexture [[texture(2)]], texture2d<float> occlusionTexture [[texture(3)]], texture2d<float> emissiveTexture [[texture(4)]], sampler baseColorTextureSmplr [[sampler(0)]], sampler normalTextureSmplr [[sampler(1)]], sampler metallicRoughnessTextureSmplr [[sampler(2)]], sampler occlusionTextureSmplr [[sampler(3)]], sampler emissiveTextureSmplr [[sampler(4)]])
{
    model_3d_frag_out out = {};
    float4 textureColor = baseColorTexture.sample(baseColorTextureSmplr, in.texCoord);
    float componentAlpha = fast::clamp(pc.materialColor.w, 0.0, 1.0);
    float materialAlpha = textureColor.w * fast::clamp(in.materialFactors.w, 0.0, 1.0);
    float alphaMode = in.materialFactors3.x;
    float alphaCutoff = fast::clamp(in.materialFactors3.y, 0.0, 1.0);
    if (((alphaMode > 0.5) && (alphaMode < 1.5)) && (materialAlpha < alphaCutoff))
    {
        discard_fragment();
    }
    float _106;
    if (alphaMode > 1.5)
    {
        _106 = materialAlpha * componentAlpha;
    }
    else
    {
        _106 = componentAlpha;
    }
    float outputAlpha = _106;
    float3 texturedBaseColor = fast::clamp(in.baseColor * textureColor.xyz, float3(0.0), float3(1.0));
    float3 normal = fast::normalize(in.worldNormal);
    float3 tangent = in.worldTangent.xyz - (normal * dot(normal, in.worldTangent.xyz));
    if (dot(tangent, tangent) > 9.9999997473787516355514526367188e-05)
    {
        tangent = fast::normalize(tangent);
        float3 bitangent = fast::normalize(cross(normal, tangent) * in.worldTangent.w);
        float3 tangentNormal = (normalTexture.sample(normalTextureSmplr, in.texCoord).xyz * 2.0) - float3(1.0);
        float3 _170 = tangentNormal;
        float2 _172 = _170.xy * fast::max(in.materialFactors.z, 0.0);
        tangentNormal.x = _172.x;
        tangentNormal.y = _172.y;
        tangentNormal = fast::normalize(tangentNormal);
        normal = fast::normalize(float3x3(float3(tangent), float3(bitangent), float3(normal)) * tangentNormal);
    }
    float4 metallicRoughness = metallicRoughnessTexture.sample(metallicRoughnessTextureSmplr, in.texCoord);
    float metallic = fast::clamp(in.materialFactors.x * metallicRoughness.z, 0.0, 1.0);
    float roughness = fast::clamp(in.materialFactors.y * metallicRoughness.y, 0.039999999105930328369140625, 1.0);
    float occlusion = mix(1.0, occlusionTexture.sample(occlusionTextureSmplr, in.texCoord).x, fast::clamp(in.materialFactors2.x, 0.0, 1.0));
    float3 emissive = emissiveTexture.sample(emissiveTextureSmplr, in.texCoord).xyz * fast::max(in.materialFactors2.yzw, float3(0.0));
    float3 sunDirection = fast::normalize(in.lightDirection);
    float3 skyDirection = float3(0.0, 1.0, 0.0);
    float3 view = float3(0.0, 0.0, 1.0);
    float3 halfVector = fast::normalize(sunDirection + view);
    float sunDiffuse = fast::max(dot(normal, sunDirection), 0.0);
    float wrappedSun = fast::clamp((sunDiffuse * 0.920000016689300537109375) + 0.07999999821186065673828125, 0.0, 1.0);
    float skyFacing = fast::clamp((dot(normal, skyDirection) * 0.5) + 0.5, 0.0, 1.0);
    float nDotV = fast::max(dot(normal, view), 0.0);
    float vDotH = fast::max(dot(view, halfVector), 0.0);
    float rimFacing = powr(1.0 - nDotV, 2.25);
    float specularPower = mix(128.0, 12.0, roughness);
    float sunSpecular = powr(fast::max(dot(normal, halfVector), 0.0), specularPower);
    float3 sunColor = float3(1.0, 0.920000016689300537109375, 0.7799999713897705078125);
    float3 skyColor = float3(0.519999980926513671875, 0.63999998569488525390625, 0.819999992847442626953125);
    float3 groundColor = float3(0.1599999964237213134765625, 0.1500000059604644775390625, 0.14000000059604644775390625);
    float3 dielectricF0 = float3(0.039999999105930328369140625);
    float3 f0 = mix(dielectricF0, texturedBaseColor, float3(metallic));
    float3 fresnel = f0 + ((float3(1.0) - f0) * powr(1.0 - vDotH, 5.0));
    float3 diffuseColor = texturedBaseColor * mix(1.0, 0.3400000035762786865234375, metallic);
    float3 ambientColor = mix(groundColor, skyColor, float3(skyFacing));
    float3 displayLift = mix(float3(0.054999999701976776123046875, 0.064999997615814208984375, 0.07500000298023223876953125), texturedBaseColor, float3(0.86000001430511474609375));
    float3 ambientTerm = ((displayLift * ambientColor) * 0.540000021457672119140625) * occlusion;
    float3 diffuseTerm = ((diffuseColor * sunColor) * wrappedSun) * 0.89999997615814208984375;
    float3 bounceTerm = ((texturedBaseColor * groundColor) * (1.0 - skyFacing)) * 0.1599999964237213134765625;
    float3 specularTerm = ((((fresnel + float3(0.0350000001490116119384765625)) * sunColor) * sunSpecular) * mix(0.63999998569488525390625, 0.20000000298023223876953125, roughness)) * (0.3499999940395355224609375 + (0.64999997615814208984375 * nDotV));
    float3 rimTerm = (((skyColor * 0.180000007152557373046875) + (texturedBaseColor * 0.100000001490116119384765625)) * rimFacing) * mix(1.0, 0.550000011920928955078125, metallic);
    float3 litColor = fast::clamp(((((ambientTerm + diffuseTerm) + bounceTerm) + specularTerm) + rimTerm) + emissive, float3(0.0), float3(1.0));
    float3 param = litColor;
    float3 _425 = linear_to_srgb(param);
    out.outColor = float4(_425, outputAlpha);
    return out;
}

