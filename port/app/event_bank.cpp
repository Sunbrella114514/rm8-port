// Bank (event 5, and 99 = passing the bank with the loan disabled), after the task object Bank_InitContainer @0x4b2c60 (vtable 0x5E4740):
// loader CUi_bank__LoadUI @0x4c1590, Update CUi_bank__Update @0x4c1c40, buttons Bank_OnButton @0x4c1240 and the handlers behind it
// (0x4c2020 .. 0x4c42a0), computer: AI_Event_Bank @0x4f9000. Money ops: 0x47e6c0 / 0x47e720 / 0x47e870 / 0x47e8f0 / 0x47e970 /
// 0x47ea60 / 0x47eae0 / 0x47eb70 (the money struct is at role +0x290: cash, deposit, loan principal, loan date, flags).
// [A] not drawn: the 3D bank clerk (article 0x419 rendered into the "NPC" plane, animations 0..3) and the keyboard entry of the
//     edit box (CUi_bank vf: Bank_OnAmountInput @0x4c1f70); the buttons are the touch entry.
#include "event_int.h"
#include "audio.h"
#include "rules_plus.h"
#include "save.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace ev {

namespace {

const int32_t MONEY_MAX = 999999999;	// [N] int32_t

// [V] the loan record at role +0x298: principal, the date it was taken (g_GameState +4 year, +8 month, +0xC day), flags (hi 16 bits:
// number of monthly interest payments, lo 16 bits 3 = months)
struct Loan { int32_t principal = 0; int y = 0, m = 0, d = 0, count = 0; };	// [N] int32_t
std::vector<Loan> g_loan;
Loan &loanOf(int seat){ if((int)g_loan.size() <= seat) g_loan.resize(seat + 1); return g_loan[std::max(0, seat)]; }

// [V] Date_CalcDaysDiff @0x431680: days from the current date to (month tm, day td); the year only matters for February
bool leap(int y){ return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int daysBetween(int tm, int td, int cy, int cm, int cd)
{
	if(tm == cm && cd <= td) return td - cd;
	int total = 0, y = cy, m = cm;
	while(m != tm){
		int dim = 30;
		if(m < 8){ if(m % 2) dim = 31; else if(m == 2) dim = leap(y) ? 29 : 28; }
		else if(m % 2 == 0) dim = 31;
		total += m == cm ? dim - cd : dim;
		if(++m > 12){ m = 1; y++; }
	}
	return total + (m == cm ? td - cd : td);
}

// ---- the dialog
struct BankTask : Task {
	bool passing;
	float timer = 0;			// [V] +8
	int phase = 0;				// [V] +0x3C: 0 welcome, 1 menu, 9 closing
	int mode = 7;				// [V] +0x34: 0 deposit, 1 withdraw, 2 loan, 3 loan sizes, 4 repay, 5 done, 6 finished
	int32_t amount = 0;			// [N] [V] +0x38
	int voice = 0;				// [V] +0x20 the playing voice sample
	Npc *npc = nullptr;			// [V] article 0x419 (event_npc.cpp)
	bool editVisible = false;

	void vis(const char *n, bool v){ rm8UiSetVisibleN(ui, n, v); }
	void txt(const char *n, const std::string &s){ rm8UiSetTextByName(ui, n, s); }
	void align(const char *n, int h, int v){ rm8UiSetAlignN(ui, n, h, v); }
	bool voiceBusy(){ return voice > 0 && rm8SndPlaying(voice); }
	// [V] vf13 CUi_bank__OnBeforeClose @0x4C44F0: not while the menu waits for the player (phase 1, no voice); else timer (+8) = 0 and the voice (+0x20) stopped
	bool skip() override { if(phase == 1 && !voiceBusy()) return false; timer = 0; if(voice){ rm8SndStop(voice); voice = 0; } return true; }
	void speak(int strId)			// the clerk's bubble text "bobo" and its voice (string "text@soundId")
	{
		int snd = 0; std::string t = plain(rm8Str(strId), &snd);
		txt("bobo", wrap(t, 17));
		rm8SndStop(voice);
		voice = snd ? rm8SndPlay(snd) : 0;
	}
	void stopVoice(){ if(voice){ rm8SndStop(voice); voice = 0; } }
	void panels(bool edit, bool label, bool info, bool info1, bool big)	// Edit, label, Info, Info1, bigInfo
	{
		vis("Edit", edit); vis("label", label); vis("Info", info); vis("Info1", info1); vis("bigInfo", big);
		editVisible = edit;
	}
	void editText(const std::string &s){ txt("Edit", s); }
	void clearButtons(bool enable)		// [V] Bank_ClearButtons @0x4c4590: the 15 pad buttons
	{
		static const char *B[15] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "00", "Max", "back", "Clear", "ok"};
		for(const char *n : B) rm8UiSetEnabledN(ui, n, enable);
	}
	void buttonStates()			// [V] Bank_SetButtonStates @0x4c4790: pictures 0x229 0x22D 0x22A 0x22B, the active one 0x22E..0x231 [H]
	{
		rm8UiSetPicN(ui, "saving", 0x229); rm8UiSetPicN(ui, "get", 0x22D); rm8UiSetPicN(ui, "loan", 0x22A); rm8UiSetPicN(ui, "refund", 0x22B);
		switch(mode){
		case 0: rm8UiSetPicN(ui, "saving", 0x22E); break;
		case 1: rm8UiSetPicN(ui, "get", 0x22F); break;
		case 2: case 3: rm8UiSetPicN(ui, "loan", 0x230); break;
		case 4: rm8UiSetPicN(ui, "refund", 0x231); break;
		}
	}
	// [V] Bank_GetMaxAmount @0x4c2640
	int32_t maxAmount()	// [N] int32_t
	{
		switch(mode){
		case 0: return rm8GameCash(seat);
		case 1: return rm8GameDeposit(seat);
		case 3: return 200000;
		case 4: return std::min(loanPrincipal(seat), rm8GameCash(seat) + rm8GameDeposit(seat));
		}
		return 0;
	}
	void showAmount(){ char b[32]; snprintf(b, sizeof b, "%d", amount); editText(b); }

	BankTask(int s, bool pass) : passing(pass)
	{
		seat = s;
		ui = rm8UiLoad("bank", RM8UI_DIALOG);
		if(!ui) return;
		// [V] CUi_bank__LoadUI @0x4c1590
		timer = 1.6f * waitScale();
		npc = npcCreate(0x419, ui, "NPC"); npcPlay(npc, 0);
		panels(false, false, false, false, false);
		speak(4601);
		if(loanPrincipal(seat) > 0){	// the loan reminder: str 5420 "<str#5420>%d天"
			panels(false, false, false, false, true);
			txt("bigInfo", plainFmt(5420, loanDaysLeft(seat)));
		}
		clearButtons(false);
		uiTapOnly(ui, {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "00", "Max", "back", "Clear", "ok", "saving", "get", "loan", "refund", "cancel"});
		if(passing) rm8UiSetEnabledN(ui, "loan", false);	// [V] Bank_InitContainer(1): the loan button is disabled
		buttonStates();
		rm8UiOnClickName(ui, [this](const std::string &n){ click(n); });
		rm8UiShow(ui, true);
	}
	~BankTask() override { npcDestroy(npc); stopVoice(); rm8UiShow(ui, false); rm8UiOnClickName(ui, nullptr); }
	bool ok() const { return ui != nullptr; }
	bool ready() const override { return phase == 1; }

	// [V] CUi_bank__Update @0x4c1c40
	bool update(float dt) override
	{
		timer -= dt;
		if(timer <= 0 && !voiceBusy()){
			timer = 1.6f * waitScale();
			if(phase == 0){ phase = 1; speak(passing ? 4603 : 4602); npcPlay(npc, passing ? 2 : 1); }
			else if(phase == 9 && mode == 5){ mode = 6; if(passing) rm8TurnEnqueue(seat, 0x26, 0, 0); return true; }
		}
		return false;
	}

	// [V] Bank_OnComplete @0x4c42a0
	void complete()
	{
		panels(false, false, false, false, true);
		align("bigInfo", 1, 1);
		txt("bigInfo", plainFmt(5410));
		mode = 5; timer = 1.6f * waitScale(); phase = 9;
		speak(4604); npcPlay(npc, 3);
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: bank done seat %d: cash %d deposit %d loan %d", seat, rm8GameCash(seat), rm8GameDeposit(seat), loanPrincipal(seat));
	}

	// [V] Bank_OnButton @0x4c1240
	void click(const std::string &n) override
	{
		if(passing && n == "loan") return;	// disabled button (the UI does not deliver it; guards the test hook)
		if(n.size() == 1 && n[0] >= '0' && n[0] <= '9') loanButton(n[0] - '0');
		else if(n == "00") multiply100();
		else if(n == "Max"){ if(editVisible){ amount = maxAmount(); showAmount(); } }
		else if(n == "back"){ if(editVisible){ amount /= 10; showAmount(); } }
		else if(n == "Clear"){ if(editVisible){ amount = 0; editText("0"); } }
		else if(n == "ok") transfer();
		else if(n == "saving") depositUI();
		else if(n == "get") withdrawUI();
		else if(n == "loan") loanUI();
		else if(n == "refund") repayUI();
		else if(n == "cancel") exitUI();
	}

	// [V] Bank_LoanButton @0x4c2020: in mode 3 the digits 1..3 take a loan of 100000 x d; with the edit box shown they type
	void loanButton(int d)
	{
		if(mode == 3 && d >= 1 && d <= 3){
			loanTake(seat, 100000 * d);	// [N] int32_t
			complete();
		}
		if(editVisible){
			amount = rm8Add32(rm8Mul32(amount, 10), d);	// [N] a typed 10-digit amount wraps like the original's 32-bit imul (explicit, no UB)
			if(amount > maxAmount()) amount = maxAmount();
			showAmount();
		}
	}
	void multiply100()	// [V] Bank_Multiply100 @0x4c2510
	{
		if(!editVisible) return;
		amount = rm8Mul32(amount, 100);	// [N] 32-bit wrap (amount > 21474836 after the cap), explicit
		if(amount > maxAmount()) amount = maxAmount();
		showAmount();
	}

	// [V] Bank_Transfer @0x4c2a50
	void transfer()
	{
		switch(mode){
		case 0:
			if(editVisible && rm8GameCash(seat) > 0){ moneyWithdraw(seat, amount, 0); moneyAdd(seat, amount, 1); complete(); }
			break;
		case 1:
			if(editVisible && rm8GameDeposit(seat) > 0){ moneyWithdraw(seat, amount, 1); moneyAdd(seat, amount, 0); complete(); }
			break;
		case 2:
			if(loanPrincipal(seat) < 1){	// Bank_LoanOptions @0x4c4160: str 5415 \n\n 5416 \n\n 5417
				mode = 3;
				panels(false, false, false, false, true);
				align("bigInfo", 0, 1);
				txt("bigInfo", plainFmt(5415) + "\n\n" + plain(rm8Str(5416)) + "\n\n" + plain(rm8Str(5417)));
			}
			break;
		}
	}

	// [V] Bank_DepositUI @0x4c2ed0 / Bank_WithdrawUI @0x4c3240
	void depositUI()
	{
		if(phase == 9) return;
		stopVoice(); amount = 0; mode = 0; clearButtons(true); buttonStates();
		editText("0"); panels(true, true, true, true, false);
		txt("label", plainFmt(5402));
		if(rm8GameCash(seat) < 1){ txt("Info", plainFmt(5418)); txt("Info1", ""); }
		else { txt("Info", plainFmt(5411)); txt("Info1", std::to_string(rm8GameCash(seat))); }
	}
	void withdrawUI()
	{
		if(phase == 9) return;
		mode = 1; clearButtons(true); buttonStates(); stopVoice(); amount = 0;
		editText("0"); panels(true, true, true, true, false);
		txt("label", plainFmt(5404));
		if(rm8GameDeposit(seat) < 1){ txt("Info", plainFmt(5407)); txt("Info1", ""); }
		else { txt("Info", plainFmt(5401)); txt("Info1", std::to_string(rm8GameDeposit(seat))); }
	}
	// [V] Bank_LoanUI @0x4c35b0: str 5409 when a loan is open, else 5412 (sprintf) + strcat 5413, 5414 (rate, term, "press OK")
	void loanUI()
	{
		if(phase == 9) return;
		mode = 2; clearButtons(true); buttonStates(); stopVoice(); amount = 0;
		panels(false, false, false, false, true);
		if(loanPrincipal(seat) < 1){
			align("bigInfo", 0, 1);
			txt("bigInfo", plainFmt(5412) + "\n\n" + plain(rm8Str(5413)) + "\n\n" + plain(rm8Str(5414)));
		}else{
			align("bigInfo", 1, 1);
			txt("bigInfo", plainFmt(5409));
		}
	}
	// [V] Bank_RepayUI @0x4c38e0: no loan -> str 5408; else mode 4, "<str#5419>" (5419); when cash + deposit exceed the principal
	// it is repaid at once (Bank_LoanRepay @0x47ea60) and the dialog completes; the text shown at the end is the total line
	void repayUI()
	{
		if(phase == 9) return;
		stopVoice();
		panels(false, false, false, false, true);
		amount = 0;
		int32_t p = loanPrincipal(seat);	// [N] int32_t
		if(p < 1){ align("bigInfo", 1, 1); txt("bigInfo", plainFmt(5408)); return; }
		mode = 4; clearButtons(true); buttonStates(); align("bigInfo", 1, 1);
		std::string t = plainFmt(5419, (int)p);
		if(p < rm8GameCash(seat) + rm8GameDeposit(seat)){ loanRepay(seat); complete(); }
		else t += "\n\n" + plain(rm8Str(5403));
		txt("bigInfo", t);
	}
	// [V] Bank_ExitUI @0x4c3fa0
	void exitUI()
	{
		if(phase == 9) return;
		stopVoice(); mode = 5; phase = 9; timer = 0.1f * waitScale();
		panels(false, false, false, false, false);
		speak(4604); npcPlay(npc, 3);
	}

	static void loanRepay(int s)	// [V] Bank_LoanRepay @0x47ea60
	{
		Loan &L = loanOf(s);
		int32_t c = rm8GameCash(s), d = rm8GameDeposit(s) - L.principal;	// [N] int32_t
		if(d < 0){ c += d; d = 0; }
		rm8GameSetMoney(s, c, d);
		L.principal = 0; L.count = 0;
	}
};

}	// namespace

