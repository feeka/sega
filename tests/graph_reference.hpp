#pragma once
#include "sega/graph_types.hpp"
#include <functional>
#include <iostream>
#include <queue>
#include <random>
#include <set>
#include <tuple>
using namespace sega;
using Event=std::tuple<char,Vertex,uint64_t>;
size_t checks=0,cases=0,failures=0;
void check(bool ok,const std::string& what) {
    ++checks;if(!ok){++failures;if(failures<25)std::cerr<<"FAIL: "<<what<<'\n';}
}
template<class F> void throws(F f,const std::string& what) {
    try{f();check(false,what);}catch(const std::exception&){check(true,what);}
}
struct Reference {
    std::vector<std::vector<Vertex>> out,in;
    std::vector<Edge> ordered;
    Reference(size_t n,const std::vector<Edge>& edges):out(n),in(n) {
        // Intentionally do not use production normalization or navigation.
        std::set<Edge> pairs(edges.begin(),edges.end());
        for(auto e:pairs){out.at(e.first).push_back(e.second);in.at(e.second).push_back(e.first);ordered.push_back(e);}
    }
    std::vector<Event> bfs(Vertex source,bool forest) const {
        std::vector<Event> events;std::vector<int64_t> distance(out.size(),-1);std::queue<Vertex> q;
        auto tree=[&](Vertex root){
            distance[root]=0;q.push(root);
            while(!q.empty()) {
                auto u=q.front();q.pop();events.emplace_back('B',u,uint64_t(distance[u]));
                for(auto v:out[u])if(distance[v]<0){distance[v]=distance[u]+1;q.push(v);}
            }
        };
        if(forest){for(Vertex v=0;v<out.size();++v)if(distance[v]<0)tree(v);}else tree(source);
        return events;
    }
    std::vector<Event> dfs(Vertex source,bool forest) const {
        std::vector<Event> events;std::vector<bool> seen(out.size(),false);
        std::function<void(Vertex,Vertex)> visit=[&](Vertex u,Vertex parent){
            seen[u]=true;events.emplace_back('D',u,parent);
            for(auto v:out[u])if(!seen[v])visit(v,u);
            events.emplace_back('F',u,0);
        };
        // Deep example: explicit ENTER/EXIT actions, not production edge cursors.
        auto actions=[&](Vertex root){
            std::vector<Event> todo{{'D',root,no_vertex}};
            while(!todo.empty()) {
                auto [tag,u,parent]=todo.back();todo.pop_back();
                if(tag=='F'){events.emplace_back('F',u,0);continue;}
                if(seen[u])continue;
                seen[u]=true;events.emplace_back('D',u,parent);todo.emplace_back('F',u,0);
                for(auto it=out[u].rbegin();it!=out[u].rend();++it)if(!seen[*it])todo.emplace_back('D',*it,u);
            }
        };
        auto tree=[&](Vertex root){if(out.size()<2000)visit(root,no_vertex);else actions(root);};
        if(forest){for(Vertex v=0;v<out.size();++v)if(!seen[v])tree(v);}else tree(source);
        return events;
    }
};
