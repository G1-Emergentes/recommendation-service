"""Pinned Polyvore metadata importer. No images, arbitrary URLs, or database writes.
Default prepares 128 train examples and a separate 24-example validation holdout.
--embed generates/reuses NVIDIA embeddings under ignored out/. --emit-row emits one prepared row for administrative import.
"""
import argparse,hashlib,html,json,math,re,tarfile,urllib.request
from collections import Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
COMMIT='ba8aa71b6589e8920d172a8b5df84746be724d00'
ARCHIVE_SHA256='37c36efc67cabdaa58f2d73cbc1c5143d87f9918ff3d4da20e271e40c89e902b'
ORIGIN=f'https://raw.githubusercontent.com/xthan/polyvore-dataset/{COMMIT}/'
SOURCE=f'https://github.com/xthan/polyvore-dataset/blob/{COMMIT}/polyvore.tar.gz'
OUT=ROOT/'out/polyvore-source';PREPARED=ROOT/'out/polyvore-vectors.json'
ATTRIBUTION='Xintong Han et al., Learning Fashion Compatibility with Bidirectional LSTMs, ACM MM 2017. Mirage: proyección textual a categorías básicas; sin imágenes.'
TOP={11,15,17,19,21,104,252,272,273,275,286,309,341,342,343,4454,4495,4496,4497,4498}
BOTTOM={7,8,9,10,27,28,29,237,238,239,240,241,253,254,255,278,279,280,287,288,310,332,4452,4458,4459}
PIECE={3,4,5,6,243,244}
SHOE={41,42,43,46,47,48,49,50,261,262,263,264,265,266,267,268,291,292,293,294,295,296,297,298,4464,4465,4522}
CATEGORIES={**dict.fromkeys(TOP,'TOP'),**dict.fromkeys(BOTTOM,'BOTTOM'),**dict.fromkeys(PIECE,'ONE_PIECE'),**dict.fromkeys(SHOE,'FOOTWEAR')}

