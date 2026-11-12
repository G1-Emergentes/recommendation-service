"""Prepare the reviewed corpus for an administrator to ingest. Never writes to Neon.
Only the official NVIDIA embedding endpoint is called. Prepared vectors stay under out/.
"""
import hashlib,json,math,sys,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUTPUT=ROOT/'out/style-knowledge-vectors.json'

def main():
    if '--emit-row' in sys.argv:
        row=json.loads(OUTPUT.read_text(encoding='utf-8'))['documents'][int(sys.argv[-1])]
        assert len(row['embedding'])==1024 and all(math.isfinite(x) for x in row['embedding'])
        print(json.dumps(row,ensure_ascii=True,separators=(',',':')));return
    config={}
    for line in (ROOT/'.env').read_text(encoding='utf-8-sig').splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            k,v=line.split('=',1);config[k.strip()]=v.strip()
    key=config['NVIDIA_EMBEDDING_API_KEY'];assert key and not any(c in key for c in ' \t\r\n\"\'')
    assert config.get('NVIDIA_EMBEDDING_DIMENSIONS','1024')=='1024'
    model='nvidia/nemotron-3.5-embed-1b';assert config.get('NVIDIA_EMBEDDING_MODEL',model)==model
    docs=json.loads((ROOT/'knowledge/style_corpus.json').read_text(encoding='utf-8'))['documents']
    assert 1<=len(docs)<=8 and len({d['id'] for d in docs})==len(docs)
    for d in docs:
        assert d['license']=='CC-BY-SA-4.0' and d['source_url'].startswith('https://en.wikipedia.org/wiki/')
        assert 0<len(d['content'].encode())<=1400 and 0<len(d['attribution'].encode())<=240
        d['document_hash']=hashlib.sha256(json.dumps(d,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        d['embedding_model']=model
    if OUTPUT.exists():
        old=json.loads(OUTPUT.read_text(encoding='utf-8'))['documents']
        if [(d['id'],d['document_hash']) for d in old]==[(d['id'],d['document_hash']) for d in docs]:
            print('Corpus unchanged; existing prepared embeddings reused.');return
    payload=dict(model=model,input_type='passage',dimensions=1024,modality='text',encoding_format='float',truncate='NONE',input=[d['title']+'. '+d['content']+' Estilos: '+', '.join(d['styles']) for d in docs])
    req=urllib.request.Request('https://integrate.api.nvidia.com/v1/embeddings',data=json.dumps(payload).encode(),headers={'Content-Type':'application/json','Authorization':'Bearer '+key})
    with urllib.request.urlopen(req,timeout=40) as response:
        data=json.load(response)['data']
    assert len(data)==len(docs) and {x['index'] for x in data}==set(range(len(docs)))
    for item in data:
        vec=item['embedding'];assert len(vec)==1024 and all(isinstance(x,(int,float)) and not isinstance(x,bool) and math.isfinite(x) and abs(x)<=1e10 for x in vec) and sum(x*x for x in vec)>0
        docs[item['index']]['embedding']=vec
    OUTPUT.write_text(json.dumps({'documents':docs},ensure_ascii=False),encoding='utf-8')
    print(f'Prepared {len(docs)} real 1024-dimensional embeddings. No credentials recorded; no database writes.')
if __name__=='__main__':main()
