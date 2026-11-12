"""C++ live-mode client against HTTP contract doubles; no real account/provider keys."""
import contextlib, copy, http.server, json, sys, threading, time, uuid
from pathlib import Path
from urllib.parse import urlparse, parse_qs
from http_smoke import check, history_server, iam_server, request, server
from nvidia_http import nvidia_server
from conversation_http import chat_storage


@contextlib.contextmanager
def closet_server():
    garments=json.loads((Path(__file__).resolve().parents[1]/'fixtures/garments.json').read_text(encoding='utf-8'))['items']
    garments[0]['image']={'id':str(uuid.uuid4()),'version':2}
    state={'mode':'ok','calls':[],'resolves':0,'garments':garments}
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def reply(self,status,value):
            data=json.dumps(value).encode();self.send_response(status)
            self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers()
            try:self.wfile.write(data)
            except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
        def owned(self):
            token=self.headers.get('Authorization')
            check(token in ('Bearer valid-a','Bearer valid-b'),'Closet must receive IAM token')
            check(not self.headers.get('apikey'),'Do not send provider keys to Closet')
            rows=copy.deepcopy(state['garments'])
            if token=='Bearer valid-b':
                for g in rows:g['id']=g['id'].replace('10000000','30000000')
            return rows
        def do_GET(self):
            parsed=urlparse(self.path);query=parse_qs(parsed.query)
            check(parsed.path=='/api/v1/garments','Unexpected Closet list route')
            check(set(query)<={'limit','cursor'} and query['limit']==['100'],'Only contract query parameters')
            state['calls'].append(('list',self.headers.get('Authorization')))
            cursor=query.get('cursor',[''])[0];rows=self.owned();mode=state['mode']
            if mode=='empty':rows=[]
            if mode=='large':
                rows=[dict(rows[0],id=str(uuid.uuid4())) for _ in range(101)]
            if mode in ('list-401','list-503'):
                self.reply(int(mode.split('-')[1]),{});return
            if mode=='page-down' and cursor:self.reply(503,{});return
            if mode=='timeout':time.sleep(1.5)
            if mode=='bad-json':
                self.send_response(200);self.end_headers();self.wfile.write(b'not json');return
            if mode=='redirect':self.send_response(302);self.send_header('Location','/must-not-follow');self.end_headers();return
            offset=int(cursor.removeprefix('page:') or '0')
            page=rows[offset:offset+2]
            next_cursor=f'page:{offset+2}' if offset+2<len(rows) else None
            if mode=='repeat-cursor':next_cursor='page:0'
            if page:
                if mode=='missing-revision':page[0].pop('revision')
                if mode=='invalid-revision':page[0]['revision']=0
                if mode=='invalid-image':page[0]['image']={'id':str(uuid.uuid4()),'version':0}
                if mode=='long-name':page[0]['name']='x'*121
                if mode=='duplicate':page.append(copy.deepcopy(page[0]))
            self.reply(200,{'items':page,'next_cursor':next_cursor})
        def do_POST(self):
            check(self.path=='/api/v1/garments/resolve','Unexpected Closet resolve route')
            body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            check(set(body)=={'ids'} and 1<=len(body['ids'])<=100,'Resolve body is only IDs')
            state['resolves']+=1;state['calls'].append(('resolve',self.headers.get('Authorization')))
            rows={g['id']:g for g in self.owned()};mode=state['mode'];n=state['resolves']
            if mode=='resolve-401' or (mode=='post-401' and n==2):self.reply(401,{});return
            if mode=='post-down' and n==2:self.reply(503,{});return
            entries=[{'id':gid,'status':'AVAILABLE','garment':rows[gid]} for gid in body['ids']]
            if (mode=='pre-change' and n==1) or (mode=='post-change' and n==2):entries[0]['garment']['revision']+=1
            if mode=='image-change' and n==2:entries[0]['garment']['image']={'id':str(uuid.uuid4()),'version':9}
            if mode in ('post-unavailable','post-deleted') and n==2:
                entries[0].update(status='UNAVAILABLE' if mode=='post-unavailable' else 'NOT_FOUND',garment=None)
            if mode=='missing-result':entries.pop()
            if mode=='wrong-order':entries=list(reversed(entries))
            self.reply(200,{'results':entries})
    upstream=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=upstream.serve_forever,daemon=True);thread.start()
    try:yield f'http://127.0.0.1:{upstream.server_port}',state
    finally:upstream.shutdown();upstream.server_close();thread.join(3)


