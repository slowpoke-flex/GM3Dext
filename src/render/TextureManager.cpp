#include "TextureManager.h"
#include "BuiltinTextures.h"
#include <windows.h>
#include <wincodec.h>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace GM3D {

bool TextureManager::init(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!device || !context) return false;

    // 1. Create High-Quality Anisotropic Sampler with Wrap addressing
    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0.0f;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = device->CreateSamplerState(&sampDesc, m_sampler_state.GetAddressOf());
    if (FAILED(hr)) return false;

    // 2. Create Default Flat Normal (1x1 texture with [128, 128, 255, 255])
    uint8_t flat_normal_bytes[4] = { 128, 128, 255, 255 };
    D3D11_TEXTURE2D_DESC fnd{};
    fnd.Width = 1; fnd.Height = 1; fnd.MipLevels = 1; fnd.ArraySize = 1;
    fnd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    fnd.SampleDesc.Count = 1;
    fnd.Usage = D3D11_USAGE_IMMUTABLE;
    fnd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA fnData{};
    fnData.pSysMem = flat_normal_bytes;
    fnData.SysMemPitch = 4;
    if (SUCCEEDED(device->CreateTexture2D(&fnd, &fnData, m_flat_normal_tex.GetAddressOf()))) {
        device->CreateShaderResourceView(m_flat_normal_tex.Get(), nullptr, m_flat_normal_srv.GetAddressOf());
    }

    // 3. Generate Built-in Procedural Textures (IDs 0..4)
    create_procedural_textures(device, context);

    // 4. Upgrade the shaded materials to real CC0 PBR sets when assets are available.
    const uint32_t sets = load_builtin_texture_sets(device, context);
    if (sets == 0) {
        std::cout << "[TextureManager] assets/textures not found, using procedural textures only.\n";
    }

    return true;
}

void TextureManager::cleanup() {
    m_textures.clear();
    m_sampler_state.Reset();
    m_flat_normal_srv.Reset();
    m_flat_normal_tex.Reset();
    m_next_custom_id = 6;
}

static ComPtr<ID3D11ShaderResourceView> CreateNormalMapSRV(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width, uint32_t height, const uint8_t* normal_rgba) {
    if (!device || !context || !normal_rgba || width == 0 || height == 0) return nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 0; // Full mip chain
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, tex.GetAddressOf()))) return nullptr;

    context->UpdateSubresource(tex.Get(), 0, nullptr, normal_rgba, width * 4, 0);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = -1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device->CreateShaderResourceView(tex.Get(), &srvDesc, srv.GetAddressOf()))) return nullptr;

    context->GenerateMips(srv.Get());
    return srv;
}

