#include "RenderWorld.h"
#include "BuiltinTextures.h"

#include "FrustumMath.h"
#include <iostream>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Embedded HLSL PBR Shader Code
static const char* g_pbr_shader_hlsl = R"(
cbuffer SceneConstants : register(b0) {
    row_major float4x4 ViewProj;
    row_major float4x4 LightViewProj;
    float3 CameraPos;
    float Time;
    float3 SunDir;
    float ShadowStrength;
    float3 SunColor;
    float ShadowSoftness;
    float3 AmbientColor;
    float ShadowAtlasSize;
    float3 FogColor;
    float FogStart;
    float FogEnd;
    float3 PadFog;
    row_major float4x4 InvViewProj;
    row_major float4x4 LightViewProjCascade1;
};

cbuffer BoneConstants : register(b1) {
    row_major float4x4 BoneMatrices[128];
};

struct PointLightData {
    float3 Pos;
    float Radius;
    float3 Color;
    float Intensity;
};

cbuffer LightConstants : register(b2) {
    PointLightData PointLights[16];
    int PointLightCount;
    int MaxShadowLights;
    float2 PadLight;
    int4 PointLightShadowSlots[4];
};

cbuffer MaterialConstants : register(b3) {
    float4 MatBaseColor;
    float MatRoughness;
    float MatMetallic;
    float MatEmissive;
    float MatAlphaCutoff;
    float2 MatUVTiling;
    float2 MatUVOffset;
    int MatAlphaMode;
    int MatPresetId;
    float2 MatPad;
};

Texture2D ShadowMap : register(t0);
Texture2D AlbedoMap : register(t1);
Texture2D NormalMap : register(t2);
// Terrain surface detail (only sampled when Param.y == 5).
Texture2D SplatMap : register(t3);
Texture2D DetailGrass : register(t4);
Texture2D DetailRock : register(t5);
Texture2D DetailSand : register(t6);
Texture2D DetailCobble : register(t7);
Texture2D DetailGrassN : register(t8);
Texture2D DetailRockN : register(t9);
Texture2D DetailSandN : register(t10);
Texture2D DetailCobbleN : register(t11);
SamplerComparisonState ShadowSampler : register(s0);
SamplerState TextureSampler : register(s1);

struct VS_INPUT {
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    uint4 Joints : JOINTS;
    float4 Weights : WEIGHTS;
    // Instance buffer attributes (Slot 1)
    float3 InstPos : INST_POS;
    float4 InstQuat : INST_QUAT;
    float3 InstScale : INST_SCALE;
    float2 InstParam : INST_PARAM;
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float2 Param : PARAM;
    float4 ShadowPos : SHADOW_POS;
};

float3 RotateVector(float3 v, float4 q) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

PS_INPUT VS_Main(VS_INPUT input) {
    PS_INPUT output;

    float4x4 skinMat = BoneMatrices[input.Joints.x] * input.Weights.x
                     + BoneMatrices[input.Joints.y] * input.Weights.y
                     + BoneMatrices[input.Joints.z] * input.Weights.z
                     + BoneMatrices[input.Joints.w] * input.Weights.w;

    float3 skinnedPos = mul(float4(input.Pos, 1.0f), skinMat).xyz;
    float3 skinnedNormal = mul(float4(input.Normal, 0.0f), skinMat).xyz;

    float3 scaled = skinnedPos * input.InstScale;
    float3 rotated = RotateVector(scaled, input.InstQuat);
    float3 worldPos = rotated + input.InstPos;

    output.Pos = mul(float4(worldPos, 1.0f), ViewProj);
    output.WorldPos = worldPos;
    float3 safeScale = (step(0.0, input.InstScale) * 2.0 - 1.0) * max(abs(input.InstScale), 0.0001);
    output.Normal = normalize(RotateVector(skinnedNormal / safeScale, input.InstQuat));
    output.UV = input.UV;
    output.Param = input.InstParam;
    output.ShadowPos = mul(float4(worldPos, 1.0f), LightViewProj);
    return output;
}

float SampleSunCascade(float2 cascadeUV, float currentDepth, float depthBias, float cascadeOffsetX, float filterRadius) {
    float2 atlasUV = float2(cascadeOffsetX + clamp(cascadeUV.x, 0.002f, 0.998f) * 0.5f,
                            clamp(cascadeUV.y, 0.002f, 0.998f) * 0.5f);
    float2 tileMin = float2(cascadeOffsetX + 0.001f, 0.001f);
    float2 tileMax = float2(cascadeOffsetX + 0.499f, 0.499f);
    float texel = 1.0f / max(ShadowAtlasSize, 1024.0f);
    float2 texelSize = float2(texel, texel);

    static const float2 c_vogel_disk[16] = {
        float2( 0.176777f,  0.000000f),
        float2(-0.225772f,  0.206826f),
        float2( 0.034558f, -0.393771f),
        float2( 0.284571f,  0.371173f),
        float2(-0.522223f, -0.092374f),
        float2( 0.494695f, -0.314685f),
        float2(-0.165466f,  0.615525f),
        float2(-0.315561f, -0.607594f),
        float2( 0.684642f,  0.250030f),
        float2(-0.712256f,  0.294009f),
        float2( 0.343354f, -0.733729f),
        float2( 0.253730f,  0.808932f),
        float2(-0.764746f, -0.443186f),
        float2( 0.897134f, -0.197232f),
        float2(-0.547507f,  0.778772f),
        float2(-0.126487f, -0.976090f)
    };

    float shadow = 0.0f;
    [unroll]
    for (int i = 0; i < 16; ++i) {
        float2 sampleUV = clamp(atlasUV + c_vogel_disk[i] * (filterRadius * texelSize), tileMin, tileMax);
        shadow += ShadowMap.SampleCmpLevelZero(ShadowSampler, sampleUV, currentDepth - depthBias);
    }
    return shadow * (1.0f / 16.0f);
}

float CalculateShadow(float3 worldPos, float3 geomNormal, float3 L, float2 screenPos) {
    if (ShadowStrength <= 0.0f) return 1.0f;
    float NdotL = max(dot(geomNormal, L), 0.0f);

    float normalBias = 0.055f * saturate(1.0f - NdotL);
    float3 biasedWorldPos = worldPos + geomNormal * normalBias;

    float4 shadowPos0 = mul(float4(biasedWorldPos, 1.0f), LightViewProj);
    float3 proj0 = shadowPos0.xyz / shadowPos0.w;
    float2 uv0 = float2(proj0.x * 0.5f + 0.5f, -proj0.y * 0.5f + 0.5f);
    float depth0 = proj0.z;
    float edge0 = max(abs(proj0.x), abs(proj0.y));
    bool valid0 = (edge0 < 0.996f && depth0 >= 0.0f && depth0 <= 1.0f);

    float depthBias0 = max(0.00028f * (1.0f - NdotL), 0.00007f);
    float filterRadius = max(0.5f, ShadowSoftness);

    if (PadFog.x < 0.5f) {
        // Single-cascade mode
        if (!valid0) return 1.0f;
        float borderFade0 = saturate((1.0f - edge0) * 7.0f);
        float shadow0 = SampleSunCascade(uv0, depth0, depthBias0, 0.0f, filterRadius);
        return lerp(1.0f, shadow0, borderFade0 * saturate(ShadowStrength));
    }

    // 2-Cascade CSM mode (Cascade 0 = Near, Cascade 1 = Far)
    if (valid0 && edge0 < 0.76f) {
        float shadow0 = SampleSunCascade(uv0, depth0, depthBias0, 0.0f, filterRadius);
        return lerp(1.0f, shadow0, saturate(ShadowStrength));
    }

    float3 biasedWorldPos1 = worldPos + geomNormal * (normalBias * 2.2f);
    float4 shadowPos1 = mul(float4(biasedWorldPos1, 1.0f), LightViewProjCascade1);
    float3 proj1 = shadowPos1.xyz / shadowPos1.w;
    float2 uv1 = float2(proj1.x * 0.5f + 0.5f, -proj1.y * 0.5f + 0.5f);
    float depth1 = proj1.z;
    float edge1 = max(abs(proj1.x), abs(proj1.y));
    bool valid1 = (edge1 < 0.996f && depth1 >= 0.0f && depth1 <= 1.0f);
    if (!valid0 && !valid1) return 1.0f;

    float depthBias1 = depthBias0 * 2.2f;
    float shadow1 = valid1 ? SampleSunCascade(uv1, depth1, depthBias1, 0.5f, filterRadius * 0.85f) : 1.0f;
    float borderFade1 = valid1 ? saturate((1.0f - edge1) * 7.0f) : 0.0f;
    shadow1 = lerp(1.0f, shadow1, borderFade1);

    if (valid0) {
        float shadow0 = SampleSunCascade(uv0, depth0, depthBias0, 0.0f, filterRadius);
        float blendT = smoothstep(0.76f, 0.98f, edge0);
        float combined = lerp(shadow0, shadow1, blendT);
        return lerp(1.0f, combined, saturate(ShadowStrength));
    }
    return lerp(1.0f, shadow1, saturate(ShadowStrength));
}

float CalculatePointLightShadow(int shadowSlot, float3 lightPos, float lightRadius, float3 worldPos, float3 geomNormal) {
    if (shadowSlot < 0 || ShadowStrength <= 0.0f) return 1.0f;
    float3 toLight = lightPos - worldPos;
    float dist = length(toLight);
    if (dist <= 0.18f || dist >= lightRadius) return 1.0f;
    float3 Lp = toLight / dist;
    float NdotL = max(dot(geomNormal, Lp), 0.0f);
    float3 biasedPos = worldPos + geomNormal * (0.045f * saturate(1.0f - NdotL) + 0.01f);
    float3 rel = biasedPos - lightPos;
    float3 absRel = abs(rel);

    float ma = 1.0f, sc = 0.0f, tc = 0.0f;
    float faceIdx = 0.0f;
    if (absRel.x >= absRel.y && absRel.x >= absRel.z) {
        ma = absRel.x;
        if (rel.x >= 0.0f) { sc = -rel.z; tc = rel.y; faceIdx = 0.0f; }
        else               { sc =  rel.z; tc = rel.y; faceIdx = 1.0f; }
    } else if (absRel.y >= absRel.x && absRel.y >= absRel.z) {
        ma = absRel.y;
        if (rel.y >= 0.0f) { sc = rel.x; tc = -rel.z; faceIdx = 2.0f; }
        else               { sc = rel.x; tc =  rel.z; faceIdx = 3.0f; }
    } else {
        ma = absRel.z;
        if (rel.z >= 0.0f) { sc =  rel.x; tc = rel.y; faceIdx = 4.0f; }
        else               { sc = -rel.x; tc = rel.y; faceIdx = 5.0f; }
    }

    float zn = 0.15f;
    float zf = max(0.5f, lightRadius);
    if (ma <= zn) return 1.0f;

    float2 faceUV = float2((sc / ma) * 0.5f + 0.5f, -(tc / ma) * 0.5f + 0.5f);
    faceUV = clamp(faceUV, 0.004f, 0.996f);

    float currentDepth = (zf / (zf - zn)) * (1.0f - zn / ma);
    float depthBias = max(0.0014f * (1.0f - NdotL), 0.00035f);

    float2 tileMin = float2(faceIdx * 0.125f + 0.0005f, 0.5f + (float)shadowSlot * 0.125f + 0.0005f);
    float2 tileMax = float2((faceIdx + 1.0f) * 0.125f - 0.0005f, 0.5f + ((float)shadowSlot + 1.0f) * 0.125f - 0.0005f);
    float2 atlasUV = float2((faceIdx + faceUV.x) * 0.125f, 0.5f + ((float)shadowSlot + faceUV.y) * 0.125f);
    float texel = 1.0f / max(ShadowAtlasSize, 1024.0f);
    float filterR = max(0.5f, ShadowSoftness * 0.65f) * texel;

    static const float2 c_p_offsets[4] = {
        float2(-0.5f, -0.5f), float2(0.5f, -0.5f),
        float2(-0.5f,  0.5f), float2(0.5f,  0.5f)
    };
    float shadow = 0.0f;
    [unroll]
    for (int k = 0; k < 4; ++k) {
        float2 sampleUV = clamp(atlasUV + c_p_offsets[k] * filterR, tileMin, tileMax);
        shadow += ShadowMap.SampleCmpLevelZero(ShadowSampler, sampleUV, currentDepth - depthBias);
    }
    shadow *= 0.25f;
    return lerp(1.0f, shadow, saturate(ShadowStrength));
}

float3 PerturbNormal(float3 normalMapSample, float3 N, float3 worldPos, float2 uv) {
    if (dot(normalMapSample, normalMapSample) < 0.05) {
        return N; // No normal map bound, retain geometric vertex normal
    }
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = rsqrt(max(dot(T, T), dot(B, B)) + 0.00001);
    float3x3 TBN = float3x3(T * invmax, B * invmax, N);

    float3 tangentNormal = normalMapSample * 2.0 - 1.0;
    return normalize(mul(tangentNormal, TBN));
}