int32_t loanPrincipal(int seat){ return seat >= 0 ? loanOf(seat).principal : 0; }	// [N] int32_t
void loanRecord(int seat, int32_t amount){ Loan &L = loanOf(seat); if(L.principal != 0) return; int wd; rm8GameDate(L.y, L.m, L.d, wd); L.principal = amount; L.count = 0; }	// [V] Bank_TrySetLoan @0x47ea00
void loanClear(){ g_loan.clear(); }

// [V] Money_TakeLoan @0x47e970: only without an open loan; the amount goes to the deposit
void loanTake(int seat, int32_t amount)	// [N] int32_t
{
	Loan &L = loanOf(seat);
	if(L.principal != 0) return;
	rm8RulesLoanTaken(seat);	// [R] a normal loan replaces nothing: the story-loan mark (rules_plus) only belongs to the scripted loan
	moneyAdd(seat, amount, 1);
	int wd; rm8GameDate(L.y, L.m, L.d, wd);
	L.principal = amount; L.count = 0;
}

// [V] Bank_ComputeInterest @0x47eae0: days to the due date (the loan month + 3, same day)
int loanDaysLeft(int seat)
{
	Loan &L = loanOf(seat);
	if(!L.principal) return 0;
	int y, m, d, wd; rm8GameDate(y, m, d, wd);
	int tm = L.m + 3; if(tm > 12) tm -= 12;
	return daysBetween(tm, L.d, y, m, d);
}

