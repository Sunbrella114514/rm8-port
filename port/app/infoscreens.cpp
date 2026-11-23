// In-game information screens: win condition (task 0x67), player info (0x66), minimap (0x65), setting (0x69) and the info box of a tap on the board.
// Decompile: docs/infoscreens.md. [V] = read in the decompile (address), [H] = hypothesis, [A] = port adaptation.
#include "infoscreens.h"
#include "ui.h"
#include "cards.h"
#include "stock.h"
#include "events.h"
#include "event_int.h"
#include "menu.h"
#include "story.h"
#include "collect.h"
#include "npc.h"
#include "net.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <map>
#if __has_include(<SDL.h>)
#include <SDL.h>
#endif
#include <string>
#include <vector>

using namespace rw;

namespace {

enum Kind { K_NONE, K_WIN, K_PLAYER, K_MAP, K_SETTING };

Rm8Scene *g_scene = nullptr;
Kind g_kind = K_NONE;
int g_seat = -1;		// the role whose turn it is (the task's role)
bool g_stateSet = false;	// the open call set state 9 (the close puts it back to 8)
Rm8Ui *g_win = nullptr, *g_pl = nullptr, *g_mm = nullptr, *g_st = nullptr, *g_box = nullptr;
bool g_force = false;		// [A] test hook RM8_INFO_FORCE: any seat, any state (all-computer simulations)

bool force(){ static const bool f = getenv("RM8_INFO_FORCE") != nullptr; return f; }
bool oldNetInfo(){ static const bool f = getenv("RM8_NET_TEST_OLDINFO") != nullptr; return f; }	// [N] test hook: the old netplay behaviour of the local screens (state 9 / focus on this device only) to reproduce the desync

// ---- common open / close [V] Game_DispatchCardAction @0x4a4420: task created (Game_UseCard @0x4a4160), CPlayerTag_SetStatusCode9, Role_ClearMsgQueues, Role_EnqueueMsg(100)
void taskBegin(int seat)
{
	g_seat = seat; g_stateSet = false;
	if(force() || seat < 0) return;
	// [N] netplay: these screens are local (the other devices know nothing about them), so they must not touch the turn: the state 9 / cleared queue / 100 here (and the 8 at the
	// close) ran only on the device that opened the screen - its role re-ran Role_BeforeRollDice, a dice click of the remote player met state 9 there -> the hashes differed.
	// The game simply goes on under the screen (the original pauses its single-player game here)
	if(rm8NetActive() && !oldNetInfo()) return;
	if(rm8TurnState(seat) == 6){ rm8TurnSetState(seat, 9); rm8TurnClearQueue(seat); rm8TurnEnqueue(seat, 100, 0, 0); g_stateSet = true; }
}

Rm8Ui *dlg(Rm8Ui *&u, const char *name){ if(!u) u = rm8UiLoad(name, RM8UI_DIALOG); return u; }

void hideAll()
{
	for(Rm8Ui *u : {g_win, g_pl, g_mm, g_st}) if(u) rm8UiShow(u, false);
	rm8UiSetTooltip("");
}

void taskEnd()
{
	hideAll();
	// [V] Game_EndTurn @0x4a48d0: state 9 -> 8; the queued 100 then returns to the dice
	if(g_stateSet && g_seat >= 0 && rm8TurnState(g_seat) == 9) rm8TurnSetState(g_seat, 8);
	g_kind = K_NONE; g_stateSet = false;
}

// pointer helpers
bool ptr(float &x, float &y){ rm8UiPointerPos(x, y); return true; }

// ---- sliders: a knob picture over the slider widget (the knob is BtnNormalPic of the slider, not a widget of the .ui), dragged through the pointer position
struct Slider {
	Rm8Ui *u = nullptr; int id = 0, knob = 0;
	bool vertical = false; float kl = 64;	// knob length (MidSize, design units)
	int lo = 0, hi = 1, val = 0; bool enabled = true;
	int stepBtnA = 0, stepBtnB = 0;
};
void sliderPlace(Slider &s)
{
	float x, y, w, h;
	if(!s.u || !rm8UiGetRect(s.u, s.id, x, y, w, h)) return;
	rm8UiSetVisible(s.u, s.knob, s.enabled);
	float t = s.hi > s.lo ? (float)(s.val - s.lo) / (float)(s.hi - s.lo) : 0.0f;
	if(s.vertical) rm8UiSetRect(s.u, s.knob, x, y + (h - s.kl) * t, w, s.kl);
	else rm8UiSetRect(s.u, s.knob, x + (w - s.kl) * t, y - 1, s.kl, h + 2);
}
int sliderFromPointer(const Slider &s, float px, float py)
{
	float ax, ay, aw, ah, dx, dy, dw, dh;
	if(!rm8UiGetScreenRect(s.u, s.id, ax, ay, aw, ah) || !rm8UiGetRect(s.u, s.id, dx, dy, dw, dh)) return s.val;
	float sc = s.vertical ? ah / std::max(1.0f, dh) : aw / std::max(1.0f, dw);
	float kl = s.kl * sc, t;
	if(s.vertical) t = (py - ay - kl / 2) / std::max(1.0f, ah - kl); else t = (px - ax - kl / 2) / std::max(1.0f, aw - kl);
	t = std::max(0.0f, std::min(1.0f, t));
	return s.lo + (int)lroundf(t * (float)(s.hi - s.lo));
}

// ============================================================ wincondition (0x67)
// [V] Cls_5e4500__vf16_ShowWinConditionInfo @0x4acab0: free game: "<<str#7053>：n | <str#7056>>\<str#6207>：…\<str#7051>：…", story: "<role name>\<str#7051>：…\<str#7052>：n\<str#7058>：n"
std::string winText()
{
	std::string t;
	int seat = 0;
	char b[256];
	if(rm8StoryActive()){	// setup +0 != 0 (0x4acb13..0x4acd58)
		t = rm8TurnRoleCount() > 0 ? rm8TurnName(0) : "";	// CPlayerTag__GetNameReference of the first role
		snprintf(b, sizeof b, rm8Str(7051).c_str(), rm8MenuMapNameByDir(rm8GameMapId()).c_str()); t += "\n"; t += b;	// str 7051
		snprintf(b, sizeof b, rm8Str(7052).c_str(), rm8StorySetupLevel() + 1); t += "\n"; t += b;				// str 7052 (setup + 8, plus one)
		snprintf(b, sizeof b, rm8Str(7058).c_str(), rm8PuzzleCount(rm8StoryHero())); t += "\n"; t += b;			// str 7058 (Event_CountOwnedItems)
	}else{
		Rm8GameOptions o = rm8GameGetOptions();
		if(o.timeLimit == 2) t = rm8Str(7056);				// str 7056
		else { snprintf(b, sizeof b, rm8Str(7053).c_str(), rm8GameDaysLeft()); t = b; }	// str 7053, Game_GetRandomValue @0x4a3e00 = Date_CalcDaysDiff (days to the end date)
		snprintf(b, sizeof b, rm8Str(7054).c_str(), rm8Str(7041 + std::max(0, std::min(5, o.winCond))).c_str()); t += "\n"; t += b;	// str 7054 + 7041.. (setup + 0x1C)
		snprintf(b, sizeof b, rm8Str(7051).c_str(), rm8MenuMapNameByDir(rm8GameMapId()).c_str()); t += "\n"; t += b;
	}
	(void)seat;
	return t;
}

int g_winCatch = 0;
bool openWin()
{
	if(!dlg(g_win, "wincondition")) return false;
	if(!g_winCatch){
		g_winCatch = rm8UiAddWidget(g_win, "button", 0, -400, -440, 1024, 768, -1, 0, 16, 0);	// [A] a tap anywhere closes (the original: right click / Esc, vf11 @0x4acfa0)
		rm8UiOnClick(g_win, [](int){ rm8InfoClose(); });
		rm8UiSetIgnore(g_win, 1, true);	// the text label is disabled in the .ui (taps on it would do nothing): the taps go to the box
	}
	rm8UiSetText(g_win, 1, winText());
	rm8UiShow(g_win, true);
	return true;
}

// ============================================================ playerinfo (0x66)
// [V] Cls_5e4548__vf16_InitPlayerUI @0x4aa9e0: the roles that are not bankrupt fill the columns 1..4 in seat order, then column 1 is selected (UI_UpdatePlayerStats(0))
struct PlayerInfo {
	int seat[4] = {-1, -1, -1, -1}; int n = 0, sel = 0;
	int stockOff = 0, landOff = 0;
	Slider ss, sl;		// stock list scroll bar (ID 3), land list scroll bar (ID 2)
	bool built = false;
} g_P;

// [V] Role_CountOwnTerras @0x4833c0.. (0x48c430): group 0 = residential plots (+0x3C low word 1), 1 = commercial plots (> 1), 2 / 3 / 4 = institute / chain store / hotel (high word), level in [lv, lv]
int countOwn(int seat, int grp, int lv)
{
	int n = 0;
	for(int t = 0; t < rm8GameTerraCount(); t++){
		if(rm8GameTerraOwner(t) != seat || rm8GameTerraLevel(t) != lv) continue;
		int ty = rm8GameTerraType(t), b = rm8GameTerraBuild(t);
		if((grp == 0 && ty == 1) || (grp == 1 && ty > 1) || (grp >= 2 && grp <= 4 && ty > 1 && b == grp)) n++;
	}
	return n;
}

// the land rows [V] UI_DisplayStocks_Page2 @0x4ab6f0: groups 0..4 x levels 0..5 in order, skipping (group 1, level >= 1) and (group >= 2, level 0), the kinds with a count > 0
std::vector<std::string> landRows(int seat)
{
	std::vector<std::string> rows;
	char b[96], c[96];
	for(int k = 0; k < 30; k++){
		int g = k / 6, lv = k % 6;
		if((g == 1 && lv >= 1) || (g >= 2 && lv == 0) || g >= 5) continue;
		int n = countOwn(seat, g, lv);
		if(n <= 0) continue;
		if(g == 0){
			if(lv == 0) snprintf(c, sizeof c, "%s   %4d", rm8Str(25).c_str(), n);	// str 25
			else { snprintf(b, sizeof b, rm8Str(26).c_str(), lv); snprintf(c, sizeof c, "%s    %4d", b, n); }	// str 26 "%d級住宅"
		}else if(g == 1) snprintf(c, sizeof c, "%s   %4d", rm8Str(14).c_str(), n);	// str 14
		else if(g == 2){ snprintf(b, sizeof b, rm8Str(16).c_str(), lv); snprintf(c, sizeof c, "%s  %4d", b, n); }	// str 16 (%d + text)
		else if(g == 3){ snprintf(b, sizeof b, rm8Str(15).c_str(), lv); snprintf(c, sizeof c, "%s%4d", b, n); }	// str 15 (%d + text)
		else { snprintf(b, sizeof b, rm8Str(17).c_str(), lv); snprintf(c, sizeof c, "%s    %4d", b, n); }	// str 17 "%d級飯店"
		rows.push_back(c);
	}
	if(const char *pd = getenv("RM8_INFO_PADROWS")) for(int i = 0; i < atoi(pd); i++){ snprintf(c, sizeof c, "test land %d %4d", i + 1, i + 1); rows.push_back(c); }	// test hook: long lists (the scroll bars)
	return rows;
}

std::vector<std::string> stockRows(int seat)
{
	std::vector<std::string> rows;
	for(int k = 0; k < rm8StockHoldCount(seat); k++){
		int q = rm8StockHoldQty(seat, k);
		std::string nm = rm8StockHoldName(seat, k);
		if(nm.empty() || q <= 0) continue;
		char c[160]; snprintf(c, sizeof c, "%s %5d", nm.c_str(), q);	// "%s %5d" @0x4ab480
		rows.push_back(c);
	}
	if(const char *pd = getenv("RM8_INFO_PADROWS")) for(int i = 0; i < atoi(pd); i++){ char c[160]; snprintf(c, sizeof c, "test stock %d %5d", i + 1, 100 * (i + 1)); rows.push_back(c); }
	return rows;
}

void piStockPage(bool initScroll)
{
	std::vector<std::string> rows = g_P.n > g_P.sel ? stockRows(g_P.seat[g_P.sel]) : std::vector<std::string>();
	int off = std::max(0, std::min(g_P.stockOff, std::max(0, (int)rows.size() - 8)));
	g_P.stockOff = off;
	for(int i = 0; i < 8; i++) rm8UiSetText(g_pl, 50 + i, off + i < (int)rows.size() ? rows[off + i] : "");	// labels 0x32..0x39
	if(initScroll){	// [V] slider 3: disabled with <= 8 rows, else range 0 .. count - 8
		g_P.ss.enabled = (int)rows.size() > 8; g_P.ss.hi = std::max(0, (int)rows.size() - 8); g_P.ss.val = off;
		rm8UiSetEnabled(g_pl, 3, g_P.ss.enabled);
	}
	g_P.ss.val = off; sliderPlace(g_P.ss);
}
void piLandPage(bool initScroll)
{
	std::vector<std::string> rows = g_P.n > g_P.sel ? landRows(g_P.seat[g_P.sel]) : std::vector<std::string>();
	int off = std::max(0, std::min(g_P.landOff, std::max(0, (int)rows.size() - 8)));
	g_P.landOff = off;
	for(int i = 0; i < 8; i++) rm8UiSetText(g_pl, 60 + i, off + i < (int)rows.size() ? rows[off + i] : "");	// labels 0x3c..0x43
	if(initScroll){
		// [A] the original counts the kinds only up to the 8th shown row (the loop breaks at 8), so its land scroll bar (ID 2) can never be enabled and a ninth kind is
		// unreachable; the port counts them all
		g_P.sl.enabled = (int)rows.size() > 8; g_P.sl.hi = std::max(0, (int)rows.size() - 8); g_P.sl.val = off;
		rm8UiSetEnabled(g_pl, 2, g_P.sl.enabled);
	}
	g_P.sl.val = off; sliderPlace(g_P.sl);
}

// [V] UI_FillPlayerInfoDisplay @0x4aace0: column n (ID n*10): portrait npc\<pic>_12, labels n*10 + 1..7 = net assets (Role_CalculateNetCash(1)), cash +0x290, deposit +0x294,
// stock value (Stock_CalculateTotalValue(-1)), +0x324 points, plots (Role_SumOwnTerrasPrice(1)), loan +0x298
void piFillColumn(int n, int seat)	// n = 1..4
{
	int b = n * 10;
	if(seat < 0){
		rm8UiSetEnabled(g_pl, b, false); rm8UiSetPicFile(g_pl, b, "");
		for(int k = 1; k <= 7; k++) rm8UiSetText(g_pl, b + k, "");
		return;
	}
	rm8UiSetEnabled(g_pl, b, true);
	rm8UiSetPicFile(g_pl, b, std::string("prt\\npc\\") + rm8TurnPic(seat) + "_12.tga");
	int32_t cash = rm8GameCash(seat), dep = rm8GameDeposit(seat), loan = rm8EventLoan(seat), stk = rm8StockValue(seat), pts = rm8GamePoints(seat), terra = 0;	// [N] int32_t
	for(int t = 0; t < rm8GameTerraCount(); t++) if(rm8GameTerraOwner(t) == seat) terra += rm8GameTerraPrice(t);
	int32_t net = rm8Add32(rm8Add32(rm8Sub32(rm8Add32(dep, cash), loan), stk), terra);	// [N] display only, the same 32-bit wrap as the rules; [V] Role_CalculateNetCash @0x490140
	int32_t v[8] = {0, net, cash, dep, stk, pts, terra, loan};
	for(int k = 1; k <= 7; k++) rm8UiSetText(g_pl, b + k, std::to_string(v[k]));
}

// [V] UI_RefreshPlayerStockDisplay @0x4ab270: cards, both lists from the top and the scroll bars, the highlight plane (ID col + 3) of the selected column only
void piSelect(int col)
{
	if(col < 0 || col >= 4 || col >= g_P.n) return;
	g_P.sel = col; g_P.stockOff = g_P.landOff = 0;
	int seat = g_P.seat[col];
	for(int i = 0; i < 8; i++) rm8UiSetPicFile(g_pl, 70 + i, "");
	int c = 0;
	for(int s = 0; s < rm8CardMax(); s++){	// [V] UI_DisplayPlayerCards @0x4ab300: the cards in slot order, packed to the front, picture card\<name>
		int id = rm8CardAt(seat, s);
		if(id > 0 && c < 8) rm8UiSetPicFile(g_pl, 70 + c++, rm8CardTex(id));
	}
	piStockPage(true); piLandPage(true);
	for(int n = 1; n <= 4; n++) rm8UiSetVisible(g_pl, n + 3, n == col + 1);
}

bool openPlayer()
{
	if(!dlg(g_pl, "playerinfo")) return false;
	if(!g_P.built){
		g_P.built = true;
		auto mk = [&](Slider &s, int id){
			s.u = g_pl; s.id = id; s.vertical = true; s.kl = 64; s.lo = 0; s.hi = 1; s.val = 0;
			s.knob = rm8UiAddWidget(g_pl, "plane", 0, 0, 0, 32, 64, 588, 0, 16, 3);	// BtnNormalPic 588
			rm8UiSetIgnore(g_pl, s.knob, true);
		};
		mk(g_P.ss, 3); mk(g_P.sl, 2);
		// [A] the arrow pictures at both ends of a scroll bar step the list on a tap (the original: the bar's own buttons / the wheel)
		struct A { Slider *s; float x, y; int dir; } as[4] = {{&g_P.ss, 248, 423, -1}, {&g_P.ss, 248, 644, 1}, {&g_P.sl, 454, 423, -1}, {&g_P.sl, 454, 644, 1}};
		for(A &a : as){ int id = rm8UiAddWidget(g_pl, "button", 0, a.x, a.y, 32, 64, -1, 0, 16, 4); (a.dir < 0 ? a.s->stepBtnA : a.s->stepBtnB) = id; }
		rm8UiOnClick(g_pl, [](int id){
			if(id == 1){ rm8InfoClose(); return; }	// [V] button 1 -> u_Game_OnEvent_CallStateVf2c -> scene vf11 -> the task's vf11 close @0x4aab40
			if(id >= 10 && id <= 40 && id % 10 == 0){ piSelect(id / 10 - 1); return; }	// [V] vf12 @0x4aab80: UI_UpdatePlayerStats(id / 10 - 1)
			Slider *sls[2] = {&g_P.ss, &g_P.sl};
			for(Slider *s : sls){
				bool isStock = s == &g_P.ss;
				int *off = isStock ? &g_P.stockOff : &g_P.landOff;
				if(id == s->id && s->enabled){ float x, y; rm8UiPointerPos(x, y); *off = sliderFromPointer(*s, x, y); }
				else if(id == s->stepBtnA && s->enabled) *off = std::max(0, *off - 1);
				else if(id == s->stepBtnB && s->enabled) *off = std::min(s->hi, *off + 1);
				else continue;
				if(isStock) piStockPage(false); else piLandPage(false);
				return;
			}
		});
	}
	g_P.n = 0;
	for(int s = 0; s < rm8TurnRoleCount() && g_P.n < 4; s++) if(rm8TurnStatus(s) != 7) g_P.seat[g_P.n++] = s;	// [V] +0x1B4 != 7
	for(int n = g_P.n; n < 4; n++) g_P.seat[n] = -1;
	for(int n = 1; n <= 4; n++) piFillColumn(n, g_P.seat[n - 1]);
	g_P.sel = -1;
	piSelect(0);
	rm8UiShow(g_pl, true);
	return true;
}

// ============================================================ minimap (0x65)
// [V] Cls_5e4590__vf16_SetupScreenshot @0x4298c0: a parallel-projection camera 1000 above the centre of the map box looking straight down (frame turned 180 deg about Y, then 90 deg about its
// right axis), near 1, far 2000, view window = max(extent x, extent z) / 2 where extent = box size + 200; a 512 x 512 raster rendered once (Game_OnShutdown @0x4a1c00 = map, articles, roles, dice)
// shown in widget 1 (callback 0x13 @0x429eb0 draws a quad); button 2 closes; hover = names (Minimap_HandleHover @0x42a080); press / drag = camera focus (Minimap_HandleClick @0x429fb0)
struct MiniMap { Camera *cam = nullptr; Texture *tex = nullptr; bool dirty = false; float cx = 0, cz = 0, half = 1; bool built = false; } g_M;

// [A] event.bin is not exported, but its names of events 1 (card shop), 18 and 19 (the two minigames) are the words inside string.bin 5301 / 5314 / 5315
// ("%s在<name><str#5301>", "%s玩<name>小遊戲，獲得%d點券", Traditional or Simplified): they are cut out of those texts instead of being compiled in
const char *runtimeEventName(int ev)
{
	static std::map<int, std::string> cache;
	auto it = cache.find(ev);
	if(it != cache.end()) return it->second.c_str();
	int id = ev == 1 ? 5301 : ev == 18 ? 5314 : 5315;
	const std::string &s = rm8Str(id);
	std::string name;
	if(!s.empty()){
		const char *open = ev == 1 ? u8"在" : u8"玩";
		const char *close[2] = {ev == 1 ? u8"購" : u8"小遊", ev == 1 ? u8"购" : u8"小游"};
		size_t a = s.find(open);
		if(a != std::string::npos){
			a += strlen(open);
			size_t b = std::min(s.find(close[0], a), s.find(close[1], a));
			if(b != std::string::npos && b > a) name = s.substr(a, b - a);
		}
	}
	if(name.empty()) return nullptr;	// not cached: the strings may not be loaded yet
	return (cache[ev] = name).c_str();
}
const char *eventName(int ev)	// [V] the event names of event.bin (Event_FindIndex -> DAT_0063aed4 + 0x3c4)
{
	switch(ev){
	case 1: case 18: case 19: return runtimeEventName(ev);	// the names of 5 or more characters come from string.bin at run time (below)
	case 2: return u8"問答點"; case 3: return u8"卡片點"; case 4: return u8"公園"; case 5: return u8"銀行"; case 6: return u8"樂透";
	case 7: return u8"監獄"; case 8: return u8"醫院"; case 9: return u8"遊樂場"; case 10: return u8"50點"; case 11: return u8"30點"; case 12: return u8"10點";
	case 20: return u8"打地鼠"; case 21: return u8"插海盜"; case 22: return u8"分裂球"; case 23: return u8"踩蛋";
	}
	return nullptr;
}
std::string streetNameOfTerra(int t)
{
	if(!g_scene || t < 0) return "";
	auto it = g_scene->streetName.find(rm8GameTerraStreet(t));
	return it == g_scene->streetName.end() ? std::string() : it->second;
}
// the text of the tooltip at a world point (x, z): [V] role (Game_RoleUnderpoint: sphere 30 round a role with status < 3), road point (sphere 25: its event name, else its street),
// plot (its street)
std::string hoverText(float wx, float wz)
{
	if(!g_scene) return "";
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		float p[3];
		if(rm8TurnStatus(s) < 3 && rm8RolesGetPos(s, p) && hypotf(p[0] - wx, p[2] - wz) < 30.0f) return rm8TurnName(s);
	}
	for(const Rm8RoadNode &n : g_scene->rnodes){
		if(hypotf(n.x - wx, n.z - wz) >= 25.0f) continue;
		if(const char *e = eventName(n.event)) return e;
		std::string st = streetNameOfTerra(n.terra);
		if(!st.empty()) return st;
	}
	int best = -1; float bd = 40.0f;
	for(int t = 0; t < rm8GameTerraCount(); t++){
		float p[3]; if(!rm8GameTerraPos(t, p)) continue;
		float d = hypotf(p[0] - wx, p[2] - wz);
		if(d < bd){ bd = d; best = t; }
	}
	return best >= 0 ? streetNameOfTerra(best) : std::string();
}
// screen pixel -> world (x, z) of the minimap picture; false when the pointer is outside the 512 x 512 picture
bool mmToWorld(float px, float py, float &wx, float &wz)
{
	float ax, ay, aw, ah;
	if(!g_mm || !rm8UiGetScreenRect(g_mm, 1, ax, ay, aw, ah) || px < ax || py < ay || px >= ax + aw || py >= ay + ah) return false;
	float u = (px - ax) / aw, v = (py - ay) / ah;
	// the frame's right axis is (-1, 0, 0), up (0, 0, -1): screen x grows with world x, screen y with world z (librw cameras: screen x = 0.5 - dot(p, right) / 2vw)
	wx = g_M.cx + (u - 0.5f) * 2.0f * g_M.half;
	wz = g_M.cz + (v - 0.5f) * 2.0f * g_M.half;
	return true;
}
bool g_viewJump = false; float g_viewJumpX = 0, g_viewJumpZ = 0;	// [N] a minimap jump of the local view (netplay)
void mmFocus(float px, float py)
{
	float wx, wz;
	if(!mmToWorld(px, py, wx, wz)) return;
	float p[3] = {wx, 0, wz};
	// [A] the ground height at the point is not read (QuadTree_RayCastTile_Wrapper): the board camera only uses x and z
	// [N] netplay: the turn focus is simulation state (the rules' camera follows it: the card range of the computer, the same-node placement), so a local minimap click only moves
	// this device's view (rm8_app.cpp takes the jump as a pan offset); it used to move the focus on the clicking device only
	if(rm8NetActive() && !oldNetInfo()){ g_viewJump = true; g_viewJumpX = wx; g_viewJumpZ = wz; return; }
	rm8TurnFocus(p);	// Camera_SetFocusPoint
}

