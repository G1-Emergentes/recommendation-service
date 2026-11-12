-- Run after the migration as a database admin; all synthetic rows are rolled back.
begin;
set local role service_role;
do $$
declare
    actor uuid := gen_random_uuid();
    other_actor uuid := gen_random_uuid();
    garment uuid := gen_random_uuid();
    basis extensions.vector(1024) := (array[1::real] || array_fill(0::real,array[1023]))::extensions.vector;
    other_basis extensions.vector(1024) := (array[0::real,1::real] || array_fill(0::real,array[1022]))::extensions.vector;
    matches integer;
    distance double precision;
begin
    insert into recommendation.wardrobe_embeddings
        (user_id,garment_id,revision,source_updated_at,name,description,category,colors,styles,
         availability,document,document_hash,embedding)
    values (actor,garment,1,now(),'Prenda sintética','Solo prueba SQL','TOP',array['BLACK'],array['CASUAL'],
            'AVAILABLE','Texto de prueba SQL',repeat('a',64),basis),
           (other_actor,garment,1,now(),'Otra prenda sintética','Solo prueba SQL','TOP',array['BLACK'],array['CASUAL'],
            'AVAILABLE','Texto de prueba SQL',repeat('b',64),other_basis);

    select count(*),min(embedding operator(extensions.<=>) basis) into matches,distance
    from recommendation.wardrobe_embeddings where user_id=actor;
    if matches<>1 or abs(distance)>0.00001 then raise exception 'Owner-filtered vector search failed'; end if;

    begin
        insert into recommendation.wardrobe_embeddings select * from recommendation.wardrobe_embeddings
        where user_id=actor and garment_id=garment;
        raise exception 'Duplicate should have failed';
    exception when unique_violation then null; end;
    begin
        update recommendation.wardrobe_embeddings set revision=0 where user_id=actor;
        raise exception 'Invalid revision should have failed';
    exception when check_violation then null; end;
    begin
        update recommendation.wardrobe_embeddings set embedding_model='wrong-model' where user_id=actor;
        raise exception 'Wrong model should have failed';
    exception when check_violation then null; end;
    begin
        update recommendation.wardrobe_embeddings set embedding='[1,0,0]'::extensions.vector where user_id=actor;
        raise exception 'Wrong dimension should have failed';
    exception when data_exception then null; end;
    begin
        update recommendation.wardrobe_embeddings set embedding=array_fill(0::real,array[1024])::extensions.vector where user_id=actor;
        raise exception 'Zero vector should have failed';
    exception when check_violation then null; end;
    begin
        update recommendation.wardrobe_embeddings set colors=array['NOT_A_COLOR'] where user_id=actor;
        raise exception 'Invalid color should have failed';
    exception when check_violation then null; end;
end;
$$;
set local role anon;
do $$
begin
    begin
        perform 1 from recommendation.wardrobe_embeddings;
        raise exception 'Anon table access should have failed';
    exception when insufficient_privilege then null; end;
end;
$$;
set local role authenticated;
do $$
begin
    begin
        perform 1 from recommendation.wardrobe_embeddings;
        raise exception 'Authenticated table access should have failed';
    exception when insufficient_privilege then null; end;
end;
$$;
rollback;
select 'PASS: owner filtering, cosine distance, duplicate/revision/model/dimension/color/zero-vector guards and denied client access' as verification;
