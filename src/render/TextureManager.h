#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace GM3D {

struct TextureResource {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11Texture2D> normal_texture;
    ComPtr<ID3D11ShaderResourceView> normal_srv;
    // Scalar (R8) roughness map, sampled in .r by the terrain detail blend.
    ComPtr<ID3D11ShaderResourceView> rough_srv;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string name;
};

class TextureManager {
public:
    TextureManager() = default;
    ~TextureManager() = default;

    bool init(ID3D11Device* device, ID3D11DeviceContext* context);
    void cleanup();

    uint16_t load_texture_from_file(ID3D11Device* device, ID3D11DeviceContext* context, const std::string& filepath);
    uint16_t load_texture_from_memory(ID3D11Device* device, ID3D11DeviceContext* context, const uint8_t* data, size_t size, const std::string& name = "");

    // Loads the CC0 PBR sets shipped in assets/textures into the reserved TexId slots.
    // Returns how many sets were actually found; 0 means the build runs on the
    // procedural textures only. Safe to call when the folder is absent.
    uint32_t load_builtin_texture_sets(ID3D11Device* device, ID3D11DeviceContext* context);
    // Directory holding the texture sets, or an empty string when none was found.
    static std::string find_asset_dir();
    uint16_t create_rgba_texture(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width, uint32_t height, const uint8_t* rgba_data, const std::string& name = "", bool generate_mips = true);
    bool set_texture_rgba(uint16_t texture_id, ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width, uint32_t height, const uint8_t* rgba_data, const std::string& name = "", bool generate_mips = true);
    bool update_texture_region(uint16_t texture_id, ID3D11DeviceContext* context, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const uint8_t* rgba_data);
    bool set_normal_map(uint16_t texture_id, ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width, uint32_t height, const uint8_t* normal_rgba_data);
    bool set_normal_map_texture(uint16_t target_tex_id, uint16_t normal_tex_id);
    // Stores a grayscale roughness map for the terrain detail blend (R channel is used).
    bool set_roughness_map(uint16_t texture_id, ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width, uint32_t height, const uint8_t* rough_rgba_data);

    ID3D11ShaderResourceView* get_srv(uint16_t texture_id) const;
    ID3D11ShaderResourceView* get_normal_srv(uint16_t texture_id) const;
    ID3D11ShaderResourceView* get_rough_srv(uint16_t texture_id) const;
    ID3D11SamplerState* get_sampler() const { return m_sampler_state.Get(); }

    bool has_texture(uint16_t texture_id) const;
    bool replace_texture(uint16_t destination, uint16_t source);

private:
    void create_procedural_textures(ID3D11Device* device, ID3D11DeviceContext* context);

    std::unordered_map<uint16_t, TextureResource> m_textures;
    ComPtr<ID3D11SamplerState> m_sampler_state;
    ComPtr<ID3D11ShaderResourceView> m_flat_normal_srv;
    ComPtr<ID3D11Texture2D> m_flat_normal_tex;
    // 1x1 mid-roughness fallback so a set without a roughness map samples a neutral value.
    ComPtr<ID3D11ShaderResourceView> m_flat_rough_srv;
    ComPtr<ID3D11Texture2D> m_flat_rough_tex;
    uint16_t m_next_custom_id = 32; // 0..13 reserved for built-ins, see BuiltinTextures.h
};

} // namespace GM3D
