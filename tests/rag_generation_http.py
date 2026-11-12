"""Opt-in generation integration: actual dev Neon; controlled HTTP IAM/Closet/LLM/embeddings.
The reserved test actor is cleaned through authenticated sync, even on assertion failure.
"""
import copy,json,sys,uuid
from pathlib import Path
from http_smoke import check,iam_server,request,server
from closet_http import closet_server
from conversation_http import chat_storage
from nvidia_http import nvidia_server
from rag_http import embeddings_server

def main():
    root=Path(__file__).resolve().parents[1];config={}
    for line in (root/'.env.rag-test').read_text(encoding='utf-8-sig').splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            k,v=line.split('=',1);config[k.strip()]=v.strip()
    check(config.get('NEON_VECTOR_BRANCH_ID')=='br-fancy-wildflower-bcxev7zf','Use only prepared development branch')
    with iam_server() as (iam,profile),closet_server() as (closet,wardrobe),chat_storage() as (storage,db),nvidia_server() as (nvidia,model),embeddings_server() as (embedding,provider):
        original=copy.deepcopy(wardrobe['garments'])
        black=copy.deepcopy(original[0]);black.update(id='10000000-0000-4000-8000-000000000008',name='Polo negro',colors=['BLACK'],description='Polo negro liso',image=None)
        shoe=copy.deepcopy(original[2]);shoe.update(id='10000000-0000-4000-8000-000000000009',name='Botines negros',colors=['BLACK'],description='Botines negros lisos')
        many=[dict(original[0],id=f'10000000-0000-4000-8000-{i:012d}',image=None) for i in range(20,32)]
        wardrobe['garments'] = [g for g in wardrobe['garments'] if g['category']!='ONE_PIECE']+[black,shoe]+many
        with server(sys.argv[1],'live',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia,closet_url=closet,vector_url=config['NEON_VECTOR_DATABASE_URL'],embedding_url=embedding) as base:
            def call(path,body):return request(base,path,body,token='valid-a',timeout=60)
            template=dict(action='CLARIFY',assistant_message='Necesito mas prendas para esa combinacion.',outfits=[],excluded_colors=[],dark_only=False,reference_outfit=0,change_category='NONE',memory_update=dict(allowed_colors=[],relax_dark=False,style=None,occasion=None),preference_update=dict(operation='NONE',field='NONE',values=[]))
            def chat():
                status,c=call('/api/v1/conversations',{'title':'Synthetic RAG evaluation'});check(status==201,'Create synthetic conversation');return '/api/v1/conversations/'+c['id']+'/messages'
            def send(path,text,decision):
                model['response']=copy.deepcopy(decision)
                return call(path,{'message':text,'client_message_id':str(uuid.uuid4())})
            def context():return json.loads(model['calls'][-1]['messages'][1]['content'])
            def outfit(ids):return dict(garment_ids=ids,explanation='Combinacion con prendas verificadas.')
            ids=[black['id'],original[1]['id'],shoe['id']]
            try:
                path=chat();d=copy.deepcopy(template);d['excluded_colors']=['WHITE']
                status,turn=send(path,'Sin blanco por favor',d)
                check(status==200 and turn['memory']['excluded_colors']==['WHITE'],'Remember no-white constraint')
                d=copy.deepcopy(template);d.update(action='RECOMMEND',outfits=[outfit(ids)])
                status,turn=send(path,'Ahora un outfit para salir',d)
                check(status==200 and turn['recommendation'],'Generate from real vector retrieval')
                candidates=context()['garments'];allowed={g['id'] for g in candidates}
                check(len(candidates)<len(wardrobe['garments']) and len([g for g in candidates if g['category']=='TOP'])==6,'Bound category retrieval instead of sending full inventory')
                check(set(ids)<=allowed and {'TOP','BOTTOM','FOOTWEAR'}<={g['category'] for g in candidates},'Retrieve necessary category coverage and known restriction matches')
                status,turn=send(path,'Otra opcion',d)
                check(status==502,'Guard rejects repeated outfit on another-option request')
                d['outfits']=[outfit([original[4]['id'],original[1]['id'],shoe['id']])]
                status,turn=send(path,'Otra opcion',d)
                check(status==200 and turn['memory']['excluded_colors']==['WHITE'],'Distinct outfit preserves exclusions')
                schema=model['calls'][-1]['response_format']['json_schema']['schema']
                ids_schema=schema['properties']['outfits']['items']['properties']['garment_ids']
                check(len(ids_schema['enum'])==1 and len(set(ids_schema['enum'][0]))==3,'Constrain complete alternative array, preventing duplicate IDs')
                check(schema['properties']['action']['enum']==['RECOMMEND','CLARIFY'],'Alternative cannot become an unrequested modification')
                d['outfits']=[outfit([original[0]['id'],original[1]['id'],shoe['id']])]
                check(send(path,'Un outfit nuevo',d)[0]==502,'Reject forgotten white exclusion even if model omits it')
                d['outfits']=[outfit([many[-1]['id'],original[1]['id'],shoe['id']])]
                check(send(chat(),'Un outfit nuevo',d)[0]==502,'Reject owned garment outside retrieved candidate set')
                d=copy.deepcopy(template);d['dark_only']=True
                status,turn=send(path,'Oscuro pero llamativo',d)
                check(status==200 and turn['recommendation'] is None and 'confirmado' in turn['assistant_message'],'No dark trousers: clarify without inventing shade')
                check(not context()['dark_garment_ids'] or original[1]['id'] not in context()['dark_garment_ids'],'Blue alone never proves dark blue')
                status,turn=send(chat(),'Quiero un outfit sin blanco ni beige',copy.deepcopy(template))
                check(status==200 and set(turn['memory']['excluded_colors'])=={'WHITE','BEIGE'},'Keep explicit conjunction exclusions even if model omits them')
                wardrobe['resolves']=0;wardrobe['mode']='post-change';before=len(model['calls'])
                check(send(chat(),'Quiero un outfit',copy.deepcopy(template))[0]==409 and len(model['calls'])==before,'Reject source revision change after vector retrieval before LLM')
                wardrobe['mode']='ok';wardrobe['resolves']=0
                # Source has no footwear at all; the model must not invent a complete outfit.
                wardrobe['garments']=[black,original[1]]
                d=copy.deepcopy(template)
                status,turn=send(chat(),'Quiero un outfit',d)
                check(status==200 and turn['recommendation'] is None,'Insufficient wardrobe permits clarification only')
                d.update(action='RECOMMEND',outfits=[outfit(ids)])
                status,turn=send(chat(),'Quiero un outfit',d)
                check(status==200 and turn['recommendation'] is None,'Missing footwear is clarified before model can hallucinate')
                provider['mode']='wrong-dimension';before=len(model['calls'])
                check(send(chat(),'Quiero un outfit',d)[0]==503 and len(model['calls'])==before,'Embedding failure blocks generation before LLM with no full-inventory fallback')
            finally:
                provider['mode']='ok';wardrobe['mode']='empty'
                status,_=call('/api/v1/wardrobe-index/sync',{})
                check(status==200,'Clean reserved synthetic actor via authenticated empty sync')
    print('Generation RAG integration passed: bounded/category coverage, no-white memory, distinct alternatives, dark clarification, missing footwear, candidate whitelist, fail-closed embedding errors. Synthetic actor cleaned.')

if __name__=='__main__':main()