float4 PS_Main(PS_INPUT input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
    int matId = MatPresetId != 0 ? MatPresetId : (int)input.Param.x;
    if (matId >= 100) {
        // Unlit Emissive Colors for 3D Transform Gizmo & Selection Outline (bypasses all sampling)
        if (matId == 100) return float4(1.0, 0.12, 0.12, 1.0); // Bright Red (+X Axis)
        if (matId == 101) return float4(0.12, 1.0, 0.12, 1.0); // Bright Green (+Y Axis)
        if (matId == 102) return float4(0.18, 0.50, 1.0, 1.0); // Bright Blue (+Z Axis)
        if (matId == 103) return float4(1.0, 0.95, 0.15, 1.0); // Bright Yellow (Pivot Center)
        if (matId == 104) return float4(1.0, 0.72, 0.05, 1.0); // Amber Orange (Selection Bounding Box)
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    float2 uv = input.UV * MatUVTiling + MatUVOffset;

    float3 N = normalize(input.Normal);
    if (!isFrontFace) {
        N = -N;
    }

    // Normal Mapping (Screen-space tangent perturbation)
    float3 normalSample = NormalMap.Sample(TextureSampler, uv).rgb;
    N = PerturbNormal(normalSample, N, input.WorldPos, uv);

    float3 L = normalize(-SunDir);
    float3 V = normalize(CameraPos - input.WorldPos);
    float3 H = normalize(L + V);

    // Multi-Material Palette (Param.x determines material look)
    float3 baseColor = float3(0.85, 0.88, 0.92);
    float metallic = 0.05;
    float roughness = 0.45;
    float opacity = 1.0;

    if (matId == 1) {
        // Vibrant Coral / Red
        baseColor = float3(0.92, 0.28, 0.18);
        roughness = 0.35;
        metallic = 0.1;
    } else if (matId == 2) {
        // Gold / Metallic Brass
        baseColor = float3(1.0, 0.78, 0.25);
        metallic = 0.85;
        roughness = 0.25;
    } else if (matId == 3) {
        // Emerald Teal
        baseColor = float3(0.18, 0.82, 0.60);
        metallic = 0.2;
        roughness = 0.3;
    } else if (matId == 4) {
        // Dynamic Ocean Water with Animated Waves, Specular Glints & Sea Foam
        float waveTime = Time * 2.2;
        float w1 = sin(input.WorldPos.x * 0.35 + waveTime * 0.8 + input.WorldPos.z * 0.25);
        float w2 = cos(input.WorldPos.z * 0.45 - waveTime * 1.1 + input.WorldPos.x * 0.20);
        float w3 = sin((input.WorldPos.x + input.WorldPos.z) * 0.7 + waveTime * 1.5);
        float wave = (w1 + w2 + w3 * 0.5) / 2.5;

        // Normal perturbation for glittering 3D wave facets
        float3 waveNorm = normalize(float3(-(w1 + w3 * 0.5) * 0.25, 1.0, -(w2 + w3 * 0.5) * 0.25));
        N = normalize(N + waveNorm * 0.8);

        // Deep Tropical Azure to Shallow Aqua
        float3 deepOcean = float3(0.04, 0.22, 0.55);
        float3 shallowAqua = float3(0.12, 0.65, 0.78);
        baseColor = lerp(deepOcean, shallowAqua, saturate(wave * 0.5 + 0.5));

        // Sparkling Sea Foam on wave crests
        float foam = smoothstep(0.48, 0.85, wave);
        baseColor = lerp(baseColor, float3(0.85, 0.95, 1.0), foam * 0.75);

        metallic = 0.15;
        roughness = 0.06; // Ultra-crisp sparkling sun glints across the water
    } else if (matId == 5) {
        // Amethyst Purple
        baseColor = float3(0.70, 0.25, 0.92);
        roughness = 0.35;
    } else if (matId == 6) {
        // Stylized Pine Tree Foliage & Bark
        if (uv.y < 0.1) {
            baseColor = float3(0.24, 0.15, 0.09); // Dark Wood Bark
            roughness = 0.90;
        } else {
            // Rich deep alpine evergreen needles with natural gradient
            baseColor = float3(0.10, 0.28, 0.13) * (0.85 + 0.30 * uv.y);
            roughness = 0.70;
        }
        metallic = 0.02;
    } else if (matId == 7) {
        // Cobalt Royal Steel Armor with Golden Luster
        baseColor = float3(0.12, 0.25, 0.52);
        metallic = 0.85;
        roughness = 0.25;
    } else if (matId == 8) {
        // Weathered Alpine Granite Boulder
        baseColor = float3(0.44, 0.42, 0.40);
        roughness = 0.82;
        metallic = 0.02;
    } else if (matId == -1) {
        // Explicit Ground Grid Floor
        baseColor = float3(0.85, 0.88, 0.92);
    } else {
        // Custom PBR Material
        baseColor = MatBaseColor.rgb;
        roughness = clamp(MatRoughness, 0.04, 1.0);
        metallic = clamp(MatMetallic, 0.0, 1.0);
        opacity = MatBaseColor.a;
    }

    // Sample Albedo Texture (Texture 0 is pure white, preserving baseColor)
    float4 texColor = AlbedoMap.Sample(TextureSampler, uv);
    baseColor *= texColor.rgb;
    opacity *= texColor.a;

    // Presets 7/8 bind a CC0 photograph; let it carry the colour instead of the flat tint.
    if (MatPad.y > 0.5) {
        baseColor = lerp(baseColor, texColor.rgb, 0.85);
    }

    if (MatAlphaMode == 1 && opacity < MatAlphaCutoff) {
        discard;
    }

    // --------------------------------------------------------------------------------------------
    // Terrain surface detail. Four CC0 PBR sets are tiled in world space and blended by the
    // splat weights baked in TerrainSystem::update_detail_splat. The baked 2048 albedo only
    // carries biome colour (~8 texels/m), far too coarse to read as a surface, so it is
    // demoted to a large-scale hue tint here (texture ID 5 only).
    // --------------------------------------------------------------------------------------------
    if ((int)input.Param.y == 5) {
        if (MatPad.x > 0.0) {
            float4 splat = SplatMap.Sample(TextureSampler, input.UV);
            float4 w = max(splat - 0.02, 0.0);
            w /= max(dot(w, 1.0), 0.0001);

            float2 uvTop = input.WorldPos.xz * MatPad.x;
            float topWeight = saturate(N.y * N.y);

            // Photographs are sRGB encoded; the pipeline works in linear space.
            float3 gT = DetailGrass.Sample(TextureSampler, uvTop).rgb;
            float3 rT = DetailRock.Sample(TextureSampler, uvTop).rgb;
            float3 sT = DetailSand.Sample(TextureSampler, uvTop).rgb;
            float3 cT = DetailCobble.Sample(TextureSampler, uvTop).rgb;
            float3 albedoTop = gT * gT * w.x + rT * rT * w.y + sT * sT * w.z + cT * cT * w.w;

            float3 normalTop = DetailGrassN.Sample(TextureSampler, uvTop).rgb * w.x
                             + DetailRockN.Sample(TextureSampler, uvTop).rgb * w.y
                             + DetailSandN.Sample(TextureSampler, uvTop).rgb * w.z
                             + DetailCobbleN.Sample(TextureSampler, uvTop).rgb * w.w;

            float3 detailAlbedo = albedoTop;
            float3 detailNormal = normalTop;

            // Steep faces smear a top-down projection, so fall back to a side projection.
            // Flat ground never pays for it, which is the overwhelming majority of pixels.
            if (topWeight < 0.80) {
                float2 uvSide = (abs(N.x) > abs(N.z)) ? input.WorldPos.zy * MatPad.x
                                                      : input.WorldPos.xy * MatPad.x;
                float3 gS = DetailGrass.Sample(TextureSampler, uvSide).rgb;
                float3 rS = DetailRock.Sample(TextureSampler, uvSide).rgb;
                float3 sS = DetailSand.Sample(TextureSampler, uvSide).rgb;
                float3 cS = DetailCobble.Sample(TextureSampler, uvSide).rgb;
                float3 albedoSide = gS * gS * w.x + rS * rS * w.y + sS * sS * w.z + cS * cS * w.w;

                float3 normalSide = DetailGrassN.Sample(TextureSampler, uvSide).rgb * w.x
                                  + DetailRockN.Sample(TextureSampler, uvSide).rgb * w.y
                                  + DetailSandN.Sample(TextureSampler, uvSide).rgb * w.z
                                  + DetailCobbleN.Sample(TextureSampler, uvSide).rgb * w.w;

                float sideBlend = smoothstep(0.80, 0.45, topWeight);
                detailAlbedo = lerp(albedoTop, albedoSide, sideBlend);
                detailNormal = lerp(normalTop, normalSide, sideBlend);
            }

            // Keep the biome hue, drop the biome brightness: the photograph owns the detail.
            // The tint only follows the grass/sand layers, otherwise the stone plaza and the
            // cliff faces pick up a green cast from the meadow palette.
            float macroLum = max(dot(baseColor, float3(0.299, 0.587, 0.114)), 0.001);
            float3 macroTint = lerp(float3(1.0, 1.0, 1.0), baseColor / macroLum, 0.55);
            macroTint = lerp(float3(1.0, 1.0, 1.0), macroTint, saturate(w.x + 0.3 * w.z));
            baseColor = detailAlbedo * macroTint;

            // Tangent basis of the XZ projection: the map's X follows world X, its Y follows Z.
            float3 dn = detailNormal * 2.0 - 1.0;
            N = normalize(N + float3(dn.x, 0.0, dn.y) * 0.85);
        } else {
            // No assets on disk: keep the original procedural micro-grain.
            float distToCam = length(input.WorldPos - CameraPos);
            float detailFade = saturate(1.0 - (distToCam - 14.0) / 16.0);
            if (detailFade > 0.01) {
                float2 microUV = input.WorldPos.xz * 0.65;
                float d1 = sin(microUV.x * 6.283 + sin(microUV.y * 3.141)) * 0.5 + 0.5;
                float d2 = cos(microUV.y * 7.539 - sin(microUV.x * 4.188)) * 0.5 + 0.5;
                float microGrain = (d1 * 0.6 + d2 * 0.4);

                baseColor *= lerp(1.0, 0.94 + 0.12 * microGrain, detailFade);

                float3 dNorm = float3(
                    cos(microUV.x * 6.283) * 0.12,
                    1.0,
                    sin(microUV.y * 7.539) * 0.12
                );
                N = normalize(lerp(N, normalize(N + dNorm * 0.25), detailFade));
            }
        }

        // Natural dielectric mineral terrain PBR response (matte soil, scree and bedrock)
        roughness = 0.85;
        metallic = 0.01;
    }

    // Procedural anti-aliased floor grid shader on ground plane (if untextured / matId == 0 and Param.y == 0)
    if (abs(N.y) > 0.8 && abs(input.WorldPos.y) < 0.2 && matId == 0 && (int)input.Param.y == 0) {
        float2 pos = input.WorldPos.xz;
        float distToCam = length(input.WorldPos - CameraPos);
        float gridFade = saturate(1.0 - (distToCam - 20.0) / 160.0);

        float2 fw = max(fwidth(pos), 0.001);

        // 1-meter minor grid lines
        float2 g1 = abs(frac(pos - 0.5) - 0.5) / fw;
        float line1 = 1.0 - min(min(g1.x, g1.y), 1.0);

        // 5-meter major grid lines
        float2 fw5 = max(fwidth(pos * 0.2), 0.001);
        float2 g5 = abs(frac(pos * 0.2 - 0.5) - 0.5) / fw5;
        float line5 = 1.0 - min(min(g5.x, g5.y), 1.0);

        // Coordinate axes: X-Axis (Red: Z == 0), Z-Axis (Blue: X == 0)
        float axisX = 1.0 - saturate(abs(pos.y) / fw.y * 0.75);
        float axisZ = 1.0 - saturate(abs(pos.x) / fw.x * 0.75);

        float3 floorBg    = float3(0.10, 0.11, 0.13);
        float3 gridMinor  = float3(0.20, 0.23, 0.28);
        float3 gridMajor  = float3(0.36, 0.42, 0.52);
        float3 axisRedCol = float3(0.85, 0.22, 0.22);
        float3 axisBluCol = float3(0.22, 0.48, 0.92);

        float3 gridCol = floorBg;
        gridCol = lerp(gridCol, gridMinor, line1 * 0.65);
        gridCol = lerp(gridCol, gridMajor, line5 * 0.90);
        gridCol = lerp(gridCol, axisRedCol, axisX);
        gridCol = lerp(gridCol, axisBluCol, axisZ);

        baseColor = lerp(floorBg, gridCol, gridFade);
        metallic = 0.02;
        roughness = 0.85;
    }

    // PBR Cook-Torrance Microfacet BRDF
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Fresnel-Schlick
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 F = F0 + (1.0 - F0) * pow(max(1.0 - VdotH, 0.0), 5.0);

    // Normal Distribution GGX (Trowbridge-Reitz)
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = (NdotH * NdotH * (alpha2 - 1.0) + 1.0);
    float D = alpha2 / (3.14159265 * denom * denom + 0.00001);

    // Geometric Shadowing (Schlick-GGX)
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G1L = NdotL / (NdotL * (1.0 - k) + k);
    float G1V = NdotV / (NdotV * (1.0 - k) + k);
    float G = G1L * G1V;

    float3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    float3 specular = numerator / denominator;

    float3 kD = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);
    float3 diffuse = kD * baseColor / 3.14159265;

    // Geometric vertex normal for physical shadow normal-offset bias
    float geomLen = length(input.Normal);
    float3 geomNormal = geomLen > 0.001f ? (input.Normal / geomLen) : float3(0.0f, 1.0f, 0.0f);
    if (!isFrontFace) geomNormal = -geomNormal;

    // Real-Time Directional Shadow Factor with Adaptive Slope & Normal Bias, Rotated Vogel Disk PCF
    float shadow = CalculateShadow(input.WorldPos, geomNormal, L, input.Pos.xy);

    float3 radiance = SunColor * NdotL;
    float3 directLight = (diffuse + specular) * radiance * shadow;

    // ----------------------------------------------------
    // Dynamic Point Lights (Cook-Torrance PBR BRDF)
    // ----------------------------------------------------
    float3 pointLightAccum = float3(0.0, 0.0, 0.0);
    [loop]
    for (int p = 0; p < PointLightCount; ++p) {
        float3 lightVec = PointLights[p].Pos - input.WorldPos;
        float dist = length(lightVec);
        if (dist < PointLights[p].Radius && dist > 0.001) {
            float3 Lp = lightVec / dist;
            float3 Hp = normalize(Lp + V);
            float NdotLp = max(dot(N, Lp), 0.0);
            if (NdotLp > 0.0) {
                float normDist = dist / PointLights[p].Radius;
                float win = max(0.0, 1.0 - normDist * normDist);
                float atten = (win * win) / (dist * dist + 1.0);

                float NdotHp = max(dot(N, Hp), 0.0);
                float VdotHp = max(dot(V, Hp), 0.0);

                float3 Fp = F0 + (1.0 - F0) * pow(max(1.0 - VdotHp, 0.0), 5.0);
                float denomP = (NdotHp * NdotHp * (alpha2 - 1.0) + 1.0);
                float Dp = alpha2 / (3.14159265 * denomP * denomP + 0.00001);
                float G1Lp = NdotLp / (NdotLp * (1.0 - k) + k);
                float Gp = G1Lp * G1V;

                float3 specP = (Dp * Gp * Fp) / (4.0 * NdotV * NdotLp + 0.0001);
                float3 diffP = ((float3(1.0, 1.0, 1.0) - Fp) * (1.0 - metallic) * baseColor) / 3.14159265;

                int slotVecIdx = p >> 2;
                int slotCompIdx = p & 3;
                int4 slotVec = PointLightShadowSlots[slotVecIdx];
                int shadowSlot = (slotCompIdx == 0) ? slotVec.x : ((slotCompIdx == 1) ? slotVec.y : ((slotCompIdx == 2) ? slotVec.z : slotVec.w));
                float pointShadow = CalculatePointLightShadow(shadowSlot, PointLights[p].Pos, PointLights[p].Radius, input.WorldPos, geomNormal);

                float3 radP = PointLights[p].Color * PointLights[p].Intensity * atten * NdotLp * pointShadow;
                pointLightAccum += (diffP + specP) * radP;
            }
        }
    }

    // Two-Tone Hemispheric Skylight with Earth/Grass Bounce.
    // Kept close to neutral: the old sky term peaked at 1.35 in blue on top of an
    // already blue ambient, which desaturated every albedo into pastel grey.
    float hemi = N.y * 0.5 + 0.5;
    float3 groundBounce = float3(0.30, 0.27, 0.23);
    float3 skyLight = lerp(groundBounce, float3(0.66, 0.78, 1.00), hemi);

    // Radiant Ambient Sky Fill (Ensures shadows remain luminous, colorful, and detailed without black voids)
    float3 ambientSky = AmbientColor * skyLight;
    float3 ambient = ambientSky * baseColor * (1.0 - metallic);

    // Conductor fill light. There is no environment probe, so a metal (metallic ~= 1) has no
    // diffuse and only shows the sun's specular lobe, which reads as a black box. Fake the
    // sky reflection with a dedicated hemisphere probe: bright enough to give gold and steel
    // a visible sheen, independent of the deliberately dim ambient term.
    float3 envProbe = lerp(float3(0.30, 0.26, 0.21), float3(0.52, 0.66, 0.92), hemi);
    float3 envSpecular = envProbe * F0 * (1.0 - roughness) * 2.0;

    float3 color = directLight + pointLightAccum + ambient + envSpecular * metallic;

    // Atmospheric Distance & Height Fog (Lowland Valley Mist with Crisp Mountain Peaks)
    float distToCam = length(input.WorldPos - CameraPos);
    float distFog = saturate((distToCam - FogStart) / max(0.001f, FogEnd - FogStart));

    // Lowland valley mist decays exponentially as elevation rises
    float heightFog = exp(-max(0.0f, input.WorldPos.y) * 0.12f);
    float combinedFog = saturate(distFog * distFog * heightFog);

    // In-scattering: fog glows warmly when looking toward the sun
    float sunGaze = pow(max(0.0f, dot(V, L)), 3.5f);
    float3 dynamicFogColor = lerp(FogColor, SunColor * 1.15f, sunGaze * 0.45f);

    color = lerp(color, dynamicFogColor, combinedFog);

    // Emissive contribution
    color += baseColor * MatEmissive;

    // Output pure linear HDR color (ACES filmic tonemapping and gamma are applied in composite pass)
    return float4(color, opacity);
}
)";

