// Story mode (<str#6452> / 闖關模式), after docs/story_mode.md. [V] = verified in the decompile (address), [M] read but meaning inferred, [H] hypothesis,
// [A] port adaptation.
//
// This module is the runtime of the original's story rules: the pass_XX level tables (Config_LoadPassConfig @0x4950e0, exported to scenes/story.txt by
// scripts/export_story.py), the "story run" object &DAT_006405d0 (level + storage box, collect.h), the candidate-map / confirm rules of the map menu
// (Game_PopulateMapList @0x503510, OnConfirm @0x503e90), the start values of a stage (Game_StartNewRound @0x4962a0), and the win / lose message chain
// 0x75 -> 0x7e -> 0x7d -> 0x7a and 0x74 -> 0x76 -> 0x77 (game state 2 / 3 / 4 / 5 of CGamePlayScene__Game_OnUpdate @0x4a17b0).
// The free game uses the same chain (Singleton +0 == 0 skips the story parts), so rm8StoryMessage also serves it.
#pragma once
#include "turn.h"
#include "menu.h"
#include <functional>

struct Rm8PassEntry {			// [V] pass_XX.bin section = 0x50 bytes (Config_LoadPassConfig @0x4950e0)
	int role = 0;			// the hero (file number)
	int stage = 0;			// +0x00 map id
	int level = 0;			// +0x04 layer 0..3 (= setup+8)
	int opp[3] = {0, 0, 0};		// +0x08.. opponents: 0 none, 1..16 role, < 0 random 1..15
	int startCard[2] = {0, 0};	// +0x14
	int startCash = 0, startFund = 0, startTicket = 0;	// +0x1C / +0x20 / +0x24
	int startPic[2] = {0, 0};	// +0x28 puzzle pieces lying on the map
	int rewardCash = 0, rewardTicket = 0;		// +0x30 / +0x34
	int rewardCard[4] = {0, 0, 0, 0};		// +0x38
	int rewardPic[2] = {0, 0};	// +0x48
};

// ---- data (scenes/story.txt)
bool rm8StoryLoad(const std::string &scenesRoot);
bool rm8StoryLoaded();
const std::vector<Rm8PassEntry> &rm8StoryPass(int role);		// the hero's entries in file order
const Rm8PassEntry *rm8StoryFindPass(int role, int stage);		// [V] Container_GetSubElement @0x495780(role - 1, stage)
// movie.bin scripts of a hero: kind 0 = start (da17_1 + daXX_1), 1 = ending (da17_2, da17_3, ...) [V] Movie_LoadConfig @0x5152a0
std::vector<std::string> rm8StoryMovieScripts(int role, int kind);
struct Rm8Drama { std::vector<std::pair<int, std::string>> lines; };	// speech\drama.bin: (rlId, text)
const Rm8Drama *rm8StoryDrama(int id);
const std::vector<std::pair<int, int>> &rm8StoryMissions(int role, int level);	// (piece, condition) [V] Role_CheckMissionsByCharacter @0x4835e0

// ---- menu side (docs/story_mode.md 1.3). The map menu keeps the selected map in Rm8Setup::map (setup+4); the layer (setup+8) lives here.
int rm8StoryRunLevel();				// [V] run.level: the layer reached in this run (Rm8Box::stage)
int rm8StorySetupLevel();			// [V] setup+8: the layer of the confirmed pass entry
// [V] chsRoleMenu click on a role (@0x5065f0): Game_ClearBinaryData (box + run level cleared)
void rm8StoryNewRole(int role);
// [V] Game_PopulateMapList story branch @0x503510: the map ids of the current layer for the hero (up to 4, file order); an empty layer advances
// to the next one with Game_UpdateRecordStats (run.level and the profile stage are raised). Returns the count.
int rm8StoryCandidates(int role, int out[4]);
// [V] chsMapMenu button 0xC (OnBack @0x5042a0): false once a layer was cleared in this run
bool rm8StoryCanGoBack();
// [V] u_Obj_ApplyPendingChanges @0x4ac020 (back to the role menu with level 0): the carried items return to the box
void rm8StoryMenuBack();
// [V] OnConfirm @0x503e90: needs setup.map > 0; keeps the hero only, finds the pass entry (stage == setup.map), appends the opponents as computer
// seats (random for a negative role) and stores the layer. false = nothing to confirm (no map / no entry).
bool rm8StoryOnConfirm(Rm8Setup &setup);

