#version 100
precision mediump float;

attribute vec3 a_position;
attribute vec3 a_normal;

uniform mat4 u_mvpMatrix;
uniform mat4 u_modelViewMatrix;
uniform mat3 u_normalMatrix;

varying vec3 v_normal;
varying vec3 v_positionView;

void main() {
    vec4 posView = u_modelViewMatrix * vec4(a_position, 1.0);
    gl_Position = u_mvpMatrix * vec4(a_position, 1.0);
    v_normal = u_normalMatrix * a_normal;
    v_positionView = posView.xyz;
}