// Embedded HLSL Procedural Atmospheric Sky Dome with Sun Disk
static const char* g_sky_shader_hlsl = R"(
cbuffer SceneConstants : register(b0) {
    row_major float4x4 ViewProj;
    row_major float4x4 LightViewProj;
    float3 CameraPos;
    float Time;
    float3 SunDir;
    float pad1;
    float3 SunColor;
    float pad2;
    float3 AmbientColor;
    float pad3;
    float3 FogColor;
    float FogStart;
    float FogEnd;
    float3 PadFog;
    row_major float4x4 InvViewProj;
    row_major float4x4 LightViewProjCascade1;
};

struct VS_SkyOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VS_SkyOut VS_Sky(uint vertexID : SV_VertexID) {
    VS_SkyOut output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    // Draw on far plane with Z = 1.0 (pos.z = pos.w = 1.0)
    output.pos = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);
    return output;
}

float4 PS_Sky(VS_SkyOut input) : SV_Target {
    float4 clip = float4(input.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);
    float4 worldH = mul(clip, InvViewProj);
    float3 worldPos = worldH.xyz / max(worldH.w, 0.00001f);
    float3 dir = normalize(worldPos - CameraPos);

    float up = saturate(dir.y);
    float k = pow(up, 0.45f);

    float3 horizon = float3(0.40f, 0.62f, 0.88f);
    float3 zenith = float3(0.08f, 0.26f, 0.72f);
    float3 c = (dir.y >= 0.0f)
        ? lerp(horizon, zenith, k)
        : lerp(horizon, float3(0.20f, 0.28f, 0.38f), saturate(-dir.y * 2.0f));

    // Sun disc + halo (from NewEra3D: cos^512 and cos^8 via repeated squaring)
    float cosA = max(dot(dir, normalize(-SunDir)), 0.0f);
    float c2 = cosA * cosA, c4 = c2 * c2, c8 = c4 * c4;
    float c16 = c8 * c8, c64 = c16 * c16 * c16 * c16, c256 = c64 * c64, c512 = c256 * c256;
    float glow = c512 * 12.0f + c8 * 0.35f;
    c = c + SunColor * glow;

    return float4(c, 1.0f);
}
)";

// Embedded HLSL Shadow Vertex Shader
static const char* g_shadow_shader_hlsl = R"(
cbuffer SceneConstants : register(b0) {
    row_major float4x4 ViewProj;
    row_major float4x4 LightViewProj;
    float3 CameraPos;
    float Time;
    float3 SunDir;
    float pad1;
    float3 SunColor;
    float pad2;
    float3 AmbientColor;
    float pad3;
    float3 FogColor;
    float FogStart;
    float FogEnd;
    float3 PadFog;
    row_major float4x4 InvViewProj;
    row_major float4x4 LightViewProjCascade1;
};

cbuffer BoneConstants : register(b1) {
    row_major float4x4 BoneMatrices[128];
};

struct VS_INPUT {
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    uint4 Joints : JOINTS;
    float4 Weights : WEIGHTS;
    float3 InstPos : INST_POS;
    float4 InstQuat : INST_QUAT;
    float3 InstScale : INST_SCALE;
    float2 InstParam : INST_PARAM;
};

float3 RotateVector(float3 v, float4 q) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

float4 VS_Shadow(VS_INPUT input) : SV_POSITION {
    float4x4 skinMat = BoneMatrices[input.Joints.x] * input.Weights.x
                     + BoneMatrices[input.Joints.y] * input.Weights.y
                     + BoneMatrices[input.Joints.z] * input.Weights.z
                     + BoneMatrices[input.Joints.w] * input.Weights.w;

    float3 skinnedPos = mul(float4(input.Pos, 1.0f), skinMat).xyz;
    float3 scaled = skinnedPos * input.InstScale;
    float3 rotated = RotateVector(scaled, input.InstQuat);
    float3 worldPos = rotated + input.InstPos;
    return mul(float4(worldPos, 1.0f), LightViewProj);
}
)";

// Embedded HLSL Post-Processing Shader Code
static const char* g_post_process_shader_hlsl = R"(
cbuffer PostConstants : register(b0) {
    float2 TexelSize;
    float BloomThreshold;
    float BloomIntensity;
    float Exposure;
    float3 PadPost;
};

Texture2D SceneTex : register(t0);
Texture2D BloomTex : register(t1);
SamplerState LinearSampler : register(s0);

struct VS_PostOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VS_PostOut VS_Fullscreen(uint vertexID : SV_VertexID) {
    VS_PostOut output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.pos = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float4 PS_ExtractBloom(VS_PostOut input) : SV_Target {
    float4 color = SceneTex.Sample(LinearSampler, input.uv);
    float brightness = dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    if (brightness > BloomThreshold) {
        return float4(color.rgb * ((brightness - BloomThreshold) / max(brightness, 0.0001f)), 1.0f);
    }
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}

float4 PS_BlurH(VS_PostOut input) : SV_Target {
    float weights[5] = { 0.227027f, 0.1945946f, 0.1216216f, 0.054054f, 0.016216f };
    float3 result = SceneTex.Sample(LinearSampler, input.uv).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        float2 offset = float2(TexelSize.x * (float)i * 1.5f, 0.0f);
        result += SceneTex.Sample(LinearSampler, input.uv + offset).rgb * weights[i];
        result += SceneTex.Sample(LinearSampler, input.uv - offset).rgb * weights[i];
    }
    return float4(result, 1.0f);
}

float4 PS_BlurV(VS_PostOut input) : SV_Target {
    float weights[5] = { 0.227027f, 0.1945946f, 0.1216216f, 0.054054f, 0.016216f };
    float3 result = SceneTex.Sample(LinearSampler, input.uv).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        float2 offset = float2(0.0f, TexelSize.y * (float)i * 1.5f);
        result += SceneTex.Sample(LinearSampler, input.uv + offset).rgb * weights[i];
        result += SceneTex.Sample(LinearSampler, input.uv - offset).rgb * weights[i];
    }
    return float4(result, 1.0f);
}

float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PS_Composite(VS_PostOut input) : SV_Target {
    float3 hdrColor = SceneTex.Sample(LinearSampler, input.uv).rgb;
    float3 bloomColor = BloomTex.Sample(LinearSampler, input.uv).rgb;
    float3 color = hdrColor + bloomColor * (BloomIntensity * 0.45f);
    color *= Exposure;

    // 1. ACES Filmic Tone Mapping (Cinema-grade dynamic range)
    float3 mapped = ACESFilm(color);

    // 2. High-Fidelity Color Grading & Vibrance Boost
    // Shadow detail protection: lift crushed toe values so environmental indirect light remains readable
    // ACES already compresses highlights into the display range.

    // Vibrance / Saturation Boost. Photographic albedo already carries real saturation,
    // so this is now a gentle grade instead of the +18% that produced neon grass.
    float luma = dot(mapped, float3(0.2126f, 0.7152f, 0.0722f));
    float satBoost = 1.05f;
    mapped = lerp(float3(luma, luma, luma), mapped, satBoost);

    // Warm Sun Highlights & a very light cool tint in the shadows
    float3 shadowTint = float3(1.01f, 1.02f, 1.05f);
    float3 highlightTint = float3(1.02f, 1.01f, 0.97f);
    mapped *= lerp(shadowTint, highlightTint, saturate(luma));

    // 3. Subtle Optical Vignette (Cinematic depth & framing)
    float2 uvCenter = input.uv - 0.5f;
    float vignette = 1.0f - dot(uvCenter, uvCenter) * 0.45f;
    mapped *= saturate(vignette);

    // 4. Single Accurate sRGB Gamma Correction
    mapped = pow(saturate(mapped), float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));

    return float4(mapped, 1.0f);
}
)";

RenderWorld::RenderWorld() = default;

RenderWorld::~RenderWorld() {
    shutdown();
}

bool RenderWorld::initialize(const RenderConfig& config) {
    if (m_initialized) {
        return true;
    }

    m_config = config;

    if (!init_device_and_swapchain()) {
        return false;
    }

    if (!init_shaders()) {
        return false;
    }

    if (!init_shadow_resources()) {
        return false;
    }

    if (!init_instance_buffer()) {
        return false;
    }

    if (!m_mesh_manager.initialize(m_device.Get())) {
        return false;
    }

    if (!m_texture_manager.init(m_device.Get(), m_context.Get())) {
        return false;
    }

    if (!m_particle_manager.initialize(m_device.Get())) {
        return false;
    }

    if (!m_grass_system.initialize(m_device.Get())) {
        std::cerr << "Warning: Failed to initialize GrassSystem" << std::endl;
    }

    if (!init_post_processing_resources()) {
        std::cerr << "Warning: Failed to initialize HDR post-processing resources" << std::endl;
    }

    m_initialized = true;
    return true;
}

void RenderWorld::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_grass_system.shutdown();
    m_particle_manager.shutdown();
    m_texture_manager.cleanup();
    m_mesh_manager.shutdown();

    m_post_cb.Reset();
    m_linear_clamp_sampler.Reset();
    m_composite_ps.Reset();
    m_blur_v_ps.Reset();
    m_blur_h_ps.Reset();
    m_bloom_extract_ps.Reset();
    m_post_vs.Reset();

    for (int i = 0; i < 2; ++i) {
        m_bloom_srv[i].Reset();
        m_bloom_rtv[i].Reset();
        m_bloom_tex[i].Reset();
    }
    m_hdr_srv.Reset();
    m_hdr_rtv.Reset();
    m_hdr_texture.Reset();

    m_shadow_vertex_shader.Reset();
    m_shadow_depth_stencil_state.Reset();
    m_shadow_rasterizer_state.Reset();
    m_shadow_sampler.Reset();
    m_shadow_srv.Reset();
    m_shadow_dsv.Reset();
    m_shadow_texture.Reset();

    m_instance_buffer.Reset();
    m_constant_buffer.Reset();
    m_bone_constant_buffer.Reset();
    m_light_constant_buffer.Reset();
    m_entity_anims.clear();
    m_input_layout.Reset();
    m_pixel_shader.Reset();
    m_vertex_shader.Reset();
    m_rasterizer_state.Reset();
    m_depth_stencil_state.Reset();
    m_gizmo_depth_stencil_state.Reset();
    m_depth_stencil_view.Reset();
    m_depth_stencil_buffer.Reset();
    m_render_target_view.Reset();
    m_swap_chain.Reset();
    m_context.Reset();
    m_device.Reset();

    m_initialized = false;
}

bool RenderWorld::init_device_and_swapchain() {
    HWND hwnd = static_cast<HWND>(m_config.native_window_handle);
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }

    RECT rc;
    GetClientRect(hwnd, &rc);
    UINT width = std::max(64u, static_cast<UINT>(rc.right - rc.left));
    UINT height = std::max(64u, static_cast<UINT>(rc.bottom - rc.top));
    m_config.width = width;
    m_config.height = height;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    if (m_config.debug_validation) {
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1
    };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        m_device.GetAddressOf(),
        &featureLevel,
        m_context.GetAddressOf()
    );
    if (FAILED(hr)) {
        return false;
    }

    // Which adapter actually renders matters on hybrid laptops: the editor used to spend
    // 32 ms inside Present because the frame was copied between GPUs.
    ComPtr<IDXGIFactory2> factory2;
    ComPtr<IDXGIFactory>  factory1;
    {
        ComPtr<IDXGIDevice> dxgi_device;
        ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(m_device.As(&dxgi_device)) &&
            SUCCEEDED(dxgi_device->GetAdapter(adapter.GetAddressOf()))) {
            DXGI_ADAPTER_DESC adapter_desc{};
            if (SUCCEEDED(adapter->GetDesc(&adapter_desc))) {
                char adapter_name[160] = {};
                WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description, -1, adapter_name,
                                    sizeof(adapter_name), nullptr, nullptr);
                std::cout << "[RenderWorld] Adapter: " << adapter_name << " ("
                          << (adapter_desc.DedicatedVideoMemory / (1024 * 1024)) << " MB)\n";
                m_adapter_name = adapter_name;
            }
            adapter->GetParent(IID_PPV_ARGS(factory2.GetAddressOf()));
            adapter->GetParent(IID_PPV_ARGS(factory1.GetAddressOf()));
        }
    }

    // Flip-model swap chain (Windows 10+). The legacy bitblt model makes DWM copy the whole
    // back buffer on every Present, which on hybrid graphics went as far as a cross-adapter
    // copy: ~32 ms per frame, i.e. the "30 FPS with two objects" report. Flip model presents
    // by handle swap and measured 3.2 ms on the same machine even before the GPU routing fix.
    if (factory2) {
        DXGI_SWAP_CHAIN_DESC1 scd1{};
        scd1.Width = width;
        scd1.Height = height;
        scd1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd1.SampleDesc.Count = 1;
        scd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd1.BufferCount = 2;
        scd1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd1.Scaling = DXGI_SCALING_STRETCH;
        scd1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        scd1.Flags = 0;

        factory2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        ComPtr<IDXGISwapChain1> swap_chain1;
        if (SUCCEEDED(factory2->CreateSwapChainForHwnd(m_device.Get(), hwnd, &scd1, nullptr, nullptr,
                                                       swap_chain1.GetAddressOf()))) {
            hr = swap_chain1.As(&m_swap_chain);
        } else {
            hr = E_FAIL;
        }
    } else {
        hr = E_FAIL;
    }

    // Fallback for pre-Windows-10 hosts: the classic bitblt chain still works, just slower.
    if (FAILED(hr) && factory1) {
        DXGI_SWAP_CHAIN_DESC scd{};
        scd.BufferCount = 2;
        scd.BufferDesc.Width = width;
        scd.BufferDesc.Height = height;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferDesc.RefreshRate.Numerator = 60;
        scd.BufferDesc.RefreshRate.Denominator = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hwnd;
        scd.SampleDesc.Count = 1;
        scd.Windowed = TRUE;
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = factory1->CreateSwapChain(m_device.Get(), &scd, m_swap_chain.GetAddressOf());
    }

    if (FAILED(hr) || !m_swap_chain) {
        return false;
    }

    // Create BackBuffer Render Target
    ComPtr<ID3D11Texture2D> back_buffer;
    hr = m_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back_buffer.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(back_buffer.Get(), nullptr, m_render_target_view.GetAddressOf());
    if (FAILED(hr)) return false;

    // Create Depth Stencil Buffer
    D3D11_TEXTURE2D_DESC dbd{};
    dbd.Width = width;
    dbd.Height = height;
    dbd.MipLevels = 1;
    dbd.ArraySize = 1;
    dbd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dbd.SampleDesc.Count = 1;
    dbd.Usage = D3D11_USAGE_DEFAULT;
    dbd.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = m_device->CreateTexture2D(&dbd, nullptr, m_depth_stencil_buffer.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = m_device->CreateDepthStencilView(m_depth_stencil_buffer.Get(), nullptr, m_depth_stencil_view.GetAddressOf());
    if (FAILED(hr)) return false;

    // Depth Stencil State
    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

    hr = m_device->CreateDepthStencilState(&dsd, m_depth_stencil_state.GetAddressOf());
    if (FAILED(hr)) return false;

    // Gizmo Depth Stencil State (Depth test disabled so 3D Gizmo is ALWAYS drawn on top)
    D3D11_DEPTH_STENCIL_DESC gizmoDsd{};
    gizmoDsd.DepthEnable = FALSE;
    gizmoDsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    gizmoDsd.DepthFunc = D3D11_COMPARISON_ALWAYS;

    hr = m_device->CreateDepthStencilState(&gizmoDsd, m_gizmo_depth_stencil_state.GetAddressOf());
    if (FAILED(hr)) return false;

    // Main Rasterizer State (CULL_NONE ensures two-sided visibility, no hollow holes)
    D3D11_RASTERIZER_DESC rd{};
    rd.CullMode = D3D11_CULL_NONE;
    rd.FillMode = D3D11_FILL_SOLID;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;

    hr = m_device->CreateRasterizerState(&rd, m_rasterizer_state.GetAddressOf());
    if (FAILED(hr)) return false;

    return true;
}

