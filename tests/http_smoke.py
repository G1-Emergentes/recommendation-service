"""Run isolated local HTTP checks; always stop the child service."""
import contextlib
import json
import http.server
import threading
import uuid
from datetime import datetime, timezone
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def request(base, path, body=None, raw=None, content_type="application/json", token=None, headers=None, timeout=3):
    data = raw if raw is not None else (
        json.dumps(body).encode() if body is not None else None
    )
    req = urllib.request.Request(base + path, data=data)
    if data is not None:
        req.add_header("Content-Type", content_type)
    if token is not None:
        req.add_header("Authorization", "Bearer " + token)
    for name, value in (headers or {}).items():
        req.add_header(name, value)
    try:
        response = urllib.request.urlopen(req, timeout=timeout)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        return response.status, json.load(response)


def check_documentation(base):
    status, spec = request(base, "/openapi.json")
    check(status == 200 and spec["openapi"] == "3.0.3", "OpenAPI contract unavailable")
    check(spec["servers"][0]["url"] == "/", "Swagger must use the current origin")
    check(set(spec["paths"]) == {"/health", "/api/v1/recommendations",
          "/api/v1/recommendations/{id}", "/api/v1/recommendations/{id}/favorite",
          "/api/v1/recommendations/{id}/reject", "/api/v1/conversations",
          "/api/v1/conversations/{id}", "/api/v1/conversations/{id}/messages",
          "/api/v1/wardrobe-index/sync", "/api/v1/wardrobe-index/search", "/api/v1/style-knowledge/search", "/api/v1/outfit-examples/search"},
          "Contract must only advertise implemented API endpoints")
    for path, marker in (("/docs", b"Mirage Recommendation API"),
                         ("/docs/", b"swagger-ui"),
                         ("/swagger", b"swagger-ui"),
                         ("/swagger/", b"swagger-ui"),
                         ("/docs/swagger-ui.css", b".swagger-ui"),
                         ("/docs/swagger-ui-bundle.js", b"SwaggerUIBundle"),
                         ("/docs/swagger-initializer.js", b"/openapi.json")):
        with urllib.request.urlopen(base + path, timeout=3) as response:
            check(response.status == 200 and marker in response.read(),
                  f"Missing documentation asset: {path}")
            check(response.headers.get("X-Content-Type-Options") == "nosniff",
                  "Documentation must send security headers")
            check(response.headers.get_content_type() != "application/octet-stream",
                  f"Incorrect MIME type for documentation asset: {path}")
    for path in ("/.env", "/main.cpp", "/docs/../.env"):
        try:
            with urllib.request.urlopen(base + path, timeout=3):
                raise RuntimeError(f"Unexpected static file exposure: {path}")
        except urllib.error.HTTPError as error:
            check(error.code == 404, f"Unexpected status for private path: {path}")


