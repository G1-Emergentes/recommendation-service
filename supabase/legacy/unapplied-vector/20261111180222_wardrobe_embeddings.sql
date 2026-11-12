begin;

create extension if not exists vector with schema extensions;

-- A private retrieval read-model. Closet remains the source of truth.
-- No cross-project foreign keys: IAM and Closet live in separate databases.
create table recommendation.wardrobe_embeddings (
    user_id uuid not null,
    garment_id uuid not null,
    revision integer not null check (revision > 0),
    source_updated_at timestamptz not null,
    name text not null check (octet_length(name) between 1 and 120 and name ~ '[^[:space:]]'),
    description text not null check (octet_length(description) <= 1000),
    category text not null check (category in ('TOP','BOTTOM','ONE_PIECE','FOOTWEAR','OUTERWEAR','ACCESSORY')),
    subcategory text check (subcategory ~ '^[A-Z0-9_]{1,60}$'),
    colors text[] not null default '{}' check (
        cardinality(colors) <= 13 and array_position(colors,null) is null and
        colors <@ array['BLACK','WHITE','GRAY','BLUE','RED','GREEN','YELLOW','ORANGE','PURPLE','PINK','BROWN','BEIGE','MULTICOLOR']::text[]),
    styles text[] not null default '{}' check (
        cardinality(styles) <= 7 and array_position(styles,null) is null and
        styles <@ array['CASUAL','FORMAL','SPORT','STREETWEAR','MINIMALIST','CLASSIC','BOHEMIAN']::text[]),
    availability text not null check (availability in ('AVAILABLE','UNAVAILABLE')),
    image_id uuid,
    image_version integer,
    document text not null check (octet_length(document) between 1 and 4000 and document ~ '[^[:space:]]'),
    document_hash text not null check (document_hash ~ '^[0-9a-f]{64}$'),
    document_version smallint not null default 1 check (document_version = 1),
    embedding_model text not null default 'nvidia/nemotron-3.5-embed-1b'
        check (embedding_model = 'nvidia/nemotron-3.5-embed-1b'),
    embedding extensions.vector(1024) not null check (extensions.vector_norm(embedding) > 0),
    indexed_at timestamptz not null default now(),
    synced_at timestamptz not null default now(),
    primary key (user_id,garment_id),
    check ((image_id is null and image_version is null) or
           (image_id is not null and image_version is not null and image_version > 0))
);

comment on table recommendation.wardrobe_embeddings is
    'Private vector retrieval cache of Digital Closet, scoped by IAM user. Synchronization and semantic RPCs are not implemented yet.';
comment on column recommendation.wardrobe_embeddings.embedding is
    '1024-dimensional float embedding from nvidia/nemotron-3.5-embed-1b; not an image or an outfit JSON.';

-- Same boundary as the existing recommendation tables: no browser/mobile access.
-- service_role bypasses RLS; future RPCs must derive the actor from IAM and filter user_id.
alter table recommendation.wardrobe_embeddings enable row level security;
revoke all on recommendation.wardrobe_embeddings from public,anon,authenticated;
grant select,insert,update,delete on recommendation.wardrobe_embeddings to service_role;

-- The primary key supports per-user exact retrieval. Add ANN only after measuring.
commit;
