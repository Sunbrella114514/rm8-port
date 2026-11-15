// Power company (event 0xF, powerCo.ui) and ticket center (event 0x10, ticketCenter.ui), after the task objects 0x4B33E0 (vtable 0x5E48A8;
// vf16 CUi_PowerCo__vf16_OnLoad @0x4d8720, Update @0x4d9180, buy click PowerCo_CanBuyStock @0x4d83d0, purchase PowerCo_ShowPurchaseDialog @0x4d9d00 +
// PowerCo_TryBuyStock @0x4da790, quit vf11 @0x4d9bd0) and 0x4B3610 (vtable 0x5E48F0; vf16 CUi_ticketCenter__vf16__Init @0x4dda70, Update @0x4de640,
// TicketCenter_ShowConsume @0x4df7d0, TicketCenter_BuyTickets @0x4df130, TicketCenter_CheckBudget @0x4dff50, TicketCenter_CalcMultiplier @0x4e0270,
// TicketCenter_PaymentDivide @0x4e0340); computer paths PowerCo_PayElectricity @0x4fc150 and TicketCenter_PayTickets @0x4fc490.
// [V] both companies keep four share fractions (one per seat, +0..+0xC; the seat-sized float array of the manager DAT_006403cc / DAT_006403c8), the
// chairman (+0x10: the seat with the largest share, PowerCo_SelectBestStock @0x4da930) and, for the ticket center, the consumption level (+0x14).
// A visitor who is not the chairman pays: power company Role_CalcOwnRentSumTenPercent @0x48c630 = (sum of own rents / price index / 100) * 10 in
// cash, paid out by PowerCo_PaymentDivide @0x4daaa0; ticket center die * level tickets (level from the held share, CalcMultiplier). Anybody can buy 10 %
// of the shares for price index * 5000 from the deposit while shares are left (remaining = 1 - sum, GetCurrentPrice @0x4da760).
// [V] PowerCo_PaymentDivide: every active role whose share * amount rounds to > 0 is paid the *whole* amount into the deposit (the code adds param_1, not
// the share: kept as is); the paying visitor itself gets ftol(amount * its share) through message 0x5c. TicketCenter_PaymentDivide pays ftol(amount * share).
// [A] not drawn: the stock bars use plain rectangles, the share percent text replaces the J209_4 digit pictures [H: ids 821..829 = 10..90 %, 830 = 100 %, 887 = 0];
// the ticket center die is a 2D picture (the original starts the single-die scene Role_StartDiceSceneCount1 @0x484d70 and reads its result).
#include "event_int.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