// [V] Bank_RollbackLoan @0x47eb70: months since the loan; < 3: 10% of the principal once per month on the loan day; later: the loan
// is cleared and principal + 10% are due. Returns the amount to take (0 = nothing).
int32_t loanRollback(int seat)	// [N] int32_t
{
	Loan &L = loanOf(seat);
	if(!L.principal) return 0;
	int y, m, d, wd; rm8GameDate(y, m, d, wd);
	int mm = m - L.m; if(mm < 0) mm += 12;
	if(mm < 3){
		if(L.count < mm && (L.count + 1 < mm || L.d <= d)){ L.count++; return L.principal * 10 / 100; }
	}else if(mm > 3 || L.d <= d){
		int32_t p = L.principal; L.principal = 0; L.count = 0;	// [N] int32_t
		return p * 10 / 100 + p;
	}
	return 0;
}

void moneyAdd(int s, int32_t v, int account)	// [N] int32_t
{
	int32_t c = rm8GameCash(s), d = rm8GameDeposit(s);	// [N] int32_t
	if(account == 0) c = std::min(MONEY_MAX, c + v); else if(account == 1) d = std::min(MONEY_MAX, d + v);
	rm8GameSetMoney(s, c, d);
}
void moneyWithdraw(int s, int32_t v, int account)	// [N] int32_t
{
	int32_t c = rm8GameCash(s), d = rm8GameDeposit(s);	// [N] int32_t
	if(account == 0){ c -= v; if(c < 0){ d = std::max(0, d + c); c = 0; } }
	else if(account == 1){ d -= v; if(d < 0){ c = std::max(0, c + d); d = 0; } }
	rm8GameSetMoney(s, c, d);
}
void moneyDeposit(int s, int32_t v){ int32_t c = std::max(0, rm8GameCash(s) - v), d = std::min(MONEY_MAX, rm8GameDeposit(s) + v); rm8GameSetMoney(s, c, d); }	// [N] int32_t
void moneyTakeOut(int s, int32_t v){ int32_t c = std::min(MONEY_MAX, rm8GameCash(s) + v), d = std::max(0, rm8GameDeposit(s) - v); rm8GameSetMoney(s, c, d); }

