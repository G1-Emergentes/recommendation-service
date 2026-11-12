#include "recommendation/infrastructure/FixtureWardrobeClient.h"
#include "recommendation/infrastructure/ConversationTools.h"
#include "recommendation/infrastructure/FashionWebSearch.h"
#include "recommendation/infrastructure/WardrobeIndex.h"
#include "recommendation/infrastructure/StyleKnowledgeJson.h"
#include "recommendation/infrastructure/OutfitExampleJson.h"
#include "recommendation/infrastructure/DigitalClosetWardrobeClient.h"
#include "recommendation/infrastructure/IamIdentityVerifier.h"
#include "recommendation/infrastructure/IamProfileClient.h"
#include "recommendation/infrastructure/NvidiaRecommenderAgent.h"
#include "recommendation/infrastructure/SupabaseRecommendationRepository.h"
#include "recommendation/interfaces/ApiDocumentation.h"
#include "recommendation/interfaces/RecommendationHttp.h"
#include <cstdlib>
#include <drogon/drogon.h>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
std::string environment(const char *name, const char *fallback = "")
{
#ifdef _WIN32
    char *value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0)
        throw std::runtime_error("Could not read service configuration");
    const std::string result = value ? value : fallback;
    std::free(value);
    return result;
#else
    const auto value = std::getenv(name);
    return value ? value : fallback;
#endif
}
} // namespace

