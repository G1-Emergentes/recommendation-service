-- Apply only to the OLD Auth project, never to Recommendation.
begin;
lock table recommendation.requests, recommendation.outfits in access exclusive mode;
do $$
begin
    if exists(select 1 from recommendation.requests) or exists(select 1 from recommendation.outfits) then
        raise exception 'Recommendation data exists; migrate and verify it before cleanup';
    end if;
end;
$$;
drop function public.mirage_create_recommendations(jsonb);
drop function public.mirage_list_recommendations(integer,uuid,boolean);
drop function public.mirage_get_recommendation(uuid);
drop function public.mirage_recommendation_feedback(uuid,text,boolean,text);
drop function recommendation.outfit_json(uuid);
drop function recommendation.require_session();
drop table recommendation.outfits;
drop table recommendation.requests;
drop schema recommendation;
notify pgrst, 'reload schema';
commit;
