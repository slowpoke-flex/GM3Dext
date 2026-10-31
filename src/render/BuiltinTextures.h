#pragma once

#include <cstdint>

namespace GM3D {

// Stable IDs for the textures the engine creates or loads at startup.
//
// 0..5 are generated in code (debug patterns plus the baked terrain albedo), so a build
// with no assets folder on disk still renders something sensible. 6..12 are real PBR
// sets loaded from assets/textures/ (CC0, see assets/textures/CREDITS.md); each one
// carries an albedo and a normal map. If the folder is missing the IDs stay unassigned
// and every lookup falls back to the procedural textures.
namespace TexId {

constexpr uint16_t WHITE = 0;          // 1x1 white, neutral tint
constexpr uint16_t UV_CHECKER = 1;     // debug checkerboard
constexpr uint16_t TECH_GRID = 2;      // debug sci-fi grid
constexpr uint16_t WOOD_PROCEDURAL = 3;// procedural planks
constexpr uint16_t HAZARD = 4;         // procedural hazard metal
constexpr uint16_t TERRAIN = 5;        // baked multi-biome terrain albedo

// Loaded CC0 sets (albedo + normal).
constexpr uint16_t GRASS = 6;          // meadow soil + moss, the terrain ground detail
constexpr uint16_t ROCK = 7;
constexpr uint16_t SAND = 8;
constexpr uint16_t COBBLE = 9;
constexpr uint16_t WOOD = 10;
constexpr uint16_t METAL = 11;
constexpr uint16_t TURF = 12;          // dense green turf, for materials rather than ground

// RGBA8 terrain layer weights for the detail-texture blend (terrain system owns it).
constexpr uint16_t TERRAIN_SPLAT = 13;

// User/editor textures start here.
constexpr uint16_t FIRST_DYNAMIC = 32;

} // namespace TexId

// Terrain layer order expected by the detail blend in the terrain pixel shader.
enum TerrainLayerSlot : int {
    kLayerGrass = 0,
    kLayerRock = 1,
    kLayerSand = 2,
    kLayerCobble = 3,
    kLayerCount = 4,
};

} // namespace GM3D
