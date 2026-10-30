#include "PhysicsWorld.h"
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <iostream>
#include <cmath>
#include <thread>
#include <algorithm>

BPLayerInterfaceImpl::BPLayerInterfaceImpl() {
    mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
    mObjectToBroadPhase[Layers::MOVING]     = BroadPhaseLayers::MOVING;
}

JPH::BroadPhaseLayer BPLayerInterfaceImpl::GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const {
    JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
    return mObjectToBroadPhase[inLayer];
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char* BPLayerInterfaceImpl::GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const {
    switch ((JPH::BroadPhaseLayer::Type)inLayer) {
    case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
    case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
    default: JPH_ASSERT(false); return "INVALID";
    }
}
#endif

bool ObjectVsBroadPhaseLayerFilterImpl::ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const {
    switch (inLayer1) {
    case Layers::NON_MOVING:
        return inLayer2 == BroadPhaseLayers::MOVING;
    case Layers::MOVING:
        return true;
    default:
        return false;
    }
}

bool ObjectLayerPairFilterImpl::ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const {
    switch (inObject1) {
    case Layers::NON_MOVING:
        return inObject2 == Layers::MOVING;
    case Layers::MOVING:
        return true;
    default:
        return false;
    }
}

JPH::ValidateResult ContactListenerImpl::OnContactValidate(const JPH::Body&, const JPH::Body&, JPH::RVec3Arg, const JPH::CollideShapeResult&) {
    return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
}

void ContactListenerImpl::OnContactAdded(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&, JPH::ContactSettings&) {
}

void ContactListenerImpl::OnContactPersisted(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&, JPH::ContactSettings&) {
}

void ContactListenerImpl::OnContactRemoved(const JPH::SubShapeIDPair&) {
}

// ------------------------------------------------------------------------------------------------
// PhysicsWorld Implementation
// ------------------------------------------------------------------------------------------------

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld() {
    shutdown();
}

bool PhysicsWorld::initialize() {
    if (m_initialized) {
        return true;
    }

    // 1. Initialize Jolt Allocator and Factory
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    // 2. Temp allocator (16 MB scratch buffer for simulations)
    constexpr JPH::uint temp_allocator_size = 16 * 1024 * 1024;
    m_temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(temp_allocator_size);

    // 3. Thread pool job system
    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    const unsigned int num_threads = hardware_threads > 1 ? hardware_threads - 1 : 1;
    m_job_system = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(num_threads)
    );

    // 4. Initialize Physics System
    constexpr JPH::uint cMaxBodies = 10240;
    constexpr JPH::uint cNumBodyMutexes = 0;
    constexpr JPH::uint cMaxBodyPairs = 10240;
    constexpr JPH::uint cMaxContactConstraints = 10240;

    m_physics_system = std::make_unique<JPH::PhysicsSystem>();
    m_physics_system->Init(
        cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
        m_bp_layer_interface, m_obj_vs_bp_filter, m_obj_layer_pair_filter
    );

    m_physics_system->SetContactListener(&m_contact_listener);
    m_physics_system->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    m_initialized = true;
    m_accumulator = 0.0f;
    return true;
}

void PhysicsWorld::shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_physics_system) {
        JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();
        for (const auto& [handle_id, body_id] : m_id_to_body) {
            bi.RemoveBody(body_id);
            bi.DestroyBody(body_id);
        }
    }

    m_id_to_body.clear();
    m_body_to_id.clear();
    m_body_history.clear();

    m_physics_system.reset();
    m_job_system.reset();
    m_temp_allocator.reset();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    m_initialized = false;
}

