begin;

create table recommendation.conversations (
    id uuid primary key default gen_random_uuid(),
    user_id uuid not null,
    title text not null check (octet_length(title) between 1 and 120),
    created_at timestamptz not null default now(),
    version integer not null default 0 check (version between 0 and 20),
    unique (id, user_id)
);
create index conversations_owner_page on recommendation.conversations(user_id, created_at desc, id desc);
create table recommendation.conversation_turns (
    conversation_id uuid not null,
    user_id uuid not null,
    client_message_id uuid not null,
    position integer not null check (position between 1 and 20),
    message text not null check (octet_length(message) between 1 and 2000),
    assistant_message text not null check (octet_length(assistant_message) between 1 and 4000),
    recommendation jsonb,
    created_at timestamptz not null default now(),
    primary key (conversation_id, client_message_id),
    unique (conversation_id, position),
    foreign key (conversation_id, user_id) references recommendation.conversations(id, user_id)
);
alter table recommendation.conversations enable row level security;
alter table recommendation.conversation_turns enable row level security;
revoke all on recommendation.conversations, recommendation.conversation_turns from public, anon, authenticated;
grant select, insert on recommendation.conversations, recommendation.conversation_turns to service_role;
grant update(version) on recommendation.conversations to service_role;

create function public.mirage_chat_create(p_user_id uuid, p_title text) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare item recommendation.conversations;
begin
    perform recommendation.require_actor(p_user_id);
    if p_title is null or octet_length(btrim(p_title)) not between 1 and 120 then
        raise sqlstate 'PT400' using message = 'invalid_title';
    end if;
    insert into recommendation.conversations(user_id,title) values (p_user_id,btrim(p_title)) returning * into item;
    return to_jsonb(item);
end; $$;

create function public.mirage_chat_list(p_user_id uuid, p_limit integer default 20, p_cursor uuid default null) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare stamp timestamptz; items jsonb; next_id uuid;
begin
    perform recommendation.require_actor(p_user_id);
    if p_limit is null or p_limit not between 1 and 100 then raise sqlstate 'PT400' using message='invalid_limit'; end if;
    if p_cursor is not null then
        select created_at into stamp from recommendation.conversations where id=p_cursor and user_id=p_user_id;
        if not found then raise sqlstate 'PT400' using message='invalid_cursor'; end if;
    end if;
    select coalesce(jsonb_agg(to_jsonb(c) order by created_at desc,id desc),'[]'::jsonb) into items
    from (select * from recommendation.conversations where user_id=p_user_id and
          (p_cursor is null or (created_at,id)<(stamp,p_cursor)) order by created_at desc,id desc limit p_limit+1) c;
    if jsonb_array_length(items)>p_limit then
        items:=items-p_limit;
        next_id:=(items->(p_limit-1)->>'id')::uuid;
    end if;
    return jsonb_build_object('items',items,'next_cursor',next_id);
end; $$;

create function public.mirage_chat_get(p_user_id uuid, p_id uuid) returns jsonb
language plpgsql security invoker set search_path = '' as $$
declare item recommendation.conversations; turns jsonb;
begin
    perform recommendation.require_actor(p_user_id);
    select * into item from recommendation.conversations where id=p_id and user_id=p_user_id;
    if not found then return null; end if;
    select coalesce(jsonb_agg(to_jsonb(t) - 'user_id' order by position),'[]'::jsonb)
    into turns from recommendation.conversation_turns t where conversation_id=p_id and user_id=p_user_id;
    return to_jsonb(item)||jsonb_build_object('turns',turns);
end; $$;

-- Save user + assistant + optional outfits in one transaction. The version prevents
-- parallel generations from silently overwriting the conversational context.
create function public.mirage_chat_append(p_user_id uuid, p_id uuid, p_client_message_id uuid,
    p_version integer, p_message text, p_assistant_message text, p_snapshot jsonb default null) returns jsonb
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
    if p_snapshot is not null then result:=public.mirage_create_recommendations(p_user_id,p_snapshot); end if;
    insert into recommendation.conversation_turns(conversation_id,user_id,client_message_id,position,message,assistant_message,recommendation)
    values(p_id,p_user_id,p_client_message_id,item.version+1,p_message,p_assistant_message,result) returning * into prior;
    update recommendation.conversations set version=version+1 where id=p_id and user_id=p_user_id;
    return to_jsonb(prior)-'user_id';
end; $$;

revoke all on function public.mirage_chat_create(uuid,text), public.mirage_chat_list(uuid,integer,uuid),
 public.mirage_chat_get(uuid,uuid), public.mirage_chat_append(uuid,uuid,uuid,integer,text,text,jsonb) from public,anon,authenticated;
grant execute on function public.mirage_chat_create(uuid,text), public.mirage_chat_list(uuid,integer,uuid),
 public.mirage_chat_get(uuid,uuid), public.mirage_chat_append(uuid,uuid,uuid,integer,text,text,jsonb) to service_role;
notify pgrst, 'reload schema';
commit;
