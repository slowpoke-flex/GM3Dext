import sqlite3
import shutil
import os

source_dir = r'C:\Users\Revo\AppData\Local\Google\DriveFS\106599916612719274228'
target_dir = r'C:\GM3Dext_Bridge\drive_db_copy'
os.makedirs(target_dir, exist_ok=True)

for fname in os.listdir(source_dir):
    if fname.startswith('metadata_sqlite_db'):
        shutil.copyfile(os.path.join(source_dir, fname), os.path.join(target_dir, fname))

conn = sqlite3.connect(os.path.join(target_dir, 'metadata_sqlite_db'))
c = conn.cursor()

# Search across all tables and text columns
for row in c.execute("SELECT id, mime_type, local_title FROM items WHERE local_title LIKE '%NON_VERTEX%' OR local_title LIKE '%CONCEPT%'"):
    print("Found item:", row)

# Let's also search in properties or other tables
for table in ['properties', 'item_properties', 'activity_items', 'shortcut_details']:
    try:
        c.execute(f"PRAGMA table_info({table})")
        cols = [r[1] for r in c.fetchall()]
        c.execute(f"SELECT * FROM {table}")
        for r in c.fetchall():
            for val in r:
                if isinstance(val, str) and ('NON_VERTEX' in val or 'CONCEPT' in val):
                    print(f"Found in {table}:", r)
    except Exception as e:
        pass