bool openMap()
{
	if(!g_scene || !g_scene->world || !dlg(g_mm, "minimap")) return false;
	if(!g_M.built){
		g_M.built = true;
		rm8UiOnClick(g_mm, [](int id){
			if(id == 2){ rm8InfoClose(); return; }	// [V] button 2 -> u_Game_OnEvent_CallStateVf2c -> vf11 @0x429f90
			if(id == 1){ float x, y; rm8UiPointerPos(x, y); mmFocus(x, y); }	// a press without a drag still focuses ([V] Minimap_HandleClick on the press)
		});
	}
	// the box [V] g_TerraMgr + 0x48: RwBBoxAddPoint of every road tile (Land_CreateObject @0x4f1320: the tile's position +0x14), then TerraMgr_CreateAll @0x4f07d0 widens it by 50 on
	// each side of x and z; the camera centre is the middle of the box, the view window half the larger extent + 200 (0x5e07bc), as ints (ftol)
	float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
	for(const Rm8RoadNode &n : g_scene->rnodes){ x0 = std::min(x0, n.x); x1 = std::max(x1, n.x); z0 = std::min(z0, n.z); z1 = std::max(z1, n.z); }
	if(x0 > x1){ x0 = z0 = -g_scene->MW * 25.0f; x1 = z1 = g_scene->MW * 25.0f; }
	x0 -= 50; x1 += 50; z0 -= 50; z1 += 50;
	int ix = (int)(x1 - x0 + 200.0f), iz = (int)(z1 - z0 + 200.0f);
	g_M.half = (float)std::max(ix, iz) / 2.0f;
	g_M.cx = (x0 + x1) * 0.5f; g_M.cz = (z0 + z1) * 0.5f;
	g_M.dirty = true;
	rm8UiShow(g_mm, true);
	return true;
}

