import os
import uuid
import shutil
from PIL import Image
import numpy as np

PROJECT_DIR = r"C:\GameDev\projects\moba-gm"
OUT_SKELETON_DIR = r"C:\GM3Dext_Bridge\out_skeleton"
YYP_PATH = os.path.join(PROJECT_DIR, "Quality MOBA graybox.yyp")

def invert_normal_green(input_path, output_path):
    im = Image.open(input_path).convert('RGBA')
    arr = np.array(im)
    # Invert G channel: 255 - G
    arr[:, :, 1] = 255 - arr[:, :, 1]
    res = Image.fromarray(arr, 'RGBA')
    res.save(output_path)

def create_gm_multi_frame_sprite(sprite_name, frame_files, width=256, height=256, xorigin=128, yorigin=240):
    sprite_dir = os.path.join(PROJECT_DIR, "sprites", sprite_name)
    layers_dir = os.path.join(sprite_dir, "layers")
    os.makedirs(sprite_dir, exist_ok=True)
    os.makedirs(layers_dir, exist_ok=True)

    layer_id = str(uuid.uuid4())
    frames_json = []
    keyframes_json = []

    for i, src_file in enumerate(frame_files):
        frame_id = str(uuid.uuid4())
        keyframe_id = str(uuid.uuid4())

        # Frame layer folder
        frame_layer_dir = os.path.join(layers_dir, frame_id)
        os.makedirs(frame_layer_dir, exist_ok=True)

        # Copy image to frame and layer paths
        frame_png_path = os.path.join(sprite_dir, f"{frame_id}.png")
        layer_png_path = os.path.join(frame_layer_dir, f"{layer_id}.png")

        shutil.copyfile(src_file, frame_png_path)
        shutil.copyfile(src_file, layer_png_path)

        frames_json.append(
            f'{{"$GMSpriteFrame":"v1","%Name":"{frame_id}","name":"{frame_id}","resourceType":"GMSpriteFrame","resourceVersion":"2.0",}}'
        )

        keyframes_json.append(f'''            {{"$Keyframe<SpriteFrameKeyframe>":"","Channels":{{
                "0":{{"$SpriteFrameKeyframe":"","Id":{{"name":"{frame_id}","path":"sprites/{sprite_name}/{sprite_name}.yy",}},"resourceType":"SpriteFrameKeyframe","resourceVersion":"2.0",}},
              }},"Disabled":false,"id":"{keyframe_id}","IsCreationKey":false,"Key":{float(i)},"Length":1.0,"resourceType":"Keyframe<SpriteFrameKeyframe>","resourceVersion":"2.0","Stretch":false,}}''')

    frames_str = ",\n    ".join(frames_json) + ","
    keyframes_str = ",\n".join(keyframes_json) + ",\n"

    yy_content = f'''{{
  "$GMSprite":"v2",
  "%Name":"{sprite_name}",
  "bboxMode":0,
  "bbox_bottom":{height - 1},
  "bbox_left":0,
  "bbox_right":{width - 1},
  "bbox_top":0,
  "collisionKind":1,
  "collisionTolerance":0,
  "DynamicTexturePage":false,
  "edgeFiltering":false,
  "For3D":false,
  "frames":[
    {frames_str}
  ],
  "gridX":0,
  "gridY":0,
  "height":{height},
  "HTile":false,
  "layers":[
    {{"$GMImageLayer":"","%Name":"{layer_id}","blendMode":0,"displayName":"default","isLocked":false,"name":"{layer_id}","opacity":100.0,"resourceType":"GMImageLayer","resourceVersion":"2.0","visible":true,}},
  ],
  "name":"{sprite_name}",
  "nineSlice":null,
  "origin":9,
  "parent":{{
    "name":"ART",
    "path":"folders/ART.yy",
  }},
  "preMultiplyAlpha":false,
  "resourceType":"GMSprite",
  "resourceVersion":"2.0",
  "sequence":{{
    "$GMSequence":"v1",
    "%Name":"{sprite_name}",
    "autoRecord":true,
    "backdropHeight":768,
    "backdropImageOpacity":0.5,
    "backdropImagePath":"",
    "backdropWidth":1366,
    "backdropXOffset":0.0,
    "backdropYOffset":0.0,
    "events":{{
      "$KeyframeStore<MessageEventKeyframe>":"",
      "Keyframes":[],
      "resourceType":"KeyframeStore<MessageEventKeyframe>",
      "resourceVersion":"2.0",
    }},
    "eventStubScript":null,
    "eventToFunction":{{}},
    "length":{float(len(frame_files))},
    "lockOrigin":false,
    "moments":{{
      "$KeyframeStore<MomentsEventKeyframe>":"",
      "Keyframes":[],
      "resourceType":"KeyframeStore<MomentsEventKeyframe>",
      "resourceVersion":"2.0",
    }},
    "name":"{sprite_name}",
    "playback":1,
    "playbackSpeed":0.0,
    "playbackSpeedType":0,
    "resourceType":"GMSequence",
    "resourceVersion":"2.0",
    "showBackdrop":true,
    "showBackdropImage":false,
    "timeUnits":1,
    "tracks":[
      {{"$GMSpriteFramesTrack":"","builtinName":0,"events":[],"inheritsTrackColour":true,"interpolation":1,"isCreationTrack":false,"keyframes":{{"$KeyframeStore<SpriteFrameKeyframe>":"","Keyframes":[
{keyframes_str}          ],"resourceType":"KeyframeStore<SpriteFrameKeyframe>","resourceVersion":"2.0",}},"modifiers":[],"name":"frames","resourceType":"GMSpriteFramesTrack","resourceVersion":"2.0","spriteId":null,"trackColour":0,"tracks":[],"traits":0,}},
    ],
    "visibleRange":null,
    "volume":1.0,
    "xorigin":{xorigin},
    "yorigin":{yorigin},
  }},
  "swatchColours":null,
  "swfPrecision":0.5,
  "textureGroupId":{{
    "name":"Default",
    "path":"texturegroups/Default",
  }},
  "type":0,
  "VTile":false,
  "width":{width},
}}'''

    yy_path = os.path.join(sprite_dir, f"{sprite_name}.yy")
    with open(yy_path, "w", encoding="utf-8") as f:
        f.write(yy_content)
    print(f"Created sprite: {sprite_name} with {len(frame_files)} frames.")

