#version 450
layout(set = 0, binding = 0) uniform sampler2D frame;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() { color = vec4(texture(frame, uv).rgb, 1.0); }
