// The news system, after docs/news.md. [V] = read from the decompile (address given), [H] hypothesis, [A] port adaptation.
//
// Weekly news: Game_TriggerEventByDate @0x4a6780 starts event task 0x11 (Cls_5e20a8, vtable 0x5E20A8, constructor GameState__Ctor @0x493820): the loader
// vf16 @0x4B4170 builds the TV scene (News_InitDialog @0x4b41b0: anchor article 1052 in front of the camera) and, with 1 chance in 5 (rand() % 5 == 1),
// starts the animal mode (art 960 = TV snow, 2 noise samples, 1.8 s) before the broadcast mode (art 922 = colour bars, voice string 4901, 2.6 s);
// then News_SpecialEvent @0x4b4620 picks the news: News_SelectRandomNew @0x4b4850 (table 0x5E4A58, 44 ids) until News_EventSwitch @0x4b4920 accepts it (the
// retries come from the table 0x5E4B58, 38 ids), shows art 900 + id and the caption for 3.8 s, then News_OnClean @0x4b4ee0. All durations x Anim_ApplyDifficultyScale.
// The role-bound news (police, thief, lucky card, chest: News_OnUpdate @0x4b4da0 / News_OnSpecialEventUpdate @0x4b4e40) and the festival news
// (News_EventDispatcher @0x4b7f80, message 0x3C) skip the intro and show their picture at once (mode 3).
#include "news.h"
#include "event_int.h"
#include "events.h"
#include "audio.h"
#include "present.h"
#include "boardfx.h"
#include "npc.h"
#include "stock.h"
#include "cards.h"
#include "story.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <memory>
#include <map>
#include <SDL.h>

using namespace rw;

bool (*rm8NewsAuctionHook)(int seat, int terra) = nullptr;

