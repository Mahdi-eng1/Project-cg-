#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

uniform sampler2D uTexture0;
uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform vec3 uTint;
uniform float uAmbientStrength;

out vec4 FragColor;

void main() {
    vec3 albedo = texture(uTexture0, vUV).rgb * uTint;
    vec3 n = normalize(vNormal);
    vec3 l = normalize(-uLightDir);
    float diff = max(dot(n, l), 0.0);

    vec3 ambient = uAmbientStrength * albedo;
    vec3 diffuse = diff * albedo;

    float fogDist = length(vWorldPos - uViewPos);
    float fogFactor = clamp((30.0 - fogDist) / 30.0, 0.0, 1.0);
    vec3 fogColor = vec3(0.10, 0.12, 0.15);

    vec3 litColor = ambient + diffuse;
    vec3 finalColor = mix(fogColor, litColor, fogFactor);
    FragColor = vec4(finalColor, 1.0);
}