Task *makeBankTask(int seat, bool passing)
{
	BankTask *t = new BankTask(seat, passing);
	if(!t->ok()){ delete t; return nullptr; }	// [V] a loader that fails deletes the task (CreateGameObject)
	return t;
}

// [V] AI_Event_Bank @0x4f9000 (param 1 = stopped on the bank, 0 = passing): first the loan decision (Borrow, only when stopping and
// no loan is open), then Access: negative = deposit, positive = withdraw. Returns whether the role acted.
bool aiBank(int seat, bool stop)
{
	const char *script = rm8TurnAiScript(seat);
	const std::string name = rm8TurnName(seat);
	int32_t r = 0;	// [N] int32_t
	// [R] rules_plus B1: early repayment when the AI has spare cash (at the bank, stopping or passing; never a story-scripted loan, rules_plus.h)
	if(rm8RulesOn() && loanPrincipal(seat) > 0){
		int32_t P = loanPrincipal(seat); const Loan &L = loanOf(seat);	// [N] int32_t
		int y, m, d, wd; rm8GameDate(y, m, d, wd);
		int age = (int)(rm8SaveCivilDays(y, m, d) - rm8SaveCivilDays(L.y, L.m, L.d));
		if(rm8RulesRepayWanted(seat, rm8GameCash(seat) + rm8GameDeposit(seat), P, age, !stop)){
			BankTask::loanRepay(seat);
			rm8Log("rules+: seat %d (%s) repays the loan of %d early (cash %d deposit %d left, %d days after taking it, %d days before it is due)", seat, name.c_str(), P, rm8GameCash(seat), rm8GameDeposit(seat), age, loanDaysLeft(seat));
			queueText(seat, name + u8"提前還清貸款" + std::to_string(P) + u8"元");
			rm8TurnEnqueue(seat, 0x47, 0, 0);
			if(!stop){ rm8TurnEnqueue(seat, 0x26, 0, 0); rm8TurnSetState(seat, 8); }	// resume the walk (as the Access branch below)
			return true;
		}
	}
	// [R] rules_plus B1: the script's Borrow() answer is vetoed / reduced (identity while the rules are off)
	if(stop && loanPrincipal(seat) < 1 && rm8PyCallAI(script, "Borrow", seat, r) && r > 0 && (r = rm8RulesLoanDecide(seat, rm8GameCash(seat) + rm8GameDeposit(seat), r)) > 0){
		loanTake(seat, r);
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: seat %d borrows %d (cash %d deposit %d)", seat, r, rm8GameCash(seat), rm8GameDeposit(seat));
		queueText(seat, plainFmt(253, name.c_str(), (int)r));	// str 253 "%s<str#253>%d元"
		rm8TurnEnqueue(seat, 0x47, 0, 0);
		return true;
	}
	if(!rm8PyCallAI(script, "Access", seat, r) || r == 0) return false;
	if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: seat %d bank access %d", seat, r);
	if(r < 0){ r = -r; moneyDeposit(seat, r); queueText(seat, plainFmt(251, name.c_str(), (int)r)); }	// str 251 "%s<str#251>%d元"
	else { moneyTakeOut(seat, r); queueText(seat, plainFmt(252, name.c_str(), (int)r)); }		// str 252 "%s<str#252>%d元"
	rm8TurnEnqueue(seat, 0x47, 0, 0);
	if(!stop){ rm8TurnEnqueue(seat, 0x26, 0, 0); rm8TurnSetState(seat, 8); }	// resume the walk
	return true;
}

}	// namespace ev

