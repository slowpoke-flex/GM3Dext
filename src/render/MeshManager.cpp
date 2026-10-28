#include "MeshManager.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <algorithm>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool MeshManager::initialize(ID3D11Device* device) {
    if (!device) return false;

    generate_cube(device);     // ID = 0
    generate_sphere(device);   // ID = 1
    generate_cylinder(device); // ID = 2
    generate_plane(device, 1.0f, 25.0f);    // ID = 3
    generate_capsule(device);  // ID = 4
    generate_pine_tree(device);// ID = 6 (Stylized Low-Poly Pine Tree)

    return true;
}

void MeshManager::shutdown() {
    m_meshes.clear();
}

const GPUMesh* MeshManager::get_mesh(uint16_t mesh_id) const {
    auto it = m_meshes.find(mesh_id);
    if (it != m_meshes.end()) {
        return &it->second;
    }
    // Fallback to cube if mesh not found
    it = m_meshes.find(0);
    return it != m_meshes.end() ? &it->second : nullptr;
}

bool MeshManager::create_gpu_mesh(ID3D11Device* device, uint16_t mesh_id,
                                  const std::vector<Vertex3D>& vertices,
                                  const std::vector<uint32_t>& indices,
                                  bool is_skinned) {
    if (!device || vertices.empty() || indices.empty()) return false;

    GPUMesh mesh{};
    mesh.vertex_count = static_cast<uint32_t>(vertices.size());
    mesh.index_count = static_cast<uint32_t>(indices.size());
    mesh.is_skinned = is_skinned;

    // Vertex Buffer
    D3D11_BUFFER_DESC vbd{};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = static_cast<UINT>(sizeof(Vertex3D) * vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vData{};
    vData.pSysMem = vertices.data();

    HRESULT hr = device->CreateBuffer(&vbd, &vData, mesh.vertex_buffer.GetAddressOf());
    if (FAILED(hr)) return false;

    // Index Buffer
    D3D11_BUFFER_DESC ibd{};
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA iData{};
    iData.pSysMem = indices.data();

    hr = device->CreateBuffer(&ibd, &iData, mesh.index_buffer.GetAddressOf());
    if (FAILED(hr)) return false;

    m_meshes[mesh_id] = mesh;
    return true;
}

bool MeshManager::set_mesh(ID3D11Device* device, uint16_t mesh_id,
                           const std::vector<Vertex3D>& vertices,
                           const std::vector<uint32_t>& indices) {
    return create_gpu_mesh(device, mesh_id, vertices, indices, false);
}

bool MeshManager::update_mesh_vertices(ID3D11DeviceContext* context, uint16_t mesh_id,
                                       const std::vector<Vertex3D>& vertices,
                                       uint32_t start_vertex, uint32_t vertex_count) {
    if (!context || vertices.empty()) return false;
    auto it = m_meshes.find(mesh_id);
    if (it == m_meshes.end() || !it->second.vertex_buffer) return false;
    if (vertices.size() != it->second.vertex_count) return false;

    if (vertex_count == 0 || (start_vertex == 0 && vertex_count >= it->second.vertex_count)) {
        context->UpdateSubresource(it->second.vertex_buffer.Get(), 0, nullptr, vertices.data(), 0, 0);
        return true;
    }
    if (start_vertex >= it->second.vertex_count) return false;
    const uint32_t end_vertex = std::min(it->second.vertex_count, start_vertex + vertex_count);
    D3D11_BOX box{};
    box.left = start_vertex * static_cast<UINT>(sizeof(Vertex3D));
    box.right = end_vertex * static_cast<UINT>(sizeof(Vertex3D));
    box.top = 0;
    box.bottom = 1;
    box.front = 0;
    box.back = 1;
    context->UpdateSubresource(it->second.vertex_buffer.Get(), 0, &box, vertices.data() + start_vertex, 0, 0);
    return true;
}

void MeshManager::generate_cube(ID3D11Device* device) {
    // 24 vertices (4 per face) with proper normals and UVs
    std::vector<Vertex3D> vertices = {
        // Front Face (Z+)
        {-0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f},
        { 0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f},
        { 0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f},
        {-0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f},
        // Back Face (Z-)
        { 0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f},
        {-0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f},
        {-0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f},
        { 0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f},
        // Top Face (Y+)
        {-0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f},
        { 0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f},
        { 0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f},
        {-0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f},
        // Bottom Face (Y-)
        {-0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f},
        { 0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f},
        { 0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f},
        {-0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f},
        // Right Face (X+)
        { 0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f},
        { 0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f},
        { 0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f},
        { 0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f},
        // Left Face (X-)
        {-0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f},
        {-0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f},
        {-0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f},
        {-0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f}
    };

    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t i = 0; i < 6; ++i) {
        uint32_t base = i * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    create_gpu_mesh(device, 0, vertices, indices);
}

void MeshManager::generate_sphere(ID3D11Device* device, uint32_t rings, uint32_t sectors) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    float const R = 1.0f / (float)(rings - 1);
    float const S = 1.0f / (float)(sectors - 1);
    constexpr float radius = 0.5f;

    vertices.resize(rings * sectors);
    auto v = vertices.begin();
    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            float const y = sinf(-static_cast<float>(M_PI) / 2.0f + static_cast<float>(M_PI) * r * R);
            float const x = cosf(2.0f * static_cast<float>(M_PI) * s * S) * sinf(static_cast<float>(M_PI) * r * R);
            float const z = sinf(2.0f * static_cast<float>(M_PI) * s * S) * sinf(static_cast<float>(M_PI) * r * R);

            v->x = x * radius;
            v->y = y * radius;
            v->z = z * radius;

            v->nx = x;
            v->ny = y;
            v->nz = z;

            v->u = s * S;
            v->v = r * R;
            ++v;
        }
    }

    indices.resize(rings * sectors * 6);
    auto i = indices.begin();
    for (uint32_t r = 0; r < rings - 1; ++r) {
        for (uint32_t s = 0; s < sectors - 1; ++s) {
            uint32_t cur = r * sectors + s;
            uint32_t next = (r + 1) * sectors + s;

            *i++ = cur;
            *i++ = next;
            *i++ = next + 1;

            *i++ = cur;
            *i++ = next + 1;
            *i++ = cur + 1;
        }
    }

    create_gpu_mesh(device, 1, vertices, indices);
}