namespace ev {

namespace {

struct Company {
	float share[8] = {};
	int chairman = -1;
	int level = 10;
	bool power = true;
	float remaining() const { float r = 1.0f; for(int i = 0; i < 4 && i < 8; i++) r -= share[i]; return r; }	// [V] +0x4da760: 1 - the four floats
	// [V] PowerCo_SelectBestStock @0x4da930: nobody holds anything -> no chairman; otherwise the largest share (a later seat only when strictly larger)
	void selectBest()
	{
		if(fabsf(remaining() - 1.0f) < 1e-4f) chairman = -1;
		if(chairman >= 0) for(int i = 0; i < 4; i++) if(share[chairman] < share[i]) chairman = i;
	}
	// [V] PowerCo_TryBuyStock @0x4da790 / TicketCenter_CheckBudget @0x4dff50: returns the cost, 0 when refused
	long tryBuy(int seat, float amount)
	{
		if(remaining() <= 0.0f) return 0;
		long cost = rm8GamePI() * 5000;
		if(cost >= rm8GameDeposit(seat)) return 0;
		moneyWithdraw(seat, cost, 1);
		share[seat] += amount;
		if(fabsf(remaining() - 0.9f) < 1e-4f) chairman = seat;	// the first purchase makes the buyer chairman
		else selectBest();
		return cost;
	}
	void clear(){ for(float &f : share) f = 0; chairman = -1; level = 10; }
};
Company g_pw, g_tc;

void init(){ static bool done = false; if(!done){ done = true; g_pw.power = true; g_tc.power = false; } }

// [V] Role_CalcOwnRentSumTenPercent @0x48c630
long rentTenPercent(int seat)
{
	long sum = 0, pi = std::max(1L, rm8GamePI());
	for(int t = 0; t < rm8GameTerraCount(); t++) if(rm8GameTerraOwner(t) == seat){ sum = std::min(999999999L, sum + rm8GameTerraRent(t, true) / pi); }
	return (sum / 100) * 10;
}

// [V] TicketCenter_CalcMultiplier @0x4e0270 (the level stays when nothing is left): +0x14 = 10 / 20 / 30 / 40 / 50 (the decompile prints these ints as the
// denormal floats 1.4013e-44 .. 7.00649e-44 = bit patterns 10 .. 50; it was read as 1..5 before, the fee was a tenth of the original's)
int multiplier(Company &c, int die)
{
	float f = 0; for(int i = 0; i < 4; i++) f += c.share[i];
	if(f > 0.2f){ if(f > 0.4f){ if(f > 0.6f){ if(f > 0.8f){ if(f < 1.0f) c.level = 50; } else c.level = 40; } else c.level = 30; } else c.level = 20; } else c.level = 10;
	return die * c.level;
}

// [V] PowerCo_PaymentDivide @0x4daaa0 (power) / TicketCenter_PaymentDivide @0x4e0340 (tickets)
void paymentDivide(Company &c, int cur, long amount)
{
	for(int s = 0; s < rm8TurnRoleCount() && s < 4; s++){
		if(rm8TurnStatus(s) == 7) continue;
		long part = (long)(amount * c.share[s]);
		if(part <= 0) continue;
		if(c.power){
			if(s == cur) rm8TurnEnqueue(s, 0x5c, (int)part, 1);			// Role_AddMoney(part, deposit)
			else moneyAdd(s, amount, 1);							// [V] the whole amount, into the deposit
		}else{
			if(s == cur) rm8TurnEnqueue(s, 7, (int)part, 1);				// Role_AddTicket
			else addTickets(s, part);
		}
	}
}

struct CoTask : Task {
	Company &c;
	bool power;
	float timer = 0;
	int state = 0;			// [V] +0x30 (power) / +0x34 (ticket center)
	int chairmanAtStart = -1;	// [V] +0x38
	int voice = 0, die = 0;
	Npc *npc = nullptr;
	std::vector<int> bar, num, pic, nam;	// dynamic widgets per active role
	std::vector<int> seats;
	int tmpVisible = 0;
	bool busy(){ return voice > 0 && rm8SndPlaying(voice); }
	void say(const std::string &line, bool play = true)
	{
		int snd = 0; std::string t = plain(line, &snd);
		rm8UiSetTextByName(ui, "bobo", wrap(t, power ? 20 : 10));
		if(play){ rm8SndStop(voice); voice = snd ? rm8SndPlay(snd) : 0; }
	}
	void say(int id){ say(rm8Str(id)); }

