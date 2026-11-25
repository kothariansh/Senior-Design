#include <lvgl.h>
#include "dice_frames.hpp"
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <sstream>
#include <chrono>
#include "checkpoint.hpp"
#include "uart_link.h"
#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

namespace {
using namespace catan_game;
// Palette and layout for the 800 x 480 Waveshare display.
constexpr unsigned RED=0x921B24,GOLD=0xE8BB63,CREAM=0xFFF3D6,DARK=0x46151D;
constexpr unsigned MUTED=0xD9BCA9, BORDER=0xA25649, PANEL=0x70202A;
constexpr int WIDTH=800, HEIGHT=480;

constexpr unsigned COLORS[4]={0xEF6461,0x55B6FF,0xFAFAFA,0xF7AE46};
enum class Page { Welcome,Board,Players,Order,Place,Gate,Changes,Home,Trade,Bank,Build,Cards,Account,Awards,Discard,Robber,Victim,Confirm,Storage,Rolling };
Page cancelPage=Page::Home;Page page=Page::Welcome,returnPage=Page::Home;
std::unique_ptr<Game> game;
lv_obj_t* root=nullptr;
lv_timer_t* idleTimer=nullptr;
std::deque<std::function<void()>> callbacks;
std::deque<std::array<lv_point_precise_t,7>> lines;
std::function<void()> confirmed;
std::string message,confirmation;
bool wroomReady=false;
const char *sdStatus="SD: checking...";
std::string uartStatus="No player or NFC event";
std::string statusText(){return std::string(wroomReady?"WROOM: READY":"WROOM: waiting...")+"  |  "+sdStatus+"  |  "+uartStatus;}
lv_obj_t* statusLabel=nullptr;
Hand offer{},request{},discardAmount{};
int partner=-1,giveR=0,takeR=1,batches=1,buildKind=0,site=-1,devKind=0,res1=0,res2=1,hexChoice=0;
int authWho=-1,viewer=-1,tradeApproval=0;
int verifiedTurn=-1;
bool orderAnimation=false;
int animationFrame=0;
lv_obj_t *rollDice[2]={}, *rollCaption=nullptr, *saveBadge=nullptr;
uint32_t savedAt=0,readyAt=0;
bool hasSave=false;bool reportedSaveError=false;
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
 P(Welcome);P(Board);P(Players);P(Order);P(Place);P(Gate);P(Changes);P(Home);P(Trade);P(Bank);P(Build);P(Cards);P(Account);P(Awards);P(Discard);P(Robber);P(Victim);P(Confirm);P(Storage);P(Rolling);
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
checkpoint::Keys keys{};
constexpr unsigned char layouts[6][19]={
 {0x4b, 0x34, 0x13, 0x15, 0x4b, 0x2c, 0x28, 0x38, 0x32, 0x3a, 0x19, 0x25, 0x9, 0x24, 0x6, 0x50, 0x46, 0x1a, 0x3},
 {0x2, 0x26, 0x33, 0x50, 0x3a, 0x25, 0x4b, 0x19, 0x48, 0x9, 0x15, 0x4a, 0x3c, 0x23, 0x34, 0x8, 0x16, 0x24, 0x1b},
 {0x6, 0x3b, 0x3a, 0x3, 0x12, 0x33, 0x49, 0x8, 0x24, 0x48, 0x25, 0x1b, 0x14, 0x3c, 0x49, 0x2a, 0x26, 0x50, 0x15},
 {0x26, 0x13, 0x34, 0x4a, 0x9, 0x35, 0x16, 0x38, 0x4c, 0x28, 0x42, 0x2b, 0x2a, 0x9, 0x15, 0x1b, 0x3, 0x50, 0x34},
 {0x4a, 0x28, 0x49, 0x3b, 0x2c, 0x2b, 0x3, 0x12, 0x3, 0x35, 0x38, 0x1a, 0x36, 0x4, 0x14, 0x19, 0x50, 0x46, 0x25},
 {0x33, 0x22, 0x38, 0x45, 0x4b, 0x1a, 0x9, 0x29, 0x3a, 0x18, 0xb, 0x4, 0x26, 0x4c, 0x50, 0x13, 0x35, 0x24, 0x16},
};
int boardSlot=-1;bool boardLoaded=false;
uint32_t approvalAt=0;bool approving=false;
int enroll=-1;bool started=false,awaitLoad=false;
std::string lastImage,incoming,saveState="No game saved";
uint32_t saveHash=0,loadHash=0,sentAt=0;bool dirty=false;
uint32_t digest(const std::string& s){uint32_t h=2166136261u;for(unsigned char c:s)h=(h^c)*16777619u;return h;}
void updateBadge(){
 if(!saveBadge)return;
 std::string msg;
 if(!wroomReady)msg="Autosave: waiting for MCU";
 else if(strcmp(sdStatus,"SD: READY"))msg=dirty?"Unsaved | SD unavailable":"Autosave: SD unavailable";
 else if(dirty)msg=saveState=="Saving..."?"Auto-saving...":"Unsaved | retry pending";
 else if(hasSave)msg="SD saved | "+std::to_string(lv_tick_elaps(savedAt)/1000)+" s ago";
 else msg="Autosave ready";
 if(msg!=lv_label_get_text(saveBadge))lv_label_set_text(saveBadge,msg.c_str());
}
void persist(bool force=false){
 if(!started||(!boardLoaded&&g().phase==Phase::Registration))return;
 auto data=checkpoint::encode(g(),keys);
 if(!force && data==lastImage)return;
 lastImage=data;dirty=true;saveHash=digest(data);sentAt=lv_tick_get();
 if(strcmp(sdStatus,"SD: READY")){saveState="Unsaved: SD unavailable";updateBadge();return;}
 char line[120];snprintf(line,sizeof line,"BEGIN:%u:%08x\n",(unsigned)data.size(),(unsigned)saveHash);uart_link_send(line);
 for(size_t i=0;i<data.size();i+=80){auto part="DATA:"+data.substr(i,80)+"\n";uart_link_send(part.c_str());}
 uart_link_send("END\n");saveState="Saving...";updateBadge();
}
void sound(const char* kind){static uint32_t session=seed(),seq=0;char cmd[64];
 snprintf(cmd,sizeof cmd,"SOUND:%08x:%u:%s\n",(unsigned)session,(unsigned)++seq,kind);uart_link_send(cmd);}
void refresh(){
 if(redrawPending)return;
 redrawPending=true;
 if(lv_async_call([](void*){render();redrawPending=false;},nullptr)!=LV_RESULT_OK)redrawPending=false;
}
void go(Page next){std::printf("[UI] %s -> %s\n",name(page),name(next));if(next!=Page::Players)enroll=-1;page=next;message.clear();lastInput=lv_tick_get();refresh();}
void error(){sound("ERROR");message=g().error;std::printf("[UI] Rejected: %s\n",message.c_str());refresh();}
lv_obj_t* text(const std::string& s,int x,int y,int w=750,unsigned color=CREAM,const lv_font_t* f=nullptr){
 auto* o=lv_label_create(root);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_label_set_text(o,s.c_str());
 lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_font(o,f?f:font(),0);
 lv_obj_set_style_text_line_space(o,4,0);
 lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);
 return o;
}
// Center the text inside its label, then center the label on the display.
lv_obj_t* centered(const std::string& s,int y,int w=752,unsigned color=CREAM,const lv_font_t* f=nullptr){
 auto* label=text(s,0,y,w,color,f);
 lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
 lv_obj_align(label,LV_ALIGN_TOP_MID,0,y);
 return label;
}
const lv_font_t* titleFont(){
#if LV_FONT_MONTSERRAT_48
 return &lv_font_montserrat_48;
#else
 return bigFont();
#endif
}
lv_obj_t* panel(int x,int y,int w,int h,unsigned color=PANEL){
 auto* p=lv_obj_create(root);lv_obj_remove_style_all(p);
 lv_obj_set_pos(p,x,y);lv_obj_set_size(p,w,h);
 lv_obj_set_style_bg_color(p,lv_color_hex(color),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
 lv_obj_set_style_radius(p,16,0);lv_obj_set_style_border_width(p,1,0);
 lv_obj_set_style_border_color(p,lv_color_hex(BORDER),0);
 lv_obj_remove_flag(p,LV_OBJ_FLAG_SCROLLABLE);lv_obj_remove_flag(p,LV_OBJ_FLAG_CLICKABLE);
 return p;
}
void chrome(){
 auto* bar=panel(0,0,WIDTH,4,GOLD);lv_obj_set_style_radius(bar,0,0);
 auto* footer=panel(0,402,WIDTH,78,DARK);lv_obj_set_style_radius(footer,0,0);
 lv_obj_set_style_border_width(footer,0,0);
}

void bind(lv_obj_t* obj,lv_event_code_t code,std::function<void()> fn){
 callbacks.push_back(std::move(fn));lv_obj_add_event_cb(obj,[](lv_event_t* e){
  if(redrawPending)return;
  lastInput=lv_tick_get();
  auto copy=*static_cast<std::function<void()>*>(lv_event_get_user_data(e));copy();persist();
 },code,&callbacks.back());
}
lv_obj_t* button(const std::string& s,int x,int y,int w,std::function<void()> fn,bool enabled=true,int h=52){
 auto* b=lv_button_create(root);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
 const bool secondary=s=="BACK"||s=="CANCEL"||s=="REJECT"||s=="HIDE / LOCK";
 lv_obj_set_style_bg_color(b,lv_color_hex(secondary?PANEL:GOLD),0);
 lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);
 lv_obj_set_style_radius(b,12,0);lv_obj_set_style_shadow_width(b,0,0);
 lv_obj_set_style_border_width(b,1,0);
 lv_obj_set_style_border_color(b,lv_color_hex(secondary?BORDER:0xF4D48F),0);
 lv_obj_set_style_pad_all(b,6,0);
 lv_obj_set_style_bg_color(b,lv_color_hex(secondary?DARK:0xCFA34D),LV_STATE_PRESSED);
 lv_obj_set_style_bg_color(b,lv_color_hex(0x75454A),LV_STATE_DISABLED);
 lv_obj_set_style_border_color(b,lv_color_hex(0x8B5B60),LV_STATE_DISABLED);
 lv_obj_set_style_opa(b,LV_OPA_COVER,LV_STATE_DISABLED);
 auto* l=lv_label_create(b);lv_obj_set_width(l,w-12);lv_label_set_text(l,s.c_str());lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
 lv_obj_set_style_text_color(l,lv_color_hex(!enabled?MUTED:secondary?CREAM:DARK),0);
 lv_obj_set_style_text_font(l,font(),0);lv_obj_set_style_text_line_space(l,6,0);lv_obj_center(l);
 if(!enabled)lv_obj_add_state(b,LV_STATE_DISABLED);else bind(b,LV_EVENT_CLICKED,std::move(fn));
 return b;
}
void dropdown(const std::vector<std::string>& opts,int value,int x,int y,int w,std::function<void(int)> fn){
 if(opts.empty()){text("No available options",x,y,w);return;}
 std::string s;for(auto& a:opts){if(!s.empty())s+='\n';s+=a;}
 auto* d=lv_dropdown_create(root);lv_obj_set_pos(d,x,y);lv_obj_set_size(d,w,46);lv_dropdown_set_options(d,s.c_str());
 lv_dropdown_set_selected(d,std::max(0,std::min(value,(int)opts.size()-1)));lv_obj_set_style_text_font(d,font(),0);
 lv_obj_set_style_bg_color(d,lv_color_hex(CREAM),0);lv_obj_set_style_text_color(d,lv_color_hex(DARK),0);
 lv_obj_set_style_border_color(d,lv_color_hex(GOLD),0);lv_obj_set_style_border_width(d,1,0);
 lv_obj_set_style_radius(d,10,0);
 auto* list=lv_dropdown_get_list(d);
 if(list){
  lv_obj_set_style_bg_color(list,lv_color_hex(CREAM),0);lv_obj_set_style_text_color(list,lv_color_hex(DARK),0);
  lv_obj_set_style_text_font(list,font(),0);lv_obj_set_style_max_height(list,240,0);
  lv_obj_set_style_bg_color(list,lv_color_hex(GOLD),LV_PART_SELECTED);
  lv_obj_set_style_text_color(list,lv_color_hex(DARK),LV_PART_SELECTED);
 }
 bind(d,LV_EVENT_VALUE_CHANGED,[d,fn]{fn(lv_dropdown_get_selected(d));});
}
void header(const std::string& title){
 auto* label=text(title,24,18,538,GOLD,bigFont());
 lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
 lv_obj_set_height(label,40);
}
void back(Page p=Page::Home){button("BACK",20,411,145,[p]{uart_link_send("ACTION:BACK\n");go(p);});}
void confirm(const std::string& s,std::function<void()> action,Page cancel){confirmation=s;confirmed=std::move(action);cancelPage=cancel;approving=true;approvalAt=lv_tick_get();gate(cancel==Page::Discard?g().discardPlayer():g().active,Page::Confirm);}
void gate(int who,Page next){if(viewer==who && verifiedTurn==g().turn && who>=0){go(next);return;}
 authWho=who;viewer=-1;returnPage=next;go(Page::Gate);}
