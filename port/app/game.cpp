// Property rules, after docs/rules_land.md: Terra plots, money, price functions, the landing chain
// 0x1D -> 0x12 -> 0x11 -> (9 buy | 0xB build | 0x1F/0x20 rent), new day (price index, monthly interest), bankruptcy.
// Computer decisions call the original Python (TBuy / TBuild) through python.cpp.
// [A] not yet: dialogs (the human currently answers like the computer), event icons (bank, cards, ...), gods and
//     possession, cards, wonders, auctions, hotel stay, build/demolish animations (levels change at once),
//     owner colours on the plot pads, news, loans, end of game.
#include "turn.h"
#include "events.h"
#include "cards.h"
#include "present.h"
#include "ui.h"
#include "boardfx.h"
#include "stock.h"
#include "npc.h"
#include "news.h"
#include "balance.h"
#include "story.h"
#include "save.h"
#include "rules_plus.h"
#include "net.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <vector>
#include <map>
#include <algorithm>

using namespace rw;

namespace {

const int32_t RENT_TABLE[6] = {500, 1000, 2500, 5000, 8000, 12500};	// [N] [V] 0x5e65b0
const int32_t MONEY_MAX = 999999999;

struct GTerra {
	int type = 1, street = 0, level = 0, owner = -1, pct = 100, build = 0;	// [V] Terra +0x3C lo/hi, +0x44, +0x4C, +0x48
	float pos[3] = {0, 0, 0}, yaw = 0;
	Clump *model = nullptr; int modelArt = 0;	// the plot's model and its art id (boardfx.cpp animates it)
};
struct GRole { int32_t cash = 0, deposit = 0, points = 50; int shape = 0, invisible = 0; bool bankruptChecked = false; int32_t rentIn = 0, rentOut = 0; int badDays = 0; };	// [N] rentOut = +0x33C (the rent paid), badDays = +0x340 (turn starts in sleep / hotel / hospital / prison / sleepwalk; the balance screen)

Rm8Scene *g_s = nullptr;
std::vector<GTerra> g_t;
std::vector<GRole> g_r;
struct StreetFx { int closeDays = 0, flowDays = 0; double mult = 1.0; };	// [V] street record +0x108 / +0x110 / +0x10C (rules_land.md 1.3)
std::map<int, StreetFx> g_stFx;	// by street id, for the cards (cards.cpp)
int32_t g_PI = 1, g_base = 100000;	// [N] int32_t
int g_days = 0;
bool g_over = false;	// [V] g_GameState+0x48 != 1 ([A] the end screens are not in the port yet)
int g_year = 2026, g_month = 1, g_mday = 1, g_wday = 0;
std::vector<std::string> g_texts;	// texts of queued dialog / info messages (the game passes string pointers)
int pushText(const std::string &t){ g_texts.push_back(t); return (int)g_texts.size() - 1; }
std::map<int, std::string> g_textPicFile;	// text index -> picture file name of the entry (Msg_CreateTextEntry +0x100)
std::map<int, int> g_textPic;	// text index -> picture (uvlist id) of the entry: Msg_CreateTextEntry @0x49a3a0 +0x200 (the info box then is gtinfo, Role_ShowInfoDialog @0x488ae0)
const std::string &textOf(int i){ static std::string e; return i >= 0 && i < (int)g_texts.size() ? g_texts[i] : e; }
std::string streetName(int id){ auto it = g_s->streetName.find(id); return it == g_s->streetName.end() ? std::string() : it->second; }
std::string buildingName(int id){ auto it = g_s->buildingName.find(id); return it == g_s->buildingName.end() ? std::string() : it->second; }
uint32 g_seed = 1;
int rnd(){ g_seed = g_seed * 214013u + 2531011u; return (g_seed >> 16) & 0x7fff; }	// MSVC rand()

bool validT(int t){ return t >= 0 && t < (int)g_t.size(); }
bool validR(int r){ return r >= 0 && r < (int)g_r.size(); }
int32_t streetPrice(int id){ auto it = g_s->streetPrice.find(id); return it == g_s->streetPrice.end() ? 100 : it->second; }	// [N] int32_t

// ---- price functions [V] docs/rules_land.md 1.8
int32_t buildUnitCost(const GTerra &t){ return (t.type == 1 ? 250 : 500) * g_PI; }			// [N] @0x4efd50
int32_t calcPrice(const GTerra &t, bool mulPI)							// @0x4efc80
{
	int32_t base = rm8Mul32(rm8Mul32(t.type == 1 ? 3000 : 5000, t.pct), streetPrice(t.street)) / 10000;	// [N] int32_t; the product wraps like the original's imul (only a percent grown by many news reaches 2^31)
	int32_t v = buildUnitCost(t) * t.level + base;
	return mulPI ? v * g_PI : v;
}
int32_t calcRent(int ti, bool chain)								// [N] @0x4efda0
{
	const GTerra &t = g_t[ti];
	if(t.type != 1) return 0;
	int32_t p = streetPrice(t.street), r = RENT_TABLE[t.level] * p / 100;	// [N] int32_t
	if(chain) for(int u = 0; u < (int)g_t.size(); u++){
		const GTerra &o = g_t[u];
		if(u != ti && o.type == 1 && o.street == t.street && o.owner == t.owner) r += RENT_TABLE[o.level] * p / 100;
	}
	return r * g_PI;
}

// ---- money [V] SFLB_MoneyAdd @0x47e640, Bank_WithdrawMoney @0x47e720
void addMoney(int r, int32_t v, bool deposit){ int32_t &a = deposit ? g_r[r].deposit : g_r[r].cash; a = std::min(MONEY_MAX, rm8Add32(a, v)); }	// [N] int32_t; a + v wraps like the original's add (v > 1147483648 only)
void subMoney(int r, int32_t v, bool depositFirst)
{
	GRole &g = g_r[r];
	int32_t &a = depositFirst ? g.deposit : g.cash, &b = depositFirst ? g.cash : g.deposit;	// [N] int32_t
	a -= v;
	if(a < 0){ b = std::max(0, b + a); a = 0; }	// [N] int32_t
}

// ---- models [V] Land_OnEnter @0x4ef580: building.bin art per (type, level); owned empty plot: sign art 1100 + role id
void updateModel(int ti)
{
	GTerra &t = g_t[ti];
	if(t.model){ rm8SceneRemoveArticle(*g_s, t.model); t.model = nullptr; }
	t.modelArt = 0;
	int art = 0, anim = 0;
	if(t.level > 0){
		int b = t.type == 1 ? 1 : t.build;
		auto it = g_s->buildingArt.find({b, t.level});
		if(it != g_s->buildingArt.end()) art = it->second;
		anim = t.type == 1 ? 2 : 0;	// [V] Building_UpdateBuildingVisual: residential level > 0 -> action 2, else 0
	}else if(t.owner >= 0) art = 1100 + rm8TurnCharId(t.owner);
	if(!art) return;
	auto d = g_s->arts.find(art);
	if(d == g_s->arts.end()){ rm8Log("rules: no model for art %d", art); return; }
	auto a = d->second.anims.find(anim);
	float one[3] = {1, 1, 1};
	t.model = rm8SceneAddArticle(*g_s, d->second.dff, d->second.texdir, t.pos, one, t.yaw, a == d->second.anims.end() ? "-" : a->second, "-", "loop");
	if(t.model) t.modelArt = art;
}

// [V] Role_CreateAvatar @0x480b30: role +0x1F4 = DAT_005e1d20[seat] (bytes R,G,B); red, yellow, blue, green
const rw::RGBA SEAT_COLOR[4] = {{0xE6, 0x3A, 0x43, 255}, {0xEA, 0xC7, 0x16, 255}, {0x33, 0x44, 0xEB, 255}, {0x18, 0xE9, 0x8C, 255}};
// [V] Land_SetOwner @0x4eec20: owner, pad colour (white without an owner), sign / building
void setOwner(int ti, int r)
{
	g_t[ti].owner = r; updateModel(ti);
	rm8SceneSetTerraColor(*g_s, ti, r >= 0 && r < 4 ? SEAT_COLOR[r] : rw::RGBA{255, 255, 255, 255});
}

// [V] Role_CheckRentExempt @0x486580 (no possession yet): street sealed / owner not normal / bankrupt -> exempt
// [V] the same function also queues the info box (0x1B, message at the end of the queue) and the emote:
//   street sealed (street +0x108 != 0)                       -> str10 (street name), return 1, no emote
//   owner status 0 and not possessed by 糊塗神               -> 0 (the rent is paid)
//   owner status 0 + possessed by 糊塗神, or status 1..6     -> str9 (owner's name), then the 70 % emote, return 1
//   owner status 7 (bankrupt)                                -> 1, no text, no emote
//   no owner role (not reachable in the port)                -> emote only
// callers: Role_ProcessArriveLand @0x4868e0 (residential, rent > 0, not invisible) and Role_OnArriveCommercialBuilding @0x486260 (chain store / hotel, rent argument 0), humans and AI alike
bool rentExempt(int me, int ti, int32_t rent)	// [N] int32_t
{
	{ auto f = g_stFx.find(g_t[ti].street); if(f != g_stFx.end() && f->second.closeDays > 0){
		rm8TurnEnqueue(me, 0x1B, pushText(rm8StrFmt(10, streetName(g_t[ti].street).c_str())), 0);
		return true; } }	// [V] street sealed (Street.Close)
	int o = g_t[ti].owner;
	if(!validR(o)){ rm8PresentRentExempt(me, rent); return true; }
	int st = rm8TurnStatus(o);
	if(st == 0 && g_r[o].shape != 6) return false;
	if(st == 7) return true;
	rm8TurnEnqueue(me, 0x1B, pushText(rm8StrFmt(9, rm8TurnName(o))), 0);
	if(getenv("RM8_DEBUG_RULES")) rm8Log("rules: seat %d: the owner (seat %d, status %d, shape %d) of plot %d is abnormal: str9 queued, rent exempt", me, o, st, g_r[o].shape, ti);
	rm8PresentRentExempt(me, rent);
	return true;
}

// [R] rules_plus B3: paying rent to a role is an attack on the payer: weight rent / hateRentUnit x PI (at most hateRentMax), nothing below hateRentMin x PI
void hateRent(int payer, int owner, int32_t rent)	// [N] int32_t
{
	if(!rm8RulesOn()) return;
	const Rm8PlusTuning &t = rm8PlusTuning();
	if(rent < t.hateRentMin * g_PI) return;
	rm8HateNote(payer, owner, std::min(t.hateRentMax, (float)rent / (float)(t.hateRentUnit * g_PI)), "rent paid");
}

// [V] AI_AskTBuy / AI_AskTBuild: the original Python callback with the plot handle; [A] if Python is not
// available the answer of the shipped scripts without possession (1) is used
bool askAI(int me, const char *fn, int ti)
{
	int32_t res = 0;	// [N] int32_t
	bool yes = rm8PyCallAI(rm8TurnAiScript(me), fn, ti + 1, res) ? res != 0 : true;
	// [R] rules_plus B1: before the loan is due the AI keeps the repayment reserve instead of buying / building (the script is still asked first; never a story-scripted loan)
	if(yes && rm8RulesOn() && validT(ti) && rm8EventLoan(me) > 0){
		int32_t price = !strcmp(fn, "TBuy") ? calcPrice(g_t[ti], true) : buildUnitCost(g_t[ti]), liquid = g_r[me].cash + g_r[me].deposit;	// [N] int32_t
		if(rm8RulesReserveVeto(me, liquid, price, rm8EventLoan(me), rm8EventLoanDaysLeft(me))){
			rm8Log("rules+: seat %d (%s) keeps the repayment reserve: no %s of plot %d (price %d, liquid %d, loan %d due in %d days)", me, rm8TurnName(me), fn, ti, price, liquid, rm8EventLoan(me), rm8EventLoanDaysLeft(me));
			return false;
		}
	}
	return yes;
}

// [V] 0x11 Role_ProcessArriveLand @0x4868e0
void arriveLand(int me, int node)
{
	int ti = rm8GameNodeTerra(node);
	if(getenv("RM8_DEBUG_RULES")) rm8Log("rules: seat %d lands on node %d terra %d owner %d", me, node, ti, validT(ti) ? g_t[ti].owner : -9);
	if(!validT(ti)) return;
	GTerra &t = g_t[ti];
	int s = g_r[me].shape;
	if(t.owner < 0){	// A. unowned: buy? (not while possessed by 土地公 / 糊塗神; the chain goes on to 0x22 either way)
		int32_t price = calcPrice(t, true);	// [N] int32_t
		if(s == 5 || s == 6){}
		else if(g_r[me].cash < price) rm8TurnEnqueue(me, 0x1B, pushText(rm8Str(2)), 0);	// [V] str2 (cash only)
		else if(rm8TurnHuman(me)){	// [V] 1(text, 0x80 okcancel) -> 0x1C (hide) -> 9(t, result)
			std::string txt = t.type == 1 ? rm8StrFmt(1, streetName(t.street).c_str(), (int)price) : rm8StrFmt(1, rm8Str(14).c_str(), (int)price);
			rm8TurnEnqueue(me, 1, pushText(txt), 0x80); rm8TurnEnqueue(me, 0x1C, 0, 0); rm8TurnEnqueue(me, 9, ti, (int)0x80000000);
		}else if(askAI(me, "TBuy", ti)) rm8TurnEnqueue(me, 9, ti, 1);
	}else if(t.owner == me){	// B. own: build? (not while possessed by 糊塗神 or at level 5)
		if(getenv("RM8_DEBUG_RULES")) rm8Log("rules: seat %d on own plot %d (level %d, cash %d)", me, ti, t.level, g_r[me].cash);
		int32_t cost = buildUnitCost(t);	// [N] int32_t
		if(s == 6 || t.level >= 5){}
		else if(g_r[me].cash < cost) rm8TurnEnqueue(me, 0x1B, pushText(rm8Str(2)), 0);
		else if(rm8TurnHuman(me)){	// [V] str3 (empty residential), str5 + business dialog (empty commercial), str4 (upgrade)
			std::string txt; int kind = 0x80;
			if(t.level == 0 && t.type == 1) txt = rm8StrFmt(3, streetName(t.street).c_str(), (int)cost);
			else if(t.level == 0){ txt = rm8StrFmt(5, streetName(t.street).c_str()); kind = 0x81; }
			else txt = rm8StrFmt(4, streetName(t.street).c_str(), t.level, buildingName(t.type == 1 ? 1 : t.build).c_str(), (int)cost);
			rm8TurnEnqueue(me, 1, pushText(txt), kind); rm8TurnEnqueue(me, 0x1C, 0, 0); rm8TurnEnqueue(me, 0xB, ti, (int)0x80000000);
		}else if(askAI(me, "TBuild", ti)) rm8TurnEnqueue(me, 0xB, ti, 1);
	}else{	// C. someone else's residential plot: rent
		if(t.type == 1 && g_r[me].invisible < 1){
			int32_t r = calcRent(ti, true);	// [N] int32_t
			r = rm8EventWonderRent(me, t.owner, r);	// [V] Wonder_CheckConstruction(1): the land wonder +-20 %
			if(r > 0 && !rentExempt(me, ti, r)){	// [V] Role_CheckRentExempt @0x486580 (rent > 0 only): queues its own info box / emote when it exempts
				std::string txt = t.level > 0 ? rm8StrFmt(7, streetName(t.street).c_str(), t.level, buildingName(1).c_str(), rm8TurnName(t.owner), (int)r)
				                              : rm8StrFmt(6, streetName(t.street).c_str(), rm8TurnName(t.owner), (int)r);
				rm8TurnEnqueue(me, 0x1B, pushText(txt), 0); rm8TurnEnqueue(me, 0x1F, (int)r, t.owner);
			}
		}
	}
	if(s != 4 || t.owner == me) rm8TurnEnqueue(me, 0x22, ti, 0);	// [V] 0x22 gods (nothing without possession)
}

// [V] 0x12 Role_OnArriveCommercialBuilding @0x486260: a built commercial plot with an owner: the institute of the own role (not while possessed by 糊塗神), the chain store /
// hotel of another role (not invisible, no rent exemption) open the building's dialog (event_build.cpp: market.ui / hotel.ui / graduate.ui, state 9); then 0x11 either way
void arriveCommercial(int me, int node)
{
	int ti = rm8GameNodeTerra(node);
	if(validT(ti)){
		GTerra &t = g_t[ti];
		if(t.owner >= 0 && t.level != 0 && t.type == 2){
			bool go = false;
			if(t.build == 2) go = t.owner == me && g_r[me].shape != 6;
			else if(t.build == 3 || t.build == 4) go = t.owner != me && g_r[me].invisible < 1 && !rentExempt(me, ti, 0);	// [V] rent argument 0: the emote is the smallest tier
			if(go) rm8EventBuilding(me, ti);
		}
	}
	rm8TurnEnqueue(me, 0x11, node, 0);
}

}	// namespace

