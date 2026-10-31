// Startup automation: benchmarks, the grass demo, screenshots and the frame profiler.
//
// Split out of StudioApp.cpp; the bodies are unchanged.
#include "StudioApp.h"
#include "StudioShared.h"
#include "SceneSerializer.h"
#include "ViewportMath.h"
#include "Heightmap.h"
#include "core/EngineContext.h"
#include "render/RenderWorld.h"
#include "physics/PhysicsWorld.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "ImGuizmo.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <iostream>

namespace GM3D::Studio {

void StudioApp::build_bench_stress_scene() {
    // Deterministic synthetic scene: a dense grid of shadow-casting props plus four
    // shadowed point lights, so culling and shadow-pass work can be measured
    // without depending on an external scene file.
    m_scene.entities.clear();
    m_scene.next_entity_id = 1;

    const int grid = 24;
    for (int gx = 0; gx < grid; ++gx) {
        for (int gz = 0; gz < grid; ++gz) {
            Entity e;
            e.id = m_scene.next_entity_id++;
            e.name = "Bench Prop";
            e.mesh_id = ((gx + gz) % 3 == 0) ? 1 : 0;
            e.material_id = static_cast<uint16_t>(2 + (gx + gz) % 6);
            e.position[0] = (static_cast<float>(gx) - grid * 0.5f) * 4.0f;
            e.position[1] = 1.0f;
            e.position[2] = (static_cast<float>(gz) - grid * 0.5f) * 4.0f;
            e.scale[0] = e.scale[1] = e.scale[2] = 1.5f;
            e.cast_shadow = true;
            e.physics_type = 0;
            m_scene.entities.push_back(e);
        }
    }

    const float light_positions[4][3] = {
        { -24.0f, 8.0f, -24.0f },
        {  24.0f, 8.0f, -24.0f },
        { -24.0f, 8.0f,  24.0f },
        {  24.0f, 8.0f,  24.0f }
    };
    for (int i = 0; i < 4; ++i) {
        Entity light;
        light.id = m_scene.next_entity_id++;
        light.name = "Bench Light";
        light.is_point_light = true;
        light.light_radius = 40.0f;
        light.light_intensity = 30.0f;
        light.position[0] = light_positions[i][0];
        light.position[1] = light_positions[i][1];
        light.position[2] = light_positions[i][2];
        light.cast_shadow = true;
        m_scene.entities.push_back(light);
    }

    m_scene.max_shadow_lights = 4;
    m_scene.csm_enabled = true;
    m_scene.shadows_enabled = true;
    // Flat ground keeps the props above the surface and the comparison unambiguous.
    m_scene.has_terrain = false;
    m_scene.grass_enabled = false;

    m_cam_pos[0] = 0.0f; m_cam_pos[1] = 14.0f; m_cam_pos[2] = -46.0f;
    m_cam_yaw = 0.0f; m_cam_pitch = 12.0f;
    m_cam_target[0] = 0.0f; m_cam_target[1] = 2.0f; m_cam_target[2] = 0.0f;

    select_entity(0, false, false);
    sync_scene_to_engine();
    m_history.reset(m_scene);
}

void StudioApp::set_startup_camera(const float values[6]) {
    m_startup_camera_valid = true;
    for (int i = 0; i < 6; ++i) m_startup_camera[i] = values[i];
}

void StudioApp::build_grass_demo() {    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;
    if (!m_scene.has_terrain) {
        m_scene.has_terrain = true;
        rebuild_terrain();
    }
    GM3D::GrassConfig cfg = rw->get_grass_system().get_config();
    cfg.enabled = true;
    cfg.density = m_scene.grass_density;
    cfg.base_height = m_scene.grass_height;
    cfg.base_width = m_scene.grass_width;
    cfg.max_distance = m_scene.grass_max_distance;
    cfg.fade_range = m_scene.grass_fade_range;
    cfg.wind_speed = m_scene.grass_wind_speed;
    cfg.wind_strength = m_scene.grass_wind_strength;
    rw->get_grass_system().build_from_terrain(rw->get_device(), rw->get_terrain_system(), cfg);
    rw->set_grass_enabled(true);
    m_scene.grass_enabled = true;

    // Park the camera on a fixed meadow spot so repeated captures stay comparable.
    {
        const float cx = -34.0f;
        const float cz = -5.0f;
        const float cy = rw->get_terrain_system().get_height(cx, cz);
        m_cam_pos[0] = cx; m_cam_pos[1] = cy + 1.7f; m_cam_pos[2] = cz - 6.0f;
        m_cam_yaw = 0.0f; m_cam_pitch = 4.0f;
        m_cam_target[0] = cx; m_cam_target[1] = cy + 0.5f; m_cam_target[2] = cz + 8.0f;
    }
    std::cout << "[grass-demo] clumps=" << rw->get_grass_system().get_instance_count()
              << " camera=(" << m_cam_pos[0] << ", " << m_cam_pos[1] << ", " << m_cam_pos[2] << ")"
              << std::endl;
    sync_scene_to_engine();
}

bool StudioApp::capture_backbuffer(const std::string& path) {
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw || !rw->get_swap_chain() || !rw->get_device() || !rw->get_context()) return false;