namespace news {

namespace {

// ======================================================================== data
struct ArtDef { std::string dff, texdir; std::map<int, std::string> anims; };
std::map<int, ArtDef> g_art;
int g_fest[6][32];
bool g_festOk = false;
int g_hist[5] = {-1, 0, 0, 0, 0};	// [V] DAT_00604520: the last picks of News_SelectRandomNew (-1 = the end of the list; the rest is 0 in the exe's .data); a process-wide global
std::vector<int> g_forceWeekly;		// test hook RM8_NEWS_FORCE
size_t g_forceNext = 0;
bool dbg(){ static const bool d = getenv("RM8_DEBUG_NEWS") != nullptr; return d; }
float g_afterT = -1;	// debug: the roles' state is logged 10 s after a news ended (its queued effects have run by then)
void dumpState(const char *tag)
{
	if(!dbg()) return;
	for(int s = 0; s < rm8TurnRoleCount(); s++) rm8Log("news state (%s): seat %d cash %d deposit %d points %d cards %d status %d shape %d", tag, s, rm8GameCash(s), rm8GameDeposit(s), rm8GamePoints(s), rm8CardCount(s), rm8TurnStatus(s), rm8GameShape(s));
}

bool validSeat(int s){ return s >= 0 && s < rm8TurnRoleCount(); }
bool alive(int s){ return validSeat(s) && rm8TurnStatus(s) != 7; }
int rnd(){ return rm8GameRand(); }
float S(){ return ev::waitScale(); }	// [V] Anim_ApplyDifficultyScale @0x47b3b0
struct UfoRun { int seat = 0, days = 0, fx = 0; float t = 0; bool hid = false, skipped = false; };	// the UFO scene task (message 0x5F)
std::vector<UfoRun> g_ufos;

// ======================================================================== 3D models (the anchor, the picture)
struct Model {
	Clump *clump = nullptr;
	HAnimHierarchy *hier = nullptr;
	Matrix base;
	std::map<int, Animation*> anims;
	bool once = true;
	int art = 0;
};

void applyAnim(Model *n, float dt)
{
	if(!n || !n->hier || !n->hier->interpolator->currentAnim) return;
	AnimInterpolator *ip = n->hier->interpolator;
	float step = dt;
	if(n->once) step = std::min(step, std::max(0.0f, ip->currentAnim->duration - ip->currentTime - 1e-4f));
	ip->addTime(step);
	for(int i = 0; i < n->hier->numNodes; i++){
		Frame *f = n->hier->nodeInfo[i].frame;
		if(!f) continue;
		ip->applyCB(&f->matrix, ip->getInterpFrame(i));
		f->updateObjects();
	}
	n->hier->updateMatrices();
}

Model *modelLoad(int art)
{
	auto it = g_art.find(art);
	if(it == g_art.end()){ rm8Log("news: no model for art %d", art); return nullptr; }
	Model *m = new Model; m->art = art;
	m->clump = rm8LoadClump(it->second.dff, it->second.texdir);
	if(!m->clump){ rm8Log("news: cannot load %s", it->second.dff.c_str()); delete m; return nullptr; }
	Frame *f = m->clump->getFrame();
	m->base = f->matrix; m->base.flags = 0;
	m->hier = HAnimHierarchy::find(f);
	if(m->hier) m->hier->attach();
	for(auto &a : it->second.anims){
		Animation *an = rm8LoadAnim(a.second);
		if(an && m->hier && an->getNumNodes() == m->hier->numNodes) m->anims[a.first] = an;
	}
	return m;
}

// [V] Mesh_SetTexture_0047a570(act, loops, 0) = Article_SelectByTypeAndSetProperties: loops 1 = once (holds the last frame), -1 = for ever
void modelPlay(Model *m, int act, int loops)
{
	if(!m || !m->hier) return;
	auto it = m->anims.find(act);
	if(it == m->anims.end() || !it->second){ if(dbg()) rm8Log("news: art %d has no animation %d", m->art, act); return; }
	m->once = loops == 1;
	m->hier->interpolator->setCurrentAnim(it->second);
	applyAnim(m, 0);
	if(dbg()) rm8Log("news: art %d plays animation %d (%.2f s, %s)", m->art, act, it->second->duration, loops == 1 ? "once" : "loop");
}

void modelFree(Model *m)
{
	if(!m) return;
	if(m->clump) m->clump->destroy();
	delete m;
}

// the model's +z side toward the camera (as the event presenters: the paper-doll models show their front from +z), r / u / a = offsets along the camera's
// right / up / at axes, uniform scale
void modelPlace(Model *m, Camera *cam, float r, float u, float a, float scale)
{
	const Matrix *c = cam->getFrame()->getLTM();
	Matrix w;
	w.right = {-c->right.x * scale, -c->right.y * scale, -c->right.z * scale};
	w.up = {c->up.x * scale, c->up.y * scale, c->up.z * scale};
	w.at = {-c->at.x * scale, -c->at.y * scale, -c->at.z * scale};
	w.pos = {c->pos.x + c->right.x * r + c->up.x * u + c->at.x * a, c->pos.y + c->right.y * r + c->up.y * u + c->at.y * a, c->pos.z + c->right.z * r + c->up.z * u + c->at.z * a};
	w.flags = 0; w.pad1 = w.pad2 = w.pad3 = 0;
	Frame *f = m->clump->getFrame();
	f->transform(&m->base, COMBINEREPLACE);
	f->transform(&w, COMBINEPOSTCONCAT);
}

// the scene objects of the running news task: the picture (+4) and the TV anchor (+0xC)
struct Vis { Model *ill = nullptr, *tv = nullptr; } *g_vis = nullptr;
Rm8Ui *g_ui = nullptr;
std::string g_cap;	// the last caption shown (the logs)

Rm8Ui *newsUi(){ if(!g_ui) g_ui = rm8UiLoad("news", RM8UI_DIALOG); return g_ui; }

// [V] News_Container vf+8 (UI_DialogShowMessage @0x4e78b0): creates news.ui if needed (vf+0x18) and sets the text of the widget with ID 1 (the first one: Label2, the caption on the
// banner); News_OnClean: vf+0x1c(-1) hides the UI
void caption(const std::string &t)
{
	g_cap = t;
	Rm8Ui *u = newsUi();
	if(!u) return;
	rm8UiSetTextByName(u, "Label2", ev::wrap(t, 20.0f));	// [A] the label wraps in the original (GDI); 331 px = 20 full-width characters at font 16
	rm8UiShow(u, true);
	if(dbg()) rm8Log("news: caption: %s", t.c_str());
}
void captionHide(){ if(g_ui) rm8UiShow(g_ui, false); }

std::string fmt(int id, ...)
{
	std::string f = rm8Str(id);
	for(size_t p = 0; (p = f.find("%S", p)) != std::string::npos; p += 2) f[p + 1] = 's';	// the game's "%S" takes a narrow name here
	char buf[1024];
	va_list ap; va_start(ap, id); vsnprintf(buf, sizeof buf, f.c_str(), ap); va_end(ap);
	return ev::plain(buf);
}
std::string str(int id){ return ev::plain(rm8Str(id)); }

// ======================================================================== role helpers
// [V] RoleSelector_GetRandomPlayer @0x4b5150: a random role that is not bankrupt (freeOnly: also status 0); -1 if none
int randomPlayer(bool freeOnly)
{
	std::vector<int> c;
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(alive(s) && (!freeOnly || rm8TurnStatus(s) == 0)) c.push_back(s);
	if(c.empty()) return -1;
	return c[rnd() % c.size()];
}
int curSeat(){ int c = rm8TurnCurrent(); return alive(c) ? c : -1; }	// [V] g_RoleMgr[0xb]; most events check that role is not bankrupt

// [V] Role_EnqueueMsgPairWithCurrent @0x4b8a00 (disassembled): the subject's queue gets 0x2E(current + 1, 1) at the front, the current role's queue gets 0x2E(subject + 1, 0) at `pos`
// (the original's roles: ecx of the first call = the subject, of the second = the current role: after the news the current role hands over to the subject, who hands back
// after its messages); 0 if the same role
bool pairWithCurrent(int subject, int pos)
{
	int cur = rm8TurnCurrent();
	if(!validSeat(subject) || !validSeat(cur) || cur == subject) return false;
	rm8TurnEnqueueAt(subject, 0, 0x2e, cur + 1, 1);
	rm8TurnEnqueueAt(cur, pos, 0x2e, subject + 1, 0);
	return true;
}

// the all-role effects (News_GiveMoneyToAll @0x4b8150 ...): Obj_QueueTextMsg_Alt(text) + Obj_EnqueueMsg_Alt(0x65, 0x1e) + the effect message, appended to the role's queue
void textWaitThen(int seat, const std::string &text, int cmd, int a, int b)
{
	ev::queueText(seat, text);
	rm8TurnEnqueue(seat, 0x65, 0x1e, 0);
	rm8TurnEnqueue(seat, cmd, a, b);
}

// ======================================================================== news effects (News_EventSwitch @0x4b4920 and the News_Event* functions)
// [V] News_EventGetMoney @0x4b5250 / News_EventLoseMoney @0x4b53d0: amount x price index; a random role; its queue gets 0x17 / 0x16 at the front; the fine goes into the
// lottery prize pool (*(DAT_006403d0 + 0x50) += amount)
bool eventMoney(int base, int id, bool gain)
{
	if(base <= 0) return false;
	int32_t amt = (int32_t)base * rm8GamePI();	// [N] int32_t
	int seat = randomPlayer(false);
	if(seat < 0) return false;
	caption(fmt(300 + id, rm8TurnName(seat), (int)amt));
	rm8TurnEnqueueAt(seat, 0, gain ? 0x17 : 0x16, (int)amt, 0);
	if(!gain) ev::lotteryPoolAdd(amt);
	pairWithCurrent(seat, 1);
	rm8Log("news: seat %d %s %d (news %d)", seat, gain ? "gets" : "loses", amt, id);
	return true;
}
// [V] Role_TakeMoney @0x4b8af0 (role, base, string index): the role gets base x PI in cash (0x17 at the front of its queue), the text is string `strId`
bool takeMoney(int seat, int base, int strId)
{
	if(!validSeat(seat)) return false;
	int32_t amt = (int32_t)base * rm8GamePI();	// [N] int32_t
	caption(fmt(strId, rm8TurnName(seat), (int)amt));
	rm8TurnEnqueueAt(seat, 0, 0x17, (int)amt, 0);
	pairWithCurrent(seat, 1);
	rm8Log("news: seat %d is rewarded %d (string %d)", seat, amt, strId);
	return true;
}
// [V] News_EventBankHack @0x4b5570: every role that is not bankrupt loses `amt` of its deposit (not below 0, set at once); each adds amt to the lottery pool
bool eventBankHack(int base)
{
	int32_t amt = (int32_t)base * rm8GamePI();	// [N] int32_t
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		if(!alive(s)) continue;
		int32_t dep = rm8GameDeposit(s) - amt;	// [N] int32_t
		rm8GameSetMoney(s, rm8GameCash(s), dep < 0 ? 0 : dep);
		ev::lotteryPoolAdd(amt);
	}
	caption(fmt(317, (int)amt));
	rm8Log("news: bank hack, every deposit -%d", amt);
	return true;
}
// [V] News_EventPrisonAmnesty @0x4b6180 / News_EventHospitalFire @0x4b6250 (the decompile's names are swapped: status 4 = hospital, 5 = prison, cards.cpp): every role in that
// status gets stay days 0 (it leaves when its next turn starts)
void releaseStatus(int status)
{
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(alive(s) && rm8TurnStatus(s) == status){ rm8CardsSetStatusDays(s, 0); rm8Log("news: seat %d leaves status %d at its next turn", s, status); }
}
// a role pick by value: the best (max, value > 0) or the worst (min, value < bad) over the roles that are not bankrupt; ties keep the first
int pickBy(const std::function<int32_t(int)> &f, bool wantMax, int32_t bad)	// [N] int32_t
{
	int best = -1; int32_t bv = 0;	// [N] int32_t
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		int32_t v = alive(s) ? f(s) : (wantMax ? 0 : bad);	// [N] int32_t
		if(wantMax){ if(v > 0 && (best < 0 || bv < v)){ best = s; bv = v; } }
		else if(v < bad && (best < 0 || v < bv)){ best = s; bv = v; }
	}
	return best;
}
int ownedPlots(int seat){ int n = 0; for(int t = 0; t < rm8GameTerraCount(); t++) if(rm8GameTerraOwner(t) == seat) n++; return n; }	// [V] Role_CountOwnTerras(9, 0, 5) @0x48c430
// [V] News_EventStockBoom @0x4b6320 / Crash @0x4b63a0 / Liquidate @0x4b64e0: the market's closed days < 1 and the weekday < 6
bool marketOpenWeekday(){ int y, m, d, w; rm8GameDate(y, m, d, w); return rm8StockClosedDays() < 1 && w < 6; }
// [V] News_EventBirthday @0x4b5770: a random role with fewer than 5 cards is given one card by every other role holding cards (a random one among the first <role count>
// slots: the original loops over the role count, not over the 8 slots)
bool eventBirthday()
{
	int b = randomPlayer(false);
	if(!alive(b) || rm8CardCount(b) >= 5) return false;
	int n = rm8TurnRoleCount();
	std::vector<int> ids;
	for(int s = 0; s < n; s++){
		if(s == b || !alive(s) || rm8CardCount(s) <= 0) continue;
		int pick = -1;
		for(int k = 0; k < n && k < rm8CardMax(); k++){
			if(!rm8CardAt(s, k)) continue;
			if(pick < 0 || rnd() % 3 == 1) pick = k;
		}
		if(pick >= 0){ ids.push_back(rm8CardAt(s, pick)); rm8CardRemoveAt(s, pick); }
	}
	if(ids.empty()) return false;
	int pos = 0;
	for(int id : ids) rm8TurnEnqueueAt(b, pos++, 4, id - 1, 1);	// Role_EnqueueMsgAt(pos, 4, Card_FindByIdReturnIndex(id), 1)
	pairWithCurrent(b, pos);
	caption(fmt(340, rm8TurnName(b)));
	rm8Log("news: birthday of seat %d: %d cards from the others", b, (int)ids.size());
	return true;
}
// [V] News_EventSpecialEvent @0x4b5d80: one chance in two; a random role with status 0 goes to hospital for `days`
bool eventHospital(int days, int id)
{
	if(days <= 0 || (rnd() & 1) != 1) return false;
	int s = randomPlayer(true);
	if(!validSeat(s)) return false;
	caption(fmt(300 + id, rm8TurnName(s)));
	rm8TurnEnqueueAt(s, 0, 0x18, days, 0);
	pairWithCurrent(s, 1);
	rm8Log("news: seat %d goes to hospital for %d days (news %d)", s, days, id);
	return true;
}
// [V] News_EventAlienAbduction @0x4b6030: a role with status 0 vanishes for 3 days (message 0x5F)
bool eventAbduction()
{
	int s = randomPlayer(true);
	if(!validSeat(s)) return false;
	caption(fmt(320, rm8TurnName(s)));
	rm8TurnEnqueueAt(s, 0, 0x5f, 3, 0);
	pairWithCurrent(s, 1);
	rm8Log("news: seat %d is abducted for 3 days", s);
	return true;
}
// [V] the plots the building news pick from: level != 0 and an owner (owner >= 0; at most 128)
std::vector<int> builtPlots()
{
	std::vector<int> v;
	for(int t = 0; t < rm8GameTerraCount() && v.size() < 128; t++){
		if(rm8GameTerraLevel(t) == 0 || rm8GameTerraOwner(t) < 0) continue;
		v.push_back(t);
	}
	return v;
}
// [V] News_EventBuildingFire @0x4b75e0 (effect art, id): needs more than 2 built plots; the owner's queue: 0x52(art, plot) the effect, 0x4A(plot, 1) the buildings go, the hand-back at 2;
// the current role hands over to the owner first (0x2E(owner + 1, 0) at its front)
bool eventFire(int art, int id)
{
	std::vector<int> v = builtPlots();
	if(v.size() < 3) return false;
	int t = v[rnd() % v.size()], o = rm8GameTerraOwner(t);
	if(!alive(o)) return false;
	rm8TurnEnqueueAt(o, 0, 0x52, art, t);
	rm8TurnEnqueueAt(o, 1, 0x4a, t, 1);
	int cur = rm8TurnCurrent();
	if(validSeat(cur) && cur != o){ rm8TurnEnqueueAt(cur, 0, 0x2e, o + 1, 0); rm8TurnEnqueueAt(o, 2, 0x2e, cur + 1, 0); }
	caption(str(300 + id));
	rm8Log("news: the buildings of plot %d (seat %d) are destroyed (news %d)", t, o, id);
	return true;
}
// [V] News_EventDemolition @0x4b7290: like the fire (art 0x178C, hand-back at 3) plus 0x5C(Terra_CalcPrice(1) taken before the demolition, 1): the owner is paid into the deposit
bool eventDemolition()
{
	std::vector<int> v = builtPlots();
	if(v.size() < 3) return false;
	int t = v[rnd() % v.size()], o = rm8GameTerraOwner(t);
	if(!alive(o)) return false;
	rm8TurnEnqueueAt(o, 0, 0x52, 0x178c, t);
	rm8TurnEnqueueAt(o, 1, 0x4a, t, 1);
	rm8TurnEnqueueAt(o, 2, 0x5c, (int)rm8GameTerraPrice(t), 1);
	int cur = rm8TurnCurrent();
	if(validSeat(cur) && cur != o){ rm8TurnEnqueueAt(cur, 0, 0x2e, o + 1, 0); rm8TurnEnqueueAt(o, 3, 0x2e, cur + 1, 0); }
	caption(str(333));
	rm8Log("news: demolition of plot %d (seat %d), compensation %d", t, o, rm8GameTerraPrice(t));
	return true;
}
// [V] News_EventMonsterAttack @0x4b7910: any plot with a building whose owner is a role that is not character 10; the current role gets 0x31(plot) at the front (the monster)
bool eventMonster()
{
	std::vector<int> v;
	for(int t = 0; t < rm8GameTerraCount() && v.size() < 128; t++){
		if(rm8GameTerraLevel(t) == 0) continue;
		int o = rm8GameTerraOwner(t);
		if(!validSeat(o) || rm8TurnCharId(o) == 10) continue;
		v.push_back(t);
	}
	if(v.empty()) return false;
	int pick = v[rnd() % v.size()], cur = curSeat();
	if(cur < 0) return false;
	rm8TurnEnqueueAt(cur, 0, 0x31, pick, 0);
	caption(str(330));
	rm8Log("news: monster attack on plot %d", pick);
	return true;
}
// [V] News_EventAuctionPublic @0x4b6e80: more than 4 plots without an owner; a random one; the current role gets 0x4F(plot) + 0x40(plot) at the back of its queue
bool eventAuction()
{
	std::vector<int> v;
	for(int t = 0; t < rm8GameTerraCount() && v.size() < 128; t++) if(rm8GameTerraOwner(t) < 0) v.push_back(t);
	if(v.size() < 5 || !rm8NewsAuctionHook) return false;
	int t = v[rnd() % v.size()], cur = rm8TurnCurrent();
	if(!validSeat(cur)) return false;
	rm8TurnEnqueue(cur, 0x4f, t, 0);
	rm8TurnEnqueue(cur, 0x40, t, 0);
	caption(str(329));
	rm8Log("news: public auction of plot %d", t);
	return true;
}
// [V] News_EventPropertyPrice_Up @0x4b7070 / _Down @0x4b7180: a random plot (any), the current role gets the wizard message 0x4F(plot) at the front,
// Terra_AdjustPriceByPercent(+-10) at once
bool eventPrice(int pct)
{
	int n = rm8GameTerraCount();
	if(n <= 0) return false;
	int t = rnd() % n, cur = curSeat();
	if(cur < 0) return false;
	rm8TurnEnqueueAt(cur, 0, 0x4f, t, 0);
	caption(str(pct > 0 ? 334 : 326));
	rm8GameTerraAdjustPct(t, pct);
	return true;
}
// [V] News_EventTrafficJam @0x4b5c80: from day 15 on every role that is not bankrupt stays on its node for a round (the stay flag: CPlayerTag__SetDeferredAction(0) @0x4911b0)
bool eventTrafficJam()
{
	if(rm8GameDay() < 15) return false;
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(alive(s)) rm8CardsSettle(s);
	caption(str(337));
	return true;
}
// [V] News_EventLuckyCard(0x13) @0x4b5b10: a random role holding a card loses a random one (0x05(-1, 1))
bool eventDogBite(int id)
{
	int s = randomPlayer(false);
	if(!validSeat(s) || rm8CardCount(s) < 1) return false;
	caption(fmt(300 + id, rm8TurnName(s)));
	rm8TurnEnqueueAt(s, 0, 5, -1, 1);
	pairWithCurrent(s, 1);
	rm8Log("news: seat %d loses a card to the dog", s);
	return true;
}
// [V] News_EventInterestRate @0x4b67b0: every role that is not bankrupt: 0x5C(deposit * 10 / 100, 1) at the front of its queue
bool eventInterest()
{
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(alive(s)){ int v = rm8Mul32(rm8GameDeposit(s), 10) / 100; rm8TurnEnqueueAt(s, 0, 0x5c, v, 1); rm8Log("news: interest %d for seat %d", v, s); }	// [N] deposit * 10 wraps above 214748364 like the original's imul (was 64-bit on Android)
	caption(str(303));
	return true;
}