bool RenderWorld::init_shaders() {
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(
        g_pbr_shader_hlsl, strlen(g_pbr_shader_hlsl), "PBR_Shader",
        nullptr, nullptr, "VS_Main", "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, 0,
        vsBlob.GetAddressOf(), errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "VS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertex_shader.GetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<ID3DBlob> psBlob;
    errorBlob.Reset();
    hr = D3DCompile(
        g_pbr_shader_hlsl, strlen(g_pbr_shader_hlsl), "PBR_Shader",
        nullptr, nullptr, "PS_Main", "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, 0,
        psBlob.GetAddressOf(), errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "PS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixel_shader.GetAddressOf());
    if (FAILED(hr)) return false;

    // Shadow Pass Vertex Shader
    ComPtr<ID3DBlob> shadowBlob;
    errorBlob.Reset();
    hr = D3DCompile(
        g_shadow_shader_hlsl, strlen(g_shadow_shader_hlsl), "Shadow_Shader",
        nullptr, nullptr, "VS_Shadow", "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, 0,
        shadowBlob.GetAddressOf(), errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Shadow VS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    hr = m_device->CreateVertexShader(shadowBlob->GetBufferPointer(), shadowBlob->GetBufferSize(), nullptr, m_shadow_vertex_shader.GetAddressOf());
    if (FAILED(hr)) return false;

    // Input Layout: Slot 0 (Per-Vertex), Slot 1 (Per-Instance)
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA,   0},
        {"NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA,   0},
        {"TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA,   0},
        {"JOINTS",     0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 32, D3D11_INPUT_PER_VERTEX_DATA,   0},
        {"WEIGHTS",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA,   0},
        {"INST_POS",   0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_QUAT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_SCALE", 0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 28, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_PARAM", 0, DXGI_FORMAT_R32G32_FLOAT,       1, 40, D3D11_INPUT_PER_INSTANCE_DATA, 1}
    };

    hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_input_layout.GetAddressOf());
    if (FAILED(hr)) return false;

    // Scene Constant Buffer (b0)
    D3D11_BUFFER_DESC cbd{};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(SceneConstantBuffer);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = m_device->CreateBuffer(&cbd, nullptr, m_constant_buffer.GetAddressOf());
    if (FAILED(hr)) return false;

    // Bone Constant Buffer (b1) - holds up to 128 4x4 row-major matrices
    D3D11_BUFFER_DESC bcbd{};
    bcbd.Usage = D3D11_USAGE_DYNAMIC;
    bcbd.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * 128; // 8192 bytes
    bcbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bcbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    std::vector<DirectX::XMFLOAT4X4> default_bones(128);
    for (int i = 0; i < 128; ++i) {
        DirectX::XMStoreFloat4x4(&default_bones[i], DirectX::XMMatrixIdentity());
    }
    D3D11_SUBRESOURCE_DATA bData{};
    bData.pSysMem = default_bones.data();

    hr = m_device->CreateBuffer(&bcbd, &bData, m_bone_constant_buffer.GetAddressOf());
    if (FAILED(hr)) return false;

    // Light Constant Buffer (b2) - holds up to 16 point lights
    D3D11_BUFFER_DESC lcbd{};
    lcbd.Usage = D3D11_USAGE_DYNAMIC;
    lcbd.ByteWidth = sizeof(GPULightConstants);
    lcbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    lcbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA lData{};
    lData.pSysMem = &m_light_constants;

    hr = m_device->CreateBuffer(&lcbd, &lData, m_light_constant_buffer.GetAddressOf());
    if (FAILED(hr)) return false;

    // Material Constant Buffer (b3)
    D3D11_BUFFER_DESC mcbd{};
    mcbd.Usage = D3D11_USAGE_DYNAMIC;
    mcbd.ByteWidth = sizeof(MaterialGPU);
    mcbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    mcbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    MaterialGPU default_mat{};
    D3D11_SUBRESOURCE_DATA mData{};
    mData.pSysMem = &default_mat;

    hr = m_device->CreateBuffer(&mcbd, &mData, m_material_constant_buffer.GetAddressOf());
    if (FAILED(hr)) return false;

    // Alpha Blend State (for alpha_mode == 2 / transparency)
    D3D11_BLEND_DESC bdesc{};
    bdesc.RenderTarget[0].BlendEnable = TRUE;
    bdesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bdesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bdesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_device->CreateBlendState(&bdesc, m_alpha_blend_state.GetAddressOf());
    if (FAILED(hr)) return false;

    // Procedural Sky Dome Shaders & Pipeline States
    ComPtr<ID3DBlob> skyVsBlob;
    errorBlob.Reset();
    hr = D3DCompile(
        g_sky_shader_hlsl, strlen(g_sky_shader_hlsl), "Sky_Shader",
        nullptr, nullptr, "VS_Sky", "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, 0,
        skyVsBlob.GetAddressOf(), errorBlob.GetAddressOf()
    );
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Sky VS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }
    hr = m_device->CreateVertexShader(skyVsBlob->GetBufferPointer(), skyVsBlob->GetBufferSize(), nullptr, m_sky_vertex_shader.GetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<ID3DBlob> skyPsBlob;
    errorBlob.Reset();
    hr = D3DCompile(
        g_sky_shader_hlsl, strlen(g_sky_shader_hlsl), "Sky_Shader",
        nullptr, nullptr, "PS_Sky", "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, 0,
        skyPsBlob.GetAddressOf(), errorBlob.GetAddressOf()
    );
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Sky PS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }
    hr = m_device->CreatePixelShader(skyPsBlob->GetBufferPointer(), skyPsBlob->GetBufferSize(), nullptr, m_sky_pixel_shader.GetAddressOf());
    if (FAILED(hr)) return false;

    // Sky Depth Stencil State (Depth test LESS_EQUAL, depth write ZERO)
    D3D11_DEPTH_STENCIL_DESC skyDsd{};
    skyDsd.DepthEnable = TRUE;
    skyDsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    skyDsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = m_device->CreateDepthStencilState(&skyDsd, m_sky_depth_stencil_state.GetAddressOf());
    if (FAILED(hr)) return false;

    // Sky Rasterizer State (CULL_NONE so sphere dome is visible from inside)
    D3D11_RASTERIZER_DESC skyRd{};
    skyRd.FillMode = D3D11_FILL_SOLID;
    skyRd.CullMode = D3D11_CULL_NONE;
    skyRd.FrontCounterClockwise = FALSE;
    skyRd.DepthClipEnable = TRUE;
    hr = m_device->CreateRasterizerState(&skyRd, m_sky_rasterizer_state.GetAddressOf());
    if (FAILED(hr)) return false;

    return true;
}

bool RenderWorld::create_shadow_atlas(UINT atlas_size) {
    atlas_size = std::clamp(atlas_size, 1024u, 4096u);
    m_shadow_dsv.Reset();
    m_shadow_srv.Reset();
    m_shadow_texture.Reset();

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = atlas_size;
    texDesc.Height = atlas_size;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, m_shadow_texture.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = m_device->CreateDepthStencilView(m_shadow_texture.Get(), &dsvDesc, m_shadow_dsv.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = m_device->CreateShaderResourceView(m_shadow_texture.Get(), &srvDesc, m_shadow_srv.GetAddressOf());
    if (FAILED(hr)) return false;

    m_shadow_atlas_size = atlas_size;
    return true;
}

bool RenderWorld::init_shadow_resources() {
    // 1-3. Create Shadow Atlas Texture, DSV, and SRV
    if (!create_shadow_atlas(m_shadow_atlas_size)) return false;

    // 4. Create Shadow Comparison Sampler (Linear filtering with comparison)
    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sampDesc.BorderColor[0] = 1.0f;
    sampDesc.BorderColor[1] = 1.0f;
    sampDesc.BorderColor[2] = 1.0f;
    sampDesc.BorderColor[3] = 1.0f;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sampDesc.MinLOD = 0.0f;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = m_device->CreateSamplerState(&sampDesc, m_shadow_sampler.GetAddressOf());
    if (FAILED(hr)) return false;

    // 5. Shadow Rasterizer State (CULL_NONE for shadow casting from all surfaces)
    D3D11_RASTERIZER_DESC shadowRd{};
    shadowRd.CullMode = D3D11_CULL_NONE;
    shadowRd.FillMode = D3D11_FILL_SOLID;
    shadowRd.FrontCounterClockwise = FALSE;
    shadowRd.DepthBias = 50;
    shadowRd.SlopeScaledDepthBias = 1.5f;
    shadowRd.DepthBiasClamp = 0.0f;
    shadowRd.DepthClipEnable = TRUE;

    hr = m_device->CreateRasterizerState(&shadowRd, m_shadow_rasterizer_state.GetAddressOf());
    if (FAILED(hr)) return false;

    // 6. Shadow Depth Stencil State
    D3D11_DEPTH_STENCIL_DESC shadowDsd{};
    shadowDsd.DepthEnable = TRUE;
    shadowDsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    shadowDsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

    hr = m_device->CreateDepthStencilState(&shadowDsd, m_shadow_depth_stencil_state.GetAddressOf());
    if (FAILED(hr)) return false;

    return true;
}

bool RenderWorld::init_instance_buffer() {
    D3D11_BUFFER_DESC ibd{};
    ibd.Usage = D3D11_USAGE_DYNAMIC;
    ibd.ByteWidth = static_cast<UINT>(sizeof(InstanceDataGPU) * kInstanceArenaCapacity);
    ibd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    ibd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->CreateBuffer(&ibd, nullptr, m_instance_buffer.GetAddressOf());
    return SUCCEEDED(hr);
}

InstanceDataGPU* RenderWorld::begin_instance_write(uint32_t count, uint32_t& out_offset) {
    if (!m_instance_buffer || count == 0) return nullptr;
    if (count > kInstanceArenaCapacity) return nullptr;

    if (m_instance_cursor + count > kInstanceArenaCapacity) {
        m_instance_cursor = 0;
        m_instance_discard_pending = true;
    }

    const D3D11_MAP mode = m_instance_discard_pending ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_instance_buffer.Get(), 0, mode, 0, &mapped))) return nullptr;
    m_instance_discard_pending = false;

    out_offset = m_instance_cursor;
    m_instance_cursor += count;
    return static_cast<InstanceDataGPU*>(mapped.pData) + out_offset;
}

void RenderWorld::end_instance_write() {
    m_context->Unmap(m_instance_buffer.Get(), 0);
}

void RenderWorld::resize(uint32_t width, uint32_t height) {
    if (!m_initialized || width == 0 || height == 0) return;
    if (width == m_config.width && height == m_config.height) return;

    m_config.width = width;
    m_config.height = height;

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_render_target_view.Reset();
    m_depth_stencil_view.Reset();
    m_depth_stencil_buffer.Reset();

    m_swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    ComPtr<ID3D11Texture2D> back_buffer;
    m_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back_buffer.GetAddressOf()));
    m_device->CreateRenderTargetView(back_buffer.Get(), nullptr, m_render_target_view.GetAddressOf());

    D3D11_TEXTURE2D_DESC dbd{};
    dbd.Width = width;
    dbd.Height = height;
    dbd.MipLevels = 1;
    dbd.ArraySize = 1;
    dbd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dbd.SampleDesc.Count = 1;
    dbd.Usage = D3D11_USAGE_DEFAULT;
    dbd.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    m_device->CreateTexture2D(&dbd, nullptr, m_depth_stencil_buffer.GetAddressOf());
    m_device->CreateDepthStencilView(m_depth_stencil_buffer.Get(), nullptr, m_depth_stencil_view.GetAddressOf());

    create_post_textures(width, height);
}

static void MatrixMultiply(float* out, const float* a, const float* b) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r * 4 + c] =
                a[r * 4 + 0] * b[0 * 4 + c] +
                a[r * 4 + 1] * b[1 * 4 + c] +
                a[r * 4 + 2] * b[2 * 4 + c] +
                a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

static void ComputeLookAtLH(float* out, float ex, float ey, float ez, float tx, float ty, float tz, float upx, float upy, float upz) {
    float zx = tx - ex, zy = ty - ey, zz = tz - ez;
    float lenZ = sqrtf(zx * zx + zy * zy + zz * zz);
    if (lenZ > 0.0001f) { zx /= lenZ; zy /= lenZ; zz /= lenZ; } else { zz = 1.0f; }

    float xx = upy * zz - upz * zy;
    float xy = upz * zx - upx * zz;
    float xz = upx * zy - upy * zx;
    float lenX = sqrtf(xx * xx + xy * xy + xz * xz);
    if (lenX < 0.0001f) {
        upx = 0.0f; upy = 0.0f; upz = 1.0f;
        xx = upy * zz - upz * zy;
        xy = upz * zx - upx * zz;
        xz = upx * zy - upy * zx;
        lenX = sqrtf(xx * xx + xy * xy + xz * xz);
    }
    if (lenX > 0.0001f) { xx /= lenX; xy /= lenX; xz /= lenX; } else { xx = 1.0f; }

    float yx = zy * xz - zz * xy;
    float yy = zz * xx - zx * xz;
    float yz = zx * xy - zy * xx;

    float view[16] = {
        xx, yx, zx, 0.0f,
        xy, yy, zy, 0.0f,
        xz, yz, zz, 0.0f,
        -(xx * ex + xy * ey + xz * ez),
        -(yx * ex + yy * ey + yz * ez),
        -(zx * ex + zy * ey + zz * ez),
        1.0f
    };
    memcpy(out, view, sizeof(view));
}

