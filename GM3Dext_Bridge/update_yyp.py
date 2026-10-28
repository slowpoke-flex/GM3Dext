import re

YYP_PATH = r"C:\GameDev\projects\moba-gm\Quality MOBA graybox.yyp"

with open(YYP_PATH, 'r', encoding='utf-8') as f:
    content = f.read()

# Resources to insert
new_resources = [
    '    {"id":{"name":"obj_test_3d","path":"objects/obj_test_3d/obj_test_3d.yy",},},',
    '    {"id":{"name":"rm_test_3d","path":"rooms/rm_test_3d/rm_test_3d.yy",},},',
    '    {"id":{"name":"scr_pbr","path":"scripts/scr_pbr/scr_pbr.yy",},},',
    '    {"id":{"name":"sh_moba_pbr","path":"shaders/sh_moba_pbr/sh_moba_pbr.yy",},},',
    '    {"id":{"name":"spr_paladin_normal","path":"sprites/spr_paladin_normal/spr_paladin_normal.yy",},},',
    '    {"id":{"name":"spr_tree_normal","path":"sprites/spr_tree_normal/spr_tree_normal.yy",},},',
]

# Check if already present
for res in new_resources:
    name = res.split('"name":"')[1].split('"')[0]
    if f'"{name}"' not in content:
        # Insert before the closing bracket of "resources": [
        pos = content.find('"resources":[') + len('"resources":[')
        content = content[:pos] + '\n' + res + content[pos:]
        print(f"Added resource: {name}")

# Add rm_test_3d to RoomOrderNodes
if '{"roomId":{"name":"rm_test_3d"' not in content:
    needle = '"RoomOrderNodes":['
    pos = content.find(needle) + len(needle)
    rm_entry = '\n    {"roomId":{"name":"rm_test_3d","path":"rooms/rm_test_3d/rm_test_3d.yy",},},'
    content = content[:pos] + rm_entry + content[pos:]
    print("Added rm_test_3d to RoomOrderNodes")

with open(YYP_PATH, 'w', encoding='utf-8') as f:
    f.write(content)

print("Updated Quality MOBA graybox.yyp successfully!")