void MeshManager::generate_cylinder(ID3D11Device* device, uint32_t sectors) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    constexpr float radius = 0.5f;
    constexpr float half_h = 0.5f;

    // 1. Side wall
    uint32_t side_start = static_cast<uint32_t>(vertices.size());
    for (uint32_t s = 0; s <= sectors; ++s) {
        float angle = (float)s / (float)sectors * 2.0f * (float)M_PI;
        float x = cosf(angle);
        float z = sinf(angle);
        float u = (float)s / (float)sectors;

        // Top edge
        vertices.push_back({x * radius,  half_h, z * radius, x, 0.0f, z, u, 0.0f});
        // Bottom edge
        vertices.push_back({x * radius, -half_h, z * radius, x, 0.0f, z, u, 1.0f});
    }

    for (uint32_t s = 0; s < sectors; ++s) {
        uint32_t i0 = side_start + s * 2;       // Top-Left
        uint32_t i1 = i0 + 1;                   // Bottom-Left
        uint32_t i2 = i0 + 2;                   // Top-Right
        uint32_t i3 = i0 + 3;                   // Bottom-Right

        // Match sphere clockwise winding: (top-left -> bottom-left -> bottom-right)
        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i3);

        // (top-left -> bottom-right -> top-right)
        indices.push_back(i0);
        indices.push_back(i3);
        indices.push_back(i2);
    }

    // 2. Top cap (Facing +Y)
    uint32_t top_center_idx = static_cast<uint32_t>(vertices.size());
    vertices.push_back({0.0f, half_h, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f});
    uint32_t top_edge_start = static_cast<uint32_t>(vertices.size());
    for (uint32_t s = 0; s <= sectors; ++s) {
        float angle = (float)s / (float)sectors * 2.0f * (float)M_PI;
        float x = cosf(angle);
        float z = sinf(angle);
        vertices.push_back({x * radius, half_h, z * radius, 0.0f, 1.0f, 0.0f, x * 0.5f + 0.5f, z * 0.5f + 0.5f});
    }
    for (uint32_t s = 0; s < sectors; ++s) {
        // Clockwise from +Y looking down: Center -> (s+1) -> s
        indices.push_back(top_center_idx);
        indices.push_back(top_edge_start + s + 1);
        indices.push_back(top_edge_start + s);
    }

    // 3. Bottom cap (Facing -Y)
    uint32_t bot_center_idx = static_cast<uint32_t>(vertices.size());
    vertices.push_back({0.0f, -half_h, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f});
    uint32_t bot_edge_start = static_cast<uint32_t>(vertices.size());
    for (uint32_t s = 0; s <= sectors; ++s) {
        float angle = (float)s / (float)sectors * 2.0f * (float)M_PI;
        float x = cosf(angle);
        float z = sinf(angle);
        vertices.push_back({x * radius, -half_h, z * radius, 0.0f, -1.0f, 0.0f, x * 0.5f + 0.5f, z * 0.5f + 0.5f});
    }
    for (uint32_t s = 0; s < sectors; ++s) {
        // Clockwise from -Y looking up: Center -> s -> (s+1)
        indices.push_back(bot_center_idx);
        indices.push_back(bot_edge_start + s);
        indices.push_back(bot_edge_start + s + 1);
    }

    create_gpu_mesh(device, 2, vertices, indices);
}