	CoTask(int s, Company &co) : c(co), power(co.power)
	{
		seat = s;
		ui = rm8UiLoad(power ? "PowerCo" : "ticketCenter", RM8UI_DIALOG);
		if(!ui) return;
		timer = 1.6f * waitScale();
		for(int k = 0; k < rm8TurnRoleCount(); k++) if(rm8TurnStatus(k) != 7 && k < 4) seats.push_back(k);
		const char *plane = power ? "powerCoPlane" : "ticketCenterPlane";
		(void)plane;
		float PW = power ? 417 : 404;
		float planeX = power ? 198 : 231, planeY = power ? 89 : 63;
		int n = (int)seats.size();
		for(int k = 0; k < n; k++){
			int r = seats[k];
			float x = planeX + (k * PW) / n - 32 + (PW / n) / 2;
			float py = power ? 200.0f : 190.0f, ny = power ? 264.0f : 254.0f;	// [V] RolePic y 200 / 0xbe, RoleName y 0x108 / 0xfe (plane coordinates)
			bar.push_back(rm8UiAddWidget(ui, "plane", 0, x, planeY + py, 64, 0, -1, 0xffff8000, 16, 8));
			num.push_back(rm8UiAddWidget(ui, "label", 0, x, planeY + py, 64, 22, -1, 0, 16, 9));
			pic.push_back(rm8UiAddWidget(ui, "label", 0, x, planeY + py, 64, 64, -1, 0, 16, 9));
			nam.push_back(rm8UiAddWidget(ui, "label", 0, x, planeY + ny, 64, 32, -1, 0, 14, 9));
			rm8UiSetPicFile(ui, pic.back(), std::string("prt\\npc\\") + rm8TurnPic(r) + "_12.tga");
			rm8UiSetText(ui, nam.back(), std::string(rm8TurnName(r)) + "\\" + std::to_string(r + 1) + "P");
			rm8UiSetIgnore(ui, bar.back(), true); rm8UiSetIgnore(ui, num.back(), true); rm8UiSetIgnore(ui, pic.back(), true); rm8UiSetIgnore(ui, nam.back(), true);
		}
		refreshBars();
		rm8UiSetEnabledN(ui, power ? "Buy" : "buy", false);
		rm8UiSetEnabledN(ui, power ? "quit" : "quit", false);
		if(!power) showPlane(false);
		uiTapOnly(ui, {power ? "Buy" : "buy", "quit", "Temp"});
		rm8UiSetVisibleN(ui, "Temp", false); rm8UiSetTextByName(ui, "Temp", "");
		npc = npcCreate(power ? 0x416 : 0x414, ui, "NPC"); npcPlay(npc, 0);
		say(power ? 4401 : 4201);
		chairmanAtStart = c.chairman;
		rm8UiOnClickName(ui, [this](const std::string &nm){ click(nm); });
		rm8UiShow(ui, true);
	}
	~CoTask() override
	{
		hideDieGo(this); npcDestroy(npc); if(voice) rm8SndStop(voice);
		for(auto *v : {&bar, &num, &pic, &nam}) for(int id : *v) rm8UiSetVisible(ui, id, false);
		rm8UiShow(ui, false); rm8UiOnClickName(ui, nullptr);
	}
	bool ok() const { return ui != nullptr; }
	bool ready() const override { return state == 2 || (!power && state == 10); }

	// [V] the share table (portraits, names, bars, percentages) are children of ticketCenterPlane (the loader creates them under it): they show / hide with it - during the
	// dice throw screen (state 10) the plane is hidden and nothing of the table may remain
	void showPlane(bool v)
	{
		if(power) return;
		rm8UiSetVisibleN(ui, "ticketCenterPlane", v);
		for(auto *vec : {&bar, &num, &pic, &nam}) for(int id : *vec) rm8UiSetVisible(ui, id, v);
	}
	void refreshBars()
	{
		for(size_t k = 0; k < seats.size(); k++){
			float s = c.share[seats[k]];
			char t[16]; snprintf(t, sizeof t, "%d%%", (int)lroundf(s * 100));
			rm8UiSetText(ui, num[k], s > 0 ? t : "");
		}
		// positions: recomputed in place (x kept from the creation order)
		float PW = power ? 417 : 404, planeX = power ? 198 : 231, py = power ? 89 : 63;
		int n = (int)seats.size();
		for(int k = 0; k < n; k++){
			float x = planeX + (k * PW) / n - 32 + (PW / n) / 2, bh = 130.0f * c.share[seats[k]], base = py + (power ? 200.0f : 190.0f);
			rm8UiSetRect(ui, bar[k], x, base - bh, 64, bh);	// [A] the bar grows upwards from the portrait, 130 px = 100 %
			rm8UiSetRect(ui, num[k], x, base - bh - 22, 64, 22);
		}
		char b[16]; snprintf(b, sizeof b, "%d%%", (int)lroundf(c.remaining() * 100));
		rm8UiSetTextByName(ui, "leftPercent", b);
	}