void rm8GameClear(Rm8Scene &s)
{
	rm8BoardFxCancel();	// running build animations / the bankruptcy cutscene refer to the data below
	rm8NewsClear();		// news.cpp: the running news scene, the pick history, the lottery pool
	rm8EventSceneClear();	// events: the wonder buildings are articles of this scene (event_wonder.cpp)
	for(GTerra &t : g_t) if(t.model) rm8SceneRemoveArticle(s, t.model);
	g_t.clear(); g_r.clear(); g_s = nullptr; g_over = false; g_stFx.clear();
}

static Rm8GameOptions g_opts;
static int g_endMonth = 0, g_endDay = 0;	// end date of a limited game (time limit 0 / 1), 0 = none
void rm8GameSetOptions(const Rm8GameOptions &o){ g_opts = o; }
Rm8GameOptions rm8GameGetOptions(){ return g_opts; }	// [A] the win condition screen (infoscreens.cpp) reads setup +0x18 / +0x1C
// [V] Game_GetRandomValue @0x4a3e00 = Date_CalcDaysDiff @0x431680 (end date, today): the days left to the end date (month lengths of the calendar, the year of today for February),
// 100 for an unlimited game; the day itself is 0
int rm8GameDaysLeft()
{
	if(!g_endMonth) return 100;
	auto mlen = [](int m, int y){ if(m < 8) return m % 2 == 0 ? (m == 2 ? ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0 ? 29 : 28) : 30) : 31; return m % 2 == 0 ? 31 : 30; };
	int days = 0, m = g_month, y = g_year;
	if(m == g_endMonth && g_endDay >= g_mday) return g_endDay - g_mday;
	days = mlen(m, y) - g_mday;
	for(m = m % 12 + 1; m != g_endMonth; m = m % 12 + 1){ if(m == 1) y++; days += mlen(m, y); }
	return days + g_endDay;
}
static int g_leader = -1;
int rm8GameTakeLeader(){ int l = g_leader; g_leader = -1; return l; }	// the seat that wins at the end date: the turn goes to it (Game_TurnNextRole @0x4971a0, roles.cpp)