// ============================================================ setting (0x69)
// [V] Cls_5e4470__vf16_InitSettingUI @0x4ac840: slider 1 music (init.txt 9, 0..5), 2 sound (8, 0..5), 3 speed (6 - 2, 1..5); Config_ApplySetting @0x50a710 applies each change at
// once; button 5 -> vf12 @0x4aca30 -> CUi_setting__vf12_reload @0x4ac980: init.txt is saved, the dialog closes. Hovering a slider shows str 7301 / 7302 / 7303 (vf1 @0x4ac8f0)
struct Setting { Slider s[3]; bool built = false; } g_S;
const int kSetLo[3] = {0, 0, 1}, kSetHi[3] = {5, 5, 5};

void stRefresh(){ for(int i = 0; i < 3; i++){ g_S.s[i].val = rm8MenuOptionGet(i + 1); sliderPlace(g_S.s[i]); } }
void stSet(int i, int v)
{
	v = std::max(kSetLo[i], std::min(kSetHi[i], v));
	if(v == g_S.s[i].val) return;
	rm8MenuOptionSet(i + 1, v);	// Config_ApplySetting
	g_S.s[i].val = v; sliderPlace(g_S.s[i]);
}
bool openSetting()
{
	if(!dlg(g_st, "setting")) return false;
	if(!g_S.built){
		g_S.built = true;
		for(int i = 0; i < 3; i++){
			Slider &s = g_S.s[i];
			s.u = g_st; s.id = i + 1; s.vertical = false; s.kl = 41; s.lo = kSetLo[i]; s.hi = kSetHi[i];
			s.knob = rm8UiAddWidget(g_st, "plane", 0, 0, 0, 41, 17, 1039, 0, 16, 3);	// BtnNormalPic 1039, MidSize 41
			rm8UiSetIgnore(g_st, s.knob, true);
		}
		rm8UiOnClick(g_st, [](int id){
			if(id == 5){ rm8InfoClose(); return; }
			if(id >= 1 && id <= 3){ float x, y; rm8UiPointerPos(x, y); stSet(id - 1, sliderFromPointer(g_S.s[id - 1], x, y)); }
		});
	}
	stRefresh();
	rm8UiShow(g_st, true);
	return true;
}

