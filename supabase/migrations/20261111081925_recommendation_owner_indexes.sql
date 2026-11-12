begin;
create index conversation_turns_owner_fk on recommendation.conversation_turns(conversation_id, user_id);
create index recommendation_outfits_owner_fk on recommendation.outfits(request_id, user_id);
commit;
