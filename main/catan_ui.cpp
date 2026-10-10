#include <lvgl.h>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <sstream>
#include <chrono>
#include "catan_game.hpp"
#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

namespace {
using namespace catan_game;
constexpr unsigned RED=0x921B24,GOLD=0xE8BB63,CREAM=0xFFF3D6,DARK=0x65151D;
constexpr unsigned COLORS[4]={0xEF6461,0x55B6FF,0xFAFAFA,0xF7AE46};
enum class Page { Welcome,Board,Players,Order,Place,Gate,Changes,Home,Trade,Bank,Build,Cards,Account,Awards,Discard,Robber,Victim,Confirm,Storage };
Page page=Page::Welcome,returnPage=Page::Home;
std::unique_ptr<Game> game;
lv_obj_t* root=nullptr;
lv_timer_t* idleTimer=nullptr;
std::deque<std::function<void()>> callbacks;
std::deque<std::array<lv_point_precise_t,7>> lines;
std::function<void()> confirmed;
std::string message,confirmation;
Hand offer{},request{},discardAmount{};
int partner=-1,giveR=0,takeR=1,batches=1,buildKind=0,site=-1,devKind=0,res1=0,res2=1,hexChoice=0;
int authWho=-1,viewer=-1,tradeApproval=0;
bool redrawPending=false;
uint32_t lastInput=0;
void render();void route();void gate(int who,Page next);void go(Page next);
Game& g(){return *game;}
uint32_t seed(){
#ifdef ESP_PLATFORM
 return esp_random();
#else
 return static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
#endif
}
const char* name(Page x){
 switch(x){
#define P(x) case Page::x:return #x
 P(Welcome);P(Board);P(Players);P(Order);P(Place);P(Gate);P(Changes);P(Home);P(Trade);P(Bank);P(Build);P(Cards);P(Account);P(Awards);P(Discard);P(Robber);P(Victim);P(Confirm);P(Storage);
#undef P
 }return "Unknown";
}
const lv_font_t* font(){
#if LV_FONT_MONTSERRAT_20
 return &lv_font_montserrat_20;
#else
 return LV_FONT_DEFAULT;
#endif
}
const lv_font_t* bigFont(){
#if LV_FONT_MONTSERRAT_32
 return &lv_font_montserrat_32;
#else
 return font();
#endif
}
std::string player(int i){return "Player "+std::to_string(i+1);}
std::string hand(const Hand& h){std::string s;for(int r=0;r<5;r++){if(r)s+="  |  ";s+=std::string(resource[r])+": "+std::to_string(h[r]);}return s;}
void refresh(){
 if(redrawPending)return;
 redrawPending=true;
 lv_async_call([](void*){redrawPending=false;render();},nullptr);
}
void go(Page next){std::printf("[UI] %s -> %s\n",name(page),name(next));page=next;message.clear();lastInput=lv_tick_get();refresh();}
void error(){message=g().error;std::printf("[UI] Rejected: %s\n",message.c_str());refresh();}
void text(const std::string& s,int x,int y,int w=750,unsigned color=CREAM,const lv_font_t* f=nullptr){
 auto* o=lv_label_create(root);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_label_set_text(o,s.c_str());
 lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_font(o,f?f:font(),0);
}
void bind(lv_obj_t* obj,lv_event_code_t code,std::function<void()> fn){
 callbacks.push_back(std::move(fn));lv_obj_add_event_cb(obj,[](lv_event_t* e){
  if(redrawPending)return;
  lastInput=lv_tick_get();
  auto copy=*static_cast<std::function<void()>*>(lv_event_get_user_data(e));copy();
 },code,&callbacks.back());
}
lv_obj_t* button(const std::string& s,int x,int y,int w,std::function<void()> fn,bool enabled=true,int h=52){
 auto* b=lv_button_create(root);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
 lv_obj_set_style_bg_color(b,lv_color_hex(GOLD),0);lv_obj_set_style_radius(b,12,0);lv_obj_set_style_shadow_width(b,0,0);
 auto* l=lv_label_create(b);lv_obj_set_width(l,w-12);lv_label_set_text(l,s.c_str());lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
 lv_obj_set_style_text_color(l,lv_color_hex(RED),0);lv_obj_set_style_text_font(l,font(),0);lv_obj_center(l);
 if(!enabled)lv_obj_add_state(b,LV_STATE_DISABLED);else bind(b,LV_EVENT_CLICKED,std::move(fn));
 return b;
}
void dropdown(const std::vector<std::string>& opts,int value,int x,int y,int w,std::function<void(int)> fn){
 if(opts.empty()){text("No available options",x,y,w);return;}
 std::string s;for(auto& a:opts){if(!s.empty())s+='\n';s+=a;}
 auto* d=lv_dropdown_create(root);lv_obj_set_pos(d,x,y);lv_obj_set_size(d,w,46);lv_dropdown_set_options(d,s.c_str());
 lv_dropdown_set_selected(d,std::max(0,std::min(value,(int)opts.size()-1)));lv_obj_set_style_text_font(d,font(),0);
 bind(d,LV_EVENT_VALUE_CHANGED,[d,fn]{fn(lv_dropdown_get_selected(d));});
}
void header(const std::string& title){text(title,22,14,750,GOLD,bigFont());}
void back(Page p=Page::Home){button("BACK",20,411,145,[p]{go(p);});}
void confirm(const std::string& s,std::function<void()> action,Page cancel){confirmation=s;confirmed=std::move(action);returnPage=cancel;go(Page::Confirm);}
void gate(int who,Page next){authWho=who;viewer=-1;returnPage=next;go(Page::Gate);}
void route(){
 viewer=-1;site=-1;
 switch(g().phase){
 case Phase::Registration:go(Page::Players);break;
 case Phase::Order:go(Page::Order);break;
 case Phase::Settlement:case Phase::SetupRoad:case Phase::FreeRoad:go(Page::Place);break;
 case Phase::Handoff:gate(g().active,Page::Changes);break;
 case Phase::Discard:discardAmount.fill(0);gate(g().discardPlayer(),Page::Discard);break;
 case Phase::Robber:hexChoice=(g().robber+1)%19;go(Page::Robber);break;
 case Phase::Victim:go(Page::Victim);break;
 default:gate(g().active,Page::Home);break;
 }
}
void drawBoard(){
 const int ox=211,oy=226,sx=34,sy=18;
 const unsigned terrain[6]={0xBB7352,0x346F46,0x86A55A,0xD5B85B,0x84939D,0xCEB99D};
 for(int h=0;h<(int)g().hexes.size();h++){
  auto a=g().hexes[h];lines.push_back({});auto& points=lines.back();
  for(int k=0;k<7;k++){auto v=g().vertices[a.v[k%6]];points[k].x=ox+v.x*sx;points[k].y=oy+v.y*sy;}
  auto* line=lv_line_create(root);lv_line_set_points(line,points.data(),7);lv_obj_set_style_line_width(line,2,0);lv_obj_set_style_line_color(line,lv_color_hex(terrain[a.res<0?5:a.res]),0);
  const char* codes[]={"BR","LU","WO","GR","OR"};
  std::string t="H"+std::to_string(h+1)+"\n"+(a.res<0?"DE":codes[a.res])+" "+(a.number?std::to_string(a.number):"-");if(h==g().robber)t+=" R";
  text(t,ox+a.x*sx-28,oy+a.y*sy-17,64,CREAM,LV_FONT_DEFAULT);
 }
 for(int i=0;i<(int)g().edges.size();i++){
  auto e=g().edges[i];if(e.owner<0&&!(site==i&&(buildKind==0||g().phase==Phase::SetupRoad||g().phase==Phase::FreeRoad)))continue;
  auto a=g().vertices[e.a],b=g().vertices[e.b];lines.push_back({});auto& pts=lines.back();
  pts[0].x=ox+a.x*sx;pts[0].y=oy+a.y*sy;pts[1].x=ox+b.x*sx;pts[1].y=oy+b.y*sy;
  auto* l=lv_line_create(root);lv_line_set_points(l,pts.data(),2);lv_obj_set_style_line_width(l,5,0);lv_obj_set_style_line_color(l,lv_color_hex(e.owner<0?GOLD:COLORS[e.owner]),0);
 }
 for(int v=0;v<(int)g().vertices.size();v++){
  auto a=g().vertices[v];bool selected=site==v&&buildKind!=0;
  if(a.owner<0&&!selected)continue;
  auto* o=lv_obj_create(root);lv_obj_set_size(o,a.level==2?18:13,a.level==2?18:13);lv_obj_set_pos(o,ox+a.x*sx-6,oy+a.y*sy-6);
  lv_obj_set_style_bg_color(o,lv_color_hex(a.owner<0?GOLD:COLORS[a.owner]),0);lv_obj_set_style_border_width(o,selected?3:1,0);lv_obj_set_style_border_color(o,lv_color_hex(GOLD),0);
 }
 text("SAMPLE BOARD | R = robber",40,379,380,GOLD,LV_FONT_DEFAULT);
}
std::string vertexDescription(int v){
 auto a=g().vertices[v];std::string s="V"+std::to_string(v+1)+" (";for(int h:a.hexes)s+="H"+std::to_string(h+1)+" ";s+=")";
 if(a.port==-1)s+=" 3:1";else if(a.port>=0)s+=" "+std::string(resource[a.port])+" 2:1";return s;
}
void placement(){
 bool setup=g().phase==Phase::Settlement||g().phase==Phase::SetupRoad;
 if(setup)buildKind=g().phase==Phase::Settlement?1:0;
 if(g().phase==Phase::FreeRoad)buildKind=0;
 header(player(g().active)+" | "+(buildKind==0?"Place road":buildKind==1?"Place settlement":"Upgrade city"));
 std::vector<int> ids;std::vector<std::string> labels;
 int n=buildKind==0?g().edges.size():g().vertices.size();
 for(int i=0;i<n;i++){
  bool legal=buildKind==0?g().roadLegal(i,setup):buildKind==1?g().settlementLegal(i,setup):(g().vertices[i].owner==g().active&&g().vertices[i].level==1&&g().pieces(g().active,2)<4);
  if(legal){ids.push_back(i);if(buildKind==0){auto e=g().edges[i];labels.push_back("E"+std::to_string(i+1)+": V"+std::to_string(e.a+1)+" - V"+std::to_string(e.b+1));}else labels.push_back(vertexDescription(i));}
 }
 if(std::find(ids.begin(),ids.end(),site)==ids.end())site=ids.empty()?-1:ids[0];
 drawBoard();
 text(setup?"Setup: two rounds in snake order.":g().phase==Phase::FreeRoad?"Road Building: free placement.":"Confirm before paying resources.",440,80,330);
 dropdown(labels,std::find(ids.begin(),ids.end(),site)-ids.begin(),440,146,335,[ids](int v){site=ids[v];refresh();});
 if(site>=0){
  std::string detail;if(buildKind==0){auto e=g().edges[site];detail=vertexDescription(e.a)+"\n"+vertexDescription(e.b);}else detail=vertexDescription(site);
  text(detail,440,207,330);
 }
 text("Only legal locations are listed.\nMatch the highlighted location\non the physical board.",440,285,335);
 button("CONFIRM PLACEMENT",440,411,335,[]{int selected=site,kind=buildKind;confirm(std::string("Confirm this physical placement?\n")+(kind==0?"Road E":"Vertex V")+std::to_string(selected+1),[selected,kind]{if(g().build(kind,selected))route();else error();},Page::Place);},site>=0);
 if(!setup&&g().phase!=Phase::FreeRoad)back(Page::Build);
}
void resourcePicker(Hand& amounts,int y,int who,const char* title){
 text(title,25,y,740,GOLD);
 for(int r=0;r<5;r++){
  int x=25+r*154;text(std::string(resource[r])+": "+std::to_string(amounts[r]),x,y+30,150);
  button("-",x,y+62,60,[&amounts,r]{amounts[r]=std::max(0,amounts[r]-1);refresh();},true,40);
  button("+",x+72,y+62,60,[&amounts,r,who]{if(amounts[r]<g().p[who].hand[r])amounts[r]++;refresh();},true,40);
 }
}
void tradeReview(){
 if(g().total(offer)==0||g().total(request)==0){message="Choose an offer from each player.";refresh();return;}
 tradeApproval=1;confirm("You give:\n"+hand(offer)+"\n\nYou receive:\n"+hand(request),[]{tradeApproval=2;gate(partner,Page::Trade);},Page::Trade);
}
void render(){
 if(!root)return;
 lv_obj_clean(root);callbacks.clear();lines.clear();
 switch(page){
 case Page::Welcome:
  text("TEAM 27 | DIGITAL TRADING",170,40,570,GOLD);text("CATAN",300,115,400,GOLD,bigFont());
  text("Build. Trade. Settle.",255,200,500);button("START GAME",245,285,310,[]{game.reset(new Game(seed()));viewer=-1;tradeApproval=0;go(Page::Board);});
  text("Prototype: sample board, simulated cards, no saved games",85,409,700,GOLD);break;
 case Page::Board:
  // Intentionally preserve the requested board-recognition bypass.
  button("BACK",100,208,240,[]{go(Page::Welcome);});button("CONTINUE",410,208,300,[]{go(Page::Players);});break;
 case Page::Players:
  header("Who's playing?");text("Choose 3-4 players. Slot order is clockwise seating order.",22,76);
  for(int i=0;i<4;i++){auto* card=button(player(i)+(g().p[i].enabled?"\nADDED - tap to remove":"\n+ ADD PLAYER"),22+i*194,139,180,[i]{g().p[i].enabled=!g().p[i].enabled;message.clear();refresh();},true,163);lv_obj_set_style_bg_color(card,lv_color_hex(g().p[i].enabled?CREAM:GOLD),0);}
  text(std::to_string(g().count())+" / 4 players added",280,325,450,GOLD);back(Page::Board);
  button("CONTINUE",480,411,292,[]{if(g().start())go(Page::Order);else error();});break;
 case Page::Order:
  header("Roll to choose the starting player");text("Highest roll starts; play continues clockwise. Reroll highest ties.",22,70);
  for(int j=0;j<(int)g().contenders.size();j++){int i=g().contenders[j];button(player(i)+(g().orderRoll[i]?" | "+std::to_string(g().orderRoll[i]):" | ROLL"),110,119+j*62,570,[i]{if(g().rollOrder(i))refresh();else error();},!g().orderRoll[i]);}
  button("CONFIRM ORDER",445,411,330,[]{if(g().finishOrder())route();else error();});break;
 case Page::Place:placement();break;
 case Page::Gate:
  header("Private player access");text("Pass the console to "+player(authWho)+".",90,125,650,GOLD,bigFont());
  text("SIMULATION: this button substitutes for an NFC card read.\nIt does not authenticate a real person.",90,210,640);
  button("SIMULATE CARD: "+player(authWho),120,309,560,[]{viewer=authWho;Page next=returnPage;go(next);});break;
 case Page::Changes:
  header(player(g().active)+" | Turn "+std::to_string(g().turn));
  text("Changes since your last turn (already applied):",25,91,740,GOLD);text(hand(g().p[g().active].pending),25,143,740);
  text("Current resources:",25,225,740,GOLD);text(hand(g().p[g().active].hand),25,271,740);
  button("ACKNOWLEDGE & BEGIN TURN",220,411,550,[]{if(g().acknowledge())go(Page::Home);else error();});break;
 case Page::Home:{
  header(player(g().active)+" | Turn "+std::to_string(g().turn));
  text(g().phase==Phase::Roll?"Roll the dice to begin, or play an eligible development card.":"Roll: "+std::to_string(g().d1)+" + "+std::to_string(g().d2)+". Choose an action.",22,66);
  bool actions=g().phase==Phase::Actions;
  button("ROLL DICE",22,113,238,[]{if(g().roll())route();else error();},g().phase==Phase::Roll);
  button("PLAYER TRADE",281,113,238,[]{offer.fill(0);request.fill(0);tradeApproval=0;partner=-1;for(int i=0;i<4;i++)if(i!=g().active&&g().p[i].enabled){partner=i;break;}go(Page::Trade);},actions);
  button("BANK & HARBORS",540,113,238,[]{go(Page::Bank);},actions);
  button("BUILD & EXPAND",22,185,238,[]{go(Page::Build);},actions);
  button("YOUR CARDS",281,185,238,[]{go(Page::Cards);});button("YOUR ACCOUNT",540,185,238,[]{go(Page::Account);});
  button("TABLE AWARDS",22,257,238,[]{go(Page::Awards);});button("SAVE / STORAGE",281,257,238,[]{go(Page::Storage);});
  button("HIDE / LOCK",540,257,238,[]{gate(g().active,Page::Home);});
  text("Demo mode: sample board + digital development deck",22,337,750,GOLD);
  button("END TURN",470,411,305,[]{confirm("End your turn and hide your account?",[]{if(g().endTurn())route();else error();},Page::Home);},actions);break;
 }
 case Page::Trade:{
  header(tradeApproval==2?player(partner)+" | Review trade":"Player trade");
  if(tradeApproval==2){
   text("You give:\n"+hand(request)+"\n\nYou receive:\n"+hand(offer),25,110,740);
   button("REJECT",22,411,240,[]{tradeApproval=0;gate(g().active,Page::Home);});
   button("APPROVE TRADE",450,411,325,[]{if(g().trade(partner,offer,request)){tradeApproval=0;gate(g().active,Page::Home);}else error();});
  }else{
   std::vector<int> ids;std::vector<std::string> names;for(int i=0;i<4;i++)if(i!=g().active&&g().p[i].enabled){ids.push_back(i);names.push_back(player(i));}
   dropdown(names,std::find(ids.begin(),ids.end(),partner)-ids.begin(),500,62,270,[ids](int i){partner=ids[i];request.fill(0);refresh();});
   resourcePicker(offer,108,g().active,"YOU OFFER");
   // Proposal quantities should not disclose the partner's balance. Limit to 19 here; validate on commit.
   text("YOU REQUEST",25,246,740,GOLD);
   for(int r=0;r<5;r++){int x=25+r*154;text(std::string(resource[r])+": "+std::to_string(request[r]),x,276,150);
    button("-",x,308,60,[r]{request[r]=std::max(0,request[r]-1);refresh();},true,40);
    button("+",x+72,308,60,[r]{request[r]=std::min(19,request[r]+1);refresh();},true,40);}
   back();button("REVIEW & APPROVE",450,411,325,[]{tradeReview();});
  }break;
 }
 case Page::Bank:{
  header("Bank & harbors");text(hand(g().p[g().active].hand),22,70);std::vector<std::string> opts(resource,resource+5);
  text("Give",25,127,200,GOLD);dropdown(opts,giveR,25,163,330,[](int i){giveR=i;refresh();});text("Receive",425,127,200,GOLD);dropdown(opts,takeR,425,163,330,[](int i){takeR=i;refresh();});
  text("Your rate: "+std::to_string(g().rate(giveR))+":1. Receive "+std::to_string(batches)+"; pay "+std::to_string(batches*g().rate(giveR))+".",25,242);
  button("-",260,302,95,[]{batches=std::max(1,batches-1);refresh();});button("+",410,302,95,[]{batches=std::min(19,batches+1);refresh();});
  back();button("CONFIRM EXCHANGE",435,411,340,[]{confirm("Pay "+std::to_string(batches*g().rate(giveR))+" "+resource[giveR]+" for "+std::to_string(batches)+" "+resource[takeR]+"?",[]{if(g().bankTrade(giveR,takeR,batches))go(Page::Home);else error();},Page::Bank);});break;
 }
 case Page::Build:
  header("Build & expand");text(hand(g().p[g().active].hand),22,70);
  button("ROAD | brick + lumber",50,123,700,[]{buildKind=0;site=-1;go(Page::Place);});
  button("SETTLEMENT | brick + lumber + wool + grain",50,191,700,[]{buildKind=1;site=-1;go(Page::Place);});
  button("CITY | 2 grain + 3 ore",50,259,700,[]{buildKind=2;site=-1;go(Page::Place);});
  button("DEVELOPMENT | wool + grain + ore",50,327,700,[]{confirm("Buy one SIMULATED development card?\nThis prototype uses a digital deck, not the physical deck.",[]{if(g().buyCard())go(Page::Cards);else error();},Page::Build);});back();break;
 case Page::Cards:{
  header("Your development cards");
  for(int k=0;k<5;k++)text(std::string(cardName[k])+": "+std::to_string(g().p[g().active].cards[k])+" ready, "+std::to_string(g().p[g().active].fresh[k])+" new",22,67+k*34,445);
  text("New action cards wait until a later turn.\nOne action card per turn. VP cards stay private.",22,255,745);
  std::vector<std::string> kinds(cardName,cardName+4),rs(resource,resource+5);
  dropdown(kinds,devKind,465,67,310,[](int i){devKind=i;refresh();});
  if(devKind==2||devKind==3)dropdown(rs,res1,465,126,310,[](int i){res1=i;});
  if(devKind==2)dropdown(rs,res2,465,185,310,[](int i){res2=i;});
  text("Resources above apply to Plenty / Monopoly.",22,330,745,GOLD);
  back();button("PLAY SELECTED CARD",430,411,345,[]{confirm(std::string("Play ")+cardName[devKind]+"?",[]{if(g().playCard(devKind,res1,res2))route();else error();},Page::Cards);});break;
 }
 case Page::Account:{
  header(player(g().active)+" | Private account");text(hand(g().p[g().active].hand),22,67);
  text("Roads: "+std::to_string(g().roads(g().active))+"  Settlements: "+std::to_string(g().pieces(g().active,1))+"  Cities: "+std::to_string(g().pieces(g().active,2)),22,112);
  auto* box=lv_obj_create(root);lv_obj_set_pos(box,20,156);lv_obj_set_size(box,760,225);lv_obj_set_style_bg_color(box,lv_color_hex(DARK),0);
  auto* label=lv_label_create(box);lv_obj_set_width(label,715);std::string history;
  for(auto i=g().p[g().active].history.rbegin();i!=g().p[g().active].history.rend();i++)history+=*i+"\n";
  lv_label_set_text(label,history.empty()?"No transactions yet.":history.c_str());lv_obj_set_style_text_color(label,lv_color_hex(CREAM),0);back();break;
 }
 case Page::Awards:{
  header("Table awards");text("Largest Army: "+(g().army<0?std::string("unassigned"):player(g().army)),22,88);
  text("Longest Road: "+(g().longest<0?std::string("unassigned"):player(g().longest)),22,140);
  text("Longest Road is manually adjudicated by the table.\nConfirm eligibility on the physical board (at least 5 segments).\nWinner determination is not automated.",22,203);
  std::vector<int> ids{-1};std::vector<std::string> opts{"Unassigned"};for(int i=0;i<4;i++)if(g().p[i].enabled){ids.push_back(i);opts.push_back(player(i));}
  dropdown(opts,std::find(ids.begin(),ids.end(),g().longest)-ids.begin(),22,313,750,[ids](int n){int who=ids[n];confirm("Record Longest Road as "+(who<0?std::string("unassigned"):player(who))+"?",[who]{g().longest=who;go(Page::Awards);},Page::Awards);});back();break;
 }
 case Page::Discard:{
  int who=g().discardPlayer();header(player(who)+" | Discard after a 7");text("Required: "+std::to_string(g().owed[who])+"  Selected: "+std::to_string(g().total(discardAmount)),22,80);
  text(hand(g().p[who].hand),22,126);resourcePicker(discardAmount,188,who,"SELECT RESOURCES TO RETURN TO BANK");
  button("CONFIRM DISCARD",390,411,385,[who]{confirm("Return these resources?\n"+hand(discardAmount),[who]{if(g().discard(who,discardAmount))route();else error();},Page::Discard);});break;
 }
 case Page::Robber:{
  header(player(g().active)+" | Move robber");site=-1;drawBoard();std::vector<int> ids;std::vector<std::string> opts;
  for(int i=0;i<19;i++)if(i!=g().robber){ids.push_back(i);auto h=g().hexes[i];opts.push_back("H"+std::to_string(i+1)+" "+(h.res<0?"Desert":resource[h.res]));}
  dropdown(opts,std::find(ids.begin(),ids.end(),hexChoice)-ids.begin(),440,140,335,[ids](int i){hexChoice=ids[i];});
  text("Move the physical robber to\nthe same selected hex.",440,229,330);
  button("CONFIRM ROBBER",440,411,335,[]{if(g().moveRobber(hexChoice))route();else error();});break;
 }
 case Page::Victim:
  header("Choose a player to rob");text("One resource is selected randomly from their hand.",22,82);
  {auto ids=g().victims();for(int j=0;j<(int)ids.size();j++){int who=ids[j];button(player(who),125,145+j*72,550,[who]{confirm("Steal one random resource from "+player(who)+"?",[who]{if(g().steal(who))route();else error();},Page::Victim);});}}break;
 case Page::Confirm:
  header("Confirm action");text(confirmation,30,100,740);
  button("CANCEL",22,411,245,[]{confirmed=nullptr;go(returnPage);});
  button("CONFIRM",450,411,325,[]{auto fn=confirmed;if(fn)fn();});break;
 case Page::Storage:
  header("Persistence & integrations");text("SD persistence is not connected in this build.\nA reset or power loss clears the game.\n\nBoard recognition / Bluetooth: pending integration.\nNFC: simulated card button only.\nSound: not connected; all feedback is visual.",25,100,740);back();break;
 }
 if(!message.empty())text(message,22,382,752,GOLD,LV_FONT_DEFAULT);
}
} // namespace

extern "C" void catan_ui_init(void){
 if(root){go(Page::Welcome);return;}
 game.reset(new Game(seed()));root=lv_obj_create(nullptr);
 lv_obj_set_style_bg_color(root,lv_color_hex(RED),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
 lv_obj_set_style_pad_all(root,0,0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_screen_load(root);
 lastInput=lv_tick_get();render();
 idleTimer=lv_timer_create([](lv_timer_t*){
  bool privatePage=page==Page::Home||page==Page::Cards||page==Page::Account||page==Page::Trade||page==Page::Bank||page==Page::Build||page==Page::Discard||page==Page::Changes||page==Page::Confirm;
  if(privatePage&&!redrawPending&&lv_tick_elaps(lastInput)>30000){
   confirmed=nullptr;tradeApproval=0;
   if(g().phase==Phase::Discard)gate(g().discardPlayer(),Page::Discard);
   else if(g().phase==Phase::Handoff)gate(g().active,Page::Changes);
   else if(g().phase==Phase::Settlement||g().phase==Phase::SetupRoad||g().phase==Phase::FreeRoad)go(Page::Place);
   else if(g().phase==Phase::Robber||g().phase==Phase::Victim)route();
   else gate(g().active,Page::Home);
  }
 },1000,nullptr);
}
