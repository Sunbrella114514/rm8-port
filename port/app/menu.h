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
struct Rm8Options { int music = 3, sound = 4, speed = 5, difficulty = 3, fullscreen = 0, character = 6; bool simplified = true; };

// RM8MENU_PLAY_ENDING: the finalMenu (ending movie viewer) asks for the ending movies of rm8MenuMovieRole(); the app plays them and reopens the finalMenu page
enum Rm8MenuResult { RM8MENU_RUNNING, RM8MENU_START_GAME, RM8MENU_QUIT, RM8MENU_LOAD_GAME, RM8MENU_MINIGAME, RM8MENU_PLAY_ENDING };

// Other modules plug in here (all optional; an unset hook makes the entry unavailable / returns false).
struct Rm8SaveSlotInfo { int year = 0, month = 0, day = 0, hour = 0, minute = 0, map = 0; std::vector<int> charIds; };	// [V] 0x50cf20: header +2/+6/+8/+10, +0x10 map, +0x14 count, +0x18.. roles
struct Rm8MenuHooks {
	std::function<bool(int slot, bool multi, Rm8SaveSlotInfo &out)> saveSlotInfo;	// slmenu: header of record_NN.sav (story) / record_mNN.sav (multi); false = empty slot
	std::function<bool(int slot, bool multi)> loadGame;				// slmenu: load the slot (CApp_LoadSavedGame @0x406380); true = the game started
	std::function<bool(int game, const Rm8Setup &setup)> startMiniGame;		// ltgameMenu: CApp_SwitchToMiniGame(n) @0x50ddd0; true = started
	std::function<void(const Rm8Options &o)> applyOptions;				// option menu / startup: music + sound levels, speed, difficulty
};
Rm8MenuHooks &rm8MenuHooks();

// ---- lifecycle (rm8_app.cpp)
// Loads the .ui files, the stage list and the options; true if the menus can run (needs the packs and scenes/ui).
bool rm8MenuInit(const std::string &gameDir, const std::string &scenesRoot, const std::vector<std::string> &maps);
void rm8MenuOpen(bool showLogo, bool backToMapMenu);	// start the menus (main menu after the logo; or the map menu = "exit game" of a free game)
void rm8MenuClose();					// hide the menu screens (the game starts)
// story pages the game states open directly (CApp_SwitchToGameScene @0x405650): ROLES = state 3 of a story game (the player declined "<str#93>", role menu),
// MAP_NEXT = state 4 (a stage was cleared: the map menu starts in its state 0xB, the host announces the next choice), FINAL = state 7 (back from an ending movie)
enum Rm8MenuPage { RM8PAGE_ROLES = 1, RM8PAGE_MAP_NEXT, RM8PAGE_FINAL };
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

// ---- in-game system menu (gsys.ui, opened by gmaindlg button 10 @0x4a2e10 / Esc / Android back). "exit" -> exit.ui -> back to the menus
void rm8MenuGameSystemOpen(bool open);
bool rm8MenuGameSystemVisible();
bool rm8MenuGameSystemExitRequested();			// true once after the exit was confirmed (the app unloads the board and calls rm8MenuOpen(false, true))
int rm8MenuOptionGet(int sliderId);			// [A] option values for the in-game setting screen (1 music 0..5, 2 sound 0..5, 3 speed - 2, 1..5); Set applies at once, Save writes rm8_settings.txt
void rm8MenuOptionSet(int sliderId, int v);
void rm8MenuOptionsSave();
std::string rm8MenuMapNameByDir(const std::string &dir);	// "01" -> the stage name; a download map's folder name
void rm8MenuGameTick();				// test hook (RM8_MENU_AUTO tokens sys / sys6 / sysok / shot:x) while a menu game runs; call once per frame
