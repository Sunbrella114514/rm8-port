// Minigames (ltlgame_18..23.dll of the original) re-implemented natively. See docs/minigames/port.md.
// The DLL framework (host interface CMiniGameHost_2 @0x5E8928, game object vtable Init / GetResult / Update / Draw / KeyDown /
// KeyUp / MouseButton / MouseMove) is mirrored by mg_core.h; every game lives in its own mg_NN.cpp.
// The whole thing renders in the game's native 1024x768 space, letterboxed (uniform scale H / 768) into the camera's frame buffer.
#pragma once
#include "scene.h"
#include <string>
#include <vector>
#include <functional>

// one player as CMiniGameHost_vf16 @0x50DDD0 builds it (current player first)
struct Rm8MiniPlayer {
	int seat = 0;		// [V] role +0x1A8 (the "id" echoed back in the result)
	bool human = true;	// [V] params type 2 = human, 1 = computer (role +0x1B0 != 0)
	int charId = 1;		// roleinfo Id 1..16 (role +0x308 -> icon; game 21 uses it for voice ids)
	int outfit = 0;		// [V] descriptor +0x50 = role +0x1ac: the outfit 0..4 (game 21: 4 = swimsuit model, icon + 0xc0 @10005d37)
	std::string pic;	// roleinfo picName (p_NN): portrait prt\npc\<pic>_01 (game 18 draws the second player's)
	std::string name;
};

// the GetResult structure (out[0] = count, then 5 ints per player: seat, score, flagA, rating, flagB) [V] 0x50E300
struct Rm8MiniPlayerResult { int seat = 0, score = 0, flagA = 0, rating = 0, flagB = 0; };
struct Rm8MiniResult { int game = 0, count = 0; Rm8MiniPlayerResult p[8]; };

// what the host does with a result (MiniGameHost_Cleanup @0x50E300) for the player whose turn it is / the other players
struct Rm8MiniReward {
	int seat = -1;
	int ticketsDelta = 0;		// Role_AddTicket / Role_SubTicket (the caller clamps 0..9999)
	std::vector<int> cards;		// card.bin indexes handed out (Id = index + 1) by Role_EnqueueMsg(4, idx, cardEmote)
	int cardEmote = 0;		// [V] the third argument of that message: 0 in MiniGameHost_Cleanup @0x50E300 for the rewards of games 18..23 (no emote), 1 for the bodyguard card of game 21 (@0x50E7AA)
	int hospitalDays = 0;		// game 21: the defeated pirate (Role_EnqueueMsg(0x18, 3))
	int textId = 0;			// string.bin id of the message (%s = player name, %d = tickets / days), 0 = none
	int textArg = 0;
	int reaction = 0;		// FUN_00484890(n): 4 (rating 0), 1 (rating 1), 2 (rating 2)
	int bestScore = -1;		// FUN_004aa5d0(event - 0x11, score): personal record (>= 0 when set)
};

