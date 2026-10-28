import http.server
import socketserver
import json
import base64
import os
import sys
import subprocess
import threading
import time

PORT = 8099
DIRECTORY = r"C:\GM3Dext_Bridge"
OUT_DIR = os.path.join(DIRECTORY, "out_skeleton")
os.makedirs(OUT_DIR, exist_ok=True)

done_event = threading.Event()

class BakeHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def do_POST(self):
        if self.path == '/save_frame':
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            payload = json.loads(post_data.decode('utf-8'))
            
            filename = payload['filename']
            data_url = payload['data']
            header, encoded = data_url.split(',', 1)
            file_bytes = base64.b64decode(encoded)
            
            out_file = os.path.join(OUT_DIR, filename)
            with open(out_file, 'wb') as f:
                f.write(file_bytes)
            print(f"Saved: {filename} ({len(file_bytes)} bytes)")
            
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(b'{"status":"ok"}')
            
        elif self.path == '/bake_done':
            print("Received bake_done signal!")
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'{"status":"done"}')
            done_event.set()
        else:
            self.send_error(404)

def run_server(httpd):
    print(f"Serving HTTP on port {PORT}...")
    httpd.serve_forever()

def main():
    httpd = socketserver.TCPServer(("", PORT), BakeHandler)
    server_thread = threading.Thread(target=run_server, args=(httpd,), daemon=True)
    server_thread.start()

    chrome_path = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
    url = f"http://localhost:{PORT}/bake_skeleton.html"
    print(f"Launching Chrome headless to {url}...")
    
    cmd = [
        chrome_path,
        "--headless=new",
        "--use-gl=angle",
        "--use-angle=swiftshader",
        "--enable-webgl",
        "--no-sandbox",
        "--disable-gpu-watchdog",
        url
    ]
    
    proc = subprocess.Popen(cmd)
    
    # Wait for completion or timeout
    finished = done_event.wait(timeout=25.0)
    if finished:
        print("Baking completed successfully!")
    else:
        print("Timed out waiting for bake!")
    
    proc.terminate()
    try:
        proc.wait(timeout=2.0)
    except:
        proc.kill()
        
    httpd.shutdown()
    print("Server stopped.")

if __name__ == '__main__':
    main()
