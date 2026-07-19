#version 460

layout(push_constant) uniform PushConstants {
    mat4 worldToClip;
    vec4 normalToWorld0;
    vec4 normalToWorld1;
    vec4 normalToWorld2;
    vec4 materialColor;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in vec4 inMaterial;
layout(location = 6) in vec4 inMaterial2;
layout(location = 7) in vec4 inMaterial3;

layout(location = 0) out vec3 baseColor;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec3 lightDirection;
layout(location = 3) out vec2 texCoord;
layout(location = 4) out vec4 worldTangent;
layout(location = 5) out vec4 materialFactors;
layout(location = 6) out vec4 materialFactors2;
layout(location = 7) out vec4 materialFactors3;

void main()
{
    mat3 normalToWorld = mat3(
        pc.normalToWorld0.xyz,
        pc.normalToWorld1.xyz,
        pc.normalToWorld2.xyz
    );
    vec3 light = vec3(pc.normalToWorld0.w, pc.normalToWorld1.w, pc.normalToWorld2.w);

    gl_Position = pc.worldToClip * vec4(inPosition, 1.0);
    baseColor = clamp(inColor * pc.materialColor.rgb, vec3(0.0), vec3(1.0));
    worldNormal = normalize(normalToWorld * inNormal);
    lightDirection = length(light) > 0.0001 ? normalize(light) : normalize(vec3(-0.35, 0.65, 0.68));
    texCoord = inTexCoord;
    worldTangent = vec4(normalize(normalToWorld * inTangent.xyz), inTangent.w);
    materialFactors = inMaterial;
    materialFactors2 = inMaterial2;
    materialFactors3 = inMaterial3;
}
