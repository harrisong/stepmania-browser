#!/usr/bin/env python3
"""
StepMania Browser - Local Companion Server
Run this locally. The GitHub Pages game page calls this to import YouTube songs.
"""
import sys, json, subprocess, tempfile, base64
from pathlib import Path
from http.server import HTTPServer, BaseHTTPRequestHandler

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent.parent / "stepmania-songs-importer"))
try:
    from importer import StepManiaImporter
except ImportError:
    # Also try sibling directory of the stepmania-browser repo
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "stepmania-songs-importer"))
    from importer import StepManiaImporter


def import_youtube(url: str, difficulty: str = "Medium") -> dict:
    with tempfile.TemporaryDirectory() as tmp:
        importer = StepManiaImporter(output_dir=tmp, difficulty=difficulty)
        mp3_path = importer.download_from_youtube(url)
        if not mp3_path:
            return {"error": "Download failed"}

        title, artist, _, bpm = importer.extract_metadata(mp3_path)
        duration = importer.get_song_length(mp3_path)

        # Convert to OGG (browser port only supports OGG/Vorbis)
        ogg_path = Path(tmp) / "song.ogg"
        r = subprocess.run(
            ["ffmpeg", "-y", "-i", mp3_path, "-c:a", "libvorbis", "-q:a", "6", str(ogg_path)],
            capture_output=True
        )
        if r.returncode != 0:
            return {"error": "FFmpeg conversion failed"}

        # Find and convert thumbnail to PNG for background/banner
        bg_b64 = None
        mp3_stem = Path(mp3_path).stem
        for ext in ['webp', 'jpg', 'jpeg', 'png']:
            thumb_path = Path(mp3_path).with_suffix(f'.{ext}')
            if thumb_path.exists():
                png_path = Path(tmp) / "bg.png"
                r2 = subprocess.run(
                    ["ffmpeg", "-y", "-i", str(thumb_path), "-vf", "scale=640:-1", str(png_path)],
                    capture_output=True
                )
                if r2.returncode == 0 and png_path.exists():
                    bg_b64 = base64.b64encode(png_path.read_bytes()).decode()
                break

        # Generate .sm content with background reference
        sm_content = importer.generate_sm_file(title, artist, "song.ogg", bpm, duration)
        if bg_b64:
            sm_content = sm_content.replace("#BACKGROUND:;", "#BACKGROUND:bg.png;")
            sm_content = sm_content.replace("#BANNER:;", "#BANNER:bg.png;")

        # Return base64-encoded OGG + sm text + optional background
        ogg_b64 = base64.b64encode(ogg_path.read_bytes()).decode()
        result = {
            "success": True,
            "title": title,
            "artist": artist,
            "bpm": round(bpm, 2),
            "duration": round(duration, 1),
            "difficulty": difficulty,
            "sm": sm_content,
            "ogg_b64": ogg_b64,
        }
        if bg_b64:
            result["bg_b64"] = bg_b64
        return result


class Handler(BaseHTTPRequestHandler):
    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def do_OPTIONS(self):
        self.send_response(200)
        self._cors()
        self.end_headers()

    def do_POST(self):
        if self.path != "/api/import":
            self.send_response(404)
            self._cors()
            self.end_headers()
            return

        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        data = json.loads(body)
        url = data.get("url", "")
        difficulty = data.get("difficulty", "Medium")

        print(f"Importing: {url} (difficulty={difficulty})")
        result = import_youtube(url, difficulty)

        self.send_response(200 if result.get("success") else 500)
        self.send_header("Content-Type", "application/json")
        self._cors()
        self.end_headers()
        self.wfile.write(json.dumps(result).encode())

    def do_GET(self):
        if self.path == "/health":
            self.send_response(200)
            self._cors()
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"status":"ok"}')
        else:
            self.send_response(404)
            self._cors()
            self.end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    server = HTTPServer(("127.0.0.1", port), Handler)
    print(f"StepMania Companion Server running at http://localhost:{port}")
    print(f"  POST /api/import  {{\"url\": \"...\", \"difficulty\": \"Medium\"}}")
    print(f"  GET  /health")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
