// save / load hooks of stock.cpp (included at the end of stock.cpp). See save.h, docs/save_port.md.
// Section: [V] FUN_004bc0f0 = the listed array (n + n objects of 0x37C bytes) then the reserve array; the object layout is in the header comment of stock.cpp
// (the port's Stock mirrors it field for field; the name is Big5 in the record). Role record: the holdings (+0x328): index in the listed array, shares, last
// price, cost [V] FUN_004997e0.
#include "save.h"

static void putStock(Rm8SaveW &w, const Stock &s)
{
	char name[0x100]; memset(name, 0, sizeof name);
	std::string b5 = rm8SaveBig5FromUtf8(s.name);
	memcpy(name, b5.data(), std::min<size_t>(b5.size(), 0xff));
	w.raw(name, 0x100); w.f32(s.p0);
	for(int i = 0; i < HIST; i++) w.f32(s.hist[i]);
	w.f32(s.hi); w.f32(s.lo); w.f32(s.cur); w.f32(s.viv);
	w.i32(s.remain); w.i32(s.susp); w.f32(s.evPct); w.i32(s.evDays); w.f32(s.mom); w.f32(s.daily); w.i32(s.calm);
	w.i32(s.cls); w.i32(s.id); w.i32(s.biggest);
}
static bool getStock(Rm8SaveR &r, Stock &s)
{
	char name[0x100]; r.raw(name, 0x100);
	s.name = rm8SaveUtf8FromBig5(name, 0x100);
	s.p0 = r.f32();
	for(int i = 0; i < HIST; i++) s.hist[i] = r.f32();
	s.hi = r.f32(); s.lo = r.f32(); s.cur = r.f32(); s.viv = r.f32();
	s.remain = r.i32(); s.susp = r.i32(); s.evPct = r.f32(); s.evDays = r.i32(); s.mom = r.f32(); s.daily = r.f32(); s.calm = r.i32();
	s.cls = r.i32(); s.id = r.i32(); s.biggest = r.i32();
	s.total = 10000;
	for(const Def &d : g_defs) if(d.name == s.name){ s.total = d.total; break; }
	return !r.bad;
}

void rm8SaveStockWrite(Rm8SaveW &w)
{
	w.i32((int)g_main.size());
	for(int si : g_main) putStock(w, S[si]);
	w.i32((int)g_aux.size());
	for(int si : g_aux) putStock(w, S[si]);
}
bool rm8SaveStockRead(Rm8SaveR &r)
{
	loadDefs();
	S.clear(); g_main.clear(); g_aux.clear(); g_delisted.clear();
	int seats = rm8TurnRoleCount();
	g_hold.assign(seats, {}); g_aiCool.assign(seats, 0);
	for(int q = 0; q < 2; q++){
		int n = r.i32();
		if(r.bad || n < 0 || n > 64) return false;
		for(int i = 0; i < n; i++){
			Stock s; if(!getStock(r, s)) return false;
			S.push_back(s); (q ? g_aux : g_main).push_back((int)S.size() - 1);
		}
	}
	uiReset();
	return true;
}

void rm8SaveStockRoleGet(int seat, Rm8SaveRole &r)
{
	r.holds.clear();
	if(seat < 0 || seat >= (int)g_hold.size()) return;
	for(const Hold &h : g_hold[seat]){
		auto it = std::find(g_main.begin(), g_main.end(), h.stock);
		Rm8SaveHold o; o.stock = it == g_main.end() ? -1 : (int)(it - g_main.begin()); o.qty = h.qty; o.last = h.last; o.cost = h.cost;
		r.holds.push_back(o);
	}
}
void rm8SaveStockRoleSet(int seat, const Rm8SaveRole &r)
{
	if(seat < 0) return;
	if(seat >= (int)g_hold.size()) g_hold.resize(seat + 1);
	g_hold[seat].clear();
	for(const Rm8SaveHold &o : r.holds){
		if(o.stock < 0 || o.stock >= (int)g_main.size() || o.qty <= 0) continue;
		g_hold[seat].push_back({g_main[o.stock], o.qty, o.last, o.cost});
	}
}

// extension chunk "STCK": the stock module's own RNG stream, the AI cool-down per seat and the market state of the manager (+0x30 closed days, +0x34 / +0x38 / +0x3C)
static void saveStockExtWrite(Rm8SaveW &w)
{
	w.u32(g_seed); w.i32(g_closed); w.i32(g_mktDays); w.f32(g_mktPct); w.f32(g_mktRate);
	w.i32((int)g_aiCool.size()); for(int c : g_aiCool) w.i32(c);
}
static void saveStockExtRead(Rm8SaveR &r)
{
	g_seed = r.u32(); g_closed = r.i32(); g_mktDays = r.i32(); g_mktPct = r.f32(); g_mktRate = r.f32();
	int n = r.i32();
	for(int i = 0; i < n && !r.bad; i++){ int c = r.i32(); if(i < (int)g_aiCool.size()) g_aiCool[i] = c; }
}
RM8_SAVE_EXT(STCK, 15, saveStockExtWrite, saveStockExtRead);
