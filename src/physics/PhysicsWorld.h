#pragma once

#include "core/SharedMemory.h"
#include <memory>
#include <unordered_map>
#include <vector>

// Jolt Physics core headers
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS(2);
}

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl();
    virtual JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override;
#endif
private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override;
};

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override;
};

class ContactListenerImpl : public JPH::ContactListener {
public:
    virtual JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override;
    virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;
    virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override;
    virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;
};

enum class BodyShapeType : uint32_t {
    Box = 0,
    Sphere = 1,
    Capsule = 2,
    Cylinder = 3
};

enum class MotionType : uint32_t {
    Static = 0,
    Kinematic = 1,
    Dynamic = 2
};

struct RigidBodyDesc {
    BodyShapeType shape_type = BodyShapeType::Box;
    MotionType motion_type = MotionType::Dynamic;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // Quaternion (X, Y, Z, W)
    float half_extents[3] = {0.5f, 0.5f, 0.5f};   // Half size for Box
    float radius = 0.5f;                          // Radius for Sphere / Capsule / Cylinder
    float half_height = 0.5f;                     // Half height for Capsule / Cylinder
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.3f;
    float linear_damping = 0.05f;
    float angular_damping = 0.05f;
};

struct RaycastHitResult {
    bool has_hit = false;
    uint32_t body_id = 0;
    float hit_x = 0.0f;
    float hit_y = 0.0f;
    float hit_z = 0.0f;
    float normal_x = 0.0f;
    float normal_y = 1.0f;
    float normal_z = 0.0f;
    float fraction = 1.0f;
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    bool initialize();
    void shutdown();
    void update(float delta_time);

    uint32_t create_body(const RigidBodyDesc& desc);
    uint32_t create_heightfield_body(float px, float py, float pz, float world_size, uint32_t sample_count, const float* heights);
    bool destroy_body(uint32_t body_id);

    bool set_linear_velocity(uint32_t body_id, float vx, float vy, float vz);
    bool add_impulse(uint32_t body_id, float ix, float iy, float iz);
    bool add_torque(uint32_t body_id, float tx, float ty, float tz);
    bool add_force(uint32_t body_id, float fx, float fy, float fz);
    void set_gravity(float gx, float gy, float gz);
    bool set_friction(uint32_t body_id, float friction);
    bool set_restitution(uint32_t body_id, float restitution);
    bool set_position_and_rotation(uint32_t body_id, float x, float y, float z, float qx, float qy, float qz, float qw);
    [[nodiscard]] uint32_t get_body_count() const noexcept { return static_cast<uint32_t>(m_id_to_body.size()); }

    RaycastHitResult cast_ray(float from_x, float from_y, float from_z, float to_x, float to_y, float to_z);

    void sync_to_shared_entities(SharedEntityData* entities, uint32_t count);
    void sync_from_shared_entities(const SharedEntityData* entities, uint32_t count);

    [[nodiscard]] bool is_initialized() const noexcept { return m_initialized; }

private:
    bool m_initialized = false;
    float m_accumulator = 0.0f;
    static constexpr float FIXED_TIME_STEP = 1.0f / 60.0f;

    std::unique_ptr<JPH::TempAllocatorImpl>   m_temp_allocator;
    std::unique_ptr<JPH::JobSystemThreadPool> m_job_system;
    std::unique_ptr<JPH::PhysicsSystem>       m_physics_system;

    BPLayerInterfaceImpl               m_bp_layer_interface;
    ObjectVsBroadPhaseLayerFilterImpl  m_obj_vs_bp_filter;
    ObjectLayerPairFilterImpl          m_obj_layer_pair_filter;
    ContactListenerImpl                m_contact_listener;

    struct TransformSample {
        JPH::RVec3 pos = JPH::RVec3::sZero();
        JPH::Quat  rot = JPH::Quat::sIdentity();
    };
    struct BodyHistory {
        TransformSample prev;
        TransformSample curr;
        bool has_prev = false;
    };

    std::unordered_map<uint32_t, JPH::BodyID> m_id_to_body;
    std::unordered_map<uint32_t, uint32_t>    m_body_to_id;
    std::unordered_map<uint32_t, BodyHistory> m_body_history;
    uint32_t m_next_handle_id = 1;
};
