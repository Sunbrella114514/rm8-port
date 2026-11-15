// save / load hooks of event_wonder.cpp (included at the end of event_wonder.cpp), through the module's public state API (ev::wonderGet / wonderSet).
// See save.h, docs/save_port.md.
// [V] the original record: per wonder object of the map (TerraMgr +0x30, FUN_004ee970): stage (+0x3C), owner (+0x44), progress percent (+0x48: 0 / 10 / 60 / 100 in the
// samples); then the manager table DAT_006403bc (0x6c bytes = 3 x 0x24: index, active, owner, 4 shares as floats, cost 50000, rate 0.1f, Wonder_Manager_Init @0x4e1670).
// The object records follow the order of the map's plots (wonders.txt "wplot" lines = anchors in tile order [H]: record_m01's wonder with 10 % is the second plot
// of map 1 and the kind 2 of the table). The module's own extension chunk "WNDR" keeps the 8 shares per kind.
#include "save.h"
#include <cstring>

namespace {

std::vector<int> wonderKinds()	// the wonder kind (0 money, 1 land, 2 card) of each wonder object of the loaded map, in record order
{
	std::vector<int> k;
	const char *map = rm8SaveGameMapDir();
	if(!map) return k;
	std::ifstream f(rm8SaveScenesDir() + "/" + map + "/wonders.txt");
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; int idx;
		if((ss >> kw >> idx) && kw == "wplot" && idx >= 0 && idx < 3) k.push_back(idx);
	}
	return k;
}
float bitsToFloat(int v){ float f; memcpy(&f, &v, 4); return f; }
int floatToBits(float f){ int v; memcpy(&v, &f, 4); return v; }

struct State { bool active = false; int owner = -1, stage = 0, percent = 0; float share[8] = {0, 0, 0, 0, 0, 0, 0, 0}; };
bool get(int kind, State &s){ return ev::wonderGet(kind, s.active, s.owner, s.share, s.stage, s.percent); }
void put(int kind, const State &s){ ev::wonderSet(kind, s.active, s.owner, s.share, s.stage, s.percent); }

void wonderGet(int w, int out[3])
{
	std::vector<int> k = wonderKinds();
	out[0] = 0; out[1] = -1; out[2] = 0;
	State s;
	if(w < 0 || w >= (int)k.size() || !get(k[w], s)) return;
	int best = -1; float bs = 0;
	if(s.owner < 0) for(int i = 0; i < 8; i++) if(s.share[i] > bs){ bs = s.share[i]; best = i; }	// the object's +0x44 names the investor with the largest share
	out[0] = s.stage; out[1] = s.owner >= 0 ? s.owner : best; out[2] = s.percent;
}
void wonderSet(int w, const int in[3])
{
	std::vector<int> k = wonderKinds();
	State s;
	if(w < 0 || w >= (int)k.size() || !get(k[w], s)) return;
	s.stage = std::max(0, std::min(5, in[0])); s.percent = std::max(0, std::min(100, in[2]));
	put(k[w], s);	// rebuilds the building of its stage on the site
}
void tableGet(int i, int out[9])
{
	State s; get(std::max(0, std::min(2, i)), s);
	out[0] = i; out[1] = s.active ? 1 : 0; out[2] = s.owner;
	for(int q = 0; q < 4; q++) out[3 + q] = floatToBits(s.share[q]);
	out[7] = 50000; out[8] = floatToBits(0.1f);	// [V] the cost / rate fields of the table (Wonder_Manager_Init)
}
void tableSet(int i, const int in[9])
{
	State s;
	if(i < 0 || i > 2 || !get(i, s)) return;
	s.active = in[1] != 0; s.owner = in[2] >= -1 && in[2] < rm8TurnRoleCount() ? in[2] : -1;
	for(int q = 0; q < 4; q++) s.share[q] = bitsToFloat(in[3 + q]);
	for(int q = 4; q < 8; q++) s.share[q] = 0;
	put(i, s);
}
struct WonderSaveInit
{
	WonderSaveInit()
	{
		Rm8SaveHooks &h = rm8SaveHooks();
		h.wonderGet = wonderGet; h.wonderSet = wonderSet; h.wonderTable = tableGet; h.wonderTableSet = tableSet;
	}
} g_wonderSaveInit;

void saveWonderExtWrite(Rm8SaveW &w)
{
	for(int i = 0; i < 3; i++){
		State s; get(i, s);
		w.i32(s.active ? 1 : 0); w.i32(s.owner); for(int q = 0; q < 8; q++) w.f32(s.share[q]); w.i32(s.stage); w.i32(s.percent);
	}
}
void saveWonderExtRead(Rm8SaveR &rd)
{
	for(int i = 0; i < 3 && !rd.bad; i++){
		State s; if(!get(i, s)) return;
		int act = s.active ? 1 : 0;
		rd.opt(act); s.active = act != 0; rd.opt(s.owner);
		for(int q = 0; q < 8; q++) rd.opt(s.share[q]);
		rd.opt(s.stage); rd.opt(s.percent);
		if(s.owner < -1 || s.owner >= rm8TurnRoleCount()) s.owner = -1;
		put(i, s);
	}
}
Rm8SaveExtReg g_wonderExt("WNDR", 45, saveWonderExtWrite, saveWonderExtRead);

}	// namespace
