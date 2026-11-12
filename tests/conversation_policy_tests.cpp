#include "recommendation/application/ConversationPolicy.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>
using namespace mirage::recommendation;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main()
{
    std::vector<Garment> wardrobe{
        {"top",Category::Top,true,{}, {"BLACK"},"Polo negro",""},
        {"bottom",Category::Bottom,true,{}, {"BLACK"},"Pantalon negro",""},
        {"shoe",Category::Footwear,true,{}, {"BLACK"},"Botines negros",""},
        {"other-shoe",Category::Footwear,true,{}, {"BLACK"},"Zapatillas negras",""},
        {"white",Category::Top,true,{}, {"WHITE"},"Polo blanco",""},
        {"unknown",Category::Top,true,{}, {},"Polo sin color confirmado",""},
        {"blue",Category::Bottom,true,{}, {"BLUE"},"Jean azul",""}};
    RecommendationParameters p;p.message="Quiero un outfit";
    AgentResult d{AgentStatus::Generated,{{{"top","bottom","shoe"},"Base negra."}},"Aqui tienes tres outfits inventados."};
    d.action="RECOMMEND";
    check(validConversationDecision(d,p,wardrobe),"Valid recommendation");
    const auto reply=conversationReply(d,p,wardrobe);
    check(reply.find("Polo negro")!=std::string::npos && reply.find("Pantalon negro")!=std::string::npos &&
          reply.find("tres")==std::string::npos,"Build user-facing proposals from validated data, not prose");
    auto bad=d;bad.outfits.clear();check(!validConversationDecision(bad,p,wardrobe),"Reject prose-only recommendation");
    bad=d;bad.action="CHAT";check(!validConversationDecision(bad,p,wardrobe),"Reject outfits on chat action");
    d.darkOnly=true;check(validConversationDecision(d,p,wardrobe),"Confirmed black palette");
    bad=d;bad.outfits[0].garmentIds[1]="blue";check(!validConversationDecision(bad,p,wardrobe),"BLUE does not prove dark blue");
    bad=d;bad.outfits[0].garmentIds[0]="white";check(!validConversationDecision(bad,p,wardrobe),"White is not dark");
    d.darkOnly=false;d.excludedColors={"WHITE"};
    bad=d;bad.outfits[0].garmentIds[0]="white";check(!validConversationDecision(bad,p,wardrobe),"Respect excluded colors");
    bad=d;bad.outfits[0].garmentIds[0]="unknown";check(!validConversationDecision(bad,p,wardrobe),"Unknown color cannot satisfy exclusion");
    check(explicitExcludedColors("Un outfit sin blanco ni beige")==std::vector<std::string>{"BEIGE","WHITE"},"Read conjunctions in explicit color exclusions");
    check(explicitExcludedColors("Puedes usar blanco; sin restricciones de color").empty(),"Do not turn permission or generic restrictions into exclusions");
    check(requestsNewOutfit("Quiero un outfit sin blanco") && !requestsNewOutfit("Que prendas negras tengo?"),"Generation routing stays separate from inventory reads");
    check(explicitRelaxDarkRequest("Puedes relajar que sea todo oscuro") && !explicitRelaxDarkRequest("No puedes relajar que sea todo oscuro"),"Only explicit affirmative relaxation overrides old flags");
    check(explicitDarkRequest("Quiero un outfit sin blanco bien oscuro"),"White exclusion does not negate dark request");
    check(explicitDarkRequest("Ahora quiero algo oscuro pero llamativo") && !explicitDarkRequest("Quiero un outfit, puedes relajar que sea oscuro"),"Explicit dark activation respects relaxation wording");
    p.previousOutfits=d.outfits;
    p.message="Otra opci\xC3\xB3n";
    check(!validConversationDecision(d,p,wardrobe),"Another option cannot repeat the prior set of IDs");
    bad=d;std::reverse(bad.outfits[0].garmentIds.begin(),bad.outfits[0].garmentIds.end());
    check(!validConversationDecision(bad,p,wardrobe),"Reordering garments does not make a new outfit");
    bad=d;bad.outfits[0].garmentIds[2]="other-shoe";
    check(validConversationDecision(bad,p,wardrobe),"Changing one real garment supplies a distinct alternative");
    p.message="Quiero el mismo outfit";
    check(validConversationDecision(d,p,wardrobe),"Explicit repetition remains possible");
    p.message="Quiero un outfit";
    p.message="Otra opcion";
    check(hasOutfitCandidate(wardrobe,p),"A second black shoe allows another combination");
    auto only=std::vector<Garment>{wardrobe[0],wardrobe[1],wardrobe[2]};
    check(!hasOutfitCandidate(only,p),"One already-shown combination cannot supply another option");
    only.pop_back();check(!hasOutfitCandidate(only,p),"No complete outfit without footwear");
    p.message="Quiero un outfit";
    d.action="MODIFY";d.referenceOutfit=1;d.changeCategory=Category::Footwear;
    check(!validConversationDecision(d,p,wardrobe),"A modification must actually change a garment");
    d.outfits[0].garmentIds[2]="other-shoe";
    check(validConversationDecision(d,p,wardrobe),"Change shoes while preserving the rest");
    bad=d;bad.outfits[0].garmentIds[1]="blue";check(!validConversationDecision(bad,p,wardrobe),"Do not change trousers when modifying shoes");
    d.action="EXPLAIN";d.outfits.clear();d.changeCategory.reset();
    check(validConversationDecision(d,p,wardrobe),"Explain a saved option without creating outfits");
    check(conversationReply(d,p,wardrobe).find("Botines negros")!=std::string::npos,"Explain original saved shoes");
    d.referenceOutfit=3;check(!validConversationDecision(d,p,wardrobe),"Do not invent an option reference");
    d.action="CLARIFY";d.referenceOutfit=0;d.darkOnly=true;
    check(validConversationDecision(d,p,wardrobe),"Clarify an impossible palette without proposing an invalid outfit");
    auto limited=wardrobe;
    limited.erase(limited.begin()+1);
    d.assistantMessage="Usa beige (ID: bottom)";
    const auto clarification=conversationReply(d,p,limited);
    check(clarification.find("confirmado")!=std::string::npos && clarification.find("ID:")==std::string::npos &&
          clarification.find("beige")==std::string::npos,"Explain missing dark palette without invented alternatives or IDs");
    std::cout<<"Conversation policy: intentions, palette, exclusions, references and modifications passed\n";
}
