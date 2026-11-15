// NPCs on the board and possession (npc.cpp, npc_act.cpp, npc_ui.cpp), after docs/npc_port.md.
// npcinfo.bin ids — 1 財神 2 窮神 3 衰神 4 福神 5 土地公 6 糊塗神 7 警察 8 小偷 9 野狗 (type 0 gods / 1 board characters: placed on the map,
// a role stopping on one meets it), 10 <npc#10> 11 寶箱 (board pickups), placed obstacles 12 路障 13 地雷 14 <card#17> 15 <str#515> (type 2),
// 16 路霸 (a possession-like card effect without a figure), 17 拼圖 (story mode puzzle piece), 23 踩蛋, 100.. (docs/config_files.md, docs/cards.md).
// Shared by cards (Npc.Create / Role.Become / Role.Seal), the rules (game.cpp) and the turn flow (roles.cpp).
#pragma once
#include "turn.h"

// ---- API of the Python modules (python.cpp / cards.cpp): handle = the NPC array index + 1
int rm8NpcCreate(int npcId, int node, int ownerSeat);	// [V] py_Npc_Create @0x500160 / Npc_CreateInstance @0x49f0e0: handle (> 0) or 0
bool rm8NpcRemove(int handle);				// [V] py_Npc_Disappear @0x5003b0: Finalize + sample 0x23
int rm8NpcAt(int node);					// [V] py_StPoint_Npc @0x501160: the NPC on a road node (handle) or 0
const char *rm8NpcName(int handle);			// [A] npcinfo.bin name (the board info box, infoscreens.cpp: NPC +0x64)
int rm8NpcId(int handle);				// [V] py_Npc_ID @0x500340: npcinfo id of a handle or 0
// [V] possession, role +0x1BC (Role.Shape / Role.Become = message 0x24 -> Role_SetPossession @0x48b6e0): 0 none, else npcinfo id 1..9, 100..103;
// 16 = the road bully effect (not a shape). days 0 = npcinfo date.
int rm8NpcShape(int seat);
bool rm8NpcBecome(int seat, int shape, int days);

// ---- enumeration for the card target selection (kind 0x20: Role.Seal, NPC ids < 10, Game_FindNearestNpc @0x49f000)
int rm8NpcCount();					// number of NPC objects (handles 1..count)
bool rm8NpcOnMap(int handle);				// [V] u_Unk_IsReady @0x49f1f0: placed (state 0)
int rm8NpcNode(int handle);				// road node it stands on, -1 none

// ---- data / lifecycle (roles.cpp / game.cpp)
bool rm8NpcLoadDefs(const std::string &npcsTxt);	// scenes/npcs.txt (scripts/export_npcs.py); called with the roles.txt load
void rm8NpcSetCamera(rw::Camera *cam);			// the camera yaw for the paper dolls (rm8RolesSetCamera)
void rm8NpcNewGame(Rm8Scene &s);			// [V] Npc_SpawnAll @0x49e2d0 (Game_InitGame @0x4a1090): all NPC objects, then the first Event_OnNpcEnable
void rm8NpcClear(Rm8Scene &s);				// rm8RolesClear
void rm8NpcUpdate(float dt);				// per frame (rm8RolesUpdate)
void rm8NpcRender();					// inside rm8RolesRender (alpha test state set)
void rm8NpcRenderLate(rw::Camera *cam);			// time bomb counters / reel dialog helpers (rm8RolesRenderLate)
void rm8NpcMonthStart();				// [V] Game_OnTurnStart @0x4a3b50: day of month 1 -> Event_OnNpcEnable @0x49d9c0
void rm8NpcStory(const int puzzles[2]);			// [V] Event_ProcessLanding @0x49db20: story mode puzzle pieces (pass_XX start_pic) spawned by the next rm8NpcNewGame; null = none
void rm8NpcClearPossessed(int seat);			// [V] CPlayerTag__ClearPossessedGod @0x48edc0 (news, Thanksgiving): the bad gods leave at the next turn start, the time bomb is cleared