@contextlib.contextmanager
def server(executable, mode, iam_url="http://127.0.0.1:8080", timeout_seconds=1, storage_url="", engine="category_rules", nvidia_url="", closet_url="", vector_url="", embedding_url="", fashion_web_url="") :
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    env = dict(os.environ, RECOMMENDATION_MODE=mode, RECOMMENDATION_PORT=str(port),
               IAM_BASE_URL=iam_url, IAM_TIMEOUT_SECONDS=str(timeout_seconds),
               DIGITAL_CLOSET_BASE_URL=closet_url or "http://127.0.0.1:8081", DIGITAL_CLOSET_TIMEOUT_SECONDS="1",
               RECOMMENDATION_SUPABASE_URL=storage_url,
               RECOMMENDATION_SUPABASE_SECRET_KEY="sb_secret_test_backend" if storage_url else "",
               IAM_SUPABASE_URL="https://auth-test.supabase.co", RECOMMENDATION_ENGINE=engine,
               NVIDIA_API_KEY="nvapi-test" if nvidia_url else "", NVIDIA_TIMEOUT_SECONDS="1",
               NVIDIA_MODEL="nvidia/nemotron-3.5-lightning-30b-a3b",
               NVIDIA_API_ORIGIN=nvidia_url or "https://integrate.api.nvidia.com",
               RECOMMENDATION_RAG_ENABLED="true" if vector_url else "false", NEON_VECTOR_DATABASE_URL=vector_url,
               NVIDIA_EMBEDDING_API_KEY="embedding-test" if embedding_url else "",
               NVIDIA_EMBEDDING_MODEL="nvidia/nemotron-3.5-embed-1b",NVIDIA_EMBEDDING_DIMENSIONS="1024",
               NVIDIA_EMBEDDING_TIMEOUT_SECONDS="1",NVIDIA_EMBEDDING_API_ORIGIN=embedding_url or "https://integrate.api.nvidia.com")
    env['FASHION_WEB_ORIGIN']=fashion_web_url or 'http://127.0.0.1:1'
    env['FASHION_WEB_TIMEOUT_SECONDS']='1'
    process = subprocess.Popen([executable], env=env, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    base = f"http://127.0.0.1:{port}"
    try:
        deadline = time.monotonic() + 10
        while True:
            if process.poll() is not None:
                raise RuntimeError(f"Service exited before startup: {process.returncode}")
            try:
                status, health = request(base, "/health")
                check(status == 200 and health["mode"] == mode, "Unexpected health response")
                break
            except (OSError, urllib.error.URLError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)
        yield base
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


@contextlib.contextmanager
def iam_server():
    state = {"revoked": False, "calls": [], "preferences": {}, "writes": [], "write_status": 200}

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_PUT(self):
            auth=self.headers.get('Authorization','');token=auth.removeprefix('Bearer ')
            owner='20000000-0000-4000-8000-000000000002' if token=='valid-b' else '20000000-0000-4000-8000-000000000001'
            check(self.path==f'/api/v1/profiles/{owner}/preferences','Write only authenticated owner preferences')
            body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            state['writes'].append((self.path,auth,body))
            status=state['write_status']
            if status==200:state['preferences'][token]=body
            data=json.dumps(body if status==200 else {'error':'private-provider-details'}).encode()
            self.send_response(status);self.send_header('Content-Type','application/json')
            self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)

        def do_GET(self):
            auth = self.headers.get("Authorization", "")
            state["calls"].append((self.path, auth))
            token = auth.removeprefix("Bearer ")
            if self.path.startswith("/api/v1/profiles/"):
                expected = "20000000-0000-4000-8000-000000000002" if token == "valid-b" else "20000000-0000-4000-8000-000000000001"
                if self.path != f"/api/v1/profiles/{expected}/preferences":
                    self.send_error(403)
                    return
                status = 200
                payload = state["preferences"].get(token, {
                    "preferredStyles": [], "preferredColors": [],
                    "avoidedColors": [], "preferredCategories": []})
                if token == "profile-missing":
                    status, payload = 404, {"error": "profile_not_found"}
                elif token == "profile-denied":
                    status, payload = 403, {"error": "private-provider-detail"}
                elif token == "profile-unavailable":
                    status, payload = 503, {"error": "private-provider-detail"}
                elif token == "profile-rate-limited":
                    status = 429
                elif token == "profile-redirect":
                    status = 302
                elif token == "profile-slow":
                    time.sleep(1.5)
                elif token == "profile-invalid":
                    payload = {"preferredStyles": "wrong-type"}
                data = b"invalid-json" if token == "profile-bad-json" else json.dumps(payload).encode()
                self.send_response(status)
                if status == 302:
                    self.send_header("Location", "/profile-redirect-target")
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                try:
                    self.wfile.write(data)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass
                return
            if self.path != "/api/v1/auth/me":
                self.send_error(404)
                return
            token = auth.removeprefix("Bearer ")
            status = 200
            payload = {"id": "20000000-0000-4000-8000-000000000001",
                       "email": "private@example.test", "user_metadata": {"user_id": "forged"}}
            if token == "valid-b":
                payload["id"] = "20000000-0000-4000-8000-000000000002"
            elif token in ("expired", "invalid", "revoked") or (token == "revocable" and state["revoked"]):
                status, payload = 401, {"error": "private-provider-detail"}
            elif token == "rejected-400":
                status = 400
            elif token == "rejected-403":
                status = 403
            elif token == "unavailable":
                status = 503
            elif token == "rate-limited":
                status = 429
            elif token == "missing-id":
                payload = {"email": "private@example.test"}
            elif token == "bad-id":
                payload["id"] = "not-a-uuid"
            elif token == "wrapped":
                payload = {"user": payload}
            elif token == "redirect":
                status = 302
            elif token == "slow":
                time.sleep(1.5)
            elif token not in ("valid-a", "revocable", "bad-json") and not token.startswith("profile-"):
                status = 401
            data = b"not-json" if token == "bad-json" else json.dumps(payload).encode()
            self.send_response(status)
            if status == 302:
                self.send_header("Location", "/redirect-target")
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass

    upstream = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=upstream.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{upstream.server_port}", state
    finally:
        upstream.shutdown()
        upstream.server_close()
        thread.join(timeout=3)


