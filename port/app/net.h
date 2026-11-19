// [N] Online multiplayer (联机) for the port: host-relayed deterministic lockstep of the human decisions, docs/netplay.md.
//
//   * Every peer (PC or Android) runs the whole game simulation itself at a fixed step (RM8_NET_STEP); the HOST decides at which simulation frame each
//     human decision (a "command": a UI click, the dice button, a card target, a skip ...) is applied and relays the command stream to everybody.
//   * The local input of a human decision goes through rm8NetInput* / the hooks of ui.cpp, cards.cpp and skip.cpp: outside a netplay game they return
//     false and the caller acts at once as before (single player is unchanged); in a netplay game they send the command (or drop it when the local player
//     does not own the seat the game waits for) and the action happens when the command comes back at its frame, on every peer at the same frame.
//   * Desync safety net: a hash of the save record (rm8SaveBuild, byte exact) at every turn start / day wrap; a mismatch makes the host send its last
//     day-wrap snapshot and the commands after it, the client rebuilds the board from it (the normal load path) and replays to the present.
//   * N2: the room is the ORIGINAL map / role menu (menu.cpp mirrors it to the clients), a networked free game can be saved by the host and resumed
//     ("等待玩家中…": the players come back by their device's player id), absent players' seats are played by the computer until they join.
// Socket layer: net_socket.cpp; protocol, session, lockstep: net.cpp; the 联机 pages, the room overlays and the in-game indicator: net_ui.cpp.
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include "menu.h"

namespace rw { struct Camera; }
union SDL_Event;
struct Rm8Scene;

const float RM8_NET_STEP = 1.0f / 60.0f;	// [N] the fixed simulation step of a netplay game (the interactive single player keeps its variable dt)
const int RM8_NET_DEFAULT_PORT = 47000;

// ---------------------------------------------------------------- state (cheap, safe without a session)
bool rm8NetActive();		// a netplay BOARD runs (lockstep): fixed step, commands, deterministic audio waits, session options
bool rm8NetSession();		// connected / listening (room, waiting or board)
bool rm8NetIsHost();
bool rm8NetIsClient();
int rm8NetLocalPeer();		// 0 = the host
bool rm8NetOwnsSeat(int seat);	// the local player plays this seat
int rm8NetSeatOwner(int seat);	// peer id of the seat's player, -1 = nobody (a computer seat / an open seat of a resumed game)
bool rm8NetPeerConnected(int peer);
uint32_t rm8NetFrame();		// simulation frames run in this game

// session options that replace the local option sliders while a netplay game runs (the host's values): -1 = no session
int rm8NetOptDifficulty();
int rm8NetOptPlus();
bool rm8NetStartDate(int &year, int &month, int &mday, int &wday);	// the host's calendar day of the game start (game.cpp); false = use the system clock
bool rm8NetSeed(unsigned &seed);				// the session's game seed (rm8_app.cpp gameSeed)

// ---------------------------------------------------------------- the command layer (docs/netplay.md section 3)
enum Rm8NetCmd {
	RM8C_NONE = 0,
	RM8C_UI_CLICK = 1,	// s = ui name, a = widget key (ui.cpp rm8UiNetKey): the click handler of that widget
	RM8C_UI_HOVER = 2,	// s = ui name, a = widget key (-1 = none): the hover / press highlight that rm8UiHot reports (shop / court descriptions, the card bar)
	RM8C_ROLL = 3,		// the dice key (space / enter): rm8RolesHumanRoll (go.ui's button is a UI_CLICK)
	RM8C_CARD = 4,		// a = cards.cpp action (RM8_CARDNET_*), b = argument: board target pick, card menu open / close, cancel
	RM8C_SKIP = 5,		// the click skip of the running wait (skip.cpp rm8SkipRequest), only from the player the game waits for
	RM8C_FAST = 6,		// the '.' key (host only): rm8SkipToggleFast
	RM8C_AUTOKEY = 7,	// a = seat: F1..F4 / the seat hexagon of an auto-played seat (rm8AutoKey)
	RM8C_AUTOSET = 8,	// a = seat, b = 1 on / 0 off: the host hands an absent player's seat to the computer and back (rm8AutoSet)
	RM8C_AUTOASK = 9,	// gsys button 1: the "auto play?" question (rm8AutoSupplyOpen)
	RM8C_MINI_RESULT = 10,	// [N4] a = game 18..23; b = 0: the result the seat's player played on its own device (s = "<n>|<seat>,<score>,<flagA>,<rating>,<flagB>|...", the
				//      GetResult structure: every device derives the same rewards); b = 1: the computer logic decides (host: the player is away / timed out)
	RM8C_SEAT = 11,		// [N2] host only: a = seat, b = peer, s = "<16 hex player id> <name>": the seat's player changes (a joiner takes an open seat; NETP follows)
	RM8C_ADOPT = 12,	// [N2] host only: a = seat: a computer seat the host offered in a resumed game becomes a player seat played by the computer (rm8AutoAdopt)
	RM8C_MINI_ACT = 13,	// [N4] 插海盗 (game 21) with everybody: a = 21, b = RM8M21_* (minigame.h), s = "<task serial>,<turn>,<arg>": a player's barrel turn / stab (its own
				//      seat's turn), the computer's choice (that player's time-out; the host: a computer seat, an absent / late player), DONE (a device's game
				//      ended), FINAL (host: the result is applied now). The referee of the task (event_mini.cpp) applies them at their frame on every device
	RM8C_COUNT
};
struct Rm8NetCommand { uint32_t frame = 0; int kind = 0, seat = -1, a = 0, b = 0; std::string s; };

