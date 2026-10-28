#include "render/TerrainSystem.h"
#include "physics/PhysicsWorld.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstring>
#include <iostream>
#include <vector>
using Microsoft::WRL::ComPtr;

static bool read_texture(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* srv, std::vector<unsigned char>& pixels) {
    if (!srv) return false;
    ComPtr<ID3D11Resource> resource;
    srv->GetResource(&resource);
    ComPtr<ID3D11Texture2D> source;
    if (FAILED(resource.As(&source))) return false;
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    desc.MipLevels = 1; desc.BindFlags = 0; desc.MiscFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source.Get(), 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    pixels.resize(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (uint32_t y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + size_t(y) * desc.Width * 4, static_cast<const unsigned char*>(mapped.pData) + size_t(y) * mapped.RowPitch, desc.Width * 4);
    context->Unmap(staging.Get(), 0);
    return true;
}

static bool read_vertex_buffer(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Buffer* vb, std::vector<Vertex3D>& out_verts) {
    if (!vb) return false;
    D3D11_BUFFER_DESC desc{};
    vb->GetDesc(&desc);
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    if (FAILED(device->CreateBuffer(&desc, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), vb);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    out_verts.resize(desc.ByteWidth / sizeof(Vertex3D));
    std::memcpy(out_verts.data(), mapped.pData, desc.ByteWidth);
    context->Unmap(staging.Get(), 0);
    return true;
}

int main() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
        std::cerr << "Could not create Direct3D WARP device\n"; return 1;
    }
    PhysicsWorld physics;
    if (!physics.initialize()) { std::cerr << "Physics initialization failed\n"; return 1; }
    MeshManager meshes; GM3D::TextureManager textures; GM3D::TerrainSystem terrain;
    GM3D::TerrainConfig cfg; cfg.grid_size = 32; cfg.world_size = 240.0f;
    if (!terrain.generate(device.Get(), context.Get(), meshes, textures, physics, cfg)) {
        std::cerr << "Terrain generation failed\n"; return 1;
    }
    std::vector<unsigned char> before, partial, full;
    if (!read_texture(device.Get(), context.Get(), textures.get_srv(5), before)) return 1;
    auto heights = terrain.heights();
    constexpr uint32_t x = 5, z = 7;
    heights[z * cfg.grid_size + x] += 25.0f;
    const float cell = cfg.world_size / (cfg.grid_size - 1);
    const float world_x = x * cell - cfg.world_size * 0.5f, world_z = z * cell - cfg.world_size * 0.5f;
    if (!terrain.replace_heights(heights) ||
        !terrain.update_mesh_region(device.Get(), context.Get(), meshes, world_x, world_z, world_x, world_z)) {
        std::cerr << "Terrain regional mesh update failed\n"; return 1;
    }

    // Verify regional vertex buffer update matches full mesh rebuild
    std::vector<Vertex3D> vb_partial, vb_full;
    const GPUMesh* gpu_mesh = meshes.get_mesh(GM3D::TerrainSystem::TERRAIN_MESH_ID);
    if (!gpu_mesh || !read_vertex_buffer(device.Get(), context.Get(), gpu_mesh->vertex_buffer.Get(), vb_partial)) {
        std::cerr << "Partial vertex buffer readback failed\n"; return 1;
    }
    if (!terrain.update_mesh(device.Get(), meshes)) {
        std::cerr << "Full mesh update failed\n"; return 1;
    }
    gpu_mesh = meshes.get_mesh(GM3D::TerrainSystem::TERRAIN_MESH_ID);
    if (!gpu_mesh || !read_vertex_buffer(device.Get(), context.Get(), gpu_mesh->vertex_buffer.Get(), vb_full)) {
        std::cerr << "Full vertex buffer readback failed\n"; return 1;
    }
    if (vb_partial.size() != vb_full.size()) {
        std::cerr << "Regional vertex buffer size differs from full mesh rebuild\n"; return 1;
    }
    for (size_t i = 0; i < vb_full.size(); ++i) {
        const auto& a = vb_partial[i];
        const auto& b = vb_full[i];
        if (std::abs(a.x - b.x) > 1e-4f || std::abs(a.y - b.y) > 1e-4f || std::abs(a.z - b.z) > 1e-4f ||
            std::abs(a.nx - b.nx) > 1e-4f || std::abs(a.ny - b.ny) > 1e-4f || std::abs(a.nz - b.nz) > 1e-4f) {
            std::cerr << "Regional vertex buffer update differs from full mesh rebuild at vertex " << i << "\n";
            return 1;
        }
    }

    // Verify regional texture update matches full rebuild
    if (!terrain.update_albedo(device.Get(), context.Get(), textures, world_x, world_z, world_x, world_z) ||
        !read_texture(device.Get(), context.Get(), textures.get_srv(5), partial) ||
        !terrain.update_albedo(device.Get(), context.Get(), textures, 0, 0, 0, 0, true) ||
        !read_texture(device.Get(), context.Get(), textures.get_srv(5), full)) {
        std::cerr << "Terrain texture update/readback failed\n"; return 1;
    }
    if (before == partial) { std::cerr << "Terrain edit did not change texture\n"; return 1; }
    if (partial != full) { std::cerr << "Partial texture update differs from full rebuild\n"; return 1; }

    // Verify splatmap layer painting alters albedo texture
    std::vector<uint8_t> splat(cfg.grid_size * cfg.grid_size * 4, 0);
    splat[(z * cfg.grid_size + x) * 4 + 2] = 255; // Paint Layer 2 (Sand) at (x, z)
    terrain.set_splatmap(splat);
    std::vector<unsigned char> painted;
    if (!terrain.update_albedo(device.Get(), context.Get(), textures, world_x, world_z, world_x, world_z) ||
        !read_texture(device.Get(), context.Get(), textures.get_srv(5), painted)) {
        std::cerr << "Painted terrain texture readback failed\n"; return 1;
    }
    if (painted == full) { std::cerr << "Terrain splatmap paint did not change texture\n"; return 1; }

    terrain.clear(physics); physics.shutdown();
    std::cout << "Terrain mesh & texture: regional updates and splatmap painting verified OK\n";
    return 0;
}
