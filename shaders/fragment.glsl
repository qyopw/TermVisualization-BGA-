#version 100
precision mediump float;

uniform vec3 u_lightDir;
uniform float u_transparency;  // 0.0 = opaque, 1.0 = fully transparent

varying vec3 v_normal;
varying vec3 v_positionView;

void main() {
    vec3 N = normalize(v_normal);
    vec3 L = normalize(u_lightDir);
    float brightness = 0.38 + 0.62 * abs(dot(N, L));
    vec3 color = vec3(0.604, 0.694, 0.792) * brightness;  // #9AB1C9 base
    gl_FragColor = vec4(color, 1.0 - u_transparency);
}