// [V] Game_InitGame @0x4a1090: cash = deposit = setup+0x0C / 2, tickets = min(setup+0x10, 9999), vehicle -> dice, PI 1, price base = mean wealth
void rm8GameNewGame(Rm8Scene &s, unsigned seed)
{
	rm8GameClear(s);
	rm8EventClear(); rm8CardClearAll();
	g_s = &s; g_seed = seed;
	for(const Rm8TerraDef &d : s.terras){
		GTerra t; t.type = d.type; t.street = d.street; t.level = d.level0; t.build = d.type == 1 && d.level0 > 0 ? 1 : 0;
		for(int k = 0; k < 3; k++) t.pos[k] = d.pos[k];
		t.yaw = d.yaw;
		g_t.push_back(t);
	}
	for(int i = 0; i < (int)g_t.size(); i++) updateModel(i);
	g_r.assign(rm8TurnRoleCount(), GRole());
	for(GRole &r : g_r){ r.cash = g_opts.cash / 2; r.deposit = g_opts.cash / 2; r.points = std::min(g_opts.tickets, 9999); }
	for(int k = 0; k < (int)g_r.size(); k++) rm8TurnSetDiceMax(k, std::max(1, std::min(3, g_opts.vehicle + 1)));	// [V] vehicle = clamp(setup+0x14 + 1, 1, 3)
	if(const char *sc = getenv("RM8_START_CASH")){ int k = -1; int32_t v = 0; if(sscanf(sc, "%d,%d", &k, &v) == 2 && validR(k)){ g_r[k].cash = v; g_r[k].deposit = 0; } }	// [N] test hook "seat,cash" (bankruptcy)
	g_PI = 1; g_base = std::max(1, g_opts.cash); g_days = 0;	// [V] price base = mean (cash + deposit) of the roles = the setup money (Game_CalcAvgPlayerWealth @0x4a6b30)
	rm8StockNewGame((int)g_r.size());	// stock.cpp: the listed stocks and their first 300 days (Stock_LoadFromINI @0x4b8e20)
	time_t now = time(nullptr); struct tm *lt = localtime(&now);
	if(lt){ g_year = lt->tm_year + 1900; g_month = lt->tm_mon + 1; g_mday = lt->tm_mday; g_wday = lt->tm_wday; }	// [V] first weekday from the system (0 = Sunday)
	{ int y, m, d, w; if(rm8NetStartDate(y, m, d, w)){ g_year = y; g_month = m; g_mday = d; g_wday = w; } }	// [N] a netplay game: the host's calendar day on every device

	if(const char *sd = getenv("RM8_START_DAY")){	// [A] test hook "<day>[,<weekday 1..7>]": the first real round (after the day-0 introduction round) is that day of the game, on that weekday (story events of day > 9 / Wednesdays)
		int n = atoi(sd), w = 0; if(const char *cm = strchr(sd, ',')) w = atoi(cm + 1);
		if(n > 1){ g_days = n - 1; if(w >= 1 && w <= 7) g_wday = w == 1 ? 7 : w - 1; }	// the wrap after the introduction round advances day and weekday once
		else if(w >= 1 && w <= 7) g_wday = w;
	}
	g_texts.clear();
	rm8SaveGameNewGame();	// save.cpp: a new game's start date / the test seed
	// [V] 0x4a1090 (lines copying +4..+0x14 to +0x18..+0x28, then month + 3 / + 6, - 12 above 12): end date = today's month + 3 (limit 0) or + 6 (limit 1), same day, year unchanged;
	// the check is Date_CalcDaysDiff(0x431680) == 0 on the day itself (Game_GetRandomValue @0x4a3e00, only for limits 0 / 1)
	g_endMonth = g_endDay = 0;
	if(g_opts.timeLimit < 2){ g_endMonth = (g_month - 1 + (g_opts.timeLimit == 0 ? 3 : 6)) % 12 + 1; g_endDay = g_mday; }
	if(g_endMonth && getenv("RM8_END_DAYS")){ g_endMonth = g_month; g_endDay = g_mday + atoi(getenv("RM8_END_DAYS")); }	// test hook: the end date n days from today (within the month)
	rm8Log("rules: %d plots, %d roles, start money %d + %d, tickets %d, vehicle %d, time limit %d, win condition %d", (int)g_t.size(), (int)g_r.size(), g_opts.cash / 2, g_opts.cash / 2, g_opts.tickets, g_opts.vehicle, g_opts.timeLimit, g_opts.winCond);
}

