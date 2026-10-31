"""Generate `scenes/meadow.json`, the violet meadow the Studio and the GameMaker build load.

The Studio is driven by hand, so a scene is easier to author as data: a smooth heightfield
of overlapping dunes, a short violet grass carpet, one animated character and a hazy
lavender sky. Everything is deterministic - rerun after editing a constant and the file
changes in exactly the way the constant says.

    python tools/meadow_scene.py
    build\\GM3DStudio.exe --scene scenes\\meadow.json --camera <printed> --screenshot shot.png

Two details are not obvious:

* `heights` replaces the procedural island generator, which builds a flat plaza ringed by
  mountains and a coastline. A meadow wants hills all the way to the fog.
* `splatmap` is a full-weight layer 0 everywhere. Without it the terrain macro albedo comes
  from the built-in green ramp and only the *painted* layers are honoured, so the scene's
  violet layer colours would be ignored.
"""
import json
import math
import os

# Scene constants. WORLD is small on purpose: the procedural carpet is generated on a fixed
# 500x500 grid, so a smaller world means tighter blade spacing (220 m -> 0.44 m).
WORLD = 220.0
GRID = 112
CHARACTER = (4.0, 2.0)
CAMERA = (-4.0, -11.86)
EYE_HEIGHT = 1.75

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   'scenes', 'meadow.json')


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def height(x, z):
    """Gentle dunes: three sine bands, ~115 m, ~74 m and ~45 m across."""
    h = 9.0
    h += 4.5 * math.sin(0.055 * x + 0.7) * math.cos(0.048 * z - 0.4)
    h += 2.2 * math.sin(0.085 * (x - z) + 1.9)
    h += 0.9 * math.cos(0.140 * (x + 0.6 * z) - 0.6)
    # Let the hills run to the mesh border: the silhouette should be hilltops fading into
    # haze, not a flat plateau edge.
    r = max(abs(x), abs(z)) / (WORLD * 0.5)
    return h - 3.0 * smoothstep(0.94, 1.0, r)


def main():
    splat = []
    for _ in range(GRID * GRID):
        splat.extend((255, 0, 0, 0))

    heights = []
    for gy in range(GRID):
        for gx in range(GRID):
            x = (gx / (GRID - 1) - 0.5) * WORLD
            z = (gy / (GRID - 1) - 0.5) * WORLD
            heights.append(round(height(x, z), 3))

    cam_y = height(*CAMERA) + EYE_HEIGHT
    target = (CHARACTER[0], height(*CHARACTER) + 1.1, CHARACTER[1])

    scene = {
        "name": "Violet Meadow",
        "version": 1,
        "environment": {
            "sun": {
                "pitch": 42.0,
                "yaw": 315.0,
                "color": [1.0, 0.93, 0.88],
                "intensity": 1.00,
            },
            "shadows": {
                "enabled": True, "strength": 0.80, "softness": 2.0, "coverage": 120.0,
                "csm_enabled": True, "quality": 2, "max_shadow_lights": 2,
            },
            "ambient": {"color": [0.44, 0.42, 0.70], "intensity": 0.75},
            "sky_color": [0.36, 0.34, 0.68],
            "fog": {"start": 40.0, "end": 150.0},
        },
        "terrain": {
            "enabled": True,
            "grid_size": GRID,
            "world_size": WORLD,
            "height_scale": 32.0,
            "seed": 1337,
            # 0: the detail photographs fight the palette on stylised ground, so the surface
            # is the flat macro colour and the blades carry the texture.
            "detail_scale": 0.0,
            "grass_enabled": True,
            "grass_density": 3.0,
            "grass_height": 0.22,
            "grass_width": 0.15,
            "grass_max_distance": 70.0,
            "grass_fade_range": 22.0,
            "grass_wind_speed": 1.2,
            "grass_wind_strength": 0.22,
            "grass_color_a": [0.10, 0.07, 0.22],
            "grass_color_b": [0.22, 0.17, 0.44],
            "heights": heights,
            "splatmap": splat,
            "layers": [
                {"name": "Meadow", "color": [0.11, 0.07, 0.24], "roughness": 0.86, "scale": 1.0},
                {"name": "Moss", "color": [0.07, 0.05, 0.17], "roughness": 0.90, "scale": 1.4},
                {"name": "Lavender", "color": [0.18, 0.13, 0.36], "roughness": 0.80, "scale": 0.8},
                {"name": "Rock", "color": [0.15, 0.13, 0.26], "roughness": 0.70, "scale": 1.8},
            ],
        },
        "materials": [
            {
                "id": 0, "name": "Wanderer",
                "base_color": [0.66, 0.64, 0.74, 1.0],
                "roughness": 0.55, "metallic": 0.0, "emissive": 0.0,
                "emissive_color": [1.0, 1.0, 1.0],
                "uv_tiling": [1.0, 1.0], "uv_offset": [0.0, 0.0],
                "alpha_mode": 0, "alpha_cutoff": 0.5,
            },
        ],
        "entities": [
            {
                "id": 1,
                "name": "Wanderer",
                "tag": "Character",
                "transform": {
                    "position": [CHARACTER[0], height(*CHARACTER), CHARACTER[1]],
                    "rotation": [0.0, 200.0, 0.0],
                    "scale": [1.0, 1.0, 1.0],
                },
                "render": {
                    "mesh_id": 0, "material_id": 0, "texture_id": 0,
                    "cast_shadow": True, "visible": True, "param1": 0.0, "param2": 0.0,
                    "model_path": "../3Dreadymodels/UAL1_Standard.glb",
                },
                "physics": {
                    "type": 0, "shape": 0,
                    "collider_size": [1.0, 1.0, 1.0], "collider_offset": [0.0, 0.0, 0.0],
                    "mass": 1.0, "friction": 0.6, "restitution": 0.1,
                },
                # 9 = Idle_Loop in UAL1_Standard.glb (43 clips, A_TPose first).
                "animator": {
                    "enabled": True, "clip_index": 9, "speed": 1.0,
                    "loop": True, "playing": True, "time": 0.0,
                },
            },
        ],
    }

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w', encoding='utf-8') as handle:
        json.dump(scene, handle, indent=1)

    print('wrote', OUT, os.path.getsize(OUT), 'bytes')
    print('camera: %.2f,%.2f,%.2f,%.2f,%.2f,%.2f' % (
        CAMERA[0], cam_y, CAMERA[1], target[0], target[1], target[2]))


if __name__ == '__main__':
    main()
