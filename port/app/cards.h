// Card inventory, the card bar of the main HUD and card use (cards.cpp), after docs/cards_port.md.
// Card ids are card.bin Id (1..49); seats as in turn.h. Shared with events.cpp (card shop, card point) and python.cpp.
#pragma once
#include "turn.h"

struct Rm8Ui;

// ---- inventory [V] the deck is 8 fixed slots (Deck_AddCard @0x49a650: first empty slot; no compaction when a slot is emptied)
int rm8CardMax();				// slots per role (8)
int rm8CardCount(int seat);			// cards held (non-empty slots)
int rm8CardAt(int seat, int slot);		// card id or 0 (slot 0..7)
bool rm8CardAdd(int seat, int cardId);		// false if full / invalid
bool rm8CardRemoveAt(int seat, int slot);	// [V] Deck_RemoveAndStore: the card is kept for rm8CardRestore
bool rm8CardRemoveId(int seat, int cardId);	// first card with that id
void rm8CardClearAll();				// new game: empties every deck, then gives the start cards (Game_StartNewRound @0x4962a0)
int rm8CardFind(int seat, int cardId);		// [V] Deck_FindCardByID @0x49a940: slot or -1
bool rm8CardReady(int seat, int slot);		// [V] slot flag +4: card chosen for use (hidden on the bar)
bool rm8CardRestore(int seat);			// [V] Deck_RestoreCard @0x49a7c0: the last removed card goes back to its slot

// ---- card.bin data (scenes/cards.txt, exported by scripts/export_cards.py)
bool rm8CardLoadDefs(const std::string &file);
int rm8CardDefCount();
const char *rm8CardName(int cardId);		// UTF-8
const char *rm8CardTex(int cardId);		// the card picture texture key (card\<name>), "" if unknown
int rm8CardPrice(int cardId);			// points (card.bin ticket)
int rm8CardType(int cardId);			// 0 normal, 1 attack, 2 passive, 3 guard, 4 god
int rm8CardLevel(int cardId);
unsigned rm8CardCondition(int cardId);
const char *rm8CardDesc(int cardId);		// string.bin 6500 + Id
const char *rm8CardPy(int cardId);		// CardPy_NN module name or nullptr
bool rm8CardSupported(int cardId);		// [A] the effect is ported (the others stay in the hand)
int rm8CardRandom(int mode);			// [V] Card_SelectByDifficulty @0x4ae270: a random card id; mode 0 = higher levels more often
int rm8CardGodDays(int npcId);			// npcinfo.bin date (possession days)

