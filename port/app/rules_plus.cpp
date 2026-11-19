// Enhanced AI / house rules (rules_plus.h): the rule set of the game, its own random stream, the pure decision functions of the loan and minigame rules, the RULE
// save chunk. [R] = house rule (not in the original); with the switch off nothing here consumes a shared random number or writes a chunk.
#include "rules_plus.h"
#include "scene.h"
#include "menu.h"
#include "turn.h"
#include "cards.h"
#include "events.h"
#include "save.h"
#include "npc.h"
#include "net.h"
#include <cmath>

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace {
bool g_plus = false;
int g_level = 3;
unsigned g_rng = 1;
bool g_storyLoan[16] = {};
bool g_got = false;	// a RULE chunk was read by the load in progress
float g_hate[8][8] = {};	// [R] B3: g_hate[victim][aggressor]
int g_perDay[8] = {};	// revenge actions taken on this target today
int g_notes = 0;
void hateClear(){ for(auto &r : g_hate) for(float &f : r) f = 0; for(int &c : g_perDay) c = 0; for(int s = 0; s < 8; s++) rm8NpcSetBombOwner(s, -1); }

unsigned mixSeed(unsigned s){ s = s * 2654435761u + 0x9E3779B9u; s ^= s >> 15; if(!s) s = 0x2545F491u; return s; }
const Rm8PlusTuning &T(){ return rm8PlusTuning(); }
int L(){ return std::max(1, std::min(3, g_level)) - 1; }
}

// [A] test / tuning hook: RM8_PLUS_TUNE="name=v1,v2,v3;name=v;..." overrides fields of Rm8PlusTuning without a rebuild (one value for a 3-column row = all difficulties)
namespace {
template<class T, size_t N> void tuneArr(const std::string &k, const char *name, T (&a)[N], const std::vector<double> &v){ if(k != name) return; for(size_t i = 0; i < N; i++) a[i] = (T)(v.size() == 1 ? v[0] : i < v.size() ? v[i] : (double)a[i]); }
template<class T> void tuneOne(const std::string &k, const char *name, T &a, const std::vector<double> &v){ if(k == name && !v.empty()) a = (T)v[0]; }
void applyTune(Rm8PlusTuning &t, const char *spec)
{
	std::stringstream ss(spec); std::string part;
	while(std::getline(ss, part, ';')){
		size_t eq = part.find('='); if(eq == std::string::npos) continue;
		std::string k = part.substr(0, eq); std::vector<double> v; std::stringstream vs(part.substr(eq + 1)); std::string x;
		while(std::getline(vs, x, ',')) v.push_back(atof(x.c_str()));
		tuneArr(k, "loanGateLiquid", t.loanGateLiquid, v); tuneArr(k, "loanIgnoreGatePct", t.loanIgnoreGatePct, v); tuneArr(k, "loanTopUpTarget", t.loanTopUpTarget, v);
		tuneArr(k, "loanMaxAmount", t.loanMaxAmount, v); tuneArr(k, "repayPct", t.repayPct, v); tuneArr(k, "repayMargin", t.repayMargin, v); tuneArr(k, "repayMinAgeDays", t.repayMinAgeDays, v);
		tuneOne(k, "repayOnPass", t.repayOnPass, v); tuneArr(k, "reserveDays", t.reserveDays, v); tuneArr(k, "reservePct", t.reservePct, v);
		tuneOne(k, "antiCampGraceTurns", t.antiCampGraceTurns, v); tuneOne(k, "antiCampBubbleTurns", t.antiCampBubbleTurns, v); tuneOne(k, "antiCampFloorDays", t.antiCampFloorDays, v);
		tuneArr(k, "exitTrapPct", t.exitTrapPct, v); tuneOne(k, "exitTrapNodes", t.exitTrapNodes, v); tuneOne(k, "exitTrapMaxDays", t.exitTrapMaxDays, v); tuneArr(k, "miniLowPct", t.miniLowPct, v);
			tuneArr(k, "hateThreshold", t.hateThreshold, v); tuneArr(k, "hateDecay", t.hateDecay, v); tuneArr(k, "hateMaxCount", t.hateMaxCount, v); tuneArr(k, "hateActPct", t.hateActPct, v);
			tuneArr(k, "hateHumanPct", t.hateHumanPct, v); tuneArr(k, "hateMaxPerDay", t.hateMaxPerDay, v); tuneArr(k, "hateReliefPct", t.hateReliefPct, v); tuneOne(k, "hateCap", t.hateCap, v);
			tuneOne(k, "hateCourtWin", t.hateCourtWin, v); tuneOne(k, "hateCourtLose", t.hateCourtLose, v); tuneOne(k, "hateRentUnit", t.hateRentUnit, v); tuneOne(k, "hateRentMin", t.hateRentMin, v);
			tuneOne(k, "hateRentMax", t.hateRentMax, v); tuneArr(k, "hateBlast", t.hateBlast, v); tuneOne(k, "hatePlotBlast", t.hatePlotBlast, v); tuneOne(k, "hatePlotBlastMax", t.hatePlotBlastMax, v);
			tuneOne(k, "hateMagic", t.hateMagic, v); tuneArr(k, "hateCard", t.hateCard, v);
			tuneArr(k, "comboBudget", t.comboBudget, v); tuneArr(k, "comboPct", t.comboPct, v); tuneArr(k, "comboRelax", t.comboRelax, v); tuneOne(k, "comboRentMin", t.comboRentMin, v);
			tuneArr(k, "blastAiPct", t.blastAiPct, v); tuneArr(k, "blastMinMissile", t.blastMinMissile, v); tuneArr(k, "blastMinNuke", t.blastMinNuke, v); tuneOne(k, "blastHatePct", t.blastHatePct, v);
			tuneOne(k, "blastRoleValue", t.blastRoleValue, v); tuneOne(k, "blastSelfFactor", t.blastSelfFactor, v); tuneOne(k, "blastNukeRange", t.blastNukeRange, v); tuneArr(k, "magicSelfGoodPct", t.magicSelfGoodPct, v);
	}
}
}
const Rm8PlusTuning &rm8PlusTuning()
{
	static Rm8PlusTuning t; static bool init = false;
	if(!init){ init = true; if(const char *e = getenv("RM8_PLUS_TUNE")){ applyTune(t, e); rm8Log("rules+: tuning override %s", e); } }
	return t;
}