// [V] News_EventSwitch @0x4b4920: false = the conditions of the news failed (nothing was queued / shown)
bool eventSwitch(int id)
{
	switch(id){
	case 1: if(!marketOpenWeekday()) return false; caption(str(301)); rm8StockMarketEvent(10.0f, 1); rm8Log("news: stock boom +10%%"); return true;	// 0x41200000 = +10.0, 1 day
	case 2: {	// News_EventTaxAudit: from day 6, the role with the largest stock value (> 0)
		if(rm8GameDay() < 6) return false;
		int s = pickBy([](int k){ return rm8StockValue(k); }, true, 0);
		if(s < 0) return false;
		return takeMoney(s, (rnd() % 5 + 1) * 1000, 302); }
	case 3: return eventInterest();
	case 4: {	// News_EventDiceEvent: from day 16, the role with the most plots (> 0)
		if(rm8GameDay() < 16) return false;
		int s = pickBy([](int k){ return (int32_t)ownedPlots(k); }, true, 0);	// [N] int32_t
		if(s < 0) return false;
		return takeMoney(s, (rnd() % 5 + 1) * 2000, 304); }
	case 5: return eventMoney((rnd() % 10 + 1) * 2000, id, true);
	case 6: {	// News_EventUnluckyDice: from day 16, the role with the fewest plots (0 counts)
		if(rm8GameDay() < 16) return false;
		int s = pickBy([](int k){ return (int32_t)ownedPlots(k); }, false, 1 << 30);	// [N] int32_t
		if(s < 0) return false;
		return takeMoney(s, (rnd() % 5 + 1) * 2000, 306); }
	case 7: case 0xe: return eventMoney((rnd() % 3 + 1) * 1000, id, false);
	case 8: caption(str(308)); releaseStatus(4); return true;	// "hospital fire": the patients leave (status 4)
	case 9: caption(str(309)); releaseStatus(5); return true;	// "prison amnesty": the prisoners leave (status 5)
	case 10: {	// News_EventBondTrading: from day 6, the role with the largest deposit (> 0)
		if(rm8GameDay() < 6) return false;
		int s = pickBy([](int k){ return rm8GameDeposit(k); }, true, 0);
		if(s < 0) return false;
		return takeMoney(s, (rnd() % 5 + 1) * 1000, 310); }
	case 0xb: if(!marketOpenWeekday()) return false; caption(str(311)); rm8StockMarketEvent(-10.0f, 1); rm8Log("news: stock crash -10%%"); return true;
	case 0xc: return eventMoney((rnd() % 10 + 1) * 2000, id, false);
	case 0xd: {	// News_EventNetworkOutage: closed for rand() % 7 + 1 days (only the closed-days counter is checked, not the weekday)
		int days = rnd() % 7 + 1;
		if(rm8StockClosedDays() >= 1) return false;
		caption(fmt(313, days)); rm8StockCloseMarket(days); rm8Log("news: the stock market is closed for %d days", days); return true; }
	case 0xf: case 0x12: case 0x26: return eventMoney((rnd() % 5 + 1) * 1000, id, false);
	case 0x10: return eventMoney((rnd() % 7 + 1) * 1000, id, false);
	case 0x11: return eventBankHack((rnd() % 10 + 1) * 2000);
	case 0x13: return eventDogBite(id);
	case 0x14: return eventAbduction();
	case 0x15: case 0x17: case 0x2d: return eventHospital(1, id);
	case 0x18: return eventHospital(2, id);
	case 0x19: return eventFire(0x178b, id);
	case 0x1a: return eventPrice(-10);
	case 0x1b: return eventFire(0x178a, id);
	case 0x1c: case 0x1f: case 0x20: return eventFire(0x178c, id);
	case 0x1d: return eventAuction();
	case 0x1e: return eventMonster();
	case 0x21: return eventDemolition();
	case 0x22: return eventPrice(10);
	case 0x23: return eventMoney((rnd() % 5 + 1) * 1000, id, true);
	case 0x24: return eventMoney((rnd() % 5 + 1) * 2000, id, true);
	case 0x25: return eventTrafficJam();
	case 0x27: return eventMoney((rnd() % 5 + 1) * 2000, id, false);
	case 0x28: return eventBirthday();
	case 0x29: {	// News_EventStockLiquidate: every role sells all its shares, the deposit gets the money; fails if nothing was sold
		if(!marketOpenWeekday()) return false;
		int32_t total = rm8StockLiquidateAll();	// [N] int32_t
		if(total <= 0) return false;
		caption(str(341)); rm8Log("news: every share is sold, %d to the deposits", total); return true; }
	case 0x2a: {	// News_EventBankruptEvent: from day 6, the role with the smallest deposit
		if(rm8GameDay() < 6) return false;
		int s = pickBy([](int k){ return rm8GameDeposit(k); }, false, 1000000000);
		if(s < 0) return false;
		return takeMoney(s, (rnd() % 5 + 1) * 2000, 342); }
	case 0x2b: case 0x2c: return eventHospital(3, id);
	}
	return false;
}

