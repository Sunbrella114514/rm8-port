// Stock market of the port, after docs/stock_port.md. Facts marked [V] are read from the decompile (address given), [H] hypothesis,
// [A] port adaptation.
//
// [V] the stock object (0x37C bytes): +0x100 issue price, +0x104..+0x343 the last 144 prices (+0x104 = yesterday), +0x344 / +0x348 highest /
// lowest price since listing, +0x34C price, +0x350 vivacity, +0x354 shares left on the market, +0x358 suspended days, +0x35C / +0x360 forced
// change % / days (stock events), +0x364 momentum, +0x368 today's change %, +0x36C calm counter, +0x370 class (map id), +0x374 stock.bin id
// (1 = never delisted), +0x378 the seat holding the most shares (-1 none). The manager (DAT_006403c4): +0x10 listed array (16), +0x20 reserve
// array, +0x30 closed days, +0x34 / +0x38 market event % / days, +0x3C market change rate.
#include "stock.h"
#include "cards.h"
#include "events.h"
#include "ui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <string>

namespace {

const int HIST = 144;
const float PRICE_CAP = 999.99f;	// [V] 0x4479ff5c

struct Stock {
	std::string name;
	float p0 = 100, viv = 1, hist[HIST] = {}, hi = 0, lo = 0, cur = 0, mom = 0, daily = 0, evPct = 0;
	int total = 10000, remain = 10000, susp = 0, calm = 0, evDays = 0, cls = 0, id = 0, biggest = -1;
};
struct Hold { int stock; int qty; float last, cost; };	// [V] 0x10-byte record: stock, shares, last buy price (+8), total cost (+0xC) (Stock_BuyPartial @0x498dd0)
struct Def { int c, id; float p0, viv; int total; std::string name; };

void uiReset();	// closes the dialogs (human side below)
std::string g_root, g_mapDir;
std::vector<Def> g_defs;
bool g_defsLoaded = false;
std::vector<Stock> S;			// every stock of this game (listed + reserve)
std::vector<int> g_main, g_aux;		// indices into S: the listed array (+0x10) and the reserve array (+0x20)
std::vector<std::vector<Hold>> g_hold;	// per seat (Role +0x328)
std::vector<int> g_aiCool;		// AI_StockTrading in_ECX[1]: turns to wait after selling everything
std::vector<std::string> g_delisted;	// [V] manager list +0x00: the "%s<str#6320>" notices - Game_OnTurnStart @0x4a3b50 formats them into a buffer and never shows them
int g_closed = 0, g_mktDays = 0;	// [V] +0x30 / +0x38
float g_mktPct = 0, g_mktRate = 0;	// [V] +0x34 / +0x3C
bool g_log = false;
unsigned g_seed = 1;
// [A] the stock module draws from its own MSVC rand() stream (seeded from the game rng at the start), so the price series can be reproduced
int rnd(){ g_seed = g_seed * 214013u + 2531011u; return (g_seed >> 16) & 0x7fff; }

int mapIdOf(const std::string &dir)
{
	if(dir.size() == 2 && isdigit((unsigned char)dir[0]) && isdigit((unsigned char)dir[1])) return atoi(dir.c_str());
	return 0x15;	// [H] downloaded maps carry stage ids >= 0x15: no map-specific stocks (only c == 100), class > 0
}

void loadDefs()
{
	if(g_defsLoaded) return;
	g_defsLoaded = true;
	std::ifstream f(g_root + "/stocks.txt");
	if(!f){ rm8Log("stock: no %s/stocks.txt", g_root.c_str()); return; }
	std::string line;
	while(std::getline(f, line)){
		std::vector<std::string> t; std::stringstream ss(line); std::string x;
		while(std::getline(ss, x, '\t')) t.push_back(x);
		if(t.size() < 7 || t[0] != "stock") continue;
		Def d; d.c = atoi(t[1].c_str()); d.id = atoi(t[2].c_str()); sscanf(t[3].c_str(), "%f", &d.p0); d.total = atoi(t[4].c_str());
		sscanf(t[5].c_str(), "%f", &d.viv); d.name = t[6];
		g_defs.push_back(d);
	}
	rm8Log("stock: %d stocks defined", (int)g_defs.size());
}

// ---- rounding
int32_t ftolD(double v){ return rm8F2I32(v); }	// [V] _ftol @0x5c58b8 truncates; [N] out of range = 0x80000000 like x87 / x86-64 (a plain (int) saturates on arm64)
int32_t roundInt(double v){ int32_t t = rm8F2I32(v); return v - t >= 0.5 ? t + 1 : t; }	// [N] Float_RoundToInt @0x45ed20

// [V] Stock_RoundPrice @0x4b9870: < 5: 0.01 steps; < 15: 0.05; < 50: 0.1; < 150: 0.5; else whole numbers (truncated first)
float roundPrice(float x)
{
	double d = x;
	if(d < 5.0) return (float)((double)ftolD(d * 100.0) / 100.0);
	if(d < 15.0){ int v = ftolD(d * 100.0), m = v % 10; v -= m < 5 ? m : m - 5; return (float)((double)v / 100.0); }
	if(d < 50.0) return (float)((double)ftolD(d * 10.0) / 10.0);
	if(d < 150.0){ int v = ftolD(d * 10.0), m = v % 10; v -= m < 5 ? m : m - 5; return (float)((double)v / 10.0); }
	return (float)ftolD(d);
}

// ---- holdings
Hold *findHold(int seat, int si)
{
	if(seat < 0 || seat >= (int)g_hold.size()) return nullptr;
	for(Hold &h : g_hold[seat]) if(h.stock == si) return &h;
	return nullptr;
}
int holdCount(int seat, int si){ Hold *h = findHold(seat, si); return h ? h->qty : 0; }	// [V] Stock_CalcHolderCount @0x4bc880
bool seatAlive(int seat){ return seat >= 0 && seat < rm8TurnRoleCount() && rm8TurnStatus(seat) != 7; }

// [V] Stock_UpdateBiggest @0x4bc710: the seat with the most shares (a strictly larger count takes over)
void updateBiggest(int si)
{
	Stock &s = S[si];
	if(s.remain == 10000){ s.biggest = -1; return; }
	int best = holdCount(s.biggest, si);
	for(int k = 0; k < rm8TurnRoleCount(); k++){
		if(!seatAlive(k)) continue;
		int c = holdCount(k, si);
		if(best < c){ s.biggest = k; best = holdCount(k, si); }
	}
}
void refreshHolders(){ for(int si : g_main) updateBiggest(si); }	// [V] Stock_RefreshAllHolders @0x4bc840

// [V] Stock_BuyPartial @0x498dd0: n = min(wanted, left); the return value is ftol(WANTED * price) (the asm reads param_2, not n)
int32_t buyPartial(int seat, int si, int want)	// [N] int32_t
{
	Stock &s = S[si];
	int n = std::min(want, s.remain);
	s.remain -= n;
	int32_t ret = ftolD((double)want * s.cur);	// [N] int32_t
	if(Hold *h = findHold(seat, si)){ h->qty += n; h->last = s.cur; h->cost = (float)((double)n * s.cur + h->cost); return ret; }
	g_hold[seat].push_back({si, n, s.cur, (float)((double)n * s.cur + 0.0)});
	return ret;
}
// [V] Stock_SellPartial @0x499010: n (< 0 or more than held = all) shares back to the market, the cost basis shrinks proportionally; returns ftol(n * price)
int32_t sellPartial(int seat, int si, int n)	// [N] int32_t
{
	Hold *h = findHold(seat, si);
	if(!h) return 0;
	if(n < 0 || n > h->qty) n = h->qty;
	h->cost = (float)((double)h->cost - ((double)n * h->cost) / (double)h->qty);
	h->qty -= n;
	S[si].remain += n;
	int32_t ret = ftolD((double)n * S[si].cur);	// [N] int32_t
	if(h->qty == 0) g_hold[seat].erase(g_hold[seat].begin() + (h - &g_hold[seat][0]));
	return ret;
}
// [V] Stock_CalculateTotalValue @0x4994c0: sum of ftol(shares * price) (index < 0), or of one holding
int32_t holdingsValue(int seat, int k)	// [N] int32_t
{
	if(seat < 0 || seat >= (int)g_hold.size()) return 0;
	int32_t sum = 0;	// [N] int32_t
	if(k < 0){ for(Hold &h : g_hold[seat]) sum = rm8Add32(sum, ftolD((double)h.qty * S[h.stock].cur)); }	// [N] 32-bit wrap like the original's add
	else if(k < (int)g_hold[seat].size()) sum = ftolD((double)g_hold[seat][k].qty * S[g_hold[seat][k].stock].cur);
	return sum;
}

// ---- prices
void fillHist(Stock &s){ for(int k = 0; k < HIST; k++) s.hist[k] = s.p0; }

// [V] Stock_ResetStock @0x4bc330 (delisting below 10): the holdings are removed WITHOUT refund, the object is reset and goes to the end of the
// reserve queue, the first reserve stock takes the slot (with no reserve it is the same object again)
bool resetStock(int slot)
{
	int si = g_main[slot];
	Stock &o = S[si];
	if(o.id == 1) return false;
	for(int seat = 0; seat < (int)g_hold.size(); seat++){	// Stock_RemoveAllOrSome(slot) for every role
		auto &v = g_hold[seat];
		for(size_t k = 0; k < v.size(); k++) if(v[k].stock == si){ v.erase(v.begin() + k); break; }
	}
	o.evPct = 0; o.evDays = 0; o.susp = 0; o.remain = 10000; o.hi = o.lo = o.p0; o.biggest = -1; o.mom = 0; o.calm = 0;
	fillHist(o); o.cur = o.p0;
	g_delisted.push_back(o.name);
	rm8Log("stock: %s delisted (price below 10)", o.name.c_str());
	g_aux.push_back(si);
	int ni = g_aux.front(); g_aux.erase(g_aux.begin());
	Stock &n = S[ni];
	n.cur = n.p0; n.mom = 0; n.susp = 0; n.calm = 0; n.daily = 0; n.evDays = 0; n.biggest = -1; n.hi = n.lo = n.p0; fillHist(n);
	g_main[slot] = ni;
	return true;
}

void capAndRange(Stock &s)
{
	if(PRICE_CAP < s.cur) s.cur = PRICE_CAP;
	if(s.hi < s.cur) s.hi = s.cur;
	if(s.cur < s.lo) s.lo = s.cur;
}
void clamp10(float &v){ if(v > 10.0f) v = 10.0f; else if(v < -10.0f) v = -10.0f; }

// [V] Stock_UpdatePrices @0x4b9a00 (once per trading day)
void updatePrices()
{
	for(int si : g_main) if(S[si].susp < 1) memmove(&S[si].hist[1], &S[si].hist[0], (HIST - 1) * sizeof(float));	// memcpy(+0x108, +0x104, 0x23c)
	bool normal = g_mktDays == 0;
	float M = 0;
	if(normal) M = (float)((double)(rnd() % 84 - 40) / 10.0);
	else if(--g_mktDays < 0) g_mktDays = 0;
	for(int slot = 0; slot < (int)g_main.size(); slot++){
		Stock &s = S[g_main[slot]];
		if(s.susp != 0){ s.susp--; continue; }
		if(!normal){	// market event: every stock moves by the market's forced change
			if(!(10.0f <= s.hist[0]) && resetStock(slot)) continue;
			s.hist[0] = s.cur;
			s.cur = (float)(((double)g_mktPct + 100.0) * s.hist[0] / 100.0);
			// [A] the original also decrements the days counter once per stock here (@0x4b9dc6), so it goes negative and the market event never
			// ends (every later day +-10%); fixed at the user's request: one decrement per day (above)
			capAndRange(s);
		}else if(s.evDays != 0){	// stock event (red / black / diamond card)
			if(!(10.0f <= s.hist[0]) && resetStock(slot)) continue;
			s.hist[0] = s.cur;
			s.daily = s.evPct;
			s.cur = (float)(((double)s.evPct + 100.0) * s.hist[0] / 100.0);
			s.evDays--;
			capAndRange(s);
		}else{
			s.hist[0] = s.cur;
			if(!(10.0f <= s.hist[0]) && resetStock(slot)) continue;
			float d = (float)(((double)(rnd() % 213 - 100) / 10.0) * s.viv);
			if(rnd() % 100 > 30) d = (float)(((double)(rnd() % 212 - 100) / 10.0) * 0.4 + d);
			if(10.0f < d && rnd() % 100 > 30) d = d / 2.0f;
			s.mom = (float)(d + s.mom); clamp10(s.mom);
			s.daily = (float)(M + s.mom); clamp10(s.daily);
			double y = s.hist[0];
			if(s.p0 / 3.0 <= y){
				if(y <= s.p0 * 4.0) s.calm++;
				else if(s.daily <= 0.0f){ if(s.daily < 0.0f) s.daily = s.daily + s.daily; }
				else if(rnd() % 100 < 20) s.daily = -10.0f;
				else s.daily = s.daily / 2.0f;
			}else if(0.0f <= s.daily){
				if(0.0f < s.daily){ if(rnd() % 100 < 60) s.daily = s.daily + s.daily; else s.daily = s.daily * 3.0f; }
			}else if(rnd() % 100 < 80){ if(rnd() % 100 < 80) s.daily = 6.0f; else s.daily = 10.0f; }
			else s.daily = s.daily / 2.0f;
			if(3 < s.calm){ s.calm = 0; if(rnd() % 100 < 90) s.daily = (float)((double)s.daily * 0.5 + s.daily); }
			if(s.daily <= 0.0f){ if(0.85f < (double)s.daily / -10.0 && rnd() % 100 < 50) s.daily = (float)(((double)(rnd() % 80) * s.daily) / 100.0); }
			else if(0.85f < (double)s.daily / 10.0 && rnd() % 100 < 50) s.daily = (float)(((double)(rnd() % 80) * s.daily) / 100.0);
			clamp10(s.daily);
			s.cur = (float)(((double)s.daily + 100.0) * s.hist[0] / 100.0);
			capAndRange(s);
			s.cur = roundPrice(s.cur);
		}
	}
	double sc = 0, sh = 0;
	for(int si : g_main){ sc += S[si].cur; sh += S[si].hist[0]; }
	g_mktRate = sh != 0 ? (float)((sc - sh) / sh) : 0;
	if(g_log){
		std::string l = "stock: prices";
		for(int si : g_main){ char b[96]; snprintf(b, sizeof b, " %s=%.2f/%.2f", S[si].name.c_str(), S[si].cur, S[si].daily); l += b; }
		rm8Log("%s", l.c_str());
	}
}

}	// namespace

