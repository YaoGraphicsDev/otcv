#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUV;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 projView;
} Ubo;

void main() {
    vec4 worldPosition = Ubo.model * vec4(inPosition, 1.0);

    outWorldPos = worldPosition.xyz;
    outWorldNormal = mat3(transpose(inverse(Ubo.model))) * inNormal;
    outUV = inUV;

    gl_Position = Ubo.projView * worldPosition;
}