// [V] News_SelectRandomNew @0x4b4850: table 0x5E4A58 (44 ids: 1..21, 23..45) for the first pick, 0x5E4B58 (38 ids: 1..20, 25..42) for the retries; a pick that is
// in the last 5 is replaced by rand() % 44 (a raw number 0..43, not through the table); the pick goes to the front of the history
int selectRandomNew(int retry)
{
	static const int T1[44] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45};
	static const int T2[38] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42};
	int pick = retry == 0 ? T1[rnd() % 44] : T2[rnd() % 38];
	for(;;){
		int k = 0;
		while(!(k > 4 || g_hist[k] < 0)){ if(pick == g_hist[k]) break; k++; }
		if(k > 4 || g_hist[k] < 0){
			memmove(&g_hist[1], &g_hist[0], 16);
			g_hist[0] = pick;
			return pick;
		}
		pick = rnd() % 44;
	}
}

// [V] News_EventDispatcher @0x4b7f80: the festival news (id 0x2E..0x3A = string 300 + id): picture art 900 + id; effects on the roles' queues (appended)
void giveMoneyToAll(int amt)	// [V] News_GiveMoneyToAll @0x4b8150: text 5321 (name, amount), wait 30 frames, 0x17(amount, cash)
{
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(alive(s)) textWaitThen(s, fmt(5321, rm8TurnName(s), amt), 0x17, amt, 0);
}
void giveTicketsToAll(int n)	// [V] News_GiveTicketToAll @0x4b82e0: text 5322, wait 30 frames, 7(points)
{
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(alive(s)) textWaitThen(s, fmt(5322, rm8TurnName(s)), 7, n, 0);
}
// [V] Card_OnGetCard(1, mode) @0x4b8470: a random card (message 4 with -1) for every role that is not bankrupt and (mode > 1 or its sex == mode) and (mode != 2 or the dice count < 2):
// sex 0 female, 1 male; 2 = the walkers; 9 = everybody
void cardToAll(int mode)
{
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		if(!alive(s) || !(mode > 1 || rm8TurnSex(s) == mode) || (mode == 2 && rm8TurnDiceMax(s) >= 2)) continue;
		ev::queueText(s, fmt(5303, rm8TurnName(s)));
		rm8TurnEnqueue(s, 0x65, 0x1e, 0);
		rm8TurnEnqueue(s, 4, -1, 1);
	}
}
// [V] Role_GetMoneyByPercent(10, 0) @0x4b8690: the deposit x 10 / 100 in cash to every female role (sex 0), text 5321
void percentToSex(int pct, int sex)
{
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		if(!alive(s) || rm8TurnSex(s) != sex) continue;
		int amt = rm8Mul32(rm8GameDeposit(s), pct) / 100;	// [N] the original's 32-bit imul (deposit > 214748364 at 10 %), explicit
		textWaitThen(s, fmt(5321, rm8TurnName(s), amt), 0x17, amt, 0);
	}
}
int g_loopSnd = 0;	// the looping sample of the news (+0x14: the Spring Festival fireworks; stopped when the timer ends: Cls_5e20a8__Cleanup @0x4b8d10)

