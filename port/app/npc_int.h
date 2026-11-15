// Internal declarations shared by npc.cpp (objects, spawn, rendering, possession visuals), npc_act.cpp (interactions, messages,
// obstacles, bombs) and npc_ui.cpp (reel dialog, greeting text, bomb billboard). See docs/npc_port.md.
#pragma once
#include "npc.h"
#include "present.h"
#include "cards.h"
#include "audio.h"
#include "ui.h"
#include <map>
#include <string>
#include <vector>

namespace npc {

// npcinfo.bin record [V] NPC_LoadFromINI @0x49d520: Id +0x204, type +0x200, date +0x228, good +0x22c, sound +0x230, scale +0x21c, artNum +0x208, artId +0x20c..
struct Def { int id = 0, type = 0, date = 0, good = 0, sound = -1; float scale[3] = {1, 1, 1}; int artNum = 0, art[4] = {0, 0, 0, 0}; std::string name; };
struct Art { std::string dff, texdir; std::map<int, std::string> anims; std::vector<std::pair<int, int>> hooks; };	// hooks: (frame "[@n]", particle effect) of the art (role.bin particle table)
struct Mdl {
	rw::Clump *clump = nullptr; rw::HAnimHierarchy *hier = nullptr; rw::Matrix base; rw::V3d p0 = {0, 0, 0};
	std::map<int, rw::Animation*> anims;
	std::vector<int> prt;		// particle emitters on the hook frames (rm8PrtAttachH)
	Rm8CpuSkin cpu;			// skinned parts above the GPU bone limit (phone: 48)
};

// an NPC object (the game's NPC base class, vtable 0x5e2d18 .. 0x5e3628)
struct Obj {
	int id = 0; const Def *def = nullptr;
	int state = 6;		// [V] +0x68: 0 on the map, 1 interacting, 2 waiting for the dialog, 3 robot doll walking, 4 left, 5 to be deleted, 6 not placed
	int node = -1;		// [V] +0x84: the road node it stands on (-1 none)
	int owner = -1;		// [V] +0x70: the seat that created it (Npc.Create)
	int actor = -1;		// [V] +0x88: the seat interacting with it
	int stage = 0;		// [V] +0x8c
	float timer = 0;	// [V] +0x90
	int steps = 0;		// [V] +0x3c (robot doll)
	int param = 0;		// [V] puzzle piece number (+0x94)
	Mdl m[4]; int nm = 0, cur = 0;
	int anim = -1; bool once = false, busy = false;	// current animation; busy = a once animation is still running ([V] +0x5c / +0x17)
	rw::Clump *shadow = nullptr; rw::Matrix shBase;
	rw::V3d pos = {0, 0, 0}; float scaleF = 1.0f;
	// robot doll walk (Path_AnimateAlongRoute) / dog flight
	int from = -1, to = -1; rw::V3d dir = {0, 0, 0}; float t = 0;
	int fx = 0;		// effect handle (the god's cloud 6122, the egg sprite)
	int snd = 0;		// the sample of the god's show ([V] Anim +0x50: the entrance sound, then the greeting voice)
	bool collectPending = false;	// [V] +0x7c: the god waits for the reel dialog (not drawn)
	// turn animation of the facing models ([V] Path_CalcAngle @0x47bfa0 with param_3 = 1: the figure spins 540 degrees / s to the next model)
	float yaw = 0; bool spinning = false; int spinTarget = 0; float spinAngle = 0, spinRate = 0;
};

// per seat state kept outside the role ([V] role +0x1DC possession days, +0x1C0 / +0x1E0 road bully, +0x288 time bomb counter)
struct Seat { int possDays = 0, bullyBy = -1, bullyDays = 0, bomb = 0; bool hidden = false; };	// hidden = role +0x1C8 cleared: the role is not drawn (CPlayerTag__Render @0x485d00 needs CanMoveNormally @0x4932e0)

extern std::map<int, Def> g_defs;
extern std::vector<Obj*> g_objs;		// the NPC array: the handle of an object is its index + 1 (the game's py_Npc_* number + 1)
extern Rm8Scene *g_s;
extern rw::Camera *g_cam;
extern float g_camYaw;
extern float g_clock;	// seconds since the game started (debug logs)

// ---- npc.cpp
Obj *byHandle(int h);
int handleOf(const Obj *o);
Obj *at(int node);				// the NPC standing on a road node (node +0x10) or nullptr
Obj *createObj(int id);			// [V] Npc_Factory @0x49e6c0 + vf+0x58 (state 6)
bool onLand(Obj &o, int node);		// [V] vf+0x64 Cls_5e2b50__OnLand @0x49b570
void finalize(Obj &o);			// [V] vf+0x68 Cls_5e2c80__Finalize @0x49b6c0 (state 4; 5 for the classes with Cls_5e2db0__Finalize @0x49cd20)
void startAnim(Obj &o, int anim, bool once);
bool animDone(const Obj &o);
void placeObj(Obj &o);
int findFreeNode();			// [V] TerraMgr_FindAvailableNPC @0x4f2610 (-1 none)
Seat &S(int seat);
int rnd();				// the rules RNG (rm8GameRand)
float waitScale();			// [V] Anim_ApplyDifficultyScale @0x47b3b0: 1 - min((speed - 1) / 10, 1)
rw::V3d nodeP(int node);
void facing(Obj &o, rw::V3d a, rw::V3d b, bool spin = false);	// [V] Path_CalcAngle @0x47bfa0 for the facing models
bool spinUpdate(Obj &o, float dt);	// [V] Path_CalcAngle's spin animation (the same as roles.cpp spinUpdate)
bool loadMdl(Mdl &m, int art);		// a model of npcs.txt (also the police car / ambulance of the transfer show)
void freeMdl(Mdl &m);
bool stepAnim(Mdl &m, float dt, bool once);
void setAnim(Mdl &m, int anim, bool once);
void placeMdl(Mdl &m, rw::V3d pos, rw::V3d sc, float yaw, rw::V3d off);
void possessedCleanup();
void buildNodeInfo();			// event icons / exit roads of the board (findFreeNode)
void spamEnable(int n);			// test hook RM8_NPC_SPAM
void actUpdate(Obj &o, float dt);	// npc_act.cpp: the per-class state machines

// ---- npc_act.cpp
int interact(int seat, Obj &o);		// the class's OnRoleClick (vf+0x74): 0 not handled, 1 = role state 10, 2 / 3 handled
void transferRender();			// the police car / ambulance of the prison / hospital show
void transferUpdate(float dt);
void actClear();
void actNewGame();
void actUpdateGlobal(float dt);	// reel dialog, running tasks, ...
bool collectConfirm(Obj &o);		// [V] Cls_5e3588__OnConfirm @0x49bf50 / Cls_5e34e8__OnConfirm @0x49c120
void bombRender(rw::Camera *cam);	// npc_ui.cpp

// ---- npc_ui.cpp
void greetingShow(const std::string &utf8);
void greetingHide();
// the reel dialog (CUi_scrbar, Lottery_InitScrollBar @0x49f810): done(amount) when the numbers have been shown
void reelOpen(const std::string &title, bool human, void (*done)(void *, int), void *ctx);
bool reelPress();			// a key / click stops the reels (human)
bool reelActive();
void reelUpdate(float dt);
void reelClear();

}	// namespace npc