	// [V] CUi_PowerCo__vf11_OnCancel @0x4d9bd0 / ticket center vf11 @0x4dd9f0: while shopping (states 2..4) -> state 5
	// [A] the decompiled OnExit goes straight to state 5; the goodbye (state 4: str 4407 / 4209 with its voice and the presenter's action) is played first, as the original is heard to do
	void quit(){ hideDieGo(this); if(state > 1 && state < 5){ rm8SndStop(voice); voice = 0; state = 4; timer = 0.2f * waitScale(); } }

	void buy()	// [V] PowerCo_ShowPurchaseDialog @0x4d9d00 / TicketCenter_BuyTickets @0x4df130
	{
		rm8SndStop(voice); voice = 0;
		long cost = c.tryBuy(seat, 0.1f);
		if(cost == 0){ say(power ? 4424 : 4226); npcPlay(npc, power ? 6 : 8); state = 4; return; }	// "deposit too low"
		say(plainFmt(power ? 4405 : 4207, (int)cost));
		npcPlay(npc, power ? 4 : 6);
		refreshBars();
		state = 0x14;
		timer = 1.6f * waitScale();
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: %s: seat %d buys 10%% for %ld (share %.1f, chairman %d)", power ? "power company" : "ticket center", seat, cost, c.share[seat], c.chairman);
	}

	void click(const std::string &n) override
	{
		if(n == "quit") quit();
		else if((n == "Buy" || n == "buy") && state == 2) buy();
		else if((n == "Temp" || n == "go") && state == 10){
			hideDieGo(this);
			if(rm8TurnEventDieStart(seat)){	// [V] Ui_OnEvent_StartDiceScene @0x4e8a80 -> Role_StartDiceSceneCount1: the real 3D die; [A] the dialog is hidden while it rolls
				rm8SndStop(voice); voice = 0; state = 30; timer = 0;
				return;
			}
			rollDie();
		}
	}

	void rollDie(int v = 0)	// the single-die scene result (vf12 OnTimer @0x4de450): state 0xb
	{
		die = v > 0 ? v : rnd() % 6 + 1;
		showConsume();
	}
	void showConsume()	// [V] TicketCenter_ShowConsume @0x4df7d0
	{
		int pay = multiplier(c, die);
		say(plainFmt(4204, die, c.level));
		npcPlay(npc, 3);
		timer = 1.6f * waitScale();
		long have = rm8GamePoints(seat);
		subTickets(seat, pay);							// [V] Role_SubTicketEmoteAny(pay, 0) @0x48a030 called at once (no emote): +0x324 - pay, floor 0
		paymentDivide(c, seat, std::min<long>(have, pay));
		consumeAmount = pay;
		state = 0xb;
	}
	int consumeAmount = 0;

