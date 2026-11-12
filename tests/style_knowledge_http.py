"""Style reference HTTP contract. --live-neon uses actual dev DB with controlled HTTP providers."""
import copy,json,sys,uuid
from pathlib import Path
from http_smoke import check,iam_server,request,server
from closet_http import closet_server
from conversation_http import chat_storage
from nvidia_http import nvidia_server
from rag_http import embeddings_server

def main():
    exe=sys.argv[1];endpoint='/api/v1/style-knowledge/search'
    with iam_server() as (iam,_):
        with server(exe,'demo',iam) as base:
            check(request(base,endpoint,{'query':'minimalista'})[0]==401,'Style knowledge requires IAM')
            for body in [{'query':''},{'query':' '},{'query':'\0'},{'query':'x'*513},{'query':'é'*257},{'query':'x','limit':4},{'query':'x','limit':0},{'query':'x','user_id':'forged'},{'query':'x','url':'https://attacker'},{'query':'x','style':'unknown'}]:
                check(request(base,endpoint,body,token='valid-a')[0]==400,'Reject malformed or privileged style arguments')
            check(request(base,endpoint,{'query':'minimalista'},token='valid-a')[0]==503,'Disabled knowledge is explicit')
    if '--live-neon' not in sys.argv:
        print('Style knowledge HTTP contract passed: IAM, input bounds, actor/URL allowlist, disabled mode.');return
    root=Path(__file__).resolve().parents[1];config={}
    for line in (root/'.env.rag-test').read_text(encoding='utf-8-sig').splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            k,v=line.split('=',1);config[k.strip()]=v.strip()
    check(config.get('NEON_VECTOR_BRANCH_ID')=='br-fancy-wildflower-bcxev7zf','Only prepared development branch')
    with iam_server() as (iam,_),closet_server() as (closet,wardrobe),chat_storage() as (storage,db),nvidia_server() as (nvidia,model),embeddings_server() as (embedding,provider):
        with server(exe,'live',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia,closet_url=closet,vector_url=config['NEON_VECTOR_DATABASE_URL'],embedding_url=embedding) as base:
            def call(path,body,token='valid-a'):return request(base,path,body,token=token,timeout=65)
            try:
                status,result=call(endpoint,{'query':'paleta monocromatica','limit':3})
                check(status==200 and 1<=len(result['items'])<=3,'Retrieve bounded published references from actual Neon')
                check(all(r['license']=='CC-BY-SA-4.0' and r['source_url'].startswith('https://en.wikipedia.org/wiki/') and r['attribution'] and 'embedding' not in r for r in result['items']),'Return licensed source metadata without vectors')
                status,other=call(endpoint,{'query':'paleta monocromatica','limit':3},'valid-b')
                check(status==200 and {r['id'] for r in other['items']}=={r['id'] for r in result['items']},'Published guidance is shared; wardrobe remains private')
                status,chat=call('/api/v1/conversations',{'title':'Synthetic style-knowledge evaluation'});check(status==201,'Create synthetic chat')
                path='/api/v1/conversations/'+chat['id']+'/messages'
                template=dict(action='CLARIFY',assistant_message='Una pregunta.',outfits=[],excluded_colors=[],dark_only=False,reference_outfit=0,change_category='NONE',memory_update=dict(allowed_colors=[],relax_dark=False,style=None,occasion=None),preference_update=dict(operation='NONE',field='NONE',values=[]))
                def send(text):return call(path,{'message':text,'client_message_id':str(uuid.uuid4())})
                model['response']=copy.deepcopy(template);model['response'].update(action='RECOMMEND',outfits=[dict(garment_ids=[g['id'] for g in wardrobe['garments'][:3]],explanation='Combination from verified garments.')])
                status,turn=send('Quiero un outfit casual')
                check(status==200 and turn['recommendation'],'Generation with mandatory style preparation')
                context=json.loads(model['calls'][-1]['messages'][1]['content'])
                check(1<=len(context['style_knowledge'])<=3 and context['garments'],'Final model context receives separate references and real candidates')
                check(turn['recommendation']['style_references']==context['style_knowledge'],'Persist retrieved provenance in JSON context')
                model['response']=copy.deepcopy(template);model['response'].update(action='SHOW_STYLE_KNOWLEDGE',outfits=[dict(garment_ids=['forged'],explanation='must be discarded')],excluded_colors=['BEIGE'])
                calls_before=len(model['calls'])
                model['tool_sequence']=[{'choices':[{'finish_reason':'tool_calls','message':{'content':None,'tool_calls':[{'type':'function','id':'call_style_read','function':{'name':'search_style_knowledge','arguments':json.dumps({'query':'criterios de minimalismo','style':'MINIMALIST','limit':2})}}]}}]}]
                status,turn=send('Que criterios definen un estilo minimalista?')
                check(status==200 and turn['recommendation'] is None and 'https://en.wikipedia.org/wiki/' in turn['assistant_message'] and 'CC-BY-SA-4.0' in turn['assistant_message'],'Style tool replies with verified sources, without creating outfits')
                check(turn['memory']['excluded_colors']==[],'Read-only knowledge never changes restrictions from model output')
                check(len(model['calls'])==calls_before+1 and any(t['function']['name']=='search_style_knowledge' for t in model['calls'][-1]['tools']),'Actual tool invocation, verified response rendered without a second model pass')
                check('20000000-' not in json.dumps(context) and 'valid-a' not in json.dumps(context),'No account identity in model context')
                provider['mode']='wrong-dimension';before=len(model['calls']);source_calls=len(wardrobe['calls'])
                model['response']=copy.deepcopy(template)
                status,_=send('Quiero un outfit nuevo')
                check(status==503 and len(model['calls'])==before and len(wardrobe['calls'])==source_calls,'Knowledge embedding failure stops before wardrobe and LLM')
            finally:
                provider['mode']='ok';wardrobe['mode']='empty'
                for token in ['valid-a','valid-b']:
                    status,_=call('/api/v1/wardrobe-index/sync',{},token);check(status==200,'Clean only reserved synthetic wardrobe actors')
    print('Style knowledge integration passed: shared licensed retrieval, mandatory context before wardrobe, provenance persistence, actual guarded tool execution, no outfits/memory writes on reads, dependency failure. Reserved actors cleaned.')
if __name__=='__main__':main()
