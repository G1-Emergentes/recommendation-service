"""Exercise durable chat state and IAM preference writes through real HTTP clients."""
import copy,json,sys,uuid
from http_smoke import check,iam_server,request,server
from conversation_http import chat_storage
from nvidia_http import nvidia_server

def main():
    with iam_server() as (iam,profile),chat_storage() as (storage,db),nvidia_server() as (nvidia,model):
        with server(sys.argv[1],'demo',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia) as base:
            def create(token='valid-a'):
                status,chat=request(base,'/api/v1/conversations',{'title':'Memory'},token=token)
                check(status==201,'Create memory chat')
                return '/api/v1/conversations/'+chat['id']+'/messages'
            path=create()
            template=dict(action='CLARIFY',assistant_message='Una pregunta.',outfits=[],excluded_colors=[],dark_only=False,
                          reference_outfit=0,change_category='NONE',
                          memory_update=dict(allowed_colors=[],relax_dark=False,style=None,occasion=None),
                          preference_update=dict(operation='NONE',field='NONE',values=[]))
            def send(text,payload=None,path=path,token='valid-a',body=None):
                model['response']=copy.deepcopy(payload or template)
                return request(base,path,body or {'message':text,'client_message_id':str(uuid.uuid4())},token=token)
            d=copy.deepcopy(template);d.update(action='REDIRECT',assistant_message='La capital de Francia es Paris.')
            status,turn=send('Cual es la capital de Francia?',d)
            check(status==200 and 'capital' not in turn['assistant_message'] and turn['recommendation'] is None,'Brief deterministic redirection')
            d=copy.deepcopy(template);d['excluded_colors']=['WHITE'];d['memory_update'].update(style='CASUAL',occasion='Universidad')
            status,turn=send('Para universidad, casual, sin blanco',d)
            check(status==200 and turn['memory']['excluded_colors']==['WHITE'],'Persist exclusions without requiring outfits')
            check(not profile['writes'],'Temporary requests must not update Profile')
            status,turn=send('Otra opcion')
            memory=turn['memory']
            check(status==200 and memory['excluded_colors']==['WHITE'] and memory['style']=='CASUAL' and memory['occasion']=='Universidad','Omitted constraints persist')
            context=json.loads(model['calls'][-1]['messages'][1]['content'])
            check(context['conversation_memory']['excluded_colors']==['WHITE'],'Explicit state reaches the model')
            d=copy.deepcopy(template);d['memory_update'].update(style='',occasion='')
            status,turn=send('Otra opcion',d)
            check(status==200 and turn['memory']['style']=='CASUAL' and turn['memory']['occasion']=='Universidad','Empty model defaults cannot silently clear style or occasion')
            d=copy.deepcopy(template);d['memory_update']['allowed_colors']=['WHITE']
            check(send('Otra opcion',d)[0]==502,'Reject unprompted removal of a restriction')
            d=copy.deepcopy(template);d.update(action='RECOMMEND',outfits=[dict(garment_ids=['10000000-0000-4000-8000-000000000001','10000000-0000-4000-8000-000000000002','10000000-0000-4000-8000-000000000003'],explanation='Oops')])
            check(send('Otra opcion',d)[0]==502,'Backend rejects forgotten exclusion, even if model omits it')
            d['memory_update']['allowed_colors']=['WHITE']
            status,turn=send('Ya puedes usar blanco',d)
            check(status==200 and turn['memory']['excluded_colors']==[],'Explicit release permits previously excluded colors')
            d=copy.deepcopy(template);d['dark_only']=True
            check(send('Todo oscuro',d)[1]['memory']['dark_only'],'Activate dark restriction')
            check(send('Otra opcion')[1]['memory']['dark_only'],'False omitted activation cannot reset dark restriction')
            d=copy.deepcopy(template);d['memory_update']['relax_dark']=True
            check(not send('Acepto una aproximacion con azul',d)[1]['memory']['dark_only'],'Explicitly relax dark restriction')
            d=copy.deepcopy(template);d['action']='PREFERENCES_UPDATE';d['preference_update']=dict(operation='ADD',field='preferred_styles',values=['MINIMALIST'])
            check(send('Hoy quiero un outfit minimalista',d)[0]==502 and not profile['writes'],'No permanent write without explicit save wording')
            body={'message':'Recuerda que prefiero estilo minimalista','client_message_id':str(uuid.uuid4())}
            status,turn=send(body['message'],d,body=body)
            check(status==200 and turn['memory']['profile_updated'] and 'MINIMALIST' in turn['assistant_message'],'Confirm only after successful Profile response')
            writes=len(profile['writes'])
            check(send(body['message'],d,body=body)==(200,turn) and len(profile['writes'])==writes,'Replay never repeats Profile write')
            new_path=create()
            d=copy.deepcopy(template);d['action']='PREFERENCES_READ'
            d['excluded_colors']=['WHITE']
            status,turn=send('Que gustos tienes guardados?',d,path=new_path)
            check(status==200 and 'MINIMALIST' in turn['assistant_message'] and turn['memory']['excluded_colors']==[],'Profile persists across chats; temporary constraints do not')
            status,other=send('Que gustos tienes guardados?',d,path=create('valid-b'),token='valid-b')
            check(status==200 and 'MINIMALIST' not in other['assistant_message'],'Other user preferences stay isolated')
            d=copy.deepcopy(template);d['action']='PREFERENCES_UPDATE';d['preference_update']=dict(operation='REMOVE',field='preferred_styles',values=['MINIMALIST'])
            check(send('Olvida que prefiero estilo minimalista',d,path=new_path)[0]==200 and profile['preferences']['valid-a']['preferredStyles']==[],'Forget a permanent preference')
            profile['write_status']=503
            status,failed=send('Recuerda que prefiero estilo minimalista',d,path=new_path)
            check(status==503 and failed['error']['code']=='PROFILE_UPDATE_UNCONFIRMED','Do not claim write success on upstream failure')
            profile['write_status']=200;db['mode']='conflict'
            status,failed=send('Olvida que prefiero estilo minimalista',d,path=new_path)
            check(status==409 and failed['error']['code']=='PROFILE_UPDATED_CHAT_NOT_SAVED','Report Profile side effect if chat commit fails')
            db['mode']='ok'
        # State is fetched from storage after restart, not retained in the process.
        with server(sys.argv[1],'demo',iam,storage_url=storage,engine='nemotron',nvidia_url=nvidia) as base:
            status,turn=send('Otra opcion')
            check(status==200 and turn['memory']['style']=='CASUAL','Chat state survives restart')
    print('Memory HTTP: redirection, constraints, release, permanent preferences, ownership, replay and partial failures passed')

if __name__=='__main__':main()
