"""Real C++ HTTP path, with controlled GitHub/NVIDIA/IAM/Closet/storage servers."""
import contextlib,copy,http.server,json,sys,threading,time,uuid
from http_smoke import check,iam_server,request,server
from conversation_http import chat_storage
from closet_http import closet_server
from nvidia_http import nvidia_server

REV='f834484cecaed20d08fea97be7781426063a7275'
PREFIX='/recsys-benchmark/FashionStylist/'+REV+'/Dataset/'
@contextlib.contextmanager
def github_source(mode='ok'):
    state={'mode':mode,'calls':[]}
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*_):pass
        def do_GET(self):
            state['calls'].append(self.path)
            check(not self.headers.get('Authorization') and not self.headers.get('Cookie'),'Never forward IAM or provider credentials')
            if self.path not in [PREFIX+'Female/look_en.csv',PREFIX+'Male/look_en.csv']:
                self.send_response(501);self.end_headers();return
            mode=state['mode']
            if mode=='timeout':time.sleep(1.4)
            status=302 if mode=='redirect' else 429 if mode=='rate-limit' else 200
            text='outfitID,items,look,season,occasion\n'
            if 'Female' in self.path:text+='b1,"p1,p2","Bohemian linen dress with soft layers",Summer,Social\n'
            else:text+='b1,"p1,p2","Quiet luxury black knit with wide trousers",Autumn/Winter,School\nb2,"p3,p4","Outdoor techwear cargo pants and black jacket",Autumn,Sports\n'
            if mode=='html':text='<html>provider changed</html>'
            if mode=='oversize':text='x'*262145
            data=text.encode();self.send_response(status)
            if mode=='redirect':self.send_header('Location','http://127.0.0.1:1/do-not-follow')
            self.send_header('Content-Length',str(len(data)));self.end_headers()
            try:self.wfile.write(data)
            except OSError:pass
    source=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=source.serve_forever,daemon=True);thread.start()
    try:yield f'http://127.0.0.1:{source.server_port}',state
    finally:source.shutdown();source.server_close();thread.join(timeout=3)

def function(args):
    return {'choices':[{'finish_reason':'tool_calls','message':{'content':None,'tool_calls':[{
        'type':'function','id':'web_'+uuid.uuid4().hex,'function':{'name':'search_fashion_web','arguments':json.dumps(args)}}]}}]}