def update_yyp():
    with open(YYP_PATH, 'r', encoding='utf-8') as f:
        content = f.read()

    new_resources = [
        '    {"id":{"name":"spr_skeleton_warrior","path":"sprites/spr_skeleton_warrior/spr_skeleton_warrior.yy",},},',
        '    {"id":{"name":"spr_skeleton_normal","path":"sprites/spr_skeleton_normal/spr_skeleton_normal.yy",},},',
    ]

    for res in new_resources:
        name = res.split('"name":"')[1].split('"')[0]
        if f'"{name}"' not in content:
            pos = content.find('"resources":[') + len('"resources":[')
            content = content[:pos] + '\n' + res + content[pos:]
            print(f"Added to YYP: {name}")

    with open(YYP_PATH, 'w', encoding='utf-8') as f:
        f.write(content)

def main():
    diff_files = []
    norm_files = []

    # Process all 8 directions
    for i in range(8):
        diff_src = os.path.join(OUT_SKELETON_DIR, f"skeleton_diff_{i}.png")
        norm_src = os.path.join(OUT_SKELETON_DIR, f"skeleton_norm_{i}.png")
        norm_dx = os.path.join(OUT_SKELETON_DIR, f"skeleton_norm_dx_{i}.png")

        invert_normal_green(norm_src, norm_dx)

        diff_files.append(diff_src)
        norm_files.append(norm_dx)

    create_gm_multi_frame_sprite("spr_skeleton_warrior", diff_files, 256, 256, 128, 240)
    create_gm_multi_frame_sprite("spr_skeleton_normal", norm_files, 256, 256, 128, 240)

    update_yyp()
    print("All Skeleton Warrior assets successfully installed into moba-gm!")

if __name__ == '__main__':
    main()