// ---- board side
// Called by the app before the board of a menu game is loaded (setup.story true = story mode, false = free game): records the mode and, in
// story mode, hands the pass entry's start puzzle pieces (the ones the profile does not own) to the NPC spawner.
void rm8StoryBegin(const Rm8Setup &setup);
// [V] Game_StartNewRound @0x4962a0 story part, after the roles exist: carried cards, carried + start cash / deposit / tickets, start cards of the
// hero's pass entry; the carried side of the box is cleared whether or not the entry exists (so a restart does not give it twice).
void rm8StoryApplyStart();
bool rm8StoryActive();				// a story board game is on (setup+0 != 0)
bool rm8StoryConfirmed();			// rm8StoryOnConfirm prepared the setup (opponents from the pass entry): a story game proper, not the free game with the single-menu flag
int rm8StoryHero();				// seat 0's role Id of the story that is on
std::string rm8StoryMapDir(int mapId);		// scene folder of a stage id (scenes/stages.txt), "" if unknown; works without the menus
bool rm8StoryOnBoard();				// rm8StoryBegin was called and the board has not ended (free or story)
// the queued role messages of the win / lose chain: 0x74 0x75 0x76 0x77 0x7a 0x7b 0x7c 0x7d 0x7e; true if handled (called from rm8GameMessage)
bool rm8StoryMessage(int seat, int cmd, int a, int b);
// [V] Role_QueueNpcDialog @0x48c700: queues the lines of drama.bin entry `id` into the seat's queue from position `pos`; returns the next position
int rm8StoryQueueDrama(int seat, int id, int pos);
// ---- story rule exceptions (docs/story_mode.md 1.11 / 1.12); all of them act on the hero (seat 0) of a story board only
void rm8StoryTurnStart(int seat);		// [V] turn start step 2: Role_RunStoryScriptEvents @0x483f40 + Role_CheckMissionsByCharacter @0x4835e0
bool rm8StoryRunScriptEvents(int seat);
bool rm8StoryMissionCheck(int seat);		// [V] true = a puzzle piece (message 0x58) was queued (Role_BeforeRollDice then queues 100 to retry)
void rm8StoryNoteSteps(int seat, int steps);	// [V] BeginMoveSteps: a roll of 18
void rm8StoryCardUsed(int seat, int cardId);	// [V] Role_FinishCardUse: the used-card flags that the conditions >= 100 read
bool rm8StoryMonsterHit(int seat, int terra);	// [V] CPlayerTag__OnParkEvent @0x48d310 = message 0x31 (the monster hits a plot): true = a story branch replaced the monster; else build it, then rm8StoryMonsterBuilt()
void rm8StoryMonsterBuilt();			// [V] 0x48d310 tail: the monster was built -> the hero's +0x37c (舞美拉's counter)
bool rm8StoryMeetSpecial(int seat, int other);	// [V] Role_StartNpcDialogForRole @0x48c950: 錢夫人 meets <q#1> on map 5
bool rm8StoryHospitalPrison(int seat);		// [V] SendToHospital / SendToPrison: 莎拉's first visit
bool rm8StoryNpcPlaced(int seat, int npcId);	// [V] CPlayerTag__OnDiceEvent @0x491270: 孫小美 places an obstacle (unlocks 金泡菜)
bool rm8StoryDogBite(int victim);		// [V] Role_RunPoliceThiefDogAction @0x48bd00 (dog case) / Cls_5e31e0__BeginAttach @0x49caf0: 烏咪 (hero, layer 0) bitten by the dog gets piece 2 (0x58)
bool rm8StoryDogBiteDone(int seat);	// [A] the hospital move of the bitten hero: queues the 0x58(2) that rm8StoryDogBite noted
bool rm8StoryMonsterDue();			// [V] Game_TriggerEventByDate @0x4a6780: the story monster news (0x1e) is due
// 詹姆斯's unlock: the chain-store signs of the map (art 0x45b, scene.txt "aown" lines). Activate = Game_ActivateCharacterBuilding @0x4a37d0 (after the roles exist: shown only
// for the story hero whose layer bit is free); Click = Game_OnTileClickWithCard @0x4a4a10 -> CPlayerTag__OnChainStoreEvent @0x491630 (true: the sign was used, switch it off)
struct Rm8Scene;
void rm8StoryActivateBuildings(Rm8Scene &s);
bool rm8StoryChainStoreClick();
// [V] Role_CheckBankruptcy @0x48ee10, story special cases of seat 0 (金泡菜 rescued once, 莎拉's bankruptcy wins): -1 general case, 0 rescued, 1 handled
int rm8StoryBankruptSpecial(int seat);
bool rm8StoryWinSkip(int seat);	// [V] a click ends the win scene (state 0xD of the role): true if it was running
void rm8StoryUpdate(float dt);			// per frame (the 14 s win scene)

