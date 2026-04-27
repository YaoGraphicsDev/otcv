#version 450

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

layout (push_constant) uniform PushConstants {
    float alpha;
} consts;

layout(set = 0, binding = 0) uniform sampler samplerIn;

// set 1 - framegraph
//  binding 0 - 15:     textures
layout(set = 1, binding = 0) uniform texture2D textureIn;

void main() {
    vec4 color = texture(sampler2D(textureIn, samplerIn), inUV);
    outColor = vec4(color.rgb, color.a * consts.alpha);
}