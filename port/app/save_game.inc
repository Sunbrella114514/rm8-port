// save / load hooks of game.cpp (included at the end of game.cpp so that it can reach the file's state). See save.h, docs/save_port.md.
// Sections owned here: the 12 header ints (g_GameState +4..+0x30), the terra / wonder records (TerraMgr_ApplyToAll @0x4f2c20), the street records
// (Terra_SaveHeights @0x42d8b0, a misnomer: Street +0x108 / +0x10C / +0x104 / +0x110), the money fields of the role record, the rules RNG.
#include "save.h"

namespace {

int g_hdr[4] = {0, 0, 0, 0};	// header +0x18 / +0x1C / +0x20 / +0x24 of a LOADED record (start year / month / day / weekday); g_hdrSet: they are valid
bool g_hdrSet = false;

int32_t civilDays(int y, int m, int d)	// [N] days since 1970-01-01
{
	y -= m <= 2;
	int32_t era = (y >= 0 ? y : y - 399) / 400;	// [N] int32_t
	unsigned yoe = (unsigned)(y - era * 400), doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int32_t)doe - 719468;	// [N] int32_t
}
void civilFromDays(int32_t z, int &y, int &m, int &d)	// [N] int32_t
{
	z += 719468;
	int32_t era = (z >= 0 ? z : z - 146096) / 146097;	// [N] int32_t
	unsigned doe = (unsigned)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int32_t yy = (int32_t)yoe + era * 400;	// [N] int32_t
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	d = (int)(doy - (153 * mp + 2) / 5 + 1); m = (int)(mp < 10 ? mp + 3 : mp - 9); y = (int)(yy + (m <= 2));
}

}	// namespace

// a new game: the start date is today's (rm8GameNewGame set g_year .. g_wday)
void rm8SaveGameNewGame()
{
	g_hdrSet = false;
	if(const char *sd = getenv("RM8_SEED")) rm8SavePySeed((unsigned)strtoul(sd, nullptr, 0));	// test hook: reproducible AI decisions
}

// [V] g_GameState +4..+0x30: year, month, day, weekday, days, then +0x18.. = the START date (Game_InitGame @0x4a1090 copies +4..+0x14 there and, for a limited
// game, adds 3 / 6 months to the month), +0x28 = 0, +0x2C = price base, +0x30 = price index
void rm8SaveGameWriteHeader(Rm8SaveW &w)
{
	int sy = g_year, sm = g_month, sd = g_mday, sw = g_wday;
	if(g_hdrSet){ sy = g_hdr[0]; sm = g_hdr[1]; sd = g_hdr[2]; sw = g_hdr[3]; }
	else if(g_days > 1){	// the start date = today minus the elapsed days (every day change moved both by one)
		int32_t z = civilDays(g_year, g_month, g_mday) - (g_days - 1);	// [N] int32_t
		civilFromDays(z, sy, sm, sd); sw = (int)(((z + 4) % 7 + 7) % 7);
	}else if(g_s && g_wday > 7) sw = g_wday % 7;
	w.i32(g_year); w.i32(g_month); w.i32(g_mday); w.i32(g_wday); w.i32(g_days);
	w.i32(sy); w.i32(g_endMonth ? g_endMonth : sm); w.i32(g_endMonth ? g_endDay : sd); w.i32(sw); w.i32(0);
	w.i32((int)g_base); w.i32((int)g_PI);
}
bool rm8SaveGameReadHeader(Rm8SaveR &r, int timeLimit)
{
	g_year = r.i32(); g_month = r.i32(); g_mday = r.i32(); g_wday = r.i32(); g_days = r.i32();
	g_hdr[0] = r.i32(); int em = r.i32(), ed = r.i32(); g_hdr[3] = r.i32(); r.i32();
	g_hdr[1] = em; g_hdr[2] = ed; g_hdrSet = true;
	g_base = std::max(1, r.i32()); g_PI = std::max(1, r.i32());
	g_endMonth = timeLimit < 2 ? em : 0; g_endDay = timeLimit < 2 ? ed : 0;
	return !r.bad;
}

// [V] FUN_004eff70 per plot: owner (+0x4C), price percent (+0x48), (building type << 16) | plot type (+0x3C), level (+0x44); a research institute (building 2) of
// level 1..5 adds Building +0x14 (2 ints, FUN_004f6b40). Then FUN_004ee970 per wonder: stage (+0x3C), owner (+0x44), invested (+0x48)
void rm8SaveGameWriteTerra(Rm8SaveW &w)
{
	int T = (int)g_t.size();
	for(int k = 0; k < T; k++){
		int idx = rm8SaveTerraPort(k);
		if(idx < 0 || idx >= T) idx = k;
		const GTerra &t = g_t[idx];
		int hi = t.level > 0 ? t.build : 0;
		w.i32(t.owner); w.i32(t.pct); w.u32(((unsigned)hi << 16) | (unsigned)t.type); w.i32(t.level);
		if(hi == 2 && t.level >= 1 && t.level <= 5){ int e[2] = {0, 0}; if(rm8SaveHooks().instituteGet) rm8SaveHooks().instituteGet(idx, e); w.i32(e[0]); w.i32(e[1]); }
	}
	int W = rm8SaveWonderCount();
	for(int i = 0; i < W; i++){ int e[3] = {0, -1, 0}; if(rm8SaveHooks().wonderGet) rm8SaveHooks().wonderGet(i, e); w.i32(e[0]); w.i32(e[1]); w.i32(e[2]); }
}
bool rm8SaveGameReadTerra(Rm8SaveR &r)
{
	int T = (int)g_t.size(), nSeat = (int)g_r.size();
	for(int k = 0; k < T; k++){
		int idx = rm8SaveTerraPort(k);
		if(idx < 0 || idx >= T) idx = k;
		int owner = r.i32(), pct = r.i32(); unsigned ty = r.u32(); int level = r.i32();
		int hi = (int)(ty >> 16);
		int e[2] = {0, 0};
		if(hi == 2 && level >= 1 && level <= 5){ e[0] = r.i32(); e[1] = r.i32(); if(rm8SaveHooks().instituteSet) rm8SaveHooks().instituteSet(idx, e); }
		if(owner < -1 || owner >= nSeat) owner = -1;
		level = std::max(0, std::min(5, level));
		if(g_t[idx].type == 1) hi = level > 0 ? 1 : 0;
		else if(level == 0) hi = 0;
		g_t[idx].pct = pct > 0 ? pct : 100;
		rm8GameSetTerra(idx, owner, level, hi);
	}
	int W = rm8SaveWonderCount();
	for(int i = 0; i < W; i++){ int e[3]; e[0] = r.i32(); e[1] = r.i32(); e[2] = r.i32(); if(rm8SaveHooks().wonderSet) rm8SaveHooks().wonderSet(i, e); }
	return !r.bad;
}

