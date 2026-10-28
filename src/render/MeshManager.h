#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include "AnimationData.h"

using Microsoft::WRL::ComPtr;

struct Vertex3D {
    float x = 0, y = 0, z = 0;          // Position (12 bytes)
    float nx = 0, ny = 1, nz = 0;       // Normal (12 bytes)
    float u = 0, v = 0;                 // TexCoord (8 bytes)
    uint32_t joints[4] = {0, 0, 0, 0};  // Bone Indices (16 bytes)
    float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f}; // Bone Weights (16 bytes)

    Vertex3D() = default;
    Vertex3D(float px, float py, float pz, float pnx, float pny, float pnz, float pu, float pv)
        : x(px), y(py), z(pz), nx(pnx), ny(pny), nz(pnz), u(pu), v(pv) {
        joints[0] = joints[1] = joints[2] = joints[3] = 0;
        weights[0] = 1.0f; weights[1] = weights[2] = weights[3] = 0.0f;
    }
};

struct InstanceDataGPU {
    float px, py, pz;     // World Position
    float qx, qy, qz, qw; // Quaternion Rotation
    float sx, sy, sz;     // Scale
    float param1, param2; // Custom parameters (e.g. material color, emission)
};

struct GPUMesh {
    ComPtr<ID3D11Buffer> vertex_buffer;
    ComPtr<ID3D11Buffer> index_buffer;
    uint32_t index_count = 0;
    uint32_t vertex_count = 0;
    bool is_skinned = false;
};

struct GLTFMaterialInfo {
    std::string name;
    float base_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissive = 0.0f;
    float emissive_color[3] = { 1.0f, 1.0f, 1.0f };
    std::string albedo_uri;
    std::string normal_uri;
    int alpha_mode = 0; // 0 = OPAQUE, 1 = MASK, 2 = BLEND
    float alpha_cutoff = 0.5f;
    std::vector<std::string> detected_extensions;
};

class MeshManager {
public:
    MeshManager() = default;
    ~MeshManager() = default;

    bool initialize(ID3D11Device* device);
    void shutdown();

    [[nodiscard]] const GPUMesh* get_mesh(uint16_t mesh_id) const;
    [[nodiscard]] bool is_mesh_skinned(uint16_t mesh_id) const;
    [[nodiscard]] const GM3D::SkinnedModelData* get_skinned_model(uint16_t mesh_id) const;
    [[nodiscard]] uint32_t get_animation_count(uint16_t mesh_id) const;
    [[nodiscard]] const GM3D::AnimationClip* get_animation(uint16_t mesh_id, uint32_t anim_idx) const;

    // Load custom Wavefront OBJ file
    uint16_t load_obj_from_file(ID3D11Device* device, const std::string& filepath);

    // Load custom glTF 2.0 / GLB binary model
    uint16_t load_glb_from_file(ID3D11Device* device, const std::string& filepath);

    // Universal 3D model loader (.glb, .gltf, .obj)
    uint16_t load_model_from_file(ID3D11Device* device, const std::string& filepath);

    // Extract materials and diagnostic report from glTF 2.0 / GLB
    static bool extract_gltf_materials(const std::string& filepath,
                                       std::vector<GLTFMaterialInfo>& out_materials,
                                       std::string& out_report);

    // Replace a loaded mesh while keeping the destination ID stable.
    bool replace_mesh(uint16_t destination, uint16_t source);

    // Direct mesh registration
    bool set_mesh(ID3D11Device* device, uint16_t mesh_id, 
                  const std::vector<Vertex3D>& vertices, 
                  const std::vector<uint32_t>& indices);

private:
    std::unordered_map<uint16_t, GPUMesh> m_meshes;
    std::unordered_map<uint16_t, GM3D::SkinnedModelData> m_skinned_models;
    uint16_t m_next_custom_mesh_id = 100;

    bool create_gpu_mesh(ID3D11Device* device, uint16_t mesh_id, 
                         const std::vector<Vertex3D>& vertices, 
                         const std::vector<uint32_t>& indices,
                         bool is_skinned = false);

    void generate_cube(ID3D11Device* device);
    void generate_sphere(ID3D11Device* device, uint32_t rings = 20, uint32_t sectors = 20);
    void generate_cylinder(ID3D11Device* device, uint32_t sectors = 24);
    void generate_plane(ID3D11Device* device, float size = 1.0f, float uv_scale = 1.0f);
    void generate_capsule(ID3D11Device* device, float radius = 0.5f, float height = 1.0f);
    void generate_pine_tree(ID3D11Device* device);
};
