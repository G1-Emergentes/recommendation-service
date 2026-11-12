#include "WardrobeIndex.h"
#include "GarmentJson.h"
#include "StyleKnowledgeJson.h"
#include "OutfitExampleJson.h"
#include <drogon/HttpClient.h>
#include <drogon/utils/Utilities.h>
#include <libpq-fe.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <regex>

namespace mirage::recommendation
{
namespace
{
using Tx=std::shared_ptr<drogon::orm::Transaction>;
using Result=drogon::orm::Result;
HistoryResult error(int status,const char *code)
{Json::Value v;v["error"]["code"]=code;v["error"]["message"]="Vector workflow could not complete. No incomplete index was saved.";return {status,v};}
bool uuid(const std::string &v)
{static const std::regex p("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");return std::regex_match(v,p);}
std::string json(const Json::Value &v)
{Json::StreamWriterBuilder w;w["indentation"]="";return Json::writeString(w,v);}
Json::Value parse(const std::string &s)
{
    Json::CharReaderBuilder b;b["rejectDupKeys"]=true;b["failIfExtra"]=true;b["stackLimit"]=64;
    Json::Value v;std::string e;std::unique_ptr<Json::CharReader> r(b.newCharReader());
    if(!r->parse(s.data(),s.data()+s.size(),&v,&e))throw std::invalid_argument("Invalid vector-store response");return v;
}
int status(WardrobeStatus s)
{return s==WardrobeStatus::Unauthorized?401:s==WardrobeStatus::Changed?409:s==WardrobeStatus::LimitExceeded?413:503;}
const char *wardrobeCode(WardrobeStatus s)
{return s==WardrobeStatus::Unauthorized?"UNAUTHENTICATED":s==WardrobeStatus::Changed?"WARDROBE_CHANGED":"WARDROBE_UNAVAILABLE";}
const std::string fingerprint=R"SQL(
 select coalesce(jsonb_agg(jsonb_build_object('garment_id',garment_id,'revision',revision,'document_hash',document_hash)
 order by garment_id),'[]'::jsonb)::text as rows,
 md5(coalesce(jsonb_agg(jsonb_build_array(garment_id,revision,source_updated_at,document_hash)
 order by garment_id),'[]'::jsonb)::text) as fingerprint
 from recommendation.wardrobe_embeddings where user_id=$1::uuid
)SQL";
std::string document(const Garment &g)
{
    const std::map<std::string,std::string> labels{{"TOP","superior"},{"BOTTOM","pantalón o falda"},{"FOOTWEAR","calzado"},
      {"ONE_PIECE","prenda de una pieza"},{"OUTERWEAR","abrigo o casaca"},{"ACCESSORY","accesorio"},
      {"BLACK","negro"},{"WHITE","blanco"},{"GRAY","gris"},{"BLUE","azul"},{"RED","rojo"},{"GREEN","verde"},
      {"YELLOW","amarillo"},{"ORANGE","naranja"},{"PURPLE","morado"},{"PINK","rosado"},{"BROWN","marrón"},
      {"BEIGE","beige"},{"MULTICOLOR","multicolor"},{"CASUAL","casual"},{"FORMAL","formal"},{"SPORT","deportivo"},
      {"STREETWEAR","urbano streetwear"},{"MINIMALIST","minimalista"},{"CLASSIC","clásico"},{"BOHEMIAN","bohemio"}};
    const auto label=[&](const std::string &v){auto i=labels.find(v);return i==labels.end()?v:i->second;};
    const auto item=garmentJson(g);
    auto description=g.description;const auto credit=description.find(" Imagen:");
    if(credit!=std::string::npos)description.resize(credit);
    std::string text="Prenda: "+g.name+". Categoría: "+label(item["category"].asString())+". ";
    if(g.subcategory)text+="Subcategoría: "+*g.subcategory+". ";
    text+="Colores: ";for(const auto &c:g.colors)text+=label(c)+"; ";
    text+="Estilos: ";for(const auto &s:g.styles)text+=label(s)+"; ";
    return text+"Descripción: "+description;
}
bool same(const std::vector<Garment> &a,const std::vector<Garment> &b)
{
    const auto stamp=[](std::string s){
        const auto offset=s.find("+00:00");if(offset!=std::string::npos)s.replace(offset,6,"Z");
        const auto dot=s.find('.');if(dot!=std::string::npos && !s.empty() && s.back()=='Z')
        {s.pop_back();while(!s.empty() && s.back()=='0')s.pop_back();if(s.back()=='.')s.pop_back();s+='Z';}return s;
    };
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)
    {auto x=garmentJson(a[i]),y=garmentJson(b[i]);x["updated_at"]=stamp(a[i].updatedAt);y["updated_at"]=stamp(b[i].updatedAt);if(x!=y)return false;}
    return true;
}
}
struct WardrobeIndex::SyncRun
{
    UserAccess access;
    std::vector<Garment> garments;
    Json::Value payload{Json::arrayValue};
    std::vector<size_t> changed;
    size_t next=0;
    std::string fingerprint;
    HistoryCompletion done;
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
};
WardrobeIndex::WardrobeIndex(const WardrobeClient &w,std::string url,std::string key,std::string model,std::string origin,double timeout)
 :wardrobe_(w),key_(std::move(key)),model_(std::move(model)),origin_(std::move(origin)),timeout_(timeout)
{
    char *problem=nullptr;auto options=PQconninfoParse(url.c_str(),&problem);
    std::map<std::string,std::string> settings;
    if(options){for(auto p=options;p->keyword;++p)if(p->val)settings[p->keyword]=p->val;PQconninfoFree(options);}
    if(problem)PQfreemem(problem);
    static const std::regex host(R"(^[a-zA-Z0-9.-]+\.neon\.tech$)"),allowedOrigin(R"(^https://integrate\.api\.nvidia\.com$|^http://(127\.0\.0\.1|localhost):[0-9]{1,5}$)");
    if(!options || !std::regex_match(settings["host"],host) || settings["user"]!="mirage_vector_app" ||
       settings["dbname"]!="recommendation_vectors" || (settings["sslmode"]!="require" && settings["sslmode"]!="verify-full") ||
       key_.empty() || key_.find_first_of(" \t\r\n\"'")!=std::string::npos || model_!="nvidia/nemotron-3.5-embed-1b" ||
       !std::regex_match(origin_,allowedOrigin) || !(timeout>=1 && timeout<=30))
        throw std::invalid_argument("Invalid backend vector-store configuration; credentials omitted");
    url+=(url.find('?')==std::string::npos?"?":"&");url+="connect_timeout=10&application_name=mirage_recommendation_vectors";
    db_=drogon::orm::DbClient::newPgClient(url,2,false);db_->setTimeout(10);db_->setReconnectInterval(10);
}
void WardrobeIndex::actor(const UserAccess &a,std::function<void(Tx)> done) const
{
    if(!uuid(a.userId) || a.accessToken.empty()){done(nullptr);return;}
    db_->newTransactionAsync([a,done](const Tx &tx){
        if(!tx){done(nullptr);return;}
        tx->execSqlAsync("select set_config('app.user_id',$1,true),set_config('statement_timeout','10000',true),set_config('lock_timeout','5000',true),set_config('TimeZone','UTC',true)",
            [tx,done](const Result &){done(tx);},[tx,done](const std::exception_ptr &){tx->rollback();done(nullptr);},a.userId);
    });
}
void WardrobeIndex::read(const UserAccess &a,HistoryCompletion done) const
{
    actor(a,[a,done](Tx tx){
        if(!tx){done(error(503,"VECTOR_STORE_UNAVAILABLE"));return;}
        tx->execSqlAsync(fingerprint,[tx,done](const Result &r){
            try{Json::Value body;body["rows"]=parse(r[0]["rows"].as<std::string>());body["fingerprint"]=r[0]["fingerprint"].as<std::string>();
                tx->setCommitCallback([done,body](bool ok){done(ok?HistoryResult{200,body}:error(503,"VECTOR_STORE_UNAVAILABLE"));});}
            catch(const std::exception &){tx->rollback();done(error(503,"VECTOR_STORE_UNAVAILABLE"));}
        },[tx,done](const std::exception_ptr &){tx->rollback();done(error(503,"VECTOR_STORE_UNAVAILABLE"));},a.userId);
    });
}
void WardrobeIndex::embed(std::vector<std::string> inputs,std::string type,Embedded done,double timeout) const
{
    Json::Value body;body["model"]=model_;body["input_type"]=type;body["dimensions"]=1024;body["modality"]="text";
    body["encoding_format"]="float";body["truncate"]="NONE";body["input"]=Json::Value(Json::arrayValue);
    for(const auto &s:inputs)body["input"].append(s);
    auto client=drogon::HttpClient::newHttpClient(origin_,nullptr,false,true);auto req=drogon::HttpRequest::newHttpJsonRequest(body);
    req->setPath("/v1/embeddings");req->setMethod(drogon::Post);req->addHeader("Authorization","Bearer "+key_);
    client->sendRequest(req,[client,count=inputs.size(),done](drogon::ReqResult state,const drogon::HttpResponsePtr &response){
        if(state!=drogon::ReqResult::Ok || !response || response->statusCode()!=drogon::k200OK || response->body().size()>512*1024)
        {done(false,{});return;}
        const auto data=response->getJsonObject();
        if(!data || !data->isObject() || !(*data)["data"].isArray() || (*data)["data"].size()!=count){done(false,{});return;}
        Vectors vectors(count);std::set<unsigned> seen;
        for(const auto &row:(*data)["data"])
        {
            if(!row.isObject() || !row["index"].isUInt() || row["index"].asUInt()>=count || !seen.insert(row["index"].asUInt()).second ||
               !row["embedding"].isArray() || row["embedding"].size()!=1024){done(false,{});return;}
            auto &vector=vectors[row["index"].asUInt()];double norm=0;
            for(const auto &v:row["embedding"])
            {if(!v.isNumeric() || !std::isfinite(v.asDouble()) || std::abs(v.asDouble())>1e10){done(false,{});return;}vector.push_back(v.asDouble());norm+=v.asDouble()*v.asDouble();}
            if(!(norm>0)){done(false,{});return;}
        }
        done(true,std::move(vectors));
    },(std::min)(timeout_,timeout));
}
void WardrobeIndex::sync(UserAccess a,HistoryCompletion done) const
{
    if(!uuid(a.userId) || a.accessToken.empty()){done(error(401,"UNAUTHENTICATED"));return;}
    bool acquired=false;{std::lock_guard<std::mutex> lock(mutex_);if(busy_.size()<8)acquired=busy_.insert(a.userId).second;}
    if(!acquired){done(error(409,"INDEX_BUSY"));return;}
    auto run=std::make_shared<SyncRun>();run->access=a;
    run->done=[this,a,done](HistoryResult r){{std::lock_guard<std::mutex> lock(mutex_);busy_.erase(a.userId);}done(std::move(r));};
    wardrobe_.listForUser(a,[this,run](WardrobeResult snapshot){
        if(snapshot.status!=WardrobeStatus::Found){run->done(error(status(snapshot.status),wardrobeCode(snapshot.status)));return;}
        if(snapshot.garments.size()>100){run->done(error(413,"INDEX_WARDROBE_LIMIT"));return;}
        run->garments=std::move(snapshot.garments);
        read(run->access,[this,run](HistoryResult old){
            if(old.status!=200){run->done(old);return;}
            run->fingerprint=old.body["fingerprint"].asString();std::map<std::string,Json::Value> cached;
            for(const auto &row:old.body["rows"])cached[row["garment_id"].asString()]=row;
            for(const auto &g:run->garments)
            {
                if(cached.count(g.id) && cached[g.id]["revision"].asUInt()>g.revision){run->done(error(409,"WARDROBE_CHANGED"));return;}
                auto item=garmentJson(g);const auto text=document(g);
                auto hash=drogon::utils::getSha256(text);if(hash.size()==32)hash=drogon::utils::binaryStringToHex(reinterpret_cast<const unsigned char *>(hash.data()),hash.size(),true);
                std::transform(hash.begin(),hash.end(),hash.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                item["document"]=text;item["document_hash"]=hash;
                if(!cached.count(g.id) || cached[g.id]["document_hash"]!=hash)run->changed.push_back(run->payload.size());
                run->payload.append(item);
            }
            nextBatch(run);
        });
    });
}
void WardrobeIndex::nextBatch(std::shared_ptr<SyncRun> run) const
{
    const double remaining=std::chrono::duration<double>(run->deadline-std::chrono::steady_clock::now()).count();
    if(remaining<=0){run->done(error(503,"INDEX_TIMEOUT"));return;}
    if(run->next==run->changed.size())
    {
        wardrobe_.listForUser(run->access,[this,run](WardrobeResult current){
            if(current.status!=WardrobeStatus::Found){run->done(error(status(current.status),wardrobeCode(current.status)));return;}
            if(!same(run->garments,current.garments)){run->done(error(409,"WARDROBE_CHANGED"));return;}write(run);
        });return;
    }
    const auto start=run->next;const auto count=(std::min)(size_t(8),run->changed.size()-start);
    std::vector<std::string> batch;for(size_t i=0;i<count;++i)batch.push_back(run->payload[static_cast<Json::ArrayIndex>(run->changed[start+i])]["document"].asString());
    embed(batch,"passage",[this,run,start,count](bool ok,Vectors vectors){
        if(!ok){run->done(error(503,"EMBEDDING_UNAVAILABLE"));return;}
        for(size_t i=0;i<count;++i){Json::Value v(Json::arrayValue);for(double n:vectors[i])v.append(n);run->payload[static_cast<Json::ArrayIndex>(run->changed[start+i])]["embedding"]=json(v);}
        run->next+=count;nextBatch(run);
    },remaining);
}
void WardrobeIndex::write(std::shared_ptr<SyncRun> run) const
{
    if(std::chrono::steady_clock::now()>=run->deadline){run->done(error(503,"INDEX_TIMEOUT"));return;}
    actor(run->access,[run](Tx tx){
        if(!tx){run->done(error(503,"VECTOR_STORE_UNAVAILABLE"));return;}
        tx->execSqlAsync("select pg_advisory_xact_lock(hashtextextended($1,0))",[run,tx](const Result &){
            tx->execSqlAsync(fingerprint,[run,tx](const Result &r){
                if(r[0]["fingerprint"].as<std::string>()!=run->fingerprint){tx->rollback();run->done(error(409,"INDEX_CHANGED"));return;}
                const std::string sql=R"SQL(
with payload as (select value as x from jsonb_array_elements($2::jsonb)),
upserted as (
 insert into recommendation.wardrobe_embeddings as w
 (user_id,garment_id,revision,source_updated_at,name,description,category,subcategory,colors,styles,availability,image_id,image_version,document,document_hash,embedding)
 select $1::uuid,(x->>'id')::uuid,(x->>'revision')::integer,(x->>'updated_at')::timestamptz,x->>'name',x->>'description',x->>'category',x->>'subcategory',
 array(select jsonb_array_elements_text(x->'colors')),array(select jsonb_array_elements_text(x->'styles')),x->>'availability',
 (x->'image'->>'id')::uuid,(x->'image'->>'version')::integer,x->>'document',x->>'document_hash',coalesce((x->>'embedding')::public.vector,c.embedding)
 from payload left join recommendation.wardrobe_embeddings c on c.user_id=$1::uuid and c.garment_id=(x->>'id')::uuid
 on conflict(user_id,garment_id) do update set revision=excluded.revision,source_updated_at=excluded.source_updated_at,
 name=excluded.name,description=excluded.description,category=excluded.category,subcategory=excluded.subcategory,colors=excluded.colors,styles=excluded.styles,
 availability=excluded.availability,image_id=excluded.image_id,image_version=excluded.image_version,document=excluded.document,document_hash=excluded.document_hash,
 embedding=excluded.embedding,indexed_at=case when w.document_hash<>excluded.document_hash then now() else w.indexed_at end,synced_at=now()
 returning garment_id), removed as (
 delete from recommendation.wardrobe_embeddings where user_id=$1::uuid and garment_id not in (select (x->>'id')::uuid from payload) returning garment_id)
 select (select count(*) from upserted) as indexed,(select count(*) from removed) as removed
)SQL";
                tx->execSqlAsync(sql,[run,tx](const Result &saved){
                    Json::Value body;body["indexed"]=saved[0]["indexed"].as<unsigned>();body["removed"]=saved[0]["removed"].as<unsigned>();
                    body["embeddings_generated"]=static_cast<unsigned>(run->changed.size());body["embedding_model"]="nvidia/nemotron-3.5-embed-1b";body["dimensions"]=1024;
                    tx->setCommitCallback([run,body](bool ok){run->done(ok?HistoryResult{200,body}:error(503,"VECTOR_STORE_UNAVAILABLE"));});
                },[run,tx](const std::exception_ptr &){tx->rollback();run->done(error(503,"VECTOR_STORE_UNAVAILABLE"));},run->access.userId,json(run->payload));
            },[run,tx](const std::exception_ptr &){tx->rollback();run->done(error(503,"VECTOR_STORE_UNAVAILABLE"));},run->access.userId);
        },[run,tx](const std::exception_ptr &){tx->rollback();run->done(error(503,"VECTOR_STORE_UNAVAILABLE"));},run->access.userId);
    });
}
bool WardrobeIndex::validExampleFilters(const Json::Value &f)
{
    if(!f.isObject() || f.size()>3)return false;
    for(const auto &key:f.getMemberNames())if(key!="query" && key!="structure" && key!="limit")return false;
    if(f.isMember("structure") && (!f["structure"].isString() || (f["structure"]!="" && f["structure"]!="SEPARATES" && f["structure"]!="ONE_PIECE")))return false;
    auto common=f;common.removeMember("structure");return validStyleFilters(common);
}
void WardrobeIndex::searchOutfitExamples(UserAccess a,Json::Value filters,HistoryCompletion done) const
{
    if(!uuid(a.userId) || a.accessToken.empty()){done(error(401,"UNAUTHENTICATED"));return;}
    if(!validExampleFilters(filters)){done(error(400,"INVALID_ARGUMENT"));return;}
    embed({filters["query"].asString()},"query",[this,a,filters,done](bool ok,Vectors vectors){
        if(!ok){done(error(503,"EMBEDDING_UNAVAILABLE"));return;}
        Json::Value vector(Json::arrayValue);for(double n:vectors[0])vector.append(n);const auto value=json(vector);
        actor(a,[filters,value,done](Tx tx){
            if(!tx){done(error(503,"VECTOR_STORE_UNAVAILABLE"));return;}
            const std::string sql=R"SQL(
 select jsonb_build_object('id',id,'source_outfit_id',source_outfit_id,'title',title,'structure',structure,'items',items,'content',content,
 'source_items_count',source_items_count,'omitted_items_count',omitted_items_count,'source_url',source_url,'source_commit',source_commit,
 'source_archive_sha256',source_archive_sha256,'source_split',source_split,'license',license,'license_scope',license_scope,'attribution',attribution)::text as reference,
 1-(embedding <=> $1::public.vector) as similarity from recommendation.outfit_examples
 where published and source_split='train' and embedding_model='nvidia/nemotron-3.5-embed-1b'
 and ($2='' or structure=$2) order by embedding <=> $1::public.vector,id limit $3::integer
)SQL";
            tx->execSqlAsync(sql,[tx,done](const Result &rows){
                Json::Value body;body["items"]=Json::Value(Json::arrayValue);body["scope"]="published_polyvore_train_projections";
                try{for(const auto &row:rows){auto item=outfitExampleJson(readOutfitExample(parse(row["reference"].as<std::string>())));
                    item["similarity"]=(std::max)(-1.0,(std::min)(1.0,row["similarity"].as<double>()));body["items"].append(item);}}
                catch(const std::exception &){tx->rollback();done(error(503,"VECTOR_STORE_UNAVAILABLE"));return;}
                tx->setCommitCallback([body,done](bool committed){done(committed?HistoryResult{200,body}:error(503,"VECTOR_STORE_UNAVAILABLE"));});
            },[tx,done](const std::exception_ptr &){tx->rollback();done(error(503,"VECTOR_STORE_UNAVAILABLE"));},value,filters.get("structure","").asString(),filters.get("limit",3).asUInt());
        });
    },timeout_);
}
bool WardrobeIndex::validStyleFilters(const Json::Value &f)
{
    if(!f.isObject() || f.size()>3 || !f["query"].isString())return false;
    for(const auto &key:f.getMemberNames())if(key!="query" && key!="style" && key!="limit")return false;
    const auto query=f["query"].asString();
    if(query.size()>512 || query.find('\0')!=std::string::npos || query.find_first_not_of(" \t\r\n")==std::string::npos)return false;
    const std::set<std::string> styles{"","CASUAL","FORMAL","SPORT","STREETWEAR","VINTAGE","MINIMALIST","BOHEMIAN","CLASSIC","ELEGANT"};
    return (!f.isMember("style") || (f["style"].isString() && styles.count(f["style"].asString()))) &&
        (!f.isMember("limit") || (f["limit"].isUInt() && f["limit"].asUInt()>=1 && f["limit"].asUInt()<=3));
}
void WardrobeIndex::searchStyleKnowledge(UserAccess a,Json::Value filters,HistoryCompletion done) const
{
    if(!uuid(a.userId) || a.accessToken.empty()){done(error(401,"UNAUTHENTICATED"));return;}
    if(!validStyleFilters(filters)){done(error(400,"INVALID_ARGUMENT"));return;}
    embed({filters["query"].asString()},"query",[this,a,filters,done](bool ok,Vectors vectors){
        if(!ok){done(error(503,"EMBEDDING_UNAVAILABLE"));return;}
        Json::Value vector(Json::arrayValue);for(double n:vectors[0])vector.append(n);const auto value=json(vector);
        actor(a,[filters,value,done](Tx tx){
            if(!tx){done(error(503,"VECTOR_STORE_UNAVAILABLE"));return;}
            const std::string sql=R"SQL(
 select jsonb_build_object('id',id,'title',title,'content',content,'styles',styles,'source_url',source_url,
 'license',license,'attribution',attribution,'content_kind',content_kind,'revision',revision)::text as reference,
 1-(embedding <=> $1::public.vector) as similarity
 from recommendation.style_knowledge where published and license='CC-BY-SA-4.0'
 and embedding_model='nvidia/nemotron-3.5-embed-1b'
 and ($2='' or cardinality(styles)=0 or $2=any(styles))
 order by embedding <=> $1::public.vector,id limit $3::integer
)SQL";
            tx->execSqlAsync(sql,[tx,done](const Result &rows){
                Json::Value body;body["items"]=Json::Value(Json::arrayValue);body["scope"]="published_style_knowledge";
                try{for(const auto &row:rows){const auto raw=parse(row["reference"].as<std::string>());auto item=styleReferenceJson(readStyleReference(raw));
                    item["styles"]=raw["styles"];item["revision"]=raw["revision"];
                    item["similarity"]=(std::max)(-1.0,(std::min)(1.0,row["similarity"].as<double>()));body["items"].append(item);}}
                catch(const std::exception &){tx->rollback();done(error(503,"VECTOR_STORE_UNAVAILABLE"));return;}
                tx->setCommitCallback([body,done](bool committed){done(committed?HistoryResult{200,body}:error(503,"VECTOR_STORE_UNAVAILABLE"));});
            },[tx,done](const std::exception_ptr &){tx->rollback();done(error(503,"VECTOR_STORE_UNAVAILABLE"));},value,filters.get("style","").asString(),filters.get("limit",3).asUInt());
        });
    },timeout_);
}
bool WardrobeIndex::validFilters(const Json::Value &f,bool requireQuery)
{
    if(!f.isObject() || f.size()>5)return false;
    const std::map<std::string,std::set<std::string>> codes{{"category",{"","TOP","BOTTOM","ONE_PIECE","FOOTWEAR","OUTERWEAR","ACCESSORY"}},
      {"color",{"","BLACK","WHITE","GRAY","BLUE","RED","GREEN","YELLOW","ORANGE","PURPLE","PINK","BROWN","BEIGE","MULTICOLOR"}},
      {"style",{"","CASUAL","FORMAL","SPORT","STREETWEAR","MINIMALIST","CLASSIC","BOHEMIAN"}}};
    for(const auto &key:f.getMemberNames())
    {
        if(key=="query") {if(!f[key].isString() || f[key].asString().size()>120 || f[key].asString().find('\0')!=std::string::npos)return false;}
        else if(key=="limit") {if(!f[key].isUInt() || f[key].asUInt()<1 || f[key].asUInt()>10)return false;}
        else if(!codes.count(key) || !f[key].isString() || !codes.at(key).count(f[key].asString()))return false;
    }
    return !requireQuery || (f["query"].isString() && f["query"].asString().find_first_not_of(" \t\r\n")!=std::string::npos);
}
void WardrobeIndex::candidates(UserAccess a,RecommendationParameters p,RecommendationPreferences preferences,WardrobeCompletion done) const
{
    Json::Value selection(Json::objectValue),f;
    selection["outfits"]=true;
    const auto array=[](const std::vector<std::string> &values){Json::Value a(Json::arrayValue);for(const auto &v:values)a.append(v);return a;};
    selection["avoided_colors"]=array(preferences.avoidedColors);
    selection["excluded_colors"]=array(p.excludedColors);
    selection["excluded_garments"]=array(p.excludedGarments);
    selection["requested_style"]=p.style;
    selection["conversation_style"]=p.conversationStyle;
    selection["dark_only"]=p.darkOnly;
    selection["previous_ids"]=Json::Value(Json::arrayValue);
    for(const auto &o:p.previousOutfits)for(const auto &id:o.garmentIds)selection["previous_ids"].append(id);
    // Keep known constraints near the top, but retain candidates to allow explicit relaxation.
    f["query"]=p.message.empty()?"Outfit para "+p.occasion+" estilo "+p.style:
        p.message+". Ocasion: "+p.conversationOccasion+". Estilo: "+p.conversationStyle;
    auto query=f["query"].asString();
    if(query.size()>1000){size_t end=1000;while(end && (static_cast<unsigned char>(query[end])&0xc0)==0x80)--end;query.resize(end);}
    f["query"]=query;f["limit"]=33;
    retrieve(std::move(a),std::move(f),std::move(selection),[done](HistoryResult result){
        if(result.status!=200){done({result.status==401?WardrobeStatus::Unauthorized:result.status==409?WardrobeStatus::Changed:
            result.status==413?WardrobeStatus::LimitExceeded:WardrobeStatus::Unavailable,{}});return;}
        std::vector<Garment> garments;
        try{for(const auto &item:result.body["items"])garments.push_back(readGarment(item));}
        catch(const std::exception &){done({WardrobeStatus::InvalidResponse,{}});return;}
        done({WardrobeStatus::Found,std::move(garments)});
    });
}
void WardrobeIndex::search(UserAccess a,Json::Value f,HistoryCompletion done) const
{
    if(!validFilters(f,true)){done(error(400,"INVALID_ARGUMENT"));return;}
    retrieve(std::move(a),std::move(f),Json::Value(Json::objectValue),std::move(done));
}
void WardrobeIndex::retrieve(UserAccess a,Json::Value f,Json::Value selection,HistoryCompletion done) const
{
    sync(a,[this,a,f,selection,done](HistoryResult synced){
        if(synced.status!=200){done(synced);return;}
        embed({f["query"].asString()},"query",[this,a,f,selection,done,synced](bool ok,Vectors vectors){
            if(!ok){done(error(503,"EMBEDDING_UNAVAILABLE"));return;}
            Json::Value vector(Json::arrayValue);for(double n:vectors[0])vector.append(n);const auto value=json(vector);
            actor(a,[this,a,f,selection,done,synced,value](Tx tx){
                if(!tx){done(error(503,"VECTOR_STORE_UNAVAILABLE"));return;}
                const std::string sql=R"SQL(
 with candidates as (
 select w.*, (embedding <=> $2::public.vector) as distance,
 row_number() over(partition by category order by
   case when not (colors && array(select jsonb_array_elements_text(coalesce($7::jsonb->'excluded_colors','[]'))))
     and (not coalesce(($7::jsonb->>'dark_only')::boolean,false) or (cardinality(colors)>0 and colors <@ array['BLACK']))
     and (coalesce($7::jsonb->>'conversation_style','')='' or cardinality(styles)=0 or ($7::jsonb->>'conversation_style')=any(styles))
     then 0 else 1 end,
   embedding <=> $2::public.vector,garment_id) as category_rank
 from recommendation.wardrobe_embeddings w where user_id=$1::uuid and availability='AVAILABLE'
 and ($3='' or category=$3) and ($4='' or $4=any(colors)) and ($5='' or $5=any(styles))
 and not (colors && array(select jsonb_array_elements_text(coalesce($7::jsonb->'avoided_colors','[]'))))
 and not (garment_id::text=any(array(select jsonb_array_elements_text(coalesce($7::jsonb->'excluded_garments','[]')))))
 and (coalesce($7::jsonb->>'requested_style','')='' or cardinality(styles)=0 or ($7::jsonb->>'requested_style')=any(styles))
 and (not coalesce(($7::jsonb->>'outfits')::boolean,false) or category in ('TOP','BOTTOM','ONE_PIECE','FOOTWEAR'))
 )
 select jsonb_build_object('id',garment_id,'name',name,'description',description,'category',category,'subcategory',subcategory,
 'colors',colors,'styles',styles,'availability',availability,'revision',revision,
 'updated_at',to_char(source_updated_at at time zone 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"'),
 'image',case when image_id is null then null else jsonb_build_object('id',image_id,'version',image_version) end)::text as garment,
 1-distance as similarity,count(*) over() as total
 from candidates
 where not coalesce(($7::jsonb->>'outfits')::boolean,false) or category_rank<=6
 or garment_id::text=any(array(select jsonb_array_elements_text(coalesce($7::jsonb->'previous_ids','[]'))))
 order by distance,garment_id limit $6::integer
)SQL";
                tx->execSqlAsync(sql,[this,a,tx,done,synced](const Result &rows){
                    Json::Value body;body["items"]=Json::Value(Json::arrayValue);body["matched_count"]=rows.empty()?0:rows[0]["total"].as<unsigned>();
                    body["has_more"]=body["matched_count"].asUInt()>rows.size();body["search_mode"]="semantic";body["sync"]=synced.body;
                    std::vector<Garment> selected;std::vector<std::string> ids;
                    try {for(const auto &row:rows){auto item=parse(row["garment"].as<std::string>());selected.push_back(readGarment(item));ids.push_back(selected.back().id);item["similarity"]=row["similarity"].as<double>();body["items"].append(item);}}
                    catch(const std::exception &){tx->rollback();done(error(503,"VECTOR_STORE_UNAVAILABLE"));return;}
                    tx->setCommitCallback([this,a,body,selected,ids,done](bool committed){
                        if(!committed){done(error(503,"VECTOR_STORE_UNAVAILABLE"));return;}
                        if(ids.empty()){done({200,body});return;}
                        wardrobe_.resolve(a,ids,[body,selected,done](WardrobeResult checked) mutable {
                            if(checked.status!=WardrobeStatus::Found){done(error(status(checked.status),wardrobeCode(checked.status)));return;}
                            if(!same(selected,checked.garments)){done(error(409,"WARDROBE_CHANGED"));return;}
                            for(Json::ArrayIndex i=0;i<body["items"].size();++i){
                                const auto similarity=body["items"][i]["similarity"];
                                body["items"][i]=garmentJson(checked.garments[i]);body["items"][i]["similarity"]=similarity;
                            }
                            done({200,body});
                        });
                    });
                },[tx,done](const std::exception_ptr &){tx->rollback();done(error(503,"VECTOR_STORE_UNAVAILABLE"));},a.userId,value,
                   f.get("category","").asString(),f.get("color","").asString(),f.get("style","").asString(),f.get("limit",10).asUInt(),json(selection));
            });
        },timeout_);
    });
}
}