bool eventFestival(int id)
{
	caption(str(300 + id));
	switch(id){
	case 0x2e: giveMoneyToAll(5000); break;
	case 0x2f: g_loopSnd = rm8SndPlay(0x68, 0); giveMoneyToAll(50000); break;	// the fireworks loop (sample 0x68, loops 0)
	case 0x30: releaseStatus(4); break;
	case 0x31: cardToAll(0); break;
	case 0x32: percentToSex(10, 0); break;
	case 0x33:	// [V] Role_BroadcastEvent @0x4b8930: every role gets 0x49 (Role_TriggerLandEvent(0x11) = the news task for it) and a camera focus message
		for(int s = 0; s < rm8TurnRoleCount(); s++) if(alive(s)){ rm8TurnEnqueue(s, 0x49, 0, 0); rm8TurnEnqueue(s, 0x1e, 0, 0); }
		break;
	case 0x34: cardToAll(2); break;
	case 0x35: giveTicketsToAll(0x37); break;
	case 0x36: cardToAll(1); break;
	case 0x37: releaseStatus(5); break;
	case 0x38: giveTicketsToAll(99); break;
	case 0x39:	// [V] Role_DoAction @0x4b8880 -> CPlayerTag__ClearPossessedGod @0x48edc0 for every role that is not bankrupt: stay days 0, bad gods leave, the time bomb is cleared
		for(int s = 0; s < rm8TurnRoleCount(); s++) if(alive(s)){ rm8CardsSetStatusDays(s, 0); rm8NpcClearPossessed(s); }
		break;
	case 0x3a: cardToAll(9); break;
	default: return false;
	}
	rm8Log("news: festival news %d (%s)", id, str(300 + id).c_str());
	return true;
}

// ======================================================================== the task (Cls_5e20a8)
struct NewsTask : ev::Task {
	int mode = 0;		// [V] +8: 1 animal, 2 broadcast intro (then the pick), 3 the news itself
	float timer = 0;	// +0x1c
	int snd[2] = {0, 0};	// +0x14 / +0x18
	Vis vis;

	NewsTask(){ g_vis = &vis; }
	~NewsTask() override
	{
		stopSounds();
		captionHide();
		modelFree(vis.ill); vis.ill = nullptr; modelFree(vis.tv); vis.tv = nullptr;
		if(g_vis == &vis) g_vis = nullptr;
	}
	void stopSounds()	// [V] Cls_5e20a8__Cleanup @0x4b8d10
	{
		for(int &h : snd) if(h){ rm8SndStop(h); h = 0; }
		if(g_loopSnd){ rm8SndStop(g_loopSnd); g_loopSnd = 0; }
	}

	// [V] News_InitDialog @0x4b41b0: the anchor article 1052 (0x41c) in front of the camera
	void initDialog(){ vis.tv = modelLoad(1052); }
	// [V] News_CreateTextEffect @0x4b4f60: the picture article, animation 0 for ever
	void createTextEffect(int art)
	{
		modelFree(vis.ill); vis.ill = nullptr;
		vis.ill = modelLoad(art);
		modelPlay(vis.ill, 0, -1);
	}
	void setTimer(float secs){ timer = secs * S(); }

	// [V] News_BroadcastMode @0x4b4490: art 922 (colour bars), the line of string 4901 and its voice, the anchor's animation 0 once; 2.6 s
	void broadcastMode()
	{
		createTextEffect(922);
		caption(str(4901));
		Rm8SpeechLine l;
		if(rm8SpeechParse(4901, l) && l.voice > 0) snd[0] = rm8SndPlay(l.voice, 1);	// CAudioMgr_PlaySample(sound of string 4901, loops 1)
		modelPlay(vis.tv, 0, 1);
		setTimer(2.6f);
		mode = 2;
	}
	// [V] News_AnimalMode @0x4b4560: art 960 (TV snow), an empty caption, the anchor's animation 2 once, samples 0x7E and 0x7F (loops 0 = for ever); 1.8 s
	void animalMode()
	{
		createTextEffect(960);
		caption("");
		modelPlay(vis.tv, 2, 1);
		snd[0] = rm8SndPlay(0x7e, 0);
		snd[1] = rm8SndPlay(0x7f, 0);
		setTimer(1.8f);
		mode = 1;
	}
	// [V] News_SpecialEvent @0x4b4620: pick until a news accepts, then its picture (art 900 + id) for 3.8 s
	void specialEvent()
	{
		int id = selectRandomNew(0);
		if(g_forceNext < g_forceWeekly.size()){	// test hook: the forced id replaces the first pick (a refused one falls back to the original retry loop)
			int f = g_forceWeekly[g_forceNext++];
			rm8Log("news: forced id %d (picked %d)", f, id);
			id = f;
		}
		int tries = 0;
		dumpState("before");
		while(!eventSwitch(id)){
			if(dbg()) rm8Log("news: id %d refused", id);
			id = selectRandomNew(1);
			if(++tries > 5000){ rm8Log("news: no news accepted (stuck guard)"); id = 0; break; }
		}
		rm8Log("news: weekly news %d on day %d: %s", id, rm8GameDay(), g_cap.c_str());
		if(id) createTextEffect(900 + id);
		setTimer(3.8f);
		mode = 3;
	}

	bool update(float dt) override
	{
		timer -= dt;
		if(timer <= 0){
			timer = 0;
			stopSounds();
			if(mode == 1) broadcastMode();
			else if(mode == 2) specialEvent();
			else if(mode == 3){ if(dbg()){ rm8Log("news: task ends"); g_afterT = 10; } return true; }	// News_OnClean, return 0x80000000
		}
		applyAnim(vis.ill, dt); applyAnim(vis.tv, dt);
		return false;
	}
};