static std::vector<uint8_t> GenerateSobelNormalMap(uint32_t width, uint32_t height, const uint8_t* albedo_rgba, float strength = 2.5f) {
    std::vector<uint8_t> normals(width * height * 4);
    auto sampleL = [&](int x, int y) -> float {
        x = (x + (int)width) % (int)width;
        y = (y + (int)height) % (int)height;
        size_t idx = (y * width + x) * 4;
        return (albedo_rgba[idx + 0] * 0.299f + albedo_rgba[idx + 1] * 0.587f + albedo_rgba[idx + 2] * 0.114f) / 255.0f;
    };

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            float tl = sampleL(x - 1, y - 1);
            float t  = sampleL(x,     y - 1);
            float tr = sampleL(x + 1, y - 1);
            float l  = sampleL(x - 1, y);
            float r  = sampleL(x + 1, y);
            float bl = sampleL(x - 1, y + 1);
            float b  = sampleL(x,     y + 1);
            float br = sampleL(x + 1, y + 1);

            float dX = (tr + 2.0f * r + br) - (tl + 2.0f * l + bl);
            float dY = (bl + 2.0f * b + br) - (tl + 2.0f * t + tr);

            float nx = -dX * strength;
            float ny = -dY * strength;
            float nz = 1.0f;
            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 0.0001f) { nx /= len; ny /= len; nz /= len; }

            size_t idx = (y * width + x) * 4;
            normals[idx + 0] = static_cast<uint8_t>(std::clamp((nx * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            normals[idx + 1] = static_cast<uint8_t>(std::clamp((ny * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            normals[idx + 2] = static_cast<uint8_t>(std::clamp((nz * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            normals[idx + 3] = 255;
        }
    }
    return normals;
}

void TextureManager::create_procedural_textures(ID3D11Device* device, ID3D11DeviceContext* context) {
    // -------------------------------------------------------------------------
    // Texture 0: Pure Solid White (1x1 fallback / neutral tint)
    // -------------------------------------------------------------------------
    {
        uint32_t white_pixel = 0xFFFFFFFF;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 1;
        desc.Height = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA subData{};
        subData.pSysMem = &white_pixel;
        subData.SysMemPitch = 4;

        TextureResource res;
        res.width = 1;
        res.height = 1;
        res.name = "Default_White";

        if (SUCCEEDED(device->CreateTexture2D(&desc, &subData, res.texture.GetAddressOf()))) {
            device->CreateShaderResourceView(res.texture.Get(), nullptr, res.srv.GetAddressOf());
            m_textures[0] = std::move(res);
        }
    }

    // -------------------------------------------------------------------------
    // Texture 1: UV Checkerboard (512x512)
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t size = 512;
        std::vector<uint8_t> pixels(size * size * 4);

        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                uint32_t cell_x = x / 64;
                uint32_t cell_y = y / 64;
                bool is_alt = ((cell_x + cell_y) % 2 == 1);

                // Thin border
                bool is_border = (x % 64 == 0 || y % 64 == 0 || x % 64 == 63 || y % 64 == 63);

                uint8_t r = 0, g = 0, b = 0;
                if (is_border) {
                    r = 20; g = 25; b = 32;
                } else if (is_alt) {
                    // Vibrant Amber / Orange
                    r = 235; g = 145; b = 35;
                } else {
                    // Slate Gray
                    r = 50; g = 58; b = 72;
                }

                size_t idx = (y * size + x) * 4;
                pixels[idx + 0] = r;
                pixels[idx + 1] = g;
                pixels[idx + 2] = b;
                pixels[idx + 3] = 255;
            }
        }

        uint16_t id = create_rgba_texture(device, context, size, size, pixels.data(), "UV_Checkerboard", true);
        if (id != 1 && m_textures.count(id)) {
            m_textures[1] = std::move(m_textures[id]);
            m_textures.erase(id);
        }
        std::vector<uint8_t> norm1 = GenerateSobelNormalMap(size, size, pixels.data(), 2.8f);
        m_textures[1].normal_srv = CreateNormalMapSRV(device, context, size, size, norm1.data());
    }

    // -------------------------------------------------------------------------
    // Texture 2: Sci-Fi Tech Grid (512x512)
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t size = 512;
        std::vector<uint8_t> pixels(size * size * 4);

        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                bool major_grid = (x % 128 == 0 || y % 128 == 0 || x % 128 == 1 || y % 128 == 1);
                bool minor_grid = (x % 32 == 0 || y % 32 == 0);

                uint8_t r = 24, g = 28, b = 36; // Dark Slate base
                if (major_grid) {
                    r = 45; g = 215; b = 250; // Neon Cyan
                } else if (minor_grid) {
                    r = 50; g = 70; b = 95;  // Muted Blue-Grey
                }

                size_t idx = (y * size + x) * 4;
                pixels[idx + 0] = r;
                pixels[idx + 1] = g;
                pixels[idx + 2] = b;
                pixels[idx + 3] = 255;
            }
        }

        uint16_t id = create_rgba_texture(device, context, size, size, pixels.data(), "Tech_Grid", true);
        if (id != 2 && m_textures.count(id)) {
            m_textures[2] = std::move(m_textures[id]);
            m_textures.erase(id);
        }
        std::vector<uint8_t> norm2 = GenerateSobelNormalMap(size, size, pixels.data(), 3.5f);
        m_textures[2].normal_srv = CreateNormalMapSRV(device, context, size, size, norm2.data());
    }

    // -------------------------------------------------------------------------
    // Texture 3: Weathered Wooden Planks (512x512)
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t size = 512;
        std::vector<uint8_t> pixels(size * size * 4);

        for (uint32_t y = 0; y < size; ++y) {
            uint32_t plank_idx = y / 64;
            uint32_t plank_y = y % 64;
            bool is_groove = (plank_y <= 2 || plank_y >= 62);

            for (uint32_t x = 0; x < size; ++x) {
                size_t idx = (y * size + x) * 4;
                if (is_groove) {
                    pixels[idx + 0] = 30;
                    pixels[idx + 1] = 20;
                    pixels[idx + 2] = 12;
                    pixels[idx + 3] = 255;
                } else {
                    // Wood grain variation
                    float wood_noise = sinf((float)x * 0.05f + (float)plank_idx * 1.5f) * 8.0f
                                     + sinf((float)x * 0.2f) * 4.0f;
                    int base_r = 165 + (int)wood_noise + (plank_idx % 2 == 0 ? 10 : -10);
                    int base_g = 110 + (int)wood_noise * 2 / 3;
                    int base_b = 60 + (int)wood_noise / 2;

                    // Nails near edges
                    bool is_nail = ((x == 24 || x == size - 24) && (plank_y == 16 || plank_y == 48));
                    if (is_nail) {
                        base_r = 40; base_g = 42; base_b = 45;
                    }

                    pixels[idx + 0] = static_cast<uint8_t>(std::clamp(base_r, 0, 255));
                    pixels[idx + 1] = static_cast<uint8_t>(std::clamp(base_g, 0, 255));
                    pixels[idx + 2] = static_cast<uint8_t>(std::clamp(base_b, 0, 255));
                    pixels[idx + 3] = 255;
                }
            }
        }

        uint16_t id = create_rgba_texture(device, context, size, size, pixels.data(), "Wood_Planks", true);
        if (id != 3 && m_textures.count(id)) {
            m_textures[3] = std::move(m_textures[id]);
            m_textures.erase(id);
        }
        std::vector<uint8_t> norm3 = GenerateSobelNormalMap(size, size, pixels.data(), 4.0f);
        m_textures[3].normal_srv = CreateNormalMapSRV(device, context, size, size, norm3.data());
    }

    // -------------------------------------------------------------------------
    // Texture 4: Industrial Hazard Metal (512x512)
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t size = 512;
        std::vector<uint8_t> pixels(size * size * 4);

        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                // 45-degree diagonal stripe
                uint32_t diag = (x + y) / 40;
                bool is_yellow = (diag % 2 == 0);

                uint8_t r = 0, g = 0, b = 0;
                if (is_yellow) {
                    r = 245; g = 195; b = 25; // Warning Yellow
                } else {
                    r = 35; g = 38; b = 42;   // Dark Charcoal Steel
                }

                // Metal border
                if (x < 6 || x >= size - 6 || y < 6 || y >= size - 6) {
                    r = 75; g = 80; b = 88;
                }

                size_t idx = (y * size + x) * 4;
                pixels[idx + 0] = r;
                pixels[idx + 1] = g;
                pixels[idx + 2] = b;
                pixels[idx + 3] = 255;
            }
        }

        uint16_t id = create_rgba_texture(device, context, size, size, pixels.data(), "Hazard_Metal", true);
        if (id != 4 && m_textures.count(id)) {
            m_textures[4] = std::move(m_textures[id]);
            m_textures.erase(id);
        }
        std::vector<uint8_t> norm4 = GenerateSobelNormalMap(size, size, pixels.data(), 3.5f);
        m_textures[4].normal_srv = CreateNormalMapSRV(device, context, size, size, norm4.data());
    }
}

