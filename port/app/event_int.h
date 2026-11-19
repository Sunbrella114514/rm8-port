// Internal interfaces between events.cpp (framework, simple events, messages), event_bank.cpp and event_shop.cpp.
#pragma once
#include "events.h"
#include "turn.h"
#include "ui.h"
#include "cards.h"
#include <string>
#include <functional>
#include <initializer_list>

namespace ev {

// An event task of the original (the object at scene +0x34, vtable 0x5DFF88 family): the constructor / loader (vf16) is the
// C++ constructor, update() is vf1 (true = the game's return value bit 31, i.e. finished -> Game_EndTurn @0x4a48d0).
struct Task {
	int ev = 0, seat = -1;
	Rm8Ui *ui = nullptr;				// the task's dialog (taps go through rm8UiPointerDown/Up; the test hook presses widgets the same way)
	virtual ~Task(){}
	virtual bool update(float dt) = 0;
	virtual void click(const std::string &name){ (void)name; }	// widget name pressed (test hook and UI)
	virtual bool ready() const { return true; }
	virtual bool skip(){ return false; }	// [V] vtable +0x34 (vf13) of the scene click / key skip (CGamePlayScene vf13 @0x4A3060, docs/skip.md): cut the wait short; false = nothing to skip
	virtual int idOf(const std::string &name){ (void)name; return -1; }	// test hook: dynamic widget id for a symbolic name (court: "role<seat>")			// the UI accepts input (test hook gating)
	virtual int inputSeat() const { return seat; }	// [N] netplay: the seat whose decision the dialog waits for (net.cpp rm8NetInputSeat)
};

Task *makeBankTask(int seat, bool passing);	// event 5 / 99 (event_bank.cpp)
Task *makeShopTask(int seat);			// event 1 (event_shop.cpp)
Task *makeAskTask(int seat);			// event 2 (event_ask.cpp)
Task *makeLetouTask(int seat);			// event 6 (event_lottery.cpp)
Task *makeLetouDrawTask(int seat);		// event 0x62
bool aiLottery(int seat);			// [V] Lottery_BuyTicket @0x4f8290
void lotteryClear();
bool lotteryDrawDue();
void lotteryPoolAdd(int32_t v);			// [N] news fines go into the prize pool (+0x50)
Task *makeMagicTask(int seat);			// event 0xD (event_magic.cpp)
bool aiMagic(int seat);				// [V] Auction_Vendue_Process @0x4f84c0 (the magic house AI)
void swapMessage(int seat, int a, int b);	// message 0x27
void bombMessage(int seat, int a, int b);	// message 0x35
Task *makePowerTask(int seat);			// event 0xF (event_power.cpp)
Task *makeTicketTask(int seat);			// event 0x10
bool aiPower(int seat);				// [V] PowerCo_PayElectricity @0x4fc150
bool aiTicket(int seat);			// [V] TicketCenter_PayTickets @0x4fc490
void companyClear();
void testChairman(bool power, int seat);	// [A] test hook (events.cpp RM8_EVENT_TEST=ticketland / powerland): the seat holds 10 % and is the chairman
Task *makeCourtTask(int seat);			// event 0xE (event_court.cpp)
bool aiCourt(int seat);				// [V] AI_Event_Court @0x4f8810
void bailMessage(int seat, int cmd, int a, int b);	// messages 0x5d / 0x5e (prison bail)
Task *makeMiniTask(int seat, int ev);		// events 0x12..0x17 (event_mini.cpp)
Task *makeVendueTask(int seat, int terra, bool bankruptcy);	// events 0x50 / 0x51 (event_vendue.cpp)
Task *makeBuildingTask(int seat, int terra);	// the chain store / hotel / institute of a plot (event_build.cpp)
void instituteTick();	// [V] Institute_DisplaySuccess @0x4f6890 for every institute, once per new day
void instituteClear();
bool instituteGet(int terra, int &card, int &days);	// the research of an institute: card index (-1 none), days left
void instituteSet(int terra, int card, int days);
Task *makeWonderTask(int seat, int idx);	// events 0x5A / 0x5B / 0x5C (event_wonder.cpp), null when the wonder is built
Task *makeWonderStageTask(int seat, int idx);	// message 0x33: the building effect on the site
void wonderSetDir(const std::string &scenesDir);	// scenes directory (wonders.txt of the loaded map)
void wonderClear();
int wonderOwner(int idx);	// [V] Wonder_CheckConstruction @0x4e1800: seat, -1 none, -2 bankrupt owner
bool wonderGet(int idx, bool &active, int &owner, float share[8], int &stage, int &percent);	// the save module
void wonderSet(int idx, bool active, int owner, const float share[8], int stage, int percent);
int wonderOwnerRaw(int idx);	// the record's owner (-1 none)
int wonderAtNode(int node);	// [V] Land_GetLevel @0x4f0600
bool aiMiniGame(int seat, int ev, bool forceComputer = false, bool plusLow = true);	// [V] MiniGame_ExecuteGame @0x4fcb10; [N] N4: forceComputer = the host decides a human's minigame (also game 0x15), plusLow = false: no rules_plus B6 low score (computer seats only)

bool aiQuestion(int seat);			// [V] AI_Event_Question @0x4f7fa0
bool loadQuestions(const std::string &file);

// ---- helpers shared by the event files
void queueText(int seat, const std::string &t);			// [V] Role_QueueTextMsg @0x484b70: queue 0x1B (info box)
void queueTextAt(int seat, int pos, const std::string &t);	// [V] Role_QueueTextMsgAt @0x484bf0
float waitScale();						// [V] Anim_ApplyDifficultyScale @0x47b3b0 factor: 1 - min((speed - 1) / 10, 1)
int rnd();							// the rules RNG (MSVC rand)
std::string plain(const std::string &s, int *sound = nullptr);	// string.bin text: "text@4601" -> "text", sound id 4601
std::string wrap(const std::string &s, float width);		// [A] line breaks for labels that need wrapping (width in full-width characters)
std::string plainFmt(int id, ...);
std::string plainFmtV(int id, ...);				// the same with the "@N" voice suffix kept (for the presenters' say())				// rm8StrFmt + plain
void addTickets(int seat, int32_t n);				// [N] [V] Role_AddTicket @0x489ed0 (cap 9999)
void subTickets(int seat, int32_t n);				// [V] Role_SubTicket @0x489f90 (floor 0)
int cardTotal();						// number of card.bin entries
int cardLevel(int id);						// card.bin level (+0x210)
int cardPrice(int id);						// card.bin ticket (+0x214), from rm8CardPrice when the cards module has it
int cardSelectByDifficulty(int param);				// [V] Card_SelectByDifficulty @0x4ae270: card index (id - 1) or -1
int difficulty();						// [V] InitTxt_GetParam(7): the live difficulty option (rm8Difficulty, scene.h)
bool holdsCard(int seat, int id);

// the single-die go panel at the dialog's dice widget (hud.cpp rm8HudDieGo): the widget is hidden, the panel's button calls task->click("go")
void showDieGo(Task *t, const char *widget);
void hideDieGo(Task *t);

void uiTapOnly(Rm8Ui *ui, std::initializer_list<const char*> names);	// [A] every widget ignores taps except `names` (decor / planes / presenter must not swallow them)

// ---- 3D presenters (event_npc.cpp)
struct Npc;
bool loadEventArts(const std::string &file);
bool loadEventRoles(const std::string &rolesTxt);
Npc *npcCreate(int art, Rm8Ui *ui, const char *widget, int charId = 0, int outfit = 0);	// charId > 0: the character's doll (roleinfo model, body texture of `outfit`; 4 = the swimsuit model) instead of `art`
//	// [V] CArticleDFF_Load(art): null if the model is missing; the raster is shown in the dialog widget `widget`
void npcPlay(Npc *n, int act, bool loop = false);			// [V] Article_SelectByTypeAndSetProperties(act, 1, 0)
Npc *npcCreateModel(const std::string &dff, const std::string &texdir, const std::string &anim, Rm8Ui *ui, const char *widget);	// a plain model (the auction's house), animation 0 = `anim`
void npcSetSpin(Npc *n, float degPerSec, float rscale);			// [V] Vendue_RotateImage @0x4e4fe0 turns it 160 deg/s; the lot is drawn with radius * 1.6 (LittleGame_Rotate3D @0x4e1f40)
void npcDestroy(Npc *n);
void npcUpdate(float dt);

// ---- loans (event_bank.cpp)
int32_t loanPrincipal(int seat);	// [N] int32_t
void loanRecord(int seat, int32_t amount);				// [V] Bank_TrySetLoan @0x47ea00 (no money)
void loanTake(int seat, int32_t amount);				// [V] Money_TakeLoan @0x47e970
int loanDaysLeft(int seat);					// [V] Bank_ComputeInterest @0x47eae0
int32_t loanRollback(int seat);					// [N] [V] Bank_RollbackLoan @0x47eb70
void loanClear();
// money helpers [V] @0x47e6c0 / @0x47e720 / @0x47e870 / @0x47e8f0 (account 0 cash, 1 deposit)
void moneyAdd(int seat, int32_t v, int account);	// [N] int32_t
void moneyWithdraw(int seat, int32_t v, int account);
void moneyDeposit(int seat, int32_t v);
void moneyTakeOut(int seat, int32_t v);
bool aiBank(int seat, bool stop);				// [V] AI_Event_Bank @0x4f9000
bool aiCardShop(int seat);					// [V] Card_BuySell @0x4fc8d0 (event_shop.cpp)

}	// namespace ev
