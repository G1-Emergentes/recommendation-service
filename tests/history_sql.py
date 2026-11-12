"""Checks on a new disposable database in the isolated local test cluster only."""
from pathlib import Path
import copy
import json
import uuid
import psycopg2
from psycopg2 import sql


def check(value, message):
    if not value:
        raise RuntimeError(message)


def main():
    admin = psycopg2.connect(host="127.0.0.1", port=55440, user="postgres", dbname="postgres")
    admin.autocommit = True
    name = "recommendation_test_" + uuid.uuid4().hex
    connection = None
    try:
        with admin.cursor() as c:
            c.execute(sql.SQL("create database {}").format(sql.Identifier(name)))
        connection = psycopg2.connect(host="127.0.0.1", port=55440, user="postgres", dbname=name)
        connection.autocommit = True
        c = connection.cursor()
        for role in ("anon", "authenticated", "service_role"):
            c.execute("select 1 from pg_roles where rolname=%s", (role,))
            if not c.fetchone():
                c.execute(sql.SQL("create role {} {}").format(sql.Identifier(role), sql.SQL("bypassrls" if role == "service_role" else "")))
        a, b = str(uuid.uuid4()), str(uuid.uuid4())
        migration = next((Path(__file__).resolve().parents[1] / "supabase/migrations").glob("*_recommendation_isolated_history.sql"))
        for migration in sorted(migration.parent.glob("*.sql")):
            c.execute(migration.read_text(encoding="utf-8"))
        c.execute("select to_regclass('auth.users')")
        check(c.fetchone()[0] is None, "Recommendation must not require Auth tables")
        c.execute("set role service_role")
        current_actor = [None]

        def actor(user):
            current_actor[0] = user

        def rpc(function, **payload):
            payload = {"p_user_id": current_actor[0], **payload}
            keys = list(payload)
            args = sql.SQL(",").join(sql.SQL("{} => %s").format(sql.Identifier(key)) for key in keys)
            values = [json.dumps(payload[k]) if isinstance(payload[k], dict) else payload[k] for k in keys]
            c.execute(sql.SQL("select public.{}({})").format(sql.Identifier(function), args), values)
            return c.fetchone()[0]

        def rejects(action, code):
            try:
                action()
            except psycopg2.Error as error:
                check(error.pgcode == code, "Unexpected SQL failure: " + str(error.pgcode))
            else:
                raise RuntimeError("Expected SQL rejection " + code)

        snapshot = {
            "user_id": a, "mode": "demo", "engine": "category_rules", "status": "GENERATED",
            "persisted": False, "occasion": "Universidad", "parameters": {"occasion": "Universidad"},
            "profile_preferences": {k: [] for k in ("preferred_styles", "preferred_colors", "avoided_colors", "preferred_categories")},
            "outfits": [{"garment_ids": [str(uuid.uuid4()), str(uuid.uuid4())], "explanation": "Demo"} for _ in range(3)]}
        actor(a)
        created = rpc("mirage_create_recommendations", p_snapshot=snapshot)
        ids = [item["id"] for item in created["outfits"]]
        check(created["persisted"] and len(set(ids)) == 3, "Atomic creation with individual IDs")
        record = rpc("mirage_get_recommendation", p_id=ids[0])
        check(record["user_id"] == a and record["parameters"] == snapshot["parameters"], "Owner and context persisted")
        first = rpc("mirage_list_recommendations", p_limit=2)
        check(len(first["items"]) == 2 and first["next_cursor"], "Pagination first page")
        second = rpc("mirage_list_recommendations", p_limit=2, p_cursor=first["next_cursor"])
        check(len(second["items"]) == 1 and second["next_cursor"] is None, "Pagination last page")
        check({item["id"] for item in first["items"] + second["items"]} == set(ids), "No skipped/duplicate rows at timestamp ties")
        favorite = rpc("mirage_recommendation_feedback", p_id=ids[0], p_action="favorite")
        check(favorite["is_favorite"], "Favorite persists")
        check(len(rpc("mirage_list_recommendations", p_favorite=True)["items"]) == 1, "Favorite filter")
        check(rpc("mirage_recommendation_feedback", p_id=ids[0], p_action="favorite")["is_favorite"], "Idempotent favorite")
        check(not rpc("mirage_recommendation_feedback", p_id=ids[0], p_action="favorite", p_favorite=False)["is_favorite"], "Unfavorite")
        rpc("mirage_recommendation_feedback", p_id=ids[0], p_action="favorite")
        rejected = rpc("mirage_recommendation_feedback", p_id=ids[0], p_action="reject", p_reason="No me gusta")
        check(rejected["status"] == "REJECTED" and not rejected["is_favorite"], "Reject clears favorite")
        repeated = rpc("mirage_recommendation_feedback", p_id=ids[0], p_action="reject", p_reason="No me gusta")
        check(repeated["rejected_at"] == rejected["rejected_at"], "Reject replay preserves original timestamp")
        rejects(lambda: rpc("mirage_recommendation_feedback", p_id=ids[0], p_action="favorite"), "PT409")
        actor(b)
        check(rpc("mirage_get_recommendation", p_id=ids[0]) is None, "Foreign detail hidden")
        check(rpc("mirage_list_recommendations")["items"] == [], "Foreign history hidden")
        check(rpc("mirage_recommendation_feedback", p_id=ids[0], p_action="reject") is None, "Foreign mutation hidden")
        rejects(lambda: rpc("mirage_list_recommendations", p_cursor=ids[0]), "PT400")
        rejects(lambda: rpc("mirage_create_recommendations", p_snapshot=snapshot), "PT400")
        c.execute("reset role")
        c.execute("set role authenticated")
        rejects(lambda: rpc("mirage_get_recommendation", p_id=ids[0]), "42501")
        rejects(lambda: c.execute("select count(*) from recommendation.outfits"), "42501")
        c.execute("reset role")
        c.execute("set role service_role")
        actor(a)
        bad = copy.deepcopy(snapshot)
        bad["outfits"][1]["garment_ids"] = ["invalid"]
        rejects(lambda: rpc("mirage_create_recommendations", p_snapshot=bad), "PT400")
        c.execute("select count(*) from recommendation.requests")
        check(c.fetchone()[0] == 1, "Failed batch rolls back its request and earlier outfits")
        rejects(lambda: c.execute("update recommendation.outfits set user_id=%s where id=%s", (b, ids[0])), "42501")
        chat = rpc("mirage_chat_create", p_title="Test chat")
        chat_id = chat["id"]
        message_id = str(uuid.uuid4())
        payload = dict(p_id=chat_id, p_client_message_id=message_id, p_version=0,
                       p_message="Outfit para clases", p_assistant_message="Esta es una opcion", p_snapshot=snapshot)
        turn = rpc("mirage_chat_append", **payload)
        check(turn["position"] == 1 and turn["recommendation"]["persisted"], "Chat turn and outfits committed")
        check(rpc("mirage_chat_append", **payload) == turn, "Message replay returns original turn")
        rejects(lambda: rpc("mirage_chat_append", **dict(payload, p_message="different")), "PT409")
        rejects(lambda: rpc("mirage_chat_append", **dict(payload, p_client_message_id=str(uuid.uuid4()))), "PT409")
        detail = rpc("mirage_chat_get", p_id=chat_id)
        check(detail["version"] == 1 and detail["turns"] == [turn], "Durable ordered transcript")
        # A failed insert must also roll back recommendations created in the same RPC.
        c.execute("select count(*) from recommendation.requests")
        before = c.fetchone()[0]
        rejects(lambda: rpc("mirage_chat_append", **dict(payload, p_version=1,
                p_client_message_id=str(uuid.uuid4()), p_assistant_message="")), "23514")
        c.execute("select count(*) from recommendation.requests")
        check(c.fetchone()[0] == before and rpc("mirage_chat_get", p_id=chat_id)["version"] == 1, "Failed turn atomic rollback")
        for position in range(1,20):
            rpc("mirage_chat_append", **dict(payload, p_version=position, p_client_message_id=str(uuid.uuid4()), p_snapshot=None))
        rejects(lambda: rpc("mirage_chat_append", **dict(payload, p_version=20,p_client_message_id=str(uuid.uuid4()))), "PT409")
        actor(b)
        check(rpc("mirage_chat_get", p_id=chat_id) is None, "Foreign chat hidden")
        check(rpc("mirage_chat_append", **payload) is None, "Foreign chat mutation hidden")
        check(rpc("mirage_chat_list")["items"] == [], "Foreign chat list empty")
        rejects(lambda: rpc("mirage_chat_list", p_cursor=chat_id), "PT400")
        actor(a)
        chat2 = rpc("mirage_chat_create", p_title="Second")
        page = rpc("mirage_chat_list",p_limit=1)
        page2 = rpc("mirage_chat_list",p_limit=1,p_cursor=page["next_cursor"])
        check({page["items"][0]["id"],page2["items"][0]["id"]} == {chat_id,chat2["id"]}, "Chat keyset pagination")
        c.execute("reset role")
        for role in ("anon", "authenticated"):
            c.execute(sql.SQL("set role {}").format(sql.Identifier(role)))
            rejects(lambda: rpc("mirage_chat_get",p_id=chat_id), "42501")
            rejects(lambda: c.execute("select * from recommendation.conversations"), "42501")
            c.execute("reset role")
        c.execute("set role service_role")
        actor(a)
        live = copy.deepcopy(snapshot)
        live["mode"] = "live"
        live["garments"] = [{"id":str(uuid.uuid4()),"subcategory":"T_SHIRT","revision":3,
                            "updated_at":"2026-11-11T08:00:00Z","image":{"id":str(uuid.uuid4()),"version":2}}]
        saved_live = rpc("mirage_create_recommendations",p_snapshot=live)
        saved_detail = rpc("mirage_get_recommendation",p_id=saved_live["outfits"][0]["id"])
        check(saved_detail["mode"]=="live" and saved_detail["garments"]==live["garments"],"Live provenance and garment versions survive SQL persistence")
        actor(None)
        rejects(lambda: rpc("mirage_get_recommendation", p_id=ids[0]), "PT400")
        c.execute("reset role")
        c.execute("set role anon")
        rejects(lambda: rpc("mirage_get_recommendation", p_id=ids[0]), "42501")
        print("PostgreSQL: isolated persistence, pagination, feedback, actor scoping and backend-only grants passed")
    finally:
        if connection:
            connection.close()
        with admin.cursor() as c:
            c.execute(sql.SQL("drop database if exists {}").format(sql.Identifier(name)))
        admin.close()


if __name__ == "__main__":
    main()