uint16_t TextureManager::create_rgba_texture(ID3D11Device* device, ID3D11DeviceContext* context,
                                            uint32_t width, uint32_t height,
                                            const uint8_t* rgba_data,
                                            const std::string& name,
                                            bool generate_mips) {
    if (!device || !context || width == 0 || height == 0 || !rgba_data) return 0;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = generate_mips ? 0 : 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (generate_mips ? D3D11_BIND_RENDER_TARGET : 0);
    desc.MiscFlags = generate_mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

    TextureResource res;
    res.width = width;
    res.height = height;
    res.name = name.empty() ? ("Texture_" + std::to_string(m_next_custom_id)) : name;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, res.texture.GetAddressOf());
    if (FAILED(hr)) return 0;

    // Upload Level 0
    context->UpdateSubresource(res.texture.Get(), 0, nullptr, rgba_data, width * 4, 0);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = generate_mips ? -1 : 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = device->CreateShaderResourceView(res.texture.Get(), &srvDesc, res.srv.GetAddressOf());
    if (FAILED(hr)) return 0;

    if (generate_mips) {
        context->GenerateMips(res.srv.Get());
    }

    uint16_t id = m_next_custom_id++;
    m_textures[id] = std::move(res);
    return id;
}

bool TextureManager::update_texture_region(uint16_t texture_id, ID3D11DeviceContext* context,
                                           uint32_t x, uint32_t y, uint32_t width, uint32_t height, const uint8_t* rgba_data) {
    auto it = m_textures.find(texture_id);
    if (!context || !rgba_data || it == m_textures.end() || !it->second.texture || !it->second.srv ||
        width == 0 || height == 0 || x >= it->second.width || y >= it->second.height ||
        width > it->second.width - x || height > it->second.height - y) return false;
    const D3D11_BOX box{ x, y, 0, x + width, y + height, 1 };
    context->UpdateSubresource(it->second.texture.Get(), 0, &box, rgba_data, width * 4, 0);
    context->GenerateMips(it->second.srv.Get());
    return true;
}