// ============================================================ the info box of a tap on the board
// [V] Game_OnTileClickWithCard @0x4a4a10 (state 6): the nearest of role (1), NPC (0x20), plot (8), wonder site (0x100), article (0x80) along the pixel ray; none -> road point (2)
float g_boxT = 0; std::string g_boxText;
Rm8Ui *g_intro = nullptr;	// intro.ui: the introduction box of a landmark article (UI_ShowIntroduction @0x4a4840)
// ginfo.ui is ONE instance shared with the turn's info box (hud.cpp rm8HudInfo, rm8UiLoad caches by name): this screen only owns it while g_boxMine,
// otherwise its timeout / state-6 check hid every rent / park / points box of the turn the moment it opened (after the first board tap)
bool g_boxMine = false;
bool boxShown(){ return (g_box && g_boxMine && rm8UiVisible(g_box)) || (g_intro && rm8UiVisible(g_intro)); }
void boxHide(){ if(g_box && g_boxMine) rm8UiShow(g_box, false); g_boxMine = false; if(g_intro) rm8UiShow(g_intro, false); }
void boxShow(const std::string &text, bool intro = false)
{
	if(intro){	// [V] UI_ShowIntroduction @0x4a4840: CUi_intro vf2(text, 1); the label of intro.ui is 341 x 94 at 16 pt: the text is wrapped by hand (the CText of the original wraps itself)
		if(!g_intro) g_intro = rm8UiLoad("intro", RM8UI_DIALOG);
		if(!g_intro) return;
		g_boxText = text;
		rm8UiSetText(g_intro, 1, rm8UiWrapText(text, 335, 16));
		rm8UiShow(g_intro, true);
		g_boxT = 0;
		return;
	}
	if(!g_box) g_box = rm8UiLoad("ginfo", RM8UI_DIALOG);
	if(!g_box) return;
	g_boxText = text;
	rm8UiSetText(g_box, 1, text);	// [V] UI_ShowPlayerInfo @0x4a4800 -> CUi_ginfo vf2 (text, 1)
	rm8UiShow(g_box, true); g_boxMine = true;
	g_boxT = 0;
}

// ---- map articles (scenes/introduce.txt, scripts/export_introduce.py): name and introduction by the model key
struct ArtInfo { std::string name, intro; };
std::map<std::string, ArtInfo> g_art;
std::string lower(std::string v){ for(char &c : v) c = (char)tolower((unsigned char)c); return v; }

struct Ray { V3d o, d; };
bool pixelRay(Camera *cam, float sx, float sy, Ray &r)	// the inverse of the projection (boardfx.cpp / cards.cpp): screen x = (0.5 - xc / (2 vw.x zc)) W
{
	if(!cam || !cam->frameBuffer) return false;
	const Matrix *m = cam->getFrame()->getLTM();
	float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height;
	float kx = (0.5f - sx / W) * 2.0f * cam->viewWindow.x, ky = (0.5f - sy / H) * 2.0f * cam->viewWindow.y;
	V3d d = {m->at.x + m->right.x * kx + m->up.x * ky, m->at.y + m->right.y * kx + m->up.y * ky, m->at.z + m->right.z * kx + m->up.z * ky};
	float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z); if(l < 1e-6f) return false;
	r.o = m->pos; r.d = {d.x / l, d.y / l, d.z / l};
	return true;
}
// distance along the ray to the closest approach of a sphere centre when the ray passes within radius; < 0 = miss
float raySphere(const Ray &r, const float c[3], float rad)
{
	V3d v = {c[0] - r.o.x, c[1] - r.o.y, c[2] - r.o.z};
	float t = v.x * r.d.x + v.y * r.d.y + v.z * r.d.z;
	if(t < 0) return -1;
	V3d q = {v.x - r.d.x * t, v.y - r.d.y * t, v.z - r.d.z * t};
	return sqrtf(q.x * q.x + q.y * q.y + q.z * q.z) < rad ? t : -1;
}