def main():
    exe=sys.argv[1];endpoint='/api/v1/recommendations';body={'occasion':'Universidad','style':'CASUAL','max_results':2}
    with iam_server() as (iam,_),closet_server() as (closet,wardrobe),history_server() as (storage,db),nvidia_server() as (nvidia,model):
        with server(exe,'live',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia,closet_url=closet) as base:
            check(request(base,'/health')[1]['wardrobe_source']=='digital_closet','Report real source')
            status,result=request(base,endpoint,body,token='valid-a')
            check(status==200 and result['mode']=='live' and result['persisted'],'Persist real-source output')
            check(wardrobe['resolves']==2 and sum(c[0]=='list' for c in wardrobe['calls'])>1,'Complete pagination and two-phase resolve')
            selected={g['id']:g for g in result['garments']}
            check(set(selected)=={gid for o in result['outfits'] for gid in o['garment_ids']},'Snapshot only referenced garments')
            top=wardrobe['garments'][0]
            check(selected[top['id']]==top,'Preserve full source metadata, including image version')
            record=request(base,endpoint+'/'+result['outfits'][0]['id'],token='valid-a')[1]
            check(record['garments']==result['garments'],'Metadata survives persistence and detail read')
            sent=json.loads(model['calls'][-1]['messages'][1]['content'])
            check('subcategory' in sent['garments'][0] and not any(k in sent for k in ('user_id','access_token')),'Semantic context excludes credentials')
            check(request(base,endpoint,body,token='valid-b')[0]==200,'Second user supported')
            check(all(c[1]=='Bearer valid-b' for c in wardrobe['calls'][-2:]),'Resolve uses the same user token')
            modes={'list-401':401,'resolve-401':401,'post-401':401,'list-503':503,'page-down':503,
                   'timeout':503,'bad-json':503,'redirect':503,'repeat-cursor':503,'missing-revision':503,
                   'invalid-revision':503,'invalid-image':503,'long-name':503,'duplicate':503,
                   'pre-change':409,'post-change':409,'image-change':409,'post-unavailable':409,'post-deleted':409,
                   'post-down':503,'missing-result':503,'wrong-order':503,'large':422}
            for mode,expected in modes.items():
                wardrobe.update(mode=mode,resolves=0)
                before=len(db['records'])
                status,error=request(base,endpoint,body,token='valid-a')
                check(status==expected,f'{mode}: expected {expected}, got {status}: {error}')
                check(len(db['records'])==before,'Never persist incomplete/stale response: '+mode)
                if expected==409:check(error['error']['code']=='WARDROBE_CHANGED','Explicit stale-wardrobe error')
            wardrobe.update(mode='empty',resolves=0)
            status,empty=request(base,endpoint,body,token='valid-a')
            check(status==200 and empty['status']=='INSUFFICIENT_GARMENTS' and empty['garments']==[],'Empty wardrobe is not a failed dependency')
        wardrobe.update(mode='ok',resolves=0)
        # Rules engine also revalidates, not just model-generated combinations.
        with server(exe,'live',iam,storage_url=storage,closet_url=closet) as base:
            wardrobe['mode']='post-change'
            check(request(base,endpoint,body,token='valid-a')[0]==409,'Rules engine also rejects stale garments')
        with chat_storage() as (chat_url,chats):
            wardrobe.update(mode='ok',resolves=0)
            with server(exe,'live',iam,storage_url=chat_url,engine='nemotron',nvidia_url=nvidia,closet_url=closet) as base:
                status,chat=request(base,'/api/v1/conversations',{},token='valid-a')
                check(status==201,'Create live chat')
                send='/api/v1/conversations/'+chat['id']+'/messages'
                msg={'message':'Quiero un outfit','client_message_id':str(uuid.uuid4())}
                wardrobe.update(mode='post-change',resolves=0)
                check(request(base,send,msg,token='valid-a')[0]==409,'Chat rejects stale recommendation')
                check(chats['chats'][chat['id']]['turns']==[],'Chat must not save a partial user/assistant turn')
                wardrobe.update(mode='ok',resolves=0)
                status,turn=request(base,send,msg,token='valid-a')
                check(status==200 and turn['recommendation']['garments'],'Live chat persists garment metadata')
                check(turn['recommendation']['mode']=='live','Chat source provenance')
                wardrobe.update(mode='empty',resolves=0)
                status,greeting=request(base,send,dict(msg,message='Hola',client_message_id=str(uuid.uuid4())),token='valid-a')
                check(status==200 and greeting['recommendation'] is None,'Can converse with an empty real closet')
    print('Closet HTTP: pagination, identity, metadata, stale data, failures, empty wardrobes and chat passed')

if __name__=='__main__':main()