bool TextureManager::set_texture_rgba(uint16_t texture_id, ID3D11Device* device, ID3D11DeviceContext* context,
                                      uint32_t width, uint32_t height,
                                      const uint8_t* rgba_data,
                                      const std::string& name,
                                      bool generate_mips) {
    if (!device || !context || !rgba_data || width == 0 || height == 0) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = generate_mips ? 0 : 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (generate_mips ? D3D11_BIND_RENDER_TARGET : 0);
    desc.MiscFlags = generate_mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

    TextureResource res;
    res.width = width;
    res.height = height;
    res.name = name.empty() ? ("Texture_" + std::to_string(texture_id)) : name;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, res.texture.GetAddressOf());
    if (FAILED(hr)) return false;

    context->UpdateSubresource(res.texture.Get(), 0, nullptr, rgba_data, width * 4, 0);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = generate_mips ? -1 : 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = device->CreateShaderResourceView(res.texture.Get(), &srvDesc, res.srv.GetAddressOf());
    if (FAILED(hr)) return false;

    if (generate_mips) {
        context->GenerateMips(res.srv.Get());
    }

    if (texture_id >= m_next_custom_id) {
        m_next_custom_id = texture_id + 1;
    }
    m_textures[texture_id] = std::move(res);
    return true;
}

uint16_t TextureManager::load_texture_from_file(ID3D11Device* device, ID3D11DeviceContext* context, const std::string& filepath) {
    if (!device || !context || filepath.empty()) return 0;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory) return 0;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, nullptr, 0);
    if (wlen <= 0) { factory->Release(); return 0; }
    std::wstring wpath(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &wpath[0], wlen);

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        factory->Release();
        return 0;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        factory->Release();
        return 0;
    }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        factory->Release();
        return 0;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return 0;
    }

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (w == 0 || h == 0) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return 0;
    }

    std::vector<uint8_t> rgba(w * h * 4);
    hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(rgba.size()), rgba.data());

    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();

    if (FAILED(hr)) return 0;

    uint16_t id = create_rgba_texture(device, context, w, h, rgba.data(), filepath, true);
    if (id > 0 && m_textures.count(id)) {
        std::vector<uint8_t> norm = GenerateSobelNormalMap(w, h, rgba.data(), 3.0f);
        m_textures[id].normal_srv = CreateNormalMapSRV(device, context, w, h, norm.data());
    }
    return id;
}