void MeshManager::generate_plane(ID3D11Device* device, float size, float uv_scale) {
    float half = size * 0.5f;
    std::vector<Vertex3D> vertices = {
        {-half, 0.0f,  half,  0.0f, 1.0f, 0.0f,  0.0f, uv_scale},
        { half, 0.0f,  half,  0.0f, 1.0f, 0.0f,  uv_scale, uv_scale},
        { half, 0.0f, -half,  0.0f, 1.0f, 0.0f,  uv_scale, 0.0f},
        {-half, 0.0f, -half,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f}
    };

    std::vector<uint32_t> indices = {
        0, 1, 2,
        0, 2, 3
    };

    create_gpu_mesh(device, 3, vertices, indices);
}

void MeshManager::generate_capsule(ID3D11Device* device, float radius, float height) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    const uint32_t rings = 8;
    const uint32_t sectors = 16;
    const float half_h = height * 0.5f;

    // 1. Top hemisphere (rings from 0 to rings/2)
    for (uint32_t r = 0; r <= rings; ++r) {
        float lat = ((float)r / (float)rings) * ((float)M_PI * 0.5f); // 0 to PI/2
        float y = sinf(lat);
        float cosLat = cosf(lat);

        for (uint32_t s = 0; s <= sectors; ++s) {
            float lon = ((float)s / (float)sectors) * 2.0f * (float)M_PI;
            float x = cosLat * cosf(lon);
            float z = cosLat * sinf(lon);

            float vx = x * radius;
            float vy = half_h + y * radius;
            float vz = z * radius;

            vertices.push_back({vx, vy, vz, x, y, z, (float)s / sectors, (float)r / (rings * 2)});
        }
    }

    // 2. Bottom hemisphere (rings from 0 to rings/2)
    for (uint32_t r = 0; r <= rings; ++r) {
        float lat = ((float)r / (float)rings) * ((float)M_PI * 0.5f); // 0 to PI/2
        float y = -sinf(lat);
        float cosLat = cosf(lat);

        for (uint32_t s = 0; s <= sectors; ++s) {
            float lon = ((float)s / (float)sectors) * 2.0f * (float)M_PI;
            float x = cosLat * cosf(lon);
            float z = cosLat * sinf(lon);

            float vx = x * radius;
            float vy = -half_h + y * radius;
            float vz = z * radius;

            vertices.push_back({vx, vy, vz, x, y, z, (float)s / sectors, 0.5f + (float)r / (rings * 2)});
        }
    }

    uint32_t stride = sectors + 1;
    // Top hemisphere indices (CW)
    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            uint32_t cur = r * stride + s;
            uint32_t next = (r + 1) * stride + s;
            indices.push_back(cur);
            indices.push_back(next);
            indices.push_back(next + 1);

            indices.push_back(cur);
            indices.push_back(next + 1);
            indices.push_back(cur + 1);
        }
    }

    // Cylinder body joining the two hemispheres (CW)
    uint32_t bot_offset = stride * (rings + 1);
    for (uint32_t s = 0; s < sectors; ++s) {
        uint32_t top_ring = s;
        uint32_t bot_ring = bot_offset + s;

        indices.push_back(top_ring);
        indices.push_back(bot_ring);
        indices.push_back(bot_ring + 1);

        indices.push_back(top_ring);
        indices.push_back(bot_ring + 1);
        indices.push_back(top_ring + 1);
    }

    // Bottom hemisphere indices (CW)
    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            uint32_t cur = bot_offset + r * stride + s;
            uint32_t next = bot_offset + (r + 1) * stride + s;
            indices.push_back(cur);
            indices.push_back(next);
            indices.push_back(next + 1);

            indices.push_back(cur);
            indices.push_back(next + 1);
            indices.push_back(cur + 1);
        }
    }

    create_gpu_mesh(device, 4, vertices, indices);
}

