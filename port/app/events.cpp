// Event tasks on the board, after Game_StartEventTask @0x4a4160 / CreateGameObject @0x4b1bc0 / Role_TriggerLandEvent @0x486140.
// Facts marked [V] are read from the decompile (address given); [H] hypothesis; [A] port adaptation. docs/events.md has the table.
//
// [V] Role_TriggerLandEvent(event) @0x486140: event > 0; a computer goes to AI_OnLandEvent @0x4f79c0 (its own switch, below), a human
//     to Game_UseCard(event) @0x4a4160 = CreateGameObject(event) -> task at scene +0x34 (the loader vf16 runs at creation), the role
//     state becomes 9 (CPlayerTag_SetStatusCode9 @0x4860b0). Every frame the scene (CGamePlayScene__Game_OnUpdate @0x4a17b0) calls the
//     task's vf1: 0 = running, bit 31 = finished -> Game_EndTurn @0x4a48d0 (all roles 9 -> 8 through Role_UpdateStatusTransition
//     @0x4860d0, the task is deleted). The base vf1 (@0x407760) returns 0x80000000, so a task without its own Update ends on the
//     first frame after its loader queued messages (park, card point, point spots).
#include "event_int.h"
#include "news.h"
#include "balance.h"
#include "present.h"
#include "boardfx.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <memory>
#include <vector>
#include <algorithm>