uint16_t TextureManager::load_texture_from_memory(ID3D11Device* device, ID3D11DeviceContext* context,
                                                const uint8_t* data, size_t size,
                                                const std::string& name) {
    if (!device || !context || !data || size == 0) return 0;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory) return 0;
    }

    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream) {
        factory->Release();
        return 0;
    }

    hr = stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size));
    if (FAILED(hr)) {
        stream->Release();
        factory->Release();
        return 0;
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        stream->Release();
        factory->Release();
        return 0;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        stream->Release();
        factory->Release();
        return 0;
    }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();
        return 0;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();
        return 0;
    }

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (w == 0 || h == 0) {
        converter->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();
        return 0;
    }

    std::vector<uint8_t> rgba(w * h * 4);
    hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(rgba.size()), rgba.data());

    converter->Release();
    frame->Release();
    decoder->Release();
    stream->Release();
    factory->Release();

    if (FAILED(hr)) return 0;

    uint16_t id = create_rgba_texture(device, context, w, h, rgba.data(), name, true);
    if (id > 0 && m_textures.count(id)) {
        std::vector<uint8_t> norm = GenerateSobelNormalMap(w, h, rgba.data(), 3.0f);
        m_textures[id].normal_srv = CreateNormalMapSRV(device, context, w, h, norm.data());
    }
    return id;
}

namespace {

// Decodes any WIC-supported image (PNG/JPG/BMP/TGA-via-WIC) into tightly packed RGBA8.
bool decode_image_rgba(const std::string& path, uint32_t& out_w, uint32_t& out_h,
                       std::vector<uint8_t>& out_rgba) {
    out_w = out_h = 0;
    out_rgba.clear();

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory) return false;
    }

    const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) { factory->Release(); return false; }
    std::wstring wpath(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) { factory->Release(); return false; }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) { decoder->Release(); factory->Release(); return false; }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release(); decoder->Release(); factory->Release(); return false;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                               nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) {
        UINT w = 0, h = 0;
        converter->GetSize(&w, &h);
        if (w > 0 && h > 0) {
            out_rgba.resize(static_cast<size_t>(w) * h * 4);
            hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(out_rgba.size()), out_rgba.data());
            if (SUCCEEDED(hr)) { out_w = w; out_h = h; }
        } else {
            hr = E_FAIL;
        }
    }

    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return SUCCEEDED(hr) && out_w > 0 && out_h > 0;
}

} // namespace

std::string TextureManager::find_asset_dir() {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;

    // 1. Explicit override wins, so a game can point at its own texture folder.
    //    Accepts either the texture folder itself or a root that contains assets/textures.
    char env_buffer[MAX_PATH] = {};
    const DWORD env_len = GetEnvironmentVariableA("GM3D_ASSET_DIR", env_buffer, MAX_PATH);
    if (env_len > 0 && env_len < MAX_PATH) {
        const fs::path env_path = fs::u8path(env_buffer);
        std::error_code env_ec;
        if (fs::exists(env_path / "Grass001", env_ec)) return env_path.string();
        roots.push_back(env_path);
    }

    // 2. Next to the executable, next to the working directory, then two levels up
    //    (build/ -> repository root, which is where a dev build keeps assets/).
    char module_path[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, module_path, MAX_PATH) > 0) {
        const fs::path exe_dir = fs::u8path(module_path).parent_path();
        roots.push_back(exe_dir);
        roots.push_back(exe_dir / "..");
        roots.push_back(exe_dir / ".." / "..");
    }
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) roots.push_back(cwd);

    for (const fs::path& root : roots) {
        for (const char* leaf : { "assets/textures", "textures", "assets" }) {
            const fs::path candidate = root / fs::u8path(leaf);
            std::error_code exists_ec;
            if (fs::exists(candidate / "Grass001", exists_ec)) {
                return candidate.string();
            }
        }
    }
    return {};
}