// Local input of a human decision. true = netplay took it (sent to the host, or dropped because the local player may not decide now): the caller must
// NOT act. false = no netplay game: act locally as before.
bool rm8NetInput(int kind, int a = 0, int b = 0, const std::string &s = std::string());
int rm8NetInputSeat();		// the seat whose decision the game waits for (state 7 modal, the swap, the event task's bidder, else the current seat); -1 none
bool rm8NetLocalActor();	// the local player owns rm8NetInputSeat() and it is a human seat (not auto-played)
// [N] N4: may the local player's minigame start now? (a client that replays to the host's frame after a reconnect / resync waits: the host decides that result)
bool rm8NetMiniMayStart();

// ---------------------------------------------------------------- application hooks (rm8_app.cpp sets them)
struct Rm8NetAppHooks {
	std::function<void(const Rm8Setup &st)> startBoard;			// build the board of a new netplay game (beginBoard with the session overrides set)
	std::function<bool(const std::vector<unsigned char> &rec)> loadBoard;	// rebuild the board from a record (a resync snapshot, a resumed game); false = failed
	std::function<void(const std::string &why)> leave;			// the session ended under a running board / room: the 联机 page with a notice
	std::function<void()> hostSetup;					// "建立主机": the free game's map menu (the room) opens
	std::function<void()> hostLoad;						// "读取联机存档": the free game's load menu opens (RM8PAGE_NET_LOAD)
	std::function<void()> backToMenu;					// the 联机 page was left: the multi menu page again
	std::function<void()> showMenus;					// a client's mirrored room: the app runs the menus (rm8MenuNetMirror opened the page)
	std::function<void()> showNetPage;					// the 联机 page (net_ui.cpp) instead of the menus
};
Rm8NetAppHooks &rm8NetAppHooks();

// ---------------------------------------------------------------- main loop (rm8_app.cpp)
void rm8NetInit(const std::string &userDataDir, const std::string &gameDir, const std::string &scenesRoot);
void rm8NetPoll();			// socket I/O, heartbeats, connects, the room; call every loop iteration (also in the menus and while paused)
int rm8NetStepsDue(float realDt);	// board: how many simulation steps to run in this loop iteration (the client is limited by the host's confirmed frame)
void rm8NetFrameBegin();		// before a simulation step: the commands of the next frame are applied, test hooks
void rm8NetFrameEnd();			// after it (frame counter, hash bookkeeping)
void rm8NetLeave(const char *why);	// leave / close the session (gsys exit, back to the menu); the host ends the game for everybody
void rm8NetAppPaused(bool paused);	// Android background / foreground: the connections get a grace period after a pause (the loop keeps polling while paused)
// the netplay simulation camera: the default board view (yaw 225, pitch 40, the default zoom) on the turn focus, built from exact constants (no libm),
// so every peer computes the same "camera" for the rules (same-node placement, doll facing, dice direction, card range); the view camera stays free
void rm8NetPlaceSimCamera(rw::Camera *cam, const Rm8Scene &sc, float fx, float fz);
void rm8NetGameEnded();			// the board of a netplay game ended (game over / exit): the session closes

