import os
import base64

def to_b64(path):
    with open(path, 'rb') as f:
        return 'data:image/png;base64,' + base64.b64encode(f.read()).decode('ascii')

pal_diff = to_b64('C:/GM3Dext_Bridge/input_sprites/paladin_idle_0.png')
pal_norm = to_b64('C:/GM3Dext_Bridge/generated/paladin_idle_0_normal.png')
pal_mat  = to_b64('C:/GM3Dext_Bridge/generated/paladin_idle_0_material.png')

tree_diff = to_b64('C:/GM3Dext_Bridge/input_sprites/tree_ice.png')
tree_norm = to_b64('C:/GM3Dext_Bridge/generated/tree_ice_normal.png')
tree_mat  = to_b64('C:/GM3Dext_Bridge/generated/tree_ice_material.png')

html_content = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MOBA 2D -> 3D Relief & Dynamic Lighting Preview</title>
    <style>
        :root {{
            --bg-color: #0b0f19;
            --panel-bg: rgba(18, 24, 38, 0.85);
            --cyan: #00e5ff;
            --amber: #ffb300;
        }}
        * {{
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            user-select: none;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
        }}
        body {{
            background: var(--bg-color);
            color: #fff;
            width: 100vw;
            height: 100vh;
            overflow: hidden;
            display: flex;
            flex-direction: column;
        }}
        #top-bar {{
            height: 52px;
            background: var(--panel-bg);
            border-bottom: 1px solid rgba(255, 255, 255, 0.1);
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 0 24px;
            z-index: 10;
        }}
        .brand {{
            display: flex;
            align-items: center;
            gap: 10px;
            font-weight: 800;
            font-size: 16px;
            letter-spacing: 0.5px;
        }}
        .dot-brand {{
            color: var(--cyan);
            font-size: 22px;
            line-height: 0;
        }}
        .badge {{
            background: rgba(0, 229, 255, 0.15);
            border: 1px solid rgba(0, 229, 255, 0.4);
            color: var(--cyan);
            font-size: 10px;
            font-weight: 700;
            padding: 3px 8px;
            border-radius: 12px;
        }}
        .controls {{
            display: flex;
            align-items: center;
            gap: 16px;
        }}
        .btn {{
            background: rgba(255, 255, 255, 0.08);
            border: 1px solid rgba(255, 255, 255, 0.2);
            color: #fff;
            padding: 6px 14px;
            border-radius: 8px;
            font-size: 12px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
        }}
        .btn:hover {{
            background: rgba(255, 255, 255, 0.18);
        }}
        .btn.active {{
            background: var(--cyan);
            color: #0b0f19;
            border-color: var(--cyan);
            font-weight: 700;
        }}
        #viewport {{
            flex: 1;
            position: relative;
            background: radial-gradient(circle at center, #151d30 0%, #080b12 100%);
            cursor: crosshair;
        }}
        canvas {{
            width: 100%;
            height: 100%;
            display: block;
        }}
        #instructions {{
            position: absolute;
            bottom: 18px;
            left: 50%;
            transform: translateX(-50%);
            background: rgba(18, 24, 38, 0.85);
            border: 1px solid rgba(255, 255, 255, 0.12);
            padding: 8px 20px;
            border-radius: 20px;
            font-size: 12px;
            color: #94a3b8;
            pointer-events: none;
        }}
        .key {{
            color: #fff;
            font-weight: 700;
        }}
        #inspect-card {{
            position: absolute;
            bottom: 20px;
            left: 20px;
            background: var(--panel-bg);
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-radius: 12px;
            padding: 12px;
            display: flex;
            gap: 12px;
            font-size: 11px;
            color: #94a3b8;
        }}
        .thumb-box {{
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 4px;
        }}
        .thumb-box img {{
            width: 48px;
            height: 48px;
            object-fit: contain;
            background: rgba(0, 0, 0, 0.4);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 6px;
            image-rendering: pixelated;
        }}
    </style>