	bool update(float dt) override
	{
		if(state == 30){	// the die rolls (roles.cpp): its value arrives with the role back in state 9
			int v = rm8TurnEventDieTake(seat);
			if(v){ rm8UiShow(ui, true); rollDie(v); }
			return false;
		}
		timer -= dt;
		{ static float dd = 0; if(getenv("RM8_EVENT_TESTDBG") && (dd += dt) > 1){ dd = 0; rm8Log("events: ticket wait state %d timer %.2f voice %d busy %d", state, timer, voice, (int)busy()); } }
		if(timer > 0 || busy()) return false;
		timer = 1.6f * waitScale();
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: %s state %d", power ? "power company" : "ticket center", state);
		switch(state){
		case 0:
			state = 1;
			if(c.chairman >= 0 && c.chairman == seat){	// the owner visits: no fee
				say(power ? 4402 : 4202); npcPlay(npc, 1);
				showPlane(true);
			}else if(power){
				long amt = rentTenPercent(seat);
				say(plainFmtV(4403, (int)amt)); npcPlay(npc, 2);
				rm8GameSubMoney(seat, amt, false);		// [V] Role_SubMoney(amt, 0, 0)
				paymentDivide(c, seat, amt);
				if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: power company: seat %d pays %ld", seat, amt);
			}else{
				say(4203); npcPlay(npc, 2);
				showDieGo(this, "Temp");	// [V] the go panel (one die) at the Temp widget
				state = 10;
			}
			break;
		case 0xb:
			say(plainFmt(4205, consumeAmount)); npcPlay(npc, 4);
			rm8UiSetVisibleN(ui, "Temp", false);
			showPlane(true);
			refreshBars();
			state = 1;
			break;
		case 1:
			showPlane(true);
			refreshBars();
			if(c.remaining() <= 0.01f){ state = 4; timer = 0; break; }
			state = 2;
			rm8UiSetEnabledN(ui, power ? "Buy" : "buy", true); rm8UiSetEnabledN(ui, "quit", true);
			npcPlay(npc, power ? 3 : 5);
			say(plainFmtV(power ? 4404 : 4206, (int)(rm8GamePI() * 5000)));
			break;
		case 0x14:	// [V] ownership changed since the start -> the new chairman is announced
			if(c.chairman < 0 || c.chairman == chairmanAtStart){ state = 4; timer = 0; }
			else{ state = 3; timer = 0; }
			break;
		case 3:
			say(plainFmt(power ? 4406 : 4208, rm8TurnName(c.chairman))); npcPlay(npc, power ? 5 : 7);
			state = 4;
			break;
		case 4:
			say(power ? 4407 : 4209); npcPlay(npc, power ? 6 : 8);
			state = 5;
			break;
		case 5: return true;
		}
		return false;
	}
};

}	// namespace

void companyClear(){ init(); g_pw.clear(); g_tc.clear(); }
void testChairman(bool power, int seat){ init(); Company &c = power ? g_pw : g_tc; c.clear(); c.share[seat] = 0.1f; c.chairman = seat; }
long powerChairmanShare(int seat){ return (long)(g_pw.share[seat] * 100); }

Task *makePowerTask(int seat)
{
	init();
	CoTask *t = new CoTask(seat, g_pw);
	if(!t->ok()){ delete t; return nullptr; }
	return t;
}
Task *makeTicketTask(int seat)
{
	init();
	CoTask *t = new CoTask(seat, g_tc);
	if(!t->ok()){ delete t; return nullptr; }
	return t;
}

// [V] PowerCo_PayElectricity @0x4fc150 / TicketCenter_PayTickets @0x4fc490 (always returns 1)
static bool aiCompany(int seat, Company &c)
{
	init();
	int pos = 0;
	const std::string name = rm8TurnName(seat);
	int oldChair = c.chairman;
	if(c.chairman != seat){
		if(c.power){
			long amt = rentTenPercent(seat);
			if(amt > 0){
				paymentDivide(c, seat, amt);
				queueTextAt(seat, pos++, plainFmt(269, name.c_str(), (int)amt));		// "%s<str#151>%d元"
				rm8TurnEnqueueAt(seat, pos++, 0x16, (int)amt, 0);
			}
		}else{
			int amt = multiplier(c, rnd() % 6 + 1);
			if(amt > 0){
				paymentDivide(c, seat, amt);
				queueTextAt(seat, pos++, plainFmt(272, name.c_str(), amt));			// "%s<str#161>%d點"
				rm8TurnEnqueueAt(seat, pos++, 0x4b, amt, 1);
			}
		}
	}
	if(c.remaining() > 0.05f && rm8GameDeposit(seat) >= 20000L * rm8GamePI() && c.tryBuy(seat, 0.1f) > 0){
		queueTextAt(seat, pos++, plainFmt(c.power ? 270 : 273, name.c_str()));			// "%s購買...股票"
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: %s: computer seat %d buys 10%% (shares %.1f %.1f %.1f %.1f, chairman %d)", c.power ? "power company" : "ticket center", seat, c.share[0], c.share[1], c.share[2], c.share[3], c.chairman);
		if(oldChair != seat && c.chairman == seat){
			rm8TurnEnqueueAt(seat, pos++, 0x65, 0x1E, 0);
			queueTextAt(seat, pos++, plainFmt(c.power ? 271 : 274, name.c_str()));		// "%s成為...董事長"
		}
	}
	return true;
}
bool aiPower(int seat){ return aiCompany(seat, g_pw); }
bool aiTicket(int seat){ return aiCompany(seat, g_tc); }

}	// namespace ev

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_power.inc"