int32_t rm8EventLoan(int seat){ return ev::loanPrincipal(seat); }	// [N] int32_t
int rm8EventLoanDaysLeft(int seat){ return ev::loanDaysLeft(seat); }
// [R] the only caller is the story script (大老千 day 1): the loan is marked as story-scripted, the house rules (rules_plus: gate, early repayment, reserve) never touch it
void rm8EventSetLoan(int seat, int32_t amount){ bool had = ev::loanPrincipal(seat) != 0; ev::loanRecord(seat, amount); if(!had && ev::loanPrincipal(seat) != 0) rm8RulesNoteStoryLoan(seat); }	// [N] int32_t

// [V] Role_CheckLoanDueAndForceRepay @0x483170: the human gets the warning 5 / 3 / 1 days before the due date (str 5423); then the
// monthly interest / the forced repayment: str 5421 (interest) or 5422 (principal + interest, loan cleared) and message 0x4E
void rm8EventTurnStart(int seat)
{
	using namespace ev;
	if(loanPrincipal(seat) < 1) return;
	if(rm8TurnHuman(seat)){
		int days = loanDaysLeft(seat);
		if(days == 5 || days == 3 || days == 1) queueText(seat, plainFmt(5423, rm8TurnName(seat), days));
	}
	int32_t due = loanRollback(seat);	// [N] int32_t
	if(due < 1) return;
	if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: seat %d loan payment %d (principal left %d, cash %d deposit %d)", seat, due, loanPrincipal(seat), rm8GameCash(seat), rm8GameDeposit(seat));
	queueText(seat, plainFmt(loanPrincipal(seat) < 1 ? 5422 : 5421, rm8TurnName(seat), (int)due));
	rm8TurnEnqueue(seat, 0x4E, (int)due, 0);
}

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_bank.inc"
