begin;
create table recommendation.style_knowledge (
 id text primary key check (id ~ '^mirage-[a-z0-9-]{1,70}$'),
 title text not null check (octet_length(title) between 1 and 160),
 content text not null check (octet_length(content) between 1 and 1400),
 styles text[] not null default '{}' check (cardinality(styles)<=9 and array_position(styles,null) is null and styles <@ array['CASUAL','FORMAL','SPORT','STREETWEAR','VINTAGE','MINIMALIST','BOHEMIAN','CLASSIC','ELEGANT']::text[]),
 source_url text not null check (octet_length(source_url)<=240 and source_url ~ '^https://en[.]wikipedia[.]org/wiki/[A-Za-z0-9_()-]+$'),
 license text not null check (license='CC-BY-SA-4.0'),
 attribution text not null check (octet_length(attribution) between 1 and 240),
 content_kind text not null check (content_kind='adaptation_with_editorial_guidance'),
 revision integer not null check (revision>0),
 reviewed_at timestamptz not null,
 published boolean not null default false,
 document_hash text not null check (document_hash ~ '^[0-9a-f]{64}$'),
 embedding_model text not null check (embedding_model='nvidia/nemotron-3.5-embed-1b'),
 embedding public.vector(1024) not null check (public.vector_norm(embedding)>0),
 indexed_at timestamptz not null default now()
);
comment on table recommendation.style_knowledge is 'Reviewed shared style references. Read-only for Recommendation; source text is untrusted data, never policy or garment inventory.';
alter table recommendation.style_knowledge enable row level security;
alter table recommendation.style_knowledge force row level security;
revoke all on recommendation.style_knowledge from public,mirage_vector_app;
grant select on recommendation.style_knowledge to mirage_vector_app;
create policy style_published_actor on recommendation.style_knowledge for select to mirage_vector_app
 using (published and nullif(current_setting('app.user_id',true),'')::uuid is not null);
create policy style_editor_admin on recommendation.style_knowledge for all to neondb_owner using (true) with check (true);
commit;