// the distance along the ray to the first triangle of the clump (bind pose, double sided) that the ray crosses, < 0 = none. [V] CArticleMgr vf28 PickByRay @0x43e0e0: the world
// sphere (x 1.5) first, then Geom_RayIntersect on the geometry
float rayClump(const Ray &r, Clump *c)
{
	float best = -1;
	FORLIST(lnk, c->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		Geometry *g = a->geometry;
		if(!g || !g->numMorphTargets || !g->triangles) continue;
		const Matrix *m = a->getFrame()->getLTM();
		const Sphere &bs = g->morphTargets[0].boundingSphere;
		V3d wc; V3d::transformPoints(&wc, &bs.center, 1, m);
		float sc = std::max(length(m->right), std::max(length(m->up), length(m->at)));
		float cc[3] = {wc.x, wc.y, wc.z};
		if(raySphere(r, cc, bs.radius * sc * 1.5f) < 0) continue;
		std::vector<V3d> w(g->numVertices);
		V3d::transformPoints(w.data(), g->morphTargets[0].vertices, g->numVertices, m);
		for(int t = 0; t < g->numTriangles; t++){
			const V3d &p0 = w[g->triangles[t].v[0]], &p1 = w[g->triangles[t].v[1]], &p2 = w[g->triangles[t].v[2]];
			V3d e1 = sub(p1, p0), e2 = sub(p2, p0), pv = cross(r.d, e2);
			float det = dot(e1, pv);
			if(fabsf(det) < 1e-9f) continue;
			float inv = 1.0f / det;
			V3d tv = sub(r.o, p0);
			float u = dot(tv, pv) * inv; if(u < 0 || u > 1) continue;
			V3d qv = cross(tv, e1);
			float v = dot(r.d, qv) * inv; if(v < 0 || u + v > 1) continue;
			float tt = dot(e2, qv) * inv;
			if(tt > 0 && (best < 0 || tt < best)) best = tt;
		}
	}
	return best;
}

std::string plotText(int t)
{
	// [V] @0x4a4a10 case 3 (strings 11..19): level 0: no owner "%s^^空地", residential "%s^^<str#12>", else "<str#14>"; built: "%s^^%d級住宅" or the commercial kind by the high word
	int lv = rm8GameTerraLevel(t), ty = rm8GameTerraType(t), b = rm8GameTerraBuild(t), ow = rm8GameTerraOwner(t);
	std::string st = streetNameOfTerra(t);
	char buf[256], c2[128];
	if(lv == 0){
		if(ow < 0) snprintf(buf, sizeof buf, rm8Str(11).c_str(), st.c_str());
		else if(ty == 1) snprintf(buf, sizeof buf, rm8Str(12).c_str(), st.c_str());
		else snprintf(buf, sizeof buf, "%s", rm8Str(14).c_str());
	}else if(ty == 1) snprintf(buf, sizeof buf, rm8Str(13).c_str(), st.c_str(), lv);
	else snprintf(buf, sizeof buf, rm8Str(b == 2 ? 16 : b == 3 ? 15 : 17).c_str(), lv);
	std::string text = buf;
	if(ow >= 0 && ow < rm8TurnRoleCount()){
		snprintf(c2, sizeof c2, rm8Str(18).c_str(), rm8TurnName(ow)); text += c2;	// str 18 "\地主：%s"
		int32_t rent = rm8GameTerraRent(t, false);	// [N] int32_t
		if(ty == 1 && rent > 0){
			if(ow == rm8EventWonderOwner(1)) rent = rent * 20 / 100 + rent;	// the land wonder's owner earns 20 % more (Wonder_CheckConstruction(1))
			snprintf(c2, sizeof c2, rm8Str(19).c_str(), (int)rent); text += c2;	// str 19 "\租金：%d元"
		}
	}
	return text;
}

}	// namespace

// ---------------------------------------------------------------- API
void rm8InfoSetScene(Rm8Scene *s){ g_scene = s; }
bool rm8InfoVisible(){ return g_kind != K_NONE; }
void rm8InfoReset(){ hideAll(); boxHide(); g_kind = K_NONE; g_stateSet = false; g_seat = -1; }	// a new board: the dialogs of the old one are gone
bool rm8InfoFullScreen(){ return g_kind == K_PLAYER; }

bool rm8InfoOpenFromHud(int button, int seat)
{
	if(g_kind != K_NONE) return false;
	if(!force() && (seat < 0 || !rm8TurnHuman(seat) || rm8TurnState(seat) != 6)) return false;	// [V] Game_DispatchCardAction @0x4a4420: state 6 of the current role
	boxHide();
	if(rm8MenuGameSystemVisible()) rm8MenuGameSystemOpen(false);	// [A] the system menu does not hold state 9 in the port: it must not stay under another screen
	bool ok = false; Kind k = K_NONE;
	switch(button){
	case RM8INFO_WIN: ok = openWin(); k = K_WIN; break;
	case RM8INFO_PLAYER: ok = openPlayer(); k = K_PLAYER; break;
	case RM8INFO_MAP: ok = openMap(); k = K_MAP; break;
	}
	if(!ok) return false;
	g_kind = k;
	taskBegin(seat);
	return true;
}

bool rm8InfoSettingOpen()
{
	if(g_kind != K_NONE) return false;
	if(!openSetting()) return false;
	g_kind = K_SETTING;
	taskBegin(rm8TurnCurrent());	// [V] Game_DispatchCardAction(0x69, 1): param 1 = no state 6 test; the port's system menu does not hold state 9 itself
	return true;
}

bool rm8InfoClose()
{
	if(g_kind == K_NONE) return false;
	if(g_kind == K_SETTING) rm8MenuOptionsSave();	// [V] CUi_setting__vf12_reload @0x4ac980: InitConfig_SaveInitTxt
	taskEnd();
	return true;
}

void rm8InfoBoxYield(){ g_boxMine = false; }
bool rm8InfoTakeViewJump(float &x, float &z){ if(!g_viewJump) return false; g_viewJump = false; x = g_viewJumpX; z = g_viewJumpZ; return true; }
void rm8InfoAuto(Camera *cam);
void rm8InfoTick(Camera *cam)
{
	rm8InfoAuto(cam);
	if(boxShown()){
		g_boxT += 1.0f / 60.0f;	// [A] hidden after a few seconds / at the next tap / when the turn leaves state 6 (the original: on the button release)
		int cur = rm8TurnCurrent();
		if(g_boxT > 6.0f || (cur >= 0 && rm8TurnState(cur) != 6 && !force())) boxHide();
	}
	{	// [V] gmaindlg buttons 6..10 (CUi_gmaindlg__vf10_Init @0x4e9550): the mouse-over callback 5 = CText_SetContentFromStringTable @0x4e8ee0, string 7355 + id (7361 <str#7361>, 7362 股票,
		// 7363 <str#7363>, 7364 小地圖, 7365 <str#7365>), the mouse-out callback 6 clears it; on touch the long press (the UI library's inspection)
		static bool mine = false;
		int hot = 0;
		if(Rm8Ui *hm = rm8HudMain()) if(g_kind == K_NONE) for(int id = 6; id <= 10; id++) if(rm8UiHot(hm, id)) hot = id;
		if(hot){ rm8UiSetTooltip(rm8Str(7355 + hot)); mine = true; }
		else if(mine){ rm8UiSetTooltip(""); mine = false; }
	}
	if(g_kind == K_NONE) return;
	float px, py; rm8UiPointerPos(px, py);
	if(g_kind == K_PLAYER){
		Slider *sls[2] = {&g_P.ss, &g_P.sl};
		for(Slider *s : sls) if(s->enabled && rm8UiPressed(g_pl, s->id)){
			bool isStock = s == &g_P.ss; int *off = isStock ? &g_P.stockOff : &g_P.landOff;
			int v = sliderFromPointer(*s, px, py);
			if(v != *off){ *off = v; if(isStock) piStockPage(false); else piLandPage(false); }
		}
	}else if(g_kind == K_SETTING){
		for(int i = 0; i < 3; i++) if(rm8UiPressed(g_st, g_S.s[i].id)) stSet(i, sliderFromPointer(g_S.s[i], px, py));
		std::string tip;	// [V] vf1 @0x4ac8f0: hovering slider n shows str 7300 + n
		for(int i = 0; i < 3; i++) if(rm8UiHot(g_st, g_S.s[i].id)) tip = rm8Str(7301 + i);
		rm8UiSetTooltip(tip);
	}else if(g_kind == K_MAP){
		if(rm8UiPressed(g_mm, 1)) mmFocus(px, py);	// drag = Minimap_HandleClick on every mouse move while the button is down
		static const bool mark = getenv("RM8_INFO_MARK") != nullptr;	// test hook: a small square at every role's position, computed with the same mapping as the picking
		if(mark){
			static int ids[8] = {0, 0, 0, 0, 0, 0, 0, 0};
			float bx, by, bw, bh, dx, dy, dw, dh;
			if(rm8UiGetScreenRect(g_mm, 1, bx, by, bw, bh) && rm8UiGetRect(g_mm, 1, dx, dy, dw, dh))
				for(int k = 0; k < rm8TurnRoleCount() && k < 8; k++){
					float p[3]; if(!rm8RolesGetPos(k, p)) continue;
					if(!ids[k]) ids[k] = rm8UiAddWidget(g_mm, "plane", 0, 0, 0, 8, 8, -1, 0xffff00ff, 16, 9);
					float u = (p[0] - g_M.cx) / (2.0f * g_M.half) + 0.5f, v = (p[2] - g_M.cz) / (2.0f * g_M.half) + 0.5f;
					rm8UiSetRect(g_mm, ids[k], dx + u * dw - 4, dy + v * dh - 4, 8, 8);
				}
		}
		std::string tip; float wx, wz;
		if(rm8UiHot(g_mm, 1) && mmToWorld(px, py, wx, wz)) tip = hoverText(wx, wz);	// Minimap_HandleHover
		rm8UiSetTooltip(tip);
	}
}