uint32_t TextureManager::load_builtin_texture_sets(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!device || !context) return 0;
    const std::string dir = find_asset_dir();
    if (dir.empty()) return 0;

    struct SetDesc {
        uint16_t id;
        const char* folder;
        const char* label;
    };
    static const SetDesc kSets[] = {
        { GM3D::TexId::GRASS,  "Ground037",       "CC0_MeadowSoil" },
        { GM3D::TexId::ROCK,   "Rock030",         "CC0_Rock"       },
        { GM3D::TexId::SAND,   "Ground080",       "CC0_Sand"       },
        { GM3D::TexId::COBBLE, "PavingStones131", "CC0_Cobble"     },
        { GM3D::TexId::WOOD,   "Wood066",         "CC0_Wood"       },
        { GM3D::TexId::METAL,  "Metal032",        "CC0_Metal"      },
        { GM3D::TexId::TURF,   "Grass001",        "CC0_Turf"       },
    };

    uint32_t loaded = 0;
    for (const SetDesc& set : kSets) {
        const std::string base = dir + "\\" + set.folder + "\\" + set.folder;
        uint32_t w = 0, h = 0;
        std::vector<uint8_t> albedo;
        if (!decode_image_rgba(base + "_Color.jpg", w, h, albedo)) continue;
        if (!set_texture_rgba(set.id, device, context, w, h, albedo.data(), set.label, true)) continue;

        std::vector<uint8_t> normal;
        uint32_t nw = 0, nh = 0;
        if (decode_image_rgba(base + "_Normal.png", nw, nh, normal)) {
            set_normal_map(set.id, device, context, nw, nh, normal.data());
        } else {
            // No authored normal map: derive a usable one from the albedo instead of
            // dropping to the flat normal, which would flatten the material.
            std::vector<uint8_t> generated = GenerateSobelNormalMap(w, h, albedo.data(), 2.2f);
            set_normal_map(set.id, device, context, w, h, generated.data());
        }
        ++loaded;
    }

    if (loaded > 0) {
        std::cout << "[TextureManager] Loaded " << loaded << " CC0 PBR texture sets from " << dir << "\n";
    }
    return loaded;
}

ID3D11ShaderResourceView* TextureManager::get_srv(uint16_t texture_id) const {
    auto it = m_textures.find(texture_id);
    if (it != m_textures.end() && it->second.srv) {
        return it->second.srv.Get();
    }
    // Safe fallback to Texture 0 (Default White)
    auto def_it = m_textures.find(0);
    return (def_it != m_textures.end()) ? def_it->second.srv.Get() : nullptr;
}

ID3D11ShaderResourceView* TextureManager::get_normal_srv(uint16_t texture_id) const {
    auto it = m_textures.find(texture_id);
    if (it != m_textures.end() && it->second.normal_srv) {
        return it->second.normal_srv.Get();
    }
    return m_flat_normal_srv.Get();
}

bool TextureManager::set_normal_map(uint16_t texture_id, ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width, uint32_t height, const uint8_t* normal_rgba_data) {
    auto it = m_textures.find(texture_id);
    if (it == m_textures.end()) return false;
    it->second.normal_srv = CreateNormalMapSRV(device, context, width, height, normal_rgba_data);
    return it->second.normal_srv != nullptr;
}

bool TextureManager::set_normal_map_texture(uint16_t target_tex_id, uint16_t normal_tex_id) {
    auto target_it = m_textures.find(target_tex_id);
    auto normal_it = m_textures.find(normal_tex_id);
    if (target_it == m_textures.end() || normal_it == m_textures.end()) return false;
    target_it->second.normal_srv = normal_it->second.srv;
    target_it->second.normal_texture = normal_it->second.texture;
    return true;
}

bool TextureManager::replace_texture(uint16_t destination, uint16_t source) {
    auto target = m_textures.find(destination);
    auto incoming = m_textures.find(source);
    if (destination == source || target == m_textures.end() || incoming == m_textures.end()) return false;
    target->second = std::move(incoming->second);
    m_textures.erase(incoming);
    return true;
}

bool TextureManager::has_texture(uint16_t texture_id) const {
    return m_textures.count(texture_id) > 0;
}

} // namespace GM3D
