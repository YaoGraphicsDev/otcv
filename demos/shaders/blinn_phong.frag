#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outFragColor;

layout(push_constant) uniform PushConstants {
    int  useSolidColor;
    vec3 solidColor;
    int doubleSided;
    float opacity;      
    vec3 lightDirection;
    vec3 viewPos;
} consts;

layout(set = 0, binding = 1) uniform sampler albedoSampler;

//  binding 0 - 15:     textures
layout(set = 1, binding = 0) uniform texture2D albedoTexture;

void main() {
    vec3 albedo = (consts.useSolidColor != 0)
        ? consts.solidColor
        : texture(sampler2D(albedoTexture, albedoSampler), inUV).rgb;

    vec3 N = normalize(inWorldNormal);
    if (consts.doubleSided != 0 && !gl_FrontFacing) {
        N = -N;
    }
    vec3 L = normalize(-consts.lightDirection);
    vec3 V = normalize(consts.viewPos - inWorldPos);
    vec3 H = normalize(L + V);

    // Hardcoded lighting parameters
    vec3  lightColor        = vec3(1.0, 1.0, 1.0);
    float lightIntensity    = 1.0;
    float ambientStrength   = 0.08;
    float specularStrength  = 0.5;
    float shininess         = 64.0;

    float NdotL = max(dot(N, L), 0.0);
    float spec  = 0.0;

    if (NdotL > 0.0) {
        spec = pow(max(dot(N, H), 0.0), shininess);
    }

    vec3 ambient  = ambientStrength * albedo * lightColor * lightIntensity;
    vec3 diffuse  = NdotL * albedo * lightColor * lightIntensity;
    vec3 specular = specularStrength * spec * lightColor * lightIntensity;

    vec3 color = ambient + diffuse + specular;
    outFragColor = vec4(color, consts.opacity);
}