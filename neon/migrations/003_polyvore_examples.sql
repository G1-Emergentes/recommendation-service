begin;
create table recommendation.outfit_examples (
 id text primary key check (id ~ '^polyvore-[0-9a-f]{12}-[0-9]{1,20}$'),
 source_outfit_id text not null check (source_outfit_id ~ '^[1-9][0-9]{0,19}$'),
 title text not null check (octet_length(title) between 1 and 120),
 structure text not null check (structure in ('SEPARATES','ONE_PIECE')),
 items jsonb not null check (jsonb_typeof(items)='array' and jsonb_array_length(items) in (2,3)),
 content text not null check (octet_length(content) between 1 and 1400),
 source_items_count integer not null check (source_items_count between 2 and 8),
 omitted_items_count integer not null check (omitted_items_count>=0 and omitted_items_count=source_items_count-jsonb_array_length(items)),
 source_url text not null,
 source_commit text not null check (source_commit ~ '^[0-9a-f]{40}$'),
 source_archive_sha256 text not null check (source_archive_sha256 ~ '^[0-9a-f]{64}$'),
 source_split text not null check (source_split='train'),
 license text not null check (license='Apache-2.0'),
 license_scope text not null check (license_scope='repository_metadata_only_no_images'),
 attribution text not null check (octet_length(attribution) between 1 and 240),
 published boolean not null default false,
 document_hash text not null check (document_hash ~ '^[0-9a-f]{64}$'),
 embedding_model text not null check (embedding_model='nvidia/nemotron-3.5-embed-1b'),
 embedding public.vector(1024) not null check (public.vector_norm(embedding)>0),
 indexed_at timestamptz not null default now(),
 check (source_url='https://github.com/xthan/polyvore-dataset/blob/'||source_commit||'/polyvore.tar.gz'),
 check (id='polyvore-'||left(source_commit,12)||'-'||source_outfit_id)
);
comment on table recommendation.outfit_examples is 'Shared historical Polyvore core-category projections. Observed co-occurrence only, not user inventory, current trends, or calibrated compatibility labels. Train metadata only; no images.';
alter table recommendation.outfit_examples enable row level security;
alter table recommendation.outfit_examples force row level security;
revoke all on recommendation.outfit_examples from public,mirage_vector_app;
grant select on recommendation.outfit_examples to mirage_vector_app;
create policy examples_published_actor on recommendation.outfit_examples for select to mirage_vector_app
 using (published and source_split='train' and nullif(current_setting('app.user_id',true),'')::uuid is not null);
create policy examples_editor_admin on recommendation.outfit_examples for all to neondb_owner using (true) with check (true);
commit;
