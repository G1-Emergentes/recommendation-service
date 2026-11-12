begin;

create schema recommendation;
revoke all on schema recommendation from public, anon, authenticated;
grant usage on schema recommendation to service_role;

create table recommendation.requests (
    id uuid primary key default gen_random_uuid(),
    user_id uuid not null,
    created_at timestamptz not null default now(),
    context jsonb not null check (jsonb_typeof(context) = 'object'),
    unique (id, user_id)
);
create index recommendation_requests_owner on recommendation.requests(user_id);

create table recommendation.outfits (
    id uuid primary key default gen_random_uuid(),
    request_id uuid not null,
    user_id uuid not null,
    position integer not null check (position between 1 and 3),
    created_at timestamptz not null default now(),
    garment_ids uuid[] not null check (cardinality(garment_ids) between 2 and 3),
    explanation text not null check (length(explanation) between 1 and 2000),
    is_favorite boolean not null default false,
    rejected_at timestamptz,
    rejection_reason text check (length(rejection_reason) between 1 and 500),
    unique (request_id, position),
    foreign key (request_id, user_id) references recommendation.requests(id, user_id) on delete cascade,
    check (not (is_favorite and rejected_at is not null)),
    check (rejection_reason is null or rejected_at is not null)
);
create index recommendation_outfits_history
    on recommendation.outfits(user_id, created_at desc, id desc);
create index recommendation_outfits_request on recommendation.outfits(request_id);

alter table recommendation.requests enable row level security;
alter table recommendation.outfits enable row level security;
-- End users have no database access. IAM authenticates at the service boundary;
-- only this backend's private API key may execute the scoped RPCs below.
revoke all on recommendation.requests, recommendation.outfits from public, anon, authenticated;
grant select, insert on recommendation.requests, recommendation.outfits to service_role;
grant update(is_favorite, rejected_at, rejection_reason) on recommendation.outfits to service_role;

create function recommendation.require_actor(p_user_id uuid) returns void
language plpgsql security invoker set search_path = '' as $$
begin
    if current_user <> 'service_role' then
        raise sqlstate 'PT403' using message = 'backend_access_required';
    end if;
    if p_user_id is null then
        raise sqlstate 'PT400' using message = 'actor_required';
    end if;
end;
$$;

create function recommendation.outfit_json(p_id uuid, p_user_id uuid) returns jsonb
language sql stable security invoker set search_path = '' as $$
    select r.context || jsonb_build_object(
        'id', o.id, 'request_id', r.id, 'user_id', o.user_id,
        'created_at', o.created_at, 'persisted', true,
        'garment_ids', to_jsonb(o.garment_ids), 'explanation', o.explanation,
        'is_favorite', o.is_favorite,
        'status', case when o.rejected_at is null then 'GENERATED' else 'REJECTED' end,
        'rejected_at', o.rejected_at, 'rejection_reason', o.rejection_reason)
    from recommendation.outfits o join recommendation.requests r on r.id = o.request_id
    where o.id = p_id and o.user_id = p_user_id;
$$;

create function public.mirage_create_recommendations(p_user_id uuid, p_snapshot jsonb) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare
    request_id uuid;
    item jsonb;
    ids uuid[];
    preferences jsonb;
    list_name text;
    sequence integer := 0;
    outfits jsonb := '[]'::jsonb;
    outfit_id uuid;
    stamp timestamptz;
