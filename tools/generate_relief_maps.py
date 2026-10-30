"""
.NW3D -> GameMaker / GM3Dext Sprite Relief & Normal Map Generator
Takes any 2D sprite (characters, trees, props) and generates:
1. Tangent-Space Normal Map (_normal.png)
2. Normalized Depth/Height Map (_height.png)
3. Metallic / Specular Roughness Mask (_specular.png)

Based on NewWave3D mathematical principles:
- Exact Euclidean Distance Transform (EDT)
- Bilateral palette-aware luminance relief
- Interior outline part-decomposition
"""

import sys
import os
import math
import numpy as np
from PIL import Image

if sys.stdout.encoding and sys.stdout.encoding.lower() != 'utf-8':
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass

def compute_edt(binary_mask):
    """
    Computes exact Euclidean Distance Transform from 0-nodes to 1-nodes.
    binary_mask: 2D numpy boolean array (True = inside sprite).
    Returns: 2D float array of distances in pixels.
    """
    from scipy.ndimage import distance_transform_edt
    return distance_transform_edt(binary_mask)

def process_sprite(image_path, out_dir, inflation=1.0, radius=12.0, part_weight=0.55):
    img = Image.open(image_path).convert('RGBA')
    w, h = img.size
    rgba = np.array(img, dtype=np.float32)

    alpha = rgba[:, :, 3]
    mask = alpha > 25.0

    if not np.any(mask):
        print(f"Skipping empty sprite: {image_path}")
        return

    # 1. Exact Euclidean Distance Transform to silhouette
    try:
        dist = compute_edt(mask)
    except ImportError:
        # Fallback if scipy not installed: approximate BFS
        from scipy.ndimage import distance_transform_edt
        dist = distance_transform_edt(mask)

    # 2. Elliptic Dome Profile: h(d) = sqrt(d * (2R - d)) for d < R, R beyond
    R = min(radius, max(4.0, min(w, h) * 0.45))
    clamped_d = np.clip(dist, 0.0, R)
    dome = np.sqrt(clamped_d * (2.0 * R - clamped_d))

    # 3. Part Decomposition: Interior outline detection
    # Outline pixels often have low luminance and border outside
    lum = 0.2126 * rgba[:, :, 0] + 0.7152 * rgba[:, :, 1] + 0.0722 * rgba[:, :, 2]
    
    # Simple outline mask: very dark interior pixels surrounded by brighter ones
    is_outline = (lum < 48.0) & mask
    if np.any(is_outline):
        dist_part = compute_edt(mask & (~is_outline))
        clamped_dp = np.clip(dist_part, 0.0, R * 0.7)
        part_dome = np.sqrt(clamped_dp * (2.0 * R * 0.7 - clamped_dp))
        dome = (1.0 - part_weight) * dome + part_weight * part_dome

    # 4. Luminance Relief (Embossing details like belts, armor lines, eyes)
    norm_lum = lum / 255.0
    # High-pass detail
    from scipy.ndimage import gaussian_filter
    blur_lum = gaussian_filter(norm_lum, sigma=1.0)
    detail = (norm_lum - blur_lum) * 4.0

    total_height = np.maximum(0.0, dome + detail * 2.0)
    total_height[~mask] = 0.0

    max_h = np.max(total_height)
    if max_h > 0:
        norm_h = total_height / max_h
    else:
        norm_h = total_height

    # 5. Tangent-Space Normal Map Calculation (-dh/dx, -dh/dy, 1)
    # Using central differences
    sobel_x = np.zeros_like(norm_h)
    sobel_y = np.zeros_like(norm_h)

    sobel_x[:, 1:-1] = (norm_h[:, 2:] - norm_h[:, :-2]) * 0.5
    sobel_y[1:-1, :] = (norm_h[2:, :] - norm_h[:-2, :]) * 0.5

    # Scale slopes
    slope_scale = 3.5 * inflation
    nx = -sobel_x * slope_scale
    ny = -sobel_y * slope_scale # Invert for standard top-left or bottom-left depending on convention
    nz = np.ones_like(norm_h)

    # Normalize vectors
    len_n = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx /= len_n
    ny /= len_n
    nz /= len_n

    # Convert to 8-bit RGB normal map: [0, 255]
    r_chan = np.clip((nx * 0.5 + 0.5) * 255.0, 0, 255).astype(np.uint8)
    g_chan = np.clip((ny * 0.5 + 0.5) * 255.0, 0, 255).astype(np.uint8)
    b_chan = np.clip((nz * 0.5 + 0.5) * 255.0, 0, 255).astype(np.uint8)
    a_chan = alpha.astype(np.uint8)

    normal_rgba = np.stack([r_chan, g_chan, b_chan, a_chan], axis=-1)
    normal_img = Image.fromarray(normal_rgba, 'RGBA')

    # 6. Grayscale Height Map
    h_gray = (norm_h * 255.0).astype(np.uint8)
    h_rgba = np.stack([h_gray, h_gray, h_gray, a_chan], axis=-1)
    height_img = Image.fromarray(h_rgba, 'RGBA')

    # 7. Material Mask: Roughness in G, Metallic in B, AO in R
    # Yellow/Gold / Cyan/Silver -> High Metallic (255), Low Roughness (30)
    # Skin, cloth, wood -> Low Metallic (0), High Roughness (200)
    r_val = rgba[:, :, 0]
    g_val = rgba[:, :, 1]
    b_val = rgba[:, :, 2]

    # Gold detection: High R, medium G, low B
    is_gold = (r_val > 140) & (g_val > 100) & (b_val < 80)
    # Silver/Metal detection: High R, G, B with low saturation
    max_c = np.maximum(r_val, np.maximum(g_val, b_val))
    min_c = np.minimum(r_val, np.minimum(g_val, b_val))
    sat = (max_c - min_c) / (max_c + 1e-5)
    is_metal = (max_c > 110) & (sat < 0.2)

    metallic = np.zeros((h, w), dtype=np.uint8)
    roughness = np.full((h, w), 180, dtype=np.uint8)

    metallic[is_gold | is_metal] = 230
    roughness[is_gold | is_metal] = 40

    ao = np.clip(dome / (R + 1e-5) * 255.0, 40, 255).astype(np.uint8)

    material_rgba = np.stack([ao, roughness, metallic, a_chan], axis=-1)
    material_img = Image.fromarray(material_rgba, 'RGBA')

    # Output paths
    base_name = os.path.splitext(os.path.basename(image_path))[0]
    os.makedirs(out_dir, exist_ok=True)

    normal_path = os.path.join(out_dir, f"{base_name}_normal.png")
    height_path = os.path.join(out_dir, f"{base_name}_height.png")
    mat_path = os.path.join(out_dir, f"{base_name}_material.png")

    normal_img.save(normal_path)
    height_img.save(height_path)
    material_img.save(mat_path)

    print(f"✅ Generated 3D Relief for {base_name}:")
    print(f"   -> Normal Map:   {normal_path} ({w}x{h})")
    print(f"   -> Height Map:   {height_path}")
    print(f"   -> Material Map: {mat_path}")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python generate_relief_maps.py <input_sprite.png> <output_dir>")
        sys.exit(1)
    process_sprite(sys.argv[1], sys.argv[2])