uint16_t MeshManager::load_obj_from_file(ID3D11Device* device, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return 0;

    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<Vertex3D> out_vertices;
    std::vector<uint32_t> out_indices;

    std::string line;
    while (std::getline(file, line)) {
        if (line.substr(0, 2) == "v ") {
            std::istringstream s(line.substr(2));
            float x, y, z;
            s >> x >> y >> z;
            positions.push_back(x); positions.push_back(y); positions.push_back(z);
        } else if (line.substr(0, 3) == "vn ") {
            std::istringstream s(line.substr(3));
            float x, y, z;
            s >> x >> y >> z;
            normals.push_back(x); normals.push_back(y); normals.push_back(z);
        } else if (line.substr(0, 3) == "vt ") {
            std::istringstream s(line.substr(3));
            float u, v;
            s >> u >> v;
            uvs.push_back(u); uvs.push_back(v);
        } else if (line.substr(0, 2) == "f ") {
            std::istringstream s(line.substr(2));
            std::string vStr;
            uint32_t faceIndices[3];
            for (int i = 0; i < 3; ++i) {
                s >> vStr;
                std::istringstream vStream(vStr);
                std::string pIdxStr, tIdxStr, nIdxStr;
                std::getline(vStream, pIdxStr, '/');
                std::getline(vStream, tIdxStr, '/');
                std::getline(vStream, nIdxStr, '/');

                int pIdx = (!pIdxStr.empty()) ? std::stoi(pIdxStr) - 1 : 0;
                int tIdx = (!tIdxStr.empty()) ? std::stoi(tIdxStr) - 1 : -1;
                int nIdx = (!nIdxStr.empty()) ? std::stoi(nIdxStr) - 1 : -1;

                Vertex3D v{};
                if (pIdx * 3 + 2 < (int)positions.size()) {
                    v.x = positions[pIdx * 3 + 0];
                    v.y = positions[pIdx * 3 + 1];
                    v.z = positions[pIdx * 3 + 2];
                }
                if (nIdx >= 0 && nIdx * 3 + 2 < (int)normals.size()) {
                    v.nx = normals[nIdx * 3 + 0];
                    v.ny = normals[nIdx * 3 + 1];
                    v.nz = normals[nIdx * 3 + 2];
                } else {
                    v.ny = 1.0f;
                }
                if (tIdx >= 0 && tIdx * 2 + 1 < (int)uvs.size()) {
                    v.u = uvs[tIdx * 2 + 0];
                    v.v = uvs[tIdx * 2 + 1];
                }

                uint32_t idx = static_cast<uint32_t>(out_vertices.size());
                out_vertices.push_back(v);
                faceIndices[i] = idx;
            }
            out_indices.push_back(faceIndices[0]);
            out_indices.push_back(faceIndices[1]);
            out_indices.push_back(faceIndices[2]);
        }
    }

    if (out_vertices.empty() || out_indices.empty()) return 0;

    uint16_t id = m_next_custom_mesh_id++;
    create_gpu_mesh(device, id, out_vertices, out_indices);
    return id;
}