// ---- turn flow hooks (roles.cpp)
int rm8NpcInteract(int seat, int node);			// [V] NPC_HandleInteract @0x49dfb0 (message 0x23): 1 = the role waits (state 10), 0 / 2 / 3 continue
// [V] u_CPlayerTag_UpdateTurnCountdown @0x485a20 (vf18, a node was passed with steps left): time bomb, roadblock, possessed police / thief /
// dog. 0 = go on, 1 = stand and stop here (queue prepared), 2 = the roadblock: cancel the steps, OnStopAtTerra
int rm8NpcPass(int seat, int node);
bool rm8NpcOnStop(int seat);				// [V] CPlayerTag__OnStopAtTerra @0x485c00: bomb counter / possessed police-thief-dog; true = skip 0x23 / 0x1D
void rm8NpcTurnStep(int seat);				// [V] Role_TickPossessionExpire @0x483350 (turn start step 6)
bool rm8NpcSleepwalk(int seat);				// [V] Role_ShowSleepwalkAndRoll @0x48b140 for shape 6 (turn start step 9 / Role_BeforeRollDice): true = handled
bool rm8NpcMessage(int seat, int cmd, int a, int b);	// 0x23 0x24? 0x32 0x3A 0x3C 0x51 0x59 0x60 0x81 (called from rm8GameMessage)
bool rm8NpcSkip();					// [V] NPC vf+0x60 @0x49BAD0 (gods): cut the arrival show short (scene vf13, skip.cpp); true = a god was skipped
bool rm8NpcPress();					// a key / click for the reel dialog (human): true if consumed

// ---- hospital / prison transfer show (message 0x18 / 0x19 / 0xC9, cards.cpp)
// [V] NPC_CreateRandom_Type2 @0x4a45f0 (ambulance, art 0x527 = Mys119, hospital) / Type1 @0x4a4510 (police car, art 0x526 = Mys118, prison): the vehicle
// drives to the role, picks it up (the role is hidden at the first time, Role_SetFlag1C8 cleared by RoleAnimation_TriggerAction @0x4b1470 -> vf+0x6c @0x48ceb0)
// and at the second time the role is moved (RoleAnimation_Start @0x4b11d0 / Cls_5e3af0__vf18 @0x4b1500: role vf+0x74 SendToPrison @0x48a9c0 / Cls_5e3b90__vf18
// @0x4b15d0: vf+0x70 SendToHospital @0x48a570). kind 1 = prison, 2 = hospital. commit(seat, kind, days) does the move when the show is over.
// Returns false when there is no show (the caller commits at once).
bool rm8NpcTransfer(int seat, int kind, int days, void (*commit)(int seat, int kind, int days));
// the missile / nuke cards (cards.cpp): CPlayerTag__OnHolidayCardUse @0x48de10 + the 2.0 s task Cls_5e6a78 @0x4f4780: the blast at `node` (art 0x1777 / 0x1778, sample 0x12 / 0x13,
// radius 1 / 3, hospital 3 / 5 days, type 0 missile = the plots in the area lose their buildings, type 1 nuke = they also lose their owner)
void rm8NpcBlast(int actor, int node, int art, int snd, int radius, int baseDays, int type);
bool rm8NpcRoleHidden(int seat);				// [V] role +0x1C8 == 0 (NPC_HandleInteract state 10, the transfer show): not drawn
void rm8NpcSetRoleHidden(int seat, bool hidden);	// the UFO news hides / shows the role (+0x1C8)
bool rm8NpcTransferSkip();	// [V] RoleAnimation_SkipToEnd @0x4B1320 (vtable 0x5E3AF0 family vf13): the police car / ambulance show jumps to its end (skip.cpp)

// ---- rules (game.cpp)
int rm8NpcBully(int ownerSeat);				// [V] Role_PayToll @0x487c80: owner role +0x1C0 = the seat that played the road bully card (-1 none)
int rm8NpcTimeBomb(int seat);				// [V] +0x288 time bomb counter (Role.TimeBomb, 0 none)

// ---- possessed role figures (roles.cpp): while a role is possessed by 1..9 its models are the NPC's ([V] Role_SetPossession @0x48b6e0)
bool rm8NpcRoleShape(int seat, float scale[3]);		// [V] CPlayerTag__GetStatusShape @0x490bf0: the NPC's scale instead of roleinfo scale
void rm8NpcRoleUpdate(int seat, const float pos[3], int facing, int anim, bool once, float yaw, float tw, const float off[3], float dt);
bool rm8NpcRoleRender(int seat);			// draws the NPC figure instead of the role's own models; true = drawn