// [V] Cls_5e20a8__vf16_Init @0x4b4170
NewsTask *makeWeekly()
{
	NewsTask *t = new NewsTask();
	t->initDialog();
	static const bool forceAnimal = getenv("RM8_NEWS_ANIMAL") != nullptr;	// test hook: always the animal mode
	if(rnd() % 5 == 1 || forceAnimal) t->animalMode(); else t->broadcastMode();
	return t;
}
// [V] News_OnUpdate @0x4b4da0 / News_OnSpecialEventUpdate @0x4b4e40
NewsTask *makeEvent(int id, int subject)
{
	NewsTask *t = new NewsTask();
	t->initDialog();
	modelPlay(t->vis.tv, 0, -1);	// [H] no animation is started by News_OnUpdate: the anchor idles (animation 0 for ever)
	bool ok = false; int art = 0; int voice = -1;
	if(subject < 0){
		ok = eventSwitch(id);
		art = 900 + id;
	}else if(id >= 0 && id <= 8){	// News_EventMinigame(3, id, subject) @0x4b5ef0: the police, 3 days of prison for the subject (message 0x19)
		if(validSeat(subject)){
			art = 0x1839 + id;	// 6201..6209 = article\destiny\N07m01..09
			caption(fmt(418 + id, rm8TurnName(subject)));
			rm8TurnEnqueueAt(subject, 0, 0x19, 3, 0);
			pairWithCurrent(subject, 1);
			voice = 418 + id; ok = true;
		}
	}else if(id == 9){	// News_EventThief @0x4b7bc0: half of the subject's points (message 8)
		if(validSeat(subject)){
			art = 0x1842;
			caption(fmt(428, rm8TurnName(subject)));
			rm8TurnEnqueueAt(subject, 0, 8, (int)(rm8GamePoints(subject) / 2), 0);
			pairWithCurrent(subject, 1);
			voice = 428; ok = true;
		}
	}else if(id == 10){	// News_EventLuckyFind @0x4b7d10: two cards (message 4, -1, 1 twice)
		if(validSeat(subject)){
			art = 0x1843;
			caption(fmt(431, rm8TurnName(subject)));
			rm8TurnEnqueueAt(subject, 0, 4, -1, 1);
			rm8TurnEnqueueAt(subject, 1, 4, -1, 1);
			pairWithCurrent(subject, 2);
			voice = 431; ok = true;
		}
	}else if(id == 11){	// News_EventTreasureChest @0x4b7e50: 250 points (message 7)
		if(validSeat(subject)){
			art = 0x1844;
			caption(fmt(432, rm8TurnName(subject)));
			rm8TurnEnqueueAt(subject, 0, 7, 250, 0);
			pairWithCurrent(subject, 1);
			voice = 432; ok = true;
		}
	}else if(id >= 0x2e && id <= 0x3a){	// News_EventDispatcher @0x4b7f80: the picture first, then the text and the effects
		art = 900 + id;
		t->createTextEffect(art);
		ok = eventFestival(id);
		art = 0;
	}
	if(!ok){ delete t; return nullptr; }
	if(art) t->createTextEffect(art);
	// [V] News_EventMinigame @0x4b5ef0 / News_EventThief @0x4b7bc0 / News_EventLuckyFind @0x4b7d10 / News_EventTreasureChest @0x4b7e50 / News_OnUpdate @0x4b4da0 play NO sample: they only show the
	// caption (string table text array +0x4b0.., not the sample array +32000) and the picture. The "@061 / 062 / 064 / 066" of the strings 418.. only name the sample of the NPC / event itself:
	// the police car's siren is sample 0x47 (= 71, the same file as 61) which starts with the arrest AFTER the news (RoleAnimation_Start @0x4b11d0, message 0x19 runs when the news task has ended),
	// the lucky card / chest samples 0x41 / 0x42 are played by their NPCs before the news (message 0x53). Playing the string's sample here made the siren start with the report (BUGS #29).
	(void)voice;
	t->setTimer(3.8f);
	t->mode = 3;
	rm8Log("news: news %d%s about seat %d", id, subject < 0 ? "" : " (role news)", subject);
	return t;
}

std::unique_ptr<NewsTask> g_run;	// the role-bound news task (the weekly one lives in events.cpp's task slot)
int g_runSeat = -1;

}	// namespace

ev::Task *makeNewsTask(int seat)
{
	NewsTask *t = makeWeekly();
	t->seat = seat;
	return t;
}

}	// namespace news

namespace ev { Task *makeNewsTask(int seat){ return news::makeNewsTask(seat); } }

// ======================================================================== public API
bool rm8NewsLoadData(const std::string &dir)
{
	std::ifstream f(dir + "/news.txt");
	if(!f){ rm8Log("news: no %s/news.txt (run scripts/export_news.py)", dir.c_str()); return false; }
	std::string line;
	memset(news::g_fest, 0, sizeof news::g_fest);
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "nart"){
			int id; news::ArtDef d; std::string a;
			ss >> id >> d.dff >> d.texdir;
			while(ss >> a){ size_t c = a.find(':'); if(c != std::string::npos) d.anims[atoi(a.c_str())] = a.substr(c + 1); }
			news::g_art[id] = d;
		}else if(kw == "fest"){
			int b; ss >> b;
			if(b >= 0 && b < 6) for(int k = 0; k < 32; k++) ss >> news::g_fest[b][k];
			news::g_festOk = true;
		}
	}
	const char *fc = getenv("RM8_NEWS_FORCE");	// test hook: ids (comma separated) for the next weekly news, one per news
	news::g_forceWeekly.clear(); news::g_forceNext = 0;
	if(fc){ std::string cur; for(const char *p = fc;; p++){ if(*p == ',' || !*p){ if(!cur.empty()) news::g_forceWeekly.push_back(atoi(cur.c_str())); cur.clear(); if(!*p) break; } else cur += *p; } }
	rm8Log("news: %d pictures, festival table %s", (int)news::g_art.size(), news::g_festOk ? "loaded" : "missing");
	return !news::g_art.empty();
}

void rm8NewsClear()
{
	news::g_ufos.clear();
	news::g_run.reset(); news::g_runSeat = -1;
	news::g_vis = nullptr;
	news::captionHide();
	news::g_forceNext = 0;
}

// [V] Festival_LoadTable @0x4a5ba0 (the date as yyyymmdd, the weekday 1..7): the moving festivals of festival.bin come first (block k -> id 2 3 8 9 10 11),
// then the fixed dates 0101 -> 1, 0214 -> 4, 0308 -> 5, 0401 -> 6, 0501 -> 7, 1225 -> 13 and the 4th Thursday of November (1122..1128, weekday 4) -> 12.
// [V] the function's first file read (a line compared to the date, result 0xC) uses the line (year - 2003) of the file, a header or another year's date: it never matches
static int festivalOf(int date, int weekday)
{
	int year = date / 10000;
	if(news::g_festOk && year >= 2003 && year <= 2034){
		static const int ID[6] = {2, 3, 8, 9, 10, 11};
		for(int b = 0; b < 6; b++) if(news::g_fest[b][year - 2003] == date) return ID[b];
	}
	switch(date % 10000){
	case 101: return 1;
	case 214: return 4;
	case 308: return 5;
	case 401: return 6;
	case 501: return 7;
	case 1225: return 13;
	}
	int md = date % 10000;
	if(md >= 1122 && md <= 1128 && weekday == 4) return 12;
	return 0;
}