begin
    perform recommendation.require_actor(p_user_id);
    if jsonb_typeof(p_snapshot) is distinct from 'object'
       or p_snapshot->>'user_id' is distinct from p_user_id::text
       or p_snapshot->>'mode' is distinct from 'demo'
       or p_snapshot->>'engine' is distinct from 'category_rules'
       or coalesce(p_snapshot->>'status', '') not in ('GENERATED', 'INSUFFICIENT_GARMENTS')
       or coalesce(length(btrim(p_snapshot->>'occasion')), 0) not between 1 and 120
       or jsonb_typeof(p_snapshot->'outfits') is distinct from 'array'
       or jsonb_typeof(p_snapshot->'parameters') is distinct from 'object' then
        raise sqlstate 'PT400' using message = 'invalid_recommendation_snapshot';
    end if;
    if jsonb_array_length(p_snapshot->'outfits') > 3
       or ((p_snapshot->>'status' = 'GENERATED') <> (jsonb_array_length(p_snapshot->'outfits') > 0)) then
        raise sqlstate 'PT400' using message = 'invalid_recommendation_outfits';
    end if;
    preferences := p_snapshot->'profile_preferences';
    if jsonb_typeof(preferences) is distinct from 'object' then
        raise sqlstate 'PT400' using message = 'invalid_preferences';
    end if;
    foreach list_name in array array['preferred_styles','preferred_colors','avoided_colors','preferred_categories'] loop
        if jsonb_typeof(preferences->list_name) is distinct from 'array' then
            raise sqlstate 'PT400' using message = 'invalid_preferences';
        end if;
        if jsonb_array_length(preferences->list_name) > 50 or exists (
            select 1 from jsonb_array_elements(preferences->list_name) v
            where jsonb_typeof(v) <> 'string' or length(v #>> '{}') not between 1 and 80
        ) then
            raise sqlstate 'PT400' using message = 'invalid_preferences';
        end if;
    end loop;
    insert into recommendation.requests(user_id, context)
    values (p_user_id, p_snapshot - 'outfits' - 'persisted' - 'user_id')
    returning id, created_at into request_id, stamp;
    for item in select value from jsonb_array_elements(p_snapshot->'outfits') loop
        if jsonb_typeof(item->'garment_ids') is distinct from 'array'
           or coalesce(length(btrim(item->>'explanation')), 0) not between 1 and 2000 then
            raise sqlstate 'PT400' using message = 'invalid_outfit';
        end if;
        begin
            select array_agg(value::uuid) into ids from jsonb_array_elements_text(item->'garment_ids');
        exception when invalid_text_representation then
            raise sqlstate 'PT400' using message = 'invalid_garment_id';
        end;
        if coalesce(cardinality(ids), 0) not between 2 and 3 or array_position(ids, null) is not null
           or cardinality(ids) <> (select count(distinct value) from unnest(ids) value) then
            raise sqlstate 'PT400' using message = 'invalid_garment_ids';
        end if;
        sequence := sequence + 1;
        insert into recommendation.outfits(request_id, user_id, position, garment_ids, explanation)
        values (request_id, p_user_id, sequence, ids, item->>'explanation') returning id into outfit_id;
        outfits := outfits || jsonb_build_array(jsonb_build_object(
            'id', outfit_id, 'request_id', request_id, 'created_at', stamp,
            'garment_ids', to_jsonb(ids), 'explanation', item->>'explanation',
            'is_favorite', false, 'status', 'GENERATED', 'rejected_at', null, 'rejection_reason', null));
    end loop;
    return p_snapshot || jsonb_build_object('request_id', request_id, 'created_at', stamp,
                                            'persisted', true, 'outfits', outfits);
end;
$$;

create function public.mirage_list_recommendations(
    p_user_id uuid, p_limit integer default 20, p_cursor uuid default null, p_favorite boolean default null
) returns jsonb language plpgsql security invoker set search_path = '' as $$
declare
    anchor_time timestamptz;
    item record;
    items jsonb := '[]'::jsonb;
    last_id uuid;
    next_cursor uuid;
    count integer := 0;
begin
    perform recommendation.require_actor(p_user_id);
    if p_limit is null or p_limit not between 1 and 100 then
        raise sqlstate 'PT400' using message = 'invalid_limit';
    end if;
    if p_cursor is not null then
        select created_at into anchor_time from recommendation.outfits
        where id = p_cursor and user_id = p_user_id;
        if not found then raise sqlstate 'PT400' using message = 'invalid_cursor'; end if;
    end if;
    for item in
        select id from recommendation.outfits
        where user_id = p_user_id
          and (p_favorite is null or is_favorite = p_favorite)
          and (p_cursor is null or (created_at, id) < (anchor_time, p_cursor))
        order by created_at desc, id desc limit p_limit + 1
    loop
        count := count + 1;
        if count > p_limit then next_cursor := last_id; exit; end if;
        items := items || jsonb_build_array(recommendation.outfit_json(item.id, p_user_id));
        last_id := item.id;
    end loop;
    return jsonb_build_object('items', items, 'next_cursor', next_cursor);
end;
$$;

create function public.mirage_get_recommendation(p_user_id uuid, p_id uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
begin
    perform recommendation.require_actor(p_user_id);
    return recommendation.outfit_json(p_id, p_user_id);
end;
$$;

create function public.mirage_recommendation_feedback(
    p_user_id uuid, p_id uuid, p_action text, p_favorite boolean default true, p_reason text default null
) returns jsonb language plpgsql security invoker set search_path = '' as $$
declare item recommendation.outfits%rowtype;
begin
    perform recommendation.require_actor(p_user_id);
    if p_action is null or p_action not in ('favorite', 'reject') or p_favorite is null
       or (p_reason is not null and length(btrim(p_reason)) not between 1 and 500) then
        raise sqlstate 'PT400' using message = 'invalid_feedback';
    end if;
    select * into item from recommendation.outfits where id = p_id and user_id = p_user_id for update;
    if not found then return null; end if;
    if p_action = 'favorite' then
        if p_favorite and item.rejected_at is not null then
            raise sqlstate 'PT409' using message = 'recommendation_rejected';
        end if;
        update recommendation.outfits set is_favorite = p_favorite
        where id = p_id and is_favorite is distinct from p_favorite;
    else
        update recommendation.outfits
        set is_favorite = false, rejected_at = coalesce(rejected_at, now()), rejection_reason = btrim(p_reason)
        where id = p_id and (rejected_at is null or is_favorite or rejection_reason is distinct from btrim(p_reason));
    end if;
    return recommendation.outfit_json(p_id, p_user_id);
end;
$$;

revoke all on all functions in schema recommendation from public, anon, authenticated;
grant execute on all functions in schema recommendation to service_role;
revoke all on function public.mirage_create_recommendations(uuid,jsonb) from public, anon, authenticated;
revoke all on function public.mirage_list_recommendations(uuid,integer,uuid,boolean) from public, anon, authenticated;
revoke all on function public.mirage_get_recommendation(uuid,uuid) from public, anon, authenticated;
revoke all on function public.mirage_recommendation_feedback(uuid,uuid,text,boolean,text) from public, anon, authenticated;
grant execute on function public.mirage_create_recommendations(uuid,jsonb) to service_role;
grant execute on function public.mirage_list_recommendations(uuid,integer,uuid,boolean) to service_role;
grant execute on function public.mirage_get_recommendation(uuid,uuid) to service_role;
grant execute on function public.mirage_recommendation_feedback(uuid,uuid,text,boolean,text) to service_role;
notify pgrst, 'reload schema';
commit;
