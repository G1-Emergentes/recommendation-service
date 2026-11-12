begin;

-- Extend the immutable generation snapshot with the selected model engine.
create or replace function public.mirage_create_recommendations(p_user_id uuid, p_snapshot jsonb) returns jsonb
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
       or coalesce(p_snapshot->>'engine', '') not in ('category_rules', 'nemotron')
       or coalesce(p_snapshot->>'status', '') not in ('GENERATED', 'INSUFFICIENT_GARMENTS')
       or coalesce(length(btrim(p_snapshot->>'occasion')), 0) not between 1 and 120
       or jsonb_typeof(p_snapshot->'outfits') is distinct from 'array'
       or jsonb_typeof(p_snapshot->'parameters') is distinct from 'object'
       or (p_snapshot->>'engine' = 'nemotron' and coalesce(length(p_snapshot->>'model'), 0) not between 1 and 200) then
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

notify pgrst, 'reload schema';
commit;
