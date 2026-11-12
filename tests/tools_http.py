"""Function-call protocol and read-only guards through real C++ HTTP clients."""
import copy,json,sys,uuid
from datetime import datetime,timezone
from http_smoke import check,iam_server,request,server
from conversation_http import chat_storage
from closet_http import closet_server
from nvidia_http import nvidia_server

def main():
    with iam_server() as (iam,identity),chat_storage() as (storage,db),closet_server() as (closet,wardrobe),nvidia_server() as (nvidia,model):
        with server(sys.argv[1],'live',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia,closet_url=closet) as base:
            def create(token='valid-a'):
                status,c=request(base,'/api/v1/conversations',{'title':'Tools'},token=token)
                check(status==201,'Create tools chat');return c['id']
            chat=create();other=create('valid-b')
            template=dict(action='CLARIFY',assistant_message='Texto que no debe inventar prendas.',outfits=[],excluded_colors=[],dark_only=False,reference_outfit=0,change_category='NONE',memory_update=dict(allowed_colors=[],relax_dark=False,style=None,occasion=None),preference_update=dict(operation='NONE',field='NONE',values=[]))
            def function(name,args,call_id=None):
                return {'choices':[{'finish_reason':'tool_calls','message':{'content':None,'tool_calls':[{'type':'function','id':call_id or 'call_'+uuid.uuid4().hex,'function':{'name':name,'arguments':args if isinstance(args,str) else json.dumps(args)}}]}}]}
            def send(calls,action='CLARIFY',text='Busca prendas',token='valid-a',chat_id=chat):
                model['tool_sequence']=copy.deepcopy(calls)
                model['response']=dict(copy.deepcopy(template),action=action)
                before=len(model['calls'])
                status,turn=request(base,'/api/v1/conversations/'+chat_id+'/messages',{'message':text,'client_message_id':str(uuid.uuid4())},token=token)
                return status,turn,model['calls'][before:]
            filters=dict(query='',category='TOP',color='WHITE',style='CASUAL',limit=1)
            status,turn,calls=send([function('search_wardrobe',filters)],'SHOW_WARDROBE')
            check(status==200 and 'Polo blanco' in turn['assistant_message'] and turn['recommendation'] is None,'Search result renders real garment names')
            check(len(calls)==2 and {t['function']['name'] for t in calls[0]['tools']}=={'search_wardrobe','get_outfit','search_style_knowledge','search_fashion_web'},'Real function calling and model continuation')
            check('response_format' not in calls[0] and 'tools' not in calls[-1] and calls[-1]['response_format']['type']=='json_schema','Separate real tool calls from final JSON grammar')
            result=json.loads(calls[-1]['messages'][-1]['content'])
            check(calls[1]['messages'][-1]['role']=='tool' and result['ok'] and len(result['items'])==1 and not result['has_more'],'Correlate tool result with tool_call_id')
            check(all('20000000-' not in json.dumps(t) and 'valid-a' not in json.dumps(t) and 'sb_secret' not in json.dumps(t) for t in calls),'No identity or credentials in model requests')
            status,turn,calls=send([function('search_wardrobe',{'color':'WHITE','category':'TOP','limit':1})],'SHOW_WARDROBE')
            check(status==200 and 'Polo blanco' in turn['assistant_message'],'Omitted search filters receive safe defaults')
            status,_,calls=send([function('search_wardrobe',dict(filters,color='PURPLE'))],'SHOW_WARDROBE')
            check(status==200 and json.loads(calls[-1]['messages'][-1]['content'])['items']==[],'Empty search is a valid result')
            for bad in [dict(filters,user_id='forged'),dict(filters,url='http://attacker'),dict(filters,limit=11),dict(filters,limit=0),dict(filters,color='white'),dict(filters,query='x'*121),dict(filters,query='\x00')]:
                status,_,calls=send([function('search_wardrobe',bad)])
                check(status==200 and json.loads(calls[-1]['messages'][-1]['content'])['error']['code']=='INVALID_ARGUMENTS','Reject invalid search arguments')
            for args in [{'query':'minimalista','user_id':'forged'},{'query':'x','url':'https://attacker'},{'query':' '},{'query':'x'*513},{'query':'x','limit':4},{'query':'x','style':'unknown'}]:
                status,_,calls=send([function('search_style_knowledge',args)])
                check(status==200 and json.loads(calls[-1]['messages'][-1]['content'])['error']['code']=='INVALID_ARGUMENTS','Style tool enforces its allowlist and bounds')
            status,_,calls=send([function('delete_all',{})])
            check(status==200 and json.loads(calls[-1]['messages'][-1]['content'])['error']['code']=='UNKNOWN_TOOL','Allowlist excludes writes and arbitrary functions')
            # Use a fresh chat because the public API caps each conversation at 20 turns.
            chat=create()
            foreign=str(uuid.uuid4());own=str(uuid.uuid4());stamp=datetime.now(timezone.utc).isoformat()
            garment_ids=[g['id'] for g in wardrobe['garments'][:3]]
            record=dict(id=own,user_id='20000000-0000-4000-8000-000000000001',request_id=str(uuid.uuid4()),created_at=stamp,profile_preferences={},parameters={},persisted=True,garment_ids=garment_ids,garments=copy.deepcopy(wardrobe['garments'][:3]),explanation='Una opción guardada.',is_favorite=False,status='GENERATED')
            db['records'][own]=record;db['records'][foreign]=dict(record,id=foreign,user_id='20000000-0000-4000-8000-000000000002')
            status,turn,calls=send([function('get_outfit',{'outfit_id':own})],'SHOW_OUTFIT',chat_id=chat)
            detail=json.loads(calls[-1]['messages'][-1]['content'])
            check(status==200 and 'Polo blanco' in turn['assistant_message'] and 'histórica' in turn['assistant_message'] and detail['snapshot_only'],'Read exact owned historical outfit')
            template['outfits']=[{'garment_ids':['invented-id','another-id'],'explanation':'Invented copied outfit'}]
            status,read,_=send([function('get_outfit',{'outfit_id':own})],'SHOW_OUTFIT',chat_id=chat)
            check(status==200 and read['recommendation'] is None and 'invented' not in read['assistant_message'],'Verified read discards copied model outfits and renders only tool evidence')
            template['outfits']=[]
            for value in [foreign,str(uuid.uuid4())]:
                status,_,calls=send([function('get_outfit',{'outfit_id':value})],chat_id=chat)
                check(status==200 and json.loads(calls[-1]['messages'][-1]['content'])['error']['code']=='NOT_FOUND','Foreign and missing IDs have same response')
            before=len(db['lookups'])
            status,_,calls=send([function('get_outfit',{'outfit_id':'bad'})],chat_id=chat)
            check(status==200 and len(db['lookups'])==before,'Invalid UUID never reaches storage')
            for args in ['{"query":"","query":"duplicate"}','[1,2]','not-json']:
                check(send([function('search_wardrobe',args)],chat_id=chat)[0]==502,'Reject malformed JSON and duplicate argument keys')
            malformed=[{'choices':[None]}, {'choices':[{'finish_reason':'tool_calls','message':{'tool_calls':[[]]}}]}]
            bad=function('search_wardrobe',{});bad['choices'][0]['message']['tool_calls'][0]['function']=[];malformed.append(bad)
            for response in malformed:
                check(send([response],chat_id=chat)[0]==502,'Malformed tool envelopes must not crash the service')
            check(send([function('search_wardrobe','{"query":'+'['*80+'0'+']'*80+'}')],chat_id=chat)[0]==502,'Bound JSON nesting depth')
            check(send([],action='SHOW_WARDROBE',chat_id=chat)[0]==502,'Cannot claim search without successful execution')
            check(send([],action='SHOW_OUTFIT',chat_id=chat)[0]==502,'Cannot claim outfit lookup without successful execution')
            for args in ['x'*2049]:
                check(send([function('search_wardrobe',args)],chat_id=chat)[0]==502,'Bound tool argument size')
            parallel=function('search_wardrobe',filters)
            parallel['choices'][0]['message']['tool_calls'].append(copy.deepcopy(parallel['choices'][0]['message']['tool_calls'][0]))
            check(send([parallel],chat_id=chat)[0]==502,'Reject unexpected parallel call batch before execution')
            status,_,_=send([function('search_wardrobe',filters),function('search_wardrobe',filters)],chat_id=chat)
            check(status==502,'Repeated identical call blocked before execution')
            loop=[function('unsupported_'+str(i),{}) for i in range(4)]
            status,failed,calls=send(loop,chat_id=chat)
            check(status==502 and failed['error']['code']=='TOOL_LIMIT_EXCEEDED' and len(calls)==4,'Three calls maximum per message')
            before=len(db['chats'][chat]['turns'])
            for mode,expected in [('list-401',401),('list-503',503)]:
                model['on_tool']=lambda mode=mode:wardrobe.update(mode=mode)
                status,failed,calls=send([function('search_wardrobe',filters)],chat_id=chat)
                check(status==expected and len(calls)==1,'Fail closed on tool dependency errors')
                check(len(db['chats'][chat]['turns'])==before,'Tool failure does not save partial turn')
                wardrobe['mode']='ok';model.pop('on_tool')
            model['on_tool']=lambda:db.update(mode='down')
            status,failed,calls=send([function('get_outfit',{'outfit_id':own})],chat_id=chat)
            check(status==503 and failed['error']['code']=='TOOL_UNAVAILABLE','Surface outfit storage outage safely')
            db['mode']='ok';model.pop('on_tool')
            check(not identity['writes'],'Read-only tools never write Profile')
            status,_,calls=send([function('search_wardrobe',dict(filters,color='',category=''))],'SHOW_WARDROBE',token='valid-b',chat_id=other)
            result=json.loads(calls[-1]['messages'][-1]['content'])
            check(status==200 and all(g['id'].startswith('30000000') for g in result['items']),'Search runs with current user session')
            # Fail after initial source reads, during the actual tool execution.
            model['tool_sequence']=[function('get_outfit',{'outfit_id':own})];db['records'][own]['garments']=[]
            status,_,calls=send(model['tool_sequence'],chat_id=chat)
            check(status==200 and json.loads(calls[-1]['messages'][-1]['content'])['error']['code']=='SNAPSHOT_UNAVAILABLE','Do not fabricate missing snapshots')
            db['records'][own]['garments']=[[]]
            status,_,calls=send([function('get_outfit',{'outfit_id':own})],chat_id=chat)
            check(status==200 and json.loads(calls[-1]['messages'][-1]['content'])['error']['code']=='SNAPSHOT_UNAVAILABLE','Malformed snapshot entries do not crash the process')
    print('Tools HTTP: filters, ownership, protocol, evidence, arguments, limits, replay-safe reads and no writes passed')

if __name__=='__main__':main()
