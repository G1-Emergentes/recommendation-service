"""Offline tests for normalization, split boundaries, deterministic sampling and embedding cache."""
import copy,importlib.util,json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('polyvore_import',root/'knowledge/import_polyvore.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

def outfit(sid='123',piece=False):
    cats=[3,49] if piece else [21,27,49]
    return dict(set_id=sid,name='Casual',items=[dict(index=i+1,categoryid=cat,name=['Black tee','Blue jeans','Black boots'][i],image='https://must-not-fetch',price=999,likes=10) for i,cat in enumerate(cats)])

class PolyvoreImporterTests(unittest.TestCase):
    def test_core_projection_does_not_import_images_or_prices(self):
        row=outfit();row['items'].append(dict(index=4,categoryid=35,name='Bag'))
        result=module.project(row,'train')
        self.assertEqual(result['structure'],'SEPARATES');self.assertEqual(result['omitted_items_count'],1)
        self.assertEqual([x['category'] for x in result['items']],['TOP','BOTTOM','FOOTWEAR'])
        self.assertTrue(all(set(i)=={'category','name','source_item_index'} for i in result['items']))
        self.assertNotIn('must-not-fetch',json.dumps(result));self.assertNotIn('price',result)
        self.assertEqual(result['source_split'],'train');self.assertEqual(result['source_commit'],module.COMMIT)
        self.assertEqual(module.project(outfit(piece=True),'train')['structure'],'ONE_PIECE')
    def test_reject_incomplete_ambiguous_and_invalid_metadata(self):
        for change in ['missing-shoe','extra-top','blank-name','duplicate-index','bad-id']:
            row=outfit()
            if change=='missing-shoe':row['items'].pop()
            if change=='extra-top':row['items'].append(dict(index=4,categoryid=11,name='Other top'))
            if change=='blank-name':row['items'][0]['name']=' '
            if change=='duplicate-index':row['items'][1]['index']=1
            if change=='bad-id':row['set_id']='../../escape'
            with self.subTest(change=change),self.assertRaises(ValueError):module.project(row,'train')
        with self.assertRaises(ValueError):module.project(outfit(),'test')
    def test_sampling_is_deterministic_balanced_and_deduplicated(self):
        rows=[outfit(str(i),i%2==0) for i in range(1,30)]
        for row in rows:row['items'][0]['name']+=' '+row['set_id']
        a,_,_=module.select(rows,'train',8);b,_,_=module.select(list(reversed(rows)),'train',8)
        self.assertEqual(a,b);self.assertEqual(sum(r['structure']=='SEPARATES' for r in a),4)
        self.assertEqual(len({r['id'] for r in a}),8)
        duplicate=copy.deepcopy(rows[0]);duplicate['set_id']='999'
        selected,_,_=module.select([rows[0],duplicate],'train',8)
        self.assertEqual(len(selected),1)
        holdout=module.project(outfit('10000'),'valid');self.assertNotIn(holdout['id'],{r['id'] for r in a})
    def test_text_is_bounded_utf8_and_changes_invalidate_hash(self):
        self.assertEqual(module.clean('<b>black</b>\x00 shirt',140),'black shirt')
        self.assertLessEqual(len(module.clean('é'*300,140).encode()),140)
        a=module.project(outfit(),'train');row=outfit();row['items'][0]['name']='Different garment';b=module.project(row,'train')
        self.assertNotEqual(a['document_hash'],b['document_hash'])
    def test_unchanged_embeddings_reuse_cache_without_network(self):
        row=module.project(outfit(),'train');cached=dict(row,embedding_model='nvidia/nemotron-3.5-embed-1b',embedding=[1.0]+[0.0]*1023)
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory);prepared=temp/'vectors.json';prepared.write_text(json.dumps({'documents':[cached]}),encoding='utf-8')
            (temp/'.env').write_text('NVIDIA_EMBEDDING_API_KEY=nvapi-test\n',encoding='utf-8')
            with patch.object(module,'ROOT',temp),patch.object(module,'PREPARED',prepared),patch.object(module.urllib.request,'urlopen',side_effect=AssertionError('No provider call for unchanged content')):
                module.embed([row])
            self.assertEqual(json.loads(prepared.read_text(encoding='utf-8'))['documents'],[cached])
    def test_admin_sql_export_verifies_integrity_and_cannot_break_literal(self):
        source=outfit();source['name']="summer'); select 1; --"
        row=module.project(source,'train');row.update(embedding_model='nvidia/nemotron-3.5-embed-1b',embedding=[1.0]+[0.0]*1023)
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory);(temp/'out').mkdir();prepared=temp/'vectors.json';prepared.write_text(json.dumps({'documents':[row]}),encoding='utf-8')
            with patch.object(module,'ROOT',temp),patch.object(module,'PREPARED',prepared),patch.object(module.urllib.request,'urlopen',side_effect=AssertionError('SQL export must be offline')):
                module.export_sql();sql=(temp/'out/polyvore-upsert.sql').read_text(encoding='utf-8')
                quoted=False;separators=0;i=0
                while i<len(sql):
                    if sql[i]=="'":
                        if quoted and i+1<len(sql) and sql[i+1]=="'":i+=1
                        else:quoted=not quoted
                    elif sql[i]==';' and not quoted:separators+=1
                    i+=1
                self.assertFalse(quoted);self.assertEqual(separators,3)
                row['content']+=' altered';prepared.write_text(json.dumps({'documents':[row]}),encoding='utf-8')
                with self.assertRaises(ValueError):module.export_sql()
                self.assertEqual((temp/'out/polyvore-upsert.sql').read_text(encoding='utf-8'),sql)
if __name__=='__main__':unittest.main()
