#pragma once
// Portable prototype rules engine. No LVGL, NFC or filesystem dependencies.
#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <numeric>
#include <cmath>
#include <cstdint>

namespace catan_game {
using Hand = std::array<int,5>;
static const char* const resource[] = {"Brick","Lumber","Wool","Grain","Ore"};
static const char* const cardName[] = {"Knight","Road Building","Year of Plenty","Monopoly","Victory Point"};
enum class Phase { Registration, Order, Settlement, SetupRoad, Handoff, Roll, Actions, Discard, Robber, Victim, FreeRoad };
struct Vertex { int x,y,owner=-1,level=0,port=-2; std::vector<int> hexes,edges; };
struct Edge { int a,b,owner=-1; };
struct Hex { int x,y,res,number; std::array<int,6> v; };
struct Player {
 bool enabled=false; Hand hand{}; std::array<int,5> cards{},fresh{};
 std::vector<std::string> history; Hand pending{}; int knights=0;
};
struct Game {
 std::array<Player,4> p{}; Hand bank{{19,19,19,19,19}};
 std::vector<Vertex> vertices; std::vector<Edge> edges; std::vector<Hex> hexes;
 std::array<int,4> orderRoll{},owed{}; std::vector<int> contenders,order;
 std::vector<int> deck; std::mt19937 rng;
 Phase phase=Phase::Registration, resume=Phase::Actions;
 int active=0, setupStep=0, setupVertex=-1, turn=1, robber=9, d1=0,d2=0,freeRoads=0;
 int longest=-1,army=-1; bool devPlayed=false; std::string error;
 explicit Game(uint32_t seed=1):rng(seed) { makeBoard(); makeDeck(); }
 bool fail(const std::string& s) { error=s; return false; }
 void note(int who,const std::string& s) {
  auto& h=p[who].history; h.push_back(s); if(h.size()>40)h.erase(h.begin());
 }
 void change(int who,int r,int n,const char* reason) {
  p[who].hand[r]+=n; p[who].pending[r]+=n;
  note(who,std::string(reason)+": "+(n>=0?"+":"")+std::to_string(n)+" "+resource[r]);
 }
 int total(const Hand& h)const {return std::accumulate(h.begin(),h.end(),0);}
 int count()const {int n=0; for(auto& a:p)n+=a.enabled; return n;}
 int die(){return std::uniform_int_distribution<int>(1,6)(rng);}
 void makeDeck(){ for(int k=0;k<5;k++)for(int n=0;n<(k==0?14:k==4?5:2);n++)deck.push_back(k); std::shuffle(deck.begin(),deck.end(),rng); }
 void makeBoard(){
  const int resources[19]={1,2,3,0,4,1,2,3,0,-1,4,1,2,3,0,4,1,2,3};
  const int numbers[19]={5,2,6,3,8,10,9,12,11,0,4,8,10,9,4,5,6,3,11};
  const int dx[6]={1,0,-1,-1,0,1},dy[6]={1,2,1,-1,-2,-1};
  for(int r=-2;r<=2;r++)for(int q=std::max(-2,-r-2);q<=std::min(2,-r+2);q++){
   int id=static_cast<int>(hexes.size()); Hex h{2*q+r,3*r,resources[id],numbers[id],{}};
   for(int k=0;k<6;k++){
    int x=h.x+dx[k],y=h.y+dy[k],v=0;
    while(v<(int)vertices.size()&&(vertices[v].x!=x||vertices[v].y!=y))v++;
    if(v==(int)vertices.size())vertices.push_back(Vertex{x,y,-1,0,-2,{},{}});
    h.v[k]=v; vertices[v].hexes.push_back(id);
   }
   for(int k=0;k<6;k++){
    int a=h.v[k],b=h.v[(k+1)%6]; if(a>b)std::swap(a,b); int e=0;
    while(e<(int)edges.size()&&(edges[e].a!=a||edges[e].b!=b))e++;
    if(e==(int)edges.size()){ edges.push_back({a,b,-1});vertices[a].edges.push_back(e);vertices[b].edges.push_back(e); }
   }
   hexes.push_back(h);
  }
  // Nine sample harbors on disjoint coast edges; this is NOT a scanned board.
  std::vector<int> coast;
  for(int e=0;e<(int)edges.size();e++){
   int shared=0; for(int h:vertices[edges[e].a].hexes)
    if(std::find(vertices[edges[e].b].hexes.begin(),vertices[edges[e].b].hexes.end(),h)!=vertices[edges[e].b].hexes.end())shared++;
   if(shared==1)coast.push_back(e);
  }
  std::sort(coast.begin(),coast.end(),[&](int a,int b){
   auto A=edges[a],B=edges[b]; return std::atan2(vertices[A.a].y+vertices[A.b].y,vertices[A.a].x+vertices[A.b].x)<std::atan2(vertices[B.a].y+vertices[B.b].y,vertices[B.a].x+vertices[B.b].x);
  });
  const int ports[9]={-1,0,-1,1,2,-1,3,4,-1};
  for(int k=0;k<9;k++){auto e=edges[coast[k*coast.size()/9]];vertices[e.a].port=vertices[e.b].port=ports[k];}
 }
 bool start(){
  if(count()<3||count()>4)return fail("Select at least 3 players (maximum 4).");
  contenders.clear(); for(int i=0;i<4;i++)if(p[i].enabled)contenders.push_back(i);
  orderRoll.fill(0); phase=Phase::Order; return true;
 }
 bool rollOrder(int who){
  if(phase!=Phase::Order||std::find(contenders.begin(),contenders.end(),who)==contenders.end()||orderRoll[who])return fail("That player cannot roll now.");
  d1=die();d2=die();orderRoll[who]=d1+d2;return true;
 }
 bool finishOrder(){
  if(phase!=Phase::Order)return fail("Not choosing order.");
  int hi=0;for(int i:contenders){if(!orderRoll[i])return fail("Each listed player must roll.");hi=std::max(hi,orderRoll[i]);}
  std::vector<int> ties;for(int i:contenders)if(orderRoll[i]==hi)ties.push_back(i);
  if(ties.size()>1){contenders=ties;orderRoll.fill(0);return fail("Highest roll tied. Only tied players roll again.");}
  order.clear();for(int k=0;k<4;k++){int i=(ties[0]+k)%4;if(p[i].enabled)order.push_back(i);}
  active=order[0];phase=Phase::Settlement;return true;
 }
 int pieces(int who,int level)const {int n=0;for(auto& v:vertices)n+=(v.owner==who&&v.level==level);return n;}
 int roads(int who)const {int n=0;for(auto& e:edges)n+=(e.owner==who);return n;}
 bool affordable(const Hand& cost)const {for(int r=0;r<5;r++)if(p[active].hand[r]<cost[r])return false;return true;}
 void pay(const Hand& cost){for(int r=0;r<5;r++)if(cost[r]){change(active,r,-cost[r],"Purchase");bank[r]+=cost[r];}}
 bool settlementLegal(int v,bool initial)const {
  if(v<0||v>=(int)vertices.size()||vertices[v].owner!=-1||pieces(active,1)>=5)return false;
  bool linked=false;
  for(int e:vertices[v].edges){auto a=edges[e];int other=a.a==v?a.b:a.a;if(vertices[other].owner!=-1)return false;linked|=a.owner==active;}
  return initial||linked;
 }
 bool roadLegal(int id,bool initial=false)const {
  if(id<0||id>=(int)edges.size()||edges[id].owner!=-1||roads(active)>=15)return false;
  auto e=edges[id];if(initial)return e.a==setupVertex||e.b==setupVertex;
  for(int v:{e.a,e.b}){
   if(vertices[v].owner==active)return true;
   if(vertices[v].owner!=-1)continue;
   for(int j:vertices[v].edges)if(edges[j].owner==active)return true;
  }return false;
 }
 bool build(int kind,int id){ // 0 road, 1 settlement, 2 city
  bool initial=phase==Phase::Settlement||phase==Phase::SetupRoad;
  if(phase!=Phase::Actions&&phase!=Phase::FreeRoad&&!initial)return fail("Building is not available now.");
  if((phase==Phase::Settlement&&kind!=1)||(phase==Phase::SetupRoad&&kind!=0)||(phase==Phase::FreeRoad&&kind!=0))return fail("Complete the pending placement first.");
  Hand cost{};
  if(kind==0){if(!roadLegal(id,initial))return fail("Road must connect to your network and use a free edge.");cost={{1,1,0,0,0}};}
  else if(kind==1){if(!settlementLegal(id,initial))return fail("Settlement violates distance, connection, or piece limits.");cost={{1,1,1,1,0}};}
  else if(kind==2){if(id<0||id>=(int)vertices.size()||vertices[id].owner!=active||vertices[id].level!=1||pieces(active,2)>=4)return fail("Choose your settlement; maximum four cities.");cost={{0,0,0,2,3}};}
  else return fail("Unknown purchase.");
  if(!initial&&phase!=Phase::FreeRoad&&!affordable(cost))return fail("Insufficient resources. No changes made.");
  if(!initial&&phase!=Phase::FreeRoad)pay(cost);
  if(kind==0)edges[id].owner=active;else {vertices[id].owner=active;vertices[id].level=kind==2?2:1;}
  note(active,std::string("Placed ")+(kind==0?"road E":kind==1?"settlement V":"city V")+std::to_string(id+1));
  if(phase==Phase::Settlement){
   setupVertex=id;
   if(setupStep>=(int)order.size())for(int h:vertices[id].hexes){int r=hexes[h].res;if(r>=0&&bank[r]>0){bank[r]--;change(active,r,1,"Starting resource");}}
   phase=Phase::SetupRoad;
  }else if(phase==Phase::SetupRoad){
   setupStep++;int n=order.size();
   if(setupStep==2*n){active=order[0];phase=Phase::Handoff;}
   else {active=order[setupStep<n?setupStep:2*n-1-setupStep];phase=Phase::Settlement;}
  }else if(phase==Phase::FreeRoad){freeRoads--; finishFreeRoadIfNeeded();}
  return true;
 }
 bool anyRoad()const {for(int e=0;e<(int)edges.size();e++)if(roadLegal(e))return true;return false;}
 void finishFreeRoadIfNeeded(){if(freeRoads<=0||!anyRoad()){freeRoads=0;phase=resume;}}
 bool acknowledge(){if(phase!=Phase::Handoff)return fail("No turn handoff pending.");p[active].pending.fill(0);phase=Phase::Roll;return true;}
 void produce(int sum){
  std::array<Hand,4> need{};
  for(int h=0;h<(int)hexes.size();h++)if(h!=robber&&hexes[h].res>=0&&hexes[h].number==sum)
   for(int v:hexes[h].v)if(vertices[v].owner>=0)need[vertices[v].owner][hexes[h].res]+=vertices[v].level;
  for(int r=0;r<5;r++){
   int all=0,n=0;for(int i=0;i<4;i++){all+=need[i][r];n+=need[i][r]>0;}
   if(all>bank[r]&&n>1)continue;
   for(int i=0;i<4;i++)if(need[i][r]){int amount=std::min(bank[r],need[i][r]);bank[r]-=amount;change(i,r,amount,"Production");}
  }
 }
 bool roll(){if(phase!=Phase::Roll)return fail("Dice already rolled or pending action unresolved.");d1=die();d2=die();return resolveRoll(d1+d2);}
 bool resolveRoll(int sum){
  if(phase!=Phase::Roll||sum<2||sum>12)return fail("Invalid roll state.");
  if(sum==7){owed.fill(0);for(int i=0;i<4;i++)if(total(p[i].hand)>7)owed[i]=total(p[i].hand)/2;resume=Phase::Actions;phase=Phase::Discard;advanceDiscard();}
  else{produce(sum);phase=Phase::Actions;}return true;
 }
 void advanceDiscard(){for(int n:owed)if(n)return;phase=Phase::Robber;}
 int discardPlayer()const {for(int i=0;i<4;i++)if(owed[i])return i;return -1;}
 bool discard(int who,const Hand& amount){
  if(phase!=Phase::Discard||who<0||who>3||!owed[who]||total(amount)!=owed[who])return fail("Select exactly the required number of resources.");
  for(int r=0;r<5;r++)if(amount[r]<0||amount[r]>p[who].hand[r])return fail("Cannot discard resources you do not have.");
  for(int r=0;r<5;r++)if(amount[r]){change(who,r,-amount[r],"Discard");bank[r]+=amount[r];}
  owed[who]=0;advanceDiscard();return true;
 }
 std::vector<int> victims()const {
  std::vector<int> result;for(int v:hexes[robber].v){int i=vertices[v].owner;if(i>=0&&i!=active&&total(p[i].hand)>0&&std::find(result.begin(),result.end(),i)==result.end())result.push_back(i);}return result;
 }
 bool moveRobber(int h){if(phase!=Phase::Robber||h<0||h>=(int)hexes.size()||h==robber)return fail("Select a different robber hex.");robber=h;phase=victims().empty()?resume:Phase::Victim;return true;}
 bool steal(int who){
  auto eligible=victims();if(phase!=Phase::Victim||std::find(eligible.begin(),eligible.end(),who)==eligible.end())return fail("Select an eligible victim.");
  int index=std::uniform_int_distribution<int>(0,total(p[who].hand)-1)(rng),r=0;
  while(index>=p[who].hand[r])index-=p[who].hand[r++];
  change(who,r,-1,"Robbed");change(active,r,1,"Robber steal");phase=resume;return true;
 }
 bool trade(int other,const Hand& give,const Hand& take){
  if(phase!=Phase::Actions||other<0||other>3||other==active||!p[other].enabled)return fail("Invalid trade partner or phase.");
  if(total(give)<=0||total(take)<=0)return fail("Both players must offer resources; gifts are not trades.");
  for(int r=0;r<5;r++)if(give[r]<0||take[r]<0||give[r]>p[active].hand[r]||take[r]>p[other].hand[r]||(give[r]&&take[r]))return fail("Check balances; the same resource cannot appear on both sides.");
  for(int r=0;r<5;r++){int d=take[r]-give[r];if(d){change(active,r,d,"Player trade");change(other,r,-d,"Player trade");}}return true;
 }
 int rate(int r)const {int result=4;for(auto& v:vertices)if(v.owner==active){if(v.port==-1)result=std::min(result,3);if(v.port==r)result=2;}return result;}
 bool bankTrade(int give,int take,int batches){
  if(phase!=Phase::Actions||give<0||give>=5||take<0||take>=5||give==take||batches<1||batches>19)return fail("Choose different resource types and a positive quantity.");
  int cost=rate(give)*batches;if(p[active].hand[give]<cost||bank[take]<batches)return fail("Player or bank has insufficient resources.");
  change(active,give,-cost,"Bank trade");bank[give]+=cost;change(active,take,batches,"Bank trade");bank[take]-=batches;return true;
 }
 bool buyCard(){Hand cost{{0,0,1,1,1}};if(phase!=Phase::Actions||deck.empty()||!affordable(cost))return fail("Need wool, grain, ore and an available development card.");pay(cost);int k=deck.back();deck.pop_back();p[active].fresh[k]++;note(active,std::string("SIM development draw: ")+cardName[k]);return true;}
 bool playCard(int k,int r1=0,int r2=0){
  if((phase!=Phase::Actions&&phase!=Phase::Roll)||k<0||k>=4||devPlayed||p[active].cards[k]<=0)return fail("Play one older development card per turn. New cards must wait.");
  if((k==2||k==3)&&(r1<0||r1>=5||r2<0||r2>=5))return fail("Choose a resource.");
  if(k==1&&!anyRoad())return fail("No legal road placements available.");
  if(k==2){Hand need{};need[r1]++;need[r2]++;for(int r=0;r<5;r++)if(bank[r]<need[r])return fail("Bank cannot supply the selected resources.");}
  p[active].cards[k]--;devPlayed=true;resume=phase;
  note(active,std::string("Played ")+cardName[k]);
  if(k==0){p[active].knights++;if(p[active].knights>=3&&(army<0||p[active].knights>p[army].knights))army=active;phase=Phase::Robber;}
  if(k==1){freeRoads=std::min(2,15-roads(active));phase=Phase::FreeRoad;}
  if(k==2){for(int r:{r1,r2}){bank[r]--;change(active,r,1,"Year of Plenty");}}
  if(k==3){for(int i=0;i<4;i++)if(i!=active&&p[i].hand[r1]){int n=p[i].hand[r1];change(i,r1,-n,"Monopoly");change(active,r1,n,"Monopoly");}}
  return true;
 }
 bool endTurn(){
  if(phase!=Phase::Actions)return fail("Complete the roll and all mandatory actions first.");
  for(int k=0;k<5;k++){p[active].cards[k]+=p[active].fresh[k];p[active].fresh[k]=0;}
  auto it=std::find(order.begin(),order.end(),active);active=order[(it-order.begin()+1)%order.size()];turn++;devPlayed=false;phase=Phase::Handoff;return true;
 }
 bool conserved()const {for(int r=0;r<5;r++){int n=bank[r];if(n<0)return false;for(auto& a:p){if(a.hand[r]<0)return false;n+=a.hand[r];}if(n!=19)return false;}return true;}
};
}
