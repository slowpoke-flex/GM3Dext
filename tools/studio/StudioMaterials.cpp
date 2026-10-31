// Material editor panel and its sphere preview.
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
#include "ImGuizmo.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace GM3D::Studio {

static void draw_material_preview_sphere(ImDrawList* draw_list, const ImVec2& center, float radius, const Material& mat) {
    if (!draw_list || radius < 2.0f) return;
    float r = mat.base_color[0];
    float g = mat.base_color[1];
    float b = mat.base_color[2];
    ImU32 baseCol = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
    ImU32 shadowCol = ImGui::ColorConvertFloat4ToU32(ImVec4(r * 0.35f, g * 0.35f, b * 0.35f, 1.0f));
    ImU32 specCol = mat.metallic > 0.5f 
        ? ImGui::ColorConvertFloat4ToU32(ImVec4(r * 0.8f + 0.2f, g * 0.8f + 0.2f, b * 0.8f + 0.2f, 1.0f))
        : IM_COL32(255, 255, 255, 220);

    draw_list->AddCircleFilled(center, radius, shadowCol, 32);
    ImVec2 highlightPos(center.x - radius * 0.28f, center.y - radius * 0.28f);
    draw_list->AddCircleFilled(highlightPos, radius * 0.72f, baseCol, 32);

    float specRadius = radius * (0.15f + (1.0f - std::clamp(mat.roughness, 0.0f, 1.0f)) * 0.22f);
    draw_list->AddCircleFilled(highlightPos, specRadius, specCol, 16);
}

