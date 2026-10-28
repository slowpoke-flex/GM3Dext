import urllib.request
import os

url = "https://raw.githubusercontent.com/mrdoob/three.js/r128/examples/js/loaders/GLTFLoader.js"
out_path = r"C:\GM3Dext_Bridge\GLTFLoader.js"

try:
    print(f"Downloading GLTFLoader.js from {url}...")
    urllib.request.urlretrieve(url, out_path)
    print(f"Success! File size: {os.path.getsize(out_path)} bytes")
except Exception as e:
    print(f"Error downloading: {e}")