static void ComputeOrthoLH(float* out, float w, float h, float nearZ, float farZ) {
    float ortho[16] = {
        2.0f / w, 0.0f,     0.0f,                       0.0f,
        0.0f,     2.0f / h, 0.0f,                       0.0f,
        0.0f,     0.0f,     1.0f / (farZ - nearZ),      0.0f,
        0.0f,     0.0f,     -nearZ / (farZ - nearZ),    1.0f
    };
    memcpy(out, ortho, sizeof(ortho));
}

void RenderWorld::update_view_proj(const SharedBufferHeader& header) {
    // 1. Camera LookAtLH Matrix
    float ex = header.camera_pos[0], ey = header.camera_pos[1], ez = header.camera_pos[2];
    float tx = header.camera_target[0], ty = header.camera_target[1], tz = header.camera_target[2];

    float view[16];
    ComputeLookAtLH(view, ex, ey, ez, tx, ty, tz, 0.0f, 1.0f, 0.0f);

    // 2. PerspectiveFovLH Matrix
    float fovRad = (header.camera_fov > 0.0f ? header.camera_fov : 60.0f) * (float)M_PI / 180.0f;
    float aspect = (m_config.height > 0) ? (float)m_config.width / (float)m_config.height : 16.0f / 9.0f;
    if (m_custom_render_target) {
        ComPtr<ID3D11Resource> res;
        m_custom_render_target->GetResource(res.GetAddressOf());
        ComPtr<ID3D11Texture2D> tex;
        if (SUCCEEDED(res.As(&tex))) {
            D3D11_TEXTURE2D_DESC td;
            tex->GetDesc(&td);
            if (td.Height > 0) {
                aspect = (float)td.Width / (float)td.Height;
            }
        }
    }
    float nearZ = header.camera_near > 0.0f ? header.camera_near : 0.1f;
    float farZ = header.camera_far > nearZ ? header.camera_far : 1000.0f;

    float h = 1.0f / tanf(fovRad * 0.5f);
    float w = h / aspect;
    float q = farZ / (farZ - nearZ);

    float proj[16] = {
        w,    0.0f, 0.0f,        0.0f,
        0.0f, h,    0.0f,        0.0f,
        0.0f, 0.0f, q,           1.0f,
        0.0f, 0.0f, -q * nearZ,  0.0f
    };

    SceneConstantBuffer cb{};
    MatrixMultiply(cb.view_proj, view, proj);

    // Invert View-Projection for procedural fullscreen sky ray reconstruction
    DirectX::XMMATRIX vpMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(cb.view_proj));
    DirectX::XMVECTOR det;
    DirectX::XMMATRIX invVp = DirectX::XMMatrixInverse(&det, vpMat);
    DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(cb.inv_view_proj), invVp);

    cb.camera_pos[0] = ex;
    cb.camera_pos[1] = ey;
    cb.camera_pos[2] = ez;
    cb.time = m_total_time;

    // Directional Sun Light
    float lenSun = sqrtf(m_sun_dir[0]*m_sun_dir[0] + m_sun_dir[1]*m_sun_dir[1] + m_sun_dir[2]*m_sun_dir[2]);
    float sunDir[3] = { 0.0f, -1.0f, 0.0f };
    if (lenSun > 0.0001f) {
        sunDir[0] = m_sun_dir[0] / lenSun;
        sunDir[1] = m_sun_dir[1] / lenSun;
        sunDir[2] = m_sun_dir[2] / lenSun;
    }

    cb.pad1 = m_shadows_enabled ? m_shadow_strength : 0.0f;
    cb.pad2 = m_shadow_softness;
    cb.sun_dir[0] = sunDir[0];
    cb.sun_dir[1] = sunDir[1];
    cb.sun_dir[2] = sunDir[2];
    cb.sun_color[0] = m_sun_color[0] * m_sun_intensity;
    cb.sun_color[1] = m_sun_color[1] * m_sun_intensity;
    cb.sun_color[2] = m_sun_color[2] * m_sun_intensity;

    // Ambient light
    cb.ambient_color[0] = m_ambient_color[0] * m_ambient_intensity;
    cb.ambient_color[1] = m_ambient_color[1] * m_ambient_intensity;
    cb.ambient_color[2] = m_ambient_color[2] * m_ambient_intensity;
    cb.pad3 = static_cast<float>(m_shadow_atlas_size);

    // Atmospheric Distance Fog
    cb.fog_color[0] = m_sky_color[0];
    cb.fog_color[1] = m_sky_color[1];
    cb.fog_color[2] = m_sky_color[2];
    cb.fog_start = m_fog_start;
    cb.fog_end = m_fog_end;
    cb.pad_fog[0] = m_csm_enabled ? 1.0f : 0.0f;

    // Light View-Projection Matrices with Texel Snapping (Cascade 0 Near & Cascade 1 Far)
    float focusX = header.camera_target[0];
    float focusY = header.camera_target[1];
    float focusZ = header.camera_target[2];

    const float halfAtlas = static_cast<float>(std::max<UINT>(512u, m_shadow_atlas_size / 2));

    // Cascade 0 (Near)
    const float orthoSize0 = m_shadow_coverage;
    const float lightDist0 = 130.0f;
    float lightEye0X = focusX - sunDir[0] * lightDist0;
    float lightEye0Y = focusY - sunDir[1] * lightDist0;
    float lightEye0Z = focusZ - sunDir[2] * lightDist0;

    float lightView0[16];
    ComputeLookAtLH(lightView0, lightEye0X, lightEye0Y, lightEye0Z, focusX, focusY, focusZ, 0.0f, 1.0f, 0.0f);

    const float worldTexelSize0 = orthoSize0 / halfAtlas;
    lightView0[12] = std::floor(lightView0[12] / worldTexelSize0) * worldTexelSize0;
    lightView0[13] = std::floor(lightView0[13] / worldTexelSize0) * worldTexelSize0;

    float lightProj0[16];
    ComputeOrthoLH(lightProj0, orthoSize0, orthoSize0, 1.0f, 270.0f);
    MatrixMultiply(cb.light_view_proj, lightView0, lightProj0);

    // Cascade 1 (Far)
    const float orthoSize1 = m_shadow_coverage * 3.2f;
    const float lightDist1 = 220.0f;
    float lightEye1X = focusX - sunDir[0] * lightDist1;
    float lightEye1Y = focusY - sunDir[1] * lightDist1;
    float lightEye1Z = focusZ - sunDir[2] * lightDist1;

    float lightView1[16];
    ComputeLookAtLH(lightView1, lightEye1X, lightEye1Y, lightEye1Z, focusX, focusY, focusZ, 0.0f, 1.0f, 0.0f);

    const float worldTexelSize1 = orthoSize1 / halfAtlas;
    lightView1[12] = std::floor(lightView1[12] / worldTexelSize1) * worldTexelSize1;
    lightView1[13] = std::floor(lightView1[13] / worldTexelSize1) * worldTexelSize1;

    float lightProj1[16];
    ComputeOrthoLH(lightProj1, orthoSize1, orthoSize1, 1.0f, 500.0f);
    MatrixMultiply(cb.light_view_proj_c1, lightView1, lightProj1);

    m_cached_scene_cb = cb;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(m_context->Map(m_constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &cb, sizeof(SceneConstantBuffer));
        m_context->Unmap(m_constant_buffer.Get(), 0);
    }
}


