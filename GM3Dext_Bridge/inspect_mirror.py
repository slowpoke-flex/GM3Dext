import sqlite3
import os

db_path = r'C:\Users\Revo\AppData\Local\Google\DriveFS\106599916612719274228\mirror_sqlite.db'
if os.path.exists(db_path):
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    c.execute("SELECT name FROM sqlite_master WHERE type='table'")
    print('Mirror tables:', c.fetchall())
    for t in c.fetchall():
        try:
            c.execute(f"SELECT * FROM {t[0]} WHERE 1")
            print(f"Table {t[0]}:", c.fetchall()[:5])
        except:
            pass