static void settle(int me);
bool rm8GameMessage(int me, int cmd, int a, int b)
{
	if(!g_s || !validR(me)) return false;
	if(rm8PresentMessage(me, cmd, a, b)) return true;	// present.cpp: speech / expressions 0x5A, effects 0x2C 0x52 0x53, hand-offs 0x2E, speech box 0x54..0x56 (observes the money messages)
	if(rm8NewsMessage(me, cmd, a, b)) return true;	// news.cpp: the messages of the news effects 0x31 0x40 0x49 0x4A 0x4F 0x5B 0x5C 0x5F (before the cards, which swallow 0x4A)
	if(rm8CardMessage(me, cmd, a, b)) return true;	// cards.cpp: card use, statuses, street / land effects of the CardPy scripts
	if(rm8NpcMessage(me, cmd, a, b)) return true;	// npc_act.cpp: 0x23 NPC interaction, 0x32 robot doll, 0x3A / 0x51 time bomb, 0x3C news, 0x58 puzzle, 0x59 seal, 0x60 mine, 0x81
	if(rm8StoryMessage(me, cmd, a, b)) return true;	// story.cpp: the win / lose chain 0x74 0x75 0x76 0x77 0x7a 0x7b 0x7c 0x7d 0x7e (menu games only; docs/story_mode.md 1.8 / 1.9)
	switch(cmd){
	case 0x1D: {	// [V] Role_OnArriveNode @0x4861a0
		int node = a;
		const Rm8RoadNode *n = node >= 0 && node < (int)g_s->rnodes.size() ? &g_s->rnodes[node] : nullptr;
		{ static const bool dbg = getenv("RM8_DEBUG_EVENTS") != nullptr; if(dbg) rm8Log("rules: seat %d arrives at node %d (event %d, shape %d)", me, node, n ? n->event : -1, g_r[me].shape); }
		if(n && n->event > 0 && n->event != 7 && n->event != 8 && g_r[me].shape != 6){ rm8EventLand(me, n->event, node); return true; }	// [V] Role_TriggerLandEvent (events.cpp)
		if(rm8EventWonderNode(me, node)) return true;	// [V] Role_OnArriveNode @0x4861a0 -> Role_TriggerLandLevelEvent @0x4863f0: a wonder site starts its event (no 0x12 / 0x11 then)
		rm8TurnEnqueue(me, 0x12, node, 0);
		return true; }
	case 0x12: arriveCommercial(me, a); return true;
	case 0x11: arriveLand(me, a); return true;
	case 9: {	// [V] Role_BuyLandConfirmed @0x487030 (no possession: plain purchase)
		if(!b || (unsigned)b == 0x80000000u || !validT(a)) return true;
		int32_t price = calcPrice(g_t[a], true);	// [N] int32_t
		if(g_r[me].shape == 3){	// [V] Role_BuyLandConfirmed @0x487030: 衰神 - the price is paid, no land: str412, 70 %: speak 4 / 5
			subMoney(me, price, false);
			rm8TurnEnqueueAt(me, 0, 0x1B, pushText(rm8Str(412)), 0);
			if(rm8PresentChance70()) rm8RoleSayAt(me, 1, 4 + (rnd() & 1));
			rm8Log("rules: seat %d (衰神) pays %d for plot %d and gets nothing", me, price, a);
			return true;
		}
		setOwner(a, me);
		rm8BoardFxLandTaken();	// [V] Role_TakeLandOwnership @0x487250: sample 1, humans and computers alike
		if(g_r[me].shape == 4) price = 0;	// [V] 福神: free
		if(price > 0) subMoney(me, price, false);
		rm8Log("rules: seat %d buys plot %d for %d", me, a, price);
		rm8PresentBuy(me, a, price);
		if(g_r[me].shape == 4) rm8TurnEnqueueAt(me, 0, 0x1B, pushText(rm8Str(414)), 0);	// str414 at the front, the emote behind it
		return true; }
	case 0xB: case 0xC: {	// [V] Role_BuildOnLand @0x487380 (0xC free)
		if(!b || (unsigned)b == 0x80000000u || !validT(a)) return true;
		GTerra &t = g_t[a];
		if(cmd == 0xB) subMoney(me, buildUnitCost(t), false);
		if(cmd == 0xB && g_r[me].shape == 3){	// [V] Role_BuildOnLand @0x487380: 衰神 - the cost is paid, nothing is built: str412, wait 30 frames, 70 %: speak 4 / 5
			rm8TurnEnqueueAt(me, 0, 0x1B, pushText(rm8Str(412)), 0);
			rm8TurnEnqueueAt(me, 1, 0x65, 30, 0);
			if(rm8PresentChance70()) rm8RoleSayAt(me, 2, 4 + (rnd() & 1));
			return true;
		}
		int nb = t.build, nl = t.level;	// the new kind / level (the rand() is drawn now, like the original); written halfway through the animation
		if(t.type == 1){ nb = 1; nl++; }
		else if(t.level == 0){ nb = (b >= 2 && b <= 4) ? b : rnd() % 3 + 2; nl = 1; }
		else nl++;
		nl = std::min(nl, 5);
		auto apply = [a, nb, nl]() -> Rm8BuildModel { GTerra &u = g_t[a]; u.build = nb; u.level = nl; updateModel(a); return {u.model, u.modelArt}; };	// [V] Land_OnEnter
		if(!rm8BoardFxBuildStart(me, t.pos, t.type == 1, t.level, {t.model, t.modelArt}, apply)) apply();	// [V] NPC_CreateAndDisplay @0x4F4B50 (boardfx.cpp)
		rm8Log("rules: seat %d builds plot %d to level %d", me, a, nl);
		rm8PresentBuild(me, a, nl);
		return true; }
	case 0x1F: {	// [V] Role_PayToll @0x487c80: 財神 pays nothing (str407), 窮神 double (str409), the owner's road bully (+0x1C0) redirects the rent
		int32_t r = a; int owner = b, pos = 0, sh = g_r[me].shape;	// [N] int32_t
		if(sh == 1){ r = 0; rm8TurnEnqueueAt(me, 0, 0x1B, pushText(rm8Str(407)), 0); pos = 1; }
		else if(sh == 2){ r = a * 2; rm8TurnEnqueueAt(me, 0, 0x1B, pushText(rm8Str(409)), 0); pos = 1; }
		else if(rm8CardsHasBodyguard(me)){ r = 0; rm8TurnEnqueueAt(me, 0, 0x1B, pushText(rm8Str(567)), 0); pos = 1; }	// [V] vf+0x64 (莎拉 with her bodyguard): str567, nothing is paid
		if(r < 1){	// 70 %: speak 0xD / 0xE / 0xF by the original rent (PI * 1000 / 5000)
			if(rm8PresentChance70()) rm8RoleSayAt(me, pos, a >= g_PI * 5000 ? 0xF : a >= g_PI * 1000 ? 0xE : 0xD);
			return true;
		}
		int ben = validR(owner) ? rm8NpcBully(owner) : -1;
		if(ben >= 0 && validR(ben) && rm8TurnStatus(ben) != 7){	// [V] Role_TransferRentRoadblock @0x488100: me pays (cash first), the beneficiary gets it on the deposit
			subMoney(me, r, false); g_r[me].rentOut = rm8Add32(g_r[me].rentOut, r);	// [N] the totals of a long game wrap like the original's 32-bit add
			rm8TurnEnqueueAt(me, pos, 0x1B, pushText(rm8StrFmt(566, rm8TurnName(ben))), 0);	// str566 "<str#566>，<str#566>%s"
			addMoney(ben, r, true); g_r[ben].rentIn = rm8Add32(g_r[ben].rentIn, r);
			rm8Log("rules: seat %d pays rent %d to seat %d (road bully of seat %d)", me, r, ben, owner);
			hateRent(me, ben, r);
			if(rnd() % 100 < 60 && rm8TurnStatus(owner) == 0){
				rm8TurnEnqueueAt(me, pos + 1, 0x2e, owner + 1, 0); rm8RoleSay(owner, 0x57); rm8TurnEnqueue(owner, 0x2e, me + 1, 0);
			}
			return true;
		}
		rm8TurnEnqueueAt(me, pos, 0x20, (int)r, owner);
		return true; }
	case 0xA: {	// [V] Role_TakeLandOwnership @0x487250 (t, b): the owner changes, the buildings stay; b != 0: the previous owner may react
		if(!validT(a)) return true;
		int prev = g_t[a].owner;
		setOwner(a, me);
		rm8BoardFxLandTaken();
		if(b) rm8PresentLandTaken(me, prev);
		return true; }
	case 0x20: {	// [V] Role_TransferRentToOwner @0x487f20 (no frame-up card yet)
		int32_t r = a; int owner = b;	// [N] int32_t
		if(!validR(owner)) return true;
		subMoney(me, r, false); g_r[me].rentOut = rm8Add32(g_r[me].rentOut, r);	// [N] 32-bit wrap like the original
		rm8BoardFxRentPaid();	// [V] sample 0x7B right after Role_SubMoney (@0x487f20)
		addMoney(owner, r, true); g_r[owner].rentIn = rm8Add32(g_r[owner].rentIn, r);	// [V] the owner receives the full amount into the deposit
		rm8Log("rules: seat %d pays rent %d to seat %d", me, r, owner);
		hateRent(me, owner, r);
		rm8PresentRent(me, owner, r);
		return true; }
	case 0x7F: rm8BoardFxBankruptcy(me); return true;	// [V] CPlayerTag__OnMovieDrama @0x492F30
	case 0x22: {	// [V] Role_ApplyGodOnLand @0x48bb50 (status < 3): 福神 on an own plot below level 5 builds for free (str415), 土地公 takes a plot that is not his (str416)
		if(!validT(a) || rm8TurnStatus(me) >= 3) return true;
		GTerra &t = g_t[a]; int sh = g_r[me].shape;
		if(sh == 4 && t.owner == me && t.level < 5){
			rm8TurnEnqueueAt(me, 0, 0x1B, pushText(rm8Str(415)), 0);
			rm8TurnEnqueueAt(me, 1, 0xC, a, t.type == 1 || t.level != 0 ? 1 : rnd() % 3 + 2);
		}else if(sh == 5 && t.owner != me){
			rm8TurnEnqueueAt(me, 0, 0x1B, pushText(rm8Str(416)), 0);
			rm8TurnEnqueueAt(me, 1, 0xA, a, 1);
		}
		return true; }
	case 1:	// [V] Role_OpenModalUi(kind = b & 0xFFFF, text): state 7 until the dialog answers
		if(rm8CardModal(me, a, b)) return true;	// card selections (kinds 1 2 8 0x20 0x82..0x85)
		rm8HudOpenModal(me, b & 0xFFFF, textOf(a)); rm8TurnSetState(me, 7);
		return true;
	case 0x1C: rm8HudCloseModal(); rm8PresentHideSpeech(); return true;	// [V] hide the dialog
	case 0x1B:	// [V] info box: first pass re-queues itself (b = 1) and focuses; second shows it for 180 frames
		if(!b){ rm8TurnEnqueueAt(me, 0, 0x1B, a, 1); return true; }
		{ auto pf = g_textPicFile.find(a); auto pic = g_textPic.find(a);
		  if(pf != g_textPicFile.end()) rm8HudInfoPicFile(textOf(a), pf->second);	// [V] +0x100 (the name) is tested first
		  else if(pic != g_textPic.end() && pic->second > 0) rm8HudInfoPic(textOf(a), pic->second); else rm8HudInfo(textOf(a)); }
		rm8TurnEnqueueAt(me, 0, 0x1C, 0, 0); rm8TurnEnqueueAt(me, 0, 0x65, 180, 0);
		return true;
	case 0x5A: return true;	// (present.cpp handles it first)
	case 0x23: return true;	// [A] NPC interaction not yet
	case 0x72: settle(me); return true;
	case 0x73:	// [V] CPlayerTag__OnRoleBankruptcy @0x48f750: status 7, state 0, next role if it was this one's turn
		rm8HudSeatBankrupt(me);
		rm8TurnSetStatus(me, 7);
		rm8TurnSkip(me);
		return true;
	case 0x74: case 0x75:	// [V] 0x74 CPlayerTag__AskContinueNextRound @0x48f8e0 (game over), 0x75 the winner's ending @0x48ff70
		rm8Log("rules: game over (%s)", cmd == 0x74 ? "no human left" : "one human left");
		g_over = true;
		rm8TurnSetStatus(me, 7);
		rm8TurnSkip(me);
		return true;
	}
	return rm8EventMessage(me, cmd, a, b);	// events.cpp: 4 receive card, 7 / 8 points, 0x47, 0x4E
}

