// Collections kept across games (collect.cpp): the per-profile progress of the game (gamedate.bin) and the story storage box.
// docs/media_collect.md. [V] = verified in the decompile (address), [A] = port adaptation.
//  - profile: the game's "GameDate" object (GameDate_Load @0x4a99e0 / GameDate_Save @0x4a9d60 on Config\gamedate.bin; its accessors are
//    named Event_* in the decompile): role unlocks, story level per role, 9 puzzle pieces per role, map unlock flags, quest counters.
//  - puzzle album (picmenu): the pieces; rm8PuzzleCollect is the effect of message 0x58 (NPC 17 / story missions).
//  - storage box (boxmenu): the "BinaryData" object at 0x6405d0 (story mode only in the original; saved with the story records).
#pragma once
#include "turn.h"

// data folder: <dir>/rm8_gamedate.bin and <dir>/rm8_box.bin ([A] the game folder is read-only; dir = the parent of the scenes root, next to
// rm8_settings.txt). Loads both files; a missing profile starts as Event_InitializeData @0x4aa0b0 (roles 1..10 unlocked, nothing collected).
// RM8_IMPORT_GAMEDATE=1 reads <gameDir>/Config/gamedate.bin once when there is no port profile yet (test: a complete album).
void rm8CollectInit(const std::string &dataDir, const std::string &gameDir);

// ---- profile (role = roleinfo Id 1..16)
int rm8ProfileLevel(int role);			// [V] Event_GetLevel @0x4aa230: story progress 0..4 (finalMenu plays the ending movie from 4)
void rm8ProfileSetMaxLevel(int role, int level);	// [V] Event_SetMaxLevel @0x4aa250 (only raises)
bool rm8ProfileRoleUnlocked(int role);		// [V] Event_GetTerrainData @0x4aa1e0 (roles 1..10 start unlocked)
void rm8ProfileUnlockRole(int role);		// [V] Event_SetTerrainData(role, 1) @0x4aa200
bool rm8ProfileMapUnlocked(int type);		// [V] Event_GetEventByType @0x4aa190: map 1..11 flags (all but 3 start set); the menus show every map [A]
// [V] quest data 1..9 @0x4aa5b0 / 0x4aa5d0 (gamedate.bin ints 168..176): the best score of minigame n (ltgameMenu HI-SCORE = Event_GetQuestData(n)
// @0x507f20, written by the minigame host after a game: Event_SetQuestData(game - 0x11, score) @0x4aa5d0 keeps the larger value, at most 9999)
int rm8ProfileQuest(int n);
// [V] the story unlock flags (gamedate ints 177..180, GameDate +0x880..+0x88C): Event_SetBitFlag @0x4aa380 (11 chain-store bits; all set unlocks role 15), Event_IncrementCounter @0x4aa420
// (the 4th unlocks role 13), Event_SetFlag2 @0x4aa470 (4 bits; all set unlocks role 11), Event_SetFlag3 @0x4aa510 (0, 1, 2 in order; 2 unlocks role 14). True = the bit was newly set.
bool rm8ProfileSetBit(int bit);
bool rm8ProfileBitFree(int bit);	// [V] Event_CheckBitFlag @0x4aa350: true when the bit is NOT set yet
int rm8ProfileIncrementCounter();	// returns the new count
bool rm8ProfileSetFlag2(int bit);
bool rm8ProfileSetFlag3(int idx);
void rm8ProfileSetQuest(int n, int score);

// ---- puzzle pieces (picmenu)
bool rm8PuzzleHas(int role, int piece);		// [V] Event_GetItemData(role, piece != 0), piece 1..9
int rm8PuzzleCount(int role);			// [V] Event_CountOwnedItems @0x4aa290
// [V] CPlayerTag__OnPuzzleCardUse @0x491830, the handler of role message 0x58 (param = piece 1..9, the NPC's +0x94 / a story mission piece):
// Event_SetItemData(role of the seat, piece) saves the profile and an info message (cmd 0x1B at the front of the queue, string 434
// "%s獲得%d號拼圖。") is queued. Returns true if the piece was new for the profile; false for an invalid piece or one already owned.
bool rm8PuzzleCollect(int seat, int param);
bool rm8PuzzleGrant(int role, int piece);	// the profile part of rm8PuzzleCollect without a seat / message (Event_SetItemData): true if new
int rm8PuzzlePic(int role, int piece);		// [V] uvlist id of a piece picture: (role - 1) * 9 + 0x12d + piece

// ---- storage box [V] &DAT_006405d0 (Game_GetBinaryDataManager @0x494110), saved by Game_SaveBinaryData @0x4abd60 (84 bytes in the story records)
// "stored" = side 0 (left grid of boxmenu), "carry" = side 1 (right grid, goes along into the next stage)
struct Rm8Box {
	int stage = 0;			// +0x00: highest story stage reached (Game_UpdateRecordStats @0x4abe20)
	int cash[2] = {0, 0};		// *(+4): [0] stored, [1] carried (boxmenu labels 22 / 27)
	int points[2] = {0, 0};		// *(+8): [0] stored, [1] carried (labels 25 / 28)
	int cards[2][8] = {{0}, {0}};	// +0x0C stored cards, +0x2C carried cards (card.bin Id, 0 empty)
};
Rm8Box &rm8Box();
void rm8BoxSave();				// [A] persists to rm8_box.bin after every change
void rm8BoxClear();				// [V] Game_ClearBinaryData @0x4abcc0 (a new story role)
bool rm8BoxStoreCard(int cardId);		// [V] Role_StoreCardToBox @0x488de0 -> CardArray_FindSlotAndAdd @0x4abed0: false if the 8 slots are full
void rm8BoxAddReward(int cash, int points, const int *cards, int nCards);	// [V] CPlayerTag__OnTreasurechest @0x48f9d0 (stored side)
bool rm8BoxMoveCash();				// [V] Cls_5e7f10__vf12_RefreshDisplay @0x502580: carried = stored, stored = 0 (needs stored > 0)
bool rm8BoxMovePoints();			// [V] Cls_5e7f10__vf12_RefreshLand @0x502640 (the "land" of the name is the 點券 row)
bool rm8BoxMoveCard(int fromSide, int slot);	// [V] vf12_RefreshCards @0x502700 (side 0 -> 1) / RefreshSlots @0x5027e0 (1 -> 0): first free slot of the other side
int rm8BoxMoveAll(int fromSide);		// [V] the arrow buttons 3 / 2 (8 slots): number of cards moved
void rm8BoxMergePending();			// [V] u_Obj_ApplyPendingChanges @0x4ac020 (leaving the map menu): carried -> stored
// [V] Game_StartNewRound @0x4962a0 (story): the carried cards go to the first role's deck, carried cash / points are added to it, then
// u_Obj_ResetSlotArrays @0x4abfd0 clears the carried side. [A] the stage's own start money / cards (pass config) are not applied here.
void rm8BoxApplyToSeat(int seat);
