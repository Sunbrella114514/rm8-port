// Save / load of the board game, after docs/save_format.md (the original's record_*.sav, version 9) and docs/save_port.md.
// [V] = verified in the decompile (address), [H] hypothesis, [A] port adaptation.
//
// The file is the ORIGINAL record (header 0x64, setup, sflb, stocks, roles, terra, streets, npcs, ticket center, power company, lottery, wonder table;
// Game_WriteRecordToFile @0x4a7540 / Save_LoadRecord @0x4a78f0) so that the original's own saves load here and these load there, followed by ONE
// port extension block ("RM8X": tagged, length-prefixed chunks with the state the original record has no place for: RNG streams, idle timers,
// NPC details ...). The original's reader stops after the wonder table, so the extra bytes are ignored there; without the block the port starts
// the missing state from defaults.
//
// Every module serialises its own state in its own .cpp. Three kinds of hooks, all declared here:
//   1. original-format sections (rm8Save<Module>Write / Read): the byte layout is fixed by the original;
//   2. the role record (rm8Save<Module>RoleGet / RoleSet): each module fills / applies the fields of Rm8SaveRole that it owns;
//   3. extension chunks (RM8_SAVE_EXT): free format, versioned, tolerant: a module owner adds new fields at the END of its chunk (the reader keeps the
//      default of a field that is not in the file) and bumps nothing; see docs/save_port.md "adding state".
#pragma once
#include "turn.h"
#include "menu.h"
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// ---------------------------------------------------------------- byte buffers (little endian, like the original's RwStreamWrite / Read)
struct Rm8SaveW {
	std::vector<unsigned char> b;
	void raw(const void *p, size_t n){ const unsigned char *c = (const unsigned char*)p; b.insert(b.end(), c, c + n); }
	void i32(int v){ raw(&v, 4); }
	void u32(unsigned v){ raw(&v, 4); }
	void f32(float v){ raw(&v, 4); }
	void u8(int v){ unsigned char c = (unsigned char)v; raw(&c, 1); }
	void str(const std::string &s){ i32((int)s.size()); raw(s.data(), s.size()); }
	void zeros(size_t n){ b.insert(b.end(), n, 0); }
};
struct Rm8SaveR {
	const unsigned char *p = nullptr; size_t n = 0, pos = 0; bool bad = false;
	Rm8SaveR(){}
	Rm8SaveR(const unsigned char *d, size_t len) : p(d), n(len) {}
	size_t left() const { return n - pos; }
	bool raw(void *dst, size_t k){ if(bad || k > left()){ bad = true; if(dst) memset(dst, 0, k); return false; } if(dst) memcpy(dst, p + pos, k); pos += k; return true; }
	void skip(size_t k){ raw(nullptr, k); }
	int i32(){ int v = 0; raw(&v, 4); return v; }
	unsigned u32(){ unsigned v = 0; raw(&v, 4); return v; }
	float f32(){ float v = 0; raw(&v, 4); return v; }
	int u8(){ unsigned char c = 0; raw(&c, 1); return c; }
	std::string str(){ int k = i32(); std::string s; if(k < 0 || (size_t)k > left()){ bad = true; return s; } s.assign((const char*)p + pos, k); pos += k; return s; }
	// optional trailing field of an extension chunk: false (and `v` untouched) when the chunk ends before it
	bool opt(int &v){ if(left() < 4) return false; v = i32(); return true; }
	bool opt(float &v){ if(left() < 4) return false; v = f32(); return true; }
};

// days since 1970-01-01 of a calendar date (the loan record keeps its date as year / month / day / weekday / day count)
inline int32_t rm8SaveCivilDays(int y, int m, int d)	// [N] int32_t
{
	y -= m <= 2;
	int32_t era = (y >= 0 ? y : y - 399) / 400;	// [N] int32_t
	unsigned yoe = (unsigned)(y - era * 400), doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int32_t)doe - 719468;	// [N] int32_t
}

