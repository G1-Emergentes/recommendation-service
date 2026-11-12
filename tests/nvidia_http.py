"""Local model adapter checks. Never reads a real NVIDIA key or calls NVIDIA."""
import contextlib
import copy
import http.server
import json
import sys
import threading
import time
from http_smoke import check, history_server, iam_server, request, server


@contextlib.contextmanager
def nvidia_server():
    state = {"mode": "valid", "calls": []}

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            check(self.path == "/v1/chat/completions", "Unexpected model route")
            check(self.headers.get("Authorization") == "Bearer nvapi-test", "Missing model credential")
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            state["calls"].append(body)
            mode = state["mode"]
            if mode == "timeout":
                time.sleep(1.5)
            status = {"http401": 401, "http429": 429, "http503": 503, "redirect": 302}.get(mode, 200)
            context = json.loads(body["messages"][1]["content"])
            if 'garments' not in context:mode='valid'
            def ids(category):
                return [item["id"] for item in context.get("garments",[]) if item["category"] == category]
            tops, bottoms, shoes, pieces = (ids(name) for name in ("TOP", "BOTTOM", "FOOTWEAR", "ONE_PIECE"))
            outfits = []
            if tops and bottoms and shoes:
                outfits.append({"garment_ids": [tops[0], bottoms[0], shoes[0]],
                                "explanation": "Una combinacion casual con colores sencillos para ir a clases."})
            if pieces and shoes:
                outfits.append({"garment_ids": [pieces[0], shoes[0]], "explanation": "Una alternativa comoda de una pieza."})
            outfits = outfits[:context.get("max_results",3)]
            if mode == "unknown-id":
                outfits[0]["garment_ids"][0] = "99999999-0000-4000-8000-000000000099"
            elif mode == "unavailable-id":
                outfits[0]["garment_ids"][0] = "10000000-0000-4000-8000-000000000006"
            elif mode == "duplicate-id":
                outfits[0]["garment_ids"][1] = outfits[0]["garment_ids"][0]
            elif mode == "incomplete":
                outfits[0]["garment_ids"] = outfits[0]["garment_ids"][:2]
            elif mode == "duplicate-outfit":
                outfits = [outfits[0], dict(outfits[0], garment_ids=list(reversed(outfits[0]["garment_ids"])))]
            elif mode == "too-many":
                outfits = outfits * 4
            elif mode == "empty-explanation":
                outfits[0]["explanation"] = "   "
            elif mode == "long-explanation":
                outfits[0]["explanation"] = "x" * 1001
            elif mode == "empty-outfits":
                outfits = []
            payload = {"outfits": outfits}
            if "message" in context:
                payload["assistant_message"] = "Estas son mis propuestas para tu mensaje."
                payload.update(action="RECOMMEND",excluded_colors=[],dark_only=False,reference_outfit=0,change_category="NONE")
                payload['memory_update']={'allowed_colors':[],'relax_dark':False,'style':None,'occasion':None}
                payload['preference_update']={'operation':'NONE','field':'NONE','values':[]}
                if context["message"] == "Hola":
                    payload.update(action="CHAT",outfits=[],assistant_message="Hola, para que ocasion buscas un outfit?")
                if context["message"].startswith("Cambia"):
                    payload.update(action="CLARIFY",outfits=[],assistant_message="No hay otro calzado disponible. Quieres cambiar otra prenda?")
                if context["message"].startswith("Explica"):
                    payload.update(action="EXPLAIN",outfits=[],reference_outfit=1)
                if mode == "empty-chat": payload["assistant_message"] = ""
                if mode == "prose-only":payload.update(action="RECOMMEND",outfits=[],assistant_message="Aqui tienes tres outfits nuevos.")
                if mode == "chat-with-outfits":payload['action']="CHAT"
                if mode == "invalid-action":payload['action']="UNKNOWN"
                if mode == "excluded-white":payload['excluded_colors']=["WHITE"]
                if mode == "dark-violation":payload['dark_only']=True
                if mode == "bad-reference":payload.update(action="EXPLAIN",outfits=[],reference_outfit=3)
                if mode == "modify-no-change":payload.update(action="MODIFY",outfits=outfits[:1],reference_outfit=1,change_category="FOOTWEAR")
                if state.get('response') is not None:payload=copy.deepcopy(state['response'])
            output = json.dumps(payload)
            if mode == "code-fence":
                output = "```json\n" + output + "\n```"
            envelope = {"choices": [{"finish_reason": "length" if mode == "truncated" else "stop",
                                      "message": {"content": output, "reasoning_content": "private-reasoning-marker"}}]}
            if state.get('tool_sequence'):envelope=copy.deepcopy(state['tool_sequence'].pop(0))
            if state.get('on_tool') and envelope.get('choices',[{}])[0].get('finish_reason')=='tool_calls':
                state['on_tool']()
            if mode == "empty-choices":
                envelope["choices"] = []
            if status != 200:
                envelope = {"error": {"message": "private-provider-details"}}
            data = b"not-json" if mode == "not-json" else json.dumps(envelope).encode()
            self.send_response(status)
            if status == 302:
                self.send_header("Location", "/must-not-follow")
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