// ---------------------------------------------------------------- the room (host on the original menus, clients mirrored; docs/netplay.md section 5)
bool rm8NetHostListen(int port, std::string &err);	// "建立主机": listen; the map menu opens in room mode (menu.cpp RM8MENU_NET_HOST)
void rm8NetJoin(const std::string &addr);		// "加入游戏": connect to "ip:port" / "host:port"
std::string rm8NetJoinState();				// "" idle, else the connect / handshake state line
bool rm8NetLobbyStart(bool force);			// host: start (force = with the ready players only); false = not everybody is ready (ask first)
void rm8NetRoomReady();					// client: toggle the ready flag (O on the mirrored menus)
void rm8NetRoomPickRole(int charId, int outfit);	// client: ask for a character / outfit of its own seat
void rm8NetChat(const std::string &text);		// a chat line to everybody (room, waiting screen, game)
struct Rm8NetRoomSeat { int kind = 2, owner = -1; bool ready = false, connected = false, me = false; std::string name; };	// kind as Rm8MenuNetSeat
struct Rm8NetRoomPeer { int id = -1, seat = -1; bool ready = false, connected = false, me = false; std::string name; };
struct Rm8NetRoomView { bool valid = false, host = false; int screen = 0; std::string addr; std::vector<Rm8NetRoomSeat> seats; std::vector<Rm8NetRoomPeer> peers; int ready = 0, need = 0, mySeat = -1; bool meReady = false; };
Rm8NetRoomView rm8NetRoomView();			// the room overlay (net_ui.cpp)
// ---- a resumed networked save (docs/netplay.md section 9.1): "等待玩家中…" on the loaded board
bool rm8NetHostResume(const std::string &recordPath, std::string &err);	// listen, load the record as the game's frame 0, wait for the players
bool rm8NetHostResumeArmed();				// "读取联机存档" chose the load menu: its pick is hosted
void rm8NetHostResumeArm(bool on);
enum Rm8NetWaitState { RM8W_LOCAL = 0, RM8W_SAVED, RM8W_OPEN, RM8W_COMPUTER, RM8W_COMPUTER_OPEN };
struct Rm8NetWaitRow { int seat = 0, charId = 0, state = 0, owner = -1; bool present = false, toggle = false; std::string name; };
struct Rm8NetWaitView { bool on = false, host = false; std::vector<Rm8NetWaitRow> rows; int missing = 0; };
Rm8NetWaitView rm8NetWaitView();
void rm8NetWaitToggle(int seat);			// host: 本机 <-> 等待玩家 (a computer seat: 电脑 <-> 等待玩家)
bool rm8NetWaitStart(bool force);			// host: continue the game; false = players are missing (ask first: their seats are played by the computer)

// ---------------------------------------------------------------- screens (net_ui.cpp)
enum Rm8NetUiResult { RM8NET_UI_RUNNING, RM8NET_UI_CLOSED };
void rm8NetUiOpen();			// the 联机 page: host / join / resume / player name
bool rm8NetUiVisible();			// a 联机 page is shown (the app runs it instead of the menus)
bool rm8NetUiEvent(const SDL_Event &e, int w, int h);
Rm8NetUiResult rm8NetUiUpdate(float dt);
void rm8NetUiRender(rw::Camera *cam);
void rm8NetUiHide();			// net.cpp: the board starts / loads, the menus mirror the room: the pages go
std::string rm8NetStatusLine();		// net.cpp: the in-game line (whose decision, remote / absent players, reconnect / resync)
std::string rm8NetUiTakeShot();		// test hook RM8_NET_UI_AUTO shot:<png>: the file the app should write the screen to now ("" none)
std::string rm8NetMiniShotDue();	// [N] N4 test hook RM8_NET_MINI_SHOT=<prefix>[,<s>]: "<prefix>_g<game>_<n>.png" once per minigame, <s> (default 10) s after it began waiting
bool rm8NetTestSetup(Rm8Setup &st, std::vector<int> &kinds);	// test hook RM8_NET_SETUP (net_ui.cpp): the room RM8_NET_HOST opens without clicking through the menus
void rm8NetUiNotice(const std::string &text, float seconds = 4.0f);	// a short message line (pages, room overlay, game)
void rm8NetHudRender(rw::Camera *cam);	// in a netplay game: whose turn, waiting for a remote player, connection / resync state, the waiting screen, chat
void rm8NetUiMenuTick(float dt);	// the room overlay over the original menus (called in the menu loop while a room is open)
bool rm8NetUiMenuEvent(const SDL_Event &e);	// the room overlay's keys / pointer before the menus get them (true = used)
void rm8NetUiRoomStartAsk();		// host: O of the role menu in a room: start, or ask about the force start
uint64_t rm8NetPlayerId();				// this device's persistent player id (net_id.txt in the user data folder)
std::string rm8NetMyName();				// this device's player name (rm8_settings.txt net_name, else "玩家" + 4 hex digits of the id)
struct Rm8NetSavedSeat { int peer = -1; uint64_t playerId = 0; std::string name; };
const std::vector<Rm8NetSavedSeat> &rm8NetSavedSeats();	// the NETP chunk of the last loaded record (who owned which seat)
int rm8NetPort();					// the listen port (rm8_settings.txt net_port, default 47000)
std::string rm8NetLastAddr();				// the last joined address (rm8_settings.txt net_last)
std::string rm8NetSeatName(int seat);			// "<role name>（玩家名）" etc. for the in-game indicator