void rm8InfoPreRender(Camera *main)
{
	if(g_kind != K_MAP || !g_M.dirty || !g_scene || !main || !main->world) return;
	if(!g_M.cam){
		g_M.cam = Camera::create();
		g_M.cam->setFrame(Frame::create());
		g_M.cam->frameBuffer = Raster::create(512, 512, 32, Raster::CAMERATEXTURE | Raster::C8888);
		g_M.cam->zBuffer = Raster::create(512, 512, 0, Raster::ZBUFFER);
		g_M.tex = Texture::create(g_M.cam->frameBuffer);
		g_M.tex->setFilter(Texture::LINEAR);
		g_M.tex->setAddressU(Texture::CLAMP); g_M.tex->setAddressV(Texture::CLAMP);
		g_M.cam->setProjection(Camera::PARALLEL);
		g_M.cam->setNearPlane(1.0f); g_M.cam->setFarPlane(2000.0f);	// [V] 0x3f800000 / 0x44fa0000
	}
	V2d vw = {g_M.half, g_M.half};
	g_M.cam->setViewWindow(&vw);
	main->world->addCamera(g_M.cam);	// [A] added for this render only (a camera left in a world that is destroyed with the board would dangle)
	Matrix m; m.setIdentity();
	m.right = {-1, 0, 0}; m.up = {0, 0, -1}; m.at = {0, -1, 0}; m.pos = {g_M.cx, 1000.0f, g_M.cz}; m.flags = 0;	// [V] 180 deg about Y, then 90 deg about the right axis; y = 0x447a0000
	g_M.cam->getFrame()->transform(&m, COMBINEREPLACE);
	rm8UiSetTextureN(g_mm, "Plane6", g_M.tex);
	RGBA none = {0, 0, 0, 0};
	g_M.cam->clear(&none, Camera::CLEARIMAGE | Camera::CLEARZ);
	g_M.cam->beginUpdate();
	rm8RenderScene(*g_scene);
	rm8RolesRender();
	g_M.cam->endUpdate();
	main->world->removeCamera(g_M.cam);
	g_M.dirty = false;
}

bool rm8InfoBoardTap(Camera *cam, float sx, float sy)
{
	if(g_kind != K_NONE || !g_scene) return false;
	if(boxShown()){ boxHide(); }	// the next tap hides the box ([A] touch has no button release / right click to do it)
	int cur = rm8TurnCurrent();
	if(!force() && (cur < 0 || !rm8TurnHuman(cur) || rm8TurnState(cur) != 6)) return false;	// [V] Game_HandleCardClickInGame @0x4a2940: state 6 only
	Ray ray;
	if(!pixelRay(cam, sx, sy, ray)){ if(getenv("RM8_INFO_AUTO")) rm8Log("info: no pixel ray (frame buffer %p)", cam ? (void*)cam->frameBuffer : nullptr); return false; }
	float bestT = 1e30f; int kind = 0, idx = -1;
	// 1 roles: Game_FindClosestRole @0x498170, ray vs the role's sphere (radius 30, Game_RoleUnderpoint @0x4982d0); the roles that are visible (status < 3)
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		float p[3];
		if(rm8TurnStatus(s) >= 3 || !rm8RolesGetPos(s, p)) continue;
		p[1] += 30.0f;	// [H] the sphere is about the body, not the feet
		float t = raySphere(ray, p, 30.0f);
		if(t >= 0 && t < bestT){ bestT = t; kind = 1; idx = s; }
	}
	// 0x20 NPCs on the board (NPC_FindNearest @0x49f000)
	for(int h = 1; h <= rm8NpcCount(); h++){
		if(!rm8NpcOnMap(h)) continue;
		int nd = rm8NpcNode(h);
		if(nd < 0 || nd >= (int)g_scene->rnodes.size()) continue;
		const Rm8RoadNode &n = g_scene->rnodes[nd];
		float p[3] = {n.x, n.y + 25.0f, n.z};
		float t = raySphere(ray, p, 30.0f);
		if(t >= 0 && t < bestT){ bestT = t; kind = 2; idx = h; }
	}
	// 8 plots (TerraMgr_FindClosestTerra @0x4f23b0): the pad the ray crosses (ground plane of the plot, [H] radius 35)
	if(fabsf(ray.d.y) > 1e-4f) for(int t = 0; t < rm8GameTerraCount(); t++){
		float p[3]; if(!rm8GameTerraPos(t, p)) continue;
		float dt = (p[1] - ray.o.y) / ray.d.y;
		if(dt < 0 || dt >= bestT) continue;
		if(hypotf(ray.o.x + ray.d.x * dt - p[0], ray.o.z + ray.d.z * dt - p[2]) < 35.0f){ bestT = dt; kind = 3; idx = t; }
	}
	// 0x100 wonder sites (TerraMgr_FindClosestStock @0x4f2b80) and 2 road points (StPoint_TestLineSphereIntersect: radius 25)
	int nodeHit = -1; float nodeT = 1e30f;
	for(int i = 0; i < (int)g_scene->rnodes.size(); i++){
		const Rm8RoadNode &n = g_scene->rnodes[i];
		float p[3] = {n.x, n.y, n.z};
		float t = raySphere(ray, p, 25.0f);
		if(t >= 0 && t < nodeT){ nodeT = t; nodeHit = i; }
	}
	if(nodeHit >= 0 && ev::wonderAtNode(nodeHit) >= 0 && nodeT < bestT){ bestT = nodeT; kind = 4; idx = nodeHit; }
	// 0x80 map articles ([V] CArticleMgr vf+0x70 PickByRay): the nearest one the ray crosses that has an introduction or a name (houses of the plots and the event icons
	// have none in scenes/introduce.txt: the plot / road point answer for them)
	std::string artKey; bool artIntro = false;
	if(!g_art.empty()) for(size_t i = 0; i < g_scene->clumps.size() && i < g_scene->clumpNames.size(); i++){
		auto it = g_art.find(lower(g_scene->clumpNames[i]));
		if(it == g_art.end()) continue;
		float t = rayClump(ray, g_scene->clumps[i]);
		if(t > 0 && t < bestT){ bestT = t; kind = 5; artKey = it->first; artIntro = !it->second.intro.empty(); }
	}
	std::string text; bool asIntro = false;
	switch(kind){
	case 5: {	// [V] @0x4a4a10 case 5: vf32 (the introduction) -> UI_ShowIntroduction, else vf31 (the name) -> UI_ShowPlayerInfo
		const ArtInfo &ai = g_art[artKey];
		if(artIntro){ text = ai.intro; asIntro = true; } else text = ai.name;
		break; }
	case 1: text = rm8TurnName(idx); break;	// case 1: CPlayerTag__GetNameReference
	case 2: text = rm8NpcName(idx); break;	// case 2: the NPC's name (+0x64)
	case 3: text = plotText(idx); break;
	case 4: {	// case 4 @0x4a4a10: "<wonder name>\n\n<str#212>：%d%%" while it is being built (percent < 100)
		int w = ev::wonderAtNode(idx); bool act; int own, st, pc; float sh[8];
		if(ev::wonderGet(w, act, own, sh, st, pc) && act){
			char b[96]; snprintf(b, sizeof b, rm8Str(212).c_str(), pc);
			text = rm8Str(201 + w) + "\n\n" + b;
		}
		break; }
	default:
		if(nodeHit >= 0){	// the default branch: Game_FindBuildingByType(ray, 2): the event name, else the street
			const Rm8RoadNode &n = g_scene->rnodes[nodeHit];
			if(const char *e = eventName(n.event)) text = e; else text = streetNameOfTerra(n.terra);
		}
		break;
	}
	if(text.empty()) return false;
	boxShow(text, asIntro);
	return true;
}