// what the original's game state asks the application to do (CGamePlayScene__Game_OnUpdate @0x4a17b0, g_GameState + 0x48)
enum Rm8StoryAction {
	RM8STORY_NONE = 0,
	RM8STORY_RESTART,	// state 2: same setup, board rebuilt (no start movie, no carried items)
	RM8STORY_ROLE_MENU,	// state 3 in story mode: back to the role menu
	RM8STORY_MAP_MENU,	// state 3 in the free game / state 4: the map menu (story: layer cleared, "請任選一關")
	RM8STORY_ENDING,	// state 5: the last layer is cleared: unlocks, then the ending movies and the staff credits
};
Rm8StoryAction rm8StoryPeekAction();
Rm8StoryAction rm8StoryTakeAction();

// the application side of the actions (set once by rm8_app.cpp); rm8StoryDispatch() runs the pending action through these
struct Rm8StoryHost {
	std::function<void(const Rm8Setup &)> start;	// start a board game of this setup (the RM8_STORY_CHAIN test host)
	std::function<void()> restart;		// rebuild the board of the same setup (calls rm8StoryApplyStart after the roles exist)
	std::function<void()> roleMenu;		// unload the board, show the role menu (story)
	std::function<void(bool story)> mapMenu;	// unload the board, show the map menu (story: announces the next layer; free game: the plain map menu)
	std::function<void()> ending;		// unload the board, play the ending movies, staff credits, back to the single menu
};
Rm8StoryHost &rm8StoryHost();
bool rm8StoryDispatch();			// per frame: runs a pending action (true = one ran, the board may be gone)

// ---- unlocks / progress (docs/story_mode.md 1.8 / 1.10)
// [V] Game_UpdateRecordStats @0x4abe20: run.level = max(run.level, level); Event_SetMaxLevel(hero, level) writes the profile stage
void rm8StoryUpdateRecordStats(int level);

// ---- test hooks (env), used by rm8_app.cpp
//  RM8_STORY=<role>[,<map>]  start a story game of that hero without the menus (map: the first candidate of layer 0 when omitted)
//  RM8_STORY_WIN=<seconds>   force a win (message 0x75) after that many seconds of game time;  RM8_STORY_LOSE=<seconds> the human goes bankrupt
//  RM8_STORY_CHAIN=1         the host also plays the menus: after a win it picks the first candidate of the next layer and goes on
bool rm8StoryDebugStart(Rm8Setup &st);