// ---------------------------------------------------------------- the role record (docs/save_format.md section 6, Role_Save @0x491fb0)
struct Rm8SaveMsg { int cmd = 0, a = 0, b = 0; };
struct Rm8SaveHold { int stock = -1, qty = 0; float last = 0, cost = 0; };	// stock = index in the listed array [V] Stock_SaveRole @0x4997e0
struct Rm8SaveRole {
	int p40 = -1, p44 = -1;			// +0x40 / +0x44 road nodes (index in TerraMgr+0 = Rm8Scene::rnodes: 22 sample roles, every pair adjacent) [V]
	int ai = 0, status = 0, state = 0;	// +0x1B0 (0 human), +0x1B4 (7 bankrupt), +0x1B8 turn state
	int shape = 0, bullyBy = -1;		// +0x1BC possession, +0x1C0 road bully beneficiary
	int turtle = 0, stay = 0;		// +0x1CC, +0x1D0
	int statusDays = 0, possDays = 0, bullyDays = 0, invisible = 0;	// +0x1D8 (sleep / hospital / prison days), +0x1DC possession days, +0x1E0 bully days, +0x1E4 invisible turns
	std::vector<Rm8SaveMsg> qa, qb;		// +0x5D0 / +0x5E0 message queues
	int vehicle = 1, dice = 1;		// +0x22C, +0x230
	int tickets = 0, cash = 0, deposit = 0;	// +0x324, +0x290, +0x294
	int loan[7] = {0, 0, 0, 0, 0, 0, 0};	// +0x298.. principal, year, month, day, weekday, day count, (interest payments << 16 | 3)
	int cards[8] = {-1, -1, -1, -1, -1, -1, -1, -1};	// +0x2B4 slots, -1 empty
	std::vector<Rm8SaveHold> holds;		// +0x328
	int rent[8] = {0, 0, 0, 0, 0, 0, 0, 0};	// +0x338: rent received, rent paid, ... [H from the samples: role 2 pays 400, role 1 receives 400]
	int bomb = 0, bubble = 0;		// +0x288 / +0x28C object counters ("time bomb" / "bubble")
	int f374[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};	// +0x374 story flags (park visits +0x378, ...)
	int used[64];				// +0x4C0 used-card flags
	int f5c0[3] = {0, 0, 0};		// +0x5C0
	int f5cc = 0;
	int sarah = 0;				// role 16 only: *(+0x5F0)+0x98
	Rm8SaveRole(){ memset(used, 0, sizeof used); }
};

// ---------------------------------------------------------------- hooks the modules implement (each in its own .cpp)
// game.cpp: header ints (date, days, end date, price base, price index), terra / wonder section, street section, per-role money; the rng
void rm8SaveGameWriteHeader(Rm8SaveW &w);			// g_GameState +4..+0x30 (12 ints)
bool rm8SaveGameReadHeader(Rm8SaveR &r, int timeLimit);
void rm8SaveGameWriteTerra(Rm8SaveW &w);			// terra records (+ the wonder records through rm8SaveHooks())
bool rm8SaveGameReadTerra(Rm8SaveR &r);
void rm8SaveGameWriteStreets(Rm8SaveW &w);			// streets 1..136
bool rm8SaveGameReadStreets(Rm8SaveR &r);
void rm8SaveGameRoleGet(int seat, Rm8SaveRole &r);		// tickets, cash, deposit, shape, invisible turns, rent counters
void rm8SaveGameRoleSet(int seat, const Rm8SaveRole &r);
Rm8GameOptions rm8SaveGameOptions();				// the start options of this game (setup block +0x0C..+0x1C)
// roles.cpp: positions, states, queues, vehicle
void rm8SaveRolesRoleGet(int seat, Rm8SaveRole &r);
void rm8SaveRolesRoleSet(int seat, const Rm8SaveRole &r);
int rm8SaveRolesCurrentRaw();					// RoleMgr +0x2C (can be the seat count at the day wrap)
int rm8SaveRolesOutfit(int seat);				// +0x1AC (setup seat +4)
// after every role record was applied (Game_LoadSavedGame @0x496a90): the same-node placement of the roles, then the turn flow resumes: current >= the seat count
// = the day wrap (the turn passes to the next day: Game_TurnNextRole), else that seat keeps its turn with the saved state
bool rm8SaveRolesFinish(int current);
const char *rm8SaveGameMapDir();				// the scene folder of the running map ("03", "download201")
void rm8SaveGameNewGame();					// rm8GameNewGame: forget the start date of a loaded record
int rm8SaveWonderCount();					// the number of wonder records of the running map
int rm8SaveTerraPort(int k);					// the port's plot index of the k-th plot of the record (the original's Terra array order, scenes/save_tables.txt)
// the stock names are Big5 in the record, UTF-8 in the port (stock.cpp)
std::string rm8SaveBig5FromUtf8(const std::string &s);
std::string rm8SaveUtf8FromBig5(const char *p, size_t n);
// python.cpp: the state of Python's random module (the AI scripts' dice), and a seed for reproducible tests (RM8_SEED)
void rm8SavePyRandomWrite(Rm8SaveW &w);
void rm8SavePyRandomRead(Rm8SaveR &r);
void rm8SavePySeed(unsigned seed);
// stock.cpp: the 22 stock objects + the holdings of a role
void rm8SaveStockWrite(Rm8SaveW &w);
bool rm8SaveStockRead(Rm8SaveR &r);
void rm8SaveStockRoleGet(int seat, Rm8SaveRole &r);
void rm8SaveStockRoleSet(int seat, const Rm8SaveRole &r);
// cards.cpp
void rm8SaveCardsRoleGet(int seat, Rm8SaveRole &r);
void rm8SaveCardsRoleSet(int seat, const Rm8SaveRole &r);
// npc.cpp: the NPC objects (section) and the per-seat possession / bully / bomb fields
void rm8SaveNpcWrite(Rm8SaveW &w);
bool rm8SaveNpcRead(Rm8SaveR &r);
void rm8SaveNpcRoleGet(int seat, Rm8SaveRole &r);
void rm8SaveNpcRoleSet(int seat, const Rm8SaveRole &r);
// events: loans (role record), ticket center / power company (4 floats + chairman seat each), lottery (pool + numbers per seat)
void rm8SaveEventsRoleGet(int seat, Rm8SaveRole &r);
void rm8SaveEventsRoleSet(int seat, const Rm8SaveRole &r);
void rm8SaveCompanyWrite(Rm8SaveW &w, bool power);
bool rm8SaveCompanyRead(Rm8SaveR &r, bool power);
void rm8SaveLotteryWrite(Rm8SaveW &w);
bool rm8SaveLotteryRead(Rm8SaveR &r);
// story.cpp: the story flags of the hero (role record) and the story run
void rm8SaveStoryRoleGet(int seat, Rm8SaveRole &r);
void rm8SaveStoryRoleSet(int seat, const Rm8SaveRole &r);
void rm8SaveStoryLoadSetup(bool story, int level);		// before rm8StoryBegin of a loaded board: the confirmed story setup without the menus

