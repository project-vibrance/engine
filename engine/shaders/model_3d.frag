#version 460

layout(push_constant) uniform PushConstants {
    mat4 worldToClip;
    vec4 normalToWorld0;
    vec4 normalToWorld1;
    vec4 normalToWorld2;
    vec4 materialColor;
} pc;

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 1) uniform sampler2D normalTexture;
layout(set = 0, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 3) uniform sampler2D occlusionTexture;
layout(set = 0, binding = 4) uniform sampler2D emissiveTexture;

layout(location = 0) in vec3 baseColor;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec3 lightDirection;
layout(location = 3) in vec2 texCoord;
layout(location = 4) in vec4 worldTangent;
layout(location = 5) in vec4 materialFactors;
layout(location = 6) in vec4 materialFactors2;
layout(location = 7) in vec4 materialFactors3;
layout(location = 0) out vec4 outColor;

vec3 linear_to_srgb(vec3 color)
{
    color = max(color, vec3(0.0));
    vec3 lower = color * 12.92;
    vec3 higher = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(higher, lower, lessThanEqual(color, vec3(0.0031308)));
}

void main()
{
    vec4 textureColor = texture(baseColorTexture, texCoord);
    float componentAlpha = clamp(pc.materialColor.a, 0.0, 1.0);
    float materialAlpha = textureColor.a * clamp(materialFactors.w, 0.0, 1.0);
    float alphaMode = materialFactors3.x;
    float alphaCutoff = clamp(materialFactors3.y, 0.0, 1.0);
    if (alphaMode > 0.5 && alphaMode < 1.5 && materialAlpha < alphaCutoff) {
        discard;
    }
    float outputAlpha = alphaMode > 1.5 ? materialAlpha * componentAlpha : componentAlpha;

    vec3 texturedBaseColor = clamp(baseColor * textureColor.rgb, vec3(0.0), vec3(1.0));

    vec3 normal = normalize(worldNormal);
    vec3 tangent = worldTangent.xyz - normal * dot(normal, worldTangent.xyz);
    if (dot(tangent, tangent) > 0.0001) {
        tangent = normalize(tangent);
        vec3 bitangent = normalize(cross(normal, tangent) * worldTangent.w);
        vec3 tangentNormal = texture(normalTexture, texCoord).xyz * 2.0 - 1.0;
        tangentNormal.xy *= max(materialFactors.z, 0.0);
        tangentNormal = normalize(tangentNormal);
        normal = normalize(mat3(tangent, bitangent, normal) * tangentNormal);
    }

    vec4 metallicRoughness = texture(metallicRoughnessTexture, texCoord);
    float metallic = clamp(materialFactors.x * metallicRoughness.b, 0.0, 1.0);
    float roughness = clamp(materialFactors.y * metallicRoughness.g, 0.04, 1.0);
    float occlusion = mix(1.0, texture(occlusionTexture, texCoord).r, clamp(materialFactors2.x, 0.0, 1.0));
    vec3 emissive = texture(emissiveTexture, texCoord).rgb * max(materialFactors2.yzw, vec3(0.0));

    vec3 sunDirection = normalize(lightDirection);
    vec3 skyDirection = vec3(0.0, 1.0, 0.0);
    vec3 view = vec3(0.0, 0.0, 1.0);
    vec3 halfVector = normalize(sunDirection + view);

    float sunDiffuse = max(dot(normal, sunDirection), 0.0);
    float wrappedSun = clamp(sunDiffuse * 0.92 + 0.08, 0.0, 1.0);
    float skyFacing = clamp(dot(normal, skyDirection) * 0.5 + 0.5, 0.0, 1.0);
    float nDotV = max(dot(normal, view), 0.0);
    float vDotH = max(dot(view, halfVector), 0.0);
    float rimFacing = pow(1.0 - nDotV, 2.25);
    float specularPower = mix(128.0, 12.0, roughness);
    float sunSpecular = pow(max(dot(normal, halfVector), 0.0), specularPower);

    vec3 sunColor = vec3(1.0, 0.92, 0.78);
    vec3 skyColor = vec3(0.52, 0.64, 0.82);
    vec3 groundColor = vec3(0.16, 0.15, 0.14);
    vec3 dielectricF0 = vec3(0.04);
    vec3 f0 = mix(dielectricF0, texturedBaseColor, metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
    vec3 diffuseColor = texturedBaseColor * mix(1.0, 0.34, metallic);
    vec3 ambientColor = mix(groundColor, skyColor, skyFacing);
    vec3 displayLift = mix(vec3(0.055, 0.065, 0.075), texturedBaseColor, 0.86);

    vec3 ambientTerm = displayLift * ambientColor * 0.54 * occlusion;
    vec3 diffuseTerm = diffuseColor * sunColor * wrappedSun * 0.90;
    vec3 bounceTerm = texturedBaseColor * groundColor * (1.0 - skyFacing) * 0.16;
    vec3 specularTerm = (fresnel + vec3(0.035)) * sunColor * sunSpecular * mix(0.64, 0.20, roughness) * (0.35 + 0.65 * nDotV);
    vec3 rimTerm = (skyColor * 0.18 + texturedBaseColor * 0.10) * rimFacing * mix(1.0, 0.55, metallic);

    vec3 litColor = clamp(ambientTerm + diffuseTerm + bounceTerm + specularTerm + rimTerm + emissive, vec3(0.0), vec3(1.0));
    outColor = vec4(linear_to_srgb(litColor), outputAlpha);
}