uint16_t MeshManager::load_glb_from_file(ID3D11Device* device, const std::string& filepath) {
    cgltf_options options{};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);
    if (result != cgltf_result_success || data == nullptr) {
        return 0;
    }

    result = cgltf_load_buffers(&options, data, filepath.c_str());
    if (result != cgltf_result_success) {
        cgltf_free(data);
        return 0;
    }

    bool has_skin = (data->skins_count > 0 && data->skins[0].joints_count > 0);
    // The vertex shader's bone constant buffer has 128 entries. Reject models
    // that cannot be represented rather than indexing outside that buffer.
    if (has_skin && data->skins[0].joints_count > 128) {
        cgltf_free(data);
        return 0;
    }
    std::unordered_map<const cgltf_node*, int> node_to_joint;

    GM3D::SkinnedModelData skinned_model;
    if (has_skin) {
        const auto& skin = data->skins[0];
        skinned_model.skeleton.name = skin.name ? skin.name : "Armature";

        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            node_to_joint[skin.joints[j]] = static_cast<int>(j);
        }

        skinned_model.skeleton.joints.resize(skin.joints_count);
        skinned_model.skeleton.inverse_bind_matrices.resize(skin.joints_count);

        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            const cgltf_node* node = skin.joints[j];
            auto& jnode = skinned_model.skeleton.joints[j];
            jnode.name = node->name ? node->name : ("Joint_" + std::to_string(j));

            if (node->parent && node_to_joint.count(node->parent)) {
                jnode.parent_index = node_to_joint[node->parent];
            } else {
                jnode.parent_index = -1;
            }

            if (node->has_translation) {
                jnode.base_translation = XMFLOAT3(node->translation[0], node->translation[1], node->translation[2]);
            } else {
                jnode.base_translation = XMFLOAT3(0, 0, 0);
            }

            if (node->has_rotation) {
                jnode.base_rotation = XMFLOAT4(node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]);
            } else {
                jnode.base_rotation = XMFLOAT4(0, 0, 0, 1);
            }

            if (node->has_scale) {
                jnode.base_scale = XMFLOAT3(node->scale[0], node->scale[1], node->scale[2]);
            } else {
                jnode.base_scale = XMFLOAT3(1, 1, 1);
            }

            if (skin.inverse_bind_matrices) {
                float m[16];
                cgltf_accessor_read_float(skin.inverse_bind_matrices, j, m, 16);
                memcpy(&skinned_model.skeleton.inverse_bind_matrices[j], m, sizeof(float) * 16);
            } else {
                XMStoreFloat4x4(&skinned_model.skeleton.inverse_bind_matrices[j], XMMatrixIdentity());
            }
        }

        // Parse animations
        for (cgltf_size a = 0; a < data->animations_count; ++a) {
            const auto& anim = data->animations[a];
            GM3D::AnimationClip clip;
            clip.name = anim.name ? anim.name : ("Anim_" + std::to_string(a));

            for (cgltf_size c = 0; c < anim.channels_count; ++c) {
                const auto& ch = anim.channels[c];
                if (!ch.target_node || !node_to_joint.count(ch.target_node)) continue;
                if (!ch.sampler || !ch.sampler->input || !ch.sampler->output) continue;

                GM3D::AnimChannel channel;
                channel.joint_index = node_to_joint[ch.target_node];

                if (ch.target_path == cgltf_animation_path_type_translation) {
                    channel.path = GM3D::AnimPathType::Translation;
                } else if (ch.target_path == cgltf_animation_path_type_rotation) {
                    channel.path = GM3D::AnimPathType::Rotation;
                } else if (ch.target_path == cgltf_animation_path_type_scale) {
                    channel.path = GM3D::AnimPathType::Scale;
                } else {
                    continue;
                }

                cgltf_size key_count = ch.sampler->input->count;
                channel.times.resize(key_count);
                for (cgltf_size k = 0; k < key_count; ++k) {
                    cgltf_accessor_read_float(ch.sampler->input, k, &channel.times[k], 1);
                    clip.duration = std::max(clip.duration, channel.times[k]);
                }

                if (channel.path == GM3D::AnimPathType::Rotation) {
                    channel.quat_keys.resize(key_count);
                    for (cgltf_size k = 0; k < key_count; ++k) {
                        float q[4];
                        cgltf_accessor_read_float(ch.sampler->output, k, q, 4);
                        channel.quat_keys[k] = XMFLOAT4(q[0], q[1], q[2], q[3]);
                    }
                } else {
                    channel.vec3_keys.resize(key_count);
                    for (cgltf_size k = 0; k < key_count; ++k) {
                        float v[3];
                        cgltf_accessor_read_float(ch.sampler->output, k, v, 3);
                        channel.vec3_keys[k] = XMFLOAT3(v[0], v[1], v[2]);
                    }
                }

                clip.channels.push_back(std::move(channel));
            }

            skinned_model.animations.push_back(std::move(clip));
        }
    }

    std::vector<Vertex3D> out_vertices;
    std::vector<uint32_t> out_indices;

    auto process_primitive = [&](const cgltf_primitive& prim, const float* transform) {
        if (prim.type != cgltf_primitive_type_triangles) return;

        const cgltf_accessor* pos_acc = nullptr;
        const cgltf_accessor* norm_acc = nullptr;
        const cgltf_accessor* uv_acc = nullptr;
        const cgltf_accessor* joints_acc = nullptr;
        const cgltf_accessor* weights_acc = nullptr;

        for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
            const auto& attr = prim.attributes[i];
            if (attr.type == cgltf_attribute_type_position) pos_acc = attr.data;
            else if (attr.type == cgltf_attribute_type_normal) norm_acc = attr.data;
            else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uv_acc = attr.data;
            else if (attr.type == cgltf_attribute_type_joints && attr.index == 0) joints_acc = attr.data;
            else if (attr.type == cgltf_attribute_type_weights && attr.index == 0) weights_acc = attr.data;
        }

        if (!pos_acc) return;

        uint32_t base_index = static_cast<uint32_t>(out_vertices.size());
        cgltf_size vert_count = pos_acc->count;

        for (cgltf_size v = 0; v < vert_count; ++v) {
            Vertex3D vert{};
            float p[3] = {0, 0, 0};
            cgltf_accessor_read_float(pos_acc, v, p, 3);

            if (transform && !has_skin) {
                vert.x = transform[0] * p[0] + transform[4] * p[1] + transform[8] * p[2] + transform[12];
                vert.y = transform[1] * p[0] + transform[5] * p[1] + transform[9] * p[2] + transform[13];
                vert.z = transform[2] * p[0] + transform[6] * p[1] + transform[10] * p[2] + transform[14];
            } else {
                vert.x = p[0];
                vert.y = p[1];
                vert.z = p[2];
            }

            if (norm_acc) {
                float n[3] = {0, 1, 0};
                cgltf_accessor_read_float(norm_acc, v, n, 3);
                if (transform && !has_skin) {
                    vert.nx = transform[0] * n[0] + transform[4] * n[1] + transform[8] * n[2];
                    vert.ny = transform[1] * n[0] + transform[5] * n[1] + transform[9] * n[2];
                    vert.nz = transform[2] * n[0] + transform[6] * n[1] + transform[10] * n[2];
                    float len = sqrtf(vert.nx*vert.nx + vert.ny*vert.ny + vert.nz*vert.nz);
                    if (len > 0.0001f) { vert.nx /= len; vert.ny /= len; vert.nz /= len; }
                } else {
                    vert.nx = n[0];
                    vert.ny = n[1];
                    vert.nz = n[2];
                }
            } else {
                vert.ny = 1.0f;
            }

            if (uv_acc) {
                float uv[2] = {0, 0};
                cgltf_accessor_read_float(uv_acc, v, uv, 2);
                vert.u = uv[0];
                vert.v = uv[1];
            }

            if (joints_acc) {
                cgltf_accessor_read_uint(joints_acc, v, vert.joints, 4);
            } else {
                vert.joints[0] = vert.joints[1] = vert.joints[2] = vert.joints[3] = 0;
            }

            if (weights_acc) {
                cgltf_accessor_read_float(weights_acc, v, vert.weights, 4);
            } else {
                vert.weights[0] = 1.0f; vert.weights[1] = vert.weights[2] = vert.weights[3] = 0.0f;
            }

            out_vertices.push_back(vert);
        }

        // Indices
        if (prim.indices) {
            cgltf_size idx_count = prim.indices->count;
            for (cgltf_size k = 0; k < idx_count; ++k) {
                cgltf_size idx = cgltf_accessor_read_index(prim.indices, k);
                out_indices.push_back(base_index + static_cast<uint32_t>(idx));
            }
        } else {
            for (cgltf_size k = 0; k < vert_count; ++k) {
                out_indices.push_back(base_index + static_cast<uint32_t>(k));
            }
        }
    };

    if (data->nodes_count > 0) {
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            const cgltf_node& node = data->nodes[i];
            if (node.mesh) {
                float world_transform[16];
                cgltf_node_transform_world(&node, world_transform);
                for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p) {
                    process_primitive(node.mesh->primitives[p], world_transform);
                }
            }
        }
    } else {
        for (cgltf_size m = 0; m < data->meshes_count; ++m) {
            for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
                process_primitive(data->meshes[m].primitives[p], nullptr);
            }
        }
    }

    cgltf_free(data);

    if (out_vertices.empty() || out_indices.empty()) {
        return 0;
    }

    uint16_t id = m_next_custom_mesh_id++;
    create_gpu_mesh(device, id, out_vertices, out_indices, has_skin);

    if (has_skin) {
        m_skinned_models[id] = std::move(skinned_model);
    }

    return id;
}