// [V] FUN_0042d8b0: streets 1..136 of the street container, each: Street +0x108 (sealed days), +0x10C (price multiplier, float), +0x104 (price), +0x110 (multiplier
// days). The container holds the sections of street.bin in file order (section 0 = the blank ID 0), NOT indexed by the ID: street.bin has no ID 48, so record 47 is street 49
// (record_03: price 60 = street 49 of scenes/10/scene.txt) [V]; the scene's street list is that order (ascending IDs)
static std::vector<int> streetOrder()
{
	std::vector<int> ids;
	if(g_s) for(auto &kv : g_s->streetPrice) ids.push_back(kv.first);
	return ids;
}
void rm8SaveGameWriteStreets(Rm8SaveW &w)
{
	std::vector<int> ids = streetOrder();
	for(int i = 1; i <= 136; i++){
		int id = i < (int)ids.size() ? ids[i] : i;
		auto f = g_stFx.find(id);
		int close = f != g_stFx.end() ? f->second.closeDays : 0, flow = f != g_stFx.end() ? f->second.flowDays : 0;
		float mult = f != g_stFx.end() ? (float)f->second.mult : 1.0f;
		int price = 100;
		if(g_s){ auto p = g_s->streetPrice.find(id); if(p != g_s->streetPrice.end()) price = p->second; }
		w.i32(close); w.f32(mult); w.i32(price); w.i32(flow);
	}
}
bool rm8SaveGameReadStreets(Rm8SaveR &r)
{
	g_stFx.clear();
	std::vector<int> ids = streetOrder();
	for(int i = 1; i <= 136; i++){
		int id = i < (int)ids.size() ? ids[i] : i;
		int close = r.i32(); float mult = r.f32(); int price = r.i32(), flow = r.i32();
		if(g_s && g_s->streetPrice.count(id)) g_s->streetPrice[id] = price;
		if(close > 0 || flow > 0 || mult != 1.0f){ StreetFx &f = g_stFx[id]; f.closeDays = close; f.flowDays = flow; f.mult = mult > 0 ? mult : 1.0; }
	}
	return !r.bad;
}

void rm8SaveGameRoleGet(int seat, Rm8SaveRole &r)
{
	if(!validR(seat)) return;
	const GRole &g = g_r[seat];
	r.tickets = (int)g.points; r.cash = (int)g.cash; r.deposit = (int)g.deposit; r.shape = g.shape; r.invisible = g.invisible;
	r.rent[0] = (int)g.rentIn; r.rent[1] = (int)g.rentOut; r.rent[2] = (int)g.badDays;	// +0x340 bad-luck days (balance.cpp)	// [V] rules_land.md: +0x338 rent received, +0x33C rent paid
}
void rm8SaveGameRoleSet(int seat, const Rm8SaveRole &r)
{
	if(!validR(seat)) return;
	GRole &g = g_r[seat];
	g.points = std::max(0, r.tickets); g.cash = std::max(0, r.cash); g.deposit = std::max(0, r.deposit); g.shape = std::max(0, r.shape); g.invisible = std::max(0, r.invisible);
	g.rentIn = r.rent[0]; g.rentOut = r.rent[1]; g.badDays = r.rent[2];
	g.bankruptChecked = r.status == 7;
	if(r.status == 7) rm8HudSeatBankrupt(seat);	// the seat hexagon shows the bankrupt portrait (gmaindlg)
}

Rm8GameOptions rm8SaveGameOptions(){ return g_opts; }
const char *rm8SaveGameMapDir(){ return g_s ? g_s->mapId.c_str() : nullptr; }

// extension chunk "GAME": the rules RNG, the pool of the queued dialog texts (queue entries refer to them by index: the last 256 are kept)
static void saveGameExtWrite(Rm8SaveW &w)
{
	w.u32(g_seed);
	int N = (int)g_texts.size(), k = std::min(N, 256);
	w.i32(N); w.i32(k);
	for(int i = N - k; i < N; i++) w.str(g_texts[i]);
}
static void saveGameExtRead(Rm8SaveR &r)
{
	g_seed = r.u32();
	int N = r.i32(), k = r.i32();
	if(r.bad || N < 0 || k < 0 || k > N) return;
	g_texts.assign(N, std::string());
	for(int i = N - k; i < N; i++) g_texts[i] = r.str();
}
RM8_SAVE_EXT(GAME, 10, saveGameExtWrite, saveGameExtRead);