// ======================================================================== game hooks
void rm8StockSetMap(const std::string &scenesRoot, const std::string &mapDir)
{
	if(g_root != scenesRoot){ g_root = scenesRoot; g_defs.clear(); g_defsLoaded = false; }
	g_mapDir = mapDir;
}

void rm8StockNewGame(int seats)
{
	g_log = getenv("RM8_STOCK_LOG") != nullptr;
	uiReset();
	loadDefs();
	S.clear(); g_main.clear(); g_aux.clear(); g_delisted.clear();
	g_hold.assign(seats, {}); g_aiCool.assign(seats, 0);
	g_closed = 0; g_mktDays = 0; g_mktPct = 0; g_mktRate = 0;
	if(const char *c = getenv("RM8_STOCK_CLOSE")) g_closed = atoi(c);	// test hook: closed days from the start (after the warm-up updates the counter is not touched)
	const char *sd = getenv("RM8_STOCK_SEED");
	g_seed = sd ? (unsigned)strtoul(sd, nullptr, 0) : (unsigned)(rm8GameRand() << 16 ^ rm8GameRand());
	unsigned seed0 = g_seed;
	int map = mapIdOf(g_mapDir);
	// [V] Stock_LoadFromINI @0x4b8e20: the sections with c == map id or c == 100 in file order go to the reserve array; the stock fields are the file's
	for(const Def &d : g_defs){
		if(d.c != map && d.c != 100) continue;
		Stock s; s.name = d.name; s.p0 = d.p0; s.viv = d.viv; s.total = s.remain = d.total; s.id = d.id; s.cls = map; s.hi = s.lo = d.p0;
		S.push_back(s); g_aux.push_back((int)S.size() - 1);
	}
	// [V] then 16 times a random reserve stock moves to the listed array (rand() % reserve count)
	for(int k = 0; k < 16 && !g_aux.empty(); k++){
		int i = rnd() % (int)g_aux.size();
		g_main.push_back(g_aux[i]); g_aux.erase(g_aux.begin() + i);
	}
	// [V] Stock_InitPrices @0x4b9450: price = issue price, then 300 daily updates
	for(Stock &s : S){ fillHist(s); s.cur = s.p0; s.mom = 0; s.susp = 0; s.calm = 0; }
	rm8Log("stock: map %d, %d listed + %d reserve, seed %u", map, (int)g_main.size(), (int)g_aux.size(), seed0);
	if(g_log){ std::string l = "stock: listed"; for(int si : g_main) l += " " + S[si].name; rm8Log("%s", l.c_str()); }
	int warm = 300; if(const char *w = getenv("RM8_STOCK_WARM")) warm = atoi(w);	// test hook: more warm-up days (delisting coverage)
	for(int k = 0; k < warm; k++) updatePrices();
	if(const char *f = getenv("RM8_STOCK_FORCE")){ int sl = 0, d = 0; float p = 0; if(sscanf(f, "%d,%f,%d", &sl, &p, &d) == 3 && sl >= 0 && sl < (int)g_main.size()){ S[g_main[sl]].evPct = p; S[g_main[sl]].evDays = d; rm8Log("stock: force %d %.1f %d", sl, p, d); } }	// test hook: slot,pct,days like a card
	if(const char *a = getenv("RM8_STOCK_AFTER")) for(int k = 0, n = atoi(a); k < n; k++) updatePrices();	// test hook: updates after the forced event
	g_delisted.clear();
}

