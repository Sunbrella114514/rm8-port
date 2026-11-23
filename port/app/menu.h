// Front-end menus (docs/menus.md): logo -> main menu -> single / multi game menu -> (map, options, players) -> start a board game.
// The original menus are state objects created by Obj_CreateByTypeAndInit @0x509910 (1 mainmenu, 2 chsRoleMenu, 3 chsMapMenu,
// 5 optionMenu, 6 ltgameMenu, 8 single / mult, 9 slmenu); the port runs them as screens of one module on the original .ui files.
#pragma once
#include "scene.h"
#include <functional>
#include <string>
#include <vector>

union SDL_Event;	// SDL.h is only needed in menu.cpp (the GLFW viewer target builds menu.cpp without SDL)

// one seat of the player array of the setup block (the game's +0x20 array, 12 bytes each; docs/save_format.md section 3)
struct Rm8Seat { int charId = 1; int outfit = 0; bool computer = false; };	// [V] roleinfo Id 1..16, +4 variant, +8 0 human / 1 computer

// [V] the setup block &DAT_00632de8 (Singleton_GetPtrArray_00632de8 @0x4083f0, docs/rules_land.md 1.1)
struct Rm8Setup {
	bool story = false;		// +0x00 1 = the "single" menu (story mode), 0 = the "multi" menu (free game)
	int map = 0;			// +0x04 stage id (1..11, 21.. download maps); 100 + n = minigame n chosen in ltgameMenu
	int cash = 100000;		// +0x0C total start money (cash = deposit = half), choices 50000 / 100000 / 200000
	int tickets = 50;		// +0x10 start points (tickets): 0 / 50 / 100
	int vehicle = 0;		// +0x14 start vehicle: 0 walk, 1 motorbike, 2 car
	int timeLimit = 2;		// +0x18 0 three months, 1 six months, 2 unlimited
	int winCond = 5;		// +0x1C winner at the end date: 0 plots 1 assets 2 deposit 3 points 4 stocks 5 bankruptcy only
	std::vector<Rm8Seat> seats;	// +0x20 players (1..4)
};

// [A] game options the option menu edits (init.txt [param] in the original, Config_ApplySetting @0x50a710); saved to <data dir>/rm8_settings.txt
// [V] defaults of InitConfig_LoadInitTxt @0x401080 when there is no init.txt: speed (internal, ini + 2) 4, difficulty 1, sound 4, music 3
struct Rm8Options { int music = 3, sound = 4, speed = 4, difficulty = 1, fullscreen = 0, character = 6; bool simplified = true; bool plus = false; /* [R] rules_plus: the enhanced AI / house rules for the next game (rm8_settings.txt key rules_plus) */
	int netPort = 47000; std::string netLast; /* [N] netplay: the host's listen port (net_port), the last address joined (net_last) */
	std::string netName; /* [N] netplay: the player name of this device (net_name; "" = the default from the player id) */ };

// RM8MENU_PLAY_ENDING: the finalMenu (ending movie viewer) asks for the ending movies of rm8MenuMovieRole(); the app plays them and reopens the finalMenu page
enum Rm8MenuResult { RM8MENU_RUNNING, RM8MENU_START_GAME, RM8MENU_QUIT, RM8MENU_LOAD_GAME, RM8MENU_MINIGAME, RM8MENU_PLAY_ENDING, RM8MENU_NETPLAY /* [N] the 联机 button of the mult page */ };

// Other modules plug in here (all optional; an unset hook makes the entry unavailable / returns false).
struct Rm8SaveSlotInfo { int year = 0, month = 0, day = 0, hour = 0, minute = 0, map = 0; std::vector<int> charIds; };	// [V] 0x50cf20: header +2/+6/+8/+10, +0x10 map, +0x14 count, +0x18.. roles
struct Rm8MenuHooks {
	std::function<bool(int slot, bool multi, Rm8SaveSlotInfo &out)> saveSlotInfo;	// slmenu: header of record_NN.sav (story) / record_mNN.sav (multi); false = empty slot
	std::function<bool(int slot, bool multi)> loadGame;				// slmenu: load the slot (CApp_LoadSavedGame @0x406380); true = the game started
	std::function<bool(int game, const Rm8Setup &setup)> startMiniGame;		// ltgameMenu: CApp_SwitchToMiniGame(n) @0x50ddd0; true = started
	std::function<void()> multPage;							// [N] the free game page (mult) was entered (netplay: an armed host stops listening)
	std::function<void(const Rm8Options &o)> applyOptions;				// option menu / startup: music + sound levels, speed, difficulty
	// [N] netplay room on the original free-game screens (docs/netplay.md section 5)
	std::function<void()> netStart;							// host: the role menu's start (O) in a room: start / ask about the force start
	std::function<void()> netReady;							// client: O on the mirrored map / role menu toggles the ready flag
	std::function<void()> netLeave;							// client: X / back on the mirrored screens leaves the room
	std::function<void(int charId, int outfit)> netPickRole;				// client: a character (outfit -1 = the first free one) / an outfit (charId -1) for its own seat
};
Rm8MenuHooks &rm8MenuHooks();

// ---- options (the one source of difficulty / speed / volumes / window mode)
void rm8MenuLoadOptions(const std::string &gameDir, const std::string &scenesRoot);	// reads init.txt + rm8_settings.txt and applies them; rm8MenuInit calls it, the test modes without menus call it themselves
// rm8Difficulty() (scene.h) = [V] InitTxt_GetParam(7), live: the current option slider (1..3); RM8_DIFFICULTY overrides for tests