void route(){
 site=-1;
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
 text("BOARD | R = robber",40,379,380,GOLD,LV_FONT_DEFAULT);
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
 button("CONFIRM PLACEMENT",440,411,335,[]{int selected=site,kind=buildKind;confirm(std::string("Confirm this physical placement?\n")+(kind==0?"Road E":"Vertex V")+std::to_string(selected+1),[selected,kind]{if(g().build(kind,selected)){sound("BUILD");route();}else error();},Page::Place);},site>=0);
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
 if(g().total(offer)==0||g().total(request)==0){sound("ERROR");message="Choose an offer from each player.";refresh();return;}
 tradeApproval=1;confirm("You give:\n"+hand(offer)+"\n\nYou receive:\n"+hand(request),[]{tradeApproval=2;gate(partner,Page::Trade);},Page::Trade);
}
constexpr int landedFrame = dice_art::frames + 1;
const lv_image_dsc_t* dieFace(int style,int value,int frame){
 static lv_image_dsc_t tiles[2][6][dice_art::frames]{};
 auto &d=tiles[style][value-1][frame];
 if(!d.data){
  d.header.magic=LV_IMAGE_HEADER_MAGIC;d.header.cf=LV_COLOR_FORMAT_I4;
  d.header.w=dice_art::side;d.header.h=dice_art::side;d.header.stride=dice_art::side/2;
  d.data=dice_art::packed[style][value-1][frame];d.data_size=sizeof dice_art::packed[0][0][0];
 }
 return &d;
}
void drawRollFrame(){
 if(!rollCaption||!rollDice[0]||!rollDice[1])return;
 for(int j=0;j<2;j++){
  int f=std::max(0,std::min(dice_art::frames-1,animationFrame-2*j));
  float t=1.0f-f/float(dice_art::frames-1), w=t*t;
  int x=278+j*145+int((j?92:-92)*w);
  int y=164-int(std::abs(std::sin(f*.77f))*32*w);
  lv_obj_set_pos(rollDice[j],x,y);
  lv_image_set_src(rollDice[j],dieFace(j,j?g().d2:g().d1,f));
 }
 if(animationFrame>=landedFrame){
  auto score=std::to_string(g().d1)+"  +  "+std::to_string(g().d2)+"  =  "+std::to_string(g().d1+g().d2);
  lv_label_set_text(rollCaption,score.c_str());}
}
void animateRoll(bool order){
 orderAnimation=order;animationFrame=0;sound("DICE");go(Page::Rolling);
 auto* timer=lv_timer_create([](lv_timer_t* t){
  if(page!=Page::Rolling){lv_timer_delete(t);return;}
  ++animationFrame;drawRollFrame();
  if(animationFrame==landedFrame&&!orderAnimation&&g().d1+g().d2==7)sound("ROBBER");
  if(animationFrame>=landedFrame+12){lv_timer_delete(t);if(orderAnimation)go(Page::Order);else route();}
 },65,nullptr);
 if(!timer){if(!order && g().d1+g().d2==7)sound("ROBBER");if(order)go(Page::Order);else route();}
}
void render(){
 if(!root)return;
 saveBadge=nullptr;rollCaption=nullptr;rollDice[0]=rollDice[1]=nullptr;statusLabel=nullptr;lv_obj_clean(root);callbacks.clear();lines.clear();
 if(page!=Page::Welcome)chrome();
 switch(page){
 case Page::Welcome:{
  // All major elements share the same horizontal center (x = 400).
  auto* hero=panel(100,42,600,352,DARK);
  lv_obj_set_style_border_color(hero,lv_color_hex(BORDER),0);
  centered("TEAM 27  /  DIGITAL TRADING",69,552,GOLD,LV_FONT_DEFAULT);
  auto* title=centered("CATAN",119,552,GOLD,titleFont());
  lv_obj_set_style_text_letter_space(title,5,0);
  centered("Build. Trade. Settle.",191,552,CREAM);
  centered("Your table. Your strategy. One shared console.",231,552,MUTED,LV_FONT_DEFAULT);
  auto* start=button("START GAME",0,290,320,[]{uart_link_send("ACTION:START\n");started=true;boardLoaded=false;boardSlot=-1;awaitLoad=false;keys={};enroll=-1;lastImage.clear();dirty=false;hasSave=false;saveHash=0;game.reset(new Game(seed()));viewer=-1;tradeApproval=0;go(Page::Board);},true,60);
  lv_obj_align(start,LV_ALIGN_TOP_MID,0,290);
  statusLabel=centered(statusText(),431,752,GOLD,LV_FONT_DEFAULT);
  lv_obj_set_height(statusLabel,36);lv_label_set_long_mode(statusLabel,LV_LABEL_LONG_DOT);
  button("TEST DICE SOUND",418,367,286,[]{sound("DICE");});
  button("RESUME SAVED GAME",96,367,286,[]{awaitLoad=true;incoming.clear();uart_link_send("LOAD\n");});
  break;
 }
 case Page::Board:
  header("Choose and confirm your board");
  if(boardLoaded)drawBoard();else text("Tap a layout to preview the board.",25,110,370);
  for(int i=0;i<6;i++)button((boardSlot==i?"Selected ":"Layout ")+std::to_string(i+1),420+182*(i%2),80+72*(i/2),174,[i]{
    boardSlot=i;boardLoaded=true;site=-1;
    for(int h=0;h<19;h++){unsigned cell=layouts[i][h];int terrain=cell>>4;
     g().hexes[h].res=terrain==5?-1:terrain;g().hexes[h].number=cell&15;if(terrain==5)g().robber=h;}
    refresh();
  });
  button("CONFIRM BOARD",425,309,345,[]{go(Page::Players);},boardLoaded);
  back(Page::Welcome);break;
 case Page::Players:
  header("Who's playing?");text("Tap an empty slot, then present its NFC card. Tap an added slot to remove.",22,76);
  for(int i=0;i<4;i++){auto* card=button(player(i)+(g().p[i].enabled?"\nADDED\nTap to remove":"\n+ ADD PLAYER"),22+i*194,139,180,[i]{if(g().p[i].enabled){keys[i].clear();g().p[i].enabled=false;enroll=-1;message.clear();}else{enroll=i;message="Scan a card for "+player(i);}refresh();},true,163);lv_obj_set_style_bg_color(card,lv_color_hex(g().p[i].enabled?CREAM:GOLD),0);
   lv_obj_set_style_border_color(card,lv_color_hex(COLORS[i]),0);
   lv_obj_set_style_border_width(card,g().p[i].enabled?4:2,0);}
  centered(std::to_string(g().count())+" / 4 players added",322,752,GOLD);
  statusLabel=centered(statusText(),356,752,MUTED,LV_FONT_DEFAULT);
  lv_obj_set_height(statusLabel,40);lv_label_set_long_mode(statusLabel,LV_LABEL_LONG_DOT);back(Page::Board);
  button("CONTINUE",480,411,292,[]{if(g().start())go(Page::Order);else error();});break;
 case Page::Rolling:{
  header(orderAnimation?"Starting order":"Turn roll");
  panel(65,85,670,265,0x1D4339);
  for(int j=0;j<2;j++)rollDice[j]=lv_image_create(root);
  rollCaption=centered("Rolling...",278,640,CREAM,bigFont());drawRollFrame();break;
 }
 case Page::Order:
  header("Roll to choose the starting player");text("Highest roll starts; play continues clockwise. Reroll highest ties.",22,70);
  for(int j=0;j<(int)g().contenders.size();j++){int i=g().contenders[j];button(player(i)+(g().orderRoll[i]?" | "+std::to_string(g().orderRoll[i]):" | ROLL"),110,119+j*62,570,[i]{if(g().rollOrder(i)){animateRoll(true);}else error();},!g().orderRoll[i]);}
  button("CONFIRM ORDER",445,411,330,[]{if(g().finishOrder())route();else error();});break;
 case Page::Place:placement();break;
 case Page::Gate:
  header("Private player access");centered("Pass the console to "+player(authWho)+".",125,700,GOLD,bigFont());
  centered("Tap your registered NFC card on the reader.",210,700);if(approving)button("CANCEL",250,310,300,[]{confirmed=nullptr;approving=false;tradeApproval=0;route();});break;
 case Page::Changes:
  header(player(g().active)+" | Turn "+std::to_string(g().turn));
  text("Changes since your last turn (already applied):",25,91,740,GOLD);text(hand(g().p[g().active].pending),25,143,740);
  text("Current resources:",25,225,740,GOLD);text(hand(g().p[g().active].hand),25,271,740);
  button("ACKNOWLEDGE & BEGIN TURN",220,411,550,[]{if(g().acknowledge())go(Page::Home);else error();});break;
 case Page::Home:{
  header(player(g().active)+" | Turn "+std::to_string(g().turn));
  text(g().phase==Phase::Roll?"Roll the dice to begin, or play an eligible development card.":"Roll: "+std::to_string(g().d1)+" + "+std::to_string(g().d2)+". Choose an action.",22,66);
  bool actions=g().phase==Phase::Actions;
  button("ROLL DICE",22,113,238,[]{if(g().roll()){animateRoll(false);}else error();},g().phase==Phase::Roll);
  button("PLAYER TRADE",281,113,238,[]{offer.fill(0);request.fill(0);tradeApproval=0;partner=-1;for(int i=0;i<4;i++)if(i!=g().active&&g().p[i].enabled){partner=i;break;}go(Page::Trade);},actions);
  button("BANK & HARBORS",540,113,238,[]{go(Page::Bank);},actions);
  button("BUILD & EXPAND",22,185,238,[]{go(Page::Build);},actions);
  button("YOUR CARDS",281,185,238,[]{go(Page::Cards);});button("YOUR ACCOUNT",540,185,238,[]{go(Page::Account);});
  button("TABLE AWARDS",22,257,238,[]{go(Page::Awards);});button("SAVE / STORAGE",281,257,238,[]{go(Page::Storage);});
  button("HIDE / LOCK",540,257,238,[]{viewer=-1;gate(g().active,Page::Home);});
  text("Board confirmed | Digital development deck",22,337,750,GOLD);
  button("END TURN",470,411,305,[]{confirm("End your turn and hide your account?",[]{if(g().endTurn())route();else error();},Page::Home);},actions);break;
 }
 case Page::Trade:{
  header(tradeApproval==2?player(partner)+" | Review trade":"Player trade");
  if(tradeApproval==2){
   text("You give:\n"+hand(request)+"\n\nYou receive:\n"+hand(offer),25,110,740);
   button("REJECT",22,411,240,[]{approving=false;tradeApproval=0;gate(g().active,Page::Home);});
   button("APPROVE TRADE",450,411,325,[]{if(!approving||lv_tick_elaps(approvalAt)>=60000){sound("ERROR");approving=false;tradeApproval=0;route();return;}approving=false;if(g().trade(partner,offer,request)){sound("TRADE");tradeApproval=0;gate(g().active,Page::Home);}else error();});
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
  back();button("CONFIRM EXCHANGE",435,411,340,[]{confirm("Pay "+std::to_string(batches*g().rate(giveR))+" "+resource[giveR]+" for "+std::to_string(batches)+" "+resource[takeR]+"?",[]{if(g().bankTrade(giveR,takeR,batches)){sound("TRADE");go(Page::Home);}else error();},Page::Bank);});break;
 }
 case Page::Build:
  header("Build & expand");text(hand(g().p[g().active].hand),22,70);
  button("ROAD | brick + lumber",50,123,700,[]{buildKind=0;site=-1;go(Page::Place);});
  button("SETTLEMENT | brick + lumber + wool + grain",50,191,700,[]{buildKind=1;site=-1;go(Page::Place);});
  button("CITY | 2 grain + 3 ore",50,259,700,[]{buildKind=2;site=-1;go(Page::Place);});
  button("DEVELOPMENT | wool + grain + ore",50,327,700,[]{confirm("Buy one SIMULATED development card?\nThis prototype uses a digital deck, not the physical deck.",[]{if(g().buyCard()){sound("TRADE");go(Page::Cards);}else error();},Page::Build);});back();break;
 case Page::Cards:{
  header("Your development cards");
  for(int k=0;k<5;k++)text(std::string(cardName[k])+": "+std::to_string(g().p[g().active].cards[k])+" ready, "+std::to_string(g().p[g().active].fresh[k])+" new",22,67+k*34,445);
  text("New action cards wait until a later turn.\nOne action card per turn. VP cards stay private.",22,255,745);
  std::vector<std::string> kinds(cardName,cardName+4),rs(resource,resource+5);
  dropdown(kinds,devKind,465,67,310,[](int i){devKind=i;refresh();});
  if(devKind==2||devKind==3)dropdown(rs,res1,465,126,310,[](int i){res1=i;});
  if(devKind==2)dropdown(rs,res2,465,185,310,[](int i){res2=i;});
  text("Resources above apply to Plenty / Monopoly.",22,330,745,GOLD);
  back();button("PLAY SELECTED CARD",430,411,345,[]{confirm(std::string("Play ")+cardName[devKind]+"?",[]{if(g().playCard(devKind,res1,res2)){if(g().phase==Phase::Robber)sound("ROBBER");route();}else error();},Page::Cards);});break;
 }
 case Page::Account:{
  header(player(g().active)+" | Private account");text(hand(g().p[g().active].hand),22,67);
  text("Roads: "+std::to_string(g().roads(g().active))+"  Settlements: "+std::to_string(g().pieces(g().active,1))+"  Cities: "+std::to_string(g().pieces(g().active,2)),22,112);
  auto* box=lv_obj_create(root);lv_obj_set_pos(box,20,156);lv_obj_set_size(box,760,225);lv_obj_set_style_bg_color(box,lv_color_hex(DARK),0);
  lv_obj_set_style_border_color(box,lv_color_hex(BORDER),0);lv_obj_set_style_radius(box,12,0);
  lv_obj_set_style_pad_all(box,16,0);
  auto* label=lv_label_create(box);lv_obj_set_width(label,715);std::string history;
  for(auto i=g().p[g().active].history.rbegin();i!=g().p[g().active].history.rend();i++)history+=*i+"\n";
  lv_label_set_text(label,history.empty()?"No transactions yet.":history.c_str());lv_obj_set_style_text_color(label,lv_color_hex(CREAM),0);lv_obj_set_style_text_font(label,font(),0);lv_obj_set_style_text_line_space(label,6,0);back();break;
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
  button("CANCEL",22,411,245,[]{confirmed=nullptr;approving=false;tradeApproval=0;go(cancelPage);});
  button("CONFIRM",450,411,325,[]{auto fn=std::move(confirmed);if(lv_tick_elaps(approvalAt)>=60000){sound("ERROR");approving=false;tradeApproval=0;route();return;}approving=tradeApproval==1;if(fn)fn();});break;
 case Page::Storage:
  header("Persistence & integrations");
  text("Game checkpoint: "+saveState,25,100,740);button("RETRY SAVE",25,170,300,[]{persist(true);refresh();});
  panel(22,260,756,90,DARK);
  text("LIVE HARDWARE STATUS",40,274,720,GOLD,LV_FONT_DEFAULT);
  statusLabel=text(statusText(),40,300,720,CREAM,LV_FONT_DEFAULT);
  lv_obj_set_height(statusLabel,40);lv_label_set_long_mode(statusLabel,LV_LABEL_LONG_DOT);back();break;
 }
 if(page!=Page::Welcome){
  saveBadge=text("",562,21,226,GOLD,LV_FONT_DEFAULT);
  lv_obj_set_style_text_align(saveBadge,LV_TEXT_ALIGN_RIGHT,0);updateBadge();
 }
 if(!message.empty()){
  // A bounded two-line banner keeps errors away from footer action buttons.
  auto* notice=panel(16,354,768,48,0xFFF0D2);
  lv_obj_set_style_border_color(notice,lv_color_hex(GOLD),0);
  lv_obj_add_flag(notice,LV_OBJ_FLAG_CLICKABLE);
  auto* label=text(message,28,360,744,DARK,LV_FONT_DEFAULT);
  lv_obj_set_height(label,38);lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
 }
}
} // namespace

extern "C" void catan_ui_set_status(const char *status){
 if(!status)return;
 std::string line=status;
 if(line=="NFC:OFFLINE"){if(uartStatus!="NFC offline")sound("ERROR");uartStatus="NFC offline";if(approving){approving=false;confirmed=nullptr;tradeApproval=0;route();}refresh();return;}
 if(line.rfind("BOARD:",0)==0)return; // Built-in layouts own board selection.
 if(line.rfind("UID:",0)==0){
  auto uid=line.substr(4);if((uid.size()!=8&&uid.size()!=14)||uid.find_first_not_of("0123456789ABCDEF")!=std::string::npos)return;
  if(approving && lv_tick_elaps(approvalAt)>=60000){sound("ERROR");confirmed=nullptr;approving=false;tradeApproval=0;route();return;}
  if(page==Page::Players&&enroll>=0){
   if(std::find(keys.begin(),keys.end(),uid)!=keys.end()){message="Card already registered. Use a different card.";sound("ERROR");}
   else {keys[enroll]=uid;g().p[enroll].enabled=true;enroll=-1;message.clear();sound("SCAN");persist();}
   refresh();
  }else if(page==Page::Gate&&authWho>=0&&authWho<4){
   if(keys[authWho]==uid){viewer=authWho;verifiedTurn=g().turn;sound("SCAN");go(returnPage);}
   else{message="Incorrect card. Scan the card for "+player(authWho);sound("ERROR");refresh();}
  }return;
 }
 if(line.rfind("SAVE:",0)==0){unsigned h=0;
  if(sscanf(status,"SAVE:OK:%x",&h)==1&&h==saveHash){dirty=false;reportedSaveError=false;hasSave=true;savedAt=lv_tick_get();saveState="Saved to SD";}
  else if(sscanf(status,"SAVE:ERR:%x",&h)==1&&h==saveHash){if(!reportedSaveError)sound("ERROR");reportedSaveError=true;saveState="Save failed; retry pending";dirty=true;}
  updateBadge();if(page==Page::Storage){refresh();}return;}
 if(line.rfind("LOAD:",0)==0){
  if(!awaitLoad||page!=Page::Welcome)return;
  if(line.rfind("LOAD:BEGIN:",0)==0){incoming.clear();unsigned h; if(sscanf(status+11,"%x",&h)==1)loadHash=h;}
  else if(line.rfind("LOAD:DATA:",0)==0 && incoming.size()<4096)incoming+=line.substr(10);
  else if(line=="LOAD:END"||line=="LOAD:EMPTY"){awaitLoad=false;std::unique_ptr<Game> restored(new Game(seed()));checkpoint::Keys k{};
   if(line=="LOAD:END"&&digest(incoming)==loadHash&&checkpoint::decode(incoming,*restored,k)){
    viewer=-1;hasSave=true;savedAt=lv_tick_get();game=std::move(restored);boardLoaded=true;keys=k;started=true;dirty=false;lastImage=incoming;saveState="Restored from SD";tradeApproval=0;confirmed=nullptr;route();
   }else{sound("ERROR");message="No readable game checkpoint on SD.";refresh();}}
  return;}
 if(std::strcmp(status,"WROOM:READY")==0){wroomReady=true;readyAt=lv_tick_get();}
 else if(std::strcmp(status,"SD:CHECKING")==0)sdStatus="SD: checking...";
 else if(std::strcmp(status,"SD:READY")==0)sdStatus="SD: READY";
 else if(std::strcmp(status,"SD:ERROR")==0){if(strcmp(sdStatus,"SD: ERROR"))sound("ERROR");sdStatus="SD: ERROR";if(started&&!lastImage.empty()){dirty=true;saveState="Unsaved: SD unavailable";}}
 else uartStatus=status;
 updateBadge();if(statusLabel)lv_label_set_text(statusLabel,statusText().c_str());
}

extern "C" void catan_ui_init(void){
 if(root){go(Page::Welcome);return;}
 game.reset(new Game(seed()));root=lv_obj_create(nullptr);
 lv_obj_set_style_bg_color(root,lv_color_hex(RED),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
 lv_obj_set_style_bg_grad_color(root,lv_color_hex(DARK),0);lv_obj_set_style_bg_grad_dir(root,LV_GRAD_DIR_VER,0);
 lv_obj_set_style_border_width(root,0,0);
 lv_obj_set_style_pad_all(root,0,0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_screen_load(root);
 lastInput=lv_tick_get();render();
 idleTimer=lv_timer_create([](lv_timer_t*){
  if(approving && lv_tick_elaps(approvalAt)>=60000){sound("ERROR");confirmed=nullptr;approving=false;tradeApproval=0;route();}
  if(wroomReady&&lv_tick_elaps(readyAt)>4500){wroomReady=false;sdStatus="SD: checking...";if(started&&!lastImage.empty()){dirty=true;saveState="Unsaved: SD unavailable";}}
  updateBadge();
  if(dirty&&lv_tick_elaps(sentAt)>2500&&!strcmp(sdStatus,"SD: READY"))persist(true);
  bool privatePage=page==Page::Home||page==Page::Cards||page==Page::Account||page==Page::Trade||page==Page::Bank||page==Page::Build||page==Page::Discard||page==Page::Changes||page==Page::Confirm;
  if(privatePage&&!redrawPending&&lv_tick_elaps(lastInput)>30000){
   viewer=-1;confirmed=nullptr;approving=false;tradeApproval=0;
   if(g().phase==Phase::Discard)gate(g().discardPlayer(),Page::Discard);
   else if(g().phase==Phase::Handoff)gate(g().active,Page::Changes);
   else if(g().phase==Phase::Settlement||g().phase==Phase::SetupRoad||g().phase==Phase::FreeRoad)go(Page::Place);
   else if(g().phase==Phase::Robber||g().phase==Phase::Victim)route();
   else gate(g().active,Page::Home);
  }
 },1000,nullptr);
}
