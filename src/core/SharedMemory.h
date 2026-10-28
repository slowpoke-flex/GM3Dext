#pragma once

#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)

/**
 * @brief Header of shared memory buffer GameMaker <-> DLL.
 * Located at byte 0..63 of buffer. Size: exactly 64 bytes.
 */
struct SharedBufferHeader {
    uint32_t magic;          // Buffer magic signature: 0x474D3344 ('GM3D')
    uint32_t version;        // Protocol version (e.g. 1)
    uint32_t entity_count;   // Active entity count in current frame
    uint32_t max_entities;   // Maximum capacity of buffer
    
    // Active camera parameters passed from GameMaker
    float camera_pos[3];     // X, Y, Z
    float camera_target[3];  // X, Y, Z (LookAt target)
    float camera_fov;        // Vertical FOV in degrees
    float camera_near;       // Near clipping plane
    float camera_far;        // Far clipping plane
    
    uint32_t flags;          // Global flags (debug mode, wireframe, etc.) (4 bytes)
    uint8_t  reserved[8];    // Padding to exactly 64 bytes (8 bytes)
};

/**
 * @brief Per-entity description in shared buffer.
 * Size: exactly 64 bytes (1 CPU cache line).
 * Ensures zero-copy access and direct upload to GPU Instance buffer.
 */
struct SharedEntityData {
    // Transform
    float px, py, pz;        // World Position (12 bytes)
    float qx, qy, qz, qw;    // Rotation Quaternion (16 bytes)
    float sx, sy, sz;        // Scale (12 bytes)

    // IDs and types
    uint32_t entity_id;      // Unique GameMaker instance ID (4 bytes)
    uint16_t mesh_id;        // Mesh ID in RenderWorld (2 bytes)
    uint16_t material_id;    // PBR Material ID in RenderWorld (2 bytes)
    
    // State and rendering flags
    // Bit 0: Visibility (0 = hidden, 1 = active)
    // Bit 1: Cast Shadow
    // Bit 2: Rigid Body (synced from Jolt to GameMaker)
    // Bit 3: Kinematic Body (controlled from GameMaker)
    uint32_t flags;          // Bitmask of flags (4 bytes)
    
    // Physics (Jolt Physics Handle)
    uint32_t physics_id;     // Rigid body ID in PhysicsWorld (4 bytes)
    
    // Custom user parameters
    float custom_param1;     // Emissive factor or animation timer (4 bytes)
    float custom_param2;     // Reserved / Extra parameter (4 bytes)
};

#pragma pack(pop)

// Compile-time static assertions for structure layout
static_assert(sizeof(SharedBufferHeader) == 64, "SharedBufferHeader must be exactly 64 bytes");
static_assert(sizeof(SharedEntityData) == 64, "SharedEntityData must be exactly 64 bytes");

// Protocol constants
constexpr uint32_t GM3D_BUFFER_MAGIC = 0x474D3344; // 'GM3D' Little-Endian
constexpr uint32_t GM3D_PROTOCOL_VERSION = 1;

// Entity flags
namespace EntityFlags {
    inline constexpr uint32_t ACTIVE       = 1 << 0;
    inline constexpr uint32_t CAST_SHADOWS = 1 << 1;
    inline constexpr uint32_t CAST_SHADOW  = 1 << 1;
    inline constexpr uint32_t IS_RIGIDBODY = 1 << 2;
    inline constexpr uint32_t IS_KINEMATIC = 1 << 3;
    inline constexpr uint32_t IS_TRIGGER   = 1 << 4;
}