bool rm8MiniInit(const std::string &scenesRoot);	// loads minigame/sprites.txt (scripts/export_minigame.py); after rm8UiInit
// start game 18..23 (the first player is the one whose turn it is). seed 0 = random. false if the data is missing.
bool rm8MiniStart(int game, const std::vector<Rm8MiniPlayer> &players, unsigned seed = 0);
bool rm8MiniActive();				// a game is running (or finished and not yet collected)
bool rm8MiniDone();				// Update returned false: collect the result with rm8MiniFinish
void rm8MiniUpdate(float dt);			// dt in seconds (clamped to 0.1 like the game)
// draw the game (and the UI panels it shows: it calls rm8UiRender itself). Uses cam->frameBuffer's size.
void rm8MiniRender(rw::Camera *cam);
// keyboard: codes as the game's input layer (CApp_OnKeyDown @0x405A90): ASCII lower case letters / digits / space, plus these
enum { RM8MK_ESC = 0x80, RM8MK_UP = 0x93, RM8MK_DOWN = 0x94, RM8MK_LEFT = 0x95, RM8MK_RIGHT = 0x96, RM8MK_ENTER = 0xab };
void rm8MiniKey(int code, bool down);
// pointer in screen pixels. kind 0 down, 1 up, 2 move; id = finger id (-1 mouse); touch = comes from a touch screen
void rm8MiniPointer(int kind, float x, float y, int id = -1, bool touch = false);
void rm8MiniPause(bool paused);
void rm8MiniDrawHeadless();			// run Draw without rendering (games keep logic in Draw; the shot path steps Update + this)
void rm8MiniSetScreen(int w, int h);		// frame buffer size for pointer mapping before the first render
bool rm8MiniCursorHidden();			// the game draws its own cursor sprite: hide the system cursor (host +0x130)
int rm8MiniKeyFromSdl(int sdlKeycode);		// SDL_Keycode -> the codes above (0 = unused key)
bool rm8MiniFinish(Rm8MiniResult &out);		// GetResult + destroy the game (once)
void rm8MiniAbort();				// destroy without a result (app quit)
// the host's reward handling for a finished game; the current player is out.p[0] [V] 0x50E300
std::vector<Rm8MiniReward> rm8MiniRewards(const Rm8MiniResult &out);
// computer players do not run the DLL: MiniGame_ExecuteGame @0x4FCB10. difficulty = init.txt difficuty (FUN_00401060(7)).
// rand = the caller's rand() (MSVC sequence). reward.ticketsDelta > 0 shows textId; game 21 may give hospitalDays (textId 5317).
Rm8MiniReward rm8MiniComputer(int game, int difficulty, int (*rand)());
// [N] N4 netplay: 插海盗 (game 21) is played by every device of a netplay game together (docs/netplay.md section 9.3). The referee in the event task
// (event_mini.cpp, lockstep: the same frames on every device) decides the pirate, every stab's outcome and the computer's / time-out choices from its own
// seeded stream; the game on each device only shows those decisions in order and sends the choices of the local player (the players whose `human` is set
// in rm8MiniStart are the ones played on this device; every other seat waits for the referee's actions).
enum { RM8M21_ROT = 1, RM8M21_STAB = 2, RM8M21_AUTO = 3, RM8M21_DONE = 4, RM8M21_FINAL = 5 };	// the action kinds (RM8C_MINI_ACT b)
struct Rm8Mini21Act {		// a resolved action of the referee
	int turn = 0, kind = 0;	// turn = the stab number 0..; kind RM8M21_ROT (one 90 degree barrel step), RM8M21_STAB (the player's hole) or RM8M21_AUTO (the computer's choice)
	int hole = -1, dir = 0;	// STAB / AUTO: the hole; ROT: the direction (1 = left); AUTO: the direction of the barrel turns that bring the hole into view
	int angle = 180;	// STAB / AUTO: the barrel angle before it (the game snaps to it if it differs)
	int outcome = 0;	// STAB / AUTO: 1 = the pirate is hit
};
struct Rm8Mini21Net {
	bool on = false;
	int defender = 0;					// the pirate (player index) the referee drew
	std::function<void(int kind, int turn, int arg)> send;	// a choice of the local player (ROT: dir, STAB: hole, AUTO: its 7 s ran out)
	std::function<void(int turn)> botDue;			// the game reached a turn it does not play (after the DLL's 0.7 s pause): the host may decide it now
};
void rm8Mini21NetSet(const Rm8Mini21Net &n);	// before rm8MiniStart(21, ...)
void rm8Mini21NetClear();
void rm8Mini21NetPush(const Rm8Mini21Act &a);	// the referee's actions, in order (also those resolved before the game started here)
// standalone test entry (rm8_app --minigame N): own loop / --shot; returns the exit code. Env hooks: see docs/minigames/port.md
struct SDL_Window;
int rm8MiniStandalone(SDL_Window *win, rw::Camera *cam, int game, const std::string &shot, float shotTime, int W, int H, const std::string &scenesRoot);