namespace ev {

namespace {

std::unique_ptr<Task> g_task;
double g_evClock = 0;	// [A] game time seen by the events (debug logs)
int g_taskArg = -1;	// [A] the argument of Game_UseCard(event, arg): the plot of the auctions (0x50 / 0x51)

// a card shown by the "getcard" dialog (Role_ReceiveCardWithUi @0x488e80): auto-hidden after Role_GetWaitFrames(2) = 60 frames
Rm8Ui *g_getcard = nullptr; float g_getcardT = 0;

}	// namespace

static Task *g_goOwner = nullptr;
void showDieGo(Task *t, const char *widget)
{
	float x, y, w, h;
	if(!t || !t->ui || !rm8UiGetScreenRectN(t->ui, widget, x, y, w, h)) return;
	rm8UiSetVisibleN(t->ui, widget, false);
	g_goOwner = t;
	rm8HudDieGo(true, x, y, [](void *p){ static_cast<Task*>(p)->click("go"); }, t);
}
void hideDieGo(Task *t){ if(g_goOwner == t){ g_goOwner = nullptr; rm8HudDieGo(false, 0, 0, nullptr, nullptr); } }

void uiTapOnly(Rm8Ui *ui, std::initializer_list<const char*> names)
{
	rm8UiSetIgnoreAll(ui, true);
	for(const char *n : names) rm8UiSetIgnoreN(ui, n, false);
}

void queueText(int seat, const std::string &t){ rm8TurnEnqueue(seat, 0x1B, rm8GamePushText(t), 0); }
void queueTextAt(int seat, int pos, const std::string &t){ rm8TurnEnqueueAt(seat, pos, 0x1B, rm8GamePushText(t), 0); }
float waitScale(){ return 1.0f - std::min((rm8GameSpeed - 1) / 10.0f, 1.0f); }
int rnd(){ return rm8GameRand(); }

std::string plain(const std::string &s, int *sound)
{
	size_t at = s.rfind('@');	// the game's string table splits "text@soundId" (the id indexes Sound/snd0000.dat)
	if(at != std::string::npos && at + 1 < s.size() && isdigit((unsigned char)s[at + 1])){
		if(sound) *sound = atoi(s.c_str() + at + 1);
		return s.substr(0, at);
	}
	if(sound) *sound = 0;
	return s;
}

// [A] the game's labels wrap their text at the widget width (GDI); the port's UI library does not, so the bubble texts break lines here:
// width in full-width characters (ASCII counts a half)
std::string wrap(const std::string &s, float width)
{
	std::string out; float w = 0;
	for(size_t i = 0; i < s.size();){
		unsigned char c = (unsigned char)s[i];
		size_t n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
		float cw = c < 0x80 ? 0.5f : 1.0f;
		const char BS = 92;	// the game's line break character
		if(c == BS){ w = 0; out += BS; i++; continue; }
		if(w + cw > width){ out += BS; w = 0; }
		out.append(s, i, n); w += cw; i += n;
	}
	return out;
}

// like plainFmt but the "@soundId" suffix stays, so that say() / bubble() can play the line's voice (plain(line, &snd))
std::string plainFmtV(int id, ...)
{
	std::string f = rm8Str(id);
	for(size_t p = 0; (p = f.find("%S", p)) != std::string::npos; p += 2) f[p + 1] = 's';
	char buf[1024];
	va_list ap; va_start(ap, id); vsnprintf(buf, sizeof buf, f.c_str(), ap); va_end(ap);
	return buf;
}

std::string plainFmt(int id, ...)
{
	std::string f = rm8Str(id);
	for(size_t p = 0; (p = f.find("%S", p)) != std::string::npos; p += 2) f[p + 1] = 's';	// [A] the game's "%S" (5321, 5423) takes a narrow name; MinGW's would read a wide string
	char buf[1024];
	va_list ap; va_start(ap, id); vsnprintf(buf, sizeof buf, f.c_str(), ap); va_end(ap);
	return plain(buf);
}

void addTickets(int seat, long n){ long cur = rm8GamePoints(seat); rm8GameAddPoints(seat, std::min(n, std::max(0L, 9999 - cur))); }
void subTickets(int seat, long n){ rm8GameAddPoints(seat, -n); }	// clamps at 0 in the rules

int cardTotal(){ int n = rm8CardDefCount(); return n > 0 ? n : 49; }
// [V] card.bin level (+0x210) / ticket (+0x214) come from scenes/cards.txt (cards.cpp, loaded in main before the first game starts: docs/cards.md section 1); 5 / 0 for an unknown card
int cardLevel(int id){ int l = rm8CardLevel(id); return l > 0 ? l : 5; }
int cardPrice(int id){ return std::max(0, rm8CardPrice(id)); }
int difficulty(){ static int d = getenv("RM8_DIFFICULTY") ? atoi(getenv("RM8_DIFFICULTY")) : 3; return d; }
bool holdsCard(int seat, int id){ for(int i = 0; i < rm8CardMax(); i++) if(rm8CardAt(seat, i) == id) return true; return false; }

// [V] Card_SelectByDifficulty @0x4ae270: r = rand() % 10; level limit r / 7 + 1 (+ r / 9 for param 0); one random card with level <= limit
int cardSelectByDifficulty(int param)
{
	int r = rnd() % 10, lim = param == 0 ? r / 7 + 1 + r / 9 : r / 7 + 1;
	std::vector<int> c;
	for(int i = 0; i < cardTotal(); i++) if(cardLevel(i + 1) <= lim) c.push_back(i);
	return c.empty() ? -1 : c[rnd() % c.size()];
}

namespace {

// ---- tasks without an Update of their own: the loader queues the messages, vf1 (@0x407760) finishes at once
struct InstantTask : Task {
	bool update(float) override { return true; }
};

// [V] events 10 / 11 / 12 (vtable 0x5E4620, +4 = 0x32 / 0x1E / 10; CreateGameObject @0x4b1bc0): vf16 Cls_5e4620__ShowInfo @0x4bc990
Task *makePointTask(int seat, int ev)
{
	int amount = ev == 10 ? 50 : ev == 11 ? 30 : 10;
	int str = amount == 10 ? 5313 : amount == 30 ? 5312 : 5311;	// [V] +4 == 10 -> 0x14C1, 0x1E -> 0x14C0, else 0x14BF
	queueText(seat, plainFmt(str, rm8TurnName(seat)));		// [V] Role_QueueTextMsg(str "%s得到N點券")
	rm8TurnEnqueue(seat, 7, amount, 0);				// [V] Role_EnqueueMsg(7, amount, 0) -> Role_AddTicket
	return new InstantTask();
}

// [V] event 4 park (vtable 0x5E4668): vf16 Cls_5e4668__vf16_OnRestAtPark @0x4b1890: only the text str 5304 "%s<str#5304>"
Task *makeParkTask(int seat)
{
	queueText(seat, plainFmt(5304, rm8TurnName(seat)));
	return new InstantTask();
}

// [V] event 3 card point (vtable 0x5E46B0): vf16 Cls_5e46b0__vf16_OnGetCard @0x4b1750
Task *makeCardPointTask(int seat)
{
	int idx = cardSelectByDifficulty(0);
	if(idx >= 0){
		queueText(seat, plainFmt(5303, rm8TurnName(seat)));	// str 5303 "%s<str#5303>"
		rm8TurnEnqueue(seat, 0x65, 0x1E, 0);			// wait 30 frames
		rm8TurnEnqueue(seat, 4, idx, 1);			// Role_ReceiveCardWithUi(index, 1)
	}
	return new InstantTask();
}

Task *createTask(int seat, int event)
{
	Task *t = nullptr;
	switch(event){
	case 1: t = makeShopTask(seat); break;
	case 2: t = makeAskTask(seat); break;
	case 6: t = makeLetouTask(seat); break;
	case 0x62: t = makeLetouDrawTask(seat); break;
	case 0xE: t = makeCourtTask(seat); break;
	case 0xD: t = makeMagicTask(seat); break;
	case 0xF: t = makePowerTask(seat); break;
	case 0x10: t = makeTicketTask(seat); break;
	case 3: t = makeCardPointTask(seat); break;
	case 4: t = makeParkTask(seat); break;
	case 5: t = makeBankTask(seat, false); break;
	case 99: t = makeBankTask(seat, true); break;
	case 10: case 11: case 12: t = makePointTask(seat, event); break;
	case 0x11: t = makeNewsTask(seat); break;	// [V] vtable 0x5E20A8 (GameState__Ctor @0x493820, loader vf16 @0x4B4170): the news (news.cpp, docs/news.md)
	case 0x52: t = makeBalanceTask(seat); break;	// [V] vtable 0x5E49C8 (Cls_5e49c8, Game_TriggerNormalEvent @0x4a6620): the monthly balance (balance.cpp, docs/balance.md)
	case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: t = makeMiniTask(seat, event); break;
	case 0x5A: case 0x5B: case 0x5C: t = makeWonderTask(seat, event - 0x5A); break;	// [V] CreateGameObject: MagicHouse_InitContainer(0 / 1 / 2) = vtable 0x5E4938
	case 0x133: t = makeWonderStageTask(seat, g_taskArg); break;
	case 0x134: t = makeBuildingTask(seat, g_taskArg); break;	// [A] internal: the building of the plot g_taskArg	// [A] internal: the building effect of message 0x33
	case 0x50: case 0x51: t = makeVendueTask(seat, g_taskArg, event == 0x51); break;	// [V] CreateGameObject @0x4b1bc0: Wonder_InitContainer(plot, ...) = vtable 0x5E4980
	default: break;	// [A] other events (0x11 news, 0x50.. wonders, stocks ...) belong to other modules / are not implemented: ignored
	}
	if(t){ t->ev = event; t->seat = seat; }
	return t;
}

// [V] Game_UseCard @0x4a4160 (without the stock-event branch): the previous task is cleared, the new one becomes the scene task
bool startTask(int seat, int event)
{
	{ static const bool dbgT = getenv("RM8_DEBUG_EVENTS") != nullptr; if(dbgT) rm8Log("events: [%.2f s] task %d for seat %d", g_evClock, event, seat); }
	g_task.reset();
	Task *t = createTask(seat, event);
	if(!t) return false;
	g_task.reset(t);
	rm8TurnSetState(seat, 9);	// [V] CPlayerTag_SetStatusCode9 @0x4860b0
	return true;
}

// [V] Role_TriggerEvent50or51 @0x487bc0: Game_UseCard(0x50, plot) (0x51 for the bankruptcy kind); false when the task cannot be created
bool startAuction(int seat, int terra, bool bankruptcy)
{
	g_taskArg = terra;
	bool ok = startTask(seat, bankruptcy ? 0x51 : 0x50);
	g_taskArg = -1;
	return ok;
}

// [V] Game_EndTurn @0x4a48d0: every role in state 9 goes back to 8 (Role_UpdateStatusTransition @0x4860d0), the task is deleted
void endTask()
{
	{ static const bool dbgE = getenv("RM8_DEBUG_EVENTS") != nullptr; if(dbgE) rm8Log("events: [%.2f s] task %d ends", g_evClock, g_task ? g_task->ev : -1); }
	g_task.reset();
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(rm8TurnState(s) == 9) rm8TurnSetState(s, 8);
}

// ---- test hooks (documented in docs/events.md): RM8_EVENT_TEST=a,b,c lets seat 0 (the human) land on these events one after the
// other whenever it waits for the dice (bank, bankpass, shop, park, cardpoint, point50, point30, point10); RM8_EVENT_AUTO=n1,n2,..
// presses these widget names of the running task, 0.8 s apart, once its UI is ready
struct TestHook {
	std::vector<std::string> events, autos;
	size_t nextEvent = 0, nextAuto = 0; float t = 0;
	bool init = false;
	void load()
	{
		init = true;
		auto split = [](const char *e, std::vector<std::string> &v){ if(!e) return; std::string s = e, cur; for(char c : s + ","){ if(c == ','){ if(!cur.empty()) v.push_back(cur); cur.clear(); } else cur += c; } };
		split(getenv("RM8_EVENT_TEST"), events); split(getenv("RM8_EVENT_AUTO"), autos);
	}
	void update(float dt)
	{
		if(!init) load();
		if(const char *fa = getenv("RM8_EVENT_FOCUS")){ static float fx = 0, fz = 0; static float at = -1; if(at < 0){ at = (float)(sscanf(fa, "%f,%f,%f", &fx, &fz, &at) == 3 ? at : 0); } if((t += 0) >= 0 && g_evClock >= at){ float p[3] = {fx, 0, fz}; rm8TurnFocus(p); } }	// test hook: from game time `at` on the camera of a RM8_SHOT_FOLLOW shot stays on (x, z)
		if(events.empty() && autos.empty()) return;
		if(g_task){
			if(g_task->ready() && nextAuto < autos.size() && (t += dt) >= 0.8f){ t = 0; std::string n = autos[nextAuto++]; rm8Log("event test: [%.2f s] press %s", g_evClock, n.c_str());
				float x, y, w, h; int wid = g_task->idOf(n);
				bool goBtn = n == "go" && rm8HudDieGoRect(x, y, w, h);	// the die panel of the event dialogs (hud.cpp)
				static const bool ptr = getenv("RM8_EVENT_CLICK") == nullptr;	// default: a real touch (down + up on the widget centre); RM8_EVENT_CLICK=1 calls the task directly
				bool have = goBtn || (ptr && g_task->ui && (wid >= 0 ? rm8UiGetScreenRect(g_task->ui, wid, x, y, w, h) : rm8UiGetScreenRectN(g_task->ui, n.c_str(), x, y, w, h)));
				if(have){ rm8Log("event test: tap at %.0f,%.0f hits %s", x + w / 2, y + h / 2, rm8UiHitName(x + w / 2, y + h / 2).c_str()); rm8UiPointerDown(x + w / 2, y + h / 2, true); rm8UiPointerUp(x + w / 2, y + h / 2); }
				else g_task->click(n); }
			return;
		}
		static float dbgT = 0; if(getenv("RM8_EVENT_TESTDBG") && (dbgT += dt) > 0.15f){ dbgT = 0; rm8Log("event test: cur %d state0 %d human %d task %d status0 %d", rm8TurnCurrent(), rm8TurnState(0), (int)rm8TurnHuman(0), (int)g_task.operator bool(), rm8TurnStatus(0)); }
		if(nextEvent >= events.size() || rm8TurnCurrent() != 0 || rm8TurnState(0) != 6 || !rm8TurnHuman(0)) return;
		static const struct { const char *n; int ev; } T[] = {{"bank", 5}, {"bankpass", 99}, {"shop", 1}, {"ask", 2}, {"court", 0xE}, {"lottery", 6}, {"draw", 0x62}, {"magic", 0xD}, {"power", 0xF}, {"ticket", 0x10}, {"park", 4}, {"cardpoint", 3}, {"point50", 10}, {"point30", 11}, {"point10", 12}, {"mini18", 0x12}, {"mini19", 0x13}, {"mini20", 0x14}, {"mini21", 0x15}, {"mini22", 0x16}, {"mini23", 0x17}, {"auction", 0x50}, {"bauction", 0x51}, {"wonder0", 0x5A}, {"wonder1", 0x5B}, {"wonder2", 0x5C}};
		std::string n = events[nextEvent++];
		if(const char *pts = getenv("RM8_EVENT_POINTS")) rm8GameAddPoints(0, atol(pts));	// test: extra points for the human
		if(n == "puzzle"){	// test: seat 0 is given puzzle piece RM8_EVENT_PIECE (default 3) through message 0x58 (npc_act.cpp -> collect.cpp)
			rm8TurnEnqueue(0, 0x58, getenv("RM8_EVENT_PIECE") ? atoi(getenv("RM8_EVENT_PIECE")) : 3, 0); rm8TurnSetState(0, 8);
			return;
		}
		if(n == "storypark" || n == "storymonster"){	// test: the monster (message 0x31, the story's OnParkEvent) hits the first residential plot, which gets a house and the owner seat RM8_MONSTER_OWNER (default 0, the hero)
			int tt = -1;
			for(int k = 0; k < rm8GameTerraCount() && tt < 0; k++) if(rm8GameTerraType(k) == 1) tt = k;
			if(tt >= 0){
				rm8GameSetTerra(tt, getenv("RM8_MONSTER_OWNER") ? atoi(getenv("RM8_MONSTER_OWNER")) : 0, 1, 1);
				rm8Log("event test: [%.2f s] the monster hits plot %d", g_evClock, tt);
				rm8TurnEnqueue(0, 0x31, tt, 0); rm8TurnSetState(0, 8);
			}
			return;
		}
		if(n == "storymeet"){	// test: seat RM8_MEET_SEAT (default 1) is put on the node of seat 0 and seat 0 stops there (CPlayerTag::OnStopAtTerra: the meeting dialogue, 錢夫人 meeting 沙隆巴斯 on map 5)
			int ms = getenv("RM8_MEET_SEAT") ? atoi(getenv("RM8_MEET_SEAT")) : 1, nd = rm8TurnNode(0);
			if(ms > 0 && ms < rm8TurnRoleCount()){
				int nb[8]; int c = rm8TurnLinks(nd, nb, 8);
				rm8Log("event test: [%.2f s] seat %d meets seat 0 on node %d", g_evClock, ms, nd);
				rm8TurnPlaceAt(ms, nd, c > 0 ? nb[0] : nd); rm8PresentOnStop(0); rm8TurnSetState(0, 8);
			}
			return;
		}
		if(n == "ticketland" || n == "powerland"){	// test: seat 0 stands one step before the company's road node (RM8_FORCE_DIE=1 + RM8_AUTO_ROLL=1: it lands on it through the real turn flow)
			int ev = n == "ticketland" ? 0x10 : 0xF, nd = -1;
			for(int q = 0; q < rm8GameNodeCount() && nd < 0; q++) if(rm8GameNodeEvent(q) == ev) nd = q;
			if(nd >= 0){
				int nb[8]; int c = rm8TurnLinks(nd, nb, 8);
				if(getenv("RM8_EVENT_CHAIR")) testChairman(ev == 0xF, 0);	// RM8_EVENT_CHAIR=1: seat 0 holds 10 % already (otherwise it buys through the dialog)
				rm8Log("event test: [%.2f s] seat 0 stands next to the %s node %d", g_evClock, n == "ticketland" ? "ticket center" : "power company", nd);
				if(c > 0) rm8TurnPlaceAt(0, nb[0], nd);
			}
			return;
		}
		if(n == "hotelwalk"){	// test: seat 0 stands on the node of a hotel plot and walks into it (message 0x13: roles.cpp state 0xb); the next turn start ends the stay (0x14: the walk out)
			int tt = -1, nd = -1;
			for(int k = 0; k < rm8GameTerraCount() && tt < 0; k++) if(rm8GameTerraType(k) == 2) for(int q = 0; q < rm8GameNodeCount(); q++) if(rm8GameNodeTerra(q) == k){ tt = k; nd = q; break; }
			if(tt >= 0){
				int nb[8]; rm8GameSetTerra(tt, 1, 3, 4); int c = rm8TurnLinks(nd, nb, 8);
				rm8Log("event test: [%.2f s] seat 0 walks into the hotel of plot %d (node %d)", g_evClock, tt, nd);
				rm8TurnPlaceAt(0, nd, c > 0 ? nb[0] : nd); rm8TurnEnqueue(0, 0x13, 0, 0); if(getenv("RM8_HOTEL_QUICK")){ rm8TurnEnqueue(0, 0x65, 60, 0); rm8TurnEnqueue(0, 0x14, 0, 0); }	// RM8_HOTEL_QUICK=1: the walk out follows after one second
				rm8TurnSetState(0, 8);
			}
			return;
		}
		if(n == "market" || n == "hotel" || n == "institute"){	// test: a commercial plot gets that building (owner seat 1, seat 0 for the institute; level RM8_EVENT_LEVEL or 3) and seat 0 enters it
			int kind = n == "market" ? 3 : n == "hotel" ? 4 : 2, tt = -1;
			for(int k = 0; k < rm8GameTerraCount() && tt < 0; k++) if(rm8GameTerraType(k) == 2) tt = k;
			if(tt >= 0){
				rm8GameSetTerra(tt, kind == 2 ? 0 : 1, getenv("RM8_EVENT_LEVEL") ? atoi(getenv("RM8_EVENT_LEVEL")) : 3, kind);
				rm8Log("event test: seat 0 enters the %s on plot %d", n.c_str(), tt);
				if(getenv("RM8_EVENT_AT")){	// test: seat 0 stands on the plot's node (the real flow: the camera and the die are at the role)
					for(int q = 0; q < rm8GameNodeCount(); q++) if(rm8GameNodeTerra(q) == tt){ int nb[8]; int c = rm8TurnLinks(q, nb, 8); rm8TurnPlaceAt(0, q, c > 0 ? nb[0] : q); break; }
				}
				rm8EventBuilding(0, tt); rm8TurnSetState(0, 9); nextAuto = 0; t = 0;
			}
			return;
		}
		for(auto &e : T) if(n == e.n){ rm8Log("event test: seat 0 lands on %s (event %d)", e.n, e.ev); int arg = rm8TurnNode(0);
			if(e.ev == 0x50 || e.ev == 0x51){	// test: the plot to sell = RM8_EVENT_TERRA or the first plot without an owner (the argument of Game_UseCard is the plot, not a node)
				arg = getenv("RM8_EVENT_TERRA") ? atoi(getenv("RM8_EVENT_TERRA")) : 0;
				if(!getenv("RM8_EVENT_TERRA")) for(int k = 0; k < rm8GameTerraCount(); k++) if(rm8GameTerraOwner(k) < 0 && rm8GameTerraLevel(k) > 0){ arg = k; break; }
			}
			rm8EventLand(0, e.ev, arg); nextAuto = 0; t = 0; }
	}
} g_test;

// ---- queued messages
// [V] CUi_getcard: card name in label 1, picture "card\<texName>" (Dxx.tga) in label 2, shown for 60 frames; 0x65(60) at the queue front
// (Role_ReceiveCardWithUi @0x488e80, Role_ShowAndRemoveCardInSlot @0x489220; the hide message that follows is a timer here)
void showGetCard(int seat, int id)
{
	if(!g_getcard) g_getcard = rm8UiLoad("getcard", RM8UI_DIALOG);
	if(!g_getcard) return;
	char key[64]; snprintf(key, sizeof key, "prt\\card\\D%02d.tga", id);
	rm8UiSetText(g_getcard, 1, rm8CardName(id) ? rm8CardName(id) : "");
	rm8UiSetPicFile(g_getcard, 2, key);
	rm8UiSetVisible(g_getcard, 3, false);
	rm8UiShow(g_getcard, true);
	g_getcardT = 60.0f / 60.0f * waitScale();
	rm8TurnEnqueueAt(seat, 0, 0x65, 60, 0);
}
// [V] Role_ReceiveCardWithUi @0x488e80 (card index a; b != 0 allows the emote 70% [not shown: expressions are not in the port yet]).
// A computer with a full hand replaces its cheapest card when that one is cheaper than the new card; [A] a human with a full hand
// is handled by rm8CardsBeginSwap (cards.cpp: the getcard window and the swap, Cls_5e24f8).
void receiveCard(int seat, int idx, int allowEmote)
{
	if(idx < 0) idx = cardSelectByDifficulty(0);
	int id = idx + 1;				// [H] card index in the card array = Id - 1 (card.bin Ids are 1..49 without gaps)
	if(id < 1 || id > cardTotal()) return;
	if(rm8CardsBeginSwap(seat, id, allowEmote)) return;	// [V] a human with a full hand: Card_LoadAndShowCard + the swap by a click on the card bar (cards.cpp)
	if(rm8CardCount(seat) >= rm8CardMax()){
		int low = cardPrice(id), slot = -1;	// [V] the lowest ticket price, strictly below the new card's
		for(int i = 0; i < rm8CardMax(); i++){ int c = rm8CardAt(seat, i); if(c && cardPrice(c) < low){ low = cardPrice(c); slot = i; } }
		if(slot >= 0) rm8CardRemoveAt(seat, slot);
	}
	// [V] Role_ReceiveCardWithUi @0x488e80: the emote needs b != 0, no further 0x04 queued right behind (the blessing gives two cards: only the
	// last one can emote) and the 70 % chance (FUN_005c5802() % 100 < 0x46)
	bool emote = allowEmote != 0 && !rm8TurnQueueHas(seat, 4, true) && rm8PresentChance70();
	showGetCard(seat, id);
	// [V] class 1 + rand % 2 at index 2 (our index 1: the hide message is a timer here) instead of the 0x65(30) wait
	if(emote) rm8RoleSayAt(seat, 1, 1 + (rnd() & 1)); else rm8TurnEnqueueAt(seat, 1, 0x65, 30, 0);
	if(!rm8CardAdd(seat, id)) rm8Log("events: card %d not added (hand full)", id);
	else rm8Log("events: seat %d receives card %d", seat, id);
	rm8SndPlay(0x7C);	// [V] Role_ReceiveCardWithUi @0x488e80: CAudioMgr_PlaySample(0x7C) after Deck_AddCardByID (humans and computers, whether or not the card was added)
}

}	// namespace

}	// namespace ev

