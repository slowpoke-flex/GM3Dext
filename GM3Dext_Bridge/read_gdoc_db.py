import sqlite3

conn = sqlite3.connect(r'C:\GM3Dext_Bridge\temp_metadata.db')
c = conn.cursor()
c.execute("SELECT id, mime_type, local_title FROM items")
for row in c.fetchall():
    title = str(row[2])
    if 'NON_VERTEX' in title or 'CONCEPT' in title or 'VERTEX' in title:
        print("FOUND:", row)