bool rm8NewsNewDay(bool otherEvent)
{
	int y, m, d, w; rm8GameDate(y, m, d, w);
	int days = rm8GameDay();
	int seat = -1;
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(rm8TurnStatus(s) != 7){ seat = s; break; }	// the role Game_TurnNextRole settles on (+0x2c)
	if(seat < 0) return false;
	static const bool every = getenv("RM8_NEWS_EVERY") != nullptr;	// test hook: the weekly news on every new day
	static int fest = getenv("RM8_NEWS_FEST") ? atoi(getenv("RM8_NEWS_FEST")) : 0;	// test hook: this festival (1..13) on the first new day
	int f = festivalOf(y * 10000 + m * 100 + d, w);
	if(fest > 0){ f = fest; fest = 0; }
	if(f != 0){	// [V] Game_CheckFestival @0x4a5fb0: Role_EnqueueMsg(0x3C, id + 0x2D, seat + 1); the date news is skipped (iVar1 != 0)
		rm8Log("news: festival %d on %04d-%02d-%02d -> message 0x3C(0x%x) for seat %d", f, y, m, d, f + 0x2d, seat);
		rm8TurnEnqueue(seat, 0x3c, f + 0x2d, seat + 1);
		return false;
	}
	if(otherEvent) return false;
	// [V] Game_TriggerEventByDate @0x4a6780: weekday 6 (Saturday) and (days / 7) odd
	if(every || (w == 6 && (days / 7) % 2 != 0)){
		rm8Log("news: weekly news task on day %d (%04d-%02d-%02d weekday %d) for seat %d", days, y, m, d, w, seat);
		return rm8EventLand(seat, 0x11, rm8TurnNode(seat));
	}
	return false;
}

bool rm8NewsStart(int seat, int id, int subject)
{
	using namespace news;
	if(!validSeat(seat)) return false;
	g_run.reset();	// a running role news is replaced (one scene task at a time)
	dumpState("before");
	NewsTask *t = makeEvent(id, subject);
	if(!t) return false;
	t->seat = seat;
	g_run.reset(t); g_runSeat = seat;
	rm8TurnSetState(seat, 9);	// [V] CPlayerTag_SetStatusCode9 @0x4860b0
	return true;
}

bool rm8NewsSkip()	// [V] RoleAnimation_SkipToEnd @0x4B1320: state 1 -> the role is carried off at once, the clock jumps to the end: the vanish follows in the next update
{
	if(news::g_ufos.empty()) return false;
	for(news::UfoRun &u : news::g_ufos){
		if(!u.hid){ u.hid = true; rm8NpcSetRoleHidden(u.seat, true); rm8TurnFollowerRunOff(u.seat); }
		u.t = std::max(u.t, 4.0f);
	}
	return true;
}

void rm8NewsUpdate(float dt)
{
	using namespace news;
	for(size_t i = 0; i < g_ufos.size();){
		UfoRun &u = g_ufos[i];
		u.t += dt;
		if(!u.hid && u.t >= 2.4f){ u.hid = true; rm8NpcSetRoleHidden(u.seat, true); rm8TurnFollowerRunOff(u.seat); }	// [V] 0x4019999a = 2.4 s: RoleAnimation_TriggerAction
		if(u.t >= 4.0f){	// [V] 0x40800000 = 4.0 s: vf18
			if(u.fx) rm8FxStop(u.fx);
			rm8NpcSetRoleHidden(u.seat, false);
			rm8CardsVanish(u.seat, u.days);
			rm8Log("news: seat %d vanished for %d days", u.seat, u.days);
			if(rm8TurnState(u.seat) == 9) rm8TurnSetState(u.seat, 8);	// [V] Game_EndTurn @0x4a48d0
			g_ufos.erase(g_ufos.begin() + i); continue;
		}
		i++;
	}
	if(g_afterT > 0 && (g_afterT -= dt) <= 0){ g_afterT = -1; dumpState("after"); }
	if(g_run && g_run->update(dt)){	// [V] Game_EndTurn @0x4a48d0: the role goes back to state 8, the task is deleted
		{
			int s = g_runSeat;
			g_run.reset(); g_runSeat = -1;
			if(validSeat(s) && rm8TurnState(s) == 9) rm8TurnSetState(s, 8);
		}
	}
	// test hook RM8_NEWS_NOW="12,r3,f1,w": one item at a time, 1.5 s after the previous news ended, when the current role is in state 8 or 6: N = news N now (News_EventSwitch),
	// rN = the role news N (0..11) about the next seat, fN = festival N (1..13) through the message 0x3C, w = the whole weekly news task (event 0x11, intro + random pick)
	static const char *now = getenv("RM8_NEWS_NOW");
	static std::vector<std::string> items; static bool parsed = false; static size_t next = 0; static float gap = 0;
	if(!now) return;
	if(!parsed){ parsed = true; std::string cur; for(const char *p = now;; p++){ if(*p == ',' || !*p){ if(!cur.empty()) items.push_back(cur); cur.clear(); if(!*p) break; } else cur += *p; } }
	if(next >= items.size() || g_run || rm8EventRunning() || rm8TurnRoleCount() == 0){ gap = 0; return; }
	gap += dt;
	if(getenv("RM8_DEBUG_NEWS") && getenv("RM8_DEBUG_NEWS")[0] == '2'){ static float dd = 0; if((dd += dt) > 0.5f){ dd = 0; rm8Log("news test: wait seat %d state %d gap %.2f", rm8TurnCurrent(), rm8TurnState(rm8TurnCurrent()), gap); } }
	int cur = rm8TurnCurrent();
	if(gap < 1.0f || !alive(cur) || (rm8TurnState(cur) != 8 && rm8TurnState(cur) != 6)) return;	// 1 s after the last news, when the role is between two queue messages (or waits for the dice)
	gap = 0;
	std::string it = items[next++];
	rm8Log("news test: %s (seat %d)", it.c_str(), cur);
	if(it == "w") rm8EventLand(cur, 0x11, rm8TurnNode(cur));
	else if(it[0] == 'r') rm8NewsStart(cur, atoi(it.c_str() + 1), (cur + 1) % rm8TurnRoleCount());
	else if(it[0] == 'f') rm8TurnEnqueueAt(cur, 0, 0x3c, atoi(it.c_str() + 1) + 0x2d, cur + 1);
	else if(it[0] == 'u') rm8TurnEnqueueAt(cur, 0, 0x5f, 3, 0);	// the UFO alone (message 0x5F)
	else if(it[0] == 'm') rm8TurnEnqueueAt(cur, 0, 0x31, rm8GameTerraCount() > 0 ? atoi(it.c_str() + 1) : 0, 0);	// the monster alone (message 0x31, plot)
	else rm8NewsStart(cur, atoi(it.c_str()), -1);
}