</head>
<body>
    <div id="top-bar">
        <div class="brand">
            <span class="dot-brand">•</span>
            <span>MOBA 3D RELIEF BRIDGE</span>
            <span class="badge">GAME MAKER REAL-TIME PBR</span>
        </div>
        <div class="controls">
            <button id="btn-mode-pbr" class="btn active" onclick="setMode('pbr')">✨ 3D PBR Relief (NewWave)</button>
            <button id="btn-mode-flat" class="btn" onclick="setMode('flat')">📄 Flat 2D (Standard)</button>
            <button id="btn-mode-normal" class="btn" onclick="setMode('normal')">🔮 Normal Map Only</button>
            <button id="btn-light-color" class="btn" onclick="toggleLightColor()">🔥 Fireball Light</button>
        </div>
    </div>

    <div id="viewport">
        <canvas id="render-canvas"></canvas>
        <div id="instructions">
            <span><span class="key">Move Mouse:</span> Move 3D Light Source • <span class="key">Wheel:</span> Adjust Light Height Z</span>
        </div>
        <div id="inspect-card">
            <div class="thumb-box">
                <img src="{pal_diff}" alt="Diffuse">
                <span>Diffuse</span>
            </div>
            <div class="thumb-box">
                <img src="{pal_norm}" alt="Normal">
                <span>Normal</span>
            </div>
            <div class="thumb-box">
                <img src="{pal_mat}" alt="Material">
                <span>Material</span>
            </div>
        </div>
    </div>

    <script>
        const canvas = document.getElementById('render-canvas');
        const ctx = canvas.getContext('2d');

        let viewMode = 'pbr'; // 'pbr', 'flat', 'normal'
        let lightColorMode = 'fire'; // 'fire', 'cyan', 'holy'

        const light = {{
            x: 0,
            y: 0,
            z: 85,
            radius: 420
        }};

        // Loaded images
        const assets = {{
            paladin: {{
                diff: new Image(),
                norm: new Image(),
                mat: new Image(),
                scale: 3.0,
                x: 0,
                y: 0,
                w: 60,
                h: 69,
                loaded: false
            }},
            tree: {{
                diff: new Image(),
                norm: new Image(),
                mat: new Image(),
                scale: 1.1,
                x: 0,
                y: 0,
                w: 326,
                h: 384,
                loaded: false
            }}
        }};

        assets.paladin.diff.src = "{pal_diff}";
        assets.paladin.norm.src = "{pal_norm}";
        assets.paladin.mat.src = "{pal_mat}";

        assets.tree.diff.src = "{tree_diff}";
        assets.tree.norm.src = "{tree_norm}";
        assets.tree.mat.src = "{tree_mat}";

        let loadedCount = 0;
        function checkLoad() {{
            loadedCount++;
            if (loadedCount >= 6) {{
                assets.paladin.loaded = true;
                assets.tree.loaded = true;
                cachePixelData(assets.paladin);
                cachePixelData(assets.tree);
                render();
            }}
        }}

        assets.paladin.diff.onload = checkLoad;
        assets.paladin.norm.onload = checkLoad;
        assets.paladin.mat.onload = checkLoad;
        assets.tree.diff.onload = checkLoad;
        assets.tree.norm.onload = checkLoad;
        assets.tree.mat.onload = checkLoad;

        function cachePixelData(asset) {{
            const off = document.createElement('canvas');
            off.width = asset.w;
            off.height = asset.h;
            const octx = off.getContext('2d');

            octx.drawImage(asset.diff, 0, 0);
            asset.diffData = octx.getImageData(0, 0, asset.w, asset.h).data;

            octx.clearRect(0, 0, asset.w, asset.h);
            octx.drawImage(asset.norm, 0, 0);
            asset.normData = octx.getImageData(0, 0, asset.w, asset.h).data;

            octx.clearRect(0, 0, asset.w, asset.h);
            octx.drawImage(asset.mat, 0, 0);
            asset.matData = octx.getImageData(0, 0, asset.w, asset.h).data;

            // Output buffer
            asset.outCanvas = document.createElement('canvas');
            asset.outCanvas.width = asset.w;
            asset.outCanvas.height = asset.h;
            asset.outCtx = asset.outCanvas.getContext('2d');
            asset.outImgData = asset.outCtx.createImageData(asset.w, asset.h);
        }}

        function resize() {{
            canvas.width = window.innerWidth;
            canvas.height = window.innerHeight - 52;
            light.x = canvas.width * 0.5;
            light.y = canvas.height * 0.45;
            render();
        }}
        window.addEventListener('resize', resize);

        window.addEventListener('pointermove', (e) => {{
            const rect = canvas.getBoundingClientRect();
            light.x = e.clientX - rect.left;
            light.y = e.clientY - rect.top;
            render();
        }});

        window.addEventListener('wheel', (e) => {{
            light.z = Math.max(20, Math.min(250, light.z - e.deltaY * 0.15));
            render();
        }});

        function setMode(mode) {{
            viewMode = mode;
            document.querySelectorAll('.controls .btn').forEach(b => {{
                if (b.id.startsWith('btn-mode-')) b.classList.remove('active');
            }});
            document.getElementById('btn-mode-' + mode).classList.add('active');
            render();
        }}

        function toggleLightColor() {{
            const btn = document.getElementById('btn-light-color');
            if (lightColorMode === 'fire') {{
                lightColorMode = 'cyan';
                btn.innerText = '⚡ Frost/Lightning Light';
            }} else if (lightColorMode === 'cyan') {{
                lightColorMode = 'holy';
                btn.innerText = '✨ Holy/Radiant Light';
            }} else {{
                lightColorMode = 'fire';
                btn.innerText = '🔥 Fireball Light';
            }}
            render();
        }}

        function shadeAsset(asset, screenX, screenY) {{
            if (!asset.loaded) return;

            const w = asset.w;
            const h = asset.h;
            const diff = asset.diffData;
            const norm = asset.normData;
            const mat = asset.matData;
            const out = asset.outImgData.data;

            let lColorR = 1.0, lColorG = 0.55, lColorB = 0.2; // Fire default
            if (lightColorMode === 'cyan') {{ lColorR = 0.2; lColorG = 0.75; lColorB = 1.0; }}
            else if (lightColorMode === 'holy') {{ lColorR = 1.0; lColorG = 0.95; lColorB = 0.7; }}

            const ambient = 0.25;

            for (let y = 0; y < h; y++) {{
                for (let x = 0; x < w; x++) {{
                    const idx = (y * w + x) * 4;
                    const a = diff[idx + 3];
                    if (a === 0) {{
                        out[idx + 3] = 0;
                        continue;
                    }}

                    if (viewMode === 'flat') {{
                        out[idx + 0] = diff[idx + 0];
                        out[idx + 1] = diff[idx + 1];
                        out[idx + 2] = diff[idx + 2];
                        out[idx + 3] = a;
                        continue;
                    }}

                    if (viewMode === 'normal') {{
                        out[idx + 0] = norm[idx + 0];
                        out[idx + 1] = norm[idx + 1];
                        out[idx + 2] = norm[idx + 2];
                        out[idx + 3] = a;
                        continue;
                    }}

                    // PBR Tangent-Space Lighting
                    // Normal vector in [-1, 1]
                    const nx = (norm[idx + 0] / 255.0) * 2.0 - 1.0;
                    const ny = (norm[idx + 1] / 255.0) * 2.0 - 1.0;
                    const nz = (norm[idx + 2] / 255.0) * 2.0 - 1.0;

                    // World position of this pixel
                    const px = screenX + x * asset.scale;
                    const py = screenY + y * asset.scale;

                    // Light vector
                    const lx = light.x - px;
                    const ly = light.y - py;
                    const lz = light.z;
                    const dist = Math.hypot(lx, ly, lz);

                    // Attenuation
                    const atten = Math.max(0.0, 1.0 - dist / light.radius);
                    const attenSqr = atten * atten;

                    const invD = 1.0 / (dist + 1e-4);
                    const dirX = lx * invD;
                    const dirY = ly * invD;
                    const dirZ = lz * invD;

                    // Lambertian Diffuse (N . L)
                    const NdotL = Math.max(0.0, nx * dirX + ny * dirY + nz * dirZ);

                    // Material props: metallic & roughness
                    const metallic = mat[idx + 2] / 255.0;
                    const roughness = Math.max(0.08, mat[idx + 1] / 255.0);

                    // Blinn-Phong Specular
                    // Half vector with View = (0, 0, 1)
                    const hx = dirX;
                    const hy = dirY;
                    const hz = dirZ + 1.0;
                    const hLen = Math.hypot(hx, hy, hz) + 1e-4;
                    const NdotH = Math.max(0.0, (nx * hx + ny * hy + nz * hz) / hLen);

                    const specPower = (1.0 - roughness) * 64.0 + 4.0;
                    const spec = Math.pow(NdotH, specPower) * (0.3 + metallic * 0.9);

                    // Composite
                    const baseR = diff[idx + 0] / 255.0;
                    const baseG = diff[idx + 1] / 255.0;
                    const baseB = diff[idx + 2] / 255.0;

                    const lightTermR = ambient + NdotL * lColorR * attenSqr * 1.5;
                    const lightTermG = ambient + NdotL * lColorG * attenSqr * 1.5;
                    const lightTermB = ambient + NdotL * lColorB * attenSqr * 1.5;

                    let finalR = baseR * lightTermR + spec * lColorR * atten;
                    let finalG = baseG * lightTermG + spec * lColorG * atten;
                    let finalB = baseB * lightTermB + spec * lColorB * atten;

                    out[idx + 0] = Math.min(255, finalR * 255.0);
                    out[idx + 1] = Math.min(255, finalG * 255.0);
                    out[idx + 2] = Math.min(255, finalB * 255.0);
                    out[idx + 3] = a;
                }}
            }}

            asset.outCtx.putImageData(asset.outImgData, 0, 0);
        }}

        function drawDropShadow(asset, screenX, screenY) {{
            if (viewMode !== 'pbr') return;
            // Realistic dynamic shadow stretching away from light source
            const dx = (screenX + (asset.w * asset.scale) * 0.5) - light.x;
            const dy = (screenY + (asset.h * asset.scale) * 0.5) - light.y;
            const dist = Math.hypot(dx, dy);

            const shadowLen = Math.min(80, dist * 0.18);
            const shadowX = (dx / (dist + 1e-4)) * shadowLen;
            const shadowY = (dy / (dist + 1e-4)) * shadowLen + 15;

            ctx.save();
            ctx.filter = 'blur(8px)';
            ctx.fillStyle = 'rgba(0, 0, 0, 0.55)';
            ctx.beginPath();
            const cx = screenX + (asset.w * asset.scale) * 0.5 + shadowX;
            const cy = screenY + (asset.h * asset.scale) * 0.88 + shadowY;
            ctx.ellipse(cx, cy, (asset.w * asset.scale) * 0.35, 14, 0, 0, Math.PI * 2);
            ctx.fill();
            ctx.restore();
        }}

        function render() {{
            ctx.clearRect(0, 0, canvas.width, canvas.height);

            // Draw Ground Tiles Grid
            ctx.strokeStyle = 'rgba(255, 255, 255, 0.04)';
            ctx.lineWidth = 1;
            const tileSize = 64;
            for (let x = 0; x < canvas.width; x += tileSize) {{
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, canvas.height); ctx.stroke();
            }}
            for (let y = 0; y < canvas.height; y += tileSize) {{
                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke();
            }}

            // Positions in center
            const centerX = canvas.width * 0.5;
            const centerY = canvas.height * 0.5;

            // Ice Tree on the left
            const treeX = centerX - 320;
            const treeY = centerY - 200;

            // Paladin Hero on the right
            const palX = centerX + 80;
            const palY = centerY - 10;

            // 1. Draw Drop Shadows
            drawDropShadow(assets.tree, treeX, treeY);
            drawDropShadow(assets.paladin, palX, palY);

            // 2. Shade & Draw Tree
            shadeAsset(assets.tree, treeX, treeY);
            if (assets.tree.loaded) {{
                ctx.imageSmoothingEnabled = false;
                ctx.drawImage(assets.tree.outCanvas, treeX, treeY, assets.tree.w * assets.tree.scale, assets.tree.h * assets.tree.scale);
            }}

            // 3. Shade & Draw Paladin
            shadeAsset(assets.paladin, palX, palY);
            if (assets.paladin.loaded) {{
                ctx.imageSmoothingEnabled = false;
                ctx.drawImage(assets.paladin.outCanvas, palX, palY, assets.paladin.w * assets.paladin.scale, assets.paladin.h * assets.paladin.scale);
            }}

            // 4. Draw Light Source Cursor
            ctx.save();
            let glowColor = 'rgba(255, 160, 40, ';
            if (lightColorMode === 'cyan') glowColor = 'rgba(40, 200, 255, ';
            else if (lightColorMode === 'holy') glowColor = 'rgba(255, 240, 160, ';

            const grad = ctx.createRadialGradient(light.x, light.y, 4, light.x, light.y, 40);
            grad.addColorStop(0, '#fff');
            grad.addColorStop(0.3, glowColor + '0.8)');
            grad.addColorStop(1, glowColor + '0.0)');

            ctx.fillStyle = grad;
            ctx.beginPath();
            ctx.arc(light.x, light.y, 40, 0, Math.PI * 2);
            ctx.fill();

            // Light height indicator
            ctx.fillStyle = '#fff';
            ctx.font = '10px sans-serif';
            ctx.fillText('Z: ' + Math.round(light.z), light.x + 14, light.y - 12);
            ctx.restore();
        }}

        // Init
        resize();
    </script>
</body>
</html>
"""

with open('C:/GM3Dext_Bridge/preview_3d_moba.html', 'w', encoding='utf-8') as f:
    f.write(html_content)

print("Generated C:/GM3Dext_Bridge/preview_3d_moba.html successfully!")