@contextlib.contextmanager
def history_server():
    state = {"records": {}, "mode": "ok", "calls": []}
    lock = threading.Lock()

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            if self.headers.get("Authorization"):
                raise RuntimeError("IAM tokens must not be sent to the independent database")
            data = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
            user = data["p_user_id"]
            with lock:
                state["calls"].append((self.path, user, data))
                status, result = self.respond(user, data)
            raw = b"invalid-json" if state["mode"] == "invalid" else json.dumps(result).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def respond(self, user, data):
            if self.headers.get("apikey") != "sb_secret_test_backend":
                return 401, {"message": "private-key-error"}
            if state["mode"] == "down":
                return 503, {"message": "private-database-error"}
            if state["mode"] == "revoked":
                return 401, {"message": "private-session-error"}
            if state["mode"] == "foreign":
                return 200, {"user_id": "foreign", "persisted": True}
            if state["mode"] == "incomplete":
                return 200, {"user_id": user, "persisted": True}
            name = self.path.removeprefix("/rest/v1/rpc/")
            if name == "mirage_create_recommendations":
                snapshot = data["p_snapshot"]
                if snapshot["user_id"] != user:
                    return 403, {}
                request_id = str(uuid.uuid4())
                stamp = datetime.now(timezone.utc).isoformat()
                result = dict(snapshot, request_id=request_id, created_at=stamp, persisted=True, outfits=[])
                for source in snapshot["outfits"]:
                    item = dict(source, id=str(uuid.uuid4()), request_id=request_id, created_at=stamp,
                                is_favorite=False, status="GENERATED", rejected_at=None, rejection_reason=None)
                    record = {k: v for k, v in result.items() if k != "outfits"}
                    record.update(item)
                    state["records"][item["id"]] = record
                    result["outfits"].append(item)
                return 200, result
            if name == "mirage_list_recommendations":
                rows = [item for item in state["records"].values() if item["user_id"] == user]
                cursor = data["p_cursor"]
                if cursor:
                    anchor = state["records"].get(cursor)
                    if not anchor or anchor["user_id"] != user:
                        return 400, {}
                    rows = [item for item in rows if (item["created_at"], item["id"]) < (anchor["created_at"], anchor["id"])]
                if data["p_favorite"] is not None:
                    rows = [item for item in rows if item["is_favorite"] == data["p_favorite"]]
                rows.sort(key=lambda item: (item["created_at"], item["id"]), reverse=True)
                limit = data["p_limit"]
                return 200, {"items": rows[:limit], "next_cursor": rows[limit-1]["id"] if len(rows) > limit else None}
            if name in ("mirage_get_recommendation", "mirage_recommendation_feedback"):
                record = state["records"].get(data["p_id"])
                if not record or record["user_id"] != user:
                    return 200, None
                if name == "mirage_recommendation_feedback":
                    if data["p_action"] == "favorite":
                        if data["p_favorite"] and record["status"] == "REJECTED":
                            return 409, {"message": "private-conflict"}
                        record["is_favorite"] = data["p_favorite"]
                    else:
                        record["is_favorite"] = False
                        record["status"] = "REJECTED"
                        record["rejected_at"] = record["rejected_at"] or datetime.now(timezone.utc).isoformat()
                        record["rejection_reason"] = data.get("p_reason")
                return 200, record
            return 404, {}

    upstream = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=upstream.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{upstream.server_port}", state
    finally:
        upstream.shutdown()
        upstream.server_close()
        thread.join(timeout=3)