bool rm8NewsMessage(int seat, int cmd, int a, int b)
{
	switch(cmd){
	case 0x5c: rm8GameAddMoney(seat, a, b != 0); return true;	// [V] dispatcher @0x481df0 case 0x5C: Role_AddMoney(a, account b, no emote); account 1 = the deposit
	case 0x5b: rm8GameSubMoney(seat, a, b != 0); return true;	// [V] case 0x5B: Role_SubMoney(a, account b, no emote)
	case 0x49: rm8EventLand(seat, 0x11, rm8TurnNode(seat)); return true;	// [V] case 0x49: Role_TriggerLandEvent(0x11) (April Fool's day: the news task for every role)
	case 0x4a: {	// [V] u_Role_ResetTerraEffect @0x4876c0 (plot, b): the owner (the role of the queue) speaks class 4; b == 0: Land_ResetBuildAndOwner, else BuildObj_ResetAll(1, 1) = the buildings go
			// (level 0, the owner keeps the land)
		if(a < 0 || a >= rm8GameTerraCount()) return true;
		if(rm8GameTerraOwner(a) == seat) rm8RoleSayAt(seat, 0, 4);
		rm8GameSetTerra(a, b != 0 ? -2 : -1, 0, 0);
		rm8Log("news: plot %d %s", a, b != 0 ? "loses its buildings" : "loses its buildings and its owner");
		return true; }
	case 0x4f: {	// [V] Role_ProcessWizardMsg @0x487b60 (plot): the camera on the plot, NPC_CreateWizard = Cls_5e6938 @0x4f5d00 / @0x4f5d50: the plot's pad alpha and building colour toggle every 0.4 s
			// for 1.6 s (boardfx.cpp rm8BoardFxBlink), the role waits in state 9 (nothing happens to the plot's data)
		float p[3];
		if(rm8GameTerraPos(a, p)) rm8TurnFocus(p);
		rm8BoardFxBlink(seat, {a}, 0, nullptr);
		return true; }
	case 0x31: {	// [V] CPlayerTag__OnParkEvent @0x48d310 (plot) -> u_NPC_Chest_Construct @0x4ef000: the monster article 0x1774 (6004) at the plot, sample 6 (NPC_TreasureChest_Init @0x4f51c0),
			// 1.5 s later (Cls_5e6758__vf1 @0x4f52e0) NPC_Roadblock_Handle -> BuildObj_ResetAll(1, 1): the buildings go, the owner stays; the task ends with the effect's animation (boardfx.cpp). No emote.
		if(a < 0 || a >= rm8GameTerraCount()) return true;	// [V] 0x48d310: no plot -> nothing
		if(rm8StoryMonsterHit(seat, a)) return true;	// [V] the story branches of the hero (story.cpp) replace the monster: no chest / monster, the buildings stay
		if(!rm8BoardFxMonster(seat, a)) rm8GameSetTerra(a, -2, 0, 0);
		rm8StoryMonsterBuilt();	// [V] u_NPC_Chest_Construct succeeded -> the hero's +0x37c = 1
		return true; }
	case 0x40:	// [V] Role_TriggerEvent50or51 @0x487bc0 (plot, kind): Game_UseCard(0x50, plot) = the public auction (the hook; without it the message is left to the others)
		if(!rm8NewsAuctionHook || b == 1) return false;	// b == 1 = the bankruptcy auction (event 0x51): events.cpp (event_vendue.cpp)
		rm8NewsAuctionHook(seat, a);
		return true;
	case 0x5f: {	// [V] NPC_CreateRandom_Type3 @0x4a46d0 (days): Stock_InitializeObject_Type3 @0x4a8580 = the scene task of the 0x5E3AF0 family with the mesh art 0x178A = 6026 eff\ufo (scale 1, no
			// sample, 2.4 s / 4.0 s): RoleAnimation_Start @0x4b11d0 puts the mesh exactly on the role (Mesh_SetTranslation(role position), camera focus on the role, animation 0 once), the role is
			// in state 9; at 2.4 s RoleAnimation_TriggerAction @0x4b1470 = role vf+0x6c (the role is carried off: Role_ClearFlag1C8 + Role_ResnapToNode), at 4.0 s Cls_5e3be0__vf18 @0x4b16a0:
			// Role_SetVanishStatus(days). The mesh is drawn after RwCameraClear(z) (Cls_5e3af0__vf2 @0x4b1420): over everything (present.cpp overlay)
		float p[3];
		if(!rm8RolesGetPos(seat, p)) return true;	// [V] RoleAnimation_Start: no role -> no task
		rm8TurnFocus(p);
		news::UfoRun u; u.seat = seat; u.days = a;
		u.fx = rm8FxPlay(0x178a, p, rm8BoardFxCamYaw() - 180.0f, 0, true, 0, 1.0f, 0, false);
		if(u.fx) rm8FxSetOverlay(u.fx);
		else{ rm8CardsVanish(seat, a); return true; }	// [A] no UFO model: the vanish at once
		rm8TurnSetState(seat, 9);
		news::g_ufos.push_back(u);
		return true; }
	}
	return false;
}

void rm8NewsRender(Camera *cam)
{
	using namespace news;
	if(!g_vis || (!g_vis->ill && !g_vis->tv) || !cam) return;
	uint32 fn = GetRenderState(ALPHATESTFUNC), ref = GetRenderState(ALPHATESTREF);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196);	// [V] render state 0x1E = 0xC4
	const float s = 0.88f;	// [V] News_InitDialog: 1.0 on an 800 wide screen, else 0.88
	if(g_vis->ill){
		// the picture is a plane bigger than the TV's glass (art x01..x58 are 512-px style images, the glass is about 257 x 211 px of the 512 x 512 frame art 514): it must stay inside the
		// glass. [A] a GL scissor over the glass rectangle (the frame art's pixels 108..365 x 168..379, measured on the glass of J201_1; the original's four Im2D vertices of
		// News_InitDialog @0x4b41b0 are a 240 x 280 rectangle of the 800-wide layout, with no colour: a depth mask, whose use is not decoded) - BUGS #35
		static void (*sciss)(int, int, int, int) = (void (*)(int, int, int, int))SDL_GL_GetProcAddress("glScissor");
		static void (*glEn)(unsigned) = (void (*)(unsigned))SDL_GL_GetProcAddress("glEnable");
		static void (*glDis)(unsigned) = (void (*)(unsigned))SDL_GL_GetProcAddress("glDisable");
		float tx, ty, tw, th; bool clip = false;
		if(g_ui && sciss && glEn && glDis && cam->frameBuffer && rm8UiGetScreenRectN(g_ui, "Label1", tx, ty, tw, th) && tw > 8){
			float k = tw / 512.0f;
			int H = cam->frameBuffer->height;
			int x0 = (int)(tx + 108 * k), x1 = (int)(tx + 365 * k + 0.5f), y0 = (int)(ty + 168 * k), y1 = (int)(ty + 379 * k + 0.5f);
			glEn(0x0C11);	// GL_SCISSOR_TEST
			sciss(x0, H - y1, x1 - x0, y1 - y0);
			clip = true;
		}
		modelPlace(g_vis->ill, cam, 0, -0.75f, 60.0f, 0.05f * s); g_vis->ill->clump->render();
		if(clip) glDis(0x0C11);
	}	// [V] @0x4b4f60 / Mesh_InitializeTransform @0x4b40b0: scale 0.05
	if(g_vis->tv){
		RGBA c = {0, 0, 0, 0};
		cam->clear(&c, Camera::CLEARZ);	// [V] Cls_5e20a8__vf2_Render @0x4b4770: RwCameraClear(z) between the picture and the anchor
		modelPlace(g_vis->tv, cam, 15.0f * s, -8.0f * s, 80.0f, 0.3f * s);		// [V] @0x4b41b0: 15 s right, -8 s up, 80 ahead, scale 0.3 s
		g_vis->tv->clump->render();
	}
	SetRenderState(ALPHATESTFUNC, fn); SetRenderState(ALPHATESTREF, ref);
}

bool rm8NewsActive(){ return news::g_vis != nullptr; }

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_news.inc"