void PhysicsWorld::update(float delta_time) {
    if (!m_initialized || !m_physics_system) {
        return;
    }

    // Clamp delta time to prevent spiral of death
    constexpr float MAX_DELTA_TIME = 0.2f;
    if (delta_time > MAX_DELTA_TIME) {
        delta_time = MAX_DELTA_TIME;
    }

    m_accumulator += delta_time;

    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();

    // Fixed timestep simulation with transform history recording for smooth interpolation
    while (m_accumulator >= FIXED_TIME_STEP) {
        for (const auto& pair : m_id_to_body) {
            auto& h = m_body_history[pair.first];
            if (!h.has_prev) {
                h.prev.pos = bi.GetPosition(pair.second);
                h.prev.rot = bi.GetRotation(pair.second);
                h.curr = h.prev;
                h.has_prev = true;
            } else {
                h.prev = h.curr;
            }
        }

        constexpr int collision_steps = 1;
        m_physics_system->Update(FIXED_TIME_STEP, collision_steps, m_temp_allocator.get(), m_job_system.get());
        m_accumulator -= FIXED_TIME_STEP;

        for (const auto& pair : m_id_to_body) {
            auto& h = m_body_history[pair.first];
            h.curr.pos = bi.GetPosition(pair.second);
            h.curr.rot = bi.GetRotation(pair.second);
            h.has_prev = true;
        }
    }
}

uint32_t PhysicsWorld::create_body(const RigidBodyDesc& desc) {
    if (!m_initialized || !m_physics_system) {
        return 0;
    }

    JPH::RefConst<JPH::Shape> shape;

    switch (desc.shape_type) {
    case BodyShapeType::Box: {
        JPH::Vec3 half_extent(
            std::max(0.01f, desc.half_extents[0]),
            std::max(0.01f, desc.half_extents[1]),
            std::max(0.01f, desc.half_extents[2])
        );
        shape = new JPH::BoxShape(half_extent);
        break;
    }
    case BodyShapeType::Sphere: {
        float r = std::max(0.01f, desc.radius);
        shape = new JPH::SphereShape(r);
        break;
    }
    case BodyShapeType::Capsule: {
        float hh = std::max(0.01f, desc.half_height);
        float r = std::max(0.01f, desc.radius);
        shape = new JPH::CapsuleShape(hh, r);
        break;
    }
    case BodyShapeType::Cylinder: {
        float hh = std::max(0.01f, desc.half_height);
        float r = std::max(0.01f, desc.radius);
        shape = new JPH::CylinderShape(hh, r);
        break;
    }
    default:
        shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        break;
    }

    JPH::EMotionType motion_type = JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = Layers::MOVING;

    if (desc.motion_type == MotionType::Static) {
        motion_type = JPH::EMotionType::Static;
        layer = Layers::NON_MOVING;
    } else if (desc.motion_type == MotionType::Kinematic) {
        motion_type = JPH::EMotionType::Kinematic;
        layer = Layers::MOVING;
    }

    JPH::RVec3 pos(desc.position[0], desc.position[1], desc.position[2]);
    JPH::Quat rot(desc.rotation[0], desc.rotation[1], desc.rotation[2], desc.rotation[3]);

    JPH::BodyCreationSettings settings(shape, pos, rot, motion_type, layer);
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mLinearDamping = desc.linear_damping;
    settings.mAngularDamping = desc.angular_damping;

    if (motion_type == JPH::EMotionType::Dynamic && desc.mass > 0.0f) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
    }

    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();
    JPH::Body* body = bi.CreateBody(settings);
    if (body == nullptr) {
        return 0;
    }

    JPH::BodyID body_id = body->GetID();
    bi.AddBody(body_id, motion_type == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);

    uint32_t handle_id = m_next_handle_id++;
    m_id_to_body[handle_id] = body_id;
    m_body_to_id[body_id.GetIndexAndSequenceNumber()] = handle_id;

    return handle_id;
}