// scenes/introduce.txt: <article key> TAB <name> TAB <introduction>; in the introduction the first backslash ends the title line (INI_LoadIntroduceTexts @0x4436e0: backslash = line break;
// the title is padded with spaces in the file, they are dropped here: the label is left aligned)
bool rm8InfoLoadData(const std::string &scenesDir)
{
	g_art.clear();
	FILE *f = fopen((scenesDir + "/introduce.txt").c_str(), "rb");
	if(!f) return false;
	std::string line; int ch;
	auto flush = [&](){
		size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
		if(b != std::string::npos){
			ArtInfo ai; ai.name = line.substr(a + 1, b - a - 1); ai.intro = line.substr(b + 1);
			while(!ai.intro.empty() && ai.intro.back() == '\r') ai.intro.pop_back();
			size_t k = 0; while(k < ai.intro.size() && ai.intro[k] == ' ') k++;
			ai.intro = ai.intro.substr(k);
			g_art[lower(line.substr(0, a))] = ai;
		}
		line.clear();
	};
	while((ch = fgetc(f)) != EOF){ if(ch == '\n') flush(); else line += (char)ch; }
	if(!line.empty()) flush();
	fclose(f);
	rm8Log("info: %d articles with a name / introduction", (int)g_art.size());
	return !g_art.empty();
}