// parts of the original record owned by modules that may come later (wonders, research institutes): set the hooks, the default is "nothing built"
struct Rm8SaveHooks {
	int (*wonderCount)(int mapId) = nullptr;			// optional: the number of wonder records of the map (default: the table in save.cpp, derived from the map file)
	void (*wonderGet)(int w, int out[3]) = nullptr;		// stage (Wonder +0x3C), owner seat (+0x44, -1 none), invested (+0x48)
	void (*wonderSet)(int w, const int in[3]) = nullptr;
	bool (*instituteGet)(int terra, int out[2]) = nullptr;	// [V] building type 2 (research institute) level 1..5: Building +0x14 (2 ints, the research state)
	void (*instituteSet)(int terra, const int in[2]) = nullptr;
	void (*wonderTable)(int idx, int out[9]) = nullptr;	// optional: the 3 x 0x24 byte wonder table (default from the samples)
	void (*wonderTableSet)(int idx, const int in[9]) = nullptr;
	// [N] netplay (net.cpp): every rm8SaveBoundary call (0 day wrap, 1 human dice wait, 2 turn start, 3 roll) first goes here: the state hash / the resync snapshot
	void (*netBoundary)(int kind) = nullptr;
	bool noAutoSave = false;	// [N] a netplay client: the host's game is not written into this device's record_temp / slot 1
};

Rm8SaveHooks &rm8SaveHooks();

// ---------------------------------------------------------------- extension chunks
// tag = 4 characters; `order` = application order on load (lower first). write: everything the module needs; read: a chunk that may be shorter
// (older file) or longer (newer file) than the module knows. afterLoad (optional) runs once all chunks are read (rebuild caches / visuals).
void rm8SaveRegisterExt(const char *tag, int order, void (*write)(Rm8SaveW &), void (*read)(Rm8SaveR &), void (*afterLoad)() = nullptr);
struct Rm8SaveExtReg { Rm8SaveExtReg(const char *tag, int order, void (*write)(Rm8SaveW &), void (*read)(Rm8SaveR &), void (*afterLoad)() = nullptr){ rm8SaveRegisterExt(tag, order, write, read, afterLoad); } };
#define RM8_SAVE_EXT(tag, order, write, read) static Rm8SaveExtReg rm8SaveExtReg_##tag(#tag, order, write, read)
#define RM8_SAVE_EXT_AFTER(tag, order, write, read, after) static Rm8SaveExtReg rm8SaveExtReg_##tag(#tag, order, write, read, after)