int main()
{
    try
    {
        const auto mode = environment("RECOMMENDATION_MODE", "disabled");
        if (mode != "disabled" && mode != "demo" && mode != "live")
            throw std::invalid_argument("RECOMMENDATION_MODE must be disabled, demo or live");
        const auto portText = environment("RECOMMENDATION_PORT", "8085");
        if (portText.empty() || portText.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("RECOMMENDATION_PORT must be an integer from 1 to 65535");
        const auto port = std::stoul(portText);
        if (port < 1 || port > 65535)
            throw std::invalid_argument("RECOMMENDATION_PORT must be from 1 to 65535");

        const auto iamUrl = environment("IAM_BASE_URL", "http://127.0.0.1:8080");
        const auto timeoutText = environment("IAM_TIMEOUT_SECONDS", "25");
        if (timeoutText.empty() || timeoutText.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("IAM_TIMEOUT_SECONDS must be an integer from 1 to 30");
        mirage::recommendation::IamIdentityVerifier identity(iamUrl, std::stod(timeoutText));
        mirage::recommendation::IamProfileClient profile(iamUrl, std::stod(timeoutText));
        mirage::recommendation::SupabaseRecommendationRepository history(
            environment("RECOMMENDATION_SUPABASE_URL"),
            environment("RECOMMENDATION_SUPABASE_SECRET_KEY"), environment("IAM_SUPABASE_URL"));

        const auto engine = environment("RECOMMENDATION_ENGINE", "category_rules");
        if (engine != "category_rules" && engine != "nemotron")
            throw std::invalid_argument("RECOMMENDATION_ENGINE must be category_rules or nemotron");
        std::unique_ptr<mirage::recommendation::RecommenderAgent> agent;
        if (engine == "nemotron")
        {
            const auto modelTimeout = environment("NVIDIA_TIMEOUT_SECONDS", "30");
            if (modelTimeout.empty() ||
                modelTimeout.find_first_not_of("0123456789") != std::string::npos)
                throw std::invalid_argument(
                    "NVIDIA_TIMEOUT_SECONDS must be an integer from 1 to 60");
            agent = std::make_unique<mirage::recommendation::NvidiaRecommenderAgent>(
                environment("NVIDIA_API_KEY"),
                environment("NVIDIA_MODEL", "nvidia/nemotron-3.5-lightning-30b-a3b"),
                environment("NVIDIA_API_ORIGIN", "https://integrate.api.nvidia.com"),
                std::stod(modelTimeout));
        }

        std::unique_ptr<mirage::recommendation::WardrobeClient> wardrobe;
        std::unique_ptr<mirage::recommendation::WardrobeIndex> vectorIndex;
        std::unique_ptr<mirage::recommendation::RecommendationService> service;
        if (mode == "demo")
        {
            wardrobe = std::make_unique<mirage::recommendation::FixtureWardrobeClient>(
                RECOMMENDATION_FIXTURE_PATH);
        }
        if (mode == "live")
        {
            const auto closetTimeout = environment("DIGITAL_CLOSET_TIMEOUT_SECONDS", "10");
            if (closetTimeout.empty() || closetTimeout.find_first_not_of("0123456789") != std::string::npos)
                throw std::invalid_argument("DIGITAL_CLOSET_TIMEOUT_SECONDS must be 1-30");
            wardrobe = std::make_unique<mirage::recommendation::DigitalClosetWardrobeClient>(
                environment("DIGITAL_CLOSET_BASE_URL", "http://127.0.0.1:8081"), std::stod(closetTimeout));
        }
        const auto rag=environment("RECOMMENDATION_RAG_ENABLED","false");
        if(rag!="true" && rag!="false")throw std::invalid_argument("RECOMMENDATION_RAG_ENABLED must be true or false");
        if(rag=="true")
        {
            if(mode!="live" || !wardrobe)throw std::invalid_argument("RAG requires live Digital Closet");
            if(environment("NVIDIA_EMBEDDING_DIMENSIONS","1024")!="1024")throw std::invalid_argument("Vector store requires 1024 embedding dimensions");
            const auto embeddingTimeout=environment("NVIDIA_EMBEDDING_TIMEOUT_SECONDS","30");
            if(embeddingTimeout.empty() || embeddingTimeout.find_first_not_of("0123456789")!=std::string::npos)
                throw std::invalid_argument("Embedding timeout must be an integer");
            vectorIndex=std::make_unique<mirage::recommendation::WardrobeIndex>(*wardrobe,
                environment("NEON_VECTOR_DATABASE_URL"),environment("NVIDIA_EMBEDDING_API_KEY"),
                environment("NVIDIA_EMBEDDING_MODEL","nvidia/nemotron-3.5-embed-1b"),
                environment("NVIDIA_EMBEDDING_API_ORIGIN","https://integrate.api.nvidia.com"),std::stod(embeddingTimeout));
        }
        const auto webTimeout=environment("FASHION_WEB_TIMEOUT_SECONDS","5");
        if(webTimeout.empty() || webTimeout.find_first_not_of("0123456789")!=std::string::npos)
            throw std::invalid_argument("Fashion web timeout must be an integer from 1 to 10");
        mirage::recommendation::FashionWebSearch fashionWeb(environment("FASHION_WEB_ORIGIN","https://raw.githubusercontent.com"),std::stod(webTimeout));
        if (wardrobe)
            service = std::make_unique<mirage::recommendation::RecommendationService>(*wardrobe, profile, agent.get(),
                [&wardrobe,&history,&vectorIndex,&fashionWeb](const mirage::recommendation::UserAccess &access) {
                    return mirage::recommendation::conversationToolExecutor(access,*wardrobe,history,vectorIndex.get(),&fashionWeb);
                }, vectorIndex ? mirage::recommendation::RecommendationService::CandidateLoader{[&vectorIndex](auto access,auto parameters,auto preferences,auto done) {
                        vectorIndex->candidates(std::move(access),std::move(parameters),std::move(preferences),std::move(done));
                    }} : mirage::recommendation::RecommendationService::CandidateLoader{},
                vectorIndex ? mirage::recommendation::RecommendationService::KnowledgeLoader{[&vectorIndex](auto access,auto p,auto done){
                    Json::Value filters;
                    auto query=p.message.empty()?"Outfit para "+p.occasion+" estilo "+p.style:p.message;
                    query+=". Estilo: "+(p.style.empty()?p.conversationStyle:p.style)+". Ocasion: "+p.conversationOccasion;
                    if(query.size()>512){size_t end=512;while(end && (static_cast<unsigned char>(query[end])&0xc0)==0x80)--end;query.resize(end);}
                    filters["query"]=query;filters["style"]=p.style.empty()?p.conversationStyle:p.style;filters["limit"]=3;
                    vectorIndex->searchStyleKnowledge(access,filters,[done](auto result){
                        std::vector<mirage::recommendation::StyleReference> refs;
                        if(result.status!=200){done(false,{});return;}
                        try{for(const auto &item:result.body["items"])refs.push_back(mirage::recommendation::readStyleReference(item));}
                        catch(const std::exception &){done(false,{});return;}
                        done(true,std::move(refs));
                    });
                }} : mirage::recommendation::RecommendationService::KnowledgeLoader{},
                vectorIndex ? mirage::recommendation::RecommendationService::ExampleLoader{[&vectorIndex](auto access,auto p,auto garments,auto done){
                    using mirage::recommendation::Category;
                    bool top=false,bottom=false,piece=false,shoe=false;
                    for(const auto &g:garments){top|=g.category==Category::Top;bottom|=g.category==Category::Bottom;piece|=g.category==Category::OnePiece;shoe|=g.category==Category::Footwear;}
                    if(!shoe || (!piece && !(top&&bottom))){done(true,{});return;}
                    Json::Value filters;auto query=p.message.empty()?"Outfit para "+p.occasion+" estilo "+p.style:p.message;
                    query+=". Estilo: "+(p.style.empty()?p.conversationStyle:p.style)+". Prendas disponibles: ";
                    unsigned count=0;for(const auto &g:garments){if(count++>=5)break;query+=g.name+"; ";}
                    if(query.size()>512){size_t end=512;while(end && (static_cast<unsigned char>(query[end])&0xc0)==0x80)--end;query.resize(end);}
                    filters["query"]=query;filters["structure"]=piece && top && bottom?"":piece?"ONE_PIECE":"SEPARATES";filters["limit"]=3;
                    vectorIndex->searchOutfitExamples(access,filters,[done](auto result){
                        std::vector<mirage::recommendation::OutfitExample> refs;
                        if(result.status!=200){done(false,{});return;}
                        try{for(const auto &item:result.body["items"])refs.push_back(mirage::recommendation::readOutfitExample(item));}
                        catch(const std::exception &){done(false,{});return;}
                        done(true,std::move(refs));
                    });
                }} : mirage::recommendation::RecommendationService::ExampleLoader{});
        auto &app = drogon::app();
        app.setFileTypes({});
        app.setClientMaxBodySize(16 * 1024);
        app.setThreadNum(1);
        mirage::recommendation::registerApiDocumentation(RECOMMENDATION_DOCS_PATH);
        mirage::recommendation::registerRecommendationHttp(mode, service.get(), identity,
                                                           history, agent.get(),vectorIndex.get());
        app.addListener("127.0.0.1", static_cast<unsigned short>(port));
        std::cout << "Recommendation service: http://127.0.0.1:" << port << " (" << mode << ")"
                  << std::endl;
        app.run();
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Startup failed: " << exception.what() << '\n';
        return 1;
    }
}
