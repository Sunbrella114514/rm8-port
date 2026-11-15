// Front-end menus, see menu.h and docs/menus.md. [V] = verified in the decompile (address), [H] hypothesis, [A] port adaptation.
#include "menu.h"
#include "ui.h"
#include "audio.h"
#include "zh.h"
#include "menu_extra.h"
#include "menu_anim.h"
#include "present.h"
#include "story.h"
#include "story_host.h"
#include "collect.h"
#include "save.h"
#include "infoscreens.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>

#if __has_include(<SDL.h>)
#include <SDL.h>
#define RM8_MENU_SDL 1
#endif

using namespace rw;

Rm8MenuHooks &rm8MenuHooks(){ static Rm8MenuHooks h; return h; }
bool rm8AutoSupplyOpen();	// roles.cpp (turn.h): gsys button 1

namespace {

enum Scr { S_NONE, S_LOGO, S_MAIN, S_GAME, S_ROLES, S_MAP, S_OPTION, S_LTGAME, S_LOAD, S_EXTRA, S_FINAL };	// S_EXTRA: menu_extra.cpp (movie / picmenu / boxmenu)

struct Stage { int id = 0, clime = 1; std::string dir, name; };
struct RoleInfo { int id = 0, sex = 0, strId = 0; std::string pic, name; };	// strId: roleinfo +0x38c (the role's string.bin block)	// sex: roleinfo +0x304 [H] (chooses the outfit 4 button of the role menu)

Rm8Setup g_setup;
Rm8Options g_opt;
Rm8MenuResult g_result = RM8MENU_RUNNING;
Scr g_scr = S_NONE;
bool g_inited = false, g_active = false;
std::string g_game, g_root, g_settingsFile;
std::vector<Stage> g_stages;
std::vector<std::string> g_downloads;	// [V] Map_LoadMapList @0x4a8ce0: map\download*.map get ids 0x15, 0x16, ... in file order
std::map<int, RoleInfo> g_roles;
float g_time = 0;
int g_sw = 1280, g_sh = 720;
int g_mapInit = 1;		// the map menu's initial state: 1 normal, 0xB after a cleared story stage (Cls_5e81e0 state)
int g_movieRole = 0;	// finalMenu: the ending movie asked for

// ---- [A] settings file (the original rewrites Config\init.txt, CInitConfig__vf18_SaveConfig @0x50ab40; the game folder is read-only here)
std::string readFileKey(const std::string &path, const char *key)
{
	std::ifstream f(path); std::string line;
	while(std::getline(f, line)){
		size_t e = line.find('=');
		if(e != std::string::npos && line.substr(0, e) == key){ std::string v = line.substr(e + 1); while(!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back(); return v; }
	}
	return "";
}
void loadOptions()
{
	// [V] init.txt [param] keys fullscreen, character, speed, difficuty, sound, music (InitConfig_LoadInitTxt @0x401080; defaults sound 4, music 3, docs/audio.md)
	std::string ini = g_game + "/Config/init.txt";
	auto num = [&](const std::string &file, const char *k, int &dst){ std::string v = readFileKey(file, k); if(!v.empty()) dst = atoi(v.c_str()); };
	for(const std::string &f : {ini, g_settingsFile}){
		num(f, "music", g_opt.music); num(f, "sound", g_opt.sound); num(f, "speed", g_opt.speed); num(f, "difficuty", g_opt.difficulty);
		num(f, "character", g_opt.character);
	}
	g_opt.fullscreen = 0; num(g_settingsFile, "fullscreen", g_opt.fullscreen);	// [A] the user's init.txt window mode is not copied
	g_opt.music = std::max(0, std::min(5, g_opt.music)); g_opt.sound = std::max(0, std::min(5, g_opt.sound));
	g_opt.speed = std::max(3, std::min(7, g_opt.speed)); g_opt.difficulty = std::max(1, std::min(3, g_opt.difficulty));
	g_opt.character = std::max(4, std::min(6, g_opt.character));
	g_opt.simplified = rm8ZhEnabled();
	std::string l = readFileKey(g_settingsFile, "simplified");
	if(!l.empty() && !getenv("RM8_LANG")){ g_opt.simplified = atoi(l.c_str()) != 0; rm8ZhEnable(g_opt.simplified); }
}
void saveOptions()
{
	std::ofstream f(g_settingsFile);
	if(!f) return;
	f << "music=" << g_opt.music << "\nsound=" << g_opt.sound << "\nspeed=" << g_opt.speed << "\ndifficuty=" << g_opt.difficulty
	  << "\nfullscreen=" << g_opt.fullscreen << "\ncharacter=" << g_opt.character << "\nsimplified=" << (g_opt.simplified ? 1 : 0) << "\n";
}
void applyOptions()
{
	// difficulty is read by the rules through RM8_DIFFICULTY ([A] events.cpp caches it on first use), speed is rm8GameSpeed (roles.cpp)
	char b[8]; snprintf(b, sizeof b, "%d", g_opt.difficulty);
#ifdef _WIN32
	_putenv_s("RM8_DIFFICULTY", b);
#else
	setenv("RM8_DIFFICULTY", b, 1);
#endif
	rm8GameSpeed = g_opt.speed;
	if(rm8MenuHooks().applyOptions) rm8MenuHooks().applyOptions(g_opt);
}

// ---- helpers
const std::string &S(int id){ return rm8Str(id); }
Rm8Ui *loadUi(const char *name){ return rm8UiLoad(name, RM8UI_DIALOG); }	// [A] centred 4:3 box scaled by height (the HUD mode stretches the backgrounds on wide screens)
std::function<void(int)> g_click;		// click handler of the current screen
std::function<void()> g_back;			// Esc / Android back of the current screen
std::function<void(float)> g_upd;		// per-frame update of the current screen
Rm8Ui *g_ui = nullptr;				// the current screen's UI
int g_roleVoice = 0;				// Cls_5e8140 +0xC: the sample handle of the role's voice
void roleVoiceStop(){ if(g_roleVoice){ rm8SndStop(g_roleVoice); g_roleVoice = 0; } }
// [V] Cls_5e8140_PlayRoleSample @0x506b20: the previous sample stops, the new one is the voice of string (roleinfo strId + 0x1d) (StringTable + 32000 + index * 4, the number after '@')
void rolePlayVoice(int role)
{
	roleVoiceStop();
	auto it = g_roles.find(role);
	Rm8SpeechLine l;
	if(it == g_roles.end() || it->second.strId <= 0 || !rm8SpeechParse(it->second.strId + 0x1d, l) || l.voice <= 0) return;
	g_roleVoice = rm8SndPlay(l.voice, 1);
}
void setScreenUi(Rm8Ui *u, std::function<void(int)> click)
{
	rm8MenuAnimLeave();	// [V] the old menu object resets the flying characters when it closes
	roleVoiceStop();	// [V] Cls_5e8140__vf18_Close @0x505a10: the role's sample stops with the role menu
	rm8StoryHostEnd();	// [V] Cls_5e81e0__vf18_OnHide @0x503170: the host mesh is freed with the map menu
	rm8UiHideAll();
	g_ui = u; g_click = click; g_upd = nullptr; g_back = nullptr;
	if(u){ rm8UiOnClick(u, [](int id){ if(g_click) g_click(id); }); rm8UiShow(u, true); }
	rm8MenuAnimEnter(u);	// menu_anim.cpp: rings, glow, circles, bubbles, flying characters (by the UI's name)
}
std::string mapDirOf(int id)
{
	for(const Stage &s : g_stages) if(s.id == id) return s.dir;
	if(id >= 0x15 && id - 0x15 < (int)g_downloads.size()) return g_downloads[id - 0x15];
	return "";
}
std::string mapNameOf(int id)
{
	for(const Stage &s : g_stages) if(s.id == id) return s.name;
	if(id >= 0x15 && id - 0x15 < (int)g_downloads.size()) return g_downloads[id - 0x15];	// [V] @0x4a9020: file name without ".map"
	return "";
}
int humans(){ int n = 0; for(const Rm8Seat &s : g_setup.seats) if(!s.computer) n++; return n; }

void go(Scr s);
void goExtra(int which);	// menu_extra.cpp screens (Rm8ExtraScreen)
// ---- logo (CLogoScene @0x507690 / 0x507990): texture prt\logo\c_002 fades in, holds, fades out, then the start movie or the menu
struct Logo { Rm8Ui *ui = nullptr; int pic = 0; float t = 0; } g_logo;
const float LOGO_IN = 1.0f, LOGO_HOLD = 1.0f, LOGO_OUT = 1.0f;	// [A] times (the fade rate is a float constant in 0x507990)
void enterLogo()
{
	g_logo.ui = rm8UiCreateCanvas("logo", RM8UI_HUD);
	if(!g_logo.pic){
		g_logo.pic = rm8UiAddWidget(g_logo.ui, "plane", 0, 256, 128, 512, 512);	// [V] a 512 x 512 quad centred on the screen
		rm8UiSetPicFile(g_logo.ui, g_logo.pic, "prt\\logo\\c_002.tga");
	}
	g_logo.t = 0;
	setScreenUi(g_logo.ui, nullptr);
	g_upd = [](float dt){
		g_logo.t += dt;
		float a = g_logo.t < LOGO_IN ? g_logo.t / LOGO_IN : g_logo.t < LOGO_IN + LOGO_HOLD ? 1.0f : 1.0f - (g_logo.t - LOGO_IN - LOGO_HOLD) / LOGO_OUT;
		unsigned al = (unsigned)(std::max(0.0f, std::min(1.0f, a)) * 255);
		rm8UiSetTint(g_logo.ui, g_logo.pic, 0xffffff | (al << 24));
		if(g_logo.t >= LOGO_IN + LOGO_HOLD + LOGO_OUT) goExtra(RM8X_MOVIE_START);	// [V] 0x507b70 App_SwitchToStartMovieOrMenu: the start movie (menu_extra.cpp), the main menu when it cannot play
	};
	g_back = [](){ go(S_MAIN); };
}

// ---- main menu (mainmenu.ui, state 1) and the single / mult menu (state 8, single.ui or mult.ui)
// [V] Cls_5e8190__vf12_OnMessage @0x508b50: 1 -> single (setup+0 = 1), 2 -> mult (setup+0 = 0), 3 -> option menu, 4 -> minigames, 5 -> quit
// [V] Cls_5e8000_vf12 @0x50db90: 1 -> new game (mult: map menu 3, single: role menu 2), 2 -> load (9), 3 -> picmenu (10), 4 -> finalMenu (7), 5 -> back to the main menu
// [V] Ui_CreateMenuItemObjects @0x508730 / Ui_UpdateMenuItemRotation @0x508f60: each hexagon button i has an icon sprite (pair of pictures {normal, hover} from the table at
//     0x5e7b7c (main) / 0x5e87ec (single, mult)) placed at the "push" widget 6 + i; while the pointer is on button i it turns about the Y axis at 720 deg/s
//     (picture swapped at 90 deg, front again at 360 deg), back when the pointer leaves. [A] the turn is drawn as a width scale |cos|
const int MAIN_TAB[5][2] = {{104, 105}, {133, 134}, {107, 108}, {110, 111}, {157, 158}};
const int GAME_TAB[5][2] = {{446, 447}, {455, 456}, {449, 450}, {452, 453}, {1051, 1052}};
struct Items { float ang[5] = {0, 0, 0, 0, 0}; bool back[5] = {false, false, false, false, false}; float x[5], y[5], w[5], h[5]; int ov[5] = {0, 0, 0, 0, 0}; const int (*tab)[2] = nullptr; } g_it;
std::map<Rm8Ui*, std::vector<int>> g_ovMade;	// overlay widgets per UI (created once)
// [A] A touch tap on a hexagon button waits until the icon has finished its turn. The original has no such wait (Cls_5e8190__vf12_OnMessage @0x508b50 switches pages
// at once, no fade, no timer) but its mouse always hovers the button first, so the 0.25 s turn (90 deg, picture swap, 90 deg more at 720 deg/s: Ui_UpdateMenuItemRotation @0x508f60) is normally over before the click. A finger has no
// hover: without the wait the page would change before the turn is visible. The click runs TURN_HOLD s after the icon is back to front. Mouse clicks are not delayed.
struct Pending { int id = 0; float t = 0; } g_pend;
std::function<void(int)> g_itemClick;		// the screen's real click handler (the UI's handler defers through itemsClick)
bool g_viaPointer = false;			// set while a touch release runs (SDL finger events and the tap test token): only those clicks wait
struct PointerGuard { PointerGuard(){ g_viaPointer = true; } ~PointerGuard(){ g_viaPointer = false; } };
const float TURN_HOLD = 0.15f;
void itemsClick(int id)
{
	if(g_pend.id) return;	// a page change is on its way
	if(g_viaPointer && id >= 1 && id <= 5 && g_it.tab && g_it.ang[id - 1] < 360.0f){ g_pend.id = id; g_pend.t = 0; return; }
	if(g_itemClick) g_itemClick(id);
}
void itemsInit(Rm8Ui *u, const int (*tab)[2])
{
	g_it.tab = tab; g_pend = Pending();
	for(int i = 0; i < 5; i++){
		g_it.ang[i] = 0; g_it.back[i] = false;
		rm8UiGetRect(u, 6 + i, g_it.x[i], g_it.y[i], g_it.w[i], g_it.h[i]);
		rm8UiSetVisible(u, 6 + i, false);	// the .ui "push" widgets lie below the translucent hexagons; the sprite is drawn on top (Ui_RenderMainMenuItems @0x5086c0 after the UI)
		auto &mv = g_ovMade[u]; if(mv.size() < 5) mv.resize(5, 0);
		if(!mv[i]) mv[i] = rm8UiAddWidget(u, "plane", 0, g_it.x[i], g_it.y[i], g_it.w[i], g_it.h[i], tab[i][0], 0, 16, 9);
		g_it.ov[i] = mv[i];
		rm8UiSetPic(u, g_it.ov[i], tab[i][0]);
		rm8UiSetRect(u, g_it.ov[i], g_it.x[i], g_it.y[i], g_it.w[i], g_it.h[i]);
		rm8UiSetEnabled(u, g_it.ov[i], false);
		rm8UiSetIgnore(u, g_it.ov[i], true);	// the icon is drawn over hexagon button i + 1: the pointer must reach the button (click, hover turn)
	}
	g_upd = [](float dt){
		for(int i = 0; i < 5; i++){
			float &a = g_it.ang[i]; bool hot = rm8UiHot(g_ui, i + 1) || g_pend.id == i + 1;
			if(hot){
				if(a < 360){
					a += dt * 720;
					if(g_it.back[i]){ if(a >= 360) a = 360; }
					else if(a >= 90){ g_it.back[i] = true; a += 180; rm8UiSetPic(g_ui, g_it.ov[i], g_it.tab[i][1]); }
				}
			}else if(a > 0){
				a -= dt * 720;
				if(!g_it.back[i]){ if(a <= 0) a = 0; }
				else if(a < 270){ g_it.back[i] = false; a -= 180; rm8UiSetPic(g_ui, g_it.ov[i], g_it.tab[i][0]); }
				if(a < 0) a = 0;
			}
			float sc = fabsf(cosf(a * 3.14159265f / 180.0f));
			rm8UiSetRect(g_ui, g_it.ov[i], g_it.x[i] + g_it.w[i] * (1 - sc) / 2, g_it.y[i], std::max(1.0f, g_it.w[i] * sc), g_it.h[i]);
		}
		if(g_pend.id && g_it.ang[g_pend.id - 1] >= 360.0f){
			g_pend.t += dt;
			if(g_pend.t >= TURN_HOLD){ int id = g_pend.id; g_pend.id = 0; if(g_itemClick) g_itemClick(id); }
		}
	};
}
void enterMain()
{
	Rm8Ui *u = loadUi("mainmenu");
	g_itemClick = [](int id){
		switch(id){
		case 1: g_setup.story = true; go(S_GAME); break;
		case 2: g_setup.story = false; go(S_GAME); break;
		case 3: go(S_OPTION); break;
		case 4: go(S_LTGAME); break;
		case 5: g_result = RM8MENU_QUIT; break;
		}
	};
	setScreenUi(u, itemsClick);
	itemsInit(u, MAIN_TAB);
	g_back = [](){ g_result = RM8MENU_QUIT; };
	rm8MusicPlay(101, true);	// [V] CGameScene__vf16_Init @0x509d70: track 101 (docs/audio.md)
}
void enterGame()
{
	Rm8Ui *u = loadUi(g_setup.story ? "single" : "mult");
	g_itemClick = [](int id){
		switch(id){
		case 1:	// [V] Cls_5e8000_vf12 @0x50db90: 1 -> new game: the multi menu opens the map menu (state 3), the single menu the role menu (state 2)
			if(g_setup.story){ g_setup.seats.clear(); g_setup.map = 0; go(S_ROLES); }
				else go(S_MAP);
				break;
		case 2: go(S_LOAD); break;
		case 3: goExtra(RM8X_PICMENU); break;	// [V] Cls_5e8000_vf12 @0x50db90: 3 -> state 10 (puzzle album)
		case 5: go(S_MAIN); break;
		case 4: go(S_FINAL); break;	// [V] 4 -> state 7 (finalMenu, the ending movie viewer)
		}
	};
	setScreenUi(u, itemsClick);
	itemsInit(u, GAME_TAB);
	g_back = [](){ go(S_MAIN); };
}

// SCREENS-BEGIN
// ---- role menu (chsRoleMenu.ui, state 2): 16 roles on the right, up to 4 seats, human / computer per seat
// [V] Cls_5e8140__vf12_OnCommand @0x5065f0: 1..16 add the role (first seat human, later ones computer; max 4, 2 for minigame 101), 17 start, 18 back,
//     19..23 / 33 outfit of the selected seat, 25..28 seat -> human, 29..32 seat -> computer (only while 2 humans exist), 34..37 remove the seat
// [V] Cls_5e8140_LoadAndDisplayRoles @0x505fb0: label 98 role name, label 99 string 6900 + id, portrait uvlist 887 + id in ID 51
// [V] Cls_5e8140__vf10_StartGame @0x506a30: free game needs >= 2 seats and a human; story with 1 seat -> map menu; map >= 101: CApp_SwitchToMiniGame(map - 100)
// [V] Cls_5e8140__vf11 @0x5069b0: back -> map menu (free game) / single menu (story) / ltgameMenu (map >= 101)
// [A] outfits: Event_GetLevel(role) is the story progress (0 on a fresh profile): only outfit 0 can be chosen; unlocked roles: all
struct RoleMenu { int sel = -1; int head[4] = {0, 0, 0, 0}, hint = 0; } g_rm;
const float SEAT_X[4] = {56, 198, 340, 487}, SEAT_Y[4] = {636, 696, 636, 696};	// [V] chsRoleMenu.ui seat controls (IDs 25..28, 34..37)
void rolesRefresh()
{
	Rm8Ui *u = g_ui;
	int n = (int)g_setup.seats.size();
	if(g_rm.sel >= n) g_rm.sel = n - 1;
	std::vector<std::pair<int, int>> seatRoles;
	for(const Rm8Seat &s : g_setup.seats) seatRoles.push_back({s.charId, s.outfit});
	bool dolls = rm8MenuAnimSeats(seatRoles);	// [V] the 3D characters of the seats (menu_anim.cpp); the hexagon portrait below is the stand-in when they cannot load
	for(int k = 0; k < 4; k++){
		bool has = k < n;
		rm8UiSetVisible(u, 34 + k, has);
		rm8UiSetVisible(u, 25 + k, has && g_setup.seats[k].computer);
		rm8UiSetVisible(u, 29 + k, has && !g_setup.seats[k].computer);
		rm8UiSetVisible(u, g_rm.head[k], has);
		if(has) rm8UiSetPic(u, g_rm.head[k], dolls ? -1 : 173 + g_setup.seats[k].charId);	// the hexagon portrait of the role button (uvlist 174..189); the box still takes the click (select the seat)
	}
	for(int r = 1; r <= 16; r++){
		bool picked = false;
		for(const Rm8Seat &s : g_setup.seats) if(s.charId == r) picked = true;
		rm8UiSetVisible(u, 60 + r, picked);	// [A] check mark (IDs 61..76, picture 1095) on the roles already chosen
	}
	int role = g_rm.sel >= 0 ? g_setup.seats[g_rm.sel].charId : 0;
	rm8UiSetPic(u, 51, role ? 887 + role : -1);
	rm8UiSetText(u, 98, role ? g_roles[role].name : "");
	rm8UiSetText(u, 99, role ? rm8UiWrapText(S(6900 + role), 229, 16) : "");
	// [V] Cls_5e8140_UpdateRolePanel @0x505a80: outfits 0..min(stage, 3) (Event_GetLevel) are shown and enabled, the rest of 0..3 shown and disabled; outfit 4 sits on button 0x17
	//     (roleinfo +0x304 == 0) or 0x21 (the other one is hidden) and is enabled with stage >= 3 and all 9 puzzle pieces (Event_CountOwnedItems)
	int stage = role ? rm8ProfileLevel(role) : 0, top = std::min(stage, 3);
	bool alt = role && g_roles[role].sex != 0;
	for(int k = 0; k < 4; k++){ rm8UiSetVisible(u, 19 + k, role > 0); rm8UiSetEnabled(u, 19 + k, role > 0 && k <= top); }
	bool o4 = role && stage >= 3 && rm8PuzzleCount(role) >= 9;
	rm8UiSetVisible(u, 23, role > 0 && !alt); rm8UiSetVisible(u, 33, role > 0 && alt);
	rm8UiSetEnabled(u, 23, o4 && !alt); rm8UiSetEnabled(u, 33, o4 && alt);
}
void rolesAdd(int role)
{
	int maxSeats = g_setup.map == 101 ? 2 : 4;
	if(!rm8ProfileRoleUnlocked(role)) return;	// [V] vf16 @0x505940: a role with Event_GetTerrainData == 0 is disabled
	if((int)g_setup.seats.size() >= maxSeats) return;
	// [V] @0x5065f0: the first outfit k in 0..min(Event_GetLevel(role), 3) that no seat of this role uses; none left = the click does nothing
	for(int k = 0; k <= std::min(rm8ProfileLevel(role), 3); k++){
		bool used = false;
		for(const Rm8Seat &q : g_setup.seats) if(q.charId == role && q.outfit == k) used = true;
		if(used) continue;
		Rm8Seat s; s.charId = role; s.outfit = k; s.computer = humans() > 0;
		g_setup.seats.push_back(s);
		g_rm.sel = (int)g_setup.seats.size() - 1;
		rolesRefresh();
		rolePlayVoice(role);	// [V] @0x5065f0: Cls_5e8140_PlayRoleSample(the new seat), before Game_ClearBinaryData
		if(g_setup.story) rm8StoryNewRole(role);	// [V] Game_ClearBinaryData (box + run level); [A] the original also clears it in the free game, the port keeps its persisted box then
		return;
	}
}
// [V] u_Role_SetItemField4 @0x505e60 (buttons 0x13..0x17, 0x21): the selected seat's outfit
void rolesOutfit(int outfit)
{
	if(g_rm.sel < 0 || g_rm.sel >= (int)g_setup.seats.size()) return;
	Rm8Seat &me = g_setup.seats[g_rm.sel];
	for(int k = 0; k < (int)g_setup.seats.size(); k++) if(k != g_rm.sel && g_setup.seats[k].charId == me.charId && g_setup.seats[k].outfit == outfit) return;	// [A] one seat per (role, outfit)
	me.outfit = outfit;
	rolesRefresh();
}
void rolesStart()
{
	if(humans() < 1) return;
	int n = (int)g_setup.seats.size();
	if(g_setup.map < 101){
		if(g_setup.story){	// [V] 0x506a30: a story run needs exactly one seat, then the map menu (state 3)
			if(n == 1) go(S_MAP);
			else rm8UiSetText(g_ui, 99, u8"闖關模式只能選擇一名角色。（移植版提示）");	// [A] the game ignores the click
		}
		else if(n >= 2) g_result = RM8MENU_START_GAME;	// [V] free game
		else rm8UiSetText(g_ui, 99, u8"至少需要兩名玩家。（移植版提示）");	// [A] the game ignores the click
	}else if(n > 0){
		int game = g_setup.map - 100;
		if(rm8MenuHooks().startMiniGame && rm8MenuHooks().startMiniGame(game, g_setup)){ rm8MusicPlay(101, true); go(S_LTGAME); }
		else rm8UiSetText(g_ui, 99, u8"小遊戲尚未接入。（移植版提示）");
	}
}
void enterRoles()
{
	Rm8Ui *u = loadUi("chsRoleMenu");
	g_ui = u;
	static Rm8Ui *madeFor = nullptr;
	if(madeFor != u){
		madeFor = u;
		for(int k = 0; k < 4; k++) g_rm.head[k] = rm8UiAddWidget(u, "plane", 0, SEAT_X[k] - 7, SEAT_Y[k] - 150, 128, 128, -1, 0, 16, 6);	// [A] seat portrait (the game shows the 3D character)
	}
	setScreenUi(u, [](int id){
		if(id >= 1 && id <= 16) rolesAdd(id);
		else if(id == 17) rolesStart();
		else if(id == 18){ go(g_setup.map >= 101 ? S_LTGAME : g_setup.story ? S_GAME : S_MAP); }	// [V] 0x5069b0: story -> the single menu (state 8)
		else if(id >= 19 && id <= 23) rolesOutfit(id - 19);
		else if(id == 33) rolesOutfit(4);
		else if(id >= 25 && id <= 28){ if(id - 25 < (int)g_setup.seats.size()){ g_setup.seats[id - 25].computer = false; rolesRefresh(); } }
		else if(id >= 29 && id <= 32){ if(id - 29 < (int)g_setup.seats.size() && humans() >= 2){ g_setup.seats[id - 29].computer = true; rolesRefresh(); } }
		else if(id >= 34 && id <= 37){
			int k = id - 34;
			if(k < (int)g_setup.seats.size()){
				if(k == g_rm.sel) roleVoiceStop();	// [V] @0x5065f0: removing the selected seat stops its sample
				g_setup.seats.erase(g_setup.seats.begin() + k);
				if(k <= g_rm.sel) g_rm.sel--;
				if(g_rm.sel < 0 && !g_setup.seats.empty()) g_rm.sel = 0;
				if(!g_setup.seats.empty() && humans() < 1) g_setup.seats[0].computer = false;	// [V] 0x5065f0: the first seat becomes human again
				rolesRefresh();
			}
		}
		else for(int k = 0; k < 4; k++) if(id == g_rm.head[k] && k < (int)g_setup.seats.size()){ g_rm.sel = k; rolesRefresh(); }
	});
	g_back = [](){ g_click(18); };
	g_rm.sel = g_setup.seats.empty() ? -1 : 0;	// [A] the game starts with no selection (vf16 @0x505940 sets -1)
	for(int r = 1; r <= 16; r++) rm8UiSetEnabled(u, r, rm8ProfileRoleUnlocked(r));	// [V] vf16 @0x505940: Event_GetTerrainData(role) == 0 disables the role button
	rolesRefresh();
}
// ---- map / mode menu (chsMapMenu.ui, state 3; free game = setup+0 == 0)
// [V] Game_InitOptions @0x5031c0: defaults cash 100000, tickets 50, vehicle 0, time limit 2, win condition 5; arrow 0x11 (win condition) disabled
// [V] Cls_5e81e0__vf12_OnEvent @0x503d20: IDs 1..11 pick a map, 0xC back, 0xD..0x12 open the option list of that row, 0x47..0x4C choose in it, 0x76 confirm
// [V] CUi_SetMapSelectionText @0x502b70: name (stage.bin section) in label 98, text string 6600 + id in label 99, picture 799.. in ID 51
// [V] Game_SetOption @0x504940 for what each row writes; Game_PopulateMapList @0x503510 enables the unlocked maps (gamedate.bin) [A] all unlocked
const int MAP_PIC[12] = {0, 799, 800, 801, 813, 814, 815, 816, 817, 818, 819, 820};	// [V] DAT_005e76bc
const int ROW_TOP[6] = {84, 153, 221, 288, 357, 426};					// [V] ListBox tops of chsMapMenu.ui (IDs 71..76), left 818, width 149
int g_cand[4] = {0, 0, 0, 0}, g_nCand = 0;	// the story map menu's candidates (Cls_5e81e0 +0x10..+0x1c)
struct MapMenu {
	int openRow = -1, marker = 0, panel = 0;
	int items[6] = {0, 0, 0, 0, 0, 0}, itemRow = -1, itemCount = 0;
} g_mm;
int rowItemCount(int row){ return row == 0 || row == 1 || row == 2 || row == 3 ? 3 : row == 4 ? 6 : (int)g_downloads.size(); }
std::string rowItemText(int row, int i)
{
	static const int base[5] = {7001, 7011, 7021, 7031, 7041};	// [V] string ids (StringTable + 0x6d64 / 0x6d8c / 0x6db4 / 0x6ddc / 0x6e04)
	if(row < 5) return S(base[row] + i);
	return mapNameOf(0x15 + i);
}
void mapShowInfo(int id)
{
	if(id < 1){ rm8UiSetText(g_ui, 98, ""); rm8UiSetText(g_ui, 99, ""); rm8UiSetPic(g_ui, 51, -1); return; }
	if(id < 12){ rm8UiSetText(g_ui, 98, mapNameOf(id)); rm8UiSetText(g_ui, 99, rm8UiWrapText(S(6600 + id), 318, 16)); rm8UiSetPic(g_ui, 51, MAP_PIC[id]); }
	else { rm8UiSetText(g_ui, 98, ""); rm8UiSetText(g_ui, 99, ""); rm8UiSetPic(g_ui, 51, 1115); }	// [V] download maps: picture 0x45b, no text
}
void mapPlaceMarker()
{
	float x, y, w, h;
	if(g_setup.map >= 1 && g_setup.map <= 11 && rm8UiGetRect(g_ui, g_setup.map, x, y, w, h)){ rm8UiSetVisible(g_ui, g_mm.marker, true); rm8UiSetRect(g_ui, g_mm.marker, x, y, w, h); }
	else rm8UiSetVisible(g_ui, g_mm.marker, false);
}
void mapCloseList()
{
	g_mm.openRow = -1;
	rm8UiSetVisible(g_ui, g_mm.panel, false);
	for(int k = 0; k < 6; k++) rm8UiSetVisible(g_ui, g_mm.items[k], false);
}
void mapOpenList(int row)
{
	mapCloseList();
	int n = std::min(rowItemCount(row), 6);
	if(n < 1) return;
	g_mm.openRow = row;
	rm8UiSetRect(g_ui, g_mm.panel, 818, (float)ROW_TOP[row], 149, 24.0f * n + 4);
	rm8UiSetVisible(g_ui, g_mm.panel, true);
	for(int k = 0; k < n; k++){
		rm8UiSetRect(g_ui, g_mm.items[k], 820, ROW_TOP[row] + 2 + 24.0f * k, 145, 24);
		rm8UiSetText(g_ui, g_mm.items[k], rowItemText(row, k));
		rm8UiSetVisible(g_ui, g_mm.items[k], true);
	}
}
void mapSetOption(int row, int v)	// [V] Game_SetOption: row 1..6 as in the game (here 0..5)
{
	mapCloseList();
	switch(row){
	case 0: if(v >= 0 && v < 3){ static const int c[3] = {50000, 100000, 200000}; g_setup.cash = c[v]; rm8UiSetText(g_ui, 61, S(7001 + v)); } break;
	case 1: if(v >= 0 && v < 3){ static const int t[3] = {0, 50, 100}; g_setup.tickets = t[v]; rm8UiSetText(g_ui, 62, S(7011 + v)); } break;
	case 2: if(v >= 0 && v < 3){ g_setup.vehicle = v; rm8UiSetText(g_ui, 63, S(7021 + v)); } break;
	case 3: if(v >= 0 && v < 3){
		g_setup.timeLimit = v; rm8UiSetText(g_ui, 64, S(7031 + v));
		if(v == 2){ g_setup.winCond = 5; rm8UiSetText(g_ui, 65, S(7046)); rm8UiSetEnabled(g_ui, 17, false); }	// [V] unlimited: only bankruptcy ends the game
		else rm8UiSetEnabled(g_ui, 17, true);
	} break;
	case 4: if(v >= 0 && v < 6){ g_setup.winCond = v; rm8UiSetText(g_ui, 65, S(7041 + v)); } break;
	case 5: if(v >= 0 && v < (int)g_downloads.size()){ g_setup.map = 0x15 + v; rm8UiSetText(g_ui, 66, mapNameOf(0x15 + v)); mapShowInfo(g_setup.map); mapPlaceMarker(); } break;
	}
}
void enterMap()
{
	Rm8Ui *u = loadUi("chsMapMenu");
	g_ui = u;
	static Rm8Ui *madeFor = nullptr;
	if(madeFor != u){
		madeFor = u;
		g_mm.marker = rm8UiAddWidget(u, "plane", 0, 0, 0, 50, 64, 293, 0, 16, 6);	// [A] selected map marker (the game disables the picked button)
		g_mm.panel = rm8UiAddWidget(u, "plane", 0, 818, 84, 149, 100, -1, 0xffffffff, 14, 8);	// [A] option list (the game's listbox 71..76: white, hover yellow)
		for(int k = 0; k < 6; k++){
			g_mm.items[k] = rm8UiAddWidget(u, "button", 0, 820, 90, 145, 24, -1, 0x00ffffff, 16, 9);
			rm8UiSetColorN(u, ("dyn" + std::to_string(g_mm.items[k])).c_str(), 0xff000000);
		}
	}
	// [V] vf16 @0x503100: setup+4 = 0, Game_InitOptions
	g_setup.map = 0; g_setup.cash = 100000; g_setup.tickets = 50; g_setup.vehicle = 0; g_setup.timeLimit = 2; g_setup.winCond = 5;
	setScreenUi(u, [](int id){
		if(g_setup.story && rm8StoryHostState() != 1) return;	// [V] Cls_5e81e0__vf12_OnEvent @0x503d20 acts only in state 1: the host is talking
		if(id >= 1 && id <= 11){
			g_setup.map = id; mapShowInfo(id); mapPlaceMarker(); rm8UiSetText(g_ui, 66, ""); mapCloseList();
		}else if(id == 12){ if(g_setup.story) g_back(); else go(S_GAME); }
		else if(id >= 13 && id <= 18){ int row = id - 13; if(g_mm.openRow == row) mapCloseList(); else mapOpenList(row); }
		else if(id == 118){	// [V] 0x76 -> OnConfirm @0x503e90: needs a map; free game -> role menu (state 2); story: opponents, layer, then the host's introduction (state 2 / 5)
			if(g_setup.map >= 1){
				if(!g_setup.story) go(S_ROLES);
				else if(rm8StoryOnConfirm(g_setup)) rm8StoryHostBegin(rm8StorySetupLevel() == 0 ? 2 : 5, g_setup, g_cand, rm8StoryRunLevel());
			}
		}
		else if(id == 19){ goExtra(RM8X_BOXMENU); }	// [V] 0x13 -> state 0xB (boxmenu, story mode only; menu_extra.cpp)
		else for(int row = 0; row < 6; row++) for(int k = 0; k < 6; k++) if(id == g_mm.items[k] && g_mm.openRow == row) mapSetOption(row, k);
	});
	g_back = [](){
		if(g_mm.openRow >= 0){ mapCloseList(); return; }
		if(!g_setup.story){ go(S_GAME); return; }	// [V] vf11 @0x5042a0
		// [V] OnBack @0x5042a0: only in state 1, and not once a stage was cleared in this run (run.level > 0); Cls_5e81e0_ShowGameMenu @0x504310: the carried items go back
		// into the box (u_Obj_ApplyPendingChanges) and the role menu (state 2) opens
		if(rm8StoryHostState() != 1 || !rm8StoryCanGoBack()) return;
		rm8StoryMenuBack(); go(S_ROLES);
	};
	for(int id = 71; id <= 76; id++) rm8UiSetVisible(u, id, false);	// [V] Game_InitOptions hides the six listboxes (IDs 71..76); [A] the port draws its own list
	// [V] Game_InitOptions @0x5031c0: the free game shows the six option rows (widgets 0xd.. arrows, 0x51.. 0x3d.. row pictures / labels) and hides the chest button 0x13;
	//     the story hides all of them and shows 0x13
	for(int k = 0; k < 6; k++){ rm8UiSetVisible(u, 13 + k, !g_setup.story); rm8UiSetVisible(u, 61 + k, !g_setup.story); rm8UiSetVisible(u, 81 + k, !g_setup.story); }
	rm8UiSetVisible(u, 19, g_setup.story);	// [V] Game_InitOptions: the chest button is hidden unless setup+0 != 0 (story mode)
	rm8UiSetEnabled(u, 17, false);
	for(int row = 0; row < 6; row++) mapSetOption(row, row == 0 ? 1 : row == 1 ? 1 : row == 2 ? 0 : row == 3 ? 2 : -1);
	rm8UiSetText(u, 65, S(7046)); rm8UiSetText(u, 66, "");
	mapCloseList();
	mapShowInfo(0); mapPlaceMarker();
	g_mm.openRow = -1;
	int init = g_mapInit; g_mapInit = 1;
	// [V] Game_PopulateMapList @0x503510 free branch: Event_GetEventByType(map) == 0 -> the button is hidden (vf+0x1c), else shown + enabled (the 3rd map, Paris, is only
	//     there when gamedate.bin says so; RM8_UNLOCK_ALL=1 shows it); the story branch below hides all of them first
	for(int id = 1; id <= 11; id++){ bool open = rm8ProfileMapUnlocked(id); rm8UiSetVisible(u, id, open); rm8UiSetEnabled(u, id, open); }
	g_nCand = 0;
	if(g_setup.story){
		// [V] Game_PopulateMapList story branch @0x503510: every map button is hidden, then the candidates of the run's layer (the hero's pass entries with level == min(run.level, 3);
		//     an empty layer is skipped and recorded) are shown and enabled; u_RoleSel_SelectUniqueMatchEntry @0x504cd0 clicks the map when exactly one is left
		for(int id = 1; id <= 11; id++) rm8UiSetVisible(u, id, false);
		int role = g_setup.seats.empty() ? 0 : g_setup.seats[0].charId;
		g_nCand = rm8StoryCandidates(role, g_cand);
		for(int k = 0; k < g_nCand; k++){ rm8UiSetVisible(u, g_cand[k], true); rm8UiSetEnabled(u, g_cand[k], true); }
		if(g_nCand == 1){ int id = g_cand[0]; g_setup.map = id; mapShowInfo(id); mapPlaceMarker(); }
		rm8StoryHostBegin(init == 0xB ? 0xB : 1, g_setup, g_cand, rm8StoryRunLevel());	// [V] vf16 @0x503100 then state 0xB of vf1 @0x503830 after a cleared stage
	}
	g_upd = [](float dt){
		if(g_setup.story){
			rm8StoryHostUpdate(dt);
			if(rm8StoryHostState() == 0x65){ rm8StoryHostEnd(); g_result = RM8MENU_START_GAME; return; }	// [V] state 0x65: CApp_StartNewGame / the start movie (the app)
			if(rm8StoryHostBusy()){	// [A] the host's line (the original shows none: widget 0x34 is not in chsMapMenu.ui) in the description area
				rm8UiSetText(g_ui, 98, ""); rm8UiSetText(g_ui, 99, rm8UiWrapText(rm8StoryHostLine(), 318, 16));
				return;
			}
		}
		int h = 0;
		for(int id = 1; id <= 11; id++) if(rm8UiHot(g_ui, id)) h = id;
		mapShowInfo(h ? h : (g_setup.map > 0 ? g_setup.map : 0));	// [V] hover callbacks 5 / 6: the hovered map's text, back to the selected one
		for(int k = 0; k < 6; k++) rm8UiSetBColor(g_ui, g_mm.items[k], rm8UiHot(g_ui, g_mm.items[k]) ? 0xffffff00 : 0x00ffffff);
	};
}
// ---- finalMenu (finalMenu.ui, state 7): the ending movie viewer
// [V] Cls_5e8050__vf16_Initialize @0x506fc0: button i (1..16) is enabled when Event_GetLevel(i) >= 4 (the story was finished with that role); Cls_5e8050__vf12 @0x507100:
//     1..16 -> CApp_PlayMovieRange(i - 1, 0) (the ending scripts, no staff credits afterwards: Movie_Finish returns to the finalMenu), 0x12 -> the single / mult menu
void enterFinal()
{
	Rm8Ui *u = loadUi("finalMenu");
	setScreenUi(u, [](int id){
		if(id >= 1 && id <= 16){ g_movieRole = id; g_result = RM8MENU_PLAY_ENDING; }
		else if(id == 18) go(S_GAME);
	});
	for(int r = 1; r <= 16; r++) rm8UiSetEnabled(u, r, rm8ProfileLevel(r) >= 4);
	g_back = [](){ go(S_GAME); };
}
// ---- option menu (optionMenu.ui, state 5)
// [V] CInitConfig__vf16_InitUI @0x50aa20: slider 1 music, 2 sound, 3 speed (init.txt speed - 2), 4 difficulty, 5 fullscreen, 6 character (character - 3)
// [V] Config_ApplySetting @0x50a710 applies a change at once; the back button (ID 7, vf12 @0x50ac30) saves init.txt (vf18 @0x50ab40) and returns to the main menu (@0x50ac60)
struct Slider { int id, lo, hi, knob; };
Slider g_sl[6] = {{1, 0, 5, 0}, {2, 0, 5, 0}, {3, 1, 5, 0}, {4, 1, 3, 0}, {5, 0, 1, 0}, {6, 1, 3, 0}};	// ranges [V] Min / Max keys of optionMenu.ui
int g_drag = -1, g_langBtn = 0;
int sliderGet(int id)
{
	switch(id){ case 1: return g_opt.music; case 2: return g_opt.sound; case 3: return g_opt.speed - 2; case 4: return g_opt.difficulty; case 5: return g_opt.fullscreen; }
	return g_opt.character - 3;
}
void sliderSet(int id, int v)
{
	for(const Slider &s : g_sl) if(s.id == id) v = std::max(s.lo, std::min(s.hi, v));
	switch(id){ case 1: g_opt.music = v; break; case 2: g_opt.sound = v; break; case 3: g_opt.speed = v + 2; break; case 4: g_opt.difficulty = v; break; case 5: g_opt.fullscreen = v; break; default: g_opt.character = v + 3; }
	applyOptions();
}
void placeKnobs()
{
	for(Slider &s : g_sl){
		float x, y, w, h;
		if(!rm8UiGetRect(g_ui, s.id, x, y, w, h)) continue;
		float t = (float)(sliderGet(s.id) - s.lo) / (float)(s.hi - s.lo);
		rm8UiSetRect(g_ui, s.knob, x + (w - 64) * t, y - 1, 64, 64);
	}
}
void langLabel(){ rm8UiSetText(g_ui, g_langBtn, std::string(u8"文字：") + (g_opt.simplified ? u8"簡體" : u8"繁體")); }
void enterOption()
{
	Rm8Ui *u = loadUi("optionMenu");
	g_ui = u;
	static Rm8Ui *knobFor = nullptr;
	if(knobFor != u){	// the knob (BtnNormalPic 212 of the slider) is not a widget of the .ui: one picture per slider
		knobFor = u;
		for(Slider &s : g_sl) s.knob = rm8UiAddWidget(u, "plane", 0, 0, 0, 64, 64, 212, 0, 16, 4);
		g_langBtn = rm8UiAddWidget(u, "button", 0, 180, 640, 300, 40, -1, 0x00000000, 22, 6);	// [A] text language switch (Simplified / Traditional)
		rm8UiSetColorN(u, ("dyn" + std::to_string(g_langBtn)).c_str(), 0xff202060);
	}
	setScreenUi(u, [](int id){
		if(id == 7){ saveOptions(); go(S_MAIN); }
		else if(id == g_langBtn){ g_opt.simplified = !g_opt.simplified; rm8ZhEnable(g_opt.simplified); langLabel(); saveOptions(); }
	});
	g_back = [](){ saveOptions(); go(S_MAIN); };
	g_drag = -1;
	langLabel();
	placeKnobs();
}
// slider dragging: pointer in screen pixels, kind 0 down / 1 move / 2 up
void optionPointer(int kind, float px, float py)
{
	if(g_scr != S_OPTION || !g_ui) return;
	if(kind == 0){
		for(const Slider &s : g_sl){
			float ax, ay, aw, ah;
			if(rm8UiGetScreenRect(g_ui, s.id, ax, ay, aw, ah) && px >= ax && px < ax + aw && py >= ay - ah * 0.2f && py < ay + ah * 1.2f) g_drag = s.id;
		}
	}
	if(kind == 2){ if(g_drag >= 0) saveOptions(); g_drag = -1; return; }
	if(g_drag < 0) return;
	for(const Slider &s : g_sl) if(s.id == g_drag){
		float ax, ay, aw, ah; rm8UiGetScreenRect(g_ui, s.id, ax, ay, aw, ah);
		float kw = 64.0f * ah / 62.0f, t = (px - ax - kw / 2) / std::max(1.0f, aw - kw);
		sliderSet(s.id, s.lo + (int)lroundf(std::max(0.0f, std::min(1.0f, t)) * (s.hi - s.lo)));
		placeKnobs();
	}
}
// ---- minigame menu (ltgameMenu.ui, state 6)
// [V] Cls_5e80a0__vf12_OnMessage @0x508350: 1..6 pick a game; 1 and 4 first go to the role menu with setup+4 = 100 + n (CApp_SwitchToMiniGame(n) after it, @0x506a30),
//     the others start at once (CApp_SwitchToMiniGame @0x50ddd0); 7 back -> main menu (setup+4 = 0, @0x5083e0)
// [V] u_Menu_ShowItemInfoByIndex @0x508030 (hover callbacks 5 / 6): label 10 = string 6965 + n, label 11 = string 6971 + n, picture 977 + n in ID 51
// [A] the HI-SCORE numbers (CUi_ltgameMenu number font @0x507ca0) come from the minigame records: not shown
void ltInfo(int n)
{
	rm8UiSetText(g_ui, 10, n ? S(6965 + n) : "");
	rm8UiSetText(g_ui, 11, n ? rm8UiWrapText(S(6971 + n), 352, 16) : "");
	rm8UiSetPic(g_ui, 51, n ? 977 + n : -1);
}
void startMiniGame(int n)
{
	if(rm8MenuHooks().startMiniGame && rm8MenuHooks().startMiniGame(n, g_setup)){ rm8MusicPlay(101, true); return; }	// blocking: returns when the game is over
	rm8Log("menu: minigame %d has no handler", n);
	rm8UiSetText(g_ui, 11, u8"小遊戲尚未接入。（移植版提示）");
}
void enterLtgame()
{
	Rm8Ui *u = loadUi("ltgameMenu");
	setScreenUi(u, [](int id){
		if(id == 7){ g_setup.map = 0; go(S_MAIN); }
		else if(id == 1 || id == 4){ g_setup.map = 100 + id; go(S_ROLES); }
		else if(id >= 1 && id <= 6) startMiniGame(id);
	});
	g_back = [](){ g_click(7); };
	g_setup.map = 0;	// [V] vf16 @0x508240
	ltInfo(0);
	g_upd = [](float){ int h = 0; for(int id = 1; id <= 6; id++) if(rm8UiHot(g_ui, id)) h = id; if(h) ltInfo(h); };
}

// ---- load menu (slmenu.ui, state 9): 10 slots, date, map picture, role portraits
// [V] CSaveSlots_vf16 @0x50cf20: slot n (1..10) reads save\record_NN.sav (story) / save\record_mNN.sav (free game); label 40 + n = date (string 7103 "%d月%d日%d時%d分"),
//     picture 30 + n = map picture 945 + map (1116 for a map >= 12), role portraits 47 + 4n + k = npc\<pic>_12; an empty slot is disabled
// [V] CSaveSlots_vf12 @0x50d490: click slot n -> CApp_LoadSavedGame(path); 21 back to the single / mult menu (@0x50d560)
// [A] saving / loading is another module: the slot headers and the load come through rm8MenuHooks() (without them every slot is empty)
void loadRefresh()
{
	for(int n = 1; n <= 10; n++){
		Rm8SaveSlotInfo info; bool has = rm8MenuHooks().saveSlotInfo && rm8MenuHooks().saveSlotInfo(n, !g_setup.story, info);
		rm8UiSetEnabled(g_ui, n, has);
		char b[64]; snprintf(b, sizeof b, S(7103).c_str(), info.month, info.day, info.hour, info.minute);
		rm8UiSetText(g_ui, 40 + n, has ? b : "");
		rm8UiSetVisible(g_ui, 30 + n, has);
		if(has) rm8UiSetPic(g_ui, 30 + n, info.map < 12 ? 945 + info.map : 1116);
		for(int k = 0; k < 4; k++){
			bool hk = has && k < (int)info.charIds.size() && g_roles.count(info.charIds[k]);
			rm8UiSetVisible(g_ui, 47 + n * 4 + k, hk);
			if(hk) rm8UiSetPicFile(g_ui, 47 + n * 4 + k, "prt\\npc\\" + g_roles[info.charIds[k]].pic + "_12.tga");
		}
	}
}
void enterLoad()
{
	Rm8Ui *u = loadUi("slmenu");
	setScreenUi(u, [](int id){
		if(id == 21) go(S_GAME);
		else if(id >= 1 && id <= 10){ if(rm8MenuHooks().loadGame && rm8MenuHooks().loadGame(id, !g_setup.story)) g_result = RM8MENU_LOAD_GAME; }
	});
	g_back = [](){ go(S_GAME); };
	loadRefresh();
}

// ---- in-game system menu (gsys.ui) and the exit confirmation (exit.ui)
// [V] gmaindlg button 10 in state 6 -> task 0x68 gsys (CGamePlayScene__vf12 @0x4a2e10); gsys buttons (@0x4aa700): 1 supply -> supply.ui, 2 save, 3 load, 4 setting, 5 close, 6 exit -> exit.ui (@0x4aa790 / 0x4927b0)
// [V] exit confirmed: message 0x78 -> Game__ExitMainLoop @0x490010 (game state 3) -> 0x4a17b0 CApp_SwitchToGameScene(3) = the map menu of a free game
// [A] 1 (auto play), 2 / 3 (save / load), 4 (setting) are not wired yet
Rm8Ui *g_gsys = nullptr, *g_exit = nullptr;
bool g_exitReq = false;
void gsysInit();
void gsysAuto(int step)
{
	gsysInit();
	if(step == 0) rm8MenuGameSystemOpen(true);
	else if(step == 1 && g_exit) rm8UiShow(g_exit, true);
	else if(g_exit){ g_exitReq = true; rm8UiShow(g_exit, false); rm8UiShow(g_gsys, false); }
}
void gsysInit()
{
	if(g_gsys) return;
	g_gsys = rm8UiLoad("gsys", RM8UI_FLOAT);
	g_exit = rm8UiLoad("exit", RM8UI_DIALOG);
	if(g_gsys) rm8UiOnClick(g_gsys, [](int id){
		if(id == 5) rm8UiShow(g_gsys, false);
		else if(id == 6 && g_exit) rm8UiShow(g_exit, true);
		else if(id == 2 || id == 3){ rm8UiShow(g_gsys, false); rm8SaveUiOpen(id == 2); }	// [V] gsys buttons 2 / 3: the save / load dialog (save_ui.cpp)
		else if(id == 1){ rm8UiShow(g_gsys, false); if(!rm8AutoSupplyOpen()) rm8Log("menu: auto play is only offered while the human waits for the dice"); }	// [V] gsys button 1 -> NPC_SendActOnPlayer2 @0x4aa810 -> CUi_supply__ShowUI @0x4928e0 (roles.cpp, docs/autoplay.md)
		else if(id == 4){ rm8UiShow(g_gsys, false); rm8InfoSettingOpen(); }	// [V] gsys button 4 -> NPC_SendMiniGameSignal @0x4aa890: the setting task 0x69 (infoscreens.cpp)
		else rm8Log("menu: system menu entry %d is not wired", id);
	});
	if(g_exit) rm8UiOnClick(g_exit, [](int id){
		if(id == 2){ g_exitReq = true; rm8UiShow(g_exit, false); rm8UiShow(g_gsys, false); }
		else if(id == 3) rm8UiShow(g_exit, false);
	});
}
// SCREENS-END

void go(Scr s)
{
	g_scr = s;
	switch(s){
	case S_LOGO: enterLogo(); break;
	case S_MAIN: enterMain(); break;
	case S_GAME: enterGame(); break;
	case S_ROLES: enterRoles(); break;
	case S_MAP: enterMap(); break;
	case S_OPTION: enterOption(); break;
	case S_LTGAME: enterLtgame(); break;
	case S_LOAD: enterLoad(); break;
	case S_FINAL: enterFinal(); break;
	default: break;
	}
}

// menu_extra.cpp screens: the movies, picmenu and boxmenu use the same screen plumbing as the screens above
const Rm8MenuEnv g_extraEnv = {
	[](const char *name) -> Rm8Ui* { return loadUi(name); },
	[](Rm8Ui *u, std::function<void(int)> click, std::function<void()> back, std::function<void(float)> upd){ setScreenUi(u, click); g_back = back; g_upd = upd; },
	[](int to){ go(to == 0 ? S_MAIN : to == 1 ? S_GAME : S_MAP); },
};
void goExtra(int which){ g_scr = S_EXTRA; rm8MenuExtraEnter(g_extraEnv, which); }

// ---- test hook: RM8_MENU_AUTO="multi,new,map:1,start" presses through the menus (docs/menus.md); shot:<name> writes menu_<name>.png
void gsysAuto(int step);
struct Auto { std::vector<std::string> tok; size_t pos = 0; int wait = 20; bool on = false; std::string shotDir = "."; std::string pendingShot; } g_auto;
void autoStep()
{
	if(!g_auto.on || g_auto.pos >= g_auto.tok.size()) return;
	if(g_auto.wait > 0){ g_auto.wait--; return; }
	std::string t = g_auto.tok[g_auto.pos++];
	g_auto.wait = 6;
	auto arg = [&](size_t i){ size_t p = 0; for(size_t k = 0; k < i; k++){ p = t.find(':', p); if(p == std::string::npos) return std::string(); p++; } size_t e = t.find(':', p); return t.substr(p, e == std::string::npos ? e : e - p); };
	rm8Log("menu auto: %s (screen %d)", t.c_str(), (int)g_scr);
	if(rm8SaveUiAuto(t)) return;	// savedlg / loaddlg / slot:N / cover:ok / cover:cancel / dlgclose (save_ui.cpp)
	if(t.rfind("sysbtn:", 0) == 0){	// a real pointer press on button N of the system menu (gsys.ui): 2 save, 3 load
		float x, y, w, h;
		if(g_gsys && rm8UiGetScreenRect(g_gsys, atoi(t.c_str() + 7), x, y, w, h)){ float cx = x + w / 2, cy = y + h / 2; rm8UiPointerMove(cx, cy); rm8UiPointerDown(cx, cy, true); rm8UiPointerUp(cx, cy); }
		else rm8Log("menu auto: no system menu button %s", t.c_str() + 7);
		return;
	}
	if(t == "sys" || t == "sys6" || t == "sysok"){ gsysAuto(t == "sys" ? 0 : t == "sys6" ? 1 : 2); return; }
	if(t.rfind("shot:", 0) == 0){ g_auto.pendingShot = arg(1); g_auto.wait = 3; return; }
	if(t.rfind("wait:", 0) == 0){ g_auto.wait = atoi(arg(1).c_str()); return; }
	if(t == "back"){ if(g_back) g_back(); return; }
	if(t == "skip"){ rm8StoryHostSkip(); return; }	// the click that skips the host (Cls_5e81e0__vf13 @0x5041f0)
	// tap:id = a real pointer press / release (touch) at the centre of widget id of the current screen: tests the hit testing, not only the handler
	if(t.rfind("tap:", 0) == 0){
		float x, y, w, h;
		if(g_ui && rm8UiGetScreenRect(g_ui, atoi(arg(1).c_str()), x, y, w, h)){
			float cx = x + w / 2, cy = y + h / 2;
			rm8UiPointerMove(cx, cy); rm8UiPointerDown(cx, cy, true); { PointerGuard pg; rm8UiPointerUp(cx, cy); }
		}else rm8Log("menu auto: no widget %s", arg(1).c_str());
		return;
	}
	if(rm8MenuExtraAuto(g_extraEnv, t)) return;	// movie / box test tokens (menu_extra.cpp)
	if(!g_click) return;
	if(t == "single"){ g_click(1); }
	else if(t == "multi"){ g_click(2); }
	else if(t == "option"){ g_click(3); }
	else if(t == "minigame"){ g_click(4); }
	else if(t == "load"){ g_click(g_scr == S_MAIN ? 0 : 2); }
	else if(t.rfind("click:", 0) == 0) g_click(atoi(arg(1).c_str()));
	else if(t.rfind("opt:", 0) == 0){ g_click(13 + atoi(arg(1).c_str())); int k = atoi(arg(2).c_str()); if(k >= 0 && k < 6) g_click(g_mm.items[k]); }	// opt:row:item = open the list of a map-menu row and pick an entry
	else if(t == "new") g_click(1);
	else if(t == "start") g_click(g_scr == S_ROLES ? 17 : 118);
	else if(t.rfind("role:", 0) == 0) g_click(atoi(arg(1).c_str()));
	else if(t.rfind("map:", 0) == 0) g_click(atoi(arg(1).c_str()));
	else rm8Log("menu auto: unknown token %s", t.c_str());
}

}	// namespace

std::string rm8MenuMapDir(int id){ return mapDirOf(id); }

// ---- public interface
bool rm8MenuInit(const std::string &gameDir, const std::string &scenesRoot, const std::vector<std::string> &maps)
{
	g_game = gameDir; g_root = scenesRoot;
	g_settingsFile = (std::filesystem::path(rm8UserDataDir(scenesRoot)) / "rm8_settings.txt").string();	// [A] user data folder (scene.cpp)
	g_stages.clear(); g_downloads.clear();
	std::ifstream sf(scenesRoot + "/stages.txt"); std::string line;
	while(std::getline(sf, line)){
		std::istringstream ss(line); std::string kw; Stage st;
		if(ss >> kw >> st.id >> st.dir >> st.clime >> st.name && kw == "stage"){ std::replace(st.name.begin(), st.name.end(), '_', ' '); g_stages.push_back(st); }
	}
	for(const std::string &m : maps) if(m.rfind("download", 0) == 0) g_downloads.push_back(m);
	std::ifstream rf(scenesRoot + "/roles.txt");
	while(std::getline(rf, line)){
		if(line.rfind("rstr ", 0) == 0){ int id = 0, s = 0; if(sscanf(line.c_str(), "rstr %d %d", &id, &s) == 2) g_roles[id].strId = s; continue; }	// [V] roleinfo strId (+0x38c)
		if(line.rfind("role ", 0) != 0) continue;
		std::istringstream ss(line); std::vector<std::string> tk; std::string w;
		while(ss >> w) tk.push_back(w);
		if(tk.size() < 5) continue;
		RoleInfo &r = g_roles[atoi(tk[1].c_str())]; r.id = atoi(tk[1].c_str()); r.pic = tk[tk.size() - 3]; r.name = tk.back(); if(tk.size() >= 36) r.sex = atoi(tk[tk.size() - 4].c_str());	// [H] roleinfo +0x304 = the field after Id = sex (roles.txt: ... sex pic AI name)
	}
	loadOptions();
	rm8MenuExtraInit(gameDir, scenesRoot);
	rm8MenuAnimInit(scenesRoot);
	applyOptions();
	if(const char *a = getenv("RM8_MENU_AUTO")){
		g_auto.on = true; std::string s = a, cur;
		for(char c : s){ if(c == ','){ if(!cur.empty()) g_auto.tok.push_back(cur); cur.clear(); } else cur += c; }
		if(!cur.empty()) g_auto.tok.push_back(cur);
		if(const char *d = getenv("RM8_MENU_SHOTDIR")) g_auto.shotDir = d;
	}
	g_inited = !g_stages.empty() && !g_roles.empty() && loadUi("mainmenu");
	rm8Log("menu: %d stages + %d download maps, %d roles, settings %s", (int)g_stages.size(), (int)g_downloads.size(), (int)g_roles.size(), g_settingsFile.c_str());
	return g_inited;
}

void rm8MenuOpen(bool showLogo, bool backToMapMenu)
{
	if(!g_inited) return;
	g_active = true; g_result = RM8MENU_RUNNING; g_time = 0;
	rm8MenuAnimReset();
	rm8UiHideAll();
	if(backToMapMenu){ g_setup.story = false; go(S_MAP); rm8MusicPlay(101, true); }	// [V] 0x4a17b0: game state 3 -> CApp_SwitchToGameScene(3)
	else go(showLogo && !getenv("RM8_NOLOGO") ? S_LOGO : S_MAIN);
}
void rm8MenuClose(){ g_active = false; rm8StoryHostEnd(); rm8MenuAnimLeave(); rm8UiHideAll(); g_ui = nullptr; g_click = nullptr; g_back = nullptr; g_upd = nullptr; }
int rm8MenuMovieRole(){ return g_movieRole; }
// [V] CApp_SwitchToGameScene @0x405650 with the scene the game state asks for (0x4a17b0): 3 (story) -> the role menu, 4 -> the map menu in state 0xB, and the finalMenu after an ending movie
void rm8MenuOpenPage(Rm8MenuPage page)
{
	if(!g_inited) return;
	g_active = true; g_result = RM8MENU_RUNNING; g_time = 0;
	rm8MenuAnimReset();
	rm8UiHideAll();
	g_setup.story = true;
	switch(page){
	case RM8PAGE_ROLES:
		if(g_setup.seats.size() > 1) g_setup.seats.resize(1);	// [A] the original keeps the opponent seats; the start button then needs exactly one seat, so the port trims them
		g_setup.map = 0; go(S_ROLES); break;
	case RM8PAGE_MAP_NEXT: g_mapInit = 0xB; go(S_MAP); break;
	case RM8PAGE_FINAL: go(S_FINAL); break;
	}
	rm8MusicPlay(101, true);
}
void rm8MenuPlayStaffCredits(){ if(g_active && g_inited) goExtra(RM8X_MOVIE_STAFF); }
bool rm8MenuActive(){ return g_active; }
const Rm8Setup &rm8MenuSetup(){ return g_setup; }
const Rm8Options &rm8MenuOptions(){ return g_opt; }

#ifdef RM8_MENU_SDL
bool rm8MenuEvent(const SDL_Event &e, int w, int h)
{
	if(!g_active) return false;
	g_sw = w; g_sh = h;
	switch(e.type){
	case SDL_KEYDOWN:
		if(e.key.keysym.scancode == SDL_SCANCODE_AC_BACK || e.key.keysym.sym == SDLK_ESCAPE){ if(g_back) g_back(); return true; }
		return true;	// no other key reaches the board code
	case SDL_MOUSEMOTION: if(e.motion.which == SDL_TOUCH_MOUSEID) return true; rm8UiPointerMove((float)e.motion.x, (float)e.motion.y); optionPointer(1, (float)e.motion.x, (float)e.motion.y); return true;
	case SDL_MOUSEBUTTONDOWN: if(e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_LEFT) { rm8StoryHostSkip(); rm8UiPointerDown((float)e.button.x, (float)e.button.y); optionPointer(0, (float)e.button.x, (float)e.button.y); } return true;
	case SDL_MOUSEBUTTONUP: if(e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_LEFT) { rm8UiPointerUp((float)e.button.x, (float)e.button.y); optionPointer(2, (float)e.button.x, (float)e.button.y); } return true;	// [V] a mouse click switches at once like the original (the pointer has hovered, the icon is turning)
	case SDL_MOUSEWHEEL: return true;
	case SDL_FINGERDOWN: rm8StoryHostSkip(); rm8UiPointerMove(e.tfinger.x * w, e.tfinger.y * h); rm8UiPointerDown(e.tfinger.x * w, e.tfinger.y * h); optionPointer(0, e.tfinger.x * w, e.tfinger.y * h); return true;
	case SDL_FINGERMOTION: rm8UiPointerMove(e.tfinger.x * w, e.tfinger.y * h); optionPointer(1, e.tfinger.x * w, e.tfinger.y * h); return true;
	case SDL_FINGERUP: { PointerGuard pg; rm8UiPointerUp(e.tfinger.x * w, e.tfinger.y * h); } optionPointer(2, e.tfinger.x * w, e.tfinger.y * h); return true;
	}
	return false;
}
#else
bool rm8MenuEvent(const SDL_Event &, int, int){ return false; }
#endif

Rm8MenuResult rm8MenuUpdate(float dt)
{
	if(!g_active) return RM8MENU_RUNNING;
	g_time += dt;
	rm8MenuAnimUpdate(dt);
	autoStep();
	if(g_upd) g_upd(dt);
	return g_result;
}

void rm8MenuRender(Camera *cam){ if(g_active){ rm8MenuExtraRender(cam); rm8UiRender(cam); rm8StoryHostRender(cam); rm8MenuAnimRender(cam); } }

#ifdef RM8_MENU_SDL
void rm8MenuPostRender(int w, int h)
{
	if(g_auto.pendingShot.empty()) return;
	typedef void (*ReadPixelsFn)(int, int, int, int, unsigned, unsigned, void*);
	ReadPixelsFn rp = (ReadPixelsFn)SDL_GL_GetProcAddress("glReadPixels");
	std::string name = g_auto.pendingShot; g_auto.pendingShot.clear();
	if(!rp){ rm8Log("menu shot: no glReadPixels"); return; }
	std::vector<unsigned char> px((size_t)w * h * 4);
	rp(0, 0, w, h, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, px.data());
	Image *img = Image::create(w, h, 32); img->allocate();
	for(int y = 0; y < h; y++) memcpy(img->pixels + (size_t)y * img->stride, &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
	std::string path = g_auto.shotDir + "/menu_" + name + ".png";
	writePNG(img, path.c_str()); img->destroy();
	rm8Log("menu shot: %s", path.c_str());
}
#else
void rm8MenuPostRender(int, int){}
#endif

void rm8MenuGameSystemOpen(bool open)
{
	gsysInit();
	if(!g_gsys) return;
	rm8UiShow(g_gsys, open);
	if(!open && g_exit) rm8UiShow(g_exit, false);
}
void rm8MenuGameTick(){ autoStep(); }
bool rm8MenuGameSystemVisible(){ return g_gsys && rm8UiVisible(g_gsys); }
bool rm8MenuGameSystemExitRequested(){ bool r = g_exitReq; g_exitReq = false; return r; }
// [A] the in-game setting screen (infoscreens.cpp, task 0x69) uses the option menu's values: slider ids 1 music, 2 sound, 3 speed - 2 (Config_ApplySetting @0x50a710)
int rm8MenuOptionGet(int sliderId){ return sliderGet(sliderId); }
void rm8MenuOptionSet(int sliderId, int v){ sliderSet(sliderId, v); }
void rm8MenuOptionsSave(){ saveOptions(); }
// the name of a map by its scene folder ([V] Container_FindByValue(setup + 4) @0x4a8f80: the stage name, a download map's file name)
std::string rm8MenuMapNameByDir(const std::string &dir)
{
	for(const Stage &s : g_stages) if(s.dir == dir) return s.name;
	return dir;
}
