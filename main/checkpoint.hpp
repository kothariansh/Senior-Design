#pragma once
#include "catan_game.hpp"
#include <sstream>
#include <iomanip>
namespace checkpoint {
using namespace catan_game;
using Keys=std::array<std::string,4>;
struct Writer {
 std::ostringstream s;
 template<class T> void value(T& v){s<<int(v)<<' ';}
 void key(std::string& v){s<<(v.empty()?"-":v)<<' ';}
 void list(std::vector<int>& v){s<<v.size()<<' '; for(auto x:v)value(x);}
};
struct Reader {
 std::istringstream s;
 explicit Reader(const std::string& data):s(data){}
 template<class T> void value(T& v){int n;if(s>>n)v=static_cast<T>(n);}
 void key(std::string& v){s>>v;if(v=="-")v.clear();}
 void list(std::vector<int>& v){int n;if(!(s>>n)||n<0||n>25){s.setstate(std::ios::failbit);return;}v.resize(n);for(auto& x:v)value(x);}
};
template<class A> void fields(A& a,Game& g,Keys& keys){
 for(auto& k:keys)a.key(k);
 for(auto& p:g.p){a.value(p.enabled);a.value(p.knights);
 for(auto* h:{&p.hand,&p.pending,&p.cards,&p.fresh})for(auto& v:*h)a.value(v);}
 for(auto& v:g.bank)a.value(v);
 for(auto& h:g.hexes){a.value(h.res);a.value(h.number);}
 for(auto& v:g.vertices){a.value(v.owner);a.value(v.level);a.value(v.port);}
 for(auto& e:g.edges)a.value(e.owner);
 for(auto& v:g.orderRoll)a.value(v);
 for(auto& v:g.owed)a.value(v);
 a.list(g.order);a.list(g.contenders);a.list(g.deck);
 a.value(g.phase);a.value(g.resume);a.value(g.devPlayed);
 for(auto* x:{&g.active,&g.setupStep,&g.setupVertex,&g.turn,&g.robber,&g.d1,&g.d2,&g.freeRoads,&g.longest,&g.army})a.value(*x);
}
inline std::string encode(Game& g,Keys& k){Writer w;fields(w,g,k);return w.s.str();}
inline bool decode(const std::string& s,Game& g,Keys& k){Reader r(s);fields(r,g,k);return !r.s.fail()&&g.active>=0&&g.active<4&&g.robber>=0&&g.robber<19&&int(g.phase)>=0&&int(g.phase)<=int(Phase::FreeRoad);}
}
