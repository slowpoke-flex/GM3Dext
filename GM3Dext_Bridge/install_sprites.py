import os
import uuid
import shutil

PROJECT_DIR = r"C:\GameDev\projects\moba-gm"
BRIDGE_GEN_DIR = r"C:\GM3Dext_Bridge\generated"

def create_gm_sprite(sprite_name, source_png, width, height, xorigin, yorigin):
    sprite_dir = os.path.join(PROJECT_DIR, "sprites", sprite_name)
    layers_dir = os.path.join(sprite_dir, "layers")
    
    frame_id = str(uuid.uuid4())
    layer_id = str(uuid.uuid4())
    keyframe_id = str(uuid.uuid4())
    
    frame_layer_dir = os.path.join(layers_dir, frame_id)
    os.makedirs(frame_layer_dir, exist_ok=True)
    
    # Copy PNG to frame file and layer file
    frame_png_path = os.path.join(sprite_dir, f"{frame_id}.png")
    layer_png_path = os.path.join(frame_layer_dir, f"{layer_id}.png")
    
    shutil.copyfile(source_png, frame_png_path)
    shutil.copyfile(source_png, layer_png_path)
    
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
    {{"$GMSpriteFrame":"v1","%Name":"{frame_id}","name":"{frame_id}","resourceType":"GMSpriteFrame","resourceVersion":"2.0",}},
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
    "length":1.0,
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
            {{"$Keyframe<SpriteFrameKeyframe>":"","Channels":{{
                "0":{{"$SpriteFrameKeyframe":"","Id":{{"name":"{frame_id}","path":"sprites/{sprite_name}/{sprite_name}.yy",}},"resourceType":"SpriteFrameKeyframe","resourceVersion":"2.0",}},
              }},"Disabled":false,"id":"{keyframe_id}","IsCreationKey":false,"Key":0.0,"Length":1.0,"resourceType":"Keyframe<SpriteFrameKeyframe>","resourceVersion":"2.0","Stretch":false,}},
          ],"resourceType":"KeyframeStore<SpriteFrameKeyframe>","resourceVersion":"2.0",}},"modifiers":[],"name":"frames","resourceType":"GMSpriteFramesTrack","resourceVersion":"2.0","spriteId":null,"trackColour":0,"tracks":[],"traits":0,}},
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
}}
'''
    with open(os.path.join(sprite_dir, f"{sprite_name}.yy"), 'w', encoding='utf-8') as f:
        f.write(yy_content)
    print(f"Created GameMaker sprite: {sprite_name}")

# Create spr_paladin_normal (60x69, origin 30, 61)
create_gm_sprite("spr_paladin_normal", os.path.join(BRIDGE_GEN_DIR, "paladin_idle_0_normal.png"), 60, 69, 30, 61)

# Create spr_tree_normal (326x384, origin 164, 336)
create_gm_sprite("spr_tree_normal", os.path.join(BRIDGE_GEN_DIR, "tree_ice_normal.png"), 326, 384, 164, 336)

print("Sprite creation complete!")