// ---- lifecycle (rm8_app.cpp)
// Loads the .ui files, the stage list and the options; true if the menus can run (needs the packs and scenes/ui).
bool rm8MenuInit(const std::string &gameDir, const std::string &scenesRoot, const std::vector<std::string> &maps);
void rm8MenuOpen(bool showLogo, bool backToMapMenu);	// start the menus (main menu after the logo; or the map menu = "exit game" of a free game)
void rm8MenuClose();					// hide the menu screens (the game starts)
// story pages the game states open directly (CApp_SwitchToGameScene @0x405650): ROLES = state 3 of a story game (the player declined "<str#93>", role menu),
// MAP_NEXT = state 4 (a stage was cleared: the map menu starts in its state 0xB, the host announces the next choice), FINAL = state 7 (back from an ending movie)
enum Rm8MenuPage { RM8PAGE_ROLES = 1, RM8PAGE_MAP_NEXT, RM8PAGE_FINAL, RM8PAGE_FREE_GAME /* [N] the mult page */, RM8PAGE_FREE_MAP /* [N] the free game's map menu */, RM8PAGE_NET_LOAD /* [N] the free game's load menu for a netplay resume */ };

void rm8MenuOpenPage(Rm8MenuPage page);
int rm8MenuMovieRole();					// the role of the ending movie the finalMenu asked for (RM8MENU_PLAY_ENDING)
void rm8MenuPlayStaffCredits();				// [V] the staff movie after a story ending (Movie_Finish @0x515d40, docs/media_collect.md); back to the single menu when it ends
bool rm8MenuActive();
bool rm8MenuEvent(const SDL_Event &e, int w, int h);	// pointer / key input; true = consumed
Rm8MenuResult rm8MenuUpdate(float dt);			// per frame; the result is RM8MENU_RUNNING until a screen finishes
void rm8MenuRender(rw::Camera *cam);			// clears nothing; call between beginUpdate / endUpdate
void rm8MenuPostRender(int w, int h);			// test hook: RM8_MENU_AUTO shots (reads the back buffer before the swap)
std::string rm8MenuMapDir(int mapId);			// scene folder of a setup map id ("01".., "download201"..); "" if unknown
const Rm8Setup &rm8MenuSetup();				// valid after RM8MENU_START_GAME / RM8MENU_MINIGAME
const Rm8Options &rm8MenuOptions();
void rm8MenuSetLoadedSetup(const Rm8Setup &st);		// a loaded record's setup becomes the menu's (the story map menu after a stage cleared in a loaded game needs the hero, bug 49)
void rm8MenuSetNetLast(const std::string &addr);		// [N] netplay: remember the last joined address (rm8_settings.txt net_last)
bool rm8MenuRoleInfo(int id, std::string &name, std::string &pic);	// [N] roleinfo name / picture (p_NN) of a character id (the netplay lobby)
void rm8MenuSetNetName(const std::string &name);		// [N] netplay: this device's player name (rm8_settings.txt net_name, "" = the default)

// ---- [N] netplay room (docs/netplay.md section 5): the host sets the free game up in the ORIGINAL map menu (map + the five start options) and role menu
// (seats, outfits); a seat's human / computer tag gains a third state there, "waiting for a network player". The clients see the same screens mirrored
// (read only: O = ready, X = leave, a character / outfit click asks the host for their own seat). net.cpp publishes / applies the view.
enum Rm8MenuNetMode { RM8MENU_NET_OFF = 0, RM8MENU_NET_HOST, RM8MENU_NET_MIRROR };
struct Rm8MenuNetSeat { int kind = 2, serial = 0; };	// kind 0 a seat of the host's device, 1 a network player's seat, 2 computer; serial: stays with the seat while others are removed
struct Rm8MenuNetView { int screen = 0; Rm8Setup setup; int sel = -1; std::vector<Rm8MenuNetSeat> seats; };	// screen 0 none, 1 map menu, 2 role menu
void rm8MenuNetSetMode(int mode);
int rm8MenuNetMode();
bool rm8MenuNetGet(Rm8MenuNetView &v);				// host: the current screen, setup and seat kinds
void rm8MenuNetMirror(const Rm8MenuNetView &v, int mySeat);	// client: the host's screen with the host's values (opens the menus on that page)
bool rm8MenuNetAddGuestSeat();					// host: adds a network seat for a guest that found none (false = the room is full)
bool rm8MenuNetSetSeatRole(int serial, int charId, int outfit);	// host: a client's character / outfit request for its seat (false = taken / invalid)
bool rm8MenuNetLoadPage();					// the load menu was opened for a netplay resume (RM8PAGE_NET_LOAD): its pick is hosted, not loaded locally
void rm8MenuNetTestRoom(const Rm8Setup &st, const std::vector<int> &kinds);	// test hook (RM8_NET_HOST + RM8_NET_SETUP): the host's role menu with this setup / seat kinds


// ---- in-game system menu (gsys.ui, opened by gmaindlg button 10 @0x4a2e10 / Esc / Android back). "exit" -> exit.ui -> back to the menus
void rm8MenuGameSystemOpen(bool open);
bool rm8MenuGameSystemVisible();
bool rm8MenuGameSystemExitRequested();			// true once after the exit was confirmed (the app unloads the board and calls rm8MenuOpen(false, true))
int rm8MenuOptionGet(int sliderId);			// [A] option values for the in-game setting screen (1 music 0..5, 2 sound 0..5, 3 speed - 2, 1..5); Set applies at once, Save writes rm8_settings.txt
void rm8MenuOptionSet(int sliderId, int v);
void rm8MenuOptionsSave();
std::string rm8MenuMapNameByDir(const std::string &dir);	// "01" -> the stage name; a download map's folder name
void rm8MenuGameTick();				// test hook (RM8_MENU_AUTO tokens sys / sys6 / sysok / shot:x) while a menu game runs; call once per frame