static void hateTestInject(int day);
bool rm8RulesOn(){ return g_plus; }
int rm8RulesLevel(){ return std::max(1, std::min(3, g_level)); }

void rm8RulesBeginGame(unsigned seed)
{
	g_plus = rm8MenuOptions().plus;
	if(const char *e = getenv("RM8_RULES_PLUS")) g_plus = atoi(e) != 0;	// test hook: forces the switch for the game that starts
	if(int np = rm8NetOptPlus(); np >= 0) g_plus = np != 0;	// [N] a netplay game: the host's switch for every device (net.cpp)

	g_level = std::max(1, std::min(3, rm8Difficulty()));
	g_rng = mixSeed(seed * 40503u + 17u);
	for(bool &b : g_storyLoan) b = false;
	g_got = false;
	hateClear(); g_notes = 0;
	if(g_plus) rm8Log("rules+: house rules ON for this game, difficulty snapshot %d", g_level);
	hateTestInject(0);
}
void rm8RulesReseed(unsigned seed){ g_rng = mixSeed(seed * 40503u + 17u); }
int rm8PlusRand(){ g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return (int)((g_rng >> 8) & 0x7fff); }

// ---------------------------------------------------------------- B1 loans
void rm8RulesNoteStoryLoan(int seat){ if(g_plus && seat >= 0 && seat < 16){ g_storyLoan[seat] = true; rm8Log("rules+: the story-scripted loan of seat %d is left alone by the house rules", seat); } }
void rm8RulesLoanTaken(int seat){ if(seat >= 0 && seat < 16) g_storyLoan[seat] = false; }
bool rm8RulesStoryLoan(int seat)
{
	if(!g_plus || seat < 0 || seat >= 16 || !g_storyLoan[seat]) return false;
	if(rm8EventLoan(seat) <= 0){ g_storyLoan[seat] = false; return false; }	// the scripted loan is gone (repaid / collected)
	return true;
}

