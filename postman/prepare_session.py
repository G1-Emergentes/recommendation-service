"""Create a private Postman environment using normal IAM login; never print credentials."""
import json
import time
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def prepare(credentials, template, output, iam_url="http://127.0.0.1:8080"):
    values = {}
    for line in credentials.read_text(encoding="utf-8-sig").splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            name, value = line.split("=", 1)
            value = value.strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            values[name.strip()] = value
    if not values.get("POSTMAN_EMAIL") or not values.get("POSTMAN_PASSWORD"):
        raise RuntimeError("Completa POSTMAN_EMAIL y POSTMAN_PASSWORD en iam-service/.env.postman-test.")
    payload = json.dumps({"email": values["POSTMAN_EMAIL"], "password": values["POSTMAN_PASSWORD"]}).encode()
    request = urllib.request.Request(iam_url + "/api/v1/auth/login", data=payload,
                                     headers={"Content-Type": "application/json"})
    try:
        response = urllib.request.urlopen(request, timeout=30)
    except urllib.error.HTTPError as error:
        raise RuntimeError("IAM rechazo el login (HTTP " + str(error.code) + "). No se exporto ninguna sesion.") from None
    except urllib.error.URLError:
        raise RuntimeError("IAM no esta disponible. Inicia iam-service y vuelve a ejecutar.") from None
    with response:
        session = json.load(response)
    if not isinstance(session.get("access_token"), str) or not session["access_token"] or \
       not isinstance(session.get("refresh_token"), str) or not session["refresh_token"]:
        raise RuntimeError("IAM no devolvio una sesion completa; no se exporto el entorno.")
    environment = json.loads(template.read_text(encoding="utf-8"))
    updates = {
        "iam_base_url": iam_url,
        "access_token": session["access_token"],
        "refresh_token": session["refresh_token"],
        "access_token_expires_at": str(session.get("expires_at") or int(time.time()) + int(session.get("expires_in", 3600))),
        "user_id": session.get("user", {}).get("id", ""),
    }
    for entry in environment["values"]:
        if entry["key"] in updates:
            entry["value"] = updates[entry["key"]]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(environment, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    try:
        prepare(ROOT.parent / "iam-service/.env.postman-test",
                ROOT / "postman/Mirage-Local.postman_environment.example.json",
                ROOT / "out/postman/Mirage-Local.private.postman_environment.json")
        print("Entorno privado actualizado con una sesion valida de IAM. Tokens y contrasena omitidos.")
    except (RuntimeError, OSError, ValueError) as error:
        print(str(error))
        raise SystemExit(1)
