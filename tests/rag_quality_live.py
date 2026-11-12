"""Real NVIDIA + Neon + IAM + Closet quality evaluation; chat persistence is local HTTP double."""
import http.server,json,os,socket,subprocess,sys,threading,time,urllib.request,urllib.error,uuid
from pathlib import Path
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tests'))
from conversation_http import chat_storage
values={}
for line in (root/'.env').read_text(encoding='utf-8-sig').splitlines():
    if '=' in line and not line.lstrip().startswith('#'):
        k,v=line.split('=',1);values[k.strip()]=v.strip()
env=json.loads((root/'out/postman/Mirage-Local.private.postman_environment.json').read_text(encoding='utf-8-sig'))
token=next(x['value'] for x in env['values'] if x['key']=='access_token')
file=root/'out/rag-generation-quality.json';report={'checks':[],'model_calls':[],'storage':'local HTTP double; no real conversations created'}
def save():file.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
class Relay(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_POST(self):
        body=self.rfile.read(int(self.headers['Content-Length']));sent=json.loads(body)
        req=urllib.request.Request('https://integrate.api.nvidia.com/v1/chat/completions',data=body,headers={'Content-Type':'application/json','Authorization':self.headers['Authorization']})
        try:r=urllib.request.urlopen(req,timeout=65)
        except urllib.error.HTTPError as e:r=e
        with r:status=r.status;data=r.read()
        parsed=json.loads(data);choice=(parsed.get('choices') or [{}])[0];message=choice.get('message',{})
        context=json.loads(sent['messages'][1]['content'])
        report['model_calls'].append({'status':status,'candidate_ids':[g['id'] for g in context.get('garments',[])], 'finish_reason':choice.get('finish_reason'),'tool_calls':message.get('tool_calls',[]),'content':message.get('content')});save()
        self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
relay=http.server.ThreadingHTTPServer(('127.0.0.1',0),Relay);threading.Thread(target=relay.serve_forever,daemon=True).start()
with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
def call(port,path,body=None):
    req=urllib.request.Request(f'http://127.0.0.1:{port}'+path,data=None if body is None else json.dumps(body).encode(),headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'})
    try:r=urllib.request.urlopen(req,timeout=140)
    except urllib.error.HTTPError as e:r=e
    with r:return r.status,json.load(r)
def sets(turn):return {frozenset(o['garment_ids']) for o in (turn.get('recommendation') or {}).get('outfits',[])}
with chat_storage() as (storage,_):
    config=dict(os.environ,**values);config.update(RECOMMENDATION_PORT=str(port),NVIDIA_API_ORIGIN=f'http://127.0.0.1:{relay.server_port}',RECOMMENDATION_SUPABASE_URL=storage,RECOMMENDATION_SUPABASE_SECRET_KEY='sb_secret_test_backend',NVIDIA_TIMEOUT_SECONDS='60')
    proc=subprocess.Popen([str(root/'out/build/x64-debug/recommendation-service.exe')],env=config,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=1):break
            except OSError:time.sleep(.1)
        status,user=call(8080,'/api/v1/auth/me');assert status==200,'Expected valid local test session'
        status,closet=call(8081,'/api/v1/garments?limit=100');assert status==200
        garments={g['id']:g for g in closet['items']}
        status,chat=call(port,'/api/v1/conversations',{'title':'Local real-model RAG evaluation'});assert status==201
        path='/api/v1/conversations/'+chat['id']+'/messages'
        def send(label,text):
            start=time.monotonic();status,turn=call(port,path,{'message':text,'client_message_id':str(uuid.uuid4())})
            report['checks'].append({'label':label,'status':status,'seconds':round(time.monotonic()-start,2),'response':turn});save()
            print(label,status,turn.get('assistant_message',turn.get('error')),flush=True);assert status==200,label
            latest=next(c for c in reversed(report['model_calls']) if c['candidate_ids'])
            for outfit in sets(turn):assert outfit<=set(latest['candidate_ids']),'Only retrieved candidates'
            for outfit in sets(turn):
                for gid in outfit:assert 'WHITE' not in garments[gid]['colors'],'No white'
            return turn
        first=send('no-white','Quiero un outfit casual para salir, sin blanco. Dame una sola opcion.')
        assert sets(first),'Expected outfit without white'
        second=send('another-option','Otra opcion, manteniendo sin blanco. Si no hay una distinta, dimelo.')
        assert sets(second) and sets(second).isdisjoint(sets(first)),'Expected distinct available alternative'
        dark=send('dark-and-striking','Ahora quiero algo oscuro pero llamativo. Manten sin blanco.')
        assert not sets(dark) and dark['memory']['dark_only'],'No confirmed dark trousers: clarify'
        again=send('dark-memory','Otra opcion')
        assert not sets(again) and again['memory']['dark_only'],'Persist strict dark constraint'
        relaxed=send('explicit-relaxation','Acepto usar el jean azul; puedes relajar que sea todo oscuro. Manten sin blanco ni beige.')
        assert len(sets(relaxed))==1 and not relaxed['memory']['dark_only'] and set(relaxed['memory']['excluded_colors'])=={'WHITE','BEIGE'},'Exactly one remaining combination with both exclusions'
        exhausted=send('no-more-alternatives','Otra opcion')
        assert not sets(exhausted),'Never repeat when only one combination remains'
        report['passed']=True;save();print('PASS: 6 real-model RAG quality cases; actual Closet/NVIDIA/Neon, local chat storage.',flush=True)
    finally:
        proc.terminate();proc.wait(10);relay.shutdown();relay.server_close()