// ---- turn flow hooks (game.cpp / roles.cpp)
// queued role messages the cards module handles (2 3 5 6 99 0x3b 0x43 0x19 0x28 0x3d 0x3e 0x46 0x24 0xD 0xE 0x10 0x44 0x2F 0x30 ...;
// 4 receive card stays in events.cpp); true if handled
bool rm8CardMessage(int seat, int cmd, int a, int b);
// message 1 with a selection kind (b & 0xFFFF in 1, 2, 8, 0x20, 0x82..0x85): true if the cards module opened it (state 7)
bool rm8CardModal(int seat, int a, int b);
// [V] Role_StayOrBeforeRoll @0x484610 for a computer with a != 0: Role_UseSpecialCard picks a card through Python Competence();
// true if a card was queued (the state must become 8)
bool rm8CardsAiStart(int seat);
// [V] Role_UseCardBeforeRoll @0x4fbab0 (Role_BeforeRollDice, computer): the planned cards (not ported: always false)
bool rm8CardsAiUsePlanned(int seat);
// role fields of the cards (CPlayerTag +0x1cc turtle days, +0x1d0 stay flag, +0x28c bubble, +0x1d8 status days)
int rm8CardsTurtle(int seat);
void rm8CardsSetTurtle(int seat, int days);	// [V] Role_SetTurtleDays @0x48b670
bool rm8CardsTurtleRoll(int seat);		// [V] Role_StartRollDice @0x484e50: true = the roll is forced to 1 (turtle days - 1)
bool rm8CardsHasBubble(int seat);
// [V] vf+0x64 (py_Role_HasBdygrd @0x4ff3e0, Role_PayToll, OnHolidayCardUse): true only for 莎拉 (roleinfo 16, class Cls_5e2878 vtable 0x5e2918 @0x498ac0) while her
// bodyguard (card 46, 7 turns) is there; every other role class returns 0 (Obj_Return0 @0x4f6230)
bool rm8CardsHasBodyguard(int seat);
void rm8CardsSetCamera(rw::Camera *cam);	// the main camera (roles.cpp rm8RolesSetCamera): board picks project with it
void rm8CardsUpdate(float dt);			// per frame (roles.cpp rm8RolesUpdate): holding a card (touch), the test driver
// [V] Role_ReceiveCardWithUi @0x488e80 for a human with a full hand: the new card is shown (getcard.ui, Card_LoadAndShowCard @0x4ae790) and the
// role waits (state 9) until a card of the bar is clicked (Card_HandleSwap @0x4ae8a0: it is replaced by the new one). false = not applicable
// (computer, or the hand is not full); the caller (events.cpp) then does the computer's automatic replacement. emote = the message's b
bool rm8CardsBeginSwap(int seat, int cardId, int emote);
// the right mouse button without a drag (rm8_app.cpp): the card menu on a hovered card, closing it, or the cancel of a card selection
// ([V] CGamePlayScene vf11 @0x4a2cf0: the first role in state 7 gets the result 0); true if used
bool rm8CardsOnRightClick(float x, float y);
void rm8CardsTurnStart(int seat, int step);	// [V] Role_RunTurnStartStep @0x4842f0 steps 0 (invisible - 1) and 4 (bubble - 1)
bool rm8CardsStayCheck(int seat);		// [V] Role_StayOrBeforeRoll: the stay flag (queues the landing event again); true if it was set
void rm8CardsSettle(int seat);			// [V] CPlayerTag__SetDeferredAction @0x4911b0 (Role.Settle)
void rm8CardsEnterHotel(int seat);	// [V] CPlayerTag__EnterHotelStatus @0x4852e0: status 3 for one day (message 0x13, event_build.cpp)
bool rm8CardsStatusStep(int seat);		// [V] turn start step 9 for the statuses 1 (sleep) 4 (hospital) 5 (prison) 6 (vanished); true if handled
void rm8CardsSetStatusDays(int seat, int days);	// news: +0x1D8 (the stay days of the status); 0 = the role leaves at its next turn start
bool rm8CardsVanish(int seat, int days);	// [V] Role_SetVanishStatus @0x48ae10: status 6 (hidden, "<name> has vanished, N days left"); false for a bankrupt role
void rm8CardsSetDefaultView(float yawDeg, float pitchDeg, float dist, float viewWindowY);	// the default board view (rm8_app.cpp) that defines the card range
bool rm8CardsPosInView(const float p[3]);	// [V] Camera_IsWorldPosInView @0x4fd5a0

// ---- HUD (hud.cpp): the card bar on gmaindlg widgets 11..18 (pictures), 19..26 (names), 31..38 (buttons)
void rm8CardsHudInit(Rm8Ui *main);
void rm8CardsHudUpdate(Rm8Ui *main, rw::Camera *cam);	// every frame before rm8UiRender
void rm8CardsHudRender(rw::Camera *cam);		// after rm8UiRender: target markers
bool rm8CardsOnSlotClick(int slot);			// button 31 + slot clicked
// a tap / click on the board that the UI did not take (rm8_app.cpp); true if the card target selection used it
bool rm8CardsOnTap(float x, float y);
bool rm8CardsCancelPick();				// the prompt's cancel button: close it, result 0
bool rm8CardsPicking();					// a card target selection is open
void rm8CardsSetSellMode(bool on);			// [V] DAT_00640958: clicking a card sells it (half the price in points) - for the card shop

// ---- Python (python.cpp)
// [V] Card_CallPythonCallbackWithParams @0x4ae650 (Execute(param, sa, sb)) / Card_CallPythonCallback @0x4ae580 (Competence());
// false if the module or function is missing or raised
bool rm8PyCardCall(const char *module, bool execute, long param, long sa, long sb, long &result);
extern int rm8CardsDifficulty;				// init.txt difficuty (Role.GameDffty); the game's default file has 3