void rm8StockNewDay(int weekday)
{
	if(S.empty() || weekday >= 6) return;	// [V] Game_OnTurnStart @0x4a3b50: only when the weekday < 6
	if(g_closed > 0) g_closed--;	// [V] Stock_CheckUpdateTimer @0x4b9240
	else updatePrices();
	g_delisted.clear();
}

bool rm8StockOpen()
{
	int y, m, d, w; rm8GameDate(y, m, d, w);
	return w < 6 && g_closed < 1;
}

// ======================================================================== links
int32_t rm8StockValue(int seat){ return holdingsValue(seat, -1); }	// [N] int32_t

int32_t rm8StockSellAll(int seat)	// [N] int32_t
{
	if(seat < 0 || seat >= (int)g_hold.size()) return 0;
	int32_t sum = 0;	// [N] int32_t
	for(Hold &h : g_hold[seat]){ S[h.stock].remain += h.qty; sum = ftolD((double)h.qty * S[h.stock].cur + (double)sum); }	// Stock_SellAll @0x4992c0
	g_hold[seat].clear();
	refreshHolders();
	return sum;
}

void rm8StockMarketEvent(float pct, int days){ g_mktPct = pct; g_mktDays = days; }	// [V] Stock_SetField34_38 @0x4bc650
void rm8StockCloseMarket(int days){ g_closed = days; }					// [V] Stock_Array_SetField(days, -1) @0x4b9370
int rm8StockClosedDays(){ return g_closed; }						// [V] DAT_006403c4 + 0x30 (the news boom / crash / outage / liquidation need it < 1)
int32_t rm8StockLiquidateAll()	// [N] [V] News_EventStockLiquidate @0x4b64e0: every role that is not bankrupt sells all, the deposit gets the money
{
	int32_t total = 0;	// [N] int32_t
	for(int k = 0; k < rm8TurnRoleCount(); k++){
		if(!seatAlive(k)) continue;
		int32_t v = rm8StockSellAll(k);	// [N] int32_t
		if(v > 0){ rm8GameAddMoney(k, v, true); total = rm8Add32(total, v); }	// [N] 32-bit wrap
	}
	return total;
}

void rm8StockForceHoldings(int seat, float pct, int days)	// [V] Util_Apply2DTransform @0x4d3900: for every holding Obj_SetFields35C_360(stock, pct, days)
{
	if(seat < 0 || seat >= (int)g_hold.size()) return;
	for(Hold &h : g_hold[seat]){ S[h.stock].evPct = pct; S[h.stock].evDays = days; }
}
void rm8StockHalveHoldings(int seat)	// [V] Util_ApplyScoreChange @0x4d3720: n = ftol(shares * 0.5) leaves the holding (+4) and returns to the market (+0x354); the cost (+0xC) stays
{
	if(seat < 0 || seat >= (int)g_hold.size()) return;
	for(Hold &h : g_hold[seat]){ int n = (int)((double)h.qty * 0.5); h.qty -= n; S[h.stock].remain += n; }
	refreshHolders();
}
void rm8StockSwapHoldings(int a, int b)	// [V] Role_SwapAssetsWithRole @0x48bf70 case 4: the holding lists (+0x328) of the two roles are exchanged
{
	if(a < 0 || b < 0 || a >= (int)g_hold.size() || b >= (int)g_hold.size() || a == b) return;
	std::swap(g_hold[a], g_hold[b]);
	refreshHolders();
}