using namespace ev;

bool rm8EventLand(int seat, int event, int node)
{
	static int remapFrom = -1, remapTo = -1;	// test hook RM8_EVENT_REMAP=from:to turns every landing on event `from` into `to` (all-computer runs of rare events)
	static bool remapInit = false;
	if(!remapInit){ remapInit = true; if(const char *e = getenv("RM8_EVENT_REMAP")) sscanf(e, "%d:%d", &remapFrom, &remapTo); }
	if(event == remapFrom) event = remapTo;
	static const bool dbg = getenv("RM8_DEBUG_EVENTS") != nullptr;
	if(dbg) rm8Log("events: [%.2f s] seat %d (%s) lands on event %d at node %d: cash %ld deposit %ld points %ld cards %d", g_evClock, seat, rm8TurnHuman(seat) ? "human" : "computer", event, node, rm8GameCash(seat), rm8GameDeposit(seat), rm8GamePoints(seat), rm8CardCount(seat));
	if(event <= 0) return false;
	if(event == 0x50 || event == 0x51) return startAuction(seat, node, event == 0x51);	// [V] Game_UseCard(0x50 / 0x51, plot): the same task for a computer and a human (node = the plot here)
	if(!rm8TurnHuman(seat)){
		// [V] AI_OnLandEvent @0x4f79c0: 1 Card_BuySell, 2 AI_Event_Question, 5 AI_Event_Bank(1), 6 Lottery_BuyTicket, 0xD..0x10 and
		// 0x5E.. their AI actions, 0x12..0x17 MiniGame_ExecuteGame (falls through), 99 AI_Event_Bank(0); the default (3, 4, 10..12 ...)
		// starts the same task as for a human (Game_UseCard) and sets state 9
		switch(event){
		case 1: return aiCardShop(seat);
		case 2: return aiQuestion(seat);
		case 0xE: return aiCourt(seat);
		case 6: return aiLottery(seat);
		case 0xD: return aiMagic(seat);
		case 0xF: return aiPower(seat);
		case 0x10: return aiTicket(seat);
		case 5: return aiBank(seat, true);
		case 99: return aiBank(seat, false);
		case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
			if(aiMiniGame(seat, event)) return true;
			break;	// [V] 0x15 with a human in the game: falls through to the task
		default: break;
		}
	}
	return startTask(seat, event);
}

