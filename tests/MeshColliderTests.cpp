// Mesh collider regression test (no D3D, no window): drives PhysicsWorld directly.
//
// The point of these cases is that a mesh collider is the model's actual surface, not its
// bounding box: a box dropped next to the rim of a disc-shaped trimesh must fall past it.
#include "physics/PhysicsWorld.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* message) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", message);
    if (!ok) ++failures;
}

float settle_and_read_y(PhysicsWorld& world, uint32_t handle, int frames) {
    SharedEntityData slot{};
    slot.flags = EntityFlags::IS_RIGIDBODY;
    slot.physics_id = handle;
    for (int i = 0; i < frames; ++i) {
        world.update(1.0f / 60.0f);
        world.sync_to_shared_entities(&slot, 1);
    }
    return slot.py;
}

// Axis-aligned cube centred on the origin with half extent h.
void build_cube(float h, std::vector<float>& positions, std::vector<uint32_t>& indices) {
    positions = {
        -h, -h, -h,  h, -h, -h,  h,  h, -h, -h,  h, -h,
        -h, -h,  h,  h, -h,  h,  h,  h,  h, -h,  h,  h,
    };
    indices = {
        0, 2, 1, 0, 3, 2,  4, 5, 6, 4, 6, 7,
        0, 1, 5, 0, 5, 4,  3, 7, 6, 3, 6, 2,
        0, 4, 7, 0, 7, 3,  1, 2, 6, 1, 6, 5,
    };
}

// Flat disc of radius r at y = 0 as a triangle fan: a trimesh whose bounding box is a square,
// so a collider built from the AABB instead of the triangles would catch things it should not.
void build_disc(float r, int segments, std::vector<float>& positions, std::vector<uint32_t>& indices) {
    positions = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < segments; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        positions.push_back(std::cos(a) * r);
        positions.push_back(0.0f);
        positions.push_back(std::sin(a) * r);
    }
    for (int i = 0; i < segments; ++i) {
        indices.push_back(0);
        indices.push_back(1 + static_cast<uint32_t>(i));
        indices.push_back(1 + static_cast<uint32_t>((i + 1) % segments));
    }
}

} // namespace