int32_t rm8RulesLoanDecide(int seat, int32_t liquid, int32_t asked)	// [N] int32_t
{
	if(!g_plus || asked <= 0) return asked;
	const Rm8PlusTuning &t = T(); const int l = L(); int32_t PI = rm8GamePI();	// [N] int32_t
	if(liquid >= t.loanGateLiquid[l] * PI){
		if(t.loanIgnoreGatePct[l] <= 0 || rm8PlusRand() % 100 >= t.loanIgnoreGatePct[l]){
			rm8Log("rules+: seat %d (%s) does not borrow: liquid %d >= gate %d (script asked %d)", seat, rm8TurnName(seat), liquid, t.loanGateLiquid[l] * PI, asked);
			return 0;
		}
		rm8Log("rules+: seat %d (%s) ignores the loan gate (liquid %d)", seat, rm8TurnName(seat), liquid);
	}
	int32_t amount = asked;	// [N] int32_t
	if(t.loanTopUpTarget[l] > 0){
		int32_t need = t.loanTopUpTarget[l] * PI - liquid, tier = need <= 100000 ? 100000 : need <= 200000 ? 200000 : 300000;	// [N] int32_t
		tier = std::min(tier, t.loanMaxAmount[l]);
		amount = std::min(asked, tier);
	}
	rm8Log("rules+: seat %d (%s) borrows %d (script asked %d, liquid %d, difficulty %d)", seat, rm8TurnName(seat), amount, asked, liquid, rm8RulesLevel());
	return amount;
}

bool rm8RulesRepayWanted(int seat, int32_t liquid, int32_t principal, int ageDays, bool passing)	// [N] int32_t
{
	if(!g_plus || principal <= 0 || rm8RulesStoryLoan(seat)) return false;
	const Rm8PlusTuning &t = T(); const int l = L();
	if(t.repayPct[l] <= 0 || (passing && !t.repayOnPass) || ageDays < t.repayMinAgeDays[l]) return false;
	return liquid >= principal * t.repayPct[l] / 100 + t.repayMargin[l] * rm8GamePI();
}

bool rm8RulesReserveVeto(int seat, int32_t liquid, int32_t price, int32_t principal, int daysLeft)	// [N] int32_t
{
	if(!g_plus || principal <= 0 || rm8RulesStoryLoan(seat)) return false;
	const Rm8PlusTuning &t = T(); const int l = L();
	if(t.reserveDays[l] <= 0 || daysLeft > t.reserveDays[l]) return false;
	return liquid - price < principal * t.reservePct[l] / 100;
}

// ---------------------------------------------------------------- B6
int rm8RulesMiniScore(int game, int orig)
{
	if(!g_plus || game < 18 || game > 23 || game == 21) return orig;
	const Rm8PlusTuning &t = T(); const int l = L();
	if(rm8PlusRand() % 100 >= t.miniLowPct[l]) return orig;
	int sc = t.miniLowBase[game] + rm8PlusRand() % std::max(1, t.miniLowSpan[game]);
	if(game == 23) sc *= 10;
	int r = std::min(orig, sc);	// "fewer tickets": never more than the original result
	rm8Log("rules+: minigame %d low score %d (original %d, difficulty %d)", game, r, orig, rm8RulesLevel());
	return r;
}

