begin;
alter table recommendation.conversation_turns add column memory jsonb not null
 default '{"excluded_colors":[],"dark_only":false,"style":"","occasion":"","profile_updated":false}'::jsonb
 check(jsonb_typeof(memory)='object');
-- Replace the signature, keeping defaults for callers that do not send memory yet.
drop function public.mirage_chat_append(uuid,uuid,uuid,integer,text,text,jsonb);
create function public.mirage_chat_append(p_user_id uuid, p_id uuid, p_client_message_id uuid,
    p_version integer, p_message text, p_assistant_message text, p_snapshot jsonb default null, p_memory jsonb default null) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare item recommendation.conversations; prior recommendation.conversation_turns; result jsonb;
begin
    perform recommendation.require_actor(p_user_id);
    select * into item from recommendation.conversations where id=p_id and user_id=p_user_id for update;
    if not found then return null; end if;
    select * into prior from recommendation.conversation_turns where conversation_id=p_id and client_message_id=p_client_message_id;
    if found then
        if prior.message is distinct from p_message then raise sqlstate 'PT409' using message='message_id_reused'; end if;
        return to_jsonb(prior)-'user_id';
    end if;
    if p_version is null or item.version<>p_version then raise sqlstate 'PT409' using message='conversation_changed'; end if;
    if item.version>=20 then raise sqlstate 'PT409' using message='conversation_full'; end if;
    if p_memory is null then
        select memory into p_memory from recommendation.conversation_turns
        where conversation_id=p_id and user_id=p_user_id order by position desc limit 1;
        p_memory:=coalesce(p_memory,'{"excluded_colors":[],"dark_only":false,"style":"","occasion":"","profile_updated":false}'::jsonb);
    end if;
    if jsonb_typeof(p_memory) is distinct from 'object' or
       jsonb_typeof(p_memory->'excluded_colors') is distinct from 'array' or
       jsonb_typeof(p_memory->'dark_only') is distinct from 'boolean' or
       jsonb_typeof(p_memory->'style') is distinct from 'string' or
       jsonb_typeof(p_memory->'occasion') is distinct from 'string' or
       jsonb_typeof(p_memory->'profile_updated') is distinct from 'boolean' then
        raise sqlstate 'PT400' using message='invalid_memory';
    end if;
    if (select count(*) from jsonb_object_keys(p_memory))<>5 or
       jsonb_array_length(p_memory->'excluded_colors')>13 or
       octet_length(p_memory->>'style')>80 or octet_length(p_memory->>'occasion')>120 or
       exists(select 1 from jsonb_array_elements_text(p_memory->'excluded_colors') c
              where c not in ('BLACK','WHITE','GRAY','BLUE','RED','GREEN','YELLOW','ORANGE','PURPLE','PINK','BROWN','BEIGE','MULTICOLOR')) then
        raise sqlstate 'PT400' using message='invalid_memory';
    end if;
    if p_snapshot is not null then result:=public.mirage_create_recommendations(p_user_id,p_snapshot); end if;
    insert into recommendation.conversation_turns(conversation_id,user_id,client_message_id,position,message,assistant_message,recommendation,memory)
    values(p_id,p_user_id,p_client_message_id,item.version+1,p_message,p_assistant_message,result,p_memory) returning * into prior;
    update recommendation.conversations set version=version+1 where id=p_id and user_id=p_user_id;
    return to_jsonb(prior)-'user_id';
end; $$;

revoke all on function public.mirage_chat_append(uuid,uuid,uuid,integer,text,text,jsonb,jsonb) from public,anon,authenticated;
grant execute on function public.mirage_chat_append(uuid,uuid,uuid,integer,text,text,jsonb,jsonb) to service_role;
notify pgrst, 'reload schema';
commit;
