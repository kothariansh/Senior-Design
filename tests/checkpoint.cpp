#include "../main/checkpoint.hpp"
#include <cassert>
using namespace checkpoint;
int main(){
 Game g(123);Keys keys={"01020304","10203040506070","ABCDEFFF",""};
 for(int i=0;i<3;i++)g.p[i].enabled=true;
 assert(g.start());while(g.phase==Phase::Order){for(int i:g.contenders)assert(g.rollOrder(i));g.finishOrder();}
 g.vertices[0].level=2;g.vertices[0].owner=2;g.edges[0].owner=2;g.p[1].hand[2]=3;g.bank[2]-=3;
 auto data=encode(g,keys);assert(data.size()<4096);
 Game restored(99);Keys ids;assert(decode(data,restored,ids));assert(ids==keys);
 assert(restored.order==g.order && restored.deck==g.deck && restored.p[1].hand==g.p[1].hand);
 assert(encode(restored,ids)==data);assert(!decode(data.substr(0,data.size()/2),restored,ids));
}