// ---------------------------------------------------------------- B3 hate
void rm8HateNote(int victim, int aggressor, float weight, const char *why)
{
	if(!g_plus || victim == aggressor || weight <= 0) return;
	const int n = std::min(8, rm8TurnRoleCount());
	if(victim < 0 || aggressor < 0 || victim >= n || aggressor >= n || rm8TurnStatus(victim) == 7 || rm8TurnStatus(aggressor) == 7) return;
	const Rm8PlusTuning &t = T(); const int l = L();
	if(rm8TurnHuman(aggressor)) weight = weight * t.hateHumanPct[l] / 100.0f;
	float &h = g_hate[victim][aggressor], before = h;
	h = std::min(t.hateCap, h + weight); g_notes++;
	rm8Log("rules+: hate: seat %d (%s) -> seat %d (%s): %.2f + %.2f = %.2f (%s)", victim, rm8TurnName(victim), aggressor, rm8TurnName(aggressor), before, weight, h, why ? why : "");
	if(before < t.hateThreshold[l] && h >= t.hateThreshold[l]) rm8Log("rules+: hate: seat %d (%s) now HATES seat %d (%s) (threshold %.1f)", victim, rm8TurnName(victim), aggressor, rm8TurnName(aggressor), t.hateThreshold[l]);
}
float rm8HateScore(int victim, int aggressor){ return victim >= 0 && victim < 8 && aggressor >= 0 && aggressor < 8 ? g_hate[victim][aggressor] : 0.0f; }
int rm8HateList(int seat, int out[8])
{
	if(!g_plus || seat < 0 || seat >= 8) return 0;
	const Rm8PlusTuning &t = T(); const int l = L(), n = std::min(8, rm8TurnRoleCount());
	int c = 0;
	for(int a = 0; a < n; a++) if(a != seat && g_hate[seat][a] >= t.hateThreshold[l] && rm8TurnStatus(a) != 7) out[c++] = a;
	std::sort(out, out + c, [&](int x, int y){ return g_hate[seat][x] != g_hate[seat][y] ? g_hate[seat][x] > g_hate[seat][y] : x < y; });
	return std::min(c, std::max(0, t.hateMaxCount[l]));
}
bool rm8HateAct(int seat, int target, const char *what)
{
	if(!g_plus || seat < 0 || seat >= 8 || target < 0 || target >= 8 || rm8TurnHuman(seat)) return false;
	const Rm8PlusTuning &t = T(); const int l = L();
	if(g_perDay[target] >= t.hateMaxPerDay[l]){ rm8Log("rules+: hate: seat %d (%s) holds back against seat %d (%s): %d revenge actions on it today already (%s)", seat, rm8TurnName(seat), target, rm8TurnName(target), g_perDay[target], what); return false; }
	if(rm8PlusRand() % 100 >= t.hateActPct[l]){ rm8Log("rules+: hate: seat %d (%s) lets seat %d (%s) off this time (%s, %d%%)", seat, rm8TurnName(seat), target, rm8TurnName(target), what, t.hateActPct[l]); return false; }
	g_perDay[target]++;
	float before = g_hate[seat][target]; g_hate[seat][target] = before * (100 - t.hateReliefPct[l]) / 100.0f;
	rm8Log("rules+: hate: seat %d (%s) takes revenge on seat %d (%s): %s (hate %.2f -> %.2f, revenge #%d today)", seat, rm8TurnName(seat), target, rm8TurnName(target), what, before, g_hate[seat][target], g_perDay[target]);
	return true;
}
// [A] test hook RM8_PLUS_TEST_HATE="victim:aggressor:weight:day;...": adds hate on that game day (0 = when the game starts; the weight is not scaled)
static void hateTestInject(int day)
{
	static const char *e = getenv("RM8_PLUS_TEST_HATE");
	if(!e || !g_plus) return;
	std::stringstream ss(e); std::string part;
	while(std::getline(ss, part, ';')){
		int v = -1, a = -1, d = 0; float w = 0;
		if(sscanf(part.c_str(), "%d:%d:%f:%d", &v, &a, &w, &d) == 4 && d == day && v >= 0 && a >= 0 && v < 8 && a < 8 && v != a){
			rm8Log("rules+: TEST hate: seat %d gets +%.1f hate for seat %d", v, w, a); g_hate[v][a] = std::min(T().hateCap, g_hate[v][a] + w);
		}
	}
}
void rm8HateNewDay()
{
	if(!g_plus) return;
	const Rm8PlusTuning &t = T(); const int l = L();
	for(auto &r : g_hate) for(float &f : r){ f *= t.hateDecay[l]; if(f < 0.05f) f = 0; }
	for(int &c : g_perDay) c = 0;
	hateTestInject(rm8GameDay());
	std::string tab; const int n = std::min(8, rm8TurnRoleCount());	// one line per day with every score >= 0.5: hate[victim <- aggressor]
	for(int v = 0; v < n; v++) for(int a = 0; a < n; a++) if(g_hate[v][a] >= 0.5f){ char b[64]; snprintf(b, sizeof b, " %d<-%d:%.2f%s", v, a, g_hate[v][a], g_hate[v][a] >= t.hateThreshold[l] ? "*" : ""); tab += b; }
	if(!tab.empty()) rm8Log("rules+: hate table, day %d (* = hated, threshold %.1f):%s", rm8GameDay(), t.hateThreshold[l], tab.c_str());
}
int rm8HateTotalLog(){ return g_notes; }

