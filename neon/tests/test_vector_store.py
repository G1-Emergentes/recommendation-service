"""Live development-store checks. Synthetic rows are always rolled back."""
import sys,uuid
from pathlib import Path
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'out/neon-validation'))
import psycopg

def main():
    config={}
    for line in (root/'.env.rag-test').read_text(encoding='utf-8-sig').splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            key,value=line.split('=',1);config[key.strip()]=value.strip()
    if config.get('NEON_VECTOR_BRANCH_ID')!='br-fancy-wildflower-bcxev7zf':
        raise RuntimeError('This check is restricted to the prepared development branch')
    a,b,g=uuid.uuid4(),uuid.uuid4(),uuid.uuid4()
    unit='['+','.join(['1']+['0']*1023)+']'
    other='['+','.join(['0','1']+['0']*1022)+']'
    zero='['+','.join(['0']*1024)+']'
    def check(value,message):
        if not value:raise RuntimeError(message)
    with psycopg.connect(config['NEON_VECTOR_DATABASE_URL'],autocommit=True,connect_timeout=15) as conn:
        role=conn.execute('select current_user,rolbypassrls,rolsuper from pg_roles where rolname=current_user').fetchone()
        check(role==('mirage_vector_app',False,False),'Application role must not bypass RLS')
        check(not conn.execute("select has_schema_privilege(current_user,'recommendation','CREATE')").fetchone()[0],'Application role must not own schema')
        conn.execute('begin')
        try:
            check(conn.execute('select count(*) from recommendation.wardrobe_embeddings').fetchone()[0]==0,'No actor must reveal no rows')
            insert='''insert into recommendation.wardrobe_embeddings
                (user_id,garment_id,revision,source_updated_at,name,description,category,availability,document,document_hash,embedding)
                values(%s,%s,1,now(),'Synthetic test','Rollback-only test','TOP','AVAILABLE','Test document',%s,%s::public.vector)'''
            conn.execute("select set_config('app.user_id',%s,true)",(str(a),))
            conn.execute(insert,(a,g,'a'*64,unit))
            conn.execute("select set_config('app.user_id',%s,true)",(str(b),))
            conn.execute(insert,(b,g,'b'*64,other))
            conn.execute("select set_config('app.user_id',%s,true)",(str(a),))
            rows=conn.execute('select user_id,embedding <=> %s::public.vector as distance from recommendation.wardrobe_embeddings order by distance',(other,)).fetchall()
            check(len(rows)==1 and rows[0][0]==a and abs(rows[0][1]-1)<0.00001,'RLS must hide foreign closer vectors without a WHERE clause')
            def rejects(sql,params,error):
                try:
                    with conn.transaction():conn.execute(sql,params)
                except error:return
                raise RuntimeError('Expected database constraint or policy rejection')
            rejects(insert,(b,uuid.uuid4(),'c'*64,unit),psycopg.errors.InsufficientPrivilege)
            rejects(insert,(a,g,'a'*64,unit),psycopg.errors.UniqueViolation)
            update='update recommendation.wardrobe_embeddings set '
            rejects(update+'revision=0',(),psycopg.errors.CheckViolation)
            rejects(update+"embedding_model='wrong-model'",(),psycopg.errors.CheckViolation)
            rejects(update+'embedding=%s::public.vector',('[1,0,0]',),psycopg.DataError)
            rejects(update+'embedding=%s::public.vector',(zero,),psycopg.errors.CheckViolation)
            rejects(update+"colors=array['NOT_A_COLOR']",(),psycopg.errors.CheckViolation)
            conn.execute("select set_config('app.user_id','',true)")
            check(conn.execute('select count(*) from recommendation.wardrobe_embeddings').fetchone()[0]==0,'Clearing actor must hide all rows')
        finally:
            conn.execute('rollback')
        check(conn.execute("select nullif(current_setting('app.user_id',true),'')").fetchone()[0] is None,'Actor context must not survive transaction end')
    print('PASS: restricted Neon connection, 1024-dimensional cosine search, tenant RLS, rejected cross-user writes, schema permissions and data guards. All synthetic rows rolled back.')

if __name__=='__main__':
    try:main()
    except Exception as error:
        print('FAIL:',type(error).__name__,'— database check failed; credentials omitted.',file=sys.stderr)
        sys.exit(1)