uint16_t MeshManager::load_model_from_file(ID3D11Device* device, const std::string& filepath) {
    std::string ext = "";
    size_t dot_pos = filepath.find_last_of('.');
    if (dot_pos != std::string::npos) {
        ext = filepath.substr(dot_pos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    if (ext == ".glb" || ext == ".gltf") {
        return load_glb_from_file(device, filepath);
    } else {
        return load_obj_from_file(device, filepath);
    }
}

bool MeshManager::replace_mesh(uint16_t destination, uint16_t source) {
    auto target = m_meshes.find(destination);
    auto incoming = m_meshes.find(source);
    if (destination == source || target == m_meshes.end() || incoming == m_meshes.end()) return false;
    target->second = std::move(incoming->second);
    auto skinned_target = m_skinned_models.find(destination);
    auto skinned_incoming = m_skinned_models.find(source);
    if (skinned_incoming != m_skinned_models.end()) {
        m_skinned_models[destination] = std::move(skinned_incoming->second);
        m_skinned_models.erase(skinned_incoming);
    } else if (skinned_target != m_skinned_models.end()) {
        m_skinned_models.erase(skinned_target);
    }
    m_meshes.erase(incoming);
    return true;
}

bool MeshManager::extract_gltf_materials(const std::string& filepath,
                                         std::vector<GLTFMaterialInfo>& out_materials,
                                         std::string& out_report) {
    cgltf_options options{};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);
    if (result != cgltf_result_success || data == nullptr) {
        out_report = "Failed to parse glTF file for materials.";
        return false;
    }

    result = cgltf_load_buffers(&options, data, filepath.c_str());

    std::stringstream ss;
    const auto filePathObj = std::filesystem::path(filepath);
    ss << "glTF Material Import Report for: " << filePathObj.filename().string() << "\n";

    if (data->materials_count == 0) {
        ss << "- No embedded materials found in file. Using default PBR material.\n";
    }

    const auto model_dir = filePathObj.parent_path();

    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const auto& mat = data->materials[i];
        GLTFMaterialInfo info;
        info.name = (mat.name && mat.name[0]) ? mat.name : ("Material_" + std::to_string(i + 1));

        if (mat.has_pbr_metallic_roughness) {
            info.base_color[0] = mat.pbr_metallic_roughness.base_color_factor[0];
            info.base_color[1] = mat.pbr_metallic_roughness.base_color_factor[1];
            info.base_color[2] = mat.pbr_metallic_roughness.base_color_factor[2];
            info.base_color[3] = mat.pbr_metallic_roughness.base_color_factor[3];
            info.metallic = mat.pbr_metallic_roughness.metallic_factor;
            info.roughness = mat.pbr_metallic_roughness.roughness_factor;

            if (mat.pbr_metallic_roughness.base_color_texture.texture &&
                mat.pbr_metallic_roughness.base_color_texture.texture->image) {
                const auto* img = mat.pbr_metallic_roughness.base_color_texture.texture->image;
                if (img->uri && img->uri[0]) {
                    info.albedo_uri = (model_dir / std::filesystem::path(img->uri)).string();
                } else if (img->buffer_view) {
                    ss << "- [" << info.name << "] Albedo texture is embedded binary in GLB buffer. (Export textures to disk for direct loading).\n";
                }
            }
        }

        if (mat.normal_texture.texture && mat.normal_texture.texture->image) {
            const auto* img = mat.normal_texture.texture->image;
            if (img->uri && img->uri[0]) {
                info.normal_uri = (model_dir / std::filesystem::path(img->uri)).string();
            }
        }

        info.emissive_color[0] = mat.emissive_factor[0];
        info.emissive_color[1] = mat.emissive_factor[1];
        info.emissive_color[2] = mat.emissive_factor[2];
        info.emissive = std::max({ mat.emissive_factor[0], mat.emissive_factor[1], mat.emissive_factor[2] });

        if (mat.alpha_mode == cgltf_alpha_mode_opaque) info.alpha_mode = 0;
        else if (mat.alpha_mode == cgltf_alpha_mode_mask) { info.alpha_mode = 1; info.alpha_cutoff = mat.alpha_cutoff; }
        else if (mat.alpha_mode == cgltf_alpha_mode_blend) info.alpha_mode = 2;

        if (mat.has_clearcoat) info.detected_extensions.push_back("KHR_materials_clearcoat");
        if (mat.has_transmission) info.detected_extensions.push_back("KHR_materials_transmission");
        if (mat.has_sheen) info.detected_extensions.push_back("KHR_materials_sheen");
        if (mat.has_ior) info.detected_extensions.push_back("KHR_materials_ior");
        if (mat.has_volume) info.detected_extensions.push_back("KHR_materials_volume");

        ss << "- Material '" << info.name << "': Albedo (" 
           << std::fixed << std::setprecision(2)
           << info.base_color[0] << ", " << info.base_color[1] << ", " << info.base_color[2] 
           << "), Roughness " << info.roughness << ", Metallic " << info.metallic << "\n";

        if (!info.detected_extensions.empty()) {
            ss << "  * Note: Extensions [";
            for (size_t extIdx = 0; extIdx < info.detected_extensions.size(); ++extIdx) {
                ss << (extIdx > 0 ? ", " : "") << info.detected_extensions[extIdx];
            }
            ss << "] approximated by standard PBR pipeline.\n";
        }

        out_materials.push_back(info);
    }

    cgltf_free(data);
    out_report = ss.str();
    return true;
}

