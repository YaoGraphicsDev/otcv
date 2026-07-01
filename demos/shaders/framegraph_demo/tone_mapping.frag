#version 450

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outLDR;


layout(set = 0, binding = 0) uniform sampler samplerHDR;

// set 1 - framegraph
//  binding 0 - 15:     textures
layout(set = 1, binding = 0) uniform texture2D textureHDR;


void main() {
    vec3 hdr = texture(sampler2D(textureHDR, samplerHDR), inUV).rgb;
    hdr = max(hdr, vec3(0.0f));
    // reinhard tone mapping
    // outLDR = vec4(hdr / (hdr + vec3(1.0)), 1.0f);
    float exposure = 0.7f;
    outLDR = vec4(vec3(1.0) - exp(-hdr * vec3(exposure)), 1.0f);
}