"""RAG contract checks; --live-neon exercises the development DB with HTTP doubles.
Only reserved test actors from http_smoke are used; cleanup uses their own sync.
"""
import contextlib,http.server,json,sys,threading
from pathlib import Path
from http_smoke import check,iam_server,request,server
from closet_http import closet_server

@contextlib.contextmanager
def embeddings_server():
    state={'calls':[],'mode':'ok'}
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def do_POST(self):
            check(self.path=='/v1/embeddings','Fixed embedding route')
            check(self.headers.get('Authorization')=='Bearer embedding-test','No IAM token used at provider')
            body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            state['calls'].append(body)
            check(body['model']=='nvidia/nemotron-3.5-embed-1b' and body['dimensions']==1024,'Consistent embedding model and dimension')
            check(body['input_type'] in ('passage','query') and 'user_id' not in body,'No identity in embedding payload')
            data=[]
            for i,text in enumerate(body['input']):
                vector=[0.0]*1024;vector[0]=1.0;vector[1]=float('negro' in text.lower());vector[2]=float('blanco' in text.lower())
                if state['mode']=='zero':vector=[0.0]*1024
                if body['input_type']=='query' and state.get('fail_query_number')==sum(c['input_type']=='query' for c in state['calls']):vector=vector[:3]
                if state['mode']=='wrong-dimension':vector=vector[:3]
                data.append({'index':i,'embedding':vector})
            payload=json.dumps({'data':data}).encode();self.send_response(200)
            self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
    api=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);t=threading.Thread(target=api.serve_forever,daemon=True);t.start()
    try:yield f'http://127.0.0.1:{api.server_port}',state
    finally:api.shutdown();api.server_close();t.join(3)

def main():
    exe=sys.argv[1];prefix='/api/v1/wardrobe-index/'
    with iam_server() as (iam,_):
        with server(exe,'demo',iam) as base:
            check(request(base,prefix+'sync',{})[0]==401,'Index requires IAM')
            check(request(base,prefix+'sync',{'user_id':'forged'},token='valid-a')[0]==400,'Reject actor from caller')
            check(request(base,prefix+'search',{'query':'discreto','limit':11},token='valid-a')[0]==400,'Bound retrieval size')
            check(request(base,prefix+'search',{'query':' '},token='valid-a')[0]==400,'No empty semantic query')
            check(request(base,prefix+'sync',{},token='valid-a')[0]==503,'Disabled RAG is explicit')
    if '--live-neon' not in sys.argv:
        print('RAG HTTP contract: IAM, input guards and disabled mode passed. Live Neon check not requested.');return
    root=Path(__file__).resolve().parents[1];config={}
    for line in (root/'.env.rag-test').read_text(encoding='utf-8-sig').splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            k,v=line.split('=',1);config[k.strip()]=v.strip()
    check(config.get('NEON_VECTOR_BRANCH_ID')=='br-fancy-wildflower-bcxev7zf','Test only prepared development branch')
    with iam_server() as (iam,_),closet_server() as (closet,wardrobe),embeddings_server() as (embedding,provider):
        with server(exe,'live',iam,closet_url=closet,vector_url=config['NEON_VECTOR_DATABASE_URL'],embedding_url=embedding) as base:
            def call(path,body,token='valid-a'):return request(base,prefix+path,body,token=token,timeout=45)
            def generated():return sum(len(c['input']) for c in provider['calls'] if c['input_type']=='passage')
            try:
                status,result=call('sync',{})
                check(status==200 and result['indexed']==len(wardrobe['garments']),'Index complete snapshot, including unavailable metadata')
                before=generated();status,result=call('sync',{})
                check(status==200 and result['embeddings_generated']==0 and generated()==before,'Do not re-embed unchanged documents')
                status,result=call('search',{'query':'polo blanco','category':'TOP','color':'WHITE','limit':2})
                check(status==200 and result['items'] and all(g['category']=='TOP' and 'WHITE' in g['colors'] for g in result['items']),'Semantic retrieval respects exact filters')
                check(all('embedding' not in g and g['availability']=='AVAILABLE' for g in result['items']),'Return source metadata, not vectors')
                status,result=call('search',{'query':'polo blanco','category':'TOP','limit':2},'valid-b')
                check(status==200 and result['items'] and all(g['id'].startswith('30000000') for g in result['items']),'Actual database keeps user indexes isolated')
                wardrobe['mode']='page-down';check(call('sync',{})[0]==503,'Partial Closet read must not prune index');wardrobe['mode']='ok'
                before=generated();status,result=call('sync',{})
                check(status==200 and generated()==before,'Failed source read preserved all cached documents')
                wardrobe['garments'][0]['name']='Changed test top';wardrobe['garments'][0]['revision']+=1;provider['mode']='zero'
                check(call('sync',{})[0]==503,'Reject zero embeddings without committing changed document')
                provider['mode']='ok';status,result=call('sync',{})
                check(status==200 and result['embeddings_generated']==1,'Changed document re-embeds once')
                provider['mode']='wrong-dimension';check(call('search',{'query':'polo'})[0]==503,'Reject malformed query embeddings');provider['mode']='ok'
                wardrobe['garments'].pop(0);status,result=call('sync',{})
                check(status==200 and result['removed']==1,'Remove only missing own garment after complete source snapshot')
            finally:
                wardrobe['mode']='empty';provider['mode']='ok'
                for token in ['valid-a','valid-b']:
                    status,result=call('sync',{},token)
                    check(status==200 and result['indexed']==0,'Remove reserved synthetic actor records via authenticated sync')
    print('RAG integration passed: C++ -> Neon with mocked IAM/Closet/embeddings; tenant isolation, idempotence, partial-source and embedding failures, updates and pruning. Reserved test actors cleaned.')

if __name__=='__main__':main()