bool MeshManager::is_mesh_skinned(uint16_t mesh_id) const {
    auto it = m_meshes.find(mesh_id);
    return (it != m_meshes.end()) ? it->second.is_skinned : false;
}

const GM3D::SkinnedModelData* MeshManager::get_skinned_model(uint16_t mesh_id) const {
    auto it = m_skinned_models.find(mesh_id);
    return (it != m_skinned_models.end()) ? &it->second : nullptr;
}

uint32_t MeshManager::get_animation_count(uint16_t mesh_id) const {
    auto it = m_skinned_models.find(mesh_id);
    return (it != m_skinned_models.end()) ? static_cast<uint32_t>(it->second.animations.size()) : 0;
}

const GM3D::AnimationClip* MeshManager::get_animation(uint16_t mesh_id, uint32_t anim_idx) const {
    auto it = m_skinned_models.find(mesh_id);
    if (it != m_skinned_models.end()) {
        return it->second.get_clip(anim_idx);
    }
    return nullptr;
}

void MeshManager::generate_pine_tree(ID3D11Device* device) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    constexpr uint32_t segments = 6;
    constexpr float trunk_radius = 0.22f;
    constexpr float trunk_height = 1.0f;

    // 1. Trunk (Cylinder)
    for (uint32_t s = 0; s <= segments; ++s) {
        float angle = (float)s / (float)segments * 2.0f * (float)M_PI;
        float x = cosf(angle);
        float z = sinf(angle);
        float u = (float)s / (float)segments;

        // Bottom vertex (Y = 0)
        vertices.push_back({x * trunk_radius, 0.0f, z * trunk_radius, x, 0.0f, z, u, 0.02f});
        // Top vertex (Y = trunk_height)
        vertices.push_back({x * trunk_radius, trunk_height, z * trunk_radius, x, 0.0f, z, u, 0.08f});
    }

    for (uint32_t s = 0; s < segments; ++s) {
        uint32_t b = s * 2;
        indices.push_back(b + 0);
        indices.push_back(b + 1);
        indices.push_back(b + 2);

        indices.push_back(b + 1);
        indices.push_back(b + 3);
        indices.push_back(b + 2);
    }

    // 2. Three Conical Foliage Tiers
    struct Tier {
        float base_y;
        float apex_y;
        float radius;
        float v_coord;
    };

    Tier tiers[3] = {
        { 0.75f, 2.30f, 1.70f, 0.35f }, // Bottom tier
        { 1.65f, 3.35f, 1.30f, 0.65f }, // Middle tier
        { 2.65f, 4.40f, 0.90f, 0.95f }  // Top tier
    };

    for (int t = 0; t < 3; ++t) {
        const auto& tier = tiers[t];
        uint32_t base_idx = static_cast<uint32_t>(vertices.size());

        // Base ring vertices
        for (uint32_t s = 0; s <= segments; ++s) {
            float angle = (float)s / (float)segments * 2.0f * (float)M_PI;
            float x = cosf(angle);
            float z = sinf(angle);
            float u = (float)s / (float)segments;

            // Sloped normal for cone face
            float nx = x * 0.85f;
            float ny = 0.52f;
            float nz = z * 0.85f;

            vertices.push_back({x * tier.radius, tier.base_y, z * tier.radius, nx, ny, nz, u, tier.v_coord});
        }

        // Apex vertex
        uint32_t apex_idx = static_cast<uint32_t>(vertices.size());
        vertices.push_back({0.0f, tier.apex_y, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, tier.v_coord});

        // Conical side faces
        for (uint32_t s = 0; s < segments; ++s) {
            indices.push_back(base_idx + s);
            indices.push_back(apex_idx);
            indices.push_back(base_idx + s + 1);
        }

        // Bottom cap center
        uint32_t cap_center = static_cast<uint32_t>(vertices.size());
        vertices.push_back({0.0f, tier.base_y, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, tier.v_coord});
        for (uint32_t s = 0; s < segments; ++s) {
            indices.push_back(cap_center);
            indices.push_back(base_idx + s + 1);
            indices.push_back(base_idx + s);
        }
    }

    create_gpu_mesh(device, 6, vertices, indices);
}