void RenderWorld::render_frame(const SharedBufferHeader& header, const SharedEntityData* entities, uint32_t count, float delta_time) {
    if (!m_initialized || !m_swap_chain) return;
    m_instance_cursor = 0;
    m_instance_discard_pending = true;
    m_total_time += delta_time;
    m_particle_manager.update(delta_time);

    // Check if window size changed
    HWND hwnd = static_cast<HWND>(m_config.native_window_handle);
    RECT rc;
    if (GetClientRect(hwnd, &rc)) {
        UINT curW = rc.right - rc.left;
        UINT curH = rc.bottom - rc.top;
        if (curW > 0 && curH > 0 && (curW != m_config.width || curH != m_config.height)) {
            resize(curW, curH);
        }
    }

    // 1. Advance animations
    for (auto& [ent_id, state] : m_entity_anims) {
        if (state.is_playing) {
            state.current_time += delta_time * state.speed;
        }
    }

    // 2. Update Camera & Scene Lighting Constants
    update_view_proj(header);

    // 3. Group instances by (Mesh ID, Material ID, Texture ID)
    struct RenderItem {
        InstanceDataGPU inst;
        uint32_t entity_id;
        float bounds_min[3];  // world-space AABB, used by every culling test below
        float bounds_max[3];
        float depth;          // squared distance to the camera, used to sort blended batches
    };
    struct BatchKey {
        uint16_t mesh_id;
        uint16_t material_id;
        uint16_t texture_id;
        bool operator==(const BatchKey& o) const {
            return mesh_id == o.mesh_id && material_id == o.material_id && texture_id == o.texture_id;
        }
    };
    struct BatchKeyHash {
        size_t operator()(const BatchKey& k) const {
            return (static_cast<size_t>(k.mesh_id) << 32) ^ (static_cast<size_t>(k.material_id) << 16) ^ k.texture_id;
        }
    };

    std::unordered_map<BatchKey, std::vector<RenderItem>, BatchKeyHash> batches;
    std::unordered_map<uint16_t, std::vector<RenderItem>> shadow_batches;

    if (entities != nullptr && count > 0) {
        const GM3D::FrustumPlanes camera_frustum = GM3D::extract_frustum_planes(m_cached_scene_cb.view_proj);
        const float cam_x = m_cached_scene_cb.camera_pos[0];
        const float cam_y = m_cached_scene_cb.camera_pos[1];
        const float cam_z = m_cached_scene_cb.camera_pos[2];

        for (uint32_t i = 0; i < count; ++i) {
            const auto& e = entities[i];
            if (!(e.flags & EntityFlags::ACTIVE)) continue;

            const GPUMesh* mesh = m_mesh_manager.get_mesh(e.mesh_id);
            if (!mesh) continue;

            RenderItem item{};
            item.entity_id = e.entity_id;
            item.inst.px = e.px; item.inst.py = e.py; item.inst.pz = e.pz;
            item.inst.qx = e.qx; item.inst.qy = e.qy; item.inst.qz = e.qz; item.inst.qw = e.qw;
            item.inst.sx = e.sx; item.inst.sy = e.sy; item.inst.sz = e.sz;
            item.inst.param1 = e.custom_param1;
            item.inst.param2 = e.custom_param2;

            const float position[3] = { e.px, e.py, e.pz };
            const float rotation[4] = { e.qx, e.qy, e.qz, e.qw };
            const float scale[3] = { e.sx, e.sy, e.sz };
            GM3D::transform_aabb(position, rotation, scale,
                           mesh->bounds_min, mesh->bounds_max,
                           item.bounds_min, item.bounds_max);

            const float cx = (item.bounds_min[0] + item.bounds_max[0]) * 0.5f - cam_x;
            const float cy = (item.bounds_min[1] + item.bounds_max[1]) * 0.5f - cam_y;
            const float cz = (item.bounds_min[2] + item.bounds_max[2]) * 0.5f - cam_z;
            item.depth = cx * cx + cy * cy + cz * cz;

            const uint16_t tex_id = static_cast<uint16_t>(e.custom_param2);
            if (GM3D::aabb_inside_frustum(camera_frustum, item.bounds_min, item.bounds_max)) {
                BatchKey key{ e.mesh_id, e.material_id, tex_id };
                batches[key].push_back(item);
            }

            // Shadow casters are culled per light view later, not against the camera.
            if (e.flags & EntityFlags::CAST_SHADOW) {
                shadow_batches[e.mesh_id].push_back(item);
            }
        }
    }

    std::unordered_map<uint32_t, std::vector<DirectX::XMFLOAT4X4>> pose_cache;
    bool bones_hold_identity = false;

    // Upload an identity pose once; skinned draws invalidate it.
    auto upload_identity_bones = [&]() {
        D3D11_MAPPED_SUBRESOURCE mappedBones{};
        if (SUCCEEDED(m_context->Map(m_bone_constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedBones))) {
            static std::vector<DirectX::XMFLOAT4X4> s_default_bones;
            if (s_default_bones.empty()) {
                s_default_bones.resize(128);
                for (int b = 0; b < 128; ++b) {
                    DirectX::XMStoreFloat4x4(&s_default_bones[b], DirectX::XMMatrixIdentity());
                }
            }
            memcpy(mappedBones.pData, s_default_bones.data(), sizeof(DirectX::XMFLOAT4X4) * 128);
            m_context->Unmap(m_bone_constant_buffer.Get(), 0);
        }
        bones_hold_identity = true;
    };

    // Returns the cached pose for an entity, evaluating it at most once per frame.
    auto get_pose = [&](const GM3D::SkinnedModelData* skinned_model, uint32_t entity_id) -> const std::vector<DirectX::XMFLOAT4X4>* {
        auto pose_it = pose_cache.find(entity_id);
        if (pose_it != pose_cache.end()) return &pose_it->second;

        std::vector<DirectX::XMFLOAT4X4> bone_matrices;
        const GM3D::AnimationClip* clip = nullptr;
        float anim_time = 0.0f;
        bool anim_loop = true;

        auto anim_it = m_entity_anims.find(entity_id);
        if (anim_it != m_entity_anims.end() && anim_it->second.anim_index >= 0) {
            clip = skinned_model->get_clip(static_cast<uint32_t>(anim_it->second.anim_index));
            anim_time = anim_it->second.current_time;
            anim_loop = anim_it->second.loop;
        }

        skinned_model->skeleton.evaluate_pose(clip, anim_time, bone_matrices, anim_loop);
        pose_it = pose_cache.emplace(entity_id, std::move(bone_matrices)).first;
        return &pose_it->second;
    };

    auto upload_bones = [&](const GM3D::SkinnedModelData* skinned_model, uint32_t entity_id) {
        const std::vector<DirectX::XMFLOAT4X4>* bones = get_pose(skinned_model, entity_id);
        D3D11_MAPPED_SUBRESOURCE mappedBones{};
        if (SUCCEEDED(m_context->Map(m_bone_constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedBones))) {
            const size_t copy_bones = std::min(bones->size(), static_cast<size_t>(128));
            memcpy(mappedBones.pData, bones->data(), sizeof(DirectX::XMFLOAT4X4) * copy_bones);
            m_context->Unmap(m_bone_constant_buffer.Get(), 0);
        }
        bones_hold_identity = false;
    };

    auto draw_batch_items = [&](const std::vector<RenderItem>& items, uint16_t mesh_id, uint16_t material_id,
                               uint16_t texture_id, bool depth_only = false) {
        if (items.empty()) return;
        const GPUMesh* mesh = m_mesh_manager.get_mesh(mesh_id);
        if (!mesh) return;

        MaterialGPU matProps{};
        uint16_t albedo_id = texture_id;
        uint16_t normal_id = 0;

        if (!depth_only) {
        auto it = m_materials.find(material_id);
        if (it != m_materials.end()) {
            matProps = it->second;
            auto texIt = m_material_textures.find(material_id);
            if (texIt != m_material_textures.end()) {
                if (albedo_id == 0) albedo_id = texIt->second.first;
                normal_id = texIt->second.second;
            }
        } else {
            if (material_id >= 1 && material_id <= 8) {
                matProps.preset_id = static_cast<int32_t>(material_id);
            } else if (material_id >= 100) {
                matProps.preset_id = static_cast<int32_t>(material_id);
            } else {
                matProps.preset_id = 0;
            }
        }

        // Terrain: enable the world-space detail blend only when the splat map exists and
        // all four CC0 sets were loaded. Otherwise the shader falls back to the old
        // procedural grain, so a build without assets/ still looks intentional.
        if (albedo_id == GM3D::TexId::TERRAIN && m_terrain_system.has_detail_splat() &&
            m_texture_manager.has_texture(GM3D::TexId::GRASS) && m_texture_manager.has_texture(GM3D::TexId::ROCK) &&
            m_texture_manager.has_texture(GM3D::TexId::SAND) && m_texture_manager.has_texture(GM3D::TexId::COBBLE)) {
            matProps.pad[0] = 0.40f; // ~2.5 m per detail tile
        }

        // Presets get real photographs for their dominant surface.
        if (albedo_id == 0) {
            if (matProps.preset_id == 8 && m_texture_manager.has_texture(GM3D::TexId::ROCK)) {
                albedo_id = GM3D::TexId::ROCK;
                matProps.pad[1] = 1.0f;
            } else if (matProps.preset_id == 7 && m_texture_manager.has_texture(GM3D::TexId::METAL)) {
                albedo_id = GM3D::TexId::METAL;
                matProps.pad[1] = 1.0f;
            }
        }

        if (m_material_constant_buffer) {
            D3D11_MAPPED_SUBRESOURCE mappedMat{};
            if (SUCCEEDED(m_context->Map(m_material_constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedMat))) {
                memcpy(mappedMat.pData, &matProps, sizeof(MaterialGPU));
                m_context->Unmap(m_material_constant_buffer.Get(), 0);
            }
            ID3D11Buffer* matBuffers[] = { m_material_constant_buffer.Get() };
            m_context->PSSetConstantBuffers(3, 1, matBuffers);
        }

        ID3D11ShaderResourceView* srvs[2] = {
            m_texture_manager.get_srv(albedo_id),
            normal_id != 0 ? m_texture_manager.get_srv(normal_id) : m_texture_manager.get_normal_srv(albedo_id)
        };
        m_context->PSSetShaderResources(1, 2, srvs);

        if (matProps.alpha_mode == 2 && m_alpha_blend_state) {
            float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
            m_context->OMSetBlendState(m_alpha_blend_state.Get(), blendFactor, 0xFFFFFFFF);
        } else {
            m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        }
        } // !depth_only

        if (mesh->is_skinned) {
            const GM3D::SkinnedModelData* skinned_model = m_mesh_manager.get_skinned_model(mesh_id);
            if (!skinned_model) return;

            ID3D11Buffer* vbs[] = { mesh->vertex_buffer.Get(), m_instance_buffer.Get() };
            UINT strides[] = { sizeof(Vertex3D), sizeof(InstanceDataGPU) };
            UINT offsets[] = { 0, 0 };
            m_context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
            m_context->IASetIndexBuffer(mesh->index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            for (const auto& item : items) {
                upload_bones(skinned_model, item.entity_id);

                UINT instance_offset = 0;
                InstanceDataGPU* dst = begin_instance_write(1, instance_offset);
                if (!dst) return;
                *dst = item.inst;
                end_instance_write();

                ID3D11Buffer* skin_vbs[] = { mesh->vertex_buffer.Get(), m_instance_buffer.Get() };
                UINT skin_strides[] = { sizeof(Vertex3D), sizeof(InstanceDataGPU) };
                UINT skin_offsets[] = { 0, instance_offset * static_cast<UINT>(sizeof(InstanceDataGPU)) };
                m_context->IASetVertexBuffers(0, 2, skin_vbs, skin_strides, skin_offsets);
                m_context->IASetIndexBuffer(mesh->index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);

                m_context->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);
            }
        } else {
            // Rigid Mesh (batch instanced rendering). The identity pose only has to be
            // uploaded once until a skinned draw overwrites the bone buffer.
            if (!bones_hold_identity) upload_identity_bones();

            const UINT copy_count = std::min(static_cast<UINT>(items.size()), m_max_instances);
            UINT instance_offset = 0;
            InstanceDataGPU* dst = begin_instance_write(copy_count, instance_offset);
            if (!dst) return;
            for (UINT k = 0; k < copy_count; ++k) dst[k] = items[k].inst;
            end_instance_write();

            ID3D11Buffer* vbs[] = { mesh->vertex_buffer.Get(), m_instance_buffer.Get() };
            UINT strides[] = { sizeof(Vertex3D), sizeof(InstanceDataGPU) };
            UINT offsets[] = { 0, instance_offset * static_cast<UINT>(sizeof(InstanceDataGPU)) };
            m_context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
            m_context->IASetIndexBuffer(mesh->index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            m_context->DrawIndexedInstanced(mesh->index_count, copy_count, 0, 0, 0);
        }
    };

    // Shadow casters are culled against each light view instead of redrawing the
    // whole scene, and each view uploads its instances once instead of once per
    // batch: with two cascades and four point lights that turns up to 26 full-scene
    // passes plus ~300 dynamic-buffer renames into 26 culled views and 26 uploads.
    std::vector<RenderItem> shadow_upload;
    struct ShadowRange { uint16_t mesh_id; UINT first; UINT count; };
    std::vector<ShadowRange> shadow_ranges;

    auto draw_shadow_view = [&](const float* light_view_proj) {
        const GM3D::FrustumPlanes light_frustum = GM3D::extract_frustum_planes(light_view_proj);

        shadow_upload.clear();
        shadow_ranges.clear();
        for (const auto& [mesh_id, inst_list] : shadow_batches) {
            const UINT first = static_cast<UINT>(shadow_upload.size());
            for (const auto& item : inst_list) {
                if (GM3D::aabb_inside_frustum(light_frustum, item.bounds_min, item.bounds_max)) {
                    shadow_upload.push_back(item);
                }
            }
            const UINT count = static_cast<UINT>(shadow_upload.size()) - first;
            if (count > 0) shadow_ranges.push_back(ShadowRange{ mesh_id, first, count });
        }
        if (shadow_ranges.empty()) return;

        const UINT upload_count = std::min(static_cast<UINT>(shadow_upload.size()), m_max_instances);
        UINT upload_offset = 0;
        InstanceDataGPU* pData = begin_instance_write(upload_count, upload_offset);
        if (!pData) return;
        for (UINT k = 0; k < upload_count; ++k) pData[k] = shadow_upload[k].inst;
        end_instance_write();

        for (const auto& range : shadow_ranges) {
            if (range.first >= upload_count) break;
            const UINT count = std::min(range.count, upload_count - range.first);
            const GPUMesh* mesh = m_mesh_manager.get_mesh(range.mesh_id);
            if (!mesh) continue;

            if (mesh->is_skinned) {
                const GM3D::SkinnedModelData* skinned_model = m_mesh_manager.get_skinned_model(range.mesh_id);
                if (!skinned_model) continue;
                for (UINT i = 0; i < count; ++i) {
                    upload_bones(skinned_model, shadow_upload[range.first + i].entity_id);
                    ID3D11Buffer* skin_vbs[] = { mesh->vertex_buffer.Get(), m_instance_buffer.Get() };
                    UINT skin_strides[] = { sizeof(Vertex3D), sizeof(InstanceDataGPU) };
                    UINT skin_offsets[] = { 0, (upload_offset + range.first + i) * static_cast<UINT>(sizeof(InstanceDataGPU)) };
                    m_context->IASetVertexBuffers(0, 2, skin_vbs, skin_strides, skin_offsets);
                    m_context->IASetIndexBuffer(mesh->index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                    m_context->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);
                }
            } else {
                if (!bones_hold_identity) upload_identity_bones();
                ID3D11Buffer* vbs[] = { mesh->vertex_buffer.Get(), m_instance_buffer.Get() };
                UINT strides[] = { sizeof(Vertex3D), sizeof(InstanceDataGPU) };
                UINT offsets[] = { 0, (upload_offset + range.first) * static_cast<UINT>(sizeof(InstanceDataGPU)) };
                m_context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
                m_context->IASetIndexBuffer(mesh->index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                m_context->DrawIndexedInstanced(mesh->index_count, count, 0, 0, 0);
            }
        }
    };

    // Assign shadow slots for point lights up to m_max_shadow_lights (max 4 slots in atlas bottom half)
    int32_t next_shadow_slot = 0;
    const int32_t max_slots = std::clamp(m_max_shadow_lights, 0, 4);
    m_light_constants.max_shadow_lights = max_slots;
    for (int i = 0; i < 16; ++i) {
        if (m_shadows_enabled && m_shadow_strength > 0.0f &&
            i < m_light_constants.point_light_count && m_point_light_cast_shadow[i] &&
            m_light_constants.lights[i].intensity > 0.001f && m_light_constants.lights[i].radius > 0.2f &&
            next_shadow_slot < max_slots) {
            m_light_constants.shadow_slots[i] = next_shadow_slot++;
        } else {
            m_light_constants.shadow_slots[i] = -1;
        }
    }

    // ----------------------------------------------------
    // PASS 1: SHADOW MAP PASS (2-Cascade Sun + 6-Face Point Light Atlas)
    // ----------------------------------------------------
    m_context->ClearDepthStencilView(m_shadow_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    m_context->OMSetRenderTargets(0, nullptr, m_shadow_dsv.Get());
    m_context->OMSetDepthStencilState(m_shadow_depth_stencil_state.Get(), 0);
    m_context->RSSetState(m_shadow_rasterizer_state.Get());

    m_context->IASetInputLayout(m_input_layout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_shadow_vertex_shader.Get(), nullptr, 0);
    m_context->PSSetShader(nullptr, nullptr, 0);

    ID3D11Buffer* vscbs[] = { m_constant_buffer.Get(), m_bone_constant_buffer.Get() };
    m_context->VSSetConstantBuffers(0, 2, vscbs);

    if (m_shadows_enabled && m_shadow_strength > 0.0f && !shadow_batches.empty()) {
        const float halfAtlas = static_cast<float>(std::max<UINT>(512u, m_shadow_atlas_size / 2));

        // 1a. Sun Cascade 0 (Top-Left Quadrant)
        D3D11_VIEWPORT vp0{};
        vp0.TopLeftX = 0.0f;
        vp0.TopLeftY = 0.0f;
        vp0.Width = halfAtlas;
        vp0.Height = halfAtlas;
        vp0.MinDepth = 0.0f;
        vp0.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp0);
        draw_shadow_view(m_cached_scene_cb.light_view_proj);

        // 1b. Sun Cascade 1 (Top-Right Quadrant)
        bool mutated_cb = false;
        if (m_csm_enabled) {
            SceneConstantBuffer cb1 = m_cached_scene_cb;
            memcpy(cb1.light_view_proj, m_cached_scene_cb.light_view_proj_c1, sizeof(cb1.light_view_proj));
            D3D11_MAPPED_SUBRESOURCE mappedCb{};
            if (SUCCEEDED(m_context->Map(m_constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCb))) {
                memcpy(mappedCb.pData, &cb1, sizeof(SceneConstantBuffer));
                m_context->Unmap(m_constant_buffer.Get(), 0);
            }
            mutated_cb = true;

            D3D11_VIEWPORT vp1{};
            vp1.TopLeftX = halfAtlas;
            vp1.TopLeftY = 0.0f;
            vp1.Width = halfAtlas;
            vp1.Height = halfAtlas;
            vp1.MinDepth = 0.0f;
            vp1.MaxDepth = 1.0f;
            m_context->RSSetViewports(1, &vp1);
            draw_shadow_view(cb1.light_view_proj);        }

        // 1c. Point Light Omnidirectional 6-Face Tiles (Bottom Half: 4 slots x 6 faces)
        if (next_shadow_slot > 0) {
            const float tileSize = static_cast<float>(std::max<UINT>(128u, m_shadow_atlas_size / 8));
            static const float faceDirs[6][3] = {
                {  1.0f,  0.0f,  0.0f }, // +X
                { -1.0f,  0.0f,  0.0f }, // -X
                {  0.0f,  1.0f,  0.0f }, // +Y
                {  0.0f, -1.0f,  0.0f }, // -Y
                {  0.0f,  0.0f,  1.0f }, // +Z
                {  0.0f,  0.0f, -1.0f }  // -Z
            };
            static const float faceUps[6][3] = {
                { 0.0f, 1.0f,  0.0f }, // +X
                { 0.0f, 1.0f,  0.0f }, // -X
                { 0.0f, 0.0f, -1.0f }, // +Y
                { 0.0f, 0.0f,  1.0f }, // -Y
                { 0.0f, 1.0f,  0.0f }, // +Z
                { 0.0f, 1.0f,  0.0f }  // -Z
            };

            for (int p = 0; p < m_light_constants.point_light_count; ++p) {
                const int32_t slot = m_light_constants.shadow_slots[p];
                if (slot < 0 || slot >= 4) continue;

                const auto& pl = m_light_constants.lights[p];
                const float zn = 0.15f;
                const float zf = std::max(0.5f, pl.radius);
                const float q = zf / (zf - zn);
                const float proj90[16] = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, q,    1.0f,
                    0.0f, 0.0f, -q * zn, 0.0f
                };

                for (int face = 0; face < 6; ++face) {
                    float viewFace[16];
                    ComputeLookAtLH(
                        viewFace,
                        pl.pos[0], pl.pos[1], pl.pos[2],
                        pl.pos[0] + faceDirs[face][0],
                        pl.pos[1] + faceDirs[face][1],
                        pl.pos[2] + faceDirs[face][2],
                        faceUps[face][0], faceUps[face][1], faceUps[face][2]);

                    SceneConstantBuffer cbFace = m_cached_scene_cb;
                    MatrixMultiply(cbFace.light_view_proj, viewFace, proj90);

                    D3D11_MAPPED_SUBRESOURCE mappedCb{};
                    if (SUCCEEDED(m_context->Map(m_constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCb))) {
                        memcpy(mappedCb.pData, &cbFace, sizeof(SceneConstantBuffer));
                        m_context->Unmap(m_constant_buffer.Get(), 0);
                    }
                    mutated_cb = true;

                    D3D11_VIEWPORT vpFace{};
                    vpFace.TopLeftX = static_cast<float>(face) * tileSize;
                    vpFace.TopLeftY = halfAtlas + static_cast<float>(slot) * tileSize;
                    vpFace.Width = tileSize;
                    vpFace.Height = tileSize;
                    vpFace.MinDepth = 0.0f;
                    vpFace.MaxDepth = 1.0f;
                    m_context->RSSetViewports(1, &vpFace);

                    draw_shadow_view(cbFace.light_view_proj);
                }
            }
        }

        // Restore SceneConstantBuffer (Cascade 0 in LightViewProj) for Pass 2
        if (mutated_cb) {
            D3D11_MAPPED_SUBRESOURCE mappedCb{};
            if (SUCCEEDED(m_context->Map(m_constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCb))) {
                memcpy(mappedCb.pData, &m_cached_scene_cb, sizeof(SceneConstantBuffer));
                m_context->Unmap(m_constant_buffer.Get(), 0);
            }
        }
    }

    // Unbind shadow DSV so it can be sampled as SRV in Pass 2
    ID3D11RenderTargetView* nullRTV = nullptr;
    m_context->OMSetRenderTargets(1, &nullRTV, nullptr);

    // ----------------------------------------------------
    // PASS 2: FORWARD PBR PASS (With PCF Shadows & Textures)
    // ----------------------------------------------------
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(m_config.width);
    vp.Height = static_cast<float>(m_config.height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    ID3D11RenderTargetView* target_rtv = m_hdr_rtv ? m_hdr_rtv.Get() : m_render_target_view.Get();

    m_context->ClearRenderTargetView(target_rtv, m_sky_color);
    m_context->ClearDepthStencilView(m_depth_stencil_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    m_context->OMSetRenderTargets(1, &target_rtv, m_depth_stencil_view.Get());

    // 1. Procedural Atmospheric Sky Dome with Radiant Sun Disk
    render_sky();

    m_context->OMSetDepthStencilState(m_depth_stencil_state.Get(), 0);
    m_context->RSSetState(m_rasterizer_state.Get());
    m_context->IASetInputLayout(m_input_layout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D11_MAPPED_SUBRESOURCE mappedLight{};
    if (SUCCEEDED(m_context->Map(m_light_constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedLight))) {
        memcpy(mappedLight.pData, &m_light_constants, sizeof(GPULightConstants));
        m_context->Unmap(m_light_constant_buffer.Get(), 0);
    }

    m_context->VSSetShader(m_vertex_shader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixel_shader.Get(), nullptr, 0);
    m_context->VSSetConstantBuffers(0, 2, vscbs);
    m_context->PSSetConstantBuffers(0, 1, m_constant_buffer.GetAddressOf());
    m_context->PSSetConstantBuffers(2, 1, m_light_constant_buffer.GetAddressOf());

    // Bind Shadow Map SRV & Texture Samplers to Pixel Shader
    m_context->PSSetShaderResources(0, 1, m_shadow_srv.GetAddressOf());
    ID3D11SamplerState* samplers[] = { m_shadow_sampler.Get(), m_texture_manager.get_sampler() };
    m_context->PSSetSamplers(0, 2, samplers);

    // Terrain detail blend: splat weights at t3, four albedo sets at t4..t7 and their
    // normal maps at t8..t11. Bound as one contiguous block; only the terrain shader
    // path reads them.
    {
        const uint16_t detail_ids[4] = { GM3D::TexId::GRASS, GM3D::TexId::ROCK, GM3D::TexId::SAND, GM3D::TexId::COBBLE };
        ID3D11ShaderResourceView* detail_srvs[9] = {
            m_terrain_system.get_detail_splat_srv(),
            m_texture_manager.get_srv(detail_ids[0]),
            m_texture_manager.get_srv(detail_ids[1]),
            m_texture_manager.get_srv(detail_ids[2]),
            m_texture_manager.get_srv(detail_ids[3]),
            m_texture_manager.get_normal_srv(detail_ids[0]),
            m_texture_manager.get_normal_srv(detail_ids[1]),
            m_texture_manager.get_normal_srv(detail_ids[2]),
            m_texture_manager.get_normal_srv(detail_ids[3]),
        };
        m_context->PSSetShaderResources(3, 9, detail_srvs);
    }

    // Opaque geometry first, then blended batches far-to-near so overlapping
    // transparent surfaces composite in a deterministic order.
    struct BlendedBatch {
        BatchKey key;
        std::vector<RenderItem>* items;
        float depth;
    };
    std::vector<BlendedBatch> blended_batches;

    for (auto& [key, inst_list] : batches) {
        auto mat_it = m_materials.find(key.material_id);
        const bool blended = mat_it != m_materials.end() && mat_it->second.alpha_mode == 2;
        if (!blended) {
            draw_batch_items(inst_list, key.mesh_id, key.material_id, key.texture_id);
            continue;
        }
        float batch_depth = 0.0f;
        for (const auto& item : inst_list) {
            if (item.depth > batch_depth) batch_depth = item.depth;
        }
        blended_batches.push_back(BlendedBatch{ key, &inst_list, batch_depth });
    }

    if (!blended_batches.empty()) {
        std::sort(blended_batches.begin(), blended_batches.end(),
                  [](const BlendedBatch& a, const BlendedBatch& b) { return a.depth > b.depth; });
        for (auto& batch : blended_batches) {
            std::sort(batch.items->begin(), batch.items->end(),
                      [](const RenderItem& a, const RenderItem& b) { return a.depth > b.depth; });
            draw_batch_items(*batch.items, batch.key.mesh_id, batch.key.material_id, batch.key.texture_id);
        }
    }

    // Render Procedural Grass & Foliage System
    if (m_grass_system.is_enabled()) {
        float interaction[4] = { header.camera_target[0], header.camera_target[1], header.camera_target[2], 2.2f };
        m_grass_system.render(m_context.Get(), m_cached_scene_cb, header.camera_pos, m_total_time, interaction);
    }

    // Restore standard blend state
    m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    // 3. Draw 3D Transform Gizmo & Selection Box (If Enabled, directly into HDR scene buffer)
    if (m_gizmo.enabled) {
        draw_gizmo(header);
    }

    // Unbind SRVs after forward rendering
    ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
    m_context->PSSetShaderResources(0, 3, nullSRVs);

    // 4. Render GPU Magic Particle System with Additive Blending
    m_particle_manager.render(m_context.Get(), m_constant_buffer.Get(), header.camera_pos);

    // 5. HDR Post-Processing (Bloom Extraction, 2-Pass Gaussian Blur, ACES Tonemapping)
    if (m_hdr_rtv) {
        render_post_processing();
    }

    // 6. Present SwapChain
    // Only when this renderer owns the window. The editor renders into an offscreen viewport
    // texture and presents once itself after drawing the UI; presenting here as well made the
    // editor wait two vsync intervals per frame (a hard 30 FPS ceiling on a 60 Hz display,
    // regardless of scene contents).
    if (!m_custom_render_target) {
        m_swap_chain->Present(m_config.vsync ? 1 : 0, 0);
    }
}

void RenderWorld::particle_burst(uint32_t type, float x, float y, float z, uint32_t count, float speed, float size, float r, float g, float b) {
    m_particle_manager.spawn_burst(type, x, y, z, count, speed, size, r, g, b);
}

void RenderWorld::particle_ring(uint32_t type, float x, float y, float z, float radius, uint32_t count, float r, float g, float b) {
    m_particle_manager.spawn_ring(type, x, y, z, radius, count, r, g, b);
}

void RenderWorld::particle_clear() {
    m_particle_manager.clear();
}

void RenderWorld::set_gizmo(bool enabled, float x, float y, float z, float sx, float sy, float sz, float axis_len) {
    m_gizmo.enabled = enabled;
    m_gizmo.x = x;
    m_gizmo.y = y;
    m_gizmo.z = z;
    m_gizmo.sx = (sx > 0.001f) ? sx : 1.0f;
    m_gizmo.sy = (sy > 0.001f) ? sy : 1.0f;
    m_gizmo.sz = (sz > 0.001f) ? sz : 1.0f;
    m_gizmo.axis_len = (axis_len > 0.1f) ? axis_len : 2.8f;
}

void RenderWorld::render_sky() {
    if (!m_sky_vertex_shader || !m_sky_pixel_shader || !m_context) return;

    m_context->OMSetDepthStencilState(m_sky_depth_stencil_state.Get(), 0);
    m_context->RSSetState(m_sky_rasterizer_state.Get());

    m_context->VSSetShader(m_sky_vertex_shader.Get(), nullptr, 0);
    m_context->PSSetShader(m_sky_pixel_shader.Get(), nullptr, 0);

    m_context->VSSetConstantBuffers(0, 1, m_constant_buffer.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_constant_buffer.GetAddressOf());

    m_context->IASetInputLayout(nullptr);
    ID3D11Buffer* nullVb = nullptr;
    UINT zero = 0;
    m_context->IASetVertexBuffers(0, 1, &nullVb, &zero, &zero);
    m_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_context->Draw(3, 0);
}

void RenderWorld::draw_gizmo(const SharedBufferHeader& header) {
    if (!m_gizmo.enabled || !m_context) return;

    const GPUMesh* cube_mesh = m_mesh_manager.get_mesh(0);
    if (!cube_mesh || !cube_mesh->vertex_buffer || !cube_mesh->index_buffer) return;

    // Reset bone matrices to identity
    D3D11_MAPPED_SUBRESOURCE mappedBones{};
    if (SUCCEEDED(m_context->Map(m_bone_constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedBones))) {
        DirectX::XMFLOAT4X4 identity;
        DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
        std::vector<DirectX::XMFLOAT4X4> identities(128, identity);
        memcpy(mappedBones.pData, identities.data(), sizeof(DirectX::XMFLOAT4X4) * 128);
        m_context->Unmap(m_bone_constant_buffer.Get(), 0);
    }

    // Explicitly configure pipeline states for 3D Gizmo rendering
    m_context->OMSetDepthStencilState(m_gizmo_depth_stencil_state.Get(), 0);
    m_context->RSSetState(m_rasterizer_state.Get());
    m_context->IASetInputLayout(m_input_layout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_context->VSSetShader(m_vertex_shader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixel_shader.Get(), nullptr, 0);

    ID3D11Buffer* vscbs[] = { m_constant_buffer.Get(), m_bone_constant_buffer.Get() };
    m_context->VSSetConstantBuffers(0, 2, vscbs);
    m_context->PSSetConstantBuffers(0, 1, m_constant_buffer.GetAddressOf());

    float cdx = m_gizmo.x - header.camera_pos[0];
    float cdy = m_gizmo.y - header.camera_pos[1];
    float cdz = m_gizmo.z - header.camera_pos[2];
    float cam_dist = sqrtf(cdx * cdx + cdy * cdy + cdz * cdz);
    float scale_factor = std::max(0.5f, cam_dist * 0.075f);

    float len = m_gizmo.axis_len * scale_factor * 0.6f;
    float thick = 0.06f * scale_factor;
    float head_len = 0.42f * scale_factor;
    float head_w = 0.18f * scale_factor;

    std::vector<InstanceDataGPU> gizmo_instances;
    gizmo_instances.reserve(32);

    auto add_box = [&](float px, float py, float pz, float sx, float sy, float sz, float matId) {
        InstanceDataGPU inst{};
        inst.px = px; inst.py = py; inst.pz = pz;
        inst.qx = 0.0f; inst.qy = 0.0f; inst.qz = 0.0f; inst.qw = 1.0f;
        inst.sx = sx; inst.sy = sy; inst.sz = sz;
        inst.param1 = matId; // matId >= 100 triggers unlit emissive in PS_Main
        inst.param2 = 0.0f;
        gizmo_instances.push_back(inst);
    };

    float gx = m_gizmo.x;
    float gy = m_gizmo.y;
    float gz = m_gizmo.z;

    // 1. Selection Bounding Box (Amber Golden Frame)
    float hx = (m_gizmo.sx * 0.5f) * 1.04f;
    float hy = (m_gizmo.sy * 0.5f) * 1.04f;
    float hz = (m_gizmo.sz * 0.5f) * 1.04f;
    float edge_t = std::max(0.035f, thick * 0.55f);
    float span_x = hx * 2.0f;
    float span_y = hy * 2.0f;
    float span_z = hz * 2.0f;

    // Bottom 4 edges
    add_box(gx, gy - hy, gz - hz, span_x, edge_t, edge_t, 104);
    add_box(gx, gy - hy, gz + hz, span_x, edge_t, edge_t, 104);
    add_box(gx - hx, gy - hy, gz, edge_t, edge_t, span_z, 104);
    add_box(gx + hx, gy - hy, gz, edge_t, edge_t, span_z, 104);

    // Top 4 edges
    add_box(gx, gy + hy, gz - hz, span_x, edge_t, edge_t, 104);
    add_box(gx, gy + hy, gz + hz, span_x, edge_t, edge_t, 104);
    add_box(gx - hx, gy + hy, gz, edge_t, edge_t, span_z, 104);
    add_box(gx + hx, gy + hy, gz, edge_t, edge_t, span_z, 104);

    // Vertical 4 edges
    add_box(gx - hx, gy, gz - hz, edge_t, span_y, edge_t, 104);
    add_box(gx + hx, gy, gz - hz, edge_t, span_y, edge_t, 104);
    add_box(gx - hx, gy, gz + hz, edge_t, span_y, edge_t, 104);
    add_box(gx + hx, gy, gz + hz, edge_t, span_y, edge_t, 104);

    // 2. Center Pivot Hub (Bright Yellow Cube)
    float hub_s = head_w * 1.3f;
    add_box(gx, gy, gz, hub_s, hub_s, hub_s, 103);

    // 3. Red +X Axis Arrow
    add_box(gx + len * 0.5f, gy, gz, len, thick, thick, 100);
    add_box(gx + len + head_len * 0.5f, gy, gz, head_len, head_w, head_w, 100);

    // 4. Green +Y Axis Arrow
    add_box(gx, gy + len * 0.5f, gz, thick, len, thick, 101);
    add_box(gx, gy + len + head_len * 0.5f, gz, head_w, head_len, head_w, 101);

    // 5. Blue +Z Axis Arrow
    add_box(gx, gy, gz + len * 0.5f, thick, thick, len, 102);
    add_box(gx, gy, gz + len + head_len * 0.5f, head_w, head_w, head_len, 102);

    if (!gizmo_instances.empty()) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_context->Map(m_instance_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, gizmo_instances.data(), sizeof(InstanceDataGPU) * gizmo_instances.size());
            m_context->Unmap(m_instance_buffer.Get(), 0);

            ID3D11Buffer* vbs[] = { cube_mesh->vertex_buffer.Get(), m_instance_buffer.Get() };
            UINT strides[] = { sizeof(Vertex3D), sizeof(InstanceDataGPU) };
            UINT offsets[] = { 0, 0 };
            m_context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
            m_context->IASetIndexBuffer(cube_mesh->index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            m_context->DrawIndexedInstanced(cube_mesh->index_count, static_cast<UINT>(gizmo_instances.size()), 0, 0, 0);
        }
    }

    // Restore standard depth stencil state
    m_context->OMSetDepthStencilState(m_depth_stencil_state.Get(), 0);
}

void RenderWorld::set_sun_light(float dx, float dy, float dz, float r, float g, float b, float intensity) {
    m_sun_dir[0] = dx;
    m_sun_dir[1] = dy;
    m_sun_dir[2] = dz;
    m_sun_color[0] = r;
    m_sun_color[1] = g;
    m_sun_color[2] = b;
    m_sun_intensity = intensity;
}

void RenderWorld::set_ambient_light(float r, float g, float b, float intensity) {
    m_ambient_color[0] = r;
    m_ambient_color[1] = g;
    m_ambient_color[2] = b;
    m_ambient_intensity = intensity;
}

void RenderWorld::set_shadow_settings(bool enabled, float strength, float softness, float coverage) {
    m_shadows_enabled = enabled;
    m_shadow_strength = std::clamp(strength, 0.0f, 1.0f);
    m_shadow_softness = std::clamp(softness, 0.5f, 6.0f);
    m_shadow_coverage = std::clamp(coverage, 20.0f, 240.0f);
}

void RenderWorld::set_shadow_quality(int quality, bool csm_enabled, int max_shadow_lights) {
    m_shadow_quality = std::clamp(quality, 1, 3);
    m_csm_enabled = csm_enabled;
    m_max_shadow_lights = std::clamp(max_shadow_lights, 0, 4);

    UINT desired_atlas = 2048;
    if (m_shadow_quality == 1) desired_atlas = 1024;
    else if (m_shadow_quality == 2) desired_atlas = 2048;
    else if (m_shadow_quality == 3) desired_atlas = 4096;

    if (m_device && desired_atlas != m_shadow_atlas_size) {
        create_shadow_atlas(desired_atlas);
    }
}

void RenderWorld::set_point_light(uint32_t index, float x, float y, float z, float radius, float r, float g, float b, float intensity, bool cast_shadow) {
    if (index >= 16) return;
    auto& light = m_light_constants.lights[index];
    light.pos[0] = x;
    light.pos[1] = y;
    light.pos[2] = z;
    light.radius = radius;
    light.color[0] = r;
    light.color[1] = g;
    light.color[2] = b;
    light.intensity = intensity;
    m_point_light_cast_shadow[index] = cast_shadow;

    if (static_cast<int32_t>(index + 1) > m_light_constants.point_light_count) {
        m_light_constants.point_light_count = static_cast<int32_t>(index + 1);
    }
}

void RenderWorld::clear_point_lights() {
    m_light_constants.point_light_count = 0;
    memset(m_light_constants.lights, 0, sizeof(m_light_constants.lights));
    for (int i = 0; i < 16; ++i) {
        m_point_light_cast_shadow[i] = false;
        m_light_constants.shadow_slots[i] = -1;
    }
}

void RenderWorld::set_sky_color(float r, float g, float b) {
    m_sky_color[0] = r;
    m_sky_color[1] = g;
    m_sky_color[2] = b;
    m_sky_color[3] = 1.0f;
}

void RenderWorld::set_fog(float start, float end, float r, float g, float b) {
    m_fog_start = start;
    m_fog_end = end;
    if (r >= 0.0f) {
        m_sky_color[0] = r;
        m_sky_color[1] = g;
        m_sky_color[2] = b;
    }
}

uint16_t RenderWorld::load_obj_mesh(const std::string& filepath) {
    if (!m_device) return 0;
    return m_mesh_manager.load_model_from_file(m_device.Get(), filepath);
}

uint16_t RenderWorld::load_model(const std::string& filepath) {
    if (!m_device) return 0;
    return m_mesh_manager.load_model_from_file(m_device.Get(), filepath);
}

uint16_t RenderWorld::load_texture(const std::string& filepath) {
    if (!m_device || !m_context) return 0;
    return m_texture_manager.load_texture_from_file(m_device.Get(), m_context.Get(), filepath);
}

bool RenderWorld::set_normal_map(uint16_t target_tex_id, uint16_t normal_tex_id) {
    return m_texture_manager.set_normal_map_texture(target_tex_id, normal_tex_id);
}

void RenderWorld::set_material_properties(uint16_t id, const MaterialGPU& props) {
    m_materials[id] = props;
}

void RenderWorld::set_material_properties(uint16_t id, const MaterialGPU& props, uint16_t albedo_tex, uint16_t normal_tex) {
    m_materials[id] = props;
    if (albedo_tex != 0 || normal_tex != 0) {
        m_material_textures[id] = { albedo_tex, normal_tex };
    } else m_material_textures.erase(id);
}

void RenderWorld::clear_materials() {
    m_materials.clear();
    m_material_textures.clear();
}

const MaterialGPU* RenderWorld::get_material_properties(uint16_t id) const {
    auto it = m_materials.find(id);
    if (it != m_materials.end()) {
        return &it->second;
    }
    return nullptr;
}

bool RenderWorld::play_animation(uint32_t entity_id, int32_t anim_index, float speed, bool loop) {
    auto& state = m_entity_anims[entity_id];
    state.anim_index = anim_index;
    state.speed = speed;
    state.loop = loop;
    state.is_playing = true;
    return true;
}

bool RenderWorld::stop_animation(uint32_t entity_id) {
    auto it = m_entity_anims.find(entity_id);
    if (it != m_entity_anims.end()) {
        it->second.is_playing = false;
        it->second.anim_index = -1;
        it->second.current_time = 0.0f;
        return true;
    }
    return false;
}

bool RenderWorld::set_animation_time(uint32_t entity_id, float time) {
    auto it = m_entity_anims.find(entity_id);
    if (it != m_entity_anims.end()) {
        it->second.current_time = time;
        return true;
    }
    return false;
}

uint32_t RenderWorld::get_animation_count(uint16_t mesh_id) const {
    return m_mesh_manager.get_animation_count(mesh_id);
}

std::string RenderWorld::get_animation_name(uint16_t mesh_id, uint32_t anim_index) const {
    const auto* clip = m_mesh_manager.get_animation(mesh_id, anim_index);
    return clip ? clip->name : "";
}

float RenderWorld::get_animation_duration(uint16_t mesh_id, uint32_t anim_index) const {
    const auto* clip = m_mesh_manager.get_animation(mesh_id, anim_index);
    return clip ? clip->duration : 0.0f;
}

void RenderWorld::set_bloom(bool enabled, float intensity, float threshold) {
    m_bloom_enabled = enabled;
    m_bloom_intensity = intensity;
    m_bloom_threshold = threshold;
}

bool RenderWorld::create_post_textures(uint32_t width, uint32_t height) {
    if (!m_device || width == 0 || height == 0) return false;

    m_hdr_srv.Reset();
    m_hdr_rtv.Reset();
    m_hdr_texture.Reset();

    for (int i = 0; i < 2; ++i) {
        m_bloom_srv[i].Reset();
        m_bloom_rtv[i].Reset();
        m_bloom_tex[i].Reset();
    }

    // 1. Fullscreen HDR Texture (16-bit Float per channel)
    D3D11_TEXTURE2D_DESC hdrDesc{};
    hdrDesc.Width = std::max(64u, width);
    hdrDesc.Height = std::max(64u, height);
    hdrDesc.MipLevels = 1;
    hdrDesc.ArraySize = 1;
    hdrDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrDesc.SampleDesc.Count = 1;
    hdrDesc.SampleDesc.Quality = 0;
    hdrDesc.Usage = D3D11_USAGE_DEFAULT;
    hdrDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D(&hdrDesc, nullptr, m_hdr_texture.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(m_hdr_texture.Get(), nullptr, m_hdr_rtv.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = m_device->CreateShaderResourceView(m_hdr_texture.Get(), nullptr, m_hdr_srv.GetAddressOf());
    if (FAILED(hr)) return false;

    // 2. 1/4 Resolution Bloom Ping-Pong Textures
    m_bloom_w = std::max(16u, width / 4);
    m_bloom_h = std::max(16u, height / 4);

    D3D11_TEXTURE2D_DESC bloomDesc = hdrDesc;
    bloomDesc.Width = m_bloom_w;
    bloomDesc.Height = m_bloom_h;

    for (int i = 0; i < 2; ++i) {
        hr = m_device->CreateTexture2D(&bloomDesc, nullptr, m_bloom_tex[i].GetAddressOf());
        if (FAILED(hr)) return false;

        hr = m_device->CreateRenderTargetView(m_bloom_tex[i].Get(), nullptr, m_bloom_rtv[i].GetAddressOf());
        if (FAILED(hr)) return false;

        hr = m_device->CreateShaderResourceView(m_bloom_tex[i].Get(), nullptr, m_bloom_srv[i].GetAddressOf());
        if (FAILED(hr)) return false;
    }

    return true;
}

bool RenderWorld::init_post_processing_resources() {
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(
        g_post_process_shader_hlsl, strlen(g_post_process_shader_hlsl), "PostProcess_VS",
        nullptr, nullptr, "VS_Fullscreen", "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        vsBlob.GetAddressOf(), errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Post VS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_post_vs.GetAddressOf());
    if (FAILED(hr)) return false;

    auto compile_ps = [&](const char* entry, ID3D11PixelShader** out_ps) -> bool {
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> err;
        HRESULT r = D3DCompile(
            g_post_process_shader_hlsl, strlen(g_post_process_shader_hlsl), "PostProcess_PS",
            nullptr, nullptr, entry, "ps_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            psBlob.GetAddressOf(), err.GetAddressOf()
        );
        if (FAILED(r)) {
            if (err) {
                std::cerr << "Post PS Compile Error (" << entry << "): " << (char*)err->GetBufferPointer() << std::endl;
            }
            return false;
        }
        return SUCCEEDED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, out_ps));
    };

    if (!compile_ps("PS_ExtractBloom", m_bloom_extract_ps.GetAddressOf())) return false;
    if (!compile_ps("PS_BlurH", m_blur_h_ps.GetAddressOf())) return false;
    if (!compile_ps("PS_BlurV", m_blur_v_ps.GetAddressOf())) return false;
    if (!compile_ps("PS_Composite", m_composite_ps.GetAddressOf())) return false;

    // Linear Clamp Sampler
    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0.0f;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = m_device->CreateSamplerState(&sampDesc, m_linear_clamp_sampler.GetAddressOf());
    if (FAILED(hr)) return false;

    // Post Constants Buffer (32 bytes)
    D3D11_BUFFER_DESC cbd{};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = 32;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = m_device->CreateBuffer(&cbd, nullptr, m_post_cb.GetAddressOf());
    if (FAILED(hr)) return false;

    return create_post_textures(m_config.width, m_config.height);
}

void RenderWorld::render_post_processing() {
    if (!m_post_vs || !m_bloom_extract_ps || !m_blur_h_ps || !m_blur_v_ps || !m_composite_ps) return;

    struct PostConstantsGPU {
        float texel_size[2];
        float bloom_threshold;
        float bloom_intensity;
        float exposure;
        float pad[3];
    };

    PostConstantsGPU pc{};
    pc.bloom_threshold = m_bloom_threshold;
    pc.bloom_intensity = m_bloom_enabled ? m_bloom_intensity : 0.0f;
    pc.exposure = m_exposure;
    pc.texel_size[0] = 1.0f / static_cast<float>(m_bloom_w);
    pc.texel_size[1] = 1.0f / static_cast<float>(m_bloom_h);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(m_context->Map(m_post_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &pc, sizeof(PostConstantsGPU));
        m_context->Unmap(m_post_cb.Get(), 0);
    }

    // Unbind OM Render Targets and PS SRVs to prevent pipeline resource hazards
    ID3D11RenderTargetView* nullRTV = nullptr;
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    m_context->OMSetRenderTargets(1, &nullRTV, nullptr);
    m_context->PSSetShaderResources(0, 2, nullSRVs);

    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_post_vs.Get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_post_cb.GetAddressOf());
    m_context->PSSetSamplers(0, 1, m_linear_clamp_sampler.GetAddressOf());

    // Pass 1: Extract Bright Pass to Bloom Ping-Pong 0
    D3D11_VIEWPORT bloomVp{};
    bloomVp.Width = static_cast<float>(m_bloom_w);
    bloomVp.Height = static_cast<float>(m_bloom_h);
    bloomVp.MinDepth = 0.0f;
    bloomVp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &bloomVp);

    m_context->OMSetRenderTargets(1, m_bloom_rtv[0].GetAddressOf(), nullptr);
    m_context->PSSetShader(m_bloom_extract_ps.Get(), nullptr, 0);
    m_context->PSSetShaderResources(0, 1, m_hdr_srv.GetAddressOf());
    m_context->Draw(3, 0);

    // Unbind Bloom RTV 0
    m_context->OMSetRenderTargets(1, &nullRTV, nullptr);
    m_context->PSSetShaderResources(0, 1, nullSRVs);

    // Pass 2: Horizontal Blur from Bloom 0 to Bloom 1
    m_context->OMSetRenderTargets(1, m_bloom_rtv[1].GetAddressOf(), nullptr);
    m_context->PSSetShader(m_blur_h_ps.Get(), nullptr, 0);
    m_context->PSSetShaderResources(0, 1, m_bloom_srv[0].GetAddressOf());
    m_context->Draw(3, 0);

    // Unbind Bloom RTV 1
    m_context->OMSetRenderTargets(1, &nullRTV, nullptr);
    m_context->PSSetShaderResources(0, 1, nullSRVs);

    // Pass 3: Vertical Blur from Bloom 1 back to Bloom 0
    m_context->OMSetRenderTargets(1, m_bloom_rtv[0].GetAddressOf(), nullptr);
    m_context->PSSetShader(m_blur_v_ps.Get(), nullptr, 0);
    m_context->PSSetShaderResources(0, 1, m_bloom_srv[1].GetAddressOf());
    m_context->Draw(3, 0);

    // Unbind Bloom RTV 0
    m_context->OMSetRenderTargets(1, &nullRTV, nullptr);
    m_context->PSSetShaderResources(0, 1, nullSRVs);

    // Pass 4: Composite HDR + Bloom -> Final Target (Custom Editor RTV or Main Swapchain) with ACES Tonemapping
    D3D11_VIEWPORT fullVp{};
    if (m_custom_render_target) {
        ComPtr<ID3D11Resource> res;
        m_custom_render_target->GetResource(res.GetAddressOf());
        ComPtr<ID3D11Texture2D> tex;
        if (SUCCEEDED(res.As(&tex))) {
            D3D11_TEXTURE2D_DESC td;
            tex->GetDesc(&td);
            fullVp.Width = static_cast<float>(td.Width);
            fullVp.Height = static_cast<float>(td.Height);
        } else {
            fullVp.Width = static_cast<float>(m_config.width);
            fullVp.Height = static_cast<float>(m_config.height);
        }
    } else {
        fullVp.Width = static_cast<float>(m_config.width);
        fullVp.Height = static_cast<float>(m_config.height);
    }
    fullVp.MinDepth = 0.0f;
    fullVp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &fullVp);

    ID3D11RenderTargetView* target_out = m_custom_render_target ? m_custom_render_target.Get() : m_render_target_view.Get();
    m_context->OMSetRenderTargets(1, &target_out, nullptr);
    m_context->PSSetShader(m_composite_ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView* compSrvs[2] = { m_hdr_srv.Get(), m_bloom_srv[0].Get() };
    m_context->PSSetShaderResources(0, 2, compSrvs);
    m_context->Draw(3, 0);

    // Clean up bindings
    m_context->PSSetShaderResources(0, 2, nullSRVs);
}

uint32_t RenderWorld::generate_terrain(uint32_t grid_size, float world_size, float height_scale, uint32_t seed, PhysicsWorld& physics_world, const std::vector<float>* heights) {
    if (!m_device || !m_context) return 0;
    GM3D::TerrainConfig cfg{};
    cfg.grid_size = grid_size;
    cfg.world_size = world_size;
    cfg.height_scale = height_scale;
    cfg.seed = seed;
    uint32_t body_id = m_terrain_system.generate(m_device.Get(), m_context.Get(), m_mesh_manager, m_texture_manager, physics_world, cfg, heights);
    if (m_terrain_system.is_generated() && m_grass_system.get_instance_count() > 0) {
        m_grass_system.snap_to_terrain(m_terrain_system, m_device.Get());
    }
    return body_id;
}

float RenderWorld::get_terrain_height(float world_x, float world_z) const {
    return m_terrain_system.get_height(world_x, world_z);
}