void StudioApp::update_material_preview_texture(const Material& mat) {
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;
    ID3D11Device* device = rw->get_device();
    ID3D11DeviceContext* context = rw->get_context();
    if (!device || !context) return;

    uint32_t hash = 17;
    auto hash_combine = [&hash](float v) {
        union { float f; uint32_t u; } u;
        u.f = v;
        hash = hash * 31 + u.u;
    };
    hash_combine(mat.base_color[0]);
    hash_combine(mat.base_color[1]);
    hash_combine(mat.base_color[2]);
    hash_combine(mat.base_color[3]);
    hash_combine(mat.roughness);
    hash_combine(mat.metallic);
    hash_combine(mat.emissive);
    hash = hash * 31 + mat.alpha_mode;
    hash = hash * 31 + mat.preset_index;
    hash = hash * 31 + (mat.is_builtin_preset ? 1 : 0);

    if (m_mat_preview_tex && m_mat_preview_srv && m_mat_preview_last_id == mat.id && m_mat_preview_hash == hash) {
        return;
    }
    m_mat_preview_last_id = mat.id;
    m_mat_preview_hash = hash;

    constexpr UINT SIZE = 128;
    if (!m_mat_preview_tex) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = SIZE;
        desc.Height = SIZE;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(device->CreateTexture2D(&desc, nullptr, m_mat_preview_tex.GetAddressOf()))) return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(m_mat_preview_tex.Get(), &srvDesc, m_mat_preview_srv.GetAddressOf()))) return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_mat_preview_tex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;

    float baseR = mat.base_color[0];
    float baseG = mat.base_color[1];
    float baseB = mat.base_color[2];
    float baseA = mat.base_color[3];
    float roughness = std::clamp(mat.roughness, 0.04f, 1.0f);
    float metallic = std::clamp(mat.metallic, 0.0f, 1.0f);
    float emissive = mat.emissive;
    float emR = mat.emissive_color[0];
    float emG = mat.emissive_color[1];
    float emB = mat.emissive_color[2];

    if (mat.is_builtin_preset && mat.preset_index > 0) {
        static const float preset_colors[9][3] = {
            { 0.9f, 0.9f, 0.9f },
            { 0.95f, 0.35f, 0.30f },
            { 0.95f, 0.78f, 0.35f },
            { 0.20f, 0.85f, 0.65f },
            { 0.20f, 0.55f, 0.95f },
            { 0.75f, 0.30f, 0.95f },
            { 0.35f, 0.55f, 0.25f },
            { 0.80f, 0.85f, 0.90f },
            { 0.50f, 0.50f, 0.52f },
        };
        uint32_t pIdx = std::clamp(mat.preset_index, 0u, 8u);
        baseR = preset_colors[pIdx][0];
        baseG = preset_colors[pIdx][1];
        baseB = preset_colors[pIdx][2];
    }

    float lx = 0.577f, ly = 0.707f, lz = 0.408f;
    float invL = 1.0f / sqrtf(lx*lx + ly*ly + lz*lz);
    lx *= invL; ly *= invL; lz *= invL;

    auto* pixels = static_cast<uint8_t*>(mapped.pData);
    const float radius = 56.0f;
    const float cx = 64.0f, cy = 64.0f;

    for (UINT y = 0; y < SIZE; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(pixels + y * mapped.RowPitch);
        for (UINT x = 0; x < SIZE; ++x) {
            float dx = (static_cast<float>(x) - cx) / radius;
            float dy = (static_cast<float>(y) - cy) / radius;
            float dist2 = dx * dx + dy * dy;

            if (dist2 > 1.0f) {
                bool checker = ((x / 8) ^ (y / 8)) & 1;
                uint8_t bg = checker ? 38 : 30;
                row[x] = (255 << 24) | (bg << 16) | (bg << 8) | bg;
                continue;
            }

            float dz = sqrtf(std::max(0.0f, 1.0f - dist2));
            float nx = dx, ny = -dy, nz = dz;

            float NdotL = std::max(0.0f, nx * lx + ny * ly + nz * lz);
            float NdotV = std::max(0.0f, nz);

            float hx = lx, hy = ly, hz = lz + 1.0f;
            float invH = 1.0f / sqrtf(hx*hx + hy*hy + hz*hz);
            hx *= invH; hy *= invH; hz *= invH;
            float NdotH = std::max(0.0f, nx * hx + ny * hy + nz * hz);
            float VdotH = std::max(0.0f, hz);

            float f0_r = (1.0f - metallic) * 0.04f + metallic * baseR;
            float f0_g = (1.0f - metallic) * 0.04f + metallic * baseG;
            float f0_b = (1.0f - metallic) * 0.04f + metallic * baseB;

            float fFactor = powf(1.0f - VdotH, 5.0f);
            float Fr = f0_r + (1.0f - f0_r) * fFactor;
            float Fg = f0_g + (1.0f - f0_g) * fFactor;
            float Fb = f0_b + (1.0f - f0_b) * fFactor;

            float alpha = roughness * roughness;
            float alpha2 = alpha * alpha;
            float denom = NdotH * NdotH * (alpha2 - 1.0f) + 1.0f;
            float D = alpha2 / (3.14159265f * denom * denom + 0.0001f);

            float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
            float gV = NdotV / (NdotV * (1.0f - k) + k + 0.0001f);
            float gL = NdotL / (NdotL * (1.0f - k) + k + 0.0001f);
            float G = gV * gL;

            float specFactor = (D * G) / (4.0f * NdotV * NdotL + 0.001f);
            float specR = Fr * specFactor;
            float specG = Fg * specFactor;
            float specB = Fb * specFactor;

            float diffR = (1.0f - metallic) * baseR;
            float diffG = (1.0f - metallic) * baseG;
            float diffB = (1.0f - metallic) * baseB;

            float rim = powf(1.0f - NdotV, 3.0f) * 0.15f;
            float ambR = baseR * 0.18f;
            float ambG = baseG * 0.18f;
            float ambB = baseB * 0.18f;

            float emisR = emR * emissive;
            float emisG = emG * emissive;
            float emisB = emB * emissive;

            float colR = ambR + (diffR + specR) * NdotL * 1.8f + rim + emisR;
            float colG = ambG + (diffG + specG) * NdotL * 1.8f + rim + emisG;
            float colB = ambB + (diffB + specB) * NdotL * 1.8f + rim + emisB;

            auto tonemap = [](float c) {
                c = std::max(0.0f, c);
                return (c * (2.51f * c + 0.03f)) / (c * (2.43f * c + 0.59f) + 0.14f);
            };
            uint8_t rByte = static_cast<uint8_t>(std::clamp(tonemap(colR) * 255.0f, 0.0f, 255.0f));
            uint8_t gByte = static_cast<uint8_t>(std::clamp(tonemap(colG) * 255.0f, 0.0f, 255.0f));
            uint8_t bByte = static_cast<uint8_t>(std::clamp(tonemap(colB) * 255.0f, 0.0f, 255.0f));
            uint8_t aByte = static_cast<uint8_t>(std::clamp(baseA * 255.0f, 0.0f, 255.0f));

            row[x] = (aByte << 24) | (bByte << 16) | (gByte << 8) | rByte;
        }
    }
    context->Unmap(m_mat_preview_tex.Get(), 0);
}