bool rm8EventBuilding(int seat, int terra){ g_taskArg = terra; bool ok = startTask(seat, 0x134); g_taskArg = -1; return ok; }
void rm8EventInstituteTick(){ instituteTick(); }

bool rm8EventDaily(int mday)
{
	if((mday == 1 || rm8BalanceForced()) && !g_task){	// [V] Game_OnTurnStart @0x4a3b50: the 1st -> Game_TriggerNormalEvent @0x4a6620 = Game_UseCard(0x52, 0) for the current role (the monthly balance)
		int cur = -1;	// the role Game_TurnNextRole settles on: the first one that is not bankrupt (rm8GameOnNewRound runs while the turn index wraps to 0)
		for(int s = 0; s < rm8TurnRoleCount() && cur < 0; s++) if(rm8TurnStatus(s) != 7) cur = s;
		return cur >= 0 && startTask(cur, 0x52);
	}
	if(mday == 15 && lotteryDrawDue() && !g_task){
		int cur = rm8TurnCurrent();
		if(rm8TurnStatus(cur) != 7) return startTask(cur, 0x62);
	}
	return false;
}

bool rm8EventPass(int seat, int node)
{
	(void)node;
	if(rm8GameShape(seat) == 6) return false;	// [V] u_CPlayerTag_UpdateTurnCountdown @0x485a20: not while possessed by the Muddled God (shape 6)
	return rm8EventLand(seat, 99, node);
}

