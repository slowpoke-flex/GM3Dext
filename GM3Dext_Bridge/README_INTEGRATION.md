# MOBA 2D -> 3D Relief & Lighting Bridge (NewWave3D + GM3Dext)

This bridge connects **NewWave3D continuous mathematical relief synthesis** with **GameMaker** and **GM3Dext**.

---

## 📂 Folder Contents

- **`preview_3d_moba.html`**:
  - Interactive HTML preview. Double click to open in any web browser!
  - Move your mouse around to move a 3D point light over the Paladin and the Ice Tree.
  - Switch between **3D PBR Relief**, **Flat 2D**, and **Normal Map Only**.
  - Adjust light height Z with the mouse wheel.

- **`generate_relief_maps.py`**:
  - Standalone generator script.
  - Takes any 2D sprite (`.png`) and outputs:
    - `_normal.png`: Tangent-space 3D Normal Map
    - `_height.png`: Continuous depth/elevation map
    - `_material.png`: Roughness + Metallic mask

- **`input_sprites/`**:
  - Original 2D sprites copied from `C:\GameDev\projects\moba-gm\sprites` (`tree_ice.png`, `paladin_idle_0.png`).

- **`generated/`**:
  - Ready-to-use 1:1 Normal maps and Height maps for the Paladin and Ice Tree.

- **`gamemaker_assets/`**:
  - Ready-to-import GameMaker files:
    - `sh_moba_pbr.vsh` (Vertex Shader)
    - `sh_moba_pbr.fsh` (Fragment Shader)
    - `scr_draw_sprite_pbr.gml` (GML helper function)

---

## 🎮 How to Use in GameMaker (`moba-gm`)

1. **Import the Shader**:
   - In GameMaker IDE, right click `Shaders` -> `Create Shader` -> name it `sh_moba_pbr`.
   - Replace the Vertex shader code with `sh_moba_pbr.vsh`.
   - Replace the Fragment shader code with `sh_moba_pbr.fsh`.

2. **Import the Normal Map Sprite**:
   - Create a new sprite named `spr_h_paladin_idle_normal`.
   - Import `generated/paladin_idle_0_normal.png`.

3. **Render in Draw Event**:
   In your paladin or tree object `Draw` event, simply call:
   ```gml
   // Draw with dynamic light centered on mouse or spell
   draw_sprite_pbr(spr_h_paladin_idle, spr_h_paladin_idle_normal, image_index, x, y, 
                   mouse_x, mouse_y, 80, 
                   1.0, 0.6, 0.2, 450);
   ```

4. **Connect with GM3Dext**:
   - For true 3D projectiles (ballistic spells with Z height) and Jolt Physics ragdolls, pass the entity positions through `GM3Dext` shared buffer (`C:\GM3Dext\gml_example\scr_gm3d.gml`).