def main():
    executable = os.path.abspath(sys.argv[1])
    path = "/api/v1/recommendations"
    body = {"occasion": "Universidad", "style": "CASUAL", "max_results": 3}
    with iam_server() as (iam_url, state), history_server() as (storage_url, _):
        with server(executable, "disabled", iam_url, storage_url=storage_url) as base:
            check_documentation(base)
            status, _ = request(base, path, body)
            check(status == 401, "Disabled generation still requires authentication")
            status, response = request(base, path, body, token="valid-a")
            check(status == 503 and response["error"]["code"] == "NOT_CONFIGURED",
                  "Authenticated request must not enable generation in disabled mode")
        with server(executable, "demo", iam_url, storage_url=storage_url) as base:
            check_documentation(base)
            calls = len(state["calls"])
            for auth in (None, "Basic abc", "Bearer ", "Bearer a b", "Bearer a,b"):
                headers = {} if auth is None else {"Authorization": auth}
                status, response = request(base, path, body, headers=headers)
                check(status == 401 and response["error"]["code"] == "UNAUTHENTICATED",
                      "Reject missing or malformed bearer before calling IAM")
            check(len(state["calls"]) == calls, "Malformed headers must not reach IAM")
            status, response = request(base, path, body, token="valid-a")
            check(status == 200 and response["mode"] == "demo", "Expected authenticated demo")
            check(response["user_id"] == "20000000-0000-4000-8000-000000000001",
                  "Use IAM identity, not editable metadata")
            check(state["calls"][-2:] == [
                ("/api/v1/auth/me", "Bearer valid-a"),
                ("/api/v1/profiles/20000000-0000-4000-8000-000000000001/preferences", "Bearer valid-a")],
                "Forward the same token to identity and the verified user's preferences")
            check(len(response["outfits"]) == 3 and response["persisted"] is True,
                  "Expected three persisted outfits")
            unavailable = "10000000-0000-4000-8000-000000000006"
            check(all(unavailable not in outfit["garment_ids"] for outfit in response["outfits"]),
                  "Unavailable garment leaked into output")
            status, response = request(base, path, body, token="valid-b",
                                       headers={"X-User-Id": "forged"})
            check(status == 200 and response["user_id"].endswith("000000000002"),
                  "Caller headers must not override verified user identity")
            for token in ("expired", "invalid", "revoked", "rejected-400", "rejected-403"):
                status, response = request(base, path, body, token=token)
                check(status == 401, f"Rejected IAM identity accepted: {token}")
                check("private" not in json.dumps(response), "Do not expose IAM details")
            status, _ = request(base, path, body, token="revocable")
            check(status == 200, "Initial active session should pass")
            state["revoked"] = True
            status, _ = request(base, path, body, token="revocable")
            check(status == 401, "Revoked sessions must not survive a cached success")
            for token in ("unavailable", "rate-limited", "bad-json", "missing-id", "bad-id", "wrapped", "redirect", "slow"):
                status, response = request(base, path, body, token=token)
                check(status == 503 and response["error"]["code"] == "IAM_UNAVAILABLE",
                      f"IAM failure must fail closed: {token}")
                check("private" not in json.dumps(response), "Upstream data leaked")
            allowed_paths = {"/api/v1/auth/me",
                "/api/v1/profiles/20000000-0000-4000-8000-000000000001/preferences",
                "/api/v1/profiles/20000000-0000-4000-8000-000000000002/preferences"}
            check(all(call[0] in allowed_paths for call in state["calls"]),
                  "Only verified identity/preferences endpoints may receive the token")
            no_style = {"occasion": "Universidad", "max_results": 1}
            saved = {"preferredStyles": [" formal "], "preferredColors": [],
                     "avoidedColors": [], "preferredCategories": ["top"]}
            state["preferences"]["valid-a"] = saved
            status, response = request(base, path, no_style, token="valid-a")
            check(status == 200 and response["outfits"][0]["garment_ids"][0].endswith("000000000007"),
                  "Use saved styles when the request omits style")
            check(response["profile_preferences"]["preferred_styles"] == ["FORMAL"],
                  "Normalize Profile values at the boundary")
            status, response = request(base, path, body, token="valid-a")
            check(status == 200 and all("10000000-0000-4000-8000-000000000007" not in item["garment_ids"]
                  for item in response["outfits"]), "Explicit style must override saved styles")
            saved["preferredStyles"] = []
            saved["preferredColors"] = ["green"]
            status, response = request(base, path, no_style, token="valid-a")
            check(status == 200 and response["outfits"][0]["garment_ids"][0].endswith("000000000005"),
                  "Read updated preferred colors without cached Profile data")
            saved["preferredColors"] = []
            saved["avoidedColors"] = ["blue"]
            status, response = request(base, path, body, token="valid-a")
            check(status == 200 and len(response["outfits"]) == 1
                  and response["outfits"][0]["garment_ids"][0].endswith("000000000004"),
                  "Avoid blue garments while retaining the one-piece template")
            saved["avoidedColors"] = ["white"]
            status, response = request(base, path, body, token="valid-a")
            check(status == 200 and response["status"] == "INSUFFICIENT_GARMENTS",
                  "Avoided colors must apply to footwear")
            state["preferences"].clear()
            for token, expected, code in (
                ("profile-missing", 409, "PROFILE_REQUIRED"),
                ("profile-denied", 401, "UNAUTHENTICATED"),
                ("profile-unavailable", 503, "PROFILE_UNAVAILABLE"),
                ("profile-rate-limited", 503, "PROFILE_UNAVAILABLE"),
                ("profile-invalid", 503, "PROFILE_UNAVAILABLE"),
                ("profile-bad-json", 503, "PROFILE_UNAVAILABLE"),
                ("profile-redirect", 503, "PROFILE_UNAVAILABLE"),
                ("profile-slow", 503, "PROFILE_UNAVAILABLE")):
                status, response = request(base, path, body, token=token)
                check(status == expected and response["error"]["code"] == code,
                      f"Unexpected Profile error handling: {token}")
                check("private" not in json.dumps(response), "Do not expose Profile internals")
            state["preferences"]["valid-a"] = {"preferredStyles": [], "preferredColors": ["WHITE"],
                "avoidedColors": [" white "], "preferredCategories": []}
            status, response = request(base, path, body, token="valid-a")
            check(status == 503 and response["error"]["code"] == "PROFILE_UNAVAILABLE",
                  "Reject contradictory colors after normalization")
            state["preferences"].clear()
            check(all(call[0] in allowed_paths for call in state["calls"]),
                  "Profile redirects must not forward tokens")
            status, response = request(base, path, dict(body, excluded_garments=[
                "10000000-0000-4000-8000-000000000003"]), token="valid-a")
            check(status == 200 and response["status"] == "INSUFFICIENT_GARMENTS"
                  and response["outfits"] == [], "Excluded footwear must prevent outfits")
            for invalid in ({}, dict(body, max_results=0), dict(body, max_results=4),
                            dict(body, max_results=1.5), dict(body, user_id="other-user"),
                            dict(body, style="unknown"), dict(body, excluded_garments=["bad"]),
                            dict(body, occasion="   "), []):
                status, _ = request(base, path, invalid, token="valid-a")
                check(status == 400, f"Invalid request accepted: {invalid}")
            status, _ = request(base, path, raw=b"{bad-json", token="valid-a")
            check(status == 400, "Malformed JSON accepted")
            status, _ = request(base, path, raw=b"hello", content_type="text/plain", token="valid-a")
            check(status == 415, "Non-JSON body accepted")
    with server(executable, "demo", iam_url, storage_url=storage_url) as base:
        status, response = request(base, path, body, token="valid-a")
        check(status == 503 and response["error"]["code"] == "IAM_UNAVAILABLE",
              "Connection failure must not fall back to an anonymous demo")
    for url in ("http://example.test:8080", "https://example.test/path", "http://user:pass@localhost:8080"):
        env = dict(os.environ, IAM_BASE_URL=url)
        result = subprocess.run([executable], env=env, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, timeout=5)
        check(result.returncode != 0, "Reject unsafe IAM origins at startup")
    print("IAM sessions, fail-closed behavior, Swagger and recommendation HTTP checks passed")


if __name__ == "__main__":
    main()
