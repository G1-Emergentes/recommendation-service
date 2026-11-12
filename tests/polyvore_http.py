"""Public Polyvore contract; --live-neon tests actual dev Neon against controlled HTTP providers."""
import copy,json,sys,uuid
from pathlib import Path
from http_smoke import check,iam_server,request,server
from closet_http import closet_server
from conversation_http import chat_storage
from nvidia_http import nvidia_server
from rag_http import embeddings_server

def main():
    exe=sys.argv[1];endpoint='/api/v1/outfit-examples/search'
    with iam_server() as (iam,_):
        with server(exe,'demo',iam) as base:
            check(request(base,endpoint,{'query':'tee and jeans'})[0]==401,'Examples require IAM')
            for body in [{'query':''},{'query':' '},{'query':'\0'},{'query':'x'*513},{'query':'é'*257},{'query':'x','limit':4},{'query':'x','user_id':'forged'},{'query':'x','source_split':'valid'},{'query':'x','url':'https://attacker'},{'query':'x','structure':'TOP'},{'query':'x','style':'CASUAL'}]:
                check(request(base,endpoint,body,token='valid-a')[0]==400,'Example arguments are bounded and allowlisted')
            check(request(base,endpoint,{'query':'tee and jeans'},token='valid-a')[0]==503,'Disabled retrieval is explicit')
    if '--live-neon' not in sys.argv:
        print('Polyvore HTTP contract passed: IAM, query bounds, no actor/URL/split injection, disabled mode.');return
    root=Path(__file__).resolve().parents[1];config={}
    for line in (root/'.env.rag-test').read_text(encoding='utf-8-sig').splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            k,v=line.split('=',1);config[k.strip()]=v.strip()
    check(config.get('NEON_VECTOR_BRANCH_ID')=='br-fancy-wildflower-bcxev7zf','Only prepared development branch')
    with iam_server() as (iam,_),closet_server() as (closet,wardrobe),chat_storage() as (storage,db),nvidia_server() as (nvidia,model),embeddings_server() as (embedding,provider):
        with server(exe,'live',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia,closet_url=closet,vector_url=config['NEON_VECTOR_DATABASE_URL'],embedding_url=embedding) as base:
            def call(path,body,token='valid-a'):return request(base,path,body,token=token,timeout=70)
            def context():return json.loads(model['calls'][-1]['messages'][1]['content'])
            def create():
                status,c=call('/api/v1/conversations',{'title':'Synthetic Polyvore evaluation'});check(status==201,'Create synthetic chat');return '/api/v1/conversations/'+c['id']+'/messages'
            def send(path,text):return call(path,{'message':text,'client_message_id':str(uuid.uuid4())})
            template=dict(action='RECOMMEND',assistant_message='Una combinacion.',outfits=[dict(garment_ids=[g['id'] for g in wardrobe['garments'][:3]],explanation='Only actual closet garments.')],excluded_colors=[],dark_only=False,reference_outfit=0,change_category='NONE',memory_update=dict(allowed_colors=[],relax_dark=False,style=None,occasion=None),preference_update=dict(operation='NONE',field='NONE',values=[]))
            try:
                for shape in ['SEPARATES','ONE_PIECE']:
                    status,result=call(endpoint,{'query':'casual outfit','structure':shape,'limit':3})
                    check(status==200 and len(result['items'])==3 and all(r['structure']==shape and r['source_split']=='train' for r in result['items']),'Filter complete source structures, never validation/test')
                    check(all(r['license']=='Apache-2.0' and r['license_scope']=='repository_metadata_only_no_images' and r['id'].startswith('polyvore-') and 'embedding' not in r and all('image' not in i for i in r['items']) for r in result['items']),'Provenance with no images, vectors or user IDs')
                status,shared=call(endpoint,{'query':'casual outfit','structure':'ONE_PIECE','limit':3},'valid-b')
                check(status==200 and shared['items']==result['items'],'Public examples are shared among authenticated actors')
                # A closet without dresses must receive separates examples only.
                wardrobe['garments']=[g for g in wardrobe['garments'] if g['category']!='ONE_PIECE']
                path=create();model['response']=copy.deepcopy(template)
                status,turn=send(path,'Quiero un outfit casual')
                check(status==200 and turn['recommendation'],'Generate with actual Polyvore retrieval')
                sent=context();examples=sent['outfit_examples'];own={g['id'] for g in sent['garments']}
                check(1<=len(examples)<=3 and all(r['structure']=='SEPARATES' for r in examples),'Automatic examples match structures available in Closet')
                check(turn['recommendation']['outfit_examples']==examples,'Persist actual retrieved projections as provenance')
                check(all(set(o['garment_ids'])<=own for o in turn['recommendation']['outfits']),'Only own candidate UUIDs in generated outfits')
                bad=copy.deepcopy(template);bad['outfits'][0]['garment_ids'][0]=examples[0]['id'];model['response']=bad
                before=len(db['chats'])
                check(send(create(),'Quiero un outfit')[0]==502,'External Polyvore ID can never become a garment ID')
                # Fail only the examples query, after style knowledge and private candidate retrieval succeeded.
                model['response']=copy.deepcopy(template);provider['fail_query_number']=sum(c['input_type']=='query' for c in provider['calls'])+3
                calls_before=len(model['calls'])
                status,failed=send(create(),'Quiero un outfit nuevo')
                check(status==503 and len(model['calls'])==calls_before,'Failed Polyvore query never reaches model or silently drops references')
                provider.pop('fail_query_number',None)
            finally:
                provider['mode']='ok';provider.pop('fail_query_number',None);wardrobe['mode']='empty'
                for token in ['valid-a','valid-b']:
                    status,_=call('/api/v1/wardrobe-index/sync',{},token);check(status==200,'Clean reserved synthetic wardrobe actors only')
    print('Polyvore integration passed: authenticated published train retrieval, source structure coverage, shared read-only examples, private candidate UUIDs, provenance, external-ID rejection and fail-closed dependency errors. Synthetic wardrobe actors cleaned.')
if __name__=='__main__':main()