int rm8StockCount(){ return (int)g_main.size(); }
int32_t rm8StockFlow(int i){ return i >= 0 && i < (int)g_main.size() ? S[g_main[i]].remain : 0; }	// [N] int32_t
double rm8StockCur(int i){ return i >= 0 && i < (int)g_main.size() ? S[g_main[i]].cur : 0.0; }
double rm8StockAvr(int i)	// [V] py_Stock_AvrPrice @0x501650: sum of the 144 history prices / 144
{
	if(i < 0 || i >= (int)g_main.size()) return 0.0;
	float sum = 0;
	for(int k = 0; k < HIST; k++) sum = sum + S[g_main[i]].hist[k];
	return (double)(float)((double)sum / 144.0);
}
bool rm8StockTradeEnable(int i){ return i >= 0 && i < (int)g_main.size() && S[g_main[i]].cls > 0; }
int rm8StockHoldCount(int seat){ return seat >= 0 && seat < (int)g_hold.size() ? (int)g_hold[seat].size() : 0; }
int rm8StockHoldStock(int seat, int k)
{
	if(seat < 0 || seat >= (int)g_hold.size() || k < 0 || k >= (int)g_hold[seat].size()) return -1;
	for(int i = 0; i < (int)g_main.size(); i++) if(g_main[i] == g_hold[seat][k].stock) return i;
	return -1;
}
double rm8StockHoldPrice(int seat, int k){ return seat >= 0 && seat < (int)g_hold.size() && k >= 0 && k < (int)g_hold[seat].size() ? g_hold[seat][k].last : 0.0; }
int32_t rm8StockHoldValue(int seat, int k){ return holdingsValue(seat, k); }	// [N] int32_t
std::string rm8StockHoldName(int seat, int k){ return seat >= 0 && seat < (int)g_hold.size() && k >= 0 && k < (int)g_hold[seat].size() ? S[g_hold[seat][k].stock].name : std::string(); }	// [A] playerinfo (infoscreens.cpp): UI_DisplayStocks_Page1 @0x4ab480 "%s %5d"
int rm8StockHoldQty(int seat, int k){ return seat >= 0 && seat < (int)g_hold.size() && k >= 0 && k < (int)g_hold[seat].size() ? g_hold[seat][k].qty : 0; }

// [V] AI_StockTrading @0x4f9310
bool rm8StockAiTrade(int seat)
{
	if(S.empty() || seat < 0 || seat >= (int)g_hold.size() || !rm8StockOpen()) return false;
	const char *sc = rm8TurnAiScript(seat);
	int32_t res = 0;	// [N] int32_t
	if(holdingsValue(seat, -1) > 0 && rm8PyCallAI(sc, "SSellAll", 0, res) && res != 0){	// AI_CallScript(9, 0)
		int32_t money = rm8StockSellAll(seat);	// [N] int32_t
		rm8GameAddMoney(seat, money, true);	// Role_AddMoney(money, 1, 0): the deposit
		rm8Log("stock: seat %d sells all for %d (loan %d, %d days to the due date, cash %d, deposit %d)", seat, money, rm8EventLoan(seat), rm8EventLoanDaysLeft(seat), rm8GameCash(seat), rm8GameDeposit(seat));
		rm8TurnEnqueue(seat, 0x1B, rm8GamePushText(rm8StrFmt(283, rm8TurnName(seat))), 0);	// str 283 "%s<str#283>"
		g_aiCool[seat] = 5;
		return true;
	}
	if(g_aiCool[seat] > 0){ g_aiCool[seat]--; return false; }
	bool did = false;
	int32_t idx = 0;	// [N] int32_t
	rm8PyCallAI(sc, "SSell", 0, idx);	// AI_CallScript(5, 0)
	while(0 < idx){	// [V] "0 < idx": slot 0 is never sold by this loop (as the original)
		if(idx >= (int)g_main.size()) break;
		int si = g_main[idx];
		int32_t money = sellPartial(seat, si, -1);	// [N] int32_t
		updateBiggest(si);
		rm8Log("stock: seat %d sells %s for %d", seat, S[si].name.c_str(), money);
		rm8GameAddMoney(seat, money, true);
		did = true;
		int32_t next = 0;	// [N] int32_t
		if(!rm8PyCallAI(sc, "SSell", 0, next)) break;
		if(next == idx) break;	// [A] guard against a script that keeps naming the same (already sold) stock
		idx = next;
	}
	rm8PyCallAI(sc, "SBuy", 0, idx);	// AI_CallScript(4, 0)
	if(g_log) rm8Log("stock: seat %d (%s) step: SBuy -> %d, cash %d deposit %d", seat, sc ? sc : "-", idx, rm8GameCash(seat), rm8GameDeposit(seat));
	int32_t qty = 0;	// [N] int32_t
	if(-1 < idx && idx < (int)g_main.size() && rm8PyCallAI(sc, "SBuySum", idx, qty) && 0 < qty){	// AI_CallScript(6, idx)
		int si = g_main[idx];
		int32_t cost = buyPartial(seat, si, (int)qty);	// [N] int32_t
		rm8Log("stock: seat %d buys %d %s at %.2f (cost %d)", seat, qty, S[si].name.c_str(), S[si].cur, cost);
		if(S[si].biggest == -1) S[si].biggest = seat; else updateBiggest(si);
		rm8GameSubMoney(seat, cost, true);	// Role_SubMoney_NoEmote(cost, 1)
		did = true;
	}
	if(did) rm8TurnEnqueue(seat, 0x1B, rm8GamePushText(rm8StrFmt(282, rm8TurnName(seat))), 0);	// str 282 "%s<str#282>"
	return did;
}