void StudioApp::draw_material_editor_panel() {
    if (!m_show_materials) return;
    ImGui::SetNextWindowSize(ImVec2(580, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Materials", &m_show_materials)) {
        ImGui::End();
        return;
    }

    if (m_scene.materials.empty()) {
        m_scene.init_default_materials();
    }

    // Top Toolbar
    if (ImGui::Button("+ New Material", ImVec2(120, 26))) {
        Material new_mat;
        new_mat.id = m_scene.allocate_material_id();
        if (!new_mat.id) { m_asset_message = "No material slots remain."; m_asset_message_pending = true; }
        else {
        new_mat.name = "Custom Material " + std::to_string(new_mat.id);
        new_mat.is_builtin_preset = false;
        m_scene.materials.push_back(new_mat);
        m_selected_material_id = new_mat.id;
        }
    }
    HelpTooltip("Create a new customizable PBR material.");
    ImGui::SameLine();

    Material* active_mat = m_scene.find_material(m_selected_material_id);
    if (!active_mat && !m_scene.materials.empty()) {
        m_selected_material_id = m_scene.materials[0].id;
        active_mat = &m_scene.materials[0];
    }

    ImGui::BeginDisabled(!active_mat);
    if (ImGui::Button("Duplicate", ImVec2(90, 26))) {
        if (active_mat) {
            Material dup = *active_mat;
            dup.id = m_scene.allocate_material_id();
            if (!dup.id) { m_asset_message = "No material slots remain."; m_asset_message_pending = true; }
            else {
            dup.name = active_mat->name + " (Copy)";
            dup.is_builtin_preset = false;
            m_scene.materials.push_back(dup);
            m_selected_material_id = dup.id;
            active_mat = m_scene.find_material(m_selected_material_id);
            }
        }
    }
    HelpTooltip("Duplicate the selected material.");
    ImGui::SameLine();

    if (ImGui::Button("Delete", ImVec2(80, 26))) {
        if (active_mat && m_scene.materials.size() > 1) {
            uint32_t del_id = active_mat->id;
            m_scene.remove_material(del_id);
            m_selected_material_id = m_scene.materials.empty() ? 0 : m_scene.materials[0].id;
            active_mat = m_scene.find_material(m_selected_material_id);
        }
    }
    HelpTooltip("Delete this material and assign affected objects to the first remaining material. Undo restores both.");
    ImGui::EndDisabled();

    ImGui::Separator();

    const float avail_w = ImGui::GetContentRegionAvail().x;
    const bool compact_mode = (avail_w < 480.0f);

    if (compact_mode) {
        Material* cur = active_mat;
        std::string cur_name = cur ? cur->name : "Select Material...";
        ImGui::TextUnformatted("Active Material");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##MatComboCompact", cur_name.c_str())) {
            for (const auto& mat : m_scene.materials) {
                ImGui::PushID(static_cast<int>(mat.id));
                ImVec4 swatch(mat.base_color[0], mat.base_color[1], mat.base_color[2], mat.base_color[3]);
                ImGui::ColorButton("##sw", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
                ImGui::SameLine();
                bool is_selected = (mat.id == m_selected_material_id);
                if (ImGui::Selectable(mat.name.c_str(), is_selected)) {
                    m_selected_material_id = mat.id;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();
    } else {
        float left_width = 170.0f;
        ImGui::BeginChild("MaterialListPane", ImVec2(left_width, 0), true);
        static char filter_buf[64] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##MatFilter", "Search...", filter_buf, sizeof(filter_buf));

        std::string filter = filter_buf;
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

        for (const auto& mat : m_scene.materials) {
            std::string lower_name = mat.name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            if (!filter.empty() && lower_name.find(filter) == std::string::npos) continue;

            ImGui::PushID(static_cast<int>(mat.id));
            ImVec4 swatch(mat.base_color[0], mat.base_color[1], mat.base_color[2], mat.base_color[3]);
            ImGui::ColorButton("##sw", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
            ImGui::SameLine();

            bool is_selected = (mat.id == m_selected_material_id);
            if (ImGui::Selectable(mat.name.c_str(), is_selected)) {
                m_selected_material_id = mat.id;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();
    }

    // Right Inspector Pane
    ImGui::BeginChild("MaterialPropertiesPane", ImVec2(0, 0), true);
    if (active_mat) {
        update_material_preview_texture(*active_mat);

        if (compact_mode) {
            if (m_mat_preview_srv) {
                ImGui::Image(reinterpret_cast<ImTextureID>(m_mat_preview_srv.Get()), ImVec2(80, 80));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Cook-Torrance GGX PBR preview with roughness, metallic and Fresnel reflections");
                }
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            char name_buf[128];
            strncpy_s(name_buf, active_mat->name.c_str(), sizeof(name_buf) - 1);
            ImGui::TextUnformatted("Material Name");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##MatNameCompact", name_buf, sizeof(name_buf))) {
                active_mat->name = name_buf;
            }
            ImGui::TextDisabled("ID: %u | %s", active_mat->id,
                active_mat->is_builtin_preset ? "Builtin" : "Custom PBR");
            ImGui::EndGroup();

            if (active_mat->is_builtin_preset) {
                if (ImGui::Button("Convert to Custom PBR", ImVec2(-1, 24))) {
                    active_mat->is_builtin_preset = false;
                }
                HelpTooltip("Convert this built-in preset to an editable custom PBR material.");
            }
            Entity* sel_e = selected_entity();
            if (sel_e) {
                if (ImGui::Button("Assign to Selected Object", ImVec2(-1, 26))) {
                    sel_e->material_id = static_cast<uint16_t>(active_mat->id);
                }
            }
            if (m_selected_entity_ids.size() > 1) {
                char apply_txt[64];
                snprintf(apply_txt, sizeof(apply_txt), "Assign to All %zu Selected", m_selected_entity_ids.size());
                if (ImGui::Button(apply_txt, ImVec2(-1, 24))) {
                    for (uint32_t eid : m_selected_entity_ids) {
                        for (auto& e : m_scene.entities) {
                            if (e.id == eid) {
                                e.material_id = static_cast<uint16_t>(active_mat->id);
                            }
                        }
                    }
                }
            }
        } else {
            // Header with Name & Preview
            ImGui::Columns(2, "MatHeaderCols", false);
            ImGui::SetColumnWidth(0, 140.0f);

            // 3D Preview Image
            if (m_mat_preview_srv) {
                ImGui::Image(reinterpret_cast<ImTextureID>(m_mat_preview_srv.Get()), ImVec2(128, 128));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Cook-Torrance GGX PBR preview with roughness, metallic and Fresnel reflections");
                }
            }

            ImGui::NextColumn();

            char name_buf[128];
            strncpy_s(name_buf, active_mat->name.c_str(), sizeof(name_buf) - 1);
            ImGui::TextUnformatted("Material Name");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##MatName", name_buf, sizeof(name_buf))) {
                active_mat->name = name_buf;
            }

            ImGui::TextDisabled("ID: %u | Type: %s", active_mat->id,
                active_mat->is_builtin_preset ? "Builtin Preset" : "Custom PBR");

            if (active_mat->is_builtin_preset) {
                if (ImGui::Button("Convert to Custom PBR", ImVec2(-1, 24))) {
                    active_mat->is_builtin_preset = false;
                }
                HelpTooltip("Convert this built-in preset to an editable custom PBR material.");
            }

            Entity* sel_e = selected_entity();
            if (sel_e) {
                if (ImGui::Button("Assign to Selected Object", ImVec2(-1, 26))) {
                    sel_e->material_id = static_cast<uint16_t>(active_mat->id);
                }
            }
            if (m_selected_entity_ids.size() > 1) {
                char apply_txt[64];
                snprintf(apply_txt, sizeof(apply_txt), "Assign to All %zu Selected", m_selected_entity_ids.size());
                if (ImGui::Button(apply_txt, ImVec2(-1, 24))) {
                    for (uint32_t eid : m_selected_entity_ids) {
                        for (auto& e : m_scene.entities) {
                            if (e.id == eid) {
                                e.material_id = static_cast<uint16_t>(active_mat->id);
                            }
                        }
                    }
                }
            }

            ImGui::Columns(1);
        }
        ImGui::Separator();

        // PBR Properties
        if (ImGui::CollapsingHeader("Surface Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::ColorEdit4("Base Color", active_mat->base_color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar)) {
                active_mat->is_builtin_preset = false;
            }

            if (ImGui::SliderFloat("Roughness", &active_mat->roughness, 0.0f, 1.0f, "%.2f")) {
                active_mat->is_builtin_preset = false;
            }
            HelpTooltip("0.0 = mirror glossy reflections, 1.0 = completely matte diffuse scattering.");

            if (ImGui::SliderFloat("Metallic", &active_mat->metallic, 0.0f, 1.0f, "%.2f")) {
                active_mat->is_builtin_preset = false;
            }
            HelpTooltip("0.0 = non-metal dielectric (plastic, wood, stone), 1.0 = full metallic conductor (iron, gold, brass).");

            if (ImGui::SliderFloat("Emissive Intensity", &active_mat->emissive, 0.0f, 10.0f, "%.2f")) {
                active_mat->is_builtin_preset = false;
            }
            HelpTooltip("Self-illuminating surface glow, amplified by HDR bloom.");

            if (active_mat->emissive > 0.001f) {
                if (ImGui::ColorEdit3("Emissive Color", active_mat->emissive_color)) {
                    active_mat->is_builtin_preset = false;
                }
            }
        }

        // Texture Slots
        if (ImGui::CollapsingHeader("Texture Maps", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextUnformatted("Albedo Map");
            if (!active_mat->albedo_path.empty()) {
                auto fname = path_utf8(std::filesystem::u8path(active_mat->albedo_path).filename());
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "Loaded: %s", fname.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", active_mat->albedo_path.c_str());
            } else {
                ImGui::TextDisabled("None (Solid Color)");
            }
            if (ImGui::Button("Browse Albedo...", ImVec2(130, 24))) {
                wchar_t filename[32768] = L"";
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
                ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    std::string u8path = to_utf8(filename);
                    uint16_t tid = load_asset(u8path, false);
                    if (tid) {
                        active_mat->albedo_tex_id = tid;
                        active_mat->albedo_path = path_utf8(std::filesystem::absolute(std::filesystem::u8path(u8path)).lexically_normal());
                        active_mat->is_builtin_preset = false;
                    }
                }
            }
            if (!active_mat->albedo_path.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear##Albedo", ImVec2(60, 24))) {
                    active_mat->albedo_path.clear();
                    active_mat->albedo_tex_id = 0;
                }
            }

            ImGui::Spacing();
            ImGui::TextUnformatted("Normal Map");
            if (!active_mat->normal_path.empty()) {
                auto fname = path_utf8(std::filesystem::u8path(active_mat->normal_path).filename());
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Loaded: %s", fname.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", active_mat->normal_path.c_str());
            } else {
                ImGui::TextDisabled("None (Geometric Normals)");
            }
            if (ImGui::Button("Browse Normal...", ImVec2(130, 24))) {
                wchar_t filename[32768] = L"";
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
                ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    std::string u8path = to_utf8(filename);
                    uint16_t tid = load_asset(u8path, false);
                    if (tid) {
                        active_mat->normal_tex_id = tid;
                        active_mat->normal_path = path_utf8(std::filesystem::absolute(std::filesystem::u8path(u8path)).lexically_normal());
                        active_mat->is_builtin_preset = false;
                    }
                }
            }
            if (!active_mat->normal_path.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear##Normal", ImVec2(60, 24))) {
                    active_mat->normal_path.clear();
                    active_mat->normal_tex_id = 0;
                }
            }
        }

        // UV Coordinates
        if (ImGui::CollapsingHeader("UV Coordinates", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Tiling", active_mat->uv_tiling, 0.05f, 0.01f, 100.0f, "%.2f");
            ImGui::DragFloat2("Offset", active_mat->uv_offset, 0.02f, -100.0f, 100.0f, "%.2f");
        }

        // Alpha & Blending
        if (ImGui::CollapsingHeader("Alpha & Transparency", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* alpha_modes[] = { "Opaque", "Mask (Cutoff)", "Blend (Transparent)" };
            int current_mode = std::clamp(active_mat->alpha_mode, 0, 2);
            if (ImGui::Combo("Alpha Mode", &current_mode, alpha_modes, IM_ARRAYSIZE(alpha_modes))) {
                active_mat->alpha_mode = current_mode;
                active_mat->is_builtin_preset = false;
            }
            if (active_mat->alpha_mode == 1) {
                ImGui::SliderFloat("Alpha Cutoff", &active_mat->alpha_cutoff, 0.0f, 1.0f, "%.2f");
                HelpTooltip("Pixels with opacity below this threshold are discarded.");
            }
        }
    } else {
        ImGui::TextDisabled("Select or create a material to view its properties.");
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace GM3D::Studio
