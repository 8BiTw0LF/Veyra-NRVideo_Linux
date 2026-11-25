# Mock Jellyfin and Plex servers for the media client (MediaLibrary.cpp): the endpoints VeyAmux uses, a token check,
# and a video file with HTTP ranges. Run it, then VeyAmux with VEYAMUX_TEST_MEDIA=jellyfin:http://127.0.0.1:18096 (or plex:...).
#   python3 tests/mock_media_server.py <video.mp4> <poster.png>
import json, os, sys, re
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
VIDEO = sys.argv[1]; POSTER = sys.argv[2]
TOKEN = "jf-secret-token"; PLEX = "plex-secret-token"
log = sys.stderr
def item(i, name, typ, **kw):
    d = {"Id": i, "Name": name, "Type": typ, "ImageTags": {"Primary": "tag" + i}, "UserData": {"PlaybackPositionTicks": 0, "Played": False}}
    d.update(kw); return d
JF = {
 "views": [item("lib1", "Film", "CollectionFolder", CollectionType="movies"), item("lib2", "Serier", "CollectionFolder", CollectionType="tvshows")],
 "lib1": [item("m1", "Big Test Movie", "Movie", ProductionYear=2024, RunTimeTicks=30000000, Overview="A test film.",
               UserData={"PlaybackPositionTicks": 12000000, "Played": False, "PlayedPercentage": 40})],
 "lib2": [item("s1", "Test Series", "Series", ProductionYear=2020)],
 "seasons_s1": [item("se1", "Season 1", "Season")],
 "se1": [item("e1", "Pilot", "Episode", ParentIndexNumber=1, IndexNumber=1, SeriesName="Test Series", RunTimeTicks=30000000)],
}
class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def send(self, code, body=b"", ctype="application/json"):
        if isinstance(body, (dict, list)): body = json.dumps(body).encode()
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body))); self.end_headers()
        self.wfile.write(body)
    def body(self):
        n = int(self.headers.get("Content-Length") or 0); return self.rfile.read(n) if n else b""
    def authed(self):
        a = self.headers.get("Authorization", "") + self.headers.get("X-Plex-Token", "")
        return TOKEN in a or PLEX in a
    def file(self, path, ctype):
        data = open(path, "rb").read(); rng = self.headers.get("Range")
        if rng:
            m = re.match(r"bytes=(\d+)-(\d*)", rng); a = int(m.group(1)); b = int(m.group(2)) if m.group(2) else len(data) - 1
            chunk = data[a:b + 1]
            self.send_response(206); self.send_header("Content-Range", f"bytes {a}-{b}/{len(data)}")
        else:
            chunk = data; self.send_response(200)
        self.send_header("Content-Type", ctype); self.send_header("Accept-Ranges", "bytes"); self.send_header("Content-Length", str(len(chunk))); self.end_headers()
        self.wfile.write(chunk)
    def handle_req(self, method):
        u = urlparse(self.path); q = parse_qs(u.query); p = u.path
        b = self.body() if method == "POST" else b""
        log.write(f"{method} {p} auth={self.authed()} {b[:200]!r}\n"); log.flush()
        # ---- Jellyfin
        if p == "/System/Info/Public": return self.send(200, {"ServerName": "MockFin", "Version": "10.10.0"})
        if p == "/Users/AuthenticateByName":
            d = json.loads(b); 
            if d.get("Pw") != "pw": return self.send(401)
            return self.send(200, {"AccessToken": TOKEN, "User": {"Id": "user1"}})
        if p == "/QuickConnect/Initiate": return self.send(200, {"Secret": "sec", "Code": "123456"})
        if p == "/QuickConnect/Connect": return self.send(200, {"Authenticated": True})
        if p == "/Users/AuthenticateWithQuickConnect": return self.send(200, {"AccessToken": TOKEN, "User": {"Id": "user1"}})
        if p.startswith("/plex"): pass
        elif not self.authed(): return self.send(401)
        if p == "/Genres": return self.send(200, {"Items": [{"Name": "Action", "Id": "g1"}, {"Name": "Drama", "Id": "g2"}]})
        if p == "/UserViews": return self.send(200, {"Items": JF["views"]})
        if p == "/UserItems/Resume": return self.send(200, {"Items": JF["lib1"]})
        if p == "/Items": return self.send(200, {"Items": JF.get(q["ParentId"][0], [])})
        m = re.match(r"/Shows/(\w+)/Seasons", p)
        if m: return self.send(200, {"Items": JF["seasons_" + m.group(1)]})
        if re.match(r"/Items/\w+/Images/Primary", p): return self.file(POSTER, "image/png")
        if re.match(r"/Videos/\w+/stream", p): return self.file(VIDEO, "video/mp4")
        if p.startswith("/Sessions/Playing") or p.startswith("/UserPlayedItems"): return self.send(204)
        # ---- Plex
        if p == "/identity": return self.send(200, {"MediaContainer": {"machineIdentifier": "mock"}})
        if p == "/library/sections":
            return self.send(200, {"MediaContainer": {"Directory": [{"key": "1", "title": "Movies", "type": "movie", "agent": "tv.plex.agents.movie"},
                                                                    {"key": "3", "title": "Music", "type": "artist", "agent": "x"}]}})
        if p == "/library/sections/1/genre":
            return self.send(200, {"MediaContainer": {"Directory": [{"key": "5", "title": "Action", "fastKey": "/library/sections/1/all?genre=5"}]}})
        if p in ("/library/sections/1/all", "/library/sections/1/recentlyAdded", "/library/sections/1/unwatched"):
            return self.send(200, {"MediaContainer": {"Metadata": [{"ratingKey": "77", "key": "/library/metadata/77", "type": "movie", "title": "Plex Movie",
                "year": 2023, "duration": 3000, "viewOffset": 1500, "thumb": "/library/metadata/77/thumb/1", "Media": [{"Part": [{"key": "/library/parts/9/file.mp4"}]}]}]}})
        if p == "/library/onDeck": return self.send(200, {"MediaContainer": {"Metadata": []}})
        if p == "/photo/:/transcode": return self.file(POSTER, "image/png")
        if p == "/library/parts/9/file.mp4": return self.file(VIDEO, "video/mp4")
        if p in ("/:/timeline", "/:/scrobble"): return self.send(200)
        return self.send(404)
    def do_GET(self): self.handle_req("GET")
    def do_POST(self): self.handle_req("POST")
ThreadingHTTPServer(("127.0.0.1", 18096), H).serve_forever()