// [V] Game_OnTurnStart(1) @0x4a3b50: day 0 -> 1 only; then advance the date, price index, monthly interest
void rm8GameOnNewRound()
{
	if(!g_s) return;
	if(g_days == 0){ g_days = 1; return; }
	g_days++;
	static const int ML[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int ml = ML[g_month] + (g_month == 2 && ((g_year % 4 == 0 && g_year % 100 != 0) || g_year % 400 == 0));
	if(++g_mday > ml){ g_mday = 1; if(++g_month > 12){ g_month = 1; g_year++; } }
	g_wday = g_wday % 7 + 1;	// [V] weekdays cycle 1..7 (7 = Sunday)
	rm8HateNewDay();	// [R] rules_plus B3: the hate scores decay (no-op with the house rules off)
	for(auto &kv : g_stFx){	// [V] Terra_UpdateHeightAll @0x42d650: close / flow days - 1; the flow ends: price = ftol(price / multiplier)
		StreetFx &f = kv.second;
		if(f.closeDays > 0) f.closeDays--;
		if(f.flowDays > 0 && --f.flowDays == 0){ int &p = g_s->streetPrice[kv.first]; p = (int)(p / f.mult); f.mult = 1.0; }
	}
	if(g_PI < 99){	// [V] Game_UpdatePriceIndex @0x4a6a00 (stocks in the net worth; no loans yet)
		int32_t sum = 0; int n = 0;	// [N] int32_t
		for(int r = 0; r < (int)g_r.size(); r++){
			if(rm8TurnStatus(r) == 7) continue;
			int32_t w = rm8Add32(rm8Add32(g_r[r].cash, g_r[r].deposit), rm8StockValue(r));	// [N] int32_t; the net worth sums wrap like the original's 32-bit adds (2 x 999999999 + stocks can pass 2^31)
			for(GTerra &t : g_t) if(t.owner == r) w = rm8Add32(w, calcPrice(t, false));
			sum = rm8Add32(sum, w); n++;
		}
		if(n){ int32_t p = (sum / n) / g_base; if(p > g_PI) g_PI = std::min(p, 99); }	// [N] int32_t
	}
	if(g_mday == 1) rm8NpcMonthStart();	// [V] Game_OnTurnStart @0x4a3b50: day 1 of a month -> Event_OnNpcEnable @0x49d9c0 (the gods and a board character come back)
	rm8EventInstituteTick();	// [V] TurnStart_UpdateTerrain @0x4f11f0 (Game_OnTurnStart): Institute_CheckCompletion for every plot
	rm8StockNewDay(g_wday);	// [V] Game_OnTurnStart @0x4a3b50: after the price index, on weekdays < 6 Stock_CheckUpdateTimer @0x4b9240 (stock.cpp)
	rm8Log("rules: day %d (%04d-%02d-%02d) price index %d", g_days, g_year, g_month, g_mday, g_PI);
	// [V] Game_OnTurnStart @0x4a3b50: on the 1st (day > 5) Game_TriggerNormalEvent starts the monthly balance task 0x52 (balance.cpp: it credits the interest, Balance_ShowDepositInfo @0x4afa40),
	// on the 15th Game_TriggerHolidayEvent @0x4a66b0 (the lottery draw 0x62); both are in rm8EventDaily (events.cpp). A task that started skips the date news
	bool dayEvent = (g_days > 5 || rm8BalanceForced()) && rm8EventDaily(g_mday);
	if(g_days > 5 && g_mday == 1 && !dayEvent){	// [A] no balance task (the data / UI is missing): the interest is credited silently (Balance_ShowDepositInfo: deposit / 10; the money wonder: its owner / 5, the others / 20)
		for(int r = 0; r < (int)g_r.size(); r++) if(rm8TurnStatus(r) != 7) addMoney(r, g_r[r].deposit / rm8EventInterestDivisor(r), true);
		dayEvent = true;
	}
	if(!rm8NewsNewDay(dayEvent) && !dayEvent && rm8StoryMonsterDue() && rm8TurnStatus(0) != 7) rm8NewsStart(0, 0x1e, -1);	// [V] Game_TriggerEventByDate @0x4a6780 story part: the monster news (story.cpp decides)
	// [V] Game_CheckFestival @0x4a5fb0 (message 0x3C) / Game_TriggerEventByDate @0x4a6780 (Saturday, days / 7 odd: the news task 0x11), news.cpp
	// [V] Game_TurnNextRole @0x4971a0 -> Game_FindLeaderByWinCondition @0x4a6090 (free game): on the end date (0x4a3e00 / 0x431680 == 0) the unbankrupt seat with the largest value
	// (strictly greater, first seat wins ties) of setup+0x1C wins: 0 residential plots, 1 assets, 2 deposit, 3 points, 4 stocks (no stocks in the port: 0); 5 = bankruptcy only.
	// The announcement is str 7057 "<str#7057>，%s獲勝！". [A] the ending itself (0x75, balance) is not in the port: the text is shown and play goes on
	if(!g_over && g_endMonth && g_month == g_endMonth && g_mday == g_endDay && g_opts.winCond < 5 && !rm8StoryActive()){	// [V] 0x4a6090 only runs for the free game (setup +0 == 0)
		int best = -1; int32_t bv = 0;	// [N] int32_t
		for(int r = 0; r < (int)g_r.size(); r++){
			if(rm8TurnStatus(r) == 7) continue;
			int32_t v = 0;	// [N] int32_t
			if(g_opts.winCond == 0){ for(GTerra &t : g_t) if(t.owner == r && t.type == 1) v++; }
			else if(g_opts.winCond == 1){ v = rm8Add32(rm8Add32(g_r[r].cash, g_r[r].deposit), rm8StockValue(r)); for(GTerra &t : g_t) if(t.owner == r) v = rm8Add32(v, calcPrice(t, false)); }	// [N] 32-bit wrap like the original
			else if(g_opts.winCond == 2) v = g_r[r].deposit;
			else if(g_opts.winCond == 3) v = g_r[r].points;
			else if(g_opts.winCond == 4) v = rm8StockValue(r);	// [V] Game_FindLeaderByWinCondition @0x4a6090: value 4 = the stock market value
			if(bv < v){ best = r; bv = v; }
		}
		if(best >= 0){
			g_over = true; g_leader = best;
			rm8Log("rules: time is up, seat %d wins (condition %d, value %d)", best, g_opts.winCond, bv);
			// [V] Game_FindLeaderByWinCondition @0x4a6090 (this = the leader): its queues are cleared, str 7057 "<str#7057>，%s獲勝！", then 0x75(0, 1) (the winner's ending
			// 0x48ff70 -> 0x7e win scene, 0x7d, 0x7a -> game state 4 = the map menu, story.cpp); Game_TurnNextRole gives the turn to the leader (rm8GameTakeLeader)
			rm8TurnClearQueue(best);
			rm8TurnEnqueue(best, 0x1B, pushText(rm8StrFmt(7057, rm8TurnName(best))), 0);
			rm8TurnEnqueue(best, 0x75, 0, 1);
		}
	}
}

// [V] Role_CheckBankruptcy @0x48ee10, general case: lands lose their owner (houses stay), status 7, next turn
// [A] no auction, no stock/cards, no end-of-game handling yet
// [V] Role_CheckBankruptcy @0x48ee10 (rules_land.md 3.3): days >= 1 and cash + deposit <= 0 (stocks / land not counted)
bool rm8GameCheckBankruptcy(int me)
{
	if(!validR(me) || g_r[me].bankruptChecked) return false;
	if(g_days < 1 || g_r[me].cash + g_r[me].deposit > 0) return false;
	{ int sp = rm8StoryBankruptSpecial(me);	// [V] story, seat 0: 金泡菜 is rescued once, 莎拉's bankruptcy wins (story.cpp)
	  if(sp == 0) return false;
	  if(sp == 1){ g_r[me].bankruptChecked = true; return true; } }
	g_r[me].bankruptChecked = true;
	rm8Log("rules: seat %d is bankrupt", me);
	// [V] clear the queue, str91 "%s<str#91>" ([A] the expression 0x1A and the cutscene 0x7F are not in the port)
	rm8TurnClearQueue(me);
	rm8TurnEnqueue(me, 0x1B, pushText(rm8StrFmt(91, rm8TurnName(me))), 0);
	rm8RoleSay(me, 0x1A);	// [V] Role_CheckBankruptcy @0x48ee10: thunk_FUN_00484890(0x1A) after the text
	rm8TurnEnqueue(me, 0x7F, 0, 0);	// [V] Role_EnqueueMsg(0x7F): the ghost cutscene (boardfx.cpp)
	int h = 0, c = 0, humans = 0;
	for(int k = 0; k < (int)g_r.size(); k++){
		if(rm8TurnIsPlayer(k)) humans++;	// [V] Role_CheckBankruptcy @0x48ee10 counts +0x1B0 != 1 (a human or a seat taken over by the computer) against == 1 (a computer)
		if(k == me || rm8TurnStatus(k) == 7) continue;
		if(rm8TurnIsPlayer(k)) h++; else c++;
	}
	// [V] h == 0: 0x74 game over; h == 1 && c == 0: 0x75 the winner's ending; else 0x72 settlement (+ auctions) and 0x73.
	// [A] a demo game without any human seat keeps going until one role is left
	if(humans == 0 && h + c >= 1){ h = 2; }
	if(h == 0) rm8TurnEnqueue(me, 0x74, 0, 0);
	else if(h == 1 && c == 0){	// [V] 0x48f245..0x48f26c: 0x73 for this role, then the surviving human's queue (ECX = [ebp-8], not `this`) is cleared and gets 0x75(0, 1)
		int surv = -1;
		for(int k = 0; k < (int)g_r.size(); k++) if(k != me && rm8TurnStatus(k) != 7 && rm8TurnIsPlayer(k)) surv = k;
		rm8TurnEnqueue(me, 0x73, 0, 0);
		if(surv >= 0){ rm8TurnClearQueue(surv); rm8TurnEnqueue(surv, 0x75, 0, 1); }
	}
	else {
		rm8TurnEnqueue(me, 0x72, 0, 0);
		// [V] Role_CheckBankruptcy @0x48ee10: the first 9 own plots; a later plot with a building replaces a candidate of a lower level when candidate.level <= rand() % 5;
		// then 3 random picks (without repeats) each queue 0x4F(plot, 0) (the wizard marker, news.cpp) + 0x40(plot, 1) (the bankruptcy auction, event 0x51, event_vendue.cpp)
		{
			std::vector<int> cand;
			for(int ti = 0; ti < (int)g_t.size(); ti++){
				if(g_t[ti].owner != me) continue;
				if((int)cand.size() < 9) cand.push_back(ti);
				else if(g_t[ti].level != 0){
					for(size_t k = 0; k < cand.size(); k++)
						if(g_t[cand[k]].level < g_t[ti].level && g_t[cand[k]].level <= rnd() % 5){ cand[k] = ti; break; }
				}
			}
			for(int k = 0; k < 3 && !cand.empty(); k++){
				int i = rnd() % (int)cand.size(), t = cand[i];
				cand.erase(cand.begin() + i);
				rm8TurnEnqueue(me, 0x4f, t, 0); rm8TurnEnqueue(me, 0x40, t, 1);
			}
		}
		rm8TurnEnqueue(me, 0x73, 0, 0);
	}
	return true;
}

// [V] CPlayerTag__OnRoleSettlement @0x48f500: plots lose their owner (colour reset, buildings stay), cards cleared, bankrupt
// portrait npc\<pic>_14 on the seat. [A] wonders, lottery, stocks, power company / ticket centre are not in the port
static void settle(int me)
{
	for(int i = 0; i < (int)g_t.size(); i++) if(g_t[i].owner == me) setOwner(i, -1);
	for(int k = 0; k < rm8CardMax(); k++) rm8CardRemoveAt(me, k);	// [V] Deck_ClearSlots: 8 fixed slots (an empty slot stays empty)
	rm8StockSellAll(me);	// [V] Stock_SellAll @0x4992c0 + Stock_RefreshAllHolders: the shares go back to the market, nothing is paid
	rm8HudSeatBankrupt(me);
}

int32_t rm8GameCash(int r){ return validR(r) ? g_r[r].cash : 0; }	// [N] int32_t
int32_t rm8GameDeposit(int r){ return validR(r) ? g_r[r].deposit : 0; }
int32_t rm8GamePI(){ return g_PI; }
int rm8GameDay(){ return g_days; }
int rm8GameShape(int r){ return validR(r) ? g_r[r].shape : 0; }
int rm8GameTerraCount(){ return (int)g_t.size(); }
int rm8GameTerraOwner(int t){ return validT(t) ? g_t[t].owner : -1; }
int rm8GameTerraLevel(int t){ return validT(t) ? g_t[t].level : 0; }
int rm8GameTerraType(int t){ return validT(t) ? g_t[t].type : 0; }
int rm8GameTerraStreet(int t){ return validT(t) ? g_t[t].street : 0; }
int32_t rm8GameTerraCharge(int t){ return validT(t) ? (g_t[t].owner < 0 ? calcPrice(g_t[t], true) : buildUnitCost(g_t[t])) : 0; }	// [N] [V] Terra.Charge
int32_t rm8GameTerraRent(int t, bool chain){ return validT(t) ? calcRent(t, chain) : 0; }
int rm8GameNodeTerra(int node){ return g_s && node >= 0 && node < (int)g_s->rnodes.size() ? g_s->rnodes[node].terra : -1; }
int rm8GameTerraNode(int t){ if(!g_s) return -1; for(int n = 0, N = (int)g_s->rnodes.size(); n < N; n++) if(g_s->rnodes[n].terra == t) return n; return -1; }
int rm8GameNodeCount(){ return g_s ? (int)g_s->rnodes.size() : 0; }
bool rm8GameNodeTile(int node, int &tx, int &ty){ if(!g_s || node < 0 || node >= (int)g_s->rnodes.size()) return false; tx = g_s->rnodes[node].tx; ty = g_s->rnodes[node].ty; return true; }
std::string rm8GameMapId(){ return g_s ? g_s->mapId : std::string(); }
// articles the event modules put on the board (the wonder buildings, event_wonder.cpp); removed by the owner or at rm8GameClear
rw::Clump *rm8GameAddModel(const std::string &dff, const std::string &texdir, const float pos[3], float yaw, const std::string &anim, bool loop)
{
	if(!g_s) return nullptr;
	float one[3] = {1, 1, 1};
	return rm8SceneAddArticle(*g_s, dff, texdir, pos, one, yaw, anim.empty() ? "-" : anim, "-", loop ? "loop" : "once");
}
rw::Clump *rm8GameAddArt(int art, const float pos[3], float yaw, int anim)	// an art of the scene's art table (building.bin models)
{
	if(!g_s) return nullptr;
	auto d = g_s->arts.find(art);
	if(d == g_s->arts.end()) return nullptr;
	auto an = d->second.anims.find(anim);
	return rm8GameAddModel(d->second.dff, d->second.texdir, pos, yaw, an == d->second.anims.end() ? std::string("-") : an->second, true);
}
int rm8GameBuildingArt(int id, int level){ if(!g_s) return 0; auto it = g_s->buildingArt.find({id, level}); return it == g_s->buildingArt.end() ? 0 : it->second; }	// building.bin art of (building id, level)
void rm8GameRemoveModel(rw::Clump *c){ if(g_s && c) rm8SceneRemoveArticle(*g_s, c); }
bool rm8GameHud(int r, int32_t &cash, int32_t &dep){ if(!validR(r)) return false; cash = g_r[r].cash; dep = g_r[r].deposit; return true; }	// [N] int32_t
void rm8GameDate(int &y, int &m, int &d, int &w){ y = g_year; m = g_month; d = g_mday; w = g_wday; }
int32_t rm8GamePoints(int r){ return validR(r) ? g_r[r].points : 0; }	// [N] int32_t
// shared money / points / text access for events.cpp, cards.cpp, python.cpp
void rm8GameAddMoney(int r, int32_t v, bool deposit){ if(validR(r) && v > 0) addMoney(r, v, deposit); }	// [N] int32_t
void rm8GameSubMoney(int r, int32_t v, bool depositFirst){ if(validR(r) && v > 0) subMoney(r, v, depositFirst); }
void rm8GameSetMoney(int r, int32_t cash, int32_t deposit){ if(validR(r)){ g_r[r].cash = cash; g_r[r].deposit = deposit; } }
void rm8GameAddPoints(int r, int32_t v){ if(validR(r)) g_r[r].points = std::max(0, g_r[r].points + v); }
int rm8GameRand(){ return rnd(); }
void rm8GameReseed(unsigned s){ g_seed = s; }
int rm8GamePushText(const std::string &t){ return pushText(t); }
int rm8GamePushTextPicFile(const std::string &t, const std::string &pack){ int i = pushText(t); if(!pack.empty()) g_textPicFile[i] = pack; return i; }
int rm8GamePushTextPic(const std::string &t, int uvl){ int i = pushText(t); if(uvl > 0) g_textPic[i] = uvl; return i; }
const std::string &rm8GameText(int i){ return textOf(i); }
bool rm8GameOver(){ return g_over; }
// ---- card effects (cards.cpp)
int rm8GameNodeEvent(int node){ return g_s && node >= 0 && node < (int)g_s->rnodes.size() ? g_s->rnodes[node].event : 0; }
bool rm8GameTerraPos(int t, float p[3]){ if(!validT(t)) return false; for(int k = 0; k < 3; k++) p[k] = g_t[t].pos[k]; return true; }
// owner -2 / level -1 / build -1 keep the value; the model follows (Land_OnEnter @0x4ef580)
void rm8GameSetTerra(int t, int owner, int level, int build)
{
	if(!validT(t)) return;
	GTerra &g = g_t[t];
	if(level >= 0) g.level = std::min(level, 5);
	if(build >= 0) g.build = build;
	if(g.type == 1 && g.level > 0) g.build = 1;
	if(owner != -2) setOwner(t, owner); else updateModel(t);
}
void rm8GameSetShape(int r, int s){ if(validR(r)) g_r[r].shape = s; }
int rm8GameInvisible(int r){ return validR(r) ? g_r[r].invisible : 0; }
void rm8GameSetInvisible(int r, int n){ if(validR(r)) g_r[r].invisible = n; }
int32_t rm8GameTerraPrice(int t){ return validT(t) ? calcPrice(g_t[t], true) : 0; }	// [N] [V] Terra_CalcPrice(1) @0x4efc80
bool rm8GameStreetClosed(int st){ auto f = g_stFx.find(st); return f != g_stFx.end() && f->second.closeDays > 0; }
bool rm8GameTerraModel(int t, rw::Clump *&c, int &art){ if(!validT(t) || !g_t[t].model) return false; c = g_t[t].model; art = g_t[t].modelArt; return true; }	// the plot's building / sign article (boardfx.cpp plays its actions)
int rm8GameTerraBuild(int t){ return validT(t) ? g_t[t].build : 0; }	// building type (+0x3C high word): 1 house, 2 institute, 3 chain store, 4 hotel
// [V] the auction's texts (CUi_vendue__vf1__Update @0x4e4020 state 1): a commercial plot is "str14" (empty) or its Land_GetName (the building's name), a residential one
// is announced by its street name (str 4302 / 4310)
std::string rm8GameTerraTitle(int t)
{
	if(!validT(t)) return std::string();
	const GTerra &g = g_t[t];
	if(g.type == 2) return g.level == 0 ? rm8Str(14) : buildingName(g.build);
	return streetName(g.street);
}
// [V] the model the auction shows (Terra_GetBuildingModelId: the plot's building art for its level; a plot without a building shows art 0x27c = BK\Bk000 [export_event_arts.py]);
// false when the plot is empty (the caller uses art 0x27c) or the art is not in the scene
bool rm8GameTerraModelDef(int t, std::string &dff, std::string &texdir, std::string &anim)
{
	if(!validT(t) || !g_s) return false;
	const GTerra &g = g_t[t];
	if(g.level <= 0) return false;
	auto it = g_s->buildingArt.find({g.type == 1 ? 1 : g.build, g.level});
	if(it == g_s->buildingArt.end()) return false;
	auto d = g_s->arts.find(it->second);
	if(d == g_s->arts.end()) return false;
	int a = g.type == 1 ? 2 : 0;	// [V] CreateGameObject 0x50: animation 2 for a residential plot, else 0
	auto an = d->second.anims.find(a);
	dff = d->second.dff; texdir = d->second.texdir; anim = an == d->second.anims.end() ? std::string("-") : an->second;
	return true;
}
// [V] CreateGameObject 0x50 / 0x51 @0x4b1bc0: the auction's base price = Terra_CalcPrice(1) / 2 (the starting price is half of that)
int32_t rm8GameAuctionBase(int t){ return validT(t) ? calcPrice(g_t[t], true) / 2 : 0; }	// [N] int32_t
// [V] Terra_AdjustPriceByPercent @0x4eed70 (news "property speculators" +10 / "haunted house" -10): +0x48 = +0x48 * p / 100 + +0x48 (C int division)
void rm8GameTerraAdjustPct(int t, int p){ if(!validT(t)) return; int &c = g_t[t].pct; c = c * p / 100 + c; rm8Log("rules: plot %d price percent %d (%+d%%)", t, c, p); }
int32_t rm8GameRentPaid(int r){ return validR(r) ? g_r[r].rentOut : 0; }	// [N] [V] +0x33C: Role_TransferRentToOwner @0x487f20 / Role_TransferRentRoadblock @0x488100 add the rent paid (the balance screen's "loss")
int rm8GameBadDays(int r){ return validR(r) ? g_r[r].badDays : 0; }	// [V] +0x340: Role_RunTurnStartStep @0x4842f0 step 9 counts the turn starts in status 1 / 3 / 4 / 5 and the sleepwalk of the 糊塗神 (the balance screen's "unlucky days")
void rm8GameNoteBadDay(int r){ if(validR(r)) g_r[r].badDays++; }
void rm8GameStreetClose(int st, int days){ if(st > 0) g_stFx[st].closeDays = days; }	// [V] the street index must be > 0 (commercial plots cannot be sealed)
// [V] the sign texture of Terra_LoadCloseIcon / Terra_SetHeight / Terra_UpdateHeightAll (boardfx.cpp draws it): sealed -> land_close; else price days -> land_up (multiplier > 1) / land_down.
// [H] Terra_UpdateHeightAll puts land_up back when a seal ends during a price change whatever the multiplier; only Street.Flow(200) exists (CardPy_29), so up and down never mix
int rm8GameStreetSign(int st)
{
	auto f = g_stFx.find(st);
	if(f == g_stFx.end() || st <= 0) return 0;
	if(f->second.closeDays > 0) return 1;
	if(f->second.flowDays > 0) return f->second.mult > 1.0 ? 2 : 3;
	return 0;
}
void rm8GameStreetFlow(int st, int pct)	// [V] Terra_SetHeight @0x42d4c0: price = ftol(c / oldc * price), multiplier = c, 9 days
{
	if(!g_s || st <= 0 || pct <= 0) return;
	StreetFx &f = g_stFx[st];
	double c = pct / 100.0;
	int &p = g_s->streetPrice[st];
	p = (int)(c / f.mult * p); f.mult = c; f.flowDays = 9;
}

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_game.inc"