// ---------------------------------------------------------------- save: the RULE chunk (docs/save_port.md section 11)
// version, plus, level, rng state, seat count, then per seat: story-loan mark, anti-camping state (grace, chain, immune, camp). Only written while the rules are on:
// a record without the chunk = the original rules; the after hook makes a load always take the record's rule set (never the current option).
namespace {
void saveRulesWrite(Rm8SaveW &w)
{
	if(!g_plus) return;
	w.i32(1); w.i32(1); w.i32(g_level); w.u32(g_rng);
	int n = rm8TurnRoleCount(); w.i32(n);
	for(int s = 0; s < n; s++){
		int c[4]; rm8CardsPlusGet(s, c);
		w.i32(s < 16 && g_storyLoan[s] ? 1 : 0); for(int v : c) w.i32(v);
	}
}
void saveRulesRead(Rm8SaveR &r)
{
	int ver = r.i32(), plus = r.i32(), lvl = r.i32(); unsigned rng = r.u32();
	(void)ver;
	g_got = true; g_plus = plus != 0; g_level = std::max(1, std::min(3, lvl)); g_rng = rng ? rng : 1;
	hateClear();	// the HATE chunk (read after this one) fills them
	int n = 0; if(!r.opt(n)) return;
	for(int s = 0; s < n && s < 16 && r.left() >= 20; s++){
		g_storyLoan[s] = r.i32() != 0;
		int c[4]; for(int &v : c) v = r.i32();
		if(s < rm8TurnRoleCount()) rm8CardsPlusSet(s, c);
	}
}
void saveRulesAfter()
{
	if(!g_got){	// the record has no RULE chunk (original record / a game played with the rules off): the original rules
		if(g_plus) rm8Log("rules+: this record has no RULE chunk: the original rules");
		g_plus = false;
		for(bool &b : g_storyLoan) b = false;
		hateClear();
	}
	g_got = false;
	if(g_plus) rm8Log("rules+: loaded game with the house rules ON (difficulty snapshot %d)", g_level);
}
}

// the HATE chunk (docs/save_port.md section 11), only written while the rules are on: version, seat count n, hate[n][n] (f32), the per-target revenge counters of the day (n x i32),
// the owner of the time bomb on each role (n x i32). New fields only at the end.
namespace {
void saveHateWrite(Rm8SaveW &w)
{
	if(!g_plus) return;
	const int n = std::min(8, rm8TurnRoleCount());
	w.i32(1); w.i32(n);
	for(int a = 0; a < n; a++) for(int b = 0; b < n; b++) w.f32(g_hate[a][b]);
	for(int a = 0; a < n; a++) w.i32(g_perDay[a]);
	for(int a = 0; a < n; a++) w.i32(rm8NpcBombOwner(a));
}
void saveHateRead(Rm8SaveR &r)
{
	int ver = r.i32(), n = r.i32(); (void)ver;
	if(n < 0 || n > 8) return;
	for(int a = 0; a < n; a++) for(int b = 0; b < n; b++) g_hate[a][b] = std::max(0.0f, r.f32());
	for(int a = 0; a < n; a++){ int v = 0; if(r.opt(v)) g_perDay[a] = std::max(0, v); }
	for(int a = 0; a < n; a++){ int v = -1; if(r.opt(v)) rm8NpcSetBombOwner(a, v); }
}
}
RM8_SAVE_EXT(HATE, 2, saveHateWrite, saveHateRead);
RM8_SAVE_EXT_AFTER(RULE, 1, saveRulesWrite, saveRulesRead, saveRulesAfter);