void rm8EventUpdate(float dt)
{
	g_evClock += dt;
	if(g_getcardT > 0 && (g_getcardT -= dt) <= 0) rm8UiShow(g_getcard, false);
	npcUpdate(dt);
	g_test.update(dt);
	if(g_task && g_task->update(dt)) endTask();
}

bool rm8EventRunning(){ return g_task != nullptr; }
bool rm8EventSkip(){ return g_task && g_task->skip(); }
bool rm8EventSkipDialogs(){ if(g_getcardT <= 0) return false; g_getcardT = 0; rm8UiShow(g_getcard, false); return true; }

bool rm8EventLoadData(const std::string &scenesDir){ rm8NewsAuctionHook = [](int seat, int terra){ return startAuction(seat, terra, false); }; wonderSetDir(scenesDir); loadEventArts(scenesDir + "/event_arts.txt"); loadEventRoles(scenesDir + "/roles.txt"); return loadQuestions(scenesDir + "/questions.txt"); }

void rm8EventClear()
{
	g_task.reset();
	if(g_getcard) rm8UiShow(g_getcard, false);
	g_getcardT = 0;
	loanClear();
	lotteryClear();
	companyClear();
	wonderClear();
	instituteClear();
}

bool rm8EventMessage(int seat, int cmd, int a, int b)
{
	static const bool dbg = getenv("RM8_DEBUG_EVENTS") != nullptr;
	if(dbg && (cmd == 7 || cmd == 8)) rm8Log("events: seat %d %s %d points (was %ld)", seat, cmd == 7 ? "gets" : "pays", a, rm8GamePoints(seat));
	switch(cmd){
	case 4: receiveCard(seat, a, b); return true;				// [V] Role_ReceiveCardWithUi(a, b)
	case 5: {								// [V] Role_ShowAndRemoveCardInSlot(a) (a < 8) / Role_QueueRemoveRandomCards(b) @0x489470
		if((unsigned)a < 8){	// [V] @0x489220: the card window (60 frames), the card goes, then 70 % (and no other 0x05 queued) class 4 + rand % 2 at the queue end
			int id = rm8CardAt(seat, a);
			if(id) showGetCard(seat, id);
			if(dbg) rm8Log("events: seat %d loses card %d (slot %d)", seat, id, a);
			if(rm8CardRemoveAt(seat, a) && !rm8TurnQueueHas(seat, 5, false) && rm8PresentChance70()) rm8RoleSay(seat, 4 + (rnd() & 1));
			return true; }
		// [V] Role_QueueRemoveRandomCards @0x489470: b random filled slots (no repeats), the camera on the role, 0x05(slot) queued at 0, 1, ...
		std::vector<int> filled;
		for(int i = 0; i < rm8CardMax(); i++) if(rm8CardAt(seat, i)) filled.push_back(i);
		float fp[3]; if(rm8RolesGetPos(seat, fp)) rm8TurnFocus(fp);
		for(int n = b, pos = 0; n > 0 && !filled.empty(); n--, pos++){
			int k = rnd() % (int)filled.size();
			rm8TurnEnqueueAt(seat, pos, 5, filled[k], 0);
			filled.erase(filled.begin() + k);
		}
		return true; }
	case 0x16: rm8GameSubMoney(seat, a, b != 0); return true;		// [V] Role_SubMoney(a, account b, emote) @0x489ac0: account 0 cash first, 1 deposit first
	case 0x17: rm8GameAddMoney(seat, a, b != 0); return true;		// [V] Role_AddMoney(a, account b, emote) @0x489b80
	case 7: addTickets(seat, a); return true;				// [V] Role_AddTicket(a, b == 0)
	case 8: subTickets(seat, a); return true;				// [V] Role_SubTicket(a, 1)
	case 0x5d: case 0x5e: bailMessage(seat, cmd, a, b); return true;	// prison bail
	case 0x15: rm8GameSubMoney(seat, a, b != 0); return true;		// [V] Role_SubMoney_NoEmote(a, account b) @0x489a93
	case 0x27: swapMessage(seat, a, b); return true;			// [V] Role_SwapAssetsWithRole
	case 0x35: if(a != 0) return false; bombMessage(seat, a, b); return true;	// [V] Role_StartEffectOnRandomOwnTerras (a == 0)
	case 0x47: return true;							// [V] Role_UpdateUI (the HUD reads the values every frame)
	case 0x4B: subTickets(seat, a); return true;				// [V] Role_SubTicketEmoteAny @0x48a030 (a, emote b: present.cpp): points - a, floor 0 (the AI ticket center queues it; it was never applied before)
	case 0x4E: rm8GameSubMoney(seat, a, true); return true;			// [V] Role_SubMoney(a, 1, 0): deposit first
	case 0x33: { g_taskArg = b; bool ok = startTask(seat, 0x133); g_taskArg = -1; return ok || true; }	// [V] u_Role_StartEventAtStPoint @0x486430 (wonder b): the building effect, role state 9
	case 0x38: case 0x39: {							// [V] Role_CollectMoneyFromAllOthers @0x489c30 / Role_PayMoneyToAllOthers @0x489d50 (amount a, account b): every other role that is not bankrupt pays (0x38) / receives (0x39)
		int n = 0;								// a each (no emote), this role gets / pays a * n; the emote 1 (collect) / 4 (pay) at the front
		for(int s = 0; s < rm8TurnRoleCount(); s++){
			if(s == seat || rm8TurnStatus(s) == 7) continue;
			if(cmd == 0x38) rm8GameSubMoney(s, a, b != 0); else rm8GameAddMoney(s, a, b != 0);
			n++;
		}
		if(cmd == 0x38) rm8GameAddMoney(seat, (long)a * n, b != 0); else rm8GameSubMoney(seat, (long)a * n, b != 0);
		rm8RoleSayAt(seat, 0, cmd == 0x38 ? 1 : 4);
		if(dbg) rm8Log("events: seat %d %s %d x %d", seat, cmd == 0x38 ? "collects" : "pays", a, n);
		return true; }
	case 0x29: case 0x2A: {							// [V] Role_StartEffectOnOwnTerra @0x487a70 (an own plot loses its owner) / Role_StartEffectOnOtherTerra @0x4879b0 (a plot of somebody else / nobody becomes this role's):
		float p[3];								// the plot blinks for 1.6 s (Cls_5e67f8 @0x4f57a0 / Cls_5e6848 @0x4f58d0, boardfx.cpp), then Land_ClearOwner / Land_SetOwner + sample 1; the role is in state 9
		bool take = cmd == 0x2A;
		if(a < 0 || a >= rm8GameTerraCount() || (take ? rm8GameTerraOwner(a) == seat : rm8GameTerraOwner(a) != seat)) return true;
		if(take){
			if(rm8GameTerraPos(a, p)) rm8TurnFocus(p);			// [V] thunk_FUN_0041d850(plot + 0x88): the camera
			auto fin = [a, seat](){ rm8GameSetTerra(a, seat, -1, -1); };
			if(!rm8BoardFxBlink(seat, {a}, 1, fin)) fin();
			rm8TurnEnqueueAt(seat, 0, 0x1e, 0, 0);				// [V] Role_EnqueueMsgAt(0, 0x1E): the camera back to the role afterwards
			rm8RoleSayAt(seat, 1, 2 + (rnd() & 1));				// [V] thunk_FUN_00484930(1, 2 + rand % 2)
		}else{
			// [V] NPC_Angel_Init @0x4f5740 only accepts a plot whose owner index is > 0 (cmp [plot + 0x4C], 0 / jle): a plot of seat 0 is cleared at once without effect, state change or emote
			auto fin = [a](){ rm8GameSetTerra(a, -1, 0, 0); };
			if(rm8GameTerraOwner(a) < 1 || !rm8BoardFxBlink(seat, {a}, 0, fin)){ fin(); return true; }
			rm8RoleSayAt(seat, 0, 5 + (rnd() & 1));				// [V] thunk_FUN_00484930(0, 5 + rand % 2)
		}
		return true; }
	case 0x1A0: if(a >= 0 && a < rm8GameTerraCount()){ rm8GameSetTerra(a, seat, -1, -1); rm8BoardFxLandTaken(); if(dbg) rm8Log("events: seat %d gets plot %d", seat, a); } return true;	// [A] internal: the flicker is over: Land_SetOwner(this role) + sample 1
	case 0x1A1: if(a >= 0 && a < rm8GameTerraCount()){ rm8GameSetTerra(a, -1, -1, -1); if(dbg) rm8Log("events: seat %d loses plot %d", seat, a); } return true;					// [A] internal: Land_ClearOwner
	case 0x13: rm8TurnHotelWalk(seat, true); return true;			// [V] CPlayerTag__EnterHotelStatus @0x4852e0 (0): the role walks into the hotel (state 0xb, roles.cpp), at the end the stay (rm8CardsEnterHotel)
	case 0x14: rm8TurnHotelWalk(seat, false); return true;			// [V] CPlayerTag__LeaveHotelStatus @0x4853a0 (0): the walk out (state 0xc, status 0)
	case 0x40: return startAuction(seat, a, b == 1);			// [V] Role_TriggerEvent50or51 @0x487bc0 (plot, kind 1 = bankruptcy; kind 0 reaches the news hook first)
	case 0x41: {								// [V] Role_PayAndTakeTerra @0x487c20: focus the plot, Role_SubMoney_NoEmote(price, 0) (cash first), Land_SetOwner(this role)
		float p[3]; if(rm8GameTerraPos(a, p)) rm8TurnFocus(p);
		rm8GameSubMoney(seat, b, false);
		rm8GameSetTerra(a, seat, -1, -1);
		return true; }
	}
	(void)b;
	return false;
}