    ComPtr<ID3D11Texture2D> back_buffer;
    if (FAILED(rw->get_swap_chain()->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf())))) return false;

    D3D11_TEXTURE2D_DESC desc{};
    back_buffer->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(rw->get_device()->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf()))) return false;

    rw->get_context()->CopyResource(staging.Get(), back_buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(rw->get_context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

    const UINT width = desc.Width;
    const UINT height = desc.Height;
    const size_t row_bytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> pixels(row_bytes * height);
    for (UINT y = 0; y < height; ++y) {
        memcpy(pixels.data() + static_cast<size_t>(y) * row_bytes,
               static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
               row_bytes);
    }
    rw->get_context()->Unmap(staging.Get(), 0);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.GetAddressOf())))) return false;

    const std::filesystem::path out_path = std::filesystem::u8path(path);
    std::error_code ec;
    if (out_path.has_parent_path()) std::filesystem::create_directories(out_path.parent_path(), ec);

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.GetAddressOf()))) return false;
    if (FAILED(stream->InitializeFromFilename(out_path.wstring().c_str(), GENERIC_WRITE))) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()))) return false;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr))) return false;
    if (FAILED(frame->Initialize(nullptr))) return false;
    if (FAILED(frame->SetSize(width, height))) return false;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    if (FAILED(frame->SetPixelFormat(&format))) return false;

    HRESULT write_hr = E_FAIL;
    if (format == GUID_WICPixelFormat32bppRGBA) {
        write_hr = frame->WritePixels(height, static_cast<UINT>(row_bytes),
                                      static_cast<UINT>(pixels.size()), pixels.data());
    } else {
        // The encoder refused RGBA: convert through a WIC bitmap source.
        ComPtr<IWICBitmap> bitmap;
        if (SUCCEEDED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppRGBA,
                                                      static_cast<UINT>(row_bytes),
                                                      static_cast<UINT>(pixels.size()),
                                                      pixels.data(), bitmap.GetAddressOf()))) {
            ComPtr<IWICFormatConverter> converter;
            if (SUCCEEDED(factory->CreateFormatConverter(converter.GetAddressOf())) &&
                SUCCEEDED(converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone,
                                                nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                write_hr = frame->WriteSource(converter.Get(), nullptr);
            }
        }
    }

    if (SUCCEEDED(write_hr)) write_hr = frame->Commit();
    if (SUCCEEDED(write_hr)) write_hr = encoder->Commit();
    if (SUCCEEDED(write_hr)) {
        std::cout << "Saved screenshot: " << path << " (" << width << "x" << height << ")" << std::endl;
    }
    return SUCCEEDED(write_hr);
}

void StudioApp::finish_bench() {
    auto percentile = [](std::vector<float> values, float p) {
        if (values.empty()) return 0.0f;
        std::sort(values.begin(), values.end());
        size_t idx = static_cast<size_t>(p * static_cast<float>(values.size() - 1));
        return values[idx];
    };
    auto mean = [](const std::vector<float>& values) {
        if (values.empty()) return 0.0f;
        double sum = 0.0;
        for (float v : values) sum += v;
        return static_cast<float>(sum / static_cast<double>(values.size()));
    };

    const int entities = static_cast<int>(m_scene.entities.size());
    const float cpu_p50 = percentile(m_bench_cpu_ms, 0.5f);
    const float total_p50 = percentile(m_bench_total_ms, 0.5f);
    std::cout << "[bench] scene=" << (m_bench_stress ? "stress" : "default")
              << " entities=" << entities
              << " frames=" << m_bench_total_ms.size()
              << " cpu_ms avg=" << mean(m_bench_cpu_ms)
              << " p50=" << cpu_p50
              << " p95=" << percentile(m_bench_cpu_ms, 0.95f)
              << " | total_ms avg=" << mean(m_bench_total_ms)
              << " p50=" << total_p50
              << " p95=" << percentile(m_bench_total_ms, 0.95f)
              << " min=" << percentile(m_bench_total_ms, 0.0f)
              << " max=" << percentile(m_bench_total_ms, 1.0f)
              << " | fps(p50)=" << (total_p50 > 0.0f ? 1000.0f / total_p50 : 0.0f)
              << std::endl;

    m_bench_frames = 0;
    m_running = false;
}

void StudioApp::report_frame_profile() {
    // Averages over one second of frames (60) are enough to see which section owns the
    // frame, and short enough that the numbers react to what the user is doing.
    constexpr size_t kWindow = 60;
    if (m_prof_samples.size() < kWindow) return;
    if (m_prof_samples.size() > kWindow * 4) {
        m_prof_samples.erase(m_prof_samples.begin(), m_prof_samples.end() - static_cast<ptrdiff_t>(kWindow));
    }

    FrameSample avg{};
    for (const FrameSample& s : m_prof_samples) {
        avg.ui += s.ui; avg.sync += s.sync; avg.scene += s.scene;
        avg.imgui += s.imgui; avg.present += s.present; avg.total += s.total;
    }
    const float n = static_cast<float>(m_prof_samples.size());
    std::cout << "[frame] ui=" << avg.ui / n << " sync=" << avg.sync / n << " scene=" << avg.scene / n
              << " imgui=" << avg.imgui / n << " present=" << avg.present / n
              << " | total=" << avg.total / n << " ms (" << (avg.total > 0.0f ? 1000.0f * n / avg.total : 0.0f)
              << " FPS)\n";
    m_prof_samples.clear();
}

} // namespace GM3D::Studio