// ======================================================================== human side: stock board / single stock / trade dialog
// [V] stock events 0x5E..0x61 = task object vtable 0x5E45D8 (CreateGameObject @0x4b1bc0): vf16 CUi_StockBoard__vf16__Init @0x4dbcc0 loads
// stockBoard.ui and fills it (Stock_RefreshUI @0x4db120); a row opens singleStock.ui (Stock_TradingOnOK @0x4dad60 -> Stock_AllocStockRecord
// @0x4dc1c0 -> Stock_InitUI @0x4c6830, the board is hidden meanwhile); Buy / Sell open tradeStock.ui (Stock_TradeInit @0x4c69c0 ->
// Stock_ShowTradeDialog @0x4dd150). Events 0x5E / 0x5F / 0x60 (diamond, red, black card) use the same board, a row click picks the stock
// (MiniGame_MemoryEvent @0x4b1b00 / FastEvent @0x4b1aa0 / MouseEvent @0x4b1b60 = u_MiniGame_ResetRoleMsgQueue @0x4b19a0).
namespace {

Rm8Ui *g_board = nullptr, *g_single = nullptr, *g_trade = nullptr;
int g_uiSeat = -1;		// the role whose turn it is: the board shows and changes its money / shares
int g_sel = -1;			// slot selected in the single stock dialog (+8 of the dialog object)
bool g_buy = true;		// trade dialog: buy (true) or sell
int32_t g_qty = 0;			// [N] trade dialog: the entered number of shares (+8)
struct CardPick { bool on = false; float pct = 0; int days = 0; } g_pick;	// 0x5E..0x60: the row click forces the event on the stock
bool g_chartBuilt = false;
std::vector<int> g_cVL, g_cHL, g_cDash, g_cSeg, g_cYL, g_cML;	// chart widget ids (dynamic widgets of singleStock.ui)
int g_hiliteId = 0;		// selection picture behind the chosen stock button

const unsigned C_YELLOW = 0xFFFFFF00, C_PRICE = 0xFF80FF00, C_RED = 0xFFFF0000, C_GREEN = 0xFF00FF00, C_WHITE = 0xFFFFFFFF;	// [V] Color_SwapRB(0xff00ffff / 0xff00ff80 / 0xff0000ff / 0xff00ff00): up = red, down = green

std::string fmt(const char *f, double v){ char b[64]; snprintf(b, sizeof b, f, v); return b; }
std::string fmtI(const char *f, int32_t v){ char b[64]; snprintf(b, sizeof b, f, v); return b; }	// [N] int32_t
std::string nm(const char *f, int a, int b = -1){ char s[48]; if(b >= 0) snprintf(s, sizeof s, f, a, b); else snprintf(s, sizeof s, f, a); return s; }

// [V] Stock_SetColorByChange @0x4dc240: up red, down green, unchanged white
unsigned changeColor(float p){ if(p > 0) return C_RED; if(p < 0) return C_GREEN; return C_WHITE; }

// [V] Stock_GetStockAvgPrice @0x4dc0d0
double avgCost(int seat, int si){ Hold *h = findHold(seat, si); return h && h->qty ? (double)h->cost / (double)h->qty : 0.0; }
// [V] Stock_GetMaxTradeQty @0x4dd450: min(ftol((float)deposit / price), shares left)
int32_t maxBuy(int slot){ Stock &s = S[g_main[slot]]; return std::min<int32_t>(ftolD((double)(float)rm8GameDeposit(g_uiSeat) / (double)s.cur), s.remain); }	// [N] int32_t

// ---- board
void boardRefresh()
{
	Rm8Ui *u = g_board;
	if(!u) return;
	int n = (int)g_main.size();
	rm8UiSetTextByName(u, "Label", fmt("%.2f%%", (double)g_mktRate * 100.0));	// market change rate +0x3C
	rm8UiSetTextByName(u, "Fund", fmtI("%d", rm8GameDeposit(g_uiSeat)));
	{ float v = 0; if(g_uiSeat >= 0 && g_uiSeat < (int)g_hold.size()) for(Hold &h : g_hold[g_uiSeat]) v = (float)((double)h.qty * S[h.stock].cur + v); rm8UiSetTextByName(u, "StockValue", fmt("%.2f", v)); }	// Stock_CalcTotalValue @0x4dc000
	std::vector<int> alive;
	for(int k = 0; k < 4; k++){
		bool a = seatAlive(k);
		if(a) alive.push_back(k);
		std::string pic = nm("RolePic%d", k);
		rm8UiSetVisibleN(u, pic.c_str(), a);
		if(a) rm8UiSetPicFileN(u, pic.c_str(), std::string("prt\\npc\\") + rm8TurnPic(k) + "_12.tga");	// [H] Ui_DrawRolePictureBox3 @0x4dafe0 draws the role's face picture
	}
	for(int i = 0; i < 16; i++){
		bool on = i < n;
		rm8UiSetVisibleN(u, nm("p%d", i).c_str(), on);
		if(!on) continue;
		int si = g_main[i]; Stock &s = S[si];
		auto put = [&](const std::string &name, const std::string &txt, unsigned col){ rm8UiSetEnabledN(u, name.c_str(), true); rm8UiSetTextByName(u, name.c_str(), txt); rm8UiSetColorN(u, name.c_str(), col); };
		unsigned nc = C_YELLOW;
		if(fabsf(s.daily - 10.0f) < 1e-5f) nc = C_RED;	// [V] Stock_SetColorByThreshold @0x4dc310: limit up red, limit down green
		if(fabsf(s.daily + 10.0f) < 1e-5f) nc = C_GREEN;
		put(nm("name%d", i), s.name, nc);
		put(nm("price%d", i), fmt("%.2f", s.cur), changeColor(s.daily));
		put(nm("rate%d", i), fmt("%.2f%%", s.daily), changeColor(s.daily));
		put(nm("sum%d", i), fmtI("%d", s.remain), C_YELLOW);
		put(nm("value%d", i), fmt("%.2f", avgCost(g_uiSeat, si)), C_YELLOW);
		for(int k = 0; k < 4; k++){
			std::string h = nm("%d_hold%d", k, i);
			bool a = seatAlive(k);
			if(!a){ rm8UiSetTextByName(u, h.c_str(), ""); continue; }
			int pos = (int)(std::find(alive.begin(), alive.end(), k) - alive.begin()) + 1;	// [V] alternating colours: odd count 0x80, even 0xff (red channel)
			put(h, fmtI("%d", holdCount(k, si)), 0xFF00FF00u | ((pos & 1 ? 0x80u : 0xFFu) << 16));
		}
	}
}

// ---- chart (Stock_DrawChart @0x4c4d20 / Stock_DrawChartMonthLabels @0x4c4af0): drawn with runtime widgets inside lineLabel (abs 135,37 863x536 design units)
const float CX = 135, CY = 37, CW = 863, CH = 536;
int dyn(Rm8Ui *u, const char *type, int idx, unsigned bc, int fs = 14)
{
	return rm8UiAddWidget(u, type, 0, 0, 0, 0, 0, -1, bc, fs, idx);
}
void chartBuild()
{
	Rm8Ui *u = g_single;
	g_cVL.clear(); g_cHL.clear(); g_cDash.clear(); g_cSeg.clear(); g_cYL.clear(); g_cML.clear();
	const unsigned grid = 0x8C006400;	// [V] vertex colour 0x8c006400
	for(int i = 0; i < 31; i++) g_cVL.push_back(dyn(u, "plane", 9, grid));
	for(int i = 0; i < 21; i++) g_cHL.push_back(dyn(u, "plane", 9, grid));
	for(int i = 0; i < 18; i++) g_cDash.push_back(dyn(u, "plane", 9, 0xFFFFFFFF));
	for(int i = 0; i < 145; i++) g_cSeg.push_back(dyn(u, "plane", 10, 0xFF8080FF));	// [V] line colour (0x80, 0x80, 0xff)
	for(int i = 0; i < 6; i++){ int id = dyn(u, "label", 11, 0); rm8UiSetColor(u, id, 0xFFFFFF00); g_cYL.push_back(id); }
	for(int i = 0; i < 10; i++){ int id = dyn(u, "label", 11, 0); rm8UiSetColor(u, id, 0xFFFF8000); g_cML.push_back(id); }
	g_hiliteId = dyn(u, "plane", 2, 0);	// the selected stock button's picture (Stock_ShowDetail: image 0x322), drawn under the buttons
	g_chartBuilt = true;
}
void chartUpdate(const Stock &s)
{
	Rm8Ui *u = g_single;
	if(!g_chartBuilt) chartBuild();
	for(int i = 0; i < 31; i++) rm8UiSetRect(u, g_cVL[i], CX + i * (CW / 30.0f), CY, 1, CH);
	for(int i = 0; i < 21; i++) rm8UiSetRect(u, g_cHL[i], CX, CY + i * (CH / 20.0f), CW, 1);
	const float X0 = CX + 16, W = CW - 32, H = CH - 32, BOT = CY + CH - 16, DX = W / 145.0f;
	double range = (double)s.hi - (double)s.lo;
	bool flat = fabs(range) < 1e-5;
	double scale = flat ? 0.0 : H / range;
	int di = 0;
	for(int i = 0; i < 6; i++){
		float ty = BOT - (i / 5.0f) * H;
		if(flat && i > 0){ rm8UiSetRect(u, g_cYL[i], 0, 0, 0, 0); rm8UiSetText(u, g_cYL[i], ""); continue; }
		rm8UiSetText(u, g_cYL[i], fmt("%.2f", i / 5.0 * range + s.lo));
		rm8UiSetRect(u, g_cYL[i], CX, ty - 14, 70, 18);
		if(flat) continue;
		for(int k = 0; k < 3; k++) rm8UiSetRect(u, g_cDash[di++], CX + (k / 2.0f) * W, ty, 8, 1);
	}
	while(di < 18) rm8UiSetRect(u, g_cDash[di++], 0, 0, 0, 0);
	auto py = [&](float p){ return (float)(BOT - ((double)p - s.lo) * scale); };
	float prevY = 0;
	for(int p = 0; p <= 144; p++){	// point p: history[143 - p] (oldest first), point 144 = the price now
		float y = py(p < 144 ? s.hist[143 - p] : s.cur);
		if(p > 0){
			float top = std::min(prevY, y), h = std::max(fabsf(y - prevY), 2.0f);
			rm8UiSetRect(u, g_cSeg[p - 1], X0 + p * DX, top, DX + 1.0f, h);	// segment (p-1 -> p)
		}
		prevY = y;
	}
	rm8UiSetRect(u, g_cSeg[144], 0, 0, 0, 0);
	// month labels: first at ftol(144 - 2 * day / 7), then every 22 days back, the month number counting down
	int yy, mo, dd, wd; rm8GameDate(yy, mo, dd, wd);
	int pos = ftolD(144.0 - (double)dd / 7.0 * 2.0), month = mo;
	for(int k = 0; k < 10; k++){
		if(pos > 0){
			rm8UiSetText(u, g_cML[k], nm("%d", month) + rm8Str(6319) + " ");
			rm8UiSetRect(u, g_cML[k], X0 + pos * DX, BOT, 40, 18);
		}else{ rm8UiSetText(u, g_cML[k], ""); rm8UiSetRect(u, g_cML[k], 0, 0, 0, 0); }
		pos -= 22; if(--month < 1) month = 12;
	}
}

// ---- single stock
void singleRefresh()
{
	Rm8Ui *u = g_single;
	if(!u || g_sel < 0 || g_sel >= (int)g_main.size()) return;
	int n = (int)g_main.size(), si = g_main[g_sel]; Stock &s = S[si];
	if(!g_chartBuilt) chartBuild();
	for(int i = 0; i < 16; i++){
		std::string b = nm("Button%d", i);
		rm8UiSetVisibleN(u, b.c_str(), i < n);
		if(i >= n) continue;
		rm8UiSetTextByName(u, b.c_str(), S[g_main[i]].name);
		rm8UiSetColorN(u, b.c_str(), i == g_sel ? 0xFFFFFFFF : 0xFF000000);
	}
	rm8UiSetPic(u, g_hiliteId, 802);	// [V] Stock_ShowDetail: the chosen button gets image 0x322 and white text
	rm8UiSetRect(u, g_hiliteId, 12, 30 + 32 * (g_sel + 1), 128, 32);
	rm8UiSetTextByName(u, "Fund", fmtI("%d", rm8GameDeposit(g_uiSeat)));
	// [V] Stock_ShowDetail: limit up (change >= 10) cannot be bought (str 6315), limit down (<= -10) cannot be sold (str 6316)
	bool canBuy = s.cls > 0, canSell = s.cls > 0;
	std::string lim;
	if(!(s.daily < 10.0f)){ canBuy = false; lim = rm8Str(6315); }
	if(s.daily <= -10.0f){ canSell = false; lim = rm8Str(6316); }
	rm8UiSetEnabledN(u, "Buy", canBuy); rm8UiSetEnabledN(u, "Sell", canSell);
	rm8UiSetTextByName(u, "Label_MinMaxRate", lim);
	// [V] Stock_ShowPrice @0x4c66d0
	rm8UiSetTextByName(u, "curStockName", s.name);
	rm8UiSetTextByName(u, "price", fmt("%.2f", s.cur));
	rm8UiSetTextByName(u, "rate", fmt("%.2f%%", s.daily));
	rm8UiSetTextByName(u, "sellSum", fmtI("%d", 10000 - s.remain));	// [N] int32_t
	// [V] Stock_ShowHolders @0x4c5f10: the chairman (largest holder) and up to 3 other holders
	for(const char *t : {"chairman_name", "chairman_hold", "chairman_holdRate"}) rm8UiSetTextByName(u, t, "");
	for(int k = 1; k <= 3; k++){ rm8UiSetTextByName(u, nm("holder_name%d", k).c_str(), ""); rm8UiSetTextByName(u, nm("hold%d", k).c_str(), ""); rm8UiSetTextByName(u, nm("holdRate%d", k).c_str(), ""); }
	int chair = s.biggest;
	if(chair >= 0 && chair < rm8TurnRoleCount()){
		rm8UiSetTextByName(u, "chairman_name", rm8TurnName(chair));
		rm8UiSetTextByName(u, "chairman_hold", fmtI("%d", holdCount(chair, si)));
		rm8UiSetTextByName(u, "chairman_holdRate", fmt("%.2f%%", (double)holdCount(chair, si) / 10000.0 * 100.0));
		int k = 0;
		for(int r = 0; r < rm8TurnRoleCount() && k < 3; r++){
			if(r == chair || !seatAlive(r) || holdCount(r, si) <= 0) continue;
			k++;
			rm8UiSetTextByName(u, nm("holder_name%d", k).c_str(), rm8TurnName(r));
			rm8UiSetTextByName(u, nm("hold%d", k).c_str(), fmtI("%d", holdCount(r, si)));
			rm8UiSetTextByName(u, nm("holdRate%d", k).c_str(), fmt("%.2f%%", (double)holdCount(r, si) / 10000.0 * 100.0));
		}
	}
	chartUpdate(s);
}

// ---- trade dialog [V] Stock_TradeInputProcess @0x4dc440
void tradeShow(){ rm8UiSetTextByName(g_trade, "Edit", fmtI("%d", g_qty)); }
int32_t tradeCap(){ return g_buy ? maxBuy(g_sel) : holdCount(g_uiSeat, g_main[g_sel]); }	// [N] Stock_GetMaxTradeQty / Stock_GetPlayerStockQty
void tradeClose(){ rm8UiShow(g_trade, false); }
void tradeExec()	// [V] Stock_ExecuteTrade @0x4dcd90
{
	int si = g_main[g_sel]; Stock &s = S[si];
	if(g_buy){
		float cost = (float)((double)g_qty * s.cur);
		int32_t c = roundInt(cost);	// [N] int32_t
		if(c < 1) return;	// the original only refreshes
		rm8GameSubMoney(g_uiSeat, c, true);	// Bank_WithdrawMoney(round(cost), 1): the deposit first
		buyPartial(g_uiSeat, si, (int)g_qty);
		if(s.biggest == -1) s.biggest = g_uiSeat; else updateBiggest(si);
	}else{
		rm8GameAddMoney(g_uiSeat, roundInt((float)((double)g_qty * s.cur)), true);	// Bank_AddMoney(round(qty * price), 1)
		sellPartial(g_uiSeat, si, (int)g_qty);
		updateBiggest(si);
	}
}
void tradeKey(const std::string &name)
{
	if(g_sel < 0) return;
	int32_t cap = tradeCap();	// [N] int32_t
	if(name.size() == 1 && isdigit((unsigned char)name[0])){ g_qty = std::min(cap, g_qty * 10 + (name[0] - '0')); tradeShow(); }	// Stock_TradeInputDigit @0x4dc830
	else if(name == "00"){ g_qty = std::min(cap, g_qty * 100); tradeShow(); }	// Stock_TradeInput_00K @0x4dc9c0
	else if(name == "Max"){ g_qty = cap; tradeShow(); }	// Stock_TradeInput_Max @0x4dcba0
	else if(name == "back"){ g_qty /= 10; tradeShow(); }	// Stock_TradeInput_Backspace @0x4dcc70
	else if(name == "clear"){ g_qty = 0; tradeShow(); }	// Stock_TradeInput_Clear @0x4dcb40
	else if(name == "ok"){ tradeExec(); tradeClose(); singleRefresh(); boardRefresh(); }
	else if(name == "cancel") tradeClose();
}
void tradeOpen(bool buy)	// [V] Stock_TradeInit @0x4c69c0: Buy = true
{
	if(!g_trade) g_trade = rm8UiLoad("tradeStock", RM8UI_DIALOG);
	if(!g_trade){ rm8Log("stock: no tradeStock.ui"); return; }
	g_buy = buy; g_qty = 0;
	rm8UiSetTextByName(g_trade, "Title", rm8Str(buy ? 6312 : 6313));	// str 6312 / 6313
	tradeShow();
	rm8UiOnClickName(g_trade, [](const std::string &n){ tradeKey(n); });
	rm8UiShow(g_trade, true);
}

// ---- open / close
void closeAll(bool picked)
{
	rm8UiShow(g_trade, false); rm8UiShow(g_single, false); rm8UiShow(g_board, false);
	int seat = g_uiSeat;
	if(g_pick.on && !picked) rm8CardRestore(seat);	// [V] the queued message 0x3b (Role_RestoreDeckCard @0x489580) stays in the queue: the card comes back
	g_pick.on = false;
	if(seat >= 0 && rm8TurnState(seat) == 9) rm8TurnSetState(seat, 8);	// [V] Game_EndTurn @0x4a48d0: state 9 -> 8
	g_uiSeat = -1; g_sel = -1;
}
void uiReset(){ rm8UiShow(g_trade, false); rm8UiShow(g_single, false); rm8UiShow(g_board, false); g_pick.on = false; g_uiSeat = -1; g_sel = -1; }	// a new game
void singleOpen(int slot)
{
	if(!g_single) g_single = rm8UiLoad("SingleStock", RM8UI_DIALOG);
	if(!g_single){ rm8Log("stock: no SingleStock.ui"); return; }
	g_sel = slot;
	rm8UiOnClickName(g_single, [](const std::string &n){	// [V] Stock_OnSelectStock @0x4c4a40 / Stock_OnTradeButton @0x4c48e0 / Stock_OnButtonClick @0x4c48a0
		if(g_trade && rm8UiVisible(g_trade)) return;
		if(n == "Quit" || n == "Switch"){ rm8UiShow(g_single, false); rm8UiShow(g_board, true); boardRefresh(); g_sel = -1; return; }	// Stock_UICleanupCallback @0x4dbe80
		int b = -1; if(sscanf(n.c_str(), "Button%d", &b) == 1 && b >= 0 && b < (int)g_main.size()){ g_sel = b; singleRefresh(); return; }
		if(n == "Buy") tradeOpen(true); else if(n == "Sell") tradeOpen(false);
	});
	rm8UiShow(g_board, false);	// Stock_AllocStockRecord hides the board
	singleRefresh();
	rm8UiShow(g_single, true);
}
// a row of the board was clicked
void pickRow(int row)
{
	if(row < 0 || row >= (int)g_main.size()) return;
	if(g_pick.on){	// [V] u_MiniGame_ResetRoleMsgQueue @0x4b19a0: the event is set on the stock, the queue is cleared and message 100 queued
		Stock &s = S[g_main[row]];
		s.evPct = g_pick.pct; s.evDays = g_pick.days;	// Obj_SetFields35C_360 @0x4bc620
		rm8Log("stock: seat %d forces %s %+.0f%% for %d day(s)", g_uiSeat, s.name.c_str(), g_pick.pct, g_pick.days);
		int seat = g_uiSeat;
		closeAll(true);
		rm8TurnClearQueue(seat); rm8TurnEnqueue(seat, 100, 0, 0);
		return;
	}
	singleOpen(row);
}
int rowOf(const std::string &n)
{
	if(n.find("_hold") != std::string::npos) return atoi(n.c_str() + n.find("hold") + 4);
	for(const char *p : {"name", "price", "rate", "sum", "value"}){ size_t l = strlen(p); if(n.compare(0, l, p) == 0 && l < n.size() && isdigit((unsigned char)n[l])) return atoi(n.c_str() + l); }
	if(n.size() > 1 && n[0] == 'p' && isdigit((unsigned char)n[1])) return atoi(n.c_str() + 1);
	return -1;
}
bool boardOpen(int seat)
{
	if(S.empty()) return false;
	if(!g_board) g_board = rm8UiLoad("StockBoard", RM8UI_DIALOG);
	if(!g_board){ rm8Log("stock: no StockBoard.ui"); return false; }
	g_uiSeat = seat; g_sel = -1;
	rm8UiOnClickName(g_board, [](const std::string &n){
		if(n == "Quit"){ closeAll(false); return; }	// CUi_StockBoard__vf11__OnClickReturn @0x4dbf00
		pickRow(rowOf(n));
	});
	boardRefresh();
	rm8UiShow(g_board, true);
	return true;
}

// [V] Game_UseCard @0x4a4160 for 0x5E..0x61: the closed-market texts. Returns true if the market is closed (text queued, state 8)
bool closedText(int seat, bool endTurnMsg)
{
	int y, m, d, w; rm8GameDate(y, m, d, w);
	std::string t;
	if(w < 6){ if(g_closed > 0) t = rm8StrFmt(6318, g_closed); }
	else t = rm8Str(6317);
	if(t.empty()) return false;
	rm8TurnEnqueue(seat, 0x1B, rm8GamePushText(t), 0);
	if(endTurnMsg) rm8TurnEnqueue(seat, 100, 0, 0);	// 0x61: message 100 follows
	rm8TurnSetState(seat, 8);
	return true;
}

// ---- test hooks (docs/stock_port.md): RM8_STOCK_UI=board | single:N | card94 | card95 | card96 opens the board for seat 0 once it waits for the dice;
// RM8_STOCK_AUTO=name,name,... then presses these widget names (the top-most open dialog) at once
struct Hook { bool init = false, done = false; std::string ui; std::vector<std::string> autos; } g_hook;
void hookTick()
{
	if(g_hook.done) return;
	if(!g_hook.init){
		g_hook.init = true;
		if(const char *e = getenv("RM8_STOCK_UI")) g_hook.ui = e;
		if(const char *a = getenv("RM8_STOCK_AUTO")){ std::string cur; for(const char *p = a; ; p++){ if(*p == ',' || !*p){ if(!cur.empty()) g_hook.autos.push_back(cur); cur.clear(); if(!*p) break; } else cur += *p; } }
		if(g_hook.ui.empty()) g_hook.done = true;
	}
	if(g_hook.done || S.empty() || rm8TurnRoleCount() < 1 || !rm8TurnHuman(0) || rm8TurnState(0) != 6) return;
	g_hook.done = true;
	rm8Log("stock: test hook opens '%s' for seat 0", g_hook.ui.c_str());
	if(g_hook.ui.compare(0, 4, "card") == 0) rm8StockEvent(0, atoi(g_hook.ui.c_str() + 4), 1);
	else{
		rm8StockOpenFromHud(0);
		int sl = -1; if(sscanf(g_hook.ui.c_str(), "single:%d", &sl) == 1) singleOpen(sl);
	}
	for(const std::string &a : g_hook.autos){
		rm8Log("stock: test press %s", a.c_str());
		if(g_trade && rm8UiVisible(g_trade)) tradeKey(a);
		else if(g_single && rm8UiVisible(g_single)){
			if(a == "Quit" || a == "Switch"){ rm8UiShow(g_single, false); rm8UiShow(g_board, true); boardRefresh(); g_sel = -1; }
			else if(a == "Buy") tradeOpen(true); else if(a == "Sell") tradeOpen(false);
			else { int b = -1; if(sscanf(a.c_str(), "Button%d", &b) == 1 && b >= 0 && b < (int)g_main.size()){ g_sel = b; singleRefresh(); } }
		}else if(g_board && rm8UiVisible(g_board)){ if(a == "Quit") closeAll(false); else pickRow(rowOf(a)); }
	}
}

}	// namespace