// ---------------------------------------------------------------- save module (save.cpp)
// data folder: <dir>/save/ ([A] the game folder is never written); dir = the parent of the scenes root (next to rm8_settings.txt), RM8_DATA_DIR overrides
void rm8SaveInit(const std::string &dataDir, const std::string &scenesRoot, const std::string &gameDir);
std::string rm8SaveDir();
std::string rm8SaveScenesDir();				// the scenes root (scenes/<map>/wonders.txt ...)
std::string rm8SaveSlotPath(int slot, bool multi);		// [V] save\record_NN.sav (story) / save\record_mNN.sav (free game); slot 0 = record_temp.sav
// header of a slot for the load menu (Save_ReadSlotHeader @0x4062a0); false = empty / not a record
bool rm8SaveSlotInfo(int slot, bool multi, Rm8SaveSlotInfo &out);
std::string rm8SaveSlotLabel(int slot, bool multi);		// the line of the save / load dialog: "<name>  M月D日H時M分" or "空" (string 7101 / 7102)

// what the file says about the board that has to be built before the state is applied
struct Rm8SaveInfo {
	Rm8Setup setup;			// story flag, map, start options, seats (char Id, outfit, computer)
	int storyLevel = 0;		// setup +0x08
	int current = 0;		// RoleMgr +0x2C: the seat whose turn it was (== the seat count: saved at the day wrap)
	std::string mapDir;		// scene folder of the map ("" when this build has no scene for it)
	bool original = false;		// no port extension block: a record of the original game
	int terra = 0, wonders = 0;	// record counts the file must have
};
bool rm8SaveParse(const std::string &path, Rm8SaveInfo &info, std::string &err);	// reads + checks the file (kept in memory for rm8SaveApply)
// applies the parsed file to the board the app has just built from info.setup (new game state, scene, roles spawned); resumes the turn flow
bool rm8SaveApply(std::string &err);

// writing: the live game -> bytes / file. false = nothing to save (no board)
bool rm8SaveBuild(std::vector<unsigned char> &out, int currentOverride = -1, bool zeroTime = false);
bool rm8SaveWriteFile(const std::string &path);
// a manual save of the board that is on: written at once when the turn flow is at a quiet point (the human waits to roll, or the day changes), else at the
// next one ([A] the original only offers it with the human waiting: gmaindlg button 10 is shown in state 6 only). Returns true = written now.
bool rm8SaveRequest(int slot, bool multi);
bool rm8SavePending();
// called by the turn flow (roles.cpp): kind 0 = the day wraps (every seat idle, before the new day starts: Game_TurnNextRole @0x4971a0 -> Game_SwitchRecordSlot
// @0x4a7380 auto save), kind 1 = a human waits for the dice (state 6). Writes the auto save / pending manual saves / the RM8_SAVE_DUMP files.
void rm8SaveBoundary(int kind);
// the board ends or restarts: Game_Cleanup @0x4a1340 deletes record_temp.sav
void rm8SaveBoardEnded();
bool rm8SaveBoardOn();			// a game with road nodes and seats is running (a save makes sense)
bool rm8SaveMulti();			// the running game is a free game (record_mNN) or a story game (record_NN)
// load requests from the menus (the board cannot be rebuilt inside a UI callback): the app polls them once per frame
void rm8SaveRequestLoad(const std::string &path);
bool rm8SaveTakeLoad(std::string &path);

// ---------------------------------------------------------------- save / load dialog (save_ui.cpp): saveload.ui (+ cover.ui for the overwrite question)
// [V] gsys buttons 2 / 3 -> CUi_saveload (Cls_5e4428, mode save / load), CSaveUI__vf12_HandleSaveSlot @0x4ac2a0, Game_DisplaySaveSlotList @0x4ac420
void rm8SaveUiOpen(bool saveMode);
void rm8SaveUiClose();
bool rm8SaveUiVisible();
void rm8SaveUiTick();			// per frame while a board runs
bool rm8SaveUiAuto(const std::string &token);	// RM8_MENU_AUTO tokens "savedlg" "loaddlg" "slot:N" "cover:ok" "cover:cancel" "dlgclose" (menu.cpp forwards them): true if handled
