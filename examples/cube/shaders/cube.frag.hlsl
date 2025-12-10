// Skybox fragment shader
// Samples from a cubemap texture

[[vk::binding(0, 0)]] TextureCube<float4> skyboxTexture : register(t0);
[[vk::binding(1, 0)]] SamplerState skyboxSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float3 viewDir : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    // Normalize the view direction and sample the cubemap
    float3 dir = normalize(input.viewDir);
    return skyboxTexture.Sample(skyboxSampler, dir);
}
