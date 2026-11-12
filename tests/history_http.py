"""API/repository contract with simulated IAM and Supabase; SQL has its own checks."""
import sys
import os
import subprocess
import urllib.parse
from http_smoke import check, history_server, iam_server, request, server


def main():
    executable = sys.argv[1]
    path = "/api/v1/recommendations"
    body = {"occasion": "Universidad", "style": "CASUAL", "max_results": 3}
    with iam_server() as (iam_url, iam), history_server() as (storage_url, storage):
        with server(executable, "demo", iam_url, storage_url=storage_url) as base:
            status, page = request(base, path, token="valid-a")
            check(status == 200 and page == {"items": [], "next_cursor": None}, "Empty own history")
            status, created = request(base, path, body, token="valid-a")
            check(status == 200 and created["persisted"], "Generation must persist before success")
            ids = [item["id"] for item in created["outfits"]]
            check(len(set(ids)) == 3 and all(item["request_id"] == created["request_id"]
                  for item in created["outfits"]), "Each outfit needs its own ID and request reference")
            status, record = request(base, path + "/" + ids[0], token="valid-a")
            check(status == 200 and record["parameters"] == body and record["id"] == ids[0], "Detail retains generation snapshot")
            status, first = request(base, path + "?limit=2", token="valid-a")
            check(status == 200 and len(first["items"]) == 2 and first["next_cursor"], "History first page")
            status, second = request(base, path + "?limit=2&cursor=" + first["next_cursor"], token="valid-a")
            check(status == 200 and len(second["items"]) == 1 and second["next_cursor"] is None, "History final page")
            check({item["id"] for item in first["items"] + second["items"]} == set(ids), "Pagination without duplicates")
            favorite = path + "/" + ids[0] + "/favorite"
            status, record = request(base, favorite, {}, token="valid-a")
            check(status == 200 and record["is_favorite"], "Mark favorite")
            status, again = request(base, favorite, {}, token="valid-a")
            check(status == 200 and again == record, "Favorite replay is idempotent")
            status, page = request(base, path + "?favorite=true", token="valid-a")
            check(status == 200 and [item["id"] for item in page["items"]] == [ids[0]], "Favorite filtering")
            status, record = request(base, favorite, {"favorite": False}, token="valid-a")
            check(status == 200 and not record["is_favorite"], "Remove favorite")
            request(base, favorite, {}, token="valid-a")
            reject = path + "/" + ids[0] + "/reject"
            status, rejected = request(base, reject, {"reason": "  No me gusta  "}, token="valid-a")
            check(status == 200 and rejected["status"] == "REJECTED" and not rejected["is_favorite"]
                  and rejected["rejection_reason"] == "No me gusta", "Reject and clear favorite")
            status, again = request(base, reject, {"reason": "No me gusta"}, token="valid-a")
            check(status == 200 and again == rejected, "Rejection replay preserves timestamp")
            status, conflict = request(base, favorite, {}, token="valid-a")
            check(status == 409 and conflict["error"]["code"] == "RECOMMENDATION_REJECTED", "Reject/favorite conflict")
            for target, payload in ((path + "/" + ids[0], None), (favorite, {}), (reject, {})):
                status, missing = request(base, target, payload, token="valid-b")
                check(status == 404 and missing["error"]["code"] == "RECOMMENDATION_NOT_FOUND", "Hide foreign records and mutations")
                status, _ = request(base, target, payload)
                check(status == 401, "All recommendation operations require identity")
            status, page = request(base, path, token="valid-b")
            check(status == 200 and page["items"] == [], "Users cannot list another user's records")
            status, _ = request(base, path + "?cursor=" + ids[0], token="valid-b")
            check(status == 400, "Foreign cursor must not reveal a history page")
            for query in ("limit=0", "limit=101", "limit=bad", "limit=", "cursor=bad", "favorite=yes", "user_id=other"):
                status, _ = request(base, path + "?" + query, token="valid-a")
                check(status == 400, "Validate history query: " + query)
            for target, payload in ((favorite, {"favorite": "yes"}), (favorite, {"user_id": "other"}),
                                    (reject, {"reason": " "}), (reject, {"reason": "x" * 501}),
                                    (reject, {"reason": None}), (reject, {"status": "GENERATED"})):
                status, _ = request(base, target, payload, token="valid-a")
                check(status == 400, "Validate feedback body")
            status, _ = request(base, path + "/not-a-uuid", token="valid-a")
            check(status == 400, "Validate detail UUID")
            status, _ = request(base, path + "/00000000-0000-4000-8000-000000000099", token="valid-a")
            check(status == 404, "Missing records share the foreign-record response")
            check(storage["calls"][-1][2]["p_user_id"] in (
                "20000000-0000-4000-8000-000000000001", "20000000-0000-4000-8000-000000000002"),
                "Independent storage receives the verified actor")
            calls_before = len(iam["calls"])
            status, _ = request(base, path, token="valid-a")
            check(status == 200 and len(iam["calls"]) == calls_before + 1
                  and iam["calls"][-1][0] == "/api/v1/auth/me", "History reads do not depend on Profile")
            for mode in ("down", "invalid", "foreign", "incomplete", "revoked"):
                storage["mode"] = mode
                status, response = request(base, path + "/" + ids[0], token="valid-a")
                check(status == 503, "Independent backend credential errors must not be reported as user login failures: " + mode)
                check("private" not in str(response), "Do not expose provider errors")
            storage["mode"] = "down"
            status, response = request(base, path, body, token="valid-a")
            check(status == 503 and "outfits" not in response, "Do not return unsaved outfits as persisted")
            storage["mode"] = "ok"
        with server(executable, "disabled", iam_url, storage_url=storage_url) as base:
            status, record = request(base, path + "/" + ids[0], token="valid-a")
            check(status == 200 and record["status"] == "REJECTED", "History survives service restart and disabled generation")
        with server(executable, "demo", iam_url) as base:
            status, response = request(base, path, token="valid-a")
            check(status == 503 and response["error"]["code"] == "HISTORY_NOT_CONFIGURED", "Missing storage configuration is explicit")
    print("History HTTP: persistence, pagination, feedback, restart, owner isolation and errors passed")
    same_origin = "http://127.0.0.1:59999"
    env = dict(os.environ, RECOMMENDATION_SUPABASE_URL=same_origin,
               IAM_SUPABASE_URL=same_origin, RECOMMENDATION_SUPABASE_SECRET_KEY="sb_secret_test_backend")
    result = subprocess.run([executable], env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, timeout=5)
    check(result.returncode != 0, "Reject a Recommendation connection pointing to the Auth database")


if __name__ == "__main__":
    main()