bool rm8StockOpenFromHud(int seat)
{
	if(S.empty() || !rm8TurnHuman(seat) || rm8TurnState(seat) != 6 || g_uiSeat >= 0) return false;
	if(closedText(seat, true)) return false;	// [V] Game_UseCard(0x61): text, message 100, state 8
	if(!boardOpen(seat)) return false;
	rm8TurnSetState(seat, 9);	// CPlayerTag_SetStatusCode9, then Role_ClearMsgQueues + Role_EnqueueMsg(100) (Game_DispatchCardAction @0x4a4420)
	rm8TurnClearQueue(seat); rm8TurnEnqueue(seat, 100, 0, 0);
	return true;
}

void rm8StockTick(){ hookTick(); }
bool rm8StockUiOpen(){ return rm8UiVisible(g_board) || rm8UiVisible(g_single); }

bool rm8StockEvent(int seat, int ev, int)
{
	if(ev < 94 || ev > 96 || S.empty() || seat < 0 || seat >= (int)g_hold.size()) return false;
	float pct = ev == 96 ? -10.0f : 10.0f;	// [V] AI_OnLandEvent @0x4f79c0: 0x5E +10 for 5 days, 0x5F +10 for 1 day, 0x60 -10 for 1 day
	int days = ev == 94 ? 5 : 1;
	if(!rm8TurnHuman(seat)){
		// [V] Card_SelectBest(1) @0x4fcf40 (0x5E / 0x5F): the own holding of the highest value; Card_SelectBest(0) (0x60): the highest holding
		// of another living role that this role does not hold; Auction_PlaceBid(stock, pct, days) = Obj_SetFields35C_360
		int best = -1; int32_t bv = 0;	// [N] int32_t
		for(int k = 0; k < (int)g_hold[seat].size(); k++) if(ev != 96){ int32_t v = holdingsValue(seat, k); if(bv < v){ bv = v; best = g_hold[seat][k].stock; } }
		if(ev == 96) for(int r = 0; r < rm8TurnRoleCount(); r++){
			if(r == seat || !seatAlive(r)) continue;
			for(int k = 0; k < (int)g_hold[r].size(); k++){
				int32_t v = holdingsValue(r, k);	// [N] int32_t
				if(bv < v && !findHold(seat, g_hold[r][k].stock)){ bv = v; best = g_hold[r][k].stock; }
			}
		}
		if(best >= 0){
			S[best].evPct = pct; S[best].evDays = days;
			rm8Log("stock: seat %d forces %s %+.0f%% for %d day(s)", seat, S[best].name.c_str(), pct, days);
			rm8TurnEnqueue(seat, 0x1B, rm8GamePushText(rm8StrFmt(ev == 94 ? 558 : ev == 95 ? 528 : 527, rm8TurnName(seat))), 0);	// str 558 / 528 / 527
			rm8TurnEnqueue(seat, 100, 0, 0);
		}
		return true;	// [V] AI_OnLandEvent returns 1 in every case
	}
	if(closedText(seat, false)) return false;	// [V] Game_UseCard: closed market (text, state 8, the card comes back through 0x3b)
	if(g_uiSeat >= 0) return false;
	g_pick.on = true; g_pick.pct = pct; g_pick.days = days;
	if(!boardOpen(seat)){ g_pick.on = false; return false; }
	rm8TurnSetState(seat, 9);
	return true;
}

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_stock.inc"
