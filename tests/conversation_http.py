"""Real C++ service with HTTP stand-ins. No real credentials or provider calls."""
import contextlib, copy, http.server, json, sys, threading, uuid
from datetime import datetime, timezone
from http_smoke import check, iam_server, request, server
from nvidia_http import nvidia_server


@contextlib.contextmanager
def chat_storage():
    state = {'chats': {}, 'mode': 'ok', 'records': {}, 'lookups': []}
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def do_POST(self):
            check(not self.headers.get('Authorization'), 'IAM token leaked to chat DB')
            check(self.headers.get('apikey') == 'sb_secret_test_backend', 'Missing backend credential')
            d=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            name=self.path.removeprefix('/rest/v1/rpc/mirage_chat_')
            user=d['p_user_id']; status=200
            if state['mode']=='down': status,result=503,{}
            elif self.path=='/rest/v1/rpc/mirage_get_recommendation':
                state['lookups'].append((user,d['p_id']))
                record=state['records'].get(d['p_id'])
                result=record if record and record['user_id']==user else None
            elif name=='create':
                result=dict(id=str(uuid.uuid4()),user_id=user,title=d['p_title'],version=0,created_at=datetime.now(timezone.utc).isoformat(),turns=[])
                state['chats'][result['id']]=result
                result={k:v for k,v in result.items() if k!='turns'}
            elif name=='list':
                result={'items':[{k:v for k,v in c.items() if k!='turns'} for c in state['chats'].values() if c['user_id']==user],'next_cursor':None}
            else:
                chat=state['chats'].get(d['p_id']); result=None
                if chat and chat['user_id']==user:
                    result=chat
                    if name=='append':
                        if state['mode']=='conflict': status,result=409,{}
                        else:
                            rec=d['p_snapshot']
                            if rec:
                                rec=copy.deepcopy(rec); rec.update(request_id=str(uuid.uuid4()),persisted=True,created_at=datetime.now(timezone.utc).isoformat())
                                for o in rec['outfits']:o.update(id=str(uuid.uuid4()),request_id=rec['request_id'])
                            result=dict(conversation_id=chat['id'],client_message_id=d['p_client_message_id'],position=chat['version']+1,message=d['p_message'],assistant_message=d['p_assistant_message'],recommendation=rec,memory=d.get('p_memory',{}),created_at=datetime.now(timezone.utc).isoformat())
                            chat['turns'].append(result);chat['version']+=1
            data=json.dumps(result).encode();self.send_response(status)
            self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    upstream=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=upstream.serve_forever,daemon=True);thread.start()
    try:yield f'http://127.0.0.1:{upstream.server_port}',state
    finally:upstream.shutdown();upstream.server_close();thread.join(3)


def main():
    exe=sys.argv[1]; path='/api/v1/conversations'
    with iam_server() as (iam,_), chat_storage() as (storage,db), nvidia_server() as (nvidia,model):
        with server(exe,'demo',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia) as base:
            check(request(base,path,{'title':'Clases'})[0]==401,'Chat must require IAM')
            check(request(base,path,{'title':' '*4},token='valid-a')[0]==400,'Reject blank title')
            status,chat=request(base,path,{'title':'Clases'},token='valid-a')
            check(status==201,'Create chat')
            detail=path+'/'+chat['id']; send=detail+'/messages'
            for bad in ({},{'message':'Hola','client_message_id':'bad'},{'message':' '*4,'client_message_id':str(uuid.uuid4())},{'message':'x'*2001,'client_message_id':str(uuid.uuid4())},{'message':'Hola','client_message_id':str(uuid.uuid4()),'user_id':'forged'}):
                check(request(base,send,bad,token='valid-a')[0]==400,'Reject invalid message')
            def message(text):return {'message':text,'client_message_id':str(uuid.uuid4())}
            greeting=message('Hola')
            status,first=request(base,send,greeting,token='valid-a')
            check(status==200 and first['recommendation'] is None and first['assistant_message'],'Text without forced outfits')
            body=message('Quiero algo para la universidad')
            status,turn=request(base,send,body,token='valid-a')
            check(status==200 and turn['recommendation']['outfits'],'Chat generates persistent outfits')
            calls=len(model['calls'])
            check(request(base,send,body,token='valid-a')==(200,turn) and len(model['calls'])==calls,'Replay without another model call')
            check(request(base,send,dict(body,message='changed'),token='valid-a')[0]==409,'Reject message ID reuse')
            check(request(base,detail,token='valid-b')[0]==404,'Foreign chat hidden')
            check(request(base,send,message('Hola'),token='valid-b')[0]==404,'Foreign message blocked')
            check(len(model['calls'])==calls,'Foreign user never reaches model')
            status,third=request(base,send,message('Cambia los zapatos de la primera opcion'),token='valid-a')
            check(status==200,'Follow-up response')
            context=json.loads(model['calls'][-1]['messages'][1]['content'])
            messages=model['calls'][-1]['messages']
            check(len(messages)==7 and messages[2]['role']=='user' and messages[2]['content']=='Hola' and
                  json.loads(messages[5]['content'])['outfits'][0]['garment_ids']==turn['recommendation']['outfits'][0]['garment_ids'],
                  'Prior messages and outfit order reach model as actual chat roles')
            check('access_token' not in context and 'user_id' not in context and 'private@example.test' not in json.dumps(context),'No account details in model context')
            status,explained=request(base,send,message('Explica la primera opcion'),token='valid-a')
            check(status==200 and explained['recommendation'] is None and 'Polo blanco' in explained['assistant_message'],
                  'Explain a saved option from its structured garments, without generating new outfits')
            for mode in ('empty-chat','unknown-id','http503','timeout','prose-only','chat-with-outfits',
                         'invalid-action','excluded-white','dark-violation','bad-reference','modify-no-change'):
                model['mode']=mode
                before=len(db['chats'][chat['id']]['turns'])
                check(request(base,send,message('Otra opcion'),token='valid-a')[0] in (502,503),'Handle bad model response '+mode)
                check(len(db['chats'][chat['id']]['turns'])==before,'No partial turn on failure')
            model['mode']='valid';db['mode']='conflict'
            check(request(base,send,message('Quiero repetir el outfit'),token='valid-a')[0]==409,'Concurrent turn conflict surfaced')
            db['mode']='down'
            check(request(base,detail,token='valid-a')[0]==503,'Storage outage surfaced')
            db['mode']='ok'
            db['chats'][chat['id']]['version']=20
            check(request(base,send,message('Otro'),token='valid-a')[0]==409,'Context limit explicit')
            db['chats'][chat['id']]['version']=4
            check(len(request(base,path,token='valid-a')[1]['items'])==1,'Own chat list')
            check(request(base,path,token='valid-b')[1]['items']==[],'Foreign chat list empty')
        # Restart C++ service against the same persistence: context must survive.
        with server(exe,'demo',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia) as base:
            check(len(request(base,detail,token='valid-a')[1]['turns'])==4,'Conversation survives process restart')
    print('Conversation HTTP: ownership, replay, ordered memory, validation, provider failures and restart passed')

if __name__=='__main__':main()