def main():
    executable = sys.argv[1]
    path = "/api/v1/recommendations"
    body = {"occasion": "Universidad", "style": "CASUAL", "max_results": 2}
    with iam_server() as (iam_url, _), history_server() as (storage_url, storage), nvidia_server() as (nvidia_url, model):
        with server(executable, "demo", iam_url, storage_url=storage_url,
                    engine="nemotron", nvidia_url=nvidia_url) as base:
            status, health = request(base, "/health")
            check(status == 200 and health["llm_enabled"] and health["engine"] == "nemotron", "Model capability reporting")
            status, result = request(base, path, body, token="valid-a")
            check(status == 200 and result["llm_used"] and result["engine"] == "nemotron"
                  and result["model"] == "nvidia/nemotron-3.5-lightning-30b-a3b", "Use model and persist provenance")
            check(result["outfits"][0]["explanation"].startswith("Una combinacion"), "Use the model explanation")
            check("private-reasoning-marker" not in json.dumps(result), "Do not expose model reasoning")
            sent = model["calls"][-1]
            check(sent["stream"] is False and sent["chat_template_kwargs"]["enable_thinking"] is False,
                  "Use bounded structured output without reasoning stream")
            check(sent["response_format"]["type"] == "json_schema", "Constrain structured output")
            context = json.loads(sent["messages"][1]["content"])
            check(all(item["name"] and item["description"] for item in context["garments"]), "Send garment descriptions")
            check(not any(word in context for word in ("user_id", "access_token", "email")), "Exclude account data from model context")
            forbidden = {"10000000-0000-4000-8000-000000000006", "10000000-0000-4000-8000-000000000007"}
            check(not forbidden.intersection(item["id"] for item in context["garments"]), "Filter unavailable and incompatible-style garments before model")
            for mode in ("unknown-id", "unavailable-id", "duplicate-id", "incomplete", "duplicate-outfit",
                         "too-many", "empty-explanation", "long-explanation", "empty-outfits", "code-fence",
                         "truncated", "empty-choices", "not-json", "http401", "http429", "http503", "redirect", "timeout"):
                model["mode"] = mode
                before = len(storage["records"])
                status, result = request(base, path, body, token="valid-a")
                expected = 503 if mode.startswith("http") or mode in ("redirect", "timeout") else 502
                check(status == expected and result["error"]["code"].startswith("MODEL_"), "Handle invalid model response: " + mode)
                check(len(storage["records"]) == before, "Never persist invalid model outfits or silently fall back")
                check("private" not in json.dumps(result), "Do not return provider internals")
            model["mode"] = "valid"
            calls_before = len(model["calls"])
            status, result = request(base, path, dict(body, excluded_garments=[
                "10000000-0000-4000-8000-000000000003"]), token="valid-a")
            check(status == 200 and result["status"] == "INSUFFICIENT_GARMENTS" and not result["llm_used"], "Skip model when no complete outfit is possible")
            check(len(model["calls"]) == calls_before, "Do not spend a model call without viable garments")
        with server(executable, "demo", iam_url, storage_url=storage_url, engine="nemotron") as base:
            status, result = request(base, path, body, token="valid-a")
            check(status == 503 and result["error"]["code"] == "MODEL_NOT_CONFIGURED", "Missing key fails explicitly without external calls")
    print("NVIDIA adapter: structured output, eligible IDs, validation, provenance and failures passed")


if __name__ == "__main__":
    main()
