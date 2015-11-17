#version 330

uniform vec3 u_emissive;
uniform vec3 u_diffuse;
uniform vec3 u_eye;

uniform sampler2D u_diffuseTex;

struct PointLight
{
    vec3 position;
    vec3 color;
};

uniform PointLight u_lights[2];

in vec3 position;
in vec3 normal;
in vec2 texCoord;

out vec4 f_color;

void main()
{
    vec3 eyeDir = normalize(u_eye - position);
    vec3 light = u_emissive;

    vec3 diffuseSample = texture(u_diffuseTex, texCoord).rgb;

    for(int i = 0; i < 2; ++i)
    {
        vec3 lightDir = normalize(u_lights[i].position - position);
        light += u_lights[i].color * u_diffuse * max(dot(normal, lightDir), 0);

        vec3 halfDir = normalize(lightDir + eyeDir);
        light += u_lights[i].color * u_diffuse * pow(max(dot(normal, halfDir), 0), 128);
    }
    f_color = vec4(mix(light, diffuseSample, 0.5), 1); 

}