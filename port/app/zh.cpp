// Traditional -> Simplified conversion (see zh.h).
#include "zh.h"
#include <fstream>
#include <unordered_map>
#include <algorithm>

namespace {
std::unordered_map<std::string, std::string> g_map;	// key: UTF-8 phrase or character
std::unordered_map<std::string, std::string> g_cache;	// whole strings already converted (UI texts repeat every frame)
size_t g_maxLen = 1;	// longest key in code points
bool g_on = false;

size_t cpLen(unsigned char c){ return c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1; }

std::string convert(const std::string &s)
{
	std::string out; out.reserve(s.size());
	size_t i = 0;
	while(i < s.size()){
		// byte offsets of the next g_maxLen code points
		size_t ends[16]; size_t n = 0, j = i;
		while(n < g_maxLen && n < 16 && j < s.size()){ j += cpLen((unsigned char)s[j]); ends[n++] = std::min(j, s.size()); }
		bool hit = false;
		for(size_t k = n; k > 0; k--){	// longest match first
			auto it = g_map.find(s.substr(i, ends[k - 1] - i));
			if(it != g_map.end()){ out += it->second; i = ends[k - 1]; hit = true; break; }
		}
		if(!hit){ out.append(s, i, ends[0] - i); i = ends[0]; }
	}
	return out;
}
}

bool rm8ZhLoad(const std::string &file)
{
	std::ifstream f(file);
	if(!f) return false;
	std::string line;
	while(std::getline(f, line)){
		if(line.empty() || line[0] == '#') continue;
		size_t t = line.find('\t');
		if(t == std::string::npos) continue;
		std::string k = line.substr(0, t);
		size_t n = 0; for(size_t i = 0; i < k.size(); i += cpLen((unsigned char)k[i])) n++;
		g_maxLen = std::max(g_maxLen, std::min<size_t>(n, 16));
		g_map[k] = line.substr(t + 1);
	}
	g_cache.clear();
	g_on = !g_map.empty();
	return g_on;
}

void rm8ZhEnable(bool on){ g_on = on && !g_map.empty(); }
bool rm8ZhEnabled(){ return g_on; }

const std::string &rm8ZhConvert(const std::string &s)
{
	if(!g_on) return s;
	auto it = g_cache.find(s);
	if(it != g_cache.end()) return it->second;
	if(g_cache.size() > 4096) g_cache.clear();	// [A] bound the cache (dialog texts with numbers keep changing)
	return g_cache[s] = convert(s);
}