def digest(value):return hashlib.sha256(json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def clean(value,limit):
    if not isinstance(value,str):raise ValueError('Expected source text')
    value=' '.join(re.sub(r'<[^>]*>',' ',html.unescape(value)).split())
    value=' '.join(''.join(c for c in value if ord(c)>=32 and ord(c)!=127).split())
    if not value or '\x00' in value:raise ValueError('Empty source text')
    return value.encode()[:limit].decode('utf-8','ignore')
def project(row,split):
    if split not in ('train','valid'):raise ValueError('Unexpected split')
    if not isinstance(row,dict) or not isinstance(row.get('items'),list) or not 2<=len(row['items'])<=8:raise ValueError('Invalid source outfit')
    sid=row.get('set_id')
    if not isinstance(sid,str) or not re.fullmatch(r'[1-9][0-9]{0,19}',sid):raise ValueError('Invalid source ID')
    items=[];indices=set()
    for item in row['items']:
        if not isinstance(item,dict):raise ValueError('Invalid source item')
        category=CATEGORIES.get(item.get('categoryid'))
        if not category:continue
        index=item.get('index')
        if type(index) is not int or not 1<=index<=99 or index in indices:raise ValueError('Invalid source item index')
        indices.add(index);items.append(dict(category=category,name=clean(item.get('name'),140),source_item_index=index))
    counts=Counter(i['category'] for i in items)
    if counts==Counter(TOP=1,BOTTOM=1,FOOTWEAR=1):structure='SEPARATES'
    elif counts==Counter(ONE_PIECE=1,FOOTWEAR=1):structure='ONE_PIECE'
    else:raise ValueError('Missing or ambiguous core categories')
    order={'TOP':0,'BOTTOM':1,'ONE_PIECE':0,'FOOTWEAR':2};items.sort(key=lambda x:order[x['category']])
    title=clean(row.get('name') or 'Polyvore outfit',120)
    labels={'TOP':'superior','BOTTOM':'inferior','ONE_PIECE':'una pieza','FOOTWEAR':'calzado'}
    content='Ejemplo histórico Polyvore (2017), proyección básica. '+ '; '.join(labels[i['category']]+': '+i['name'] for i in items)+'.'
    content+=' Se omiten '+str(len(row['items'])-len(items))+' elementos originales. Coocurrencia observada, no garantía de compatibilidad ni tendencia actual.'
    result=dict(id=f'polyvore-{COMMIT[:12]}-{sid}',source_outfit_id=sid,title=title,structure=structure,items=items,content=content,source_items_count=len(row['items']),omitted_items_count=len(row['items'])-len(items),source_url=SOURCE,source_commit=COMMIT,source_archive_sha256=ARCHIVE_SHA256,source_split=split,license='Apache-2.0',license_scope='repository_metadata_only_no_images',attribution=ATTRIBUTION,published=True)
    result['document_hash']=digest(result);return result

def select(rows,split,limit):
    groups={'SEPARATES':[],'ONE_PIECE':[]};seen=set();rejected=0
    for row in sorted(rows,key=lambda x:hashlib.sha256(str(x.get('set_id','')).encode()).hexdigest()):
        try:r=project(row,split)
        except (ValueError,TypeError):rejected+=1;continue
        signature=tuple((i['category'],i['name'].casefold()) for i in r['items'])
        if signature in seen:continue
        seen.add(signature);groups[r['structure']].append(r)
    selected=[]
    for index in range(max(map(len,groups.values()),default=0)):
        for structure in ('SEPARATES','ONE_PIECE'):
            if index<len(groups[structure]):selected.append(groups[structure][index])
            if len(selected)==limit:return selected,rejected,{k:len(v) for k,v in groups.items()}
    return selected,rejected,{k:len(v) for k,v in groups.items()}

def download():
    OUT.mkdir(parents=True,exist_ok=True);archive=OUT/'polyvore.tar.gz'
    if not archive.exists():
        with urllib.request.urlopen(ORIGIN+'polyvore.tar.gz',timeout=40) as response:data=response.read(10_000_001)
        if len(data)>10_000_000 or hashlib.sha256(data).hexdigest()!=ARCHIVE_SHA256:raise ValueError('Unverified archive')
        archive.write_bytes(data)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=ARCHIVE_SHA256:raise ValueError('Changed cached archive')
    return archive

def read_split(archive,split):
    expected={'train':('train_no_dup.json',50_000_000),'valid':('valid_no_dup.json',5_000_000)}
    name,maximum=expected[split]
    with tarfile.open(archive,'r:gz') as tar:
        member=tar.getmember(name)
        if not member.isfile() or member.size>maximum:raise ValueError('Invalid archive member')
        # Never extract archive paths to the filesystem.
        with tar.extractfile(member) as stream:data=json.load(stream)
    if not isinstance(data,list):raise ValueError('Expected outfit list')
    return data

def prepare(limit):
    archive=download();train=read_split(archive,'train');valid=read_split(archive,'valid')
    if len({r['set_id'] for r in train})!=len(train) or len({r['set_id'] for r in valid})!=len(valid):raise ValueError('Duplicate source IDs')
    if {r['set_id'] for r in train}&{r['set_id'] for r in valid}:raise ValueError('Split ID overlap')
    docs,rejected,eligible=select(train,'train',limit);holdout,_,_=select(valid,'valid',24)
    if len(docs)!=limit:raise ValueError('Not enough eligible examples')
    (ROOT/'out/polyvore-corpus.json').write_text(json.dumps({'documents':docs},ensure_ascii=False,indent=2),encoding='utf-8')
    (ROOT/'out/polyvore-holdout.json').write_text(json.dumps({'documents':holdout},ensure_ascii=False,indent=2),encoding='utf-8')
    manifest=dict(repository='https://github.com/xthan/polyvore-dataset',commit=COMMIT,archive_sha256=ARCHIVE_SHA256,indexed_split='train',train_source_count=len(train),validation_source_count=len(valid),eligible_core_projections=eligible,rejected_ambiguous_or_incomplete=rejected,selected_count=len(docs),selected_structures=dict(Counter(x['structure'] for x in docs)),holdout_count=len(holdout),selected_ids=[d['id'] for d in docs],selection='SHA256(set_id) ascending, alternating core structures, exact text deduplication',images_downloaded=False,license='Apache-2.0',license_scope='repository_metadata_only_no_images',embedding_model='nvidia/nemotron-3.5-embed-1b',dimensions=1024)
    (ROOT/'knowledge/polyvore/manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return docs,manifest

def embed(docs):
    config={}
    for line in (ROOT/'.env').read_text(encoding='utf-8-sig').splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            k,v=line.split('=',1);config[k.strip()]=v.strip()
    key=config['NVIDIA_EMBEDDING_API_KEY'];model='nvidia/nemotron-3.5-embed-1b'
    if not key or any(c in key for c in ' \t\r\n\"\'') or config.get('NVIDIA_EMBEDDING_MODEL',model)!=model or config.get('NVIDIA_EMBEDDING_DIMENSIONS','1024')!='1024':raise ValueError('Invalid embedding configuration')
    cached={r['id']:r for r in json.loads(PREPARED.read_text(encoding='utf-8'))['documents']} if PREPARED.exists() else {}
    def valid(r):return r.get('embedding_model')==model and isinstance(r.get('embedding'),list) and len(r['embedding'])==1024 and all(type(v) in (int,float) and math.isfinite(v) and abs(v)<=1e10 for v in r['embedding']) and sum(v*v for v in r['embedding'])>0
    ready=[];pending=[]
    for r in docs:
        old=cached.get(r['id'],{})
        if old.get('document_hash')==r['document_hash'] and valid(old):ready.append(old)
        else:pending.append(r)
    generated=0
    for start in range(0,len(pending),8):
        batch=pending[start:start+8];payload=dict(model=model,input_type='passage',dimensions=1024,modality='text',encoding_format='float',truncate='NONE',input=[d['content'] for d in batch])
        req=urllib.request.Request('https://integrate.api.nvidia.com/v1/embeddings',data=json.dumps(payload).encode(),headers={'Content-Type':'application/json','Authorization':'Bearer '+key})
        with urllib.request.urlopen(req,timeout=40) as response:data=json.load(response)['data']
        if len(data)!=len(batch) or {x['index'] for x in data}!=set(range(len(batch))):raise ValueError('Invalid embedding batch')
        for item in data:
            r=dict(batch[item['index']],embedding_model=model,embedding=item['embedding'])
            if not valid(r):raise ValueError('Invalid embedding vector')
            ready.append(r);generated+=1
        PREPARED.write_text(json.dumps({'documents':ready},ensure_ascii=False),encoding='utf-8')
        print(f'Prepared {len(ready)}/{len(docs)} vectors',flush=True)
    ready.sort(key=lambda r:r['id']);PREPARED.write_text(json.dumps({'documents':ready},ensure_ascii=False),encoding='utf-8')
    print(f'Generated {generated}; reused {len(docs)-generated}; no database writes.',flush=True)

def export_sql():
    rows=json.loads(PREPARED.read_text(encoding='utf-8'))['documents']
    for row in rows:
        if row['source_split']!='train' or row['source_commit']!=COMMIT or row['source_archive_sha256']!=ARCHIVE_SHA256 or row['embedding_model']!='nvidia/nemotron-3.5-embed-1b':raise ValueError('Unapproved prepared source')
        if len(row['embedding'])!=1024 or not all(type(x) in (int,float) and math.isfinite(x) and abs(x)<=1e10 for x in row['embedding']) or sum(x*x for x in row['embedding'])<=0:raise ValueError('Invalid prepared vector')
        if row['document_hash']!=digest({k:v for k,v in row.items() if k not in ('document_hash','embedding_model','embedding')}):raise ValueError('Changed prepared metadata')
    if len({r['id'] for r in rows})!=len(rows):raise ValueError('Duplicate prepared IDs')
    statements=['-- Administrative import only. Application role must remain SELECT-only.','begin;']
    for start in range(0,len(rows),8):
        payload=json.dumps(rows[start:start+8],ensure_ascii=False,separators=(',',':')).replace("'","''")
        statements.append("""insert into recommendation.outfit_examples as target
(id,source_outfit_id,title,structure,items,content,source_items_count,omitted_items_count,source_url,source_commit,source_archive_sha256,source_split,license,license_scope,attribution,published,document_hash,embedding_model,embedding)
select x->>'id',x->>'source_outfit_id',x->>'title',x->>'structure',x->'items',x->>'content',(x->>'source_items_count')::integer,(x->>'omitted_items_count')::integer,
x->>'source_url',x->>'source_commit',x->>'source_archive_sha256',x->>'source_split',x->>'license',x->>'license_scope',x->>'attribution',(x->>'published')::boolean,x->>'document_hash',x->>'embedding_model',(x->>'embedding')::public.vector
from jsonb_array_elements('"""+payload+"""'::jsonb) x
on conflict(id) do update set title=excluded.title,structure=excluded.structure,items=excluded.items,content=excluded.content,source_items_count=excluded.source_items_count,omitted_items_count=excluded.omitted_items_count,
source_url=excluded.source_url,source_commit=excluded.source_commit,source_archive_sha256=excluded.source_archive_sha256,source_split=excluded.source_split,license=excluded.license,license_scope=excluded.license_scope,
attribution=excluded.attribution,published=excluded.published,document_hash=excluded.document_hash,embedding_model=excluded.embedding_model,embedding=excluded.embedding,indexed_at=now()
where target.document_hash<>excluded.document_hash;""")
    statements.append('commit;');(ROOT/'out/polyvore-upsert.sql').write_text('\n'.join(statements)+'\n',encoding='utf-8')
    print(f'Exported {len(rows)} validated rows to out/polyvore-upsert.sql; no database writes.')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--limit',type=int,default=128);parser.add_argument('--embed',action='store_true');parser.add_argument('--emit-row',type=int);parser.add_argument('--export-sql',action='store_true')
    args=parser.parse_args()
    if args.export_sql:export_sql();return
    if args.emit_row is not None:
        print(json.dumps(json.loads(PREPARED.read_text(encoding='utf-8'))['documents'][args.emit_row],ensure_ascii=True,separators=(',',':')));return
    if not 8<=args.limit<=512:raise ValueError('Import limit must be 8..512')
    docs,manifest=prepare(args.limit);print(json.dumps({k:v for k,v in manifest.items() if k!='selected_ids'},ensure_ascii=True),flush=True)
    if args.embed:embed(docs)
if __name__=='__main__':main()