uint32_t PhysicsWorld::create_heightfield_body(float px, float py, float pz, float world_size, uint32_t sample_count, const float* heights) {
    if (!m_initialized || !m_physics_system || sample_count < 2 || heights == nullptr) return 0;

    float cell_size = world_size / static_cast<float>(sample_count - 1);
    JPH::Vec3 offset(px - world_size * 0.5f, py, pz - world_size * 0.5f);
    JPH::Vec3 scale(cell_size, 1.0f, cell_size);

    JPH::HeightFieldShapeSettings settings(heights, offset, scale, sample_count);
    settings.mBlockSize = 2;
    settings.mBitsPerSample = 16;

    auto result = settings.Create();
    if (!result.IsValid()) {
        std::cerr << "Failed to create HeightFieldShape: " << result.GetError().c_str() << std::endl;
        return 0;
    }

    JPH::RefConst<JPH::Shape> shape = result.Get();
    JPH::BodyCreationSettings bcs(shape, JPH::RVec3(0, 0, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
    bcs.mFriction = 0.7f;
    bcs.mRestitution = 0.1f;

    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();
    JPH::Body* body = bi.CreateBody(bcs);
    if (!body) return 0;

    bi.AddBody(body->GetID(), JPH::EActivation::DontActivate);

    uint32_t handle_id = m_next_handle_id++;
    m_id_to_body[handle_id] = body->GetID();
    m_body_to_id[body->GetID().GetIndexAndSequenceNumber()] = handle_id;
    return handle_id;
}

bool PhysicsWorld::destroy_body(uint32_t body_id) {
    if (!m_initialized || !m_physics_system) {
        return false;
    }

    auto it = m_id_to_body.find(body_id);
    if (it == m_id_to_body.end()) {
        return false;
    }

    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();
    JPH::BodyID jph_body_id = it->second;

    bi.RemoveBody(jph_body_id);
    bi.DestroyBody(jph_body_id);

    m_body_history.erase(body_id);
    m_body_to_id.erase(jph_body_id.GetIndexAndSequenceNumber());
    m_id_to_body.erase(it);

    return true;
}

bool PhysicsWorld::set_linear_velocity(uint32_t body_id, float vx, float vy, float vz) {
    if (!m_initialized || !m_physics_system) return false;
    auto it = m_id_to_body.find(body_id);
    if (it == m_id_to_body.end()) return false;

    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();
    bi.SetLinearVelocity(it->second, JPH::Vec3(vx, vy, vz));
    return true;
}

bool PhysicsWorld::add_impulse(uint32_t body_id, float ix, float iy, float iz) {
    if (!m_initialized || !m_physics_system) return false;
    auto it = m_id_to_body.find(body_id);
    if (it == m_id_to_body.end()) return false;

    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();
    bi.AddImpulse(it->second, JPH::Vec3(ix, iy, iz));
    return true;
}

bool PhysicsWorld::add_force(uint32_t body_id, float fx, float fy, float fz) {
    if (!m_initialized || !m_physics_system) return false;
    auto it = m_id_to_body.find(body_id);
    if (it == m_id_to_body.end()) return false;

    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();
    bi.AddForce(it->second, JPH::Vec3(fx, fy, fz));
    return true;
}

bool PhysicsWorld::add_torque(uint32_t body_id, float tx, float ty, float tz) {
    if (!m_initialized || !m_physics_system) return false;
    auto it = m_id_to_body.find(body_id);
    if (it == m_id_to_body.end()) return false;

    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();
    bi.AddTorque(it->second, JPH::Vec3(tx, ty, tz));
    return true;
}

void PhysicsWorld::set_gravity(float gx, float gy, float gz) {
    if (m_initialized && m_physics_system) {
        m_physics_system->SetGravity(JPH::Vec3(gx, gy, gz));
    }
}

bool PhysicsWorld::set_friction(uint32_t body_id, float friction) {
    if (!m_initialized || !m_physics_system) return false;
    auto it = m_id_to_body.find(body_id);
    if (it == m_id_to_body.end()) return false;
    m_physics_system->GetBodyInterface().SetFriction(it->second, friction);
    return true;
}

bool PhysicsWorld::set_restitution(uint32_t body_id, float restitution) {
    if (!m_initialized || !m_physics_system) return false;
    auto it = m_id_to_body.find(body_id);
    if (it == m_id_to_body.end()) return false;
    m_physics_system->GetBodyInterface().SetRestitution(it->second, restitution);
    return true;
}

bool PhysicsWorld::set_position_and_rotation(uint32_t body_id, float x, float y, float z, float qx, float qy, float qz, float qw) {
    if (!m_initialized || !m_physics_system) return false;
    auto it = m_id_to_body.find(body_id);
    if (it == m_id_to_body.end()) return false;
    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();
    bi.SetPositionAndRotation(it->second, JPH::RVec3(x, y, z), JPH::Quat(qx, qy, qz, qw), JPH::EActivation::Activate);
    m_body_history.erase(body_id);
    return true;
}

RaycastHitResult PhysicsWorld::cast_ray(float from_x, float from_y, float from_z, float to_x, float to_y, float to_z) {
    RaycastHitResult result{};
    if (!m_initialized || !m_physics_system) return result;

    JPH::RVec3 from(from_x, from_y, from_z);
    JPH::Vec3 dir(to_x - from_x, to_y - from_y, to_z - from_z);
    JPH::RRayCast ray{ from, dir };

    JPH::RayCastResult hit;
    bool has_hit = m_physics_system->GetNarrowPhaseQuery().CastRay(ray, hit);

    if (has_hit) {
        result.has_hit = true;
        result.fraction = hit.mFraction;
        JPH::RVec3 hit_pos = ray.GetPointOnRay(hit.mFraction);
        result.hit_x = static_cast<float>(hit_pos.GetX());
        result.hit_y = static_cast<float>(hit_pos.GetY());
        result.hit_z = static_cast<float>(hit_pos.GetZ());

        JPH::BodyLockRead lock(m_physics_system->GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            JPH::Vec3 normal = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hit_pos);
            result.normal_x = normal.GetX();
            result.normal_y = normal.GetY();
            result.normal_z = normal.GetZ();

            auto it = m_body_to_id.find(hit.mBodyID.GetIndexAndSequenceNumber());
            if (it != m_body_to_id.end()) {
                result.body_id = it->second;
            }
        }
    }

    return result;
}

void PhysicsWorld::sync_to_shared_entities(SharedEntityData* entities, uint32_t count) {
    if (!m_initialized || !m_physics_system || entities == nullptr) {
        return;
    }

    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();
    const float alpha = std::clamp(m_accumulator / FIXED_TIME_STEP, 0.0f, 1.0f);

    for (uint32_t i = 0; i < count; ++i) {
        SharedEntityData& e = entities[i];
        if ((e.flags & EntityFlags::IS_RIGIDBODY) && e.physics_id != 0) {
            auto it = m_id_to_body.find(e.physics_id);
            if (it != m_id_to_body.end()) {
                auto hist_it = m_body_history.find(e.physics_id);
                if (hist_it != m_body_history.end() && hist_it->second.has_prev) {
                    const auto& h = hist_it->second;
                    JPH::RVec3 ipos = h.prev.pos + (h.curr.pos - h.prev.pos) * alpha;
                    JPH::Quat irot = h.prev.rot.SLERP(h.curr.rot, alpha);

                    e.px = static_cast<float>(ipos.GetX());
                    e.py = static_cast<float>(ipos.GetY());
                    e.pz = static_cast<float>(ipos.GetZ());

                    e.qx = irot.GetX();
                    e.qy = irot.GetY();
                    e.qz = irot.GetZ();
                    e.qw = irot.GetW();
                } else {
                    JPH::BodyID jph_id = it->second;
                    JPH::RVec3 pos = bi.GetPosition(jph_id);
                    JPH::Quat rot = bi.GetRotation(jph_id);

                    e.px = static_cast<float>(pos.GetX());
                    e.py = static_cast<float>(pos.GetY());
                    e.pz = static_cast<float>(pos.GetZ());

                    e.qx = rot.GetX();
                    e.qy = rot.GetY();
                    e.qz = rot.GetZ();
                    e.qw = rot.GetW();
                }
            }
        }
    }
}

void PhysicsWorld::sync_from_shared_entities(const SharedEntityData* entities, uint32_t count) {
    if (!m_initialized || !m_physics_system || entities == nullptr) {
        return;
    }

    JPH::BodyInterface& bi = m_physics_system->GetBodyInterface();

    for (uint32_t i = 0; i < count; ++i) {
        const SharedEntityData& e = entities[i];
        if ((e.flags & EntityFlags::IS_KINEMATIC) && e.physics_id != 0) {
            auto it = m_id_to_body.find(e.physics_id);
            if (it != m_id_to_body.end()) {
                JPH::BodyID jph_id = it->second;
                JPH::RVec3 pos(e.px, e.py, e.pz);
                JPH::Quat rot(e.qx, e.qy, e.qz, e.qw);

                bi.SetPositionAndRotation(jph_id, pos, rot, JPH::EActivation::Activate);
            }
        }
    }
}