def main():
    with iam_server() as (iam,_),chat_storage() as (storage,db),closet_server() as (closet,wardrobe),nvidia_server() as (nvidia,model),github_source() as (github,source):
        template=dict(action='SHOW_FASHION_WEB',assistant_message='Referencias consultadas.',outfits=[],excluded_colors=[],dark_only=False,reference_outfit=0,change_category='NONE',memory_update=dict(allowed_colors=[],relax_dark=False,style=None,occasion=None),preference_update=dict(operation='NONE',field='NONE',values=[]))
        with server(sys.argv[1],'live',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia,closet_url=closet,fashion_web_url=github) as base:
            def send(args=None,action='SHOW_FASHION_WEB',text='Busca inspiración web quiet luxury',outfits=None,excluded=None):
                status,chat=request(base,'/api/v1/conversations',{'title':'Fashion web tool'},token='valid-a');check(status==201,'Create own chat')
                model['tool_sequence']=[] if args is None else [function(args)]
                model['response']=dict(copy.deepcopy(template),action=action,outfits=outfits or [],excluded_colors=excluded or [])
                path='/api/v1/conversations/'+chat['id']+'/messages'
                body={'message':text,'client_message_id':str(uuid.uuid4())}
                before=len(model['calls']);status,turn=request(base,path,body,token='valid-a')
                return status,turn,model['calls'][before:],path,body
            status,_,calls,_,_=send(None,'CHAT','Hola')
            check(status==200 and not source['calls'],'No automatic web fetch on greeting')
            for args in [{'query':'quiet luxury','url':'https://attacker'},{'query':'quiet luxury','user_id':'foreign'},{'query':'https://attacker'},{'query':'quiet luxury','limit':4},{'query':' '},{'query':'quiet luxury','audience':'unknown'}]:
                status,_,calls,_,_=send(args,'CLARIFY')
                result=json.loads(calls[-1]['messages'][-1]['content'])
                check(status==200 and result['error']['code']=='INVALID_ARGUMENTS' and not source['calls'],'Reject bad args before outbound HTTP')
            status,turn,calls,path,body=send({'query':'quiet luxury','audience':'MALE','season':'WINTER','limit':1})
            check(status==200 and 'FashionStylist' in turn['assistant_message'] and 'CC-BY-NC-4.0' in turn['assistant_message'],'Show verified web sources, attribution and license')
            check(len(source['calls'])==2 and all(p.startswith(PREFIX) for p in source['calls']),'Only two fixed public CSV paths')
            result=json.loads(calls[-1]['messages'][-1]['content']);check(len(result['items'])==1 and result['items'][0]['audience']=='MALE','Audience and season filters')
            check('garments' not in result and result['source_revision']==REV,'External data never becomes inventory')
            before=len(model['calls']);status,replay=request(base,path,body,token='valid-a')
            check(status==200 and replay==turn and len(model['calls'])==before,'Same message replay avoids model and web calls')
            status,turn,calls,_,_=send({'query':'zzzznonexistent'})
            check(status==200 and 'No encontré' in turn['assistant_message'] and len(source['calls'])==2,'Valid empty results and shared cache')
            own=[g['id'] for g in wardrobe['garments'][:3]]
            outfit={'garment_ids':own,'explanation':'Inspiración externa adaptada a prendas propias.'}
            status,turn,calls,_,_=send({'query':'quiet luxury'},'RECOMMEND','Quiero un outfit con inspiración web',outfits=[outfit])
            check(status==200 and turn['recommendation']['web_references'] and turn['recommendation']['outfits'][0]['garment_ids']==own,'Tool is available before generating; persist web provenance')
            check(len(source['calls'])==2,'Repeated queries reuse bounded public cache')
            forged=dict(outfit,garment_ids=['fashionstylist-male-b1',*own[1:]])
            check(send({'query':'quiet luxury'},'RECOMMEND',outfits=[forged])[0]==502,'External IDs cannot become owned garments')
            check(send({'query':'quiet luxury'},'RECOMMEND','Quiero un outfit sin blanco con referencias web',outfits=[outfit],excluded=['WHITE'])[0]==502,'Web evidence cannot bypass color restrictions')
            check(send(None,'SHOW_FASHION_WEB')[0]==502,'No evidence means no claimed web lookup')
            check(all('valid-a' not in json.dumps(c) and 'sb_secret' not in json.dumps(c) for c in calls),'No credentials in model context')
        for mode in ['redirect','rate-limit','html','oversize','timeout']:
            with github_source(mode) as (github,source),server(sys.argv[1],'live',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia,closet_url=closet,fashion_web_url=github) as base:
                model['tool_sequence']=[function({'query':'quiet luxury'})];model['response']=dict(copy.deepcopy(template),action='CLARIFY',assistant_message='No pude consultar la fuente web; podemos usar las referencias locales.')
                status,chat=request(base,'/api/v1/conversations',{'title':'Provider unavailable'},token='valid-a')
                status,turn=request(base,'/api/v1/conversations/'+chat['id']+'/messages',{'message':'Busca inspiración web','client_message_id':str(uuid.uuid4())},token='valid-a')
                check(status==200 and 'No pude' in turn['assistant_message'],'Provider error is explicit without fabricated sources: '+mode)
                result=json.loads(model['calls'][-1]['messages'][-1]['content'])
                check(result['error']['code']=='EXTERNAL_SOURCE_UNAVAILABLE' and len(source['calls'])==1,'No redirects, oversized parsing or silent source fallback: '+mode)
    print('PASS: on-demand HTTP, cache, filters, auth isolation, idempotency, provenance, wardrobe/color guards and unavailable-source handling')

if __name__=='__main__':main()