int main() {
    PhysicsWorld world;
    if (!world.initialize()) {
        std::printf("FAIL: physics world did not initialize\n");
        return 1;
    }
    world.set_gravity(0.0f, -9.81f, 0.0f);

    // Floor: a static box whose top face is y = 0.
    RigidBodyDesc floor_desc{};
    floor_desc.shape_type = BodyShapeType::Box;
    floor_desc.motion_type = MotionType::Static;
    floor_desc.position[0] = 0.0f; floor_desc.position[1] = -0.5f; floor_desc.position[2] = 0.0f;
    floor_desc.half_extents[0] = 50.0f; floor_desc.half_extents[1] = 0.5f; floor_desc.half_extents[2] = 50.0f;
    floor_desc.friction = 0.7f;
    floor_desc.restitution = 0.0f;
    const uint32_t floor_body = world.create_body(floor_desc);
    check(floor_body != 0, "static floor body created");

    // Static trimesh: a disc of radius 2 centred at y = 5.
    std::vector<float> disc_positions;
    std::vector<uint32_t> disc_indices;
    build_disc(2.0f, 24, disc_positions, disc_indices);

    RigidBodyDesc platform_desc{};
    platform_desc.shape_type = BodyShapeType::Mesh;
    platform_desc.motion_type = MotionType::Static;
    platform_desc.position[1] = 5.0f;
    platform_desc.friction = 0.7f;
    platform_desc.restitution = 0.0f;
    platform_desc.mesh_positions = disc_positions.data();
    platform_desc.mesh_vertex_count = static_cast<uint32_t>(disc_positions.size() / 3);
    platform_desc.mesh_indices = disc_indices.data();
    platform_desc.mesh_index_count = static_cast<uint32_t>(disc_indices.size());
    MeshColliderKind platform_kind = MeshColliderKind::None;
    const uint32_t platform_body = world.create_body(platform_desc, &platform_kind);
    check(platform_body != 0, "static mesh body created");
    check(platform_kind == MeshColliderKind::Triangles, "static mesh collider is a triangle mesh");

    // A small box dropped onto the middle of the disc must stop on it.
    std::vector<float> cube_positions;
    std::vector<uint32_t> cube_indices;
    build_cube(0.25f, cube_positions, cube_indices);

    RigidBodyDesc on_disc{};
    on_disc.shape_type = BodyShapeType::Box;
    on_disc.motion_type = MotionType::Dynamic;
    on_disc.position[0] = 0.0f; on_disc.position[1] = 8.0f; on_disc.position[2] = 0.0f;
    on_disc.half_extents[0] = 0.25f; on_disc.half_extents[1] = 0.25f; on_disc.half_extents[2] = 0.25f;
    on_disc.mass = 2.0f; on_disc.friction = 0.7f; on_disc.restitution = 0.0f;
    const uint32_t on_disc_body = world.create_body(on_disc);
    const float on_disc_y = settle_and_read_y(world, on_disc_body, 180);
    std::printf("     box on disc y = %.3f (expected 5.25)\n", on_disc_y);
    check(std::fabs(on_disc_y - 5.25f) < 0.1f, "box dropped on the disc rests on its surface");

    // The same box 2.5 m off-centre is inside the disc's bounding box but outside the disc, so
    // it must fall through to the floor. An AABB or box-substituted collider would hold it up.
    RigidBodyDesc past_rim{};
    past_rim.shape_type = BodyShapeType::Box;
    past_rim.motion_type = MotionType::Dynamic;
    past_rim.position[0] = 2.5f; past_rim.position[1] = 8.0f; past_rim.position[2] = 0.0f;
    past_rim.half_extents[0] = 0.25f; past_rim.half_extents[1] = 0.25f; past_rim.half_extents[2] = 0.25f;
    past_rim.mass = 2.0f; past_rim.friction = 0.7f; past_rim.restitution = 0.0f;
    const uint32_t past_rim_body = world.create_body(past_rim);
    const float past_rim_y = settle_and_read_y(world, past_rim_body, 240);
    check(std::fabs(past_rim_y - 0.25f) < 0.15f, "box dropped past the rim falls to the floor");

    // Dynamic mesh collider: a triangle soup cannot supply mass properties, so it is built as a
    // convex hull instead - and must land flush on the floor.
    RigidBodyDesc hull{};
    hull.shape_type = BodyShapeType::Mesh;
    hull.motion_type = MotionType::Dynamic;
    hull.position[0] = 6.0f; hull.position[1] = 6.0f; hull.position[2] = 0.0f;
    hull.mass = 5.0f; hull.friction = 0.7f; hull.restitution = 0.0f;
    std::vector<float> big_cube_positions;
    std::vector<uint32_t> big_cube_indices;
    build_cube(1.0f, big_cube_positions, big_cube_indices);
    hull.mesh_positions = big_cube_positions.data();
    hull.mesh_vertex_count = static_cast<uint32_t>(big_cube_positions.size() / 3);
    hull.mesh_indices = big_cube_indices.data();
    hull.mesh_index_count = static_cast<uint32_t>(big_cube_indices.size());
    MeshColliderKind hull_kind = MeshColliderKind::None;
    const uint32_t hull_body = world.create_body(hull, &hull_kind);
    check(hull_body != 0, "dynamic mesh body created");
    check(hull_kind == MeshColliderKind::ConvexHull, "dynamic mesh collider is a convex hull");
    const float hull_y = settle_and_read_y(world, hull_body, 240);
    check(std::fabs(hull_y - 1.0f) < 0.1f, "convex hull of a 2 m cube rests flush on the floor");

    // A mesh collider without geometry must not create a broken body: it falls back to a box
    // built from the primitive half extents.
    RigidBodyDesc empty_mesh{};
    empty_mesh.shape_type = BodyShapeType::Mesh;
    empty_mesh.motion_type = MotionType::Static;
    empty_mesh.position[1] = 20.0f;
    empty_mesh.half_extents[0] = 3.0f; empty_mesh.half_extents[1] = 1.0f; empty_mesh.half_extents[2] = 2.0f;
    MeshColliderKind empty_kind = MeshColliderKind::Triangles;
    const uint32_t empty_body = world.create_body(empty_mesh, &empty_kind);
    check(empty_body != 0, "mesh collider with no geometry still creates a body");
    check(empty_kind == MeshColliderKind::None, "mesh collider with no geometry reports a fallback");

    // Degenerate triangles only (zero area) must fall back too, not produce an empty shape.
    std::vector<float> degenerate_positions = { 0, 0, 0, 1, 0, 0, 2, 0, 0 };
    std::vector<uint32_t> degenerate_indices = { 0, 1, 2 };
    RigidBodyDesc degenerate{};
    degenerate.shape_type = BodyShapeType::Mesh;
    degenerate.motion_type = MotionType::Static;
    degenerate.position[1] = 30.0f;
    degenerate.mesh_positions = degenerate_positions.data();
    degenerate.mesh_vertex_count = 3;
    degenerate.mesh_indices = degenerate_indices.data();
    degenerate.mesh_index_count = 3;
    MeshColliderKind degenerate_kind = MeshColliderKind::Triangles;
    const uint32_t degenerate_body = world.create_body(degenerate, &degenerate_kind);
    check(degenerate_body != 0, "degenerate triangle mesh still creates a body");
    check(degenerate_kind == MeshColliderKind::None, "degenerate triangle mesh reports a fallback");

    world.shutdown();
    if (failures == 0) std::printf("All mesh collider checks passed\n");
    return failures == 0 ? 0 : 1;
}