// ---------------------------------------------------------------- test hooks
// RM8_INFO_AUTO="open:player,tap:20,..." (all steps run on the first tick after the HUD exists; real pointer presses at the centres of the widgets):
//   open:win|player|map|setting      open through rm8InfoOpenFromHud / rm8InfoSettingOpen (RM8_INFO_FORCE=1 skips the seat / state test)
//   hud:6|8|9|10                     a real pointer tap at the centre of gmaindlg button N (the state-6 buttons of the HUD)
//   tap:<id>                         a real tap at the centre of widget <id> of the open screen
//   slider:<id>:<0..1>               a real press + drag + release along the slider widget <id> to that fraction of its track
//   mm:<u>:<v>                       a real tap on the minimap picture at that fraction (and the hover tooltip text is logged)
//   board:<x>:<y>                    a tap on the board at that fraction of the screen (rm8InfoBoardTap, logs the text); plot:<n> / role:<seat> / node:<n> at that object's screen position
//   close                            rm8InfoClose
static Rm8Ui *curUi()
{
	switch(g_kind){ case K_WIN: return g_win; case K_PLAYER: return g_pl; case K_MAP: return g_mm; case K_SETTING: return g_st; default: return nullptr; }
}
static bool tapWidget(Rm8Ui *u, int id, float fx = 0.5f, float fy = 0.5f)
{
	float x, y, w, h;
	if(!u || !rm8UiGetScreenRect(u, id, x, y, w, h)) return false;
	float cx = x + w * fx, cy = y + h * fy;
	if(getenv("RM8_DEBUG_HUD")) rm8Log("info auto: tap (%.0f, %.0f) hits %s", cx, cy, rm8UiHitName(cx, cy).c_str());
	rm8UiPointerMove(cx, cy); rm8UiPointerDown(cx, cy, true); rm8UiPointerUp(cx, cy);
	return true;
}
void rm8InfoAuto(Camera *cam)
{
	static const char *env = getenv("RM8_INFO_AUTO");
	static int frames = 0, wait = 0; static size_t pos = 0; static std::vector<std::string> toks;
	static const int delay = getenv("RM8_INFO_DELAY") ? atoi(getenv("RM8_INFO_DELAY")) : 0;	// frames after the HUD exists (windowed runs through the menus)
	if(!env || !rm8HudMain() || frames++ < delay) return;
	if(toks.empty()){ std::stringstream ss(env); std::string t; while(std::getline(ss, t, ',')) toks.push_back(t); }
	if(wait > 0){ wait--; return; }
	while(pos < toks.size() && wait == 0){	// one tick runs every token up to the next wait:<frames>
		std::string tok = toks[pos++];
		std::vector<std::string> a; { std::stringstream t2(tok); std::string p; while(std::getline(t2, p, ':')) a.push_back(p); }
		if(a.empty()) continue;
		rm8Log("info auto: %s", tok.c_str());
		if(a[0] == "wait" && a.size() > 1) wait = std::max(1, atoi(a[1].c_str()));
		else if(a[0] == "open" && a.size() > 1){
			int cur = rm8TurnCurrent();
			bool ok = a[1] == "setting" ? rm8InfoSettingOpen() : rm8InfoOpenFromHud(a[1] == "win" ? RM8INFO_WIN : a[1] == "player" ? RM8INFO_PLAYER : RM8INFO_MAP, cur);
			rm8Log("info auto: open %s -> %d (seat %d state %d)", a[1].c_str(), ok, cur, cur >= 0 ? rm8TurnState(cur) : -1);
		}else if(a[0] == "hud" && a.size() > 1){
			bool ok = tapWidget(rm8HudMain(), atoi(a[1].c_str()));
			rm8Log("info auto: hud button %s -> %d, screen %d, state %d", a[1].c_str(), ok, (int)g_kind, rm8TurnCurrent() >= 0 ? rm8TurnState(rm8TurnCurrent()) : -1);
		}else if(a[0] == "tap" && a.size() > 1){
			rm8Log("info auto: tap %s -> %d", a[1].c_str(), tapWidget(curUi(), atoi(a[1].c_str())));
		}else if(a[0] == "tapui" && a.size() > 2){	// a real tap on widget <id> of an already loaded UI (gsys, ginfo ...)
			rm8Log("info auto: tapui %s %s -> %d, screen %d", a[1].c_str(), a[2].c_str(), tapWidget(rm8UiLoad(a[1], RM8UI_DIALOG), atoi(a[2].c_str())), (int)g_kind);
		}else if(a[0] == "hover" && a.size() > 1){	// the mouse over a button of gmaindlg (the tooltip appears 0.5 s later)
			float x, y, w, h;
			if(rm8UiGetScreenRect(rm8HudMain(), atoi(a[1].c_str()), x, y, w, h)) rm8UiPointerMove(x + w / 2, y + h / 2);
		}else if(a[0] == "hit" && a.size() > 2 && cam && cam->frameBuffer){ rm8Log("info auto: ui at (%s, %s): '%s'", a[1].c_str(), a[2].c_str(), rm8UiHitName((float)atof(a[1].c_str()) * cam->frameBuffer->width, (float)atof(a[2].c_str()) * cam->frameBuffer->height).c_str());
		}else if(a[0] == "box"){ rm8Log("info auto: box %s '%s'", boxShown() ? "shown" : "hidden", boxShown() ? g_boxText.c_str() : "");
		}else if(a[0] == "state"){
			int cur = rm8TurnCurrent(); rm8Log("info auto: seat %d state %d, screen %d", cur, cur >= 0 ? rm8TurnState(cur) : -1, (int)g_kind);
		}else if(a[0] == "slider" && a.size() > 2){
			Rm8Ui *u = curUi(); int id = atoi(a[1].c_str()); float f = (float)atof(a[2].c_str());
			float x, y, w, h;
			if(u && rm8UiGetScreenRect(u, id, x, y, w, h)){
				bool vert = h > w;
				float x0 = vert ? x + w / 2 : x + w * 0.5f, y0 = vert ? y + h * 0.5f : y + h / 2, x1 = vert ? x0 : x + w * (0.1f + 0.8f * f), y1 = vert ? y + h * (0.1f + 0.8f * f) : y0;
				rm8UiPointerMove(x0, y0); rm8UiPointerDown(x0, y0, true); rm8InfoTick(cam);
				rm8UiPointerMove(x1, y1); rm8InfoTick(cam);
				rm8UiPointerUp(x1, y1);
			}
		}else if(a[0] == "mm" && a.size() > 2){
			float fx = (float)atof(a[1].c_str()), fy = (float)atof(a[2].c_str());
			bool ok = tapWidget(g_mm, 1, fx, fy);
			float px, py, wx = 0, wz = 0; rm8UiPointerPos(px, py);
			bool in = mmToWorld(px, py, wx, wz);
			rm8Log("info auto: minimap tap %d at world (%.1f, %.1f) %d, hover '%s'", ok, wx, wz, in, in ? hoverText(wx, wz).c_str() : "");
			float f[3]; int ser = rm8RolesFocus(f); rm8Log("info auto: board focus (%.1f, %.1f, %.1f) serial %d", f[0], f[1], f[2], ser);
		}else if(a[0] == "board" && a.size() > 2 && cam && cam->frameBuffer){
			float sx = (float)atof(a[1].c_str()) * cam->frameBuffer->width, sy = (float)atof(a[2].c_str()) * cam->frameBuffer->height;
			bool ok = rm8InfoBoardTap(cam, sx, sy);
			rm8Log("info auto: board tap (%.0f, %.0f) -> %d '%s'", sx, sy, ok, ok ? g_boxText.c_str() : "");
#if __has_include(<SDL.h>)
		}else if((a[0] == "fdown" || a[0] == "fup") && a.size() > 2){	// a real SDL finger event at that fraction of the window (windowed runs: the long press / tap path of rm8_app.cpp)
			SDL_Event e; memset(&e, 0, sizeof e);
			e.type = a[0] == "fdown" ? SDL_FINGERDOWN : SDL_FINGERUP; e.tfinger.fingerId = 91; e.tfinger.x = (float)atof(a[1].c_str()); e.tfinger.y = (float)atof(a[2].c_str());
			static float lx = 0.5f, ly = 0.5f;	// fdown:role:<seat> / fdown:art:<text> aim at that object's screen position; fup:0:0 releases where the last fdown was
			if(a[0] == "fdown" && cam && cam->frameBuffer && (a[1] == "role" || a[1] == "art")){
				float p[3] = {0, 0, 0}; bool have = false;
				if(a[1] == "role"){ have = rm8RolesGetPos(atoi(a[2].c_str()), p); p[1] += 30; }
				else if(g_scene) for(size_t i = 0; i < g_scene->clumps.size() && !have; i++) if(lower(g_scene->clumpNames[i]).find(lower(a[2])) != std::string::npos){
					FORLIST(lnk, g_scene->clumps[i]->atomics){ Atomic *at = Atomic::fromClump(lnk); const Sphere &bs = at->geometry->morphTargets[0].boundingSphere; V3d wc; V3d::transformPoints(&wc, &bs.center, 1, at->getFrame()->getLTM()); p[0] = wc.x; p[1] = wc.y; p[2] = wc.z; have = true; break; }
				}
				const Matrix *m = cam->getFrame()->getLTM();
				V3d d = {p[0] - m->pos.x, p[1] - m->pos.y, p[2] - m->pos.z};
				float xc = d.x * m->right.x + d.y * m->right.y + d.z * m->right.z, yc = d.x * m->up.x + d.y * m->up.y + d.z * m->up.z, zc = d.x * m->at.x + d.y * m->at.y + d.z * m->at.z;
				if(have && zc > 1.0f){ lx = (0.5f - xc / (2.0f * cam->viewWindow.x * zc)); ly = (0.5f - yc / (2.0f * cam->viewWindow.y * zc)); }
				rm8Log("info auto: finger down on %s %s at (%.3f, %.3f)", a[1].c_str(), a[2].c_str(), lx, ly);
				e.tfinger.x = lx; e.tfinger.y = ly;
			}else if(a[0] == "fdown"){ lx = e.tfinger.x; ly = e.tfinger.y; }
			else { e.tfinger.x = lx; e.tfinger.y = ly; }
			SDL_PushEvent(&e);
#endif
		}else if(a[0] == "art" && a.size() > 1 && cam && cam->frameBuffer && g_scene){	// tap on the screen position of the first article whose model name contains <text> (or the n-th with art:<text>:<n>)
			int want = a.size() > 2 ? atoi(a[2].c_str()) : 0, seen = 0; bool done = false;
			for(size_t i = 0; i < g_scene->clumps.size() && !done; i++){
				if(lower(g_scene->clumpNames[i]).find(lower(a[1])) == std::string::npos || seen++ < want) continue;
				Atomic *at = nullptr; FORLIST(lnk, g_scene->clumps[i]->atomics){ at = Atomic::fromClump(lnk); break; }
				if(!at || !at->geometry) continue;
				const Sphere &bs = at->geometry->morphTargets[0].boundingSphere;
				V3d wc; V3d::transformPoints(&wc, &bs.center, 1, at->getFrame()->getLTM());
				const Matrix *m = cam->getFrame()->getLTM();
				V3d d = {wc.x - m->pos.x, wc.y - m->pos.y, wc.z - m->pos.z};
				float xc = d.x * m->right.x + d.y * m->right.y + d.z * m->right.z, yc = d.x * m->up.x + d.y * m->up.y + d.z * m->up.z, zc = d.x * m->at.x + d.y * m->at.y + d.z * m->at.z;
				float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height;
				float sx = (0.5f - xc / (2.0f * cam->viewWindow.x * zc)) * W, sy = (0.5f - yc / (2.0f * cam->viewWindow.y * zc)) * H;
				bool ok = rm8InfoBoardTap(cam, sx, sy);
				rm8Log("info auto: article %s at (%.0f, %.0f) -> %d '%s'", g_scene->clumpNames[i].c_str(), sx, sy, ok, ok ? g_boxText.c_str() : "");
				done = true;
			}
			if(!done) rm8Log("info auto: no article '%s'", a[1].c_str());
		}else if((a[0] == "plot" || a[0] == "role" || a[0] == "node") && a.size() > 1 && cam && cam->frameBuffer){	// tap on the screen position of plot / role seat / road node N
			float p[3] = {0, 0, 0}; int k = atoi(a[1].c_str()); bool have = false;
			if(a[0] == "plot") have = rm8GameTerraPos(k, p);
			else if(a[0] == "role"){ have = rm8RolesGetPos(k, p); p[1] += 30; }
			else if(g_scene && k >= 0 && k < (int)g_scene->rnodes.size()){ p[0] = g_scene->rnodes[k].x; p[1] = g_scene->rnodes[k].y; p[2] = g_scene->rnodes[k].z; have = true; }
			const Matrix *m = cam->getFrame()->getLTM();
			V3d d = {p[0] - m->pos.x, p[1] - m->pos.y, p[2] - m->pos.z};
			float xc = d.x * m->right.x + d.y * m->right.y + d.z * m->right.z, yc = d.x * m->up.x + d.y * m->up.y + d.z * m->up.z, zc = d.x * m->at.x + d.y * m->at.y + d.z * m->at.z;
			if(have && zc > 1.0f){
				float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height;
				float sx = (0.5f - xc / (2.0f * cam->viewWindow.x * zc)) * W, sy = (0.5f - yc / (2.0f * cam->viewWindow.y * zc)) * H;
				bool ok = rm8InfoBoardTap(cam, sx, sy);
				rm8Log("info auto: %s %d at (%.0f, %.0f) -> %d '%s'", a[0].c_str(), k, sx, sy, ok, ok ? g_boxText.c_str() : "");
			}else rm8Log("info auto: %s %d is off screen / unknown", a[0].c_str(), k);
		}else if(a[0] == "close") rm8Log("info auto: close -> %d", rm8InfoClose());
	}
}
