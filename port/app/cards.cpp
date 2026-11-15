// Card inventory, the card bar of the main HUD and card use, after docs/cards_port.md (docs/cards.md for the card data).
// Facts marked [V] are read from the game code (addresses given), [H] hypotheses, [A] port adaptations.
//
// [V] A role holds 8 card slots (Deck_AddCard @0x49a650): a slot is {card, ready flag}; a new card takes the first empty
//     slot, nothing is compacted. Role_RefreshDeckUi @0x4812c0 shows the cards of the role whose turn it is (also a
//     computer's) on gmaindlg: picture button 11 + k (texture card\<texName>), name label 19 + k, click button 31 + k;
//     a card with the ready flag is not shown. Game_ToggleCharacterVisibility @0x4a6f90 -> Ui_ToggleCardDisplay @0x4e9900
//     enables the click buttons (state 6, or the sell / swap modes).
// [V] Using a card (human, state 6): Card_OnSelected @0x4e9340 -> Role_TryUseCardInSlot @0x488c80 queues 2(slot), state 8
//     -> Role_BeginCardUse @0x4897a0 (the card's check vf1, ready flag, camera focus) -> a selection message 1(...) ->
//     3(slot, result) -> Role_FinishCardUse @0x4896e0: Execute(result - 1) of the CardPy script; success: the card leaves the
//     deck (kept for Restore), failure / cancel: the card returns to the bar and 100 (Role_BeforeRollDice) is queued.
#include "cards.h"
#include "ui.h"
#include "npc.h"
#include "present.h"
#include "boardfx.h"
#include "story.h"
#include "audio.h"
#include "collect.h"
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <algorithm>
#include <chrono>
#include <thread>

using namespace rw;

int rm8CardsDifficulty = 3;

namespace {

struct Def {
	int id = 0, type = 0, level = 0, price = 0;
	unsigned cond = 0;
	int speak[3] = {-1, -1, -1};
	std::string name, py, tex, desc;
};
std::vector<Def> g_defs;		// the card array: card.bin order, array index = Id - 1 (Card_LoadCardBin @0x4adec0)
std::map<int, int> g_idx;		// card Id -> array index
std::map<int, int> g_godDays;		// npcinfo Id -> date (possession days, NPC +0x228)
const Def *def(int id){ auto it = g_idx.find(id); return it == g_idx.end() ? nullptr : &g_defs[it->second]; }

struct Deck { int id[8] = {0, 0, 0, 0, 0, 0, 0, 0}; bool ready[8] = {false, false, false, false, false, false, false, false}; int tempId = 0, tempSlot = -1; };
std::map<int, Deck> g_deck;
Deck &deck(int seat){ return g_deck[seat]; }

// cards-related fields of CPlayerTag that the port keeps here
struct Extra { int turtle = 0, stay = 0, bubble = 0, days = 0, guard = 0; };	// +0x1cc, +0x1d0, +0x28c counter, +0x1d8, 莎拉's bodyguard counter (path object +0x5f0 / +0x98)
std::map<int, Extra> g_x;
Extra &X(int seat){ return g_x[seat]; }

Camera *g_cam = nullptr;
Rm8Ui *g_main = nullptr;
bool g_sell = false;
struct Swap { bool on = false; int seat = -1, card = 0, emote = 0; } g_swap;	// a human with a full hand waits to replace a card by the received one
struct Throw { bool on = false; int seat = -1, slot = -1; } g_throw;		// the discard / store menu of a hand card is open
void closeThrow();
void swapFinish(bool take, int slot);

std::string fmt(const char *f, ...){ char b[512]; va_list ap; va_start(ap, f); vsnprintf(b, sizeof b, f, ap); va_end(ap); return b; }
// [V] Role_QueueTextMsg @0x484b70: info box message
void info(int seat, const std::string &txt){ rm8TurnEnqueue(seat, 0x1B, rm8GamePushText(txt), 0); }
int curTerra(int seat){ return rm8GameNodeTerra(rm8TurnNode(seat)); }
bool validSeat(int s){ return s >= 0 && s < rm8TurnRoleCount(); }

// every card of card.bin is usable now; the NPC / god cards (12 14 16 17 34 35 36..44) need npc.cpp (RM8_NPC_READY, RM8_CARD_ALL=1 forces them on for tests).
// The stock cards (22 23 47) call rm8StockEvent (stock.cpp) and come back to the hand when the event cannot start (py_Role_OnEvent)
#define RM8_NPC_READY 1
bool supported(int id)
{
	static const int ok[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 18, 19, 20, 21, 22, 23, 25, 26, 27, 28, 29, 30, 31, 32, 33, 45, 46, 47, 48, 49};
	static const int npc[] = {12, 14, 16, 17, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44};
	for(int k : ok) if(k == id) return true;
	static const bool all = RM8_NPC_READY || getenv("RM8_CARD_ALL") != nullptr;
	if(all) for(int k : npc) if(k == id) return true;
	return false;
}

// ---- projection of world positions to screen pixels (the game's Camera_ProjectWorldToScreen)
bool projectWith(const Matrix *m, const float p[3], float &sx, float &sy, const V2d *vw = nullptr)
{
	if(!g_cam || !g_cam->frameBuffer) return false;
	V3d d = {p[0] - m->pos.x, p[1] - m->pos.y, p[2] - m->pos.z};
	float xc = d.x * m->right.x + d.y * m->right.y + d.z * m->right.z;
	float yc = d.x * m->up.x + d.y * m->up.y + d.z * m->up.z;
	float zc = d.x * m->at.x + d.y * m->at.y + d.z * m->at.z;
	if(zc < 1.0f) return false;
	float W = (float)g_cam->frameBuffer->width, H = (float)g_cam->frameBuffer->height;
	V2d w = vw ? *vw : g_cam->viewWindow;
	sx = (0.5f - xc / (2.0f * w.x * zc)) * W;
	sy = (0.5f - yc / (2.0f * w.y * zc)) * H;
	return true;
}
bool project(const float p[3], float &sx, float &sy)
{
	if(!g_cam || !g_cam->frameBuffer) return false;
	return projectWith(g_cam->getFrame()->getLTM(), p, sx, sy);
}

// the card range: the default board view (yaw / pitch / distance of the board camera, no panning, no zoom) centred on the acting role, with the REAL window's size and view window
struct DefView { float yaw = 225.0f, pitch = 40.0f, dist = 0.0f, vwy = 0.225f; bool set = false; } g_def;
bool projectDefault(const float p[3], const float focus[3], float &sx, float &sy)
{
	float yr = g_def.yaw * 3.14159265f / 180.0f, pr = g_def.pitch * 3.14159265f / 180.0f, cp = cosf(pr);
	V3d eye = {focus[0] + g_def.dist * cp * sinf(yr), g_def.dist * sinf(pr), focus[2] - g_def.dist * cp * cosf(yr)};
	V3d fwd = {focus[0] - eye.x, -eye.y, focus[2] - eye.z};
	float l = sqrtf(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
	fwd.x /= l; fwd.y /= l; fwd.z /= l;
	Matrix m; m.setIdentity(); V3d up = {0, 1, 0}; m.lookAt(fwd, up); m.pos = eye;
	float W = (float)g_cam->frameBuffer->width, H = (float)g_cam->frameBuffer->height;
	V2d vw = {g_def.vwy * W / H, g_def.vwy};	// the window camera of rm8_app.cpp (setupWindowCamera): the vertical view stays, a wide screen sees more to the sides
	if(H > W){ vw.x = 0.3f; vw.y = 0.3f * H / W; }
	return projectWith(&m, p, sx, sy, &vw);
}

// ---- Im2D untextured quads (the pick markers)
void fillQuad(Camera *cam, float x0, float y0, float x1, float y1, uint32 argb)
{
	gl3::Im2DVertex v[4]; uint16 idx[6] = {0, 1, 2, 2, 1, 3};
	float px[4] = {x0, x1, x0, x1}, py[4] = {y0, y0, y1, y1};
	float rz = 1.0f / cam->nearPlane, z = im2d::GetNearZ();
	for(int q = 0; q < 4; q++){
		v[q].setScreenX(px[q]); v[q].setScreenY(py[q]); v[q].setScreenZ(z);
		v[q].setCameraZ(cam->nearPlane); v[q].setRecipCameraZ(rz);
		v[q].setColor((argb >> 16) & 255, (argb >> 8) & 255, argb & 255, argb >> 24); v[q].setU(0, rz); v[q].setV(0, rz);
	}
	im2d::RenderIndexedPrimitive(PRIMTYPETRILIST, v, 4, idx, 6);
}

}	// namespace

void rm8CardsSetDefaultView(float yawDeg, float pitchDeg, float dist, float vwy){ g_def.yaw = yawDeg; g_def.pitch = pitchDeg; g_def.dist = dist; g_def.vwy = vwy; g_def.set = true; }

// [V] Camera_IsWorldPosInView @0x4fd5a0: the projected position inside (20, 20) .. (width - 20, height - 100); true without a camera ([A] tests).
// The range of every card (the original tests it on the live camera, which is focused on the acting role, at the default distance, and cannot be panned while a
// card target is chosen; AI_PlanCardActions @0x4f9670 and py_Role/Terra/StPoint/Npc_InScn use the same test). [A] Port: one function for the human and the AI that
// does not look at the live camera (the player may have panned / zoomed): the default view on the acting role, with the real window's aspect (a wide screen sees
// more, equally for everybody) and the margins scaled with the height (the original's 20 / 100 px are for 768 lines).
bool rm8CardsPosInView(const float p[3])
{
	float sx, sy, f[3];
	if(!g_cam || !g_cam->frameBuffer) return true;
	int cur = rm8TurnCurrent();
	if(g_def.set && cur >= 0 && rm8RolesGetPos(cur, f)){ if(!projectDefault(p, f, sx, sy)) return false; }
	else if(!project(p, sx, sy)) return false;
	float W = (float)g_cam->frameBuffer->width, H = (float)g_cam->frameBuffer->height, s = H / 768.0f;
	return sx >= 20 * s && sx < W - 20 * s && sy >= 20 * s && sy < H - 100 * s;
}

// ---- inventory
int rm8CardMax(){ return 8; }
int rm8CardCount(int seat){ int n = 0; for(int i : deck(seat).id) n += i != 0; return n; }
int rm8CardAt(int seat, int slot){ return slot < 0 || slot > 7 ? 0 : deck(seat).id[slot]; }
bool rm8CardReady(int seat, int slot){ return slot >= 0 && slot < 8 && deck(seat).id[slot] && deck(seat).ready[slot]; }
bool rm8CardAdd(int seat, int id)	// [V] Deck_AddCard @0x49a650: first empty slot
{
	if(id <= 0 || (!g_defs.empty() && !def(id))) return false;
	Deck &d = deck(seat);
	for(int k = 0; k < 8; k++) if(!d.id[k]){ d.id[k] = id; d.ready[k] = false; return true; }
	return false;
}
bool rm8CardRemoveAt(int seat, int slot)	// [V] Deck_RemoveAndStore @0x49a740: the card goes to the temp register
{
	Deck &d = deck(seat);
	if(slot < 0 || slot > 7 || !d.id[slot]) return false;
	d.tempId = d.id[slot]; d.tempSlot = slot; d.id[slot] = 0; d.ready[slot] = false;
	return true;
}
int rm8CardFind(int seat, int id){ Deck &d = deck(seat); for(int k = 0; k < 8; k++) if(d.id[k] && d.id[k] == id) return k; return -1; }
bool rm8CardRemoveId(int seat, int id){ int s = rm8CardFind(seat, id); return s >= 0 && rm8CardRemoveAt(seat, s); }
bool rm8CardRestore(int seat)	// [V] Deck_RestoreCard @0x49a7c0: back into the slot it came from if that is still empty
{
	Deck &d = deck(seat);
	if(!d.tempId || d.tempSlot < 0 || d.tempSlot > 7 || d.id[d.tempSlot]) return false;
	d.id[d.tempSlot] = d.tempId; d.ready[d.tempSlot] = false;
	return true;
}
void rm8CardClearAll()
{
	g_deck.clear(); g_x.clear();
	// [V] Game_StartNewRound @0x4962a0 (calls @0x496849 / 0x496893 / 0x4968dd): every role gets the cards with array index
	// 8, 10, 11 (Remote Dice, Turn, Roadblock). [H] the story-mode start_card / pass_xx.bin cards are not given (no story mode)
	for(int s = 0; s < rm8TurnRoleCount(); s++) for(int idx : {8, 10, 11}) if(idx < (int)g_defs.size()) rm8CardAdd(s, g_defs[idx].id);
	// [A] test hook RM8_CARD_GIVE="seat:id,id;seat:id": more cards at the start of a game
	if(const char *e = getenv("RM8_CARD_GIVE")){
		std::stringstream ss(e); std::string part;
		while(std::getline(ss, part, ';')){
			size_t c = part.find(':'); if(c == std::string::npos) continue;
			int seat = atoi(part.substr(0, c).c_str());
			std::stringstream ids(part.substr(c + 1)); std::string t;
			while(std::getline(ids, t, ',')) rm8CardAdd(seat, atoi(t.c_str()));
		}
	}
}

// ---- data
bool rm8CardLoadDefs(const std::string &file)
{
	std::ifstream f(file);
	if(!f) return false;
	g_defs.clear(); g_idx.clear(); g_godDays.clear();
	std::string line;
	while(std::getline(f, line)){
		std::vector<std::string> p; std::stringstream ss(line); std::string t;
		while(std::getline(ss, t, '\t')) p.push_back(t);
		if(p.size() >= 3 && p[0] == "npc"){ g_godDays[atoi(p[1].c_str())] = atoi(p[2].c_str()); continue; }
		if(p.size() < 13 || p[0] != "card") continue;
		Def d; d.id = atoi(p[1].c_str()); d.name = p[2]; d.type = atoi(p[3].c_str()); d.level = atoi(p[4].c_str()); d.price = atoi(p[5].c_str());
		d.cond = (unsigned)strtoul(p[6].c_str(), nullptr, 10);
		for(int k = 0; k < 3; k++) d.speak[k] = atoi(p[7 + k].c_str());
		d.py = p[10] == "-" ? "" : p[10]; d.tex = p[11] == "-" ? "" : p[11]; d.desc = p.size() > 12 ? p[12] : "";
		g_idx[d.id] = (int)g_defs.size(); g_defs.push_back(d);
	}
	rm8Log("cards: %d definitions", (int)g_defs.size());
	return !g_defs.empty();
}
int rm8CardDefCount(){ return (int)g_defs.size(); }
const char *rm8CardName(int id){ const Def *d = def(id); return d ? d->name.c_str() : ""; }
const char *rm8CardTex(int id){ const Def *d = def(id); return d ? d->tex.c_str() : ""; }
int rm8CardPrice(int id){ const Def *d = def(id); return d ? d->price : 0; }
int rm8CardType(int id){ const Def *d = def(id); return d ? d->type : 0; }
int rm8CardLevel(int id){ const Def *d = def(id); return d ? d->level : 0; }
unsigned rm8CardCondition(int id){ const Def *d = def(id); return d ? d->cond : 0; }
const char *rm8CardDesc(int id){ const Def *d = def(id); return d ? d->desc.c_str() : ""; }
const char *rm8CardPy(int id){ const Def *d = def(id); return d && !d->py.empty() ? d->py.c_str() : nullptr; }
bool rm8CardSupported(int id){ return supported(id); }
int rm8CardRandom(int mode)	// [V] Card_SelectByDifficulty @0x4ae270
{
	int r = rm8GameRand() % 10;
	int lvl = mode == 0 ? r / 7 + 1 + r / 9 : r / 7 + 1;
	std::vector<int> ok;
	for(const Def &d : g_defs) if(d.level <= lvl) ok.push_back(d.id);
	return ok.empty() ? 0 : ok[rm8GameRand() % ok.size()];
}
int rm8CardGodDays(int npcId){ auto it = g_godDays.find(npcId); return it == g_godDays.end() ? 0 : it->second; }

// ---- role extras
int rm8CardsTurtle(int seat){ return X(seat).turtle; }
void rm8CardsSetTurtle(int seat, int days){ if(days > 0) X(seat).turtle = days; }	// [V] Role_SetTurtleDays @0x48b670
bool rm8CardsTurtleRoll(int seat)	// [V] Role_StartRollDice @0x484e50: turtle > 0 and no forced roll: days - 1 and the roll is forced to 1
{
	Extra &x = X(seat);
	if(x.turtle > 0){ x.turtle--; return true; }
	return false;
}
bool rm8CardsHasBubble(int seat){ return X(seat).bubble > 0; }	// [V] py_Role_HasBubble @0x4ff290 (+0x28c != 0)
bool rm8CardsHasBodyguard(int seat){ return rm8TurnCharId(seat) == 16 && X(seat).guard > 0; }	// [V] vf+0x64: 莎拉's class returns "the guard object exists", all others 0
// [A] test log RM8_RANGE_LOG=1: how much of the board is in the card range of the acting role (the same function serves the human and the AI)
static void rangeLog(int seat)
{
	int roles = 0, nodes = 0, plots = 0, npcs = 0; float p[3];
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(s != seat && rm8TurnStatus(s) < 3 && rm8RolesGetPos(s, p) && rm8CardsPosInView(p)) roles++;
	for(int n = 0; n < rm8GameNodeCount(); n++) if(rm8TurnNodePos(n, p) && rm8CardsPosInView(p)) nodes++;
	for(int t = 0; t < rm8GameTerraCount(); t++) if(rm8GameTerraPos(t, p) && rm8CardsPosInView(p)) plots++;
	for(int h = 1, N = rm8NpcCount(); h <= N; h++){ int n = rm8NpcNode(h); if(rm8NpcOnMap(h) && n >= 0 && rm8TurnNodePos(n, p) && rm8CardsPosInView(p)) npcs++; }
	rm8Log("cards: range seat %d (%s) window %dx%d: %d of %d other roles, %d of %d nodes, %d of %d plots, %d npcs", seat, rm8TurnHuman(seat) ? "human" : "AI",
		g_cam && g_cam->frameBuffer ? g_cam->frameBuffer->width : 0, g_cam && g_cam->frameBuffer ? g_cam->frameBuffer->height : 0, roles, rm8TurnRoleCount() - 1, nodes, rm8GameNodeCount(), plots, rm8GameTerraCount(), npcs);
}
void rm8CardsTurnStart(int seat, int step)
{
	if(step == 0){ static const bool rl = getenv("RM8_RANGE_LOG") != nullptr; if(rl) rangeLog(seat); }
	if(step == 0) rm8GameSetInvisible(seat, std::max(0, rm8GameInvisible(seat) - 1));	// [V] Role_DecrementField1E4 @0x48ed70
	if(step == 3 && X(seat).guard > 0) X(seat).guard--;						// [V] step 3: vf33 CPlayerTag_3__vf33_Decrement @0x480420: counter - 1, below 1 the guard is released
	if(step == 4 && X(seat).bubble > 0) X(seat).bubble--;					// [V] step 4: the bubble counter (+0x28c)
}
// [V] Role_StayOrBeforeRoll @0x484610: the stay flag (+0x1d0, set by Role.Settle) clears, text str563, then the landing event of the node again
// [A] test hook RM8_CARD_USE="seat:slot,slot;seat:slot": the seat uses one of these slots per turn (from day RM8_CARD_USE_DAY, default 0) before rolling
static bool testUse(int seat)
{
	static const char *e = getenv("RM8_CARD_USE");
	static std::map<int, std::vector<int>> plan; static bool parsed = false;
	if(!e) return false;
	if(!parsed){
		parsed = true;
		std::stringstream ss(e); std::string part;
		while(std::getline(ss, part, ';')){
			size_t c = part.find(':'); if(c == std::string::npos) continue;
			std::stringstream sl(part.substr(c + 1)); std::string t; int sd = atoi(part.substr(0, c).c_str());
			while(std::getline(sl, t, ',')) plan[sd].push_back(atoi(t.c_str()));
		}
	}
	auto it = plan.find(seat);
	if(it == plan.end() || it->second.empty()) return false;
	const char *d = getenv("RM8_CARD_USE_DAY");
	if(rm8GameDay() < (d ? atoi(d) : 0)) return false;
	int slot = it->second.front(); it->second.erase(it->second.begin());
	rm8Log("cards: test use of slot %d (card %d) by seat %d", slot, rm8CardAt(seat, slot), seat);
	rm8TurnEnqueue(seat, 2, slot, 0);
	return true;
}

bool rm8CardsStayCheck(int seat)
{
	if(testUse(seat)) return true;
	Extra &x = X(seat);
	if(!x.stay) return false;
	x.stay = 0;
	info(seat, rm8StrFmt(563, rm8TurnName(seat)));
	rm8TurnEnqueue(seat, rm8GameShape(seat) == 6 ? 0x12 : 0x1d, rm8TurnNode(seat), 0);
	return true;
}
void rm8CardsSettle(int seat){ X(seat).stay = 1; }	// [V] CPlayerTag__SetDeferredAction @0x4911b0

// news (news.cpp): the stay days of the current status (+0x1D8) - the amnesty / hospital fire / Thanksgiving set them to 0 (the role leaves at its next turn start)
void rm8CardsSetStatusDays(int seat, int days){ if(days >= 0) X(seat).days = days; }
// [V] Role_SetVanishStatus @0x48ae10 (the UFO of the news "alien abduction", message 0x5F): not for a bankrupt role; status 6 for `days` days
bool rm8CardsVanish(int seat, int days)
{
	if(rm8TurnStatus(seat) == 7) return false;
	rm8TurnSetStatus(seat, 6); X(seat).days = days;
	return true;
}

// [V] turn start step 9 for the non-normal statuses (Role_RunTurnStartStep @0x4842f0): 1 sleep (@0x48aff0), 5 prison (@0x48ac70)
// [V] CPlayerTag__EnterHotelStatus @0x4852e0 (the role has walked into the hotel, message 0x13): status 3, +0x1D8 = 1 day. [A] no walk in: the role is simply hidden (status > 2)
void rm8CardsEnterHotel(int seat){ if(rm8TurnStatus(seat) != 7){ rm8TurnSetStatus(seat, 3); X(seat).days = 1; } }
bool rm8CardsStatusStep(int seat)
{
	int st = rm8TurnStatus(seat);
	if(st == 3){	// [V] Role_TickHotelStay @0x4851a0: while the plot of the node is a hotel and days are left: str 52 "%s<str#52>\還有%d天", days - 1; else the stay ends (0x14 leave the hotel, 0x61)
		Extra &h = X(seat);
		int t = rm8GameNodeTerra(rm8TurnNode(seat));
		bool inHotel = t >= 0 && rm8GameTerraType(t) == 2 && rm8GameTerraBuild(t) == 4;
		if(getenv("RM8_DEBUG_CARDS")) rm8Log("cards: seat %d hotel stay, %d days left (plot %d, hotel %d)", seat, h.days, t, (int)inHotel);
		if(inHotel && h.days > 0){ info(seat, rm8StrFmt(52, rm8TurnName(seat), h.days)); h.days--; h.stay = 0; }
		else{ h.days = 0; rm8TurnEnqueue(seat, 0x14, 0, 0); rm8TurnEnqueue(seat, 0x25, 0, 0); }	// [V] 0x14 (the walk out, status 0, events.cpp -> roles.cpp), then 0x61 [A] -> 0x25(0)
		return true;
	}
	if(st != 1 && st != 4 && st != 5 && st != 6) return false;	// 6 = vanished (the news "alien abduction", Role_TickVanishStatus @0x48ae80)
	Extra &x = X(seat);
	static const bool dbg = getenv("RM8_DEBUG_CARDS") != nullptr;
	if(dbg) rm8Log("cards: seat %d status %d, %d days left", seat, st, x.days);
	if(x.days < 1){	// the stay is over: status 0, back to the node, continue with 0x25(0) / 0x61
		x.days = 0; rm8TurnSetStatus(seat, 0);
		if(st == 5){ info(seat, rm8StrFmt(72, rm8TurnName(seat))); rm8TurnEnqueue(seat, 0x25, 0, 0); }
		else if(st == 4){ info(seat, rm8StrFmt(62, rm8TurnName(seat))); rm8TurnEnqueue(seat, 0x25, 0, 0); }
		else rm8TurnEnqueue(seat, 0x25, 0, 0);
	}else{
		info(seat, st == 5 ? rm8StrFmt(71, rm8TurnName(seat), x.days) : st == 4 ? rm8StrFmt(61, rm8TurnName(seat), x.days) : st == 6 ? rm8StrFmt(359, rm8TurnName(seat), x.days) : rm8StrFmt(562, rm8TurnName(seat), x.days));
		if(st != 6 && rm8GameRand() % 100 < 0x28) rm8RoleSay(seat, st == 5 ? 0x14 : st == 4 ? 0x15 : 0x16);	// [V] @0x48ac70 / 0x48a820 / 0x48aff0: 40 % emote 0x14 prison / 0x15 hospital / 0x16 sleep (the vanish tick has none)
		x.days--; x.stay = 0;
	}
	return true;
}

// ---- Python (python.cpp)
namespace {
bool execute(int me, const Def &d, int param)
{
	(void)me;
	long res = 0;
	if(d.py.empty() || !rm8PyCardCall(d.py.c_str(), true, param, d.speak[1], d.speak[2], res)) return false;	// [V] Card_SetCustomValue @0x4ad7a0: (param, speakNo[1], speakNo[2])
	return res != 0;
}

// ---- the card checks (vf1 of the card classes, Card_CreateCardObject @0x4ad060)
// [V] Card_CheckConditionAndSelectTarget @0x4ad440 and the five overrides (@0x4ad910 vehicle, @0x4ad9d0 remote dice,
// @0x4adab0 swap position, @0x4adc60 grab all cards, @0x4add60 bodyguard)
bool checkCard(int me, int slot, const Def &d)
{
	if(d.type == 2) return false;
	auto speak0 = [&](){ rm8TurnEnqueue(me, 0x5A, d.speak[0], 0); };
	switch(d.id){
	case 7: case 8: {	// vehicle: the same vehicle cannot be used again
		if(rm8TurnDiceMax(me) == (d.id == 7 ? 2 : 3)) return false;
		speak0(); rm8TurnEnqueue(me, 3, slot, 1); return true; }
	case 9:	// remote dice: not while a turtle; the number dialog (kind 0x82)
		if(X(me).turtle > 0) return false;
		speak0(); rm8TurnEnqueue(me, 1, 0, 0x82); rm8TurnEnqueue(me, 0x1C, 0, 0); rm8TurnEnqueue(me, 3, slot, (int)0x80000000);
		return true;
	case 31: {	// swap position: another role that is not bankrupt (status < 3) on another node
		bool any = false;
		for(int s = 0; s < rm8TurnRoleCount() && !any; s++) any = s != me && rm8TurnStatus(s) < 3 && rm8TurnNode(s) != rm8TurnNode(me);
		if(!any) return false;
		speak0(); rm8TurnEnqueue(me, 1, 0x235, 0x85); rm8TurnEnqueue(me, 0x1C, 0, 0); rm8TurnEnqueue(me, 3, slot, (int)0x80000000);
		return true; }
	case 45: {	// grab all cards: needs 50 points
		if(rm8GamePoints(me) < 50) return false;
		speak0();
		unsigned c = ((d.cond & 0xffff) | 0x5000);
		rm8TurnEnqueue(me, 1, (int)(c >> 8), (int)((d.cond & 0xff) | (unsigned)(d.id + 9) << 16));
		rm8TurnEnqueue(me, 3, slot, (int)0x80000000);
		return true; }
	case 46:	// bodyguard: not while possessed
		if(rm8GameShape(me) != 0) return false;
		rm8TurnEnqueue(me, 3, slot, 1); return true;
	}
	int t = curTerra(me), own = t >= 0 ? rm8GameTerraOwner(t) : -1;
	unsigned hi = d.cond & 0xff000000u;
	if(hi == 0x08000000u){ if(t < 0 || own == me) return false; }
	else if(hi == 0x02000000u){ if(t < 0 || rm8GameTerraType(t) != 1) return false; }
	else if(hi == 0x04000000u){ if(t < 0 || own != me) return false; }
	else if(hi == 0x10000000u){ if(t < 0 || own >= 0) return false; }
	unsigned local = 0, kind = d.cond & 0xff;
	if(kind == 1){ if((d.cond & 0xf00) != 0x200){ local = (d.cond & 0xffff) | 0x4000; if(d.type != 1 && d.type == 4) local = (d.cond & 0xffff) | 0x6000; } }
	else if(kind == 8){ if((d.cond & 0xf000) != 0x8000) local = d.cond & 0xffffff; }
	else if(kind == 4){ if((d.cond & 0xf000) != 0x4000) local = d.cond & 0xffff; }
	else if(kind == 0x20){
		if((d.cond & 0x8000) == 0) local = 0x220;
		else if(rm8GameShape(me) == 0) { /* [H] time bomb on the role: not in the port (no NPC objects) */ }
	}else if(kind == 2){ local = d.cond & 0xf00f; if(d.type != 1) local |= 0x100; }
	if(local == 0) rm8TurnEnqueue(me, 3, slot, 1);
	else { rm8TurnEnqueue(me, 1, (int)(local >> 8), (int)((local & 0xff) | (unsigned)(d.id + 9) << 16)); rm8TurnEnqueue(me, 3, slot, (int)0x80000000); }
	if(d.speak[0] >= 0) rm8TurnEnqueueAt(me, 0, 0x5A, d.speak[0], 0);	// [V] thunk_FUN_00484930(0, speakNo[0])
	return true;
}

// [A] RM8_DEBUG_CARDS: one line per role (cash, deposit, points, node, vehicle, possession, status, turtle, bubble, invisible, cards) and the plots (owned / total level) before a card is used and
// when the role asks for the dice again after it (message 100), to see the effect of a card in the log
int g_snapSeat = -1; float g_snapT = -1;
void snapshot(const char *tag)
{
	std::string line = fmt("cards: snapshot %s:", tag);
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		std::string cs; for(int k = 0; k < 8; k++) if(rm8CardAt(s, k)) cs += (cs.empty() ? "" : ",") + std::to_string(rm8CardAt(s, k));
		line += fmt(" [s%d cash %ld dep %ld pts %ld node %d veh %d shape %d st %d turtle %d bub %d inv %d stay %d | %s]", s, rm8GameCash(s), rm8GameDeposit(s), rm8GamePoints(s), rm8TurnNode(s), rm8TurnDiceMax(s),
			rm8GameShape(s), rm8TurnStatus(s), X(s).turtle, X(s).bubble, rm8GameInvisible(s), X(s).stay, cs.c_str());
	}
	int own[8] = {0}, lv[8] = {0}, none = 0;
	for(int t = 0; t < rm8GameTerraCount(); t++){ int o = rm8GameTerraOwner(t); if(o >= 0 && o < 8){ own[o]++; lv[o] += rm8GameTerraLevel(t); } else none++; }
	line += fmt(" plots: free %d", none);
	for(int s = 0; s < rm8TurnRoleCount(); s++) line += fmt(" s%d %d/%d", s, own[s], lv[s]);
	line += " npcs:";
	for(int h = 1, N = rm8NpcCount(); h <= N; h++){ int n = rm8NpcNode(h); if(n >= 0) line += fmt(" %d@%d", rm8NpcId(h), n); }
	rm8Log("%s", line.c_str());
}

// [V] Role_BeginCardUse @0x4897a0 (msg 2)
void beginUse(int me, int slot)
{
	static const bool dbgSnap = getenv("RM8_DEBUG_CARDS") != nullptr;
	if(dbgSnap){ snapshot("before"); g_snapSeat = me; }
	Deck &dk = deck(me);
	const Def *d = slot >= 0 && slot < 8 && dk.id[slot] ? def(dk.id[slot]) : nullptr;
	if(!d){ rm8TurnEnqueue(me, 100, 0, 0); return; }
	if(!supported(d->id)){	// [A] effect not ported yet: the card stays in the hand
		info(me, fmt("%s的效果尚未移植", d->name.c_str())); rm8TurnEnqueue(me, 100, 0, 0); return;
	}
	if(!checkCard(me, slot, *d)){
		if(getenv("RM8_DEBUG_CARDS")) rm8Log("cards: seat %d (%s) card %d (%s): the check of the card refuses it (type, standing place or vehicle), it stays in the hand", me, rm8TurnName(me), d->id, d->name.c_str());
		rm8TurnEnqueue(me, 100, 0, 0); return;
	}
	dk.ready[slot] = true;
	float p[3]; if(rm8RolesGetPos(me, p)) rm8TurnFocus(p);	// [V] Camera_SetFocusPoint(role)
}

// [V] Role_FinishCardUse @0x4896e0 (msg 3)
void finishUse(int me, int slot, int result)
{
	Deck &dk = deck(me);
	if(slot < 0 || slot > 7 || !dk.id[slot]) return;
	const Def *d = def(dk.id[slot]);
	if(getenv("RM8_DEBUG_CARDS") && g_snapSeat != me && result != 0 && (unsigned)result != 0x80000000u){ snapshot("before(AI)"); g_snapSeat = me; }
	bool ok = d && result != 0 && (unsigned)result != 0x80000000u && execute(me, *d, result - 1);
	static const bool dbg = getenv("RM8_DEBUG_CARDS") != nullptr;
	if(dbg) rm8Log("cards: seat %d (%s) uses card %d (%s) result %d -> %s", me, rm8TurnName(me), d ? d->id : 0, d ? d->name.c_str() : "?", result, ok ? "executed" : "not used");
	if(!ok) g_snapSeat = -1; else g_snapT = 0;
	if(!ok){ dk.ready[slot] = false; rm8TurnEnqueue(me, 100, 0, 0); }	// Deck_ClearCardReady, back to Role_BeforeRollDice
	else { rm8StoryCardUsed(me, d->id); rm8CardRemoveAt(me, slot); }	// [V] this+0x4c0[id] = 1 (used flag: the story's puzzle conditions >= 100), Deck_RemoveAndStore
}

// [V] Role_GiveCardToRole @0x489830 (msg 6): the card of role `victim` (slot b = 1..8, 9 = automatic) goes to the queue owner
bool giveCard(int me, int victim, int b)
{
	if(!validSeat(victim) || b == 0 || (unsigned)b == 0x80000000u) return false;
	Deck &v = deck(victim);
	int slot = -1;
	if(b >= 1 && b <= 8) slot = b - 1;
	else if(b == 9){
		slot = rm8CardFind(victim, 25);
		if(slot < 0) slot = rm8CardFind(victim, 18);
		if(slot < 0){
			int best = 0;
			for(int k = 0; k < 8; k++){
				if(!v.id[k]) continue;
				const Def *c = def(v.id[k]);
				if(v.id[k] > 0x23 && v.id[k] < 0x2d){ slot = k; break; }	// god cards first
				int lv = c ? c->level : 0;
				if(best < lv || (lv == best && (rm8GameRand() & 1))){ best = lv; slot = k; }
			}
		}
	}
	if(slot < 0 || slot > 7 || !v.id[slot]) return false;
	int id = v.id[slot];
	rm8CardRemoveAt(victim, slot);
	// [V] the temp register of the victim now holds the card; Deck_AddCard to the grabber
	if(!rm8CardAdd(me, id)){ rm8CardAdd(victim, id); return false; }
	// [V] @0x489a35..0x489a74 (disassembled): the victim's queue gets Speak(0x4a) (thunk_FUN_00484890 = Role_EnqueueMsg(0x5A, n, 0), speech number 0x4a, NOT message 0x4a) and
	// 0x2E(grabber + 1); the grabber's queue gets 0x2E(victim + 1) at the front (control goes to the victim for the line and comes back)
	rm8TurnEnqueue(victim, 0x5A, 0x4a, 0); rm8TurnEnqueue(victim, 0x2e, me + 1, 0);
	rm8TurnEnqueueAt(me, 0, 0x2e, victim + 1, 0);
	return true;
}

}	// namespace

// ---- role status effects of the cards (messages queued by the CardPy scripts through python.cpp)
namespace {

// [V] CPlayerTag__SendToPrison @0x48a9c0: status 5, days, the role is placed on the prison node (event 7)
void sendToPrison(int me, int days)
{
	int prison = -1;
	for(int n = 0, N = rm8GameNodeCount(); n < N && prison < 0; n++) if(rm8GameNodeEvent(n) == 7) prison = n;
	if(prison < 0 || rm8TurnStatus(me) == 7){ rm8Log("cards: no prison node / role out"); return; }
	if(rm8TurnStatus(me) == 5){ X(me).days += days; return; }
	static const bool dbg = getenv("RM8_DEBUG_CARDS") != nullptr;
	if(dbg) rm8Log("cards: seat %d goes to prison (node %d) for %d days", me, prison, days);
	rm8TurnSetStatus(me, 5); X(me).days = days;
	int nb[8], n = rm8TurnLinks(prison, nb, 8), to = n > 0 ? nb[0] : prison;
	for(int k = 0; k < n; k++) if(true){ int m2[8]; if(rm8TurnLinks(nb[k], m2, 8) >= 2){ to = nb[k]; break; } }	// [V] the first link with >= 2 links
	rm8TurnPlaceAt(me, prison, to);
	// [V] nothing is cleared here (SendToPrison @0x48a9c0 does not touch the queues): the show of rm8NpcTransfer (police car) has run before, and when the role's queue runs empty in state 8
	// the engine itself waits 60 frames and ends the turn (@0x481df0 state 8: Role_GetWaitFrames(2), 0x65, 0x66; roles.cpp dispatch). The callers that need a clean queue clear it themselves
	if(!rm8StoryHospitalPrison(me)) rm8RoleSayAt(me, 0, 4);	// [V] @0x48a9c0: thunk_FUN_00484930(0, 4) at the end (not for 莎拉's first visit in the story mode: drama 18 + the skip question)
}

// [V] CPlayerTag__SendToHospital @0x48a570 (message 0x18): the same as the prison with status 4 and the hospital node (event 8);
// already in hospital: the days add up
void sendToHospital(int me, int days)
{
	int hosp = -1;
	for(int n = 0, N = rm8GameNodeCount(); n < N && hosp < 0; n++) if(rm8GameNodeEvent(n) == 8) hosp = n;
	if(hosp < 0 || rm8TurnStatus(me) == 7){ rm8Log("cards: no hospital node / role out"); return; }
	if(rm8TurnStatus(me) == 4){ X(me).days += days; return; }
	rm8TurnSetStatus(me, 4); X(me).days = days;
	int nb[8], n = rm8TurnLinks(hosp, nb, 8), to = n > 0 ? nb[0] : hosp;
	for(int k = 0; k < n; k++){ int m2[8]; if(rm8TurnLinks(nb[k], m2, 8) >= 2){ to = nb[k]; break; } }
	rm8TurnPlaceAt(me, hosp, to);
	if(getenv("RM8_DEBUG_CARDS")) rm8Log("cards: seat %d goes to hospital (node %d) for %d days", me, hosp, days);
	// [V] as sendToPrison (SendToHospital @0x48a570)
	if(!rm8StoryHospitalPrison(me)) rm8RoleSayAt(me, 0, 4);	// [V] @0x48a570: thunk_FUN_00484930(0, 4) (story: see sendToPrison)
	rm8StoryDogBiteDone(me);	// [V] the 0x58(2) that the dog bite queued behind the 0x18 (story.cpp; the queue clear above would have dropped it)
}

// [V] NPC_CreateRandom_Type1 @0x4a4510 (messages 0x19 / 0xC9, the prison) / Type2 @0x4a45f0 (message 0x18, the hospital): the police car / ambulance show of npc_act.cpp
// runs first (role state 9); the move itself (SendToPrison / SendToHospital: role vf+0x74 / +0x70) happens when it ends. Without the show it happens at once.
void startTransfer(int me, int kind, int days)
{
	if(!rm8NpcTransfer(me, kind, days, [](int s, int k, int d){ if(k == 1) sendToPrison(s, d); else sendToHospital(s, d); })){
		if(kind == 1) sendToPrison(me, days); else sendToHospital(me, days);
	}
}

// [V] CPlayerTag__OnMissileCardUse @0x4901d0 (message 0x19): a victim holding the frame-up card (24) passes the prison on
bool frameUp(int me, int days)
{
	if(rm8CardFind(me, 24) < 0) return false;
	if(!rm8TurnHuman(me)){
		if(rm8GameRand() % 3 >= days) return false;
		std::vector<int> c;
		for(int s = 0; s < rm8TurnRoleCount(); s++) if(s != me && rm8TurnStatus(s) != 7) c.push_back(s);
		if(c.empty()) return false;
		int t = c[rm8GameRand() % c.size()];
		rm8TurnClearQueue(me);
		info(me, rm8StrFmt(529, rm8TurnName(me), rm8TurnName(t)));
		rm8TurnEnqueue(me, 0x4c, days, t + 1);
		return true;
	}
	rm8TurnEnqueueAt(me, 0, 1, 0x239, 0x84); rm8TurnEnqueueAt(me, 1, 0x1C, 0, 0);
	rm8TurnEnqueueAt(me, 2, 0x4c, days, (int)0x80000000); rm8TurnEnqueueAt(me, 3, 0xc9, days, 0);
	return true;
}

void giveAll(int me, int tgt)	// [V] Role_GiveTicketsAndAllCardsTo @0x4895b0 (message 0x43)
{
	if(tgt == me || !validSeat(tgt)) return;
	rm8GameAddPoints(me, -50); rm8GameAddPoints(tgt, 50);
	int at = 0;
	for(int k = 0; k < 8; k++){ int id = rm8CardAt(tgt, k); if(id) rm8TurnEnqueueAt(me, at++, 4, id - 1, 1); }	// message 4 (events.cpp): card array index
	Deck &d = deck(tgt); for(int k = 0; k < 8; k++){ d.id[k] = 0; d.ready[k] = false; } d.tempId = 0;		// Deck_ClearSlots @0x49a5e0
}

// [V] Terra_BuildNeighborList @0x49fd70 (the layers of the road links around a node)
std::vector<int> neighbourhood(int node, int radius)
{
	std::vector<int> set{node}; size_t lo = 0;
	for(int layer = 0; layer < radius; layer++){
		size_t hi = set.size();
		for(size_t k = lo; k < hi; k++){
			int nb[16]; int c = rm8TurnLinks(set[k], nb, 16);
			for(int j = 0; j < c; j++) if(std::find(set.begin(), set.end(), nb[j]) == set.end()) set.push_back(nb[j]);
		}
		lo = hi;
	}
	return set;
}

void swapTerraOwners(int t1, int t2)
{
	int o1 = rm8GameTerraOwner(t1), o2 = rm8GameTerraOwner(t2);
	if(o1 == o2) return;	// [V] u_Role_ProcessNpcMsg: only when the owners differ
	rm8GameSetTerra(t1, o2, -1, -1); rm8GameSetTerra(t2, o1, -1, -1);
}

}	// namespace

static bool handleMessage(int me, int cmd, int a, int b);
bool rm8CardMessage(int me, int cmd, int a, int b)
{
	if(!validSeat(me)) return false;
	bool r = handleMessage(me, cmd, a, b);
	static const bool dbg = getenv("RM8_DEBUG_CARDS") != nullptr;
	if(dbg && r && cmd != 2 && cmd != 3) rm8Log("cards: seat %d message 0x%x (%d, %d)", me, cmd, a, b);
	return r;
}
static bool handleMessage(int me, int cmd, int a, int b)
{
	switch(cmd){
	case 2: beginUse(me, a); return true;
	case 3: finishUse(me, a, b); return true;
	case 5: return false;	// Role_ShowAndRemoveCardInSlot / Role_QueueRemoveRandomCards: events.cpp (card window + the 70 % emote)
	case 6: if(!giveCard(me, a, b)) rm8CardRestore(me); return true;	// [V] dispatcher @0x481df0: a failed give restores the grab card
	case 99: if(a >= 0 && a < 8 && deck(me).id[a]) deck(me).ready[a] = true; return true;	// Deck_SetCardReady @0x49a830
	case 0x3b: rm8CardRestore(me); return true;
	case 0x43: giveAll(me, a - 1); return true;
	case 0x48: return true;				// Role_RefreshDeckUi: the HUD reads the deck every frame
	case 0x19: if(!frameUp(me, a)) startTransfer(me, 1, a); return true;	// Role.Prison: [A] without the arrest scene
	case 0x18: if(a > 0) startTransfer(me, 2, a); return true;	// [V] Role.Hospital / dog bite / mine / minigame 21
	case 0x4c:	// [V] CPlayerTag__OnTimebombCardUse @0x490500: the frame-up card is used, the prison goes to role b - 1
		if(b - 1 >= 0 && validSeat(b - 1)){
			rm8CardRemoveId(me, 24); rm8TurnClearQueue(me);
			rm8TurnEnqueue(b - 1, 0x19, a, me + 1);
		}
		return true;
	case 0xc9: startTransfer(me, 1, a); return true;	// the arrest without a frame-up
	case 0x28:	// [V] Role_PutOthersToSleep @0x48c330: every other role with status < 3 sleeps `a` days (Role_SetSleepStatus @0x48afa0)
		for(int s = 0; s < rm8TurnRoleCount(); s++) if(s != me && rm8TurnStatus(s) < 3){ rm8TurnSetStatus(s, 1); X(s).days = a; }
		return true;
	case 0x3d: {	// Role.ChgPos: a == 0 swap with role b (@0x48ea50 / @0x490c60), else fly to node a - 1 (@0x48e920)
		if(a == 0){
			if(validSeat(b) && b != me) rm8TurnSwapPlaces(me, b);
		}else{
			int node = a - 1, nb[8], n = rm8TurnLinks(node, nb, 8), to = n > 0 ? nb[n - 1] : node;
			for(int k = 0; k < n; k++) if(rm8GameRand() & 1){ to = nb[k]; break; }	// [V] first link where rand() is odd, else the last
			rm8TurnPlaceAt(me, node, to);
		}
		float p[3]; if(rm8RolesGetPos(me, p)) rm8TurnFocus(p);
		return true; }
	case 0x3e: X(me).bubble = 10; return true;			// [V] CPlayerTag__InitializeEventDisplay(10) (bubble, +0x28c)
	case 0x46: rm8GameSetInvisible(me, 10); return true;		// [V] CPlayerTag__SetTimerValue(10) (+0x1e4, rent exempt)
	case 0x24: rm8NpcBecome(me, a, 0); return true;			// [V] Role_SetPossession @0x48b6e0 -> npc.cpp (days 0 = npcinfo date)
	case 0xD: {	// [V] Role_StartLandLevelDown @0x4875e0 (plot, emote flag): BuildLevel_StartDecrement @0x4eef80 -> NPC_InitDisplay @0x4f4f20: the demolition task (art 6002 + sample 3, the building's
		// action 1 / 3, level - 1 when the effect has ended: boardfx.cpp); without a building the level is stepped at once (BuildLevel_StepDecrement @0x4ef860) and nothing else happens.
		// The task started: the role is in state 9, flag != 0: 70 % and the plot is the role's own -> the emote 5 (thunk_FUN_00484930(0, 5))
		if(a < 0 || a >= rm8GameTerraCount() || rm8GameTerraLevel(a) <= 0) return true;
		if(!rm8BoardFxDemolish(me, a)){ rm8GameSetTerra(a, -2, rm8GameTerraLevel(a) - 1, -1); return true; }
		if(b != 0 && rm8PresentChance70() && rm8GameTerraOwner(a) == me) rm8RoleSayAt(me, 0, 5);
		return true; }
	case 0xE:	// [V] u_Role_ProcessNpcMsg @0x487b00 -> NPC_CreateFortuneTeller @0x4eee90 (class 0x5E6898, only when the two owners differ): both plots blink for 1.6 s, then Land_SetOwner
		// swaps the owners and sample 4 plays (Cls_5e6898__vf3 @0x4f5690; boardfx.cpp). No emote.
		if(a >= 0 && b >= 0 && a < rm8GameTerraCount() && b < rm8GameTerraCount() && a != b && rm8GameTerraOwner(a) != rm8GameTerraOwner(b))
			rm8BoardFxBlink(me, {a, b}, 4, [a, b](){ swapTerraOwners(a, b); });
		return true;
	case 0x10:	// [V] u_Role_ProcessBuildMsg @0x488360 -> TerraMgr_GetBuildableRoles @0x4f2040 -> Building_AnimateMultiple @0x4f3b20 (class 0x5E6988, boardfx.cpp): every plot of the street with an owner and
		// level < 5 gets art 6121 + sample 0x1E, its old model's action 1 / 3, then level + 1 (free) and the new model's action 0 / 2; none: the card comes back
		if(!rm8BoardFxAngelBuild(me, a)) rm8CardRestore(me);
		return true;
	case 0x44: {	// [V] u_Role_MsgHandler_TerraQueryBySeat @0x48d2c0 -> Street_ProcessTerraBuild @0x4f2190 -> Building_InitializeWithEffect @0x4f40a0 (class 0x5E69D8): every plot of the street (up to 63) blinks
		// for 1.6 s, then sample 1 and Land_SetOwner(role) on all of them (Building_CompleteWithSound @0x4f4240; boardfx.cpp)
		std::vector<int> plots;
		for(int t = 0; t < rm8GameTerraCount() && plots.size() < 63; t++) if(rm8GameTerraStreet(t) == a && a > 0) plots.push_back(t);
		if(plots.empty()) return true;
		auto own = [plots, me](){ for(int t : plots) if(rm8GameTerraOwner(t) != me) rm8GameSetTerra(t, me, -1, -1); };
		if(!rm8BoardFxBlink(me, plots, 1, own)) own();
		return true; }
	case 0x2F: rm8GameStreetClose(a, b); return true;		// [V] Terra_LoadCloseIcon @0x42d3b0
	case 0x30: rm8GameStreetFlow(a, b); return true;		// [V] Terra_SetHeight @0x42d4c0
	case 0x36: case 0x37: {	// [V] Terra.Bomb: the dispatcher @0x481df0 calls CPlayerTag__OnHolidayCardUse(node, 0x1777 / 0x1778, 0x12 / 0x13, radius 1 / 3, type 0 / 1, days 3 / 5) and queues 0x1e, 0x67(9)
		bool nuke = cmd == 0x37;
		if(a >= 0 && a < rm8GameNodeCount()) rm8NpcBlast(me, a, nuke ? 0x1778 : 0x1777, nuke ? 0x13 : 0x12, nuke ? 3 : 1, nuke ? 5 : 3, nuke ? 1 : 0);
		rm8TurnEnqueue(me, 0x1e, 0, 0); rm8TurnEnqueue(me, 0x67, 9, 0);
		return true; }
	case 0x62: {	// [V] CPlayerTag__OnEventConditionCheck @0x48dc80 (node a, radius b): a bubble inside the blast area bursts (CleanupEventDisplay)
		std::vector<int> nodes = neighbourhood(a, b);
		for(int s = 0; s < rm8TurnRoleCount(); s++)
			if(rm8TurnStatus(s) < 3 && X(s).bubble > 0 && std::find(nodes.begin(), nodes.end(), rm8TurnNode(s)) != nodes.end()) X(s).bubble = 0;
		return true; }
	case 0x45:	// [V] Role.Bodyguard: vf+0x80. 莎拉's class (CPlayerTag_3__vf32 @0x4803e0 = Path_Create @0x480260): the guard (7 turns) appears, sample 0x2e. Every other role
		// (CPlayerTag__vf15_ShowState @0x48ecf0): its queue is cleared, effect article 0x1787 (message 0x2c), hospital 3 days (0x18)
		if(rm8TurnCharId(me) == 16){ X(me).guard = 7; rm8SndPlay(0x2e); }
		else { rm8TurnClearQueue(me); rm8TurnEnqueue(me, 0x2c, 0x1787, 0); rm8TurnEnqueue(me, 0x18, 3, 0); }
		return true;
	// 0x2c (effect on the role), 0x2e (hand-over to another role), 0x53 (sound + wait) are handled by present.cpp and 0x4a (the plot loses its buildings) by news.cpp, both asked before this
	// module (game.cpp rm8GameMessage); nothing is swallowed here any more
	}
	return false;
}

// ---- selection dialogs (message 1 kinds 0x82 / 0x83 / 0x84 / 0x85 and the board picks 1 / 2 / 8 / 0x20)
namespace {

struct Pick { bool active = false; int seat = -1, kind = 0, flags = 0, cardId = 0; } g_pick;
int g_dlgSeat = -1;
int g_chsMap[4] = {-1, -1, -1, -1};

// the dialogs answer through rm8TurnModalResult (Role_CloseModalUi @0x4889b0) and hide themselves
void dlgResult(Rm8Ui *u, int value){ rm8UiShow(u, false); rm8TurnModalResult(g_dlgSeat, value); }

void showDice(int me)	// [V] Role_OpenModalUi 0x82 @0x4883c0: dice.ui, text str561; button id 1..6 = the number (Execute param = id - 1)
{
	static Rm8Ui *u = nullptr;
	if(!u){ u = rm8UiLoad("dice", RM8UI_DIALOG); if(u) rm8UiOnClick(u, [](int id){ if(id >= 1 && id <= 6) dlgResult(rm8UiLoad("dice", RM8UI_DIALOG), id); }); }
	if(!u) return;
	g_dlgSeat = me; rm8UiSetTextByName(u, "Plane", rm8Str(561)); rm8UiShow(u, true);
}

void showDeckList(int me, int target)	// [V] 0x83: rob.ui, the cards of role `target` (card picture on button k + 1, name on label k + 9)
{
	static Rm8Ui *u = nullptr;
	if(!u){ u = rm8UiLoad("rob", RM8UI_DIALOG); if(u) rm8UiOnClick(u, [](int id){ if(id >= 1 && id <= 8) dlgResult(rm8UiLoad("rob", RM8UI_DIALOG), id); }); }
	if(!u) return;
	g_dlgSeat = me;
	for(int k = 0; k < 8; k++){
		int id = rm8CardAt(target, k); const Def *d = id ? def(id) : nullptr;
		rm8UiSetPicFile(u, k + 1, d ? d->tex : ""); rm8UiSetText(u, k + 9, d ? d->name : ""); rm8UiSetEnabled(u, k + 1, d != nullptr);
	}
	rm8UiShow(u, true);
}

void showChsPlayer(int me, int textId, bool exceptBroke)	// [V] 0x84 / 0x85: chsplayer.ui, one button per role ("npc\<pic>_12")
{
	static Rm8Ui *u = nullptr;
	if(!u){
		u = rm8UiLoad("chsplayer", RM8UI_DIALOG);
		if(u) rm8UiOnClick(u, [](int id){ if(id >= 1 && id <= 4 && g_chsMap[id - 1] >= 0) dlgResult(rm8UiLoad("chsplayer", RM8UI_DIALOG), g_chsMap[id - 1] + 1); });
	}
	if(!u) return;
	g_dlgSeat = me; rm8UiSetTextByName(u, "Plane", rm8Str(textId));
	int n = 0;
	for(int s = 0; s < rm8TurnRoleCount() && n < 4; s++){
		if(s == me) continue;
		if(exceptBroke ? rm8TurnStatus(s) == 7 : (rm8TurnStatus(s) >= 3 || rm8TurnNode(s) == rm8TurnNode(me))) continue;	// 0x84: not bankrupt; 0x85: status < 3, another node
		g_chsMap[n] = s; rm8UiSetPicFile(u, n + 1, "prt\\npc\\" + std::string(rm8TurnPic(s)) + "_12.tga"); rm8UiSetVisible(u, n + 1, true); n++;
	}
	for(int k = n; k < 4; k++){ g_chsMap[k] = -1; rm8UiSetVisible(u, k + 1, false); }
	rm8UiShow(u, true);
}

// ---- board picks. [A] the candidates are the ones in the card range (rm8CardsPosInView: the default view on the user, the same for the AI; the camera is locked to it while
// a pick is open, see rm8CardsPicking / rm8_app.cpp). [A] the game picks with the mouse ray (Game_HandleCardClickInGame @0x4a2940, Game_FindBuildingByType
// @0x4a3f30); the port takes the candidate whose screen position is nearest to the tap. Filters as in the game.
struct Cand { int idx; float wp[3]; };
std::vector<Cand> candidates(int kind, int flags, int me)
{
	std::vector<Cand> c;
	if(kind == 1){	// [V] Game_FindClosestRole @0x498170: status < 3 (CanMoveNormally), not invisible, `others` (4) excludes the user
		for(int s = 0; s < rm8TurnRoleCount(); s++){
			if(rm8TurnStatus(s) > 2 || rm8GameInvisible(s) > 0) continue;
			if((flags & 0xf) == 4 && s == me) continue;
			if((flags & 0x20) && rm8CardsHasBodyguard(s)) continue;	// [V] @0x498170: flag 0x20 and vf+0x64
			Cand k; k.idx = s; if(!rm8RolesGetPos(s, k.wp) || !rm8CardsPosInView(k.wp)) continue; k.wp[1] += 18; c.push_back(k);
		}
	}else if(kind == 2){	// [V] TerraMgr_FindClosestRolePosition @0x4f2900: with bit 0 no NPC object and no role (status < 3) on the point
		for(int n = 0, N = rm8GameNodeCount(); n < N; n++){
			if(flags & 1){ bool occ = false; for(int s = 0; s < rm8TurnRoleCount(); s++) occ |= rm8TurnStatus(s) < 3 && rm8TurnNode(s) == n; if(occ) continue; }
			Cand k; k.idx = n; if(!rm8TurnNodePos(n, k.wp) || !rm8CardsPosInView(k.wp)) continue; k.wp[1] += 6; c.push_back(k);
		}
	}else if(kind == 8){	// [V] TerraMgr_FindClosestTerra @0x4f23b0: owner (flags & 0xf): 2 mine, 4 not mine, 8 unowned; level (flags & 0xf00): 0x200 built, 0x400 below 5
		for(int t = 0; t < rm8GameTerraCount(); t++){
			int own = rm8GameTerraOwner(t), lv = rm8GameTerraLevel(t);
			int lo = flags & 0xf, hi = (flags & 0xf00) >> 8;
			if(lo == 2 && own != me) continue;
			if(lo == 4 && own == me) continue;
			if(lo == 8 && own >= 0) continue;
			if(hi == 2 && lv == 0) continue;
			if(hi == 4 && lv > 4) continue;
			Cand k; k.idx = t; if(!rm8GameTerraPos(t, k.wp) || !rm8CardsPosInView(k.wp)) continue; k.wp[1] += 6; c.push_back(k);
		}
	}else if(kind == 0x20){	// [V] NPC_FindNearest @0x49f000: with flags != 0 only NPC ids < 10 (the gods and the board characters) that stand on the board (state 0); the result is the NPC number + 1
		for(int h = 1, N = rm8NpcCount(); h <= N; h++){
			if(!rm8NpcOnMap(h) || (flags != 0 && rm8NpcId(h) >= 10)) continue;
			int n = rm8NpcNode(h);
			Cand k; k.idx = h - 1; if(n < 0 || !rm8TurnNodePos(n, k.wp) || !rm8CardsPosInView(k.wp)) continue; k.wp[1] += 22; c.push_back(k);
		}
	}
	return c;
}

void openPick(int me, int kind, int flags, int cardId)
{
	g_pick.active = true; g_pick.seat = me; g_pick.kind = kind; g_pick.flags = flags; g_pick.cardId = cardId;
	if(getenv("RM8_RANGE_LOG")) rm8Log("cards: pick kind %d seat %d (%s): %d candidates in range", kind, me, rm8TurnHuman(me) ? "human" : "AI", (int)candidates(kind, flags, me).size());
	const char *p = kind == 1 ? "請點選目標角色" : kind == 2 ? "請點選道路上的位置" : kind == 8 ? "請點選土地" : kind == 0x20 ? "請點選神明或人物" : "請點選目標";
	rm8HudOpenModal(me, 0x80, std::string(p) + "（按取消放棄）");	// [A] prompt: the game shows the card as the mouse cursor
	if(Rm8Ui *ok = rm8UiLoad("okcancel", RM8UI_DIALOG)) rm8UiSetVisible(ok, 2, false);	// only the cancel button
}

// [A] test hook RM8_CARD_AUTO=n: the human's selections answer themselves (board picks: candidate n; dice dialog: n or 6;
// card list / player list: the first entry)
void testAnswer(int me, int kind, int a, int v)
{
	int res = 1;
	if(kind == 1 || kind == 2 || kind == 8 || kind == 0x20){
		std::vector<Cand> c = candidates(kind, g_pick.flags, me);
		res = c.empty() ? 0 : c[std::min(v, (int)c.size() - 1)].idx + 1;
		rm8Log("cards: auto pick kind %d -> %d (%d candidates)", kind, res, (int)c.size());
		rm8HudCloseModal(); g_pick.active = false;
	}else if(kind == 0x82){ res = std::max(1, std::min(6, v ? v : 6)); rm8UiShow(rm8UiLoad("dice", RM8UI_DIALOG), false); }
	else if(kind == 0x83){ res = 1; for(int k = 0; k < 8; k++) if(rm8CardAt(a, k)){ res = k + 1; break; } rm8UiShow(rm8UiLoad("rob", RM8UI_DIALOG), false); }
	else { res = g_chsMap[0] + 1; rm8UiShow(rm8UiLoad("chsplayer", RM8UI_DIALOG), false); }
	rm8TurnModalResult(me, res);
}

}	// namespace

bool rm8CardModal(int me, int a, int b)
{
	int kind = b & 0xFFFF;
	switch(kind){
	case 0x82: showDice(me); break;
	case 0x83: showDeckList(me, a); break;
	case 0x84: showChsPlayer(me, a, true); break;
	case 0x85: showChsPlayer(me, a, false); break;
	case 1: case 2: case 8: case 0x20: openPick(me, kind, a, (b >> 16) - 9); break;
	default: return false;
	}
	rm8TurnSetState(me, 7);	// [V] Role_OpenModalUi: state 7 until the dialog answers
	if(const char *e = getenv("RM8_CARD_AUTO")) testAnswer(me, kind, a, atoi(e));
	return true;
}

bool rm8CardsPicking(){ return g_pick.active && rm8TurnState(g_pick.seat) == 7; }

bool rm8CardsOnTap(float x, float y)
{
	if(g_throw.on){ closeThrow(); return true; }	// a click beside the card menu closes it
	if(!rm8CardsPicking()) return false;
	float H = g_cam && g_cam->frameBuffer ? (float)g_cam->frameBuffer->height : 768.0f, best = 70.0f * H / 768.0f;
	int hit = -1;
	for(const Cand &c : candidates(g_pick.kind, g_pick.flags, g_pick.seat)){
		float sx, sy; if(!project(c.wp, sx, sy)) continue;
		float d = hypotf(sx - x, sy - y);
		if(d < best){ best = d; hit = c.idx; }
	}
	if(hit < 0) return true;	// consumed: a tap beside every candidate does nothing
	rm8HudCloseModal();
	g_pick.active = false;
	rm8TurnModalResult(g_pick.seat, hit + 1);	// [V] result = found + 1 (Game_HandleCardClickInGame: +0x228)
	return true;
}

// ---- the AI (Role_StayOrBeforeRoll @0x484610 with a != 0)
// AI_PlanCardActions @0x4f9670 (difficulty > 2 only): (card id, parameter) steps, carried out one per Role_BeforeRollDice by Role_UseCardBeforeRoll @0x4fbab0.
// All blocks of the function are ported, in the original order: the Land-god plans (shape 5: turtle / roadblock / remote dice, each after a seal-up), exchange + buy, bamboo copter + buy,
// stay + swap, seal (35), grab (25, with a god card or a missile of the victim), turn (11) before a roadblock, mine (16) on the prison / hospital node
namespace {
struct Step { int id, param; };
std::map<int, std::vector<Step>> g_plan;
bool hasCard(int me, int id){ return rm8CardFind(me, id) >= 0; }
bool roleVisible(int s){ float p[3]; return rm8RolesGetPos(s, p) && rm8CardsPosInView(p); }
// the checks the plans share: CanMoveNormally (status < 3), not invisible, no bodyguard (vf+0x64), in view
bool targetable(int s){ return rm8TurnStatus(s) < 3 && rm8GameInvisible(s) < 1 && !rm8CardsHasBodyguard(s) && roleVisible(s); }

// [V] CPlayerTag__BuildLocationPath @0x492ff0 (range, mode): breadth-first over the road links from the role's node (index 0). Mode 0 all links; 1 only ahead (the node it walks to, +0x44),
// 2 everything but ahead (applies to the first layer only); with mode != 0 a node with 3+ links is not expanded (also the start)
std::vector<int> locPath(int me, int range, int mode)
{
	std::vector<int> list{rm8TurnNode(me)};
	int to = rm8TurnNodeTo(me);
	size_t start = 0, end = 1;
	for(int step = 0; step < range && step < 32; step++){
		for(size_t i = start; i < end; i++){
			int nb[16], n = rm8TurnLinks(list[i], nb, 16);
			if(mode != 0 && n >= 3) continue;
			for(int k = 0; k < n; k++){
				bool add = i != 0 || (mode == 1 ? nb[k] == to : (mode != 2 || nb[k] != to));
				if(add && std::find(list.begin(), list.begin() + end, nb[k]) == list.begin() + end) list.push_back(nb[k]);
			}
		}
		start = end; end = list.size();
	}
	return list;
}
bool roleOnNode(int n){ for(int s = 0; s < rm8TurnRoleCount(); s++) if(rm8TurnStatus(s) < 3 && rm8TurnNode(s) == n) return true; return false; }

// [V] AI_PlanCardActions @0x4f9670, the first block (shape 5 = possessed by the Land god, no turtle, on a house plot, the street is closed or a seal-up card (5) is held):
// with the road ahead full of other roles' plots of this street: turtle (10) on oneself, a roadblock (12) on the best plot, or a remote dice (9) to the best plot.
// A seal-up (5, 1) is queued first when the street is not closed yet
bool planLandGod(int me, std::vector<Step> &pl)
{
	if(rm8GameShape(me) != 5 || rm8CardsTurtle(me) >= 1) return false;
	int myT = rm8GameNodeTerra(rm8TurnNode(me));
	if(myT < 0 || rm8GameTerraType(myT) != 1) return false;
	int st = rm8GameTerraStreet(myT);
	bool closed = rm8GameStreetClosed(st);
	if(!closed && !hasCard(me, 5)) return false;
	auto push = [&](int id, int param){ if(!closed && pl.empty()) pl.push_back({5, 1}); pl.push_back({id, param}); };
	auto theirs = [&](int t){ return t >= 0 && rm8GameTerraOwner(t) >= 0 && rm8GameTerraOwner(t) != me; };
	if(hasCard(me, 10)){	// turtle: 2+ plots of the street that belong to others (total level > 3) and no NPC on the way
		int cnt = 0, sum = 0; std::vector<int> path = locPath(me, 3, 1);
		for(size_t k = 1; k < path.size(); k++){
			if(rm8NpcAt(path[k])){ cnt = 0; break; }
			int t = rm8GameNodeTerra(path[k]);
			if(!theirs(t)) continue;
			if(rm8GameTerraStreet(t) == st){ cnt++; sum += rm8GameTerraLevel(t); }
			else if(rm8GameTerraLevel(t) != 0){ cnt = 0; break; }
		}
		if(cnt > 1 && sum > 3){ push(10, me + 1); return true; }
	}
	if(hasCard(me, 12)){	// roadblock: the free node ahead (no role on it, no NPC) with the highest level (>= 2) of this street; an existing roadblock ends the search
		unsigned thr = 1; int best = -1; std::vector<int> path = locPath(me, 3, 1);
		for(size_t k = 1; k < path.size(); k++){
			int h = rm8NpcAt(path[k]);
			if(h == 0){
				if(roleOnNode(path[k])) continue;
				int t = rm8GameNodeTerra(path[k]);
				if(theirs(t) && rm8GameTerraStreet(t) == st && thr < (unsigned)rm8GameTerraLevel(t)){ best = path[k]; thr = rm8GameTerraLevel(t); }
			}else if(rm8NpcId(h) == 12) break;
		}
		if(best >= 0){ push(12, best + 1); return true; }
	}
	if(hasCard(me, 9)){	// remote dice: the node k steps ahead (k <= 4) with the highest level (>= 3) of this street
		unsigned thr = 2; int bestK = 0; std::vector<int> path = locPath(me, 4, 1);
		for(size_t k = 1; k < path.size(); k++){
			int h = rm8NpcAt(path[k]);
			if(h == 0){
				int t = rm8GameNodeTerra(path[k]);
				if(theirs(t) && rm8GameTerraStreet(t) == st && thr < (unsigned)rm8GameTerraLevel(t)){ bestK = (int)k; thr = rm8GameTerraLevel(t); }
			}else if(rm8NpcId(h) == 12) break;
		}
		if(bestK > 0){ push(9, bestK); return true; }
	}
	return false;
}

bool planCards(int me)
{
	std::vector<Step> &pl = g_plan[me];
	pl.clear();
	if(rm8CardsDifficulty <= 2) return false;
	int node = rm8TurnNode(me), myT = rm8GameNodeTerra(node);
	long PI = rm8GamePI();
	if(planLandGod(me, pl)) return true;
	// exchange + buy: on an own house plot (level < 3) with 2+ own plots in the street: swap with the best other plot, then buy
	if(hasCard(me, 4) && hasCard(me, 1) && myT >= 0 && rm8GameTerraType(myT) == 1 && rm8GameTerraOwner(myT) == me && rm8GameTerraLevel(myT) < 3 &&
	   rm8GameTerraPrice(myT) < rm8GameCash(me)){
		int own = 0, best = -1, dc = (rm8GameRand() & 1) + 1, st = rm8GameTerraStreet(myT);
		for(int t = 0; t < rm8GameTerraCount(); t++){
			if(rm8GameTerraType(t) != 1 || rm8GameTerraStreet(t) != st) continue;
			if(rm8GameTerraOwner(t) == me) own++;
			else if(rm8GameTerraOwner(t) >= 0 && dc < rm8GameTerraLevel(t)){ dc = rm8GameTerraLevel(t); best = t; }
		}
		if(best >= 0 && own > 1){ pl = {{4, best + 1}, {1, 1}}; return true; }
	}
	// bamboo copter + buy (or the monster): fly to a far rich plot of someone else that no role stands on
	if(hasCard(me, 32) && (hasCard(me, 1) || (hasCard(me, 6) && rm8CardSupported(6)))){
		int best = -1; long thr = ((long)(rm8GameRand() % 6000) + 6000) * PI;
		float mp[3]; rm8TurnNodePos(node, mp);
		for(int n = 0; n < rm8GameNodeCount(); n++){
			float p[3]; if(!rm8TurnNodePos(n, p) || rm8NpcAt(n) != 0) continue;
			float dx = p[0] - mp[0], dy = p[1] - mp[1], dz = p[2] - mp[2];
			if(sqrtf(dx * dx + dy * dy + dz * dz) <= 200.0f) continue;
			int t = rm8GameNodeTerra(n);
			if(t < 0 || rm8GameTerraOwner(t) < 0 || rm8GameTerraOwner(t) == me) continue;
			long rent = rm8GameTerraRent(t, false);
			if(rent <= thr) continue;
			bool occ = false; for(int s = 0; s < rm8TurnRoleCount(); s++) occ |= rm8TurnStatus(s) < 3 && rm8TurnNode(s) == n;
			if(occ) continue;
			best = n; thr = rent;
		}
		if(best >= 0){ pl = {{32, best + 1}, {hasCard(me, 1) ? 1 : 6, 1}}; return true; }
	}
	// stay + swap: on an own plot with a high rent: make a visible role on someone else's ground stay, then swap places with it
	if(hasCard(me, 31) && hasCard(me, 13) && myT >= 0 && rm8GameTerraOwner(myT) == me){
		long rent = rm8GameTerraRent(myT, true);
		bool hotel = rm8GameTerraType(myT) == 2 && rm8GameTerraBuild(myT) == 4;
		long thr = hotel ? ((long)(rm8GameRand() % 6000) + 6000) * PI : ((long)(rm8GameRand() % 8000) + 8000) * PI;
		if(thr <= rent && !rm8GameStreetClosed(rm8GameTerraStreet(myT))){
			for(int s = 0; s < rm8TurnRoleCount(); s++){
				if(s == me || rm8TurnStatus(s) != 0 || !targetable(s)) continue;
				int sh = rm8GameShape(s), t2 = rm8GameNodeTerra(rm8TurnNode(s));
				if(sh != 1 && sh != 5 && (t2 < 0 || rm8GameTerraOwner(t2) != me)){ pl = {{13, s + 1}, {31, s + 1}}; return true; }
			}
		}
	}
	// seal (35) [V] @0x4f9670: possessed by the poor / jinx god (shape 2 / 3): seal a visible good god or board character (NPC id < 10 but not 2 / 3 / 6) and use its card on oneself;
	// otherwise: the first (from index 8 down) visible bad god (2 / 3 / 6) on the board is sealed and its card goes to a visible role that is not possessed by 2 / 3 / 6 (always when
	// that role is possessed by something, with 50 % when not); no such role: the search goes on with the next plan (not with the next NPC)
	if(hasCard(me, 35)){
		int sh = rm8GameShape(me);
		auto npcVisible = [](int h){ int n = rm8NpcNode(h); float p[3]; return n >= 0 && rm8TurnNodePos(n, p) && rm8CardsPosInView(p); };
		if(sh == 2 || sh == 3){
			for(int h = 1, N = rm8NpcCount(); h <= N; h++){
				if(!rm8NpcOnMap(h)) continue;
				int id = rm8NpcId(h);
				if(npcVisible(h) && id < 10 && id != 2 && id != 3 && id != 6){ pl = {{35, h}, {35 + id, me + 1}}; return true; }
			}
		}else{
			for(int h = std::min(9, rm8NpcCount()); h >= 1; h--){
				if(!rm8NpcOnMap(h)) continue;
				int id = rm8NpcId(h);
				if(!npcVisible(h) || (id != 2 && id != 3 && id != 6)) continue;
				for(int s = 0; s < rm8TurnRoleCount(); s++){
					if(s == me || rm8TurnStatus(s) != 0 || !targetable(s)) continue;
					int shp = rm8GameShape(s);
					if(shp == 2 || shp == 3 || shp == 6) continue;
					if(shp != 0 || (rm8GameRand() & 1)){ pl = {{35, h}, {35 + id, s + 1}}; return true; }
				}
				break;
			}
		}
	}
	// grab: take the grab cards of a visible computer role, and a god card or a missile (18) of it
	if(hasCard(me, 25)){
		for(int s = 0; s < rm8TurnRoleCount(); s++){
			if(s == me || rm8TurnStatus(s) >= 3 || rm8TurnHuman(s) || !targetable(s)) continue;	// [V] the +0x1b0 field must be set (a computer)
			int c25 = 0, god = 0, missile = -1;
			for(int k = 0; k < 8; k++){
				int id = rm8CardAt(s, k);
				if(id == 0) continue;
				if(id == 25) c25++;
				else if(id == 18 && missile < 0){	// [V] a missile in its hand: fire it at a node around the victim whose blast area (radius 1) holds neither my node nor my plots
					for(int n1 : neighbourhood(rm8TurnNode(s), 1)){
						int d = 0;
						for(int n2 : neighbourhood(n1, 1)){
							if(n2 == node){ d = 9; break; }
							int t = rm8GameNodeTerra(n2);
							if(t >= 0 && rm8GameTerraOwner(t) == me) d += rm8GameTerraLevel(t);
						}
						if(d == 0){ missile = n1; break; }
					}
				}
				else if(god < 1 && id > 0x23 && id < 0x2d) god = id;
			}
			if(c25 <= 0 && god <= 0 && missile < 0) continue;
			std::vector<Step> p;
			for(int k = 0; k < c25; k++) p.push_back({25, s + 1});
			if(missile < 0){ if(god > 0){ p.push_back({25, s + 1}); p.push_back({god, (god == 0x25 || god == 0x26 || god == 0x29) ? s + 1 : me + 1}); } }
			else{ p.push_back({25, s + 1}); p.push_back({18, missile + 1}); }
			for(const Step &st : p) if(supported(st.id)) pl.push_back(st);	// [A] steps whose card is not ported are dropped
			if(!pl.empty()) return true;
		}
	}
	// turn (11): not on the way as the road bully / possessed by the Land god, no turtle; a roadblock (NPC 12) ahead on another role's plot whose rent beats 3000..5999 x price index
	if(hasCard(me, 11) && rm8CardsTurtle(me) < 1 && rm8GameShape(me) != 1 && rm8GameShape(me) != 5){
		long thr = ((long)(rm8GameRand() % 3000) + 3000) * PI;
		std::vector<int> path = locPath(me, 3, 1);
		for(size_t k = 1; k < path.size(); k++){
			int h = rm8NpcAt(path[k]);
			if(h == 0 || rm8NpcId(h) != 12) continue;
			int t = rm8GameNodeTerra(path[k]);
			if(t >= 0 && rm8GameTerraOwner(t) >= 0 && rm8GameTerraOwner(t) != me && thr < rm8GameTerraRent(t, true)){ pl = {{11, me + 1}}; return true; }
			break;
		}
	}
	// mine (16): a human sits in the prison (else the hospital) node, which is in view, and no role of the game stands there: lay the mine on it
	if(hasCard(me, 16)){
		int prison = -1, hosp = -1; float p[3];
		for(int n = 0, N = rm8GameNodeCount(); n < N; n++){ int ev = rm8GameNodeEvent(n); if(ev == 7 && prison < 0) prison = n; else if(ev == 8 && hosp < 0) hosp = n; }
		int target = prison >= 0 && rm8TurnNodePos(prison, p) && rm8CardsPosInView(p) ? prison : hosp >= 0 && rm8TurnNodePos(hosp, p) && rm8CardsPosInView(p) ? hosp : -1;
		if(target >= 0){
			bool human = false, active = false;
			for(int s = 0; s < rm8TurnRoleCount(); s++){
				if(rm8TurnStatus(s) == 7 || rm8TurnNode(s) != target) continue;
				if(rm8TurnStatus(s) < 3){ active = true; break; }
				if(rm8TurnHuman(s)) human = true;
			}
			if(human && !active){ pl = {{16, target + 1}}; return true; }
		}
	}
	return false;
}
}	// namespace

bool rm8CardsAiUsePlanned(int me)	// [V] Role_UseCardBeforeRoll @0x4fbab0: the next planned card that the role holds
{
	std::vector<Step> &pl = g_plan[me];
	while(!pl.empty()){
		Step st = pl.front(); pl.erase(pl.begin());
		int slot = rm8CardFind(me, st.id);
		const Def *d = slot >= 0 ? def(st.id) : nullptr;
		if(!d) continue;
		static const bool dbg = getenv("RM8_DEBUG_CARDS") != nullptr;
		if(dbg) rm8Log("cards: AI seat %d (%s) plans card %d (%s), parameter %d", me, rm8TurnName(me), d->id, d->name.c_str(), st.param);
		info(me, rm8StrFmt(261, rm8TurnName(me), d->name.c_str()));
		rm8TurnEnqueue(me, 99, slot, 0);
		if(d->speak[0] >= 0) rm8TurnEnqueue(me, 0x5A, d->speak[0], 0);
		rm8TurnEnqueue(me, 3, slot, st.param);
		return true;
	}
	return false;
}

bool rm8CardsAiStart(int me)
{
	// [V] Role_StayOrBeforeRoll @0x484610: AI_PlanCardActions first; a plan is carried out by Role_BeforeRollDice (Role_UseCardBeforeRoll)
	if(planCards(me)) return false;
	// [V] Role_UseSpecialCard @0x4fbd10 (AI_PlanCardActions returned 0): only after day 2; difficulty < 2 uses a card half of the time
	if(rm8GameDay() <= 2) return false;
	if(rm8CardsDifficulty < 2 && !(rm8GameRand() & 1)) return false;
	int slots[8], comp[8], n = 0, best = -1;
	for(int k = 0; k < 8; k++){
		int id = rm8CardAt(me, k); const Def *d = id ? def(id) : nullptr;
		if(!d || !supported(id) || d->py.empty()) continue;
		long res = -1;
		if(!rm8PyCardCall(d->py.c_str(), false, 0, 0, 0, res)) res = -1;	// Card_GetCustomValue @0x4ad410 = Competence()
		slots[n] = k; comp[n] = (int)res + 1;
		static const bool dbgc = getenv("RM8_DEBUG_COMP") != nullptr;	// [A] test log: the Competence() of every card the AI looks at
		if(dbgc) rm8Log("cards: competence seat %d card %d = %ld", me, id, res);
		if(comp[n] > 0){
			if(id == 21){ best = n; n++; break; }	// the rich-for-all card is always taken
			if(id == 11) best = n;
			else if(id == 26 && best < 0) best = n;
			n++;
		}
	}
	if(n <= 0) return false;
	if(best < 0) best = rm8GameRand() % n;
	const Def *d = def(rm8CardAt(me, slots[best]));
	if(!d) return false;
	static const bool dbg = getenv("RM8_DEBUG_CARDS") != nullptr;
	if(dbg) rm8Log("cards: AI seat %d (%s) picks card %d (%s), Competence %d", me, rm8TurnName(me), d->id, d->name.c_str(), comp[best] - 1);
	info(me, rm8StrFmt(261, rm8TurnName(me), d->name.c_str()));	// "%s使用%s"
	rm8TurnEnqueue(me, 99, slots[best], 0);
	if(d->speak[0] >= 0) rm8TurnEnqueue(me, 0x5A, d->speak[0], 0);
	rm8TurnEnqueue(me, 3, slots[best], comp[best]);
	return true;
}

// ---- the card bar (gmaindlg widgets 11..18 pictures, 19..26 names, 31..38 buttons)
namespace {
float g_base[8][4];
int g_hover = -1;

void restoreSlot(int k)	// [V] Card_SelectDisplay_91f0 @0x4e91f0
{
	rm8UiSetRect(g_main, 11 + k, g_base[k][0], g_base[k][1], g_base[k][2], g_base[k][3]);
	rm8UiSetText(g_main, 19 + k, "");
}
void enlargeSlot(int k, const Def &d)	// [V] Card_ShowInfo_8f40 @0x4e8f40: x - w/8, y - (h/4 + 20), size * 1.25, name label, tooltip "%s  %s  %d"
{
	float x = g_base[k][0], y = g_base[k][1], w = g_base[k][2], h = g_base[k][3];
	rm8UiSetRect(g_main, 11 + k, x - w / 8, y - (h / 4 + 20), w + w / 4, h + h / 4);
	rm8UiSetText(g_main, 19 + k, d.name);
	rm8UiSetTooltip(d.name + "  " + d.desc + "  " + std::to_string(d.price));
}
}	// namespace

// ---- the hand-full swap (Cls_5e24f8, getcard.ui) and the card menu (Cls_5e3ca8, throwcard.ui)
namespace {
Rm8Ui *getcardUi(){ return rm8UiLoad("getcard", RM8UI_DIALOG); }
// [V] Card_HandleSwap @0x4ae8a0: sample 0x7c, the clicked card goes (Role_RemoveCardFromDeck), the new one comes (Role_AddCardToDeck), emote class 2 + rand % 2 when the
// message's flag was set, Card_HideCardUI @0x4ae9a0; then the task ends (Game_EndTurn: state 9 -> 8). take = false: [A] the new card is given up (click on the card window)
void swapFinish(bool take, int slot)
{
	if(!g_swap.on) return;
	int seat = g_swap.seat;
	if(Rm8Ui *u = getcardUi()){ rm8UiShow(u, false); rm8UiOnClick(u, nullptr); }
	g_swap.on = false;
	if(getenv("RM8_DEBUG_CARDS")) rm8Log("cards: seat %d %s (new card %d, slot %d held card %d)", seat, take ? "swaps" : "gives the new card up", g_swap.card, slot, take ? rm8CardAt(seat, slot) : -1);
	if(take){
		rm8SndPlay(0x7c);
		rm8CardRemoveAt(seat, slot); rm8CardAdd(seat, g_swap.card);
		if(g_swap.emote) rm8RoleSayAt(seat, 0, 2 + (rm8GameRand() & 1));
	}
	rm8TurnSetState(seat, 8);
}

Rm8Ui *throwUi(){ return rm8UiLoad("throwcard", RM8UI_DIALOG); }
// [V] Card_ThrowClear @0x4aebd0: the menu is hidden, the task ends (state 9 -> 8; the queued 100 then asks for the dice again)
void closeThrow()
{
	if(!g_throw.on) return;
	if(Rm8Ui *u = throwUi()) rm8UiShow(u, false);
	g_throw.on = false;
	rm8TurnSetState(g_throw.seat, 8);
}
// [V] Cls_5e3ca8__Event_OnThrowCardSelection @0x4aeb20: button 1 = Role_RemoveCardFromDeck(slot), button 2 = Role_StoreCardToBox(slot) @0x488de0 (the card goes into the
// story storage box; a full box (8 cards): str 112 at the queue front and the card stays)
void throwChoice(int id)
{
	if(!g_throw.on) return;
	int seat = g_throw.seat, slot = g_throw.slot, card = rm8CardAt(seat, slot);
	if(getenv("RM8_DEBUG_CARDS")) rm8Log("cards: seat %d throw menu: %s card %d from slot %d", seat, id == 1 ? "discard" : "store", card, slot);
	if(card){
		if(id == 1) rm8CardRemoveAt(seat, slot);
		else if(id == 2){
			if(rm8BoxStoreCard(card)) rm8CardRemoveAt(seat, slot);
			else rm8TurnEnqueueAt(seat, 0, 0x1B, rm8GamePushText(rm8Str(112)), 0);
		}
	}
	closeThrow();
}
// [V] Game_DispatchCardAction(0x6f) @0x4a4420 + Game_TriggerWonderEvent @0x4a43e0 (state 6 only): state 9, the queues are cleared and 100 queued (the dice question again after the
// menu); Card_ThrowPrepare @0x4aea80: button 2 (store) only in the story mode, the menu sits above the card ([A] clamped to the screen)
bool openThrow(int slot)
{
	int cur = rm8TurnCurrent();
	if(g_throw.on || g_swap.on || !rm8TurnHuman(cur) || rm8TurnState(cur) != 6 || !rm8CardAt(cur, slot) || !g_main) return false;
	Rm8Ui *u = throwUi();
	if(!u) return false;
	static bool wired = false;
	if(!wired){ wired = true; rm8UiOnClick(u, [](int id){ if(id == 1 || id == 2) throwChoice(id); }); }
	g_throw.on = true; g_throw.seat = cur; g_throw.slot = slot;
	rm8TurnClearQueue(cur); rm8TurnEnqueue(cur, 100, 0, 0); rm8TurnSetState(cur, 9);
	rm8UiSetVisible(u, 2, rm8StoryActive());
	float x = 0, y = 0, w = 0, h = 0;
	if(rm8UiGetScreenRect(g_main, 31 + slot, x, y, w, h)){
		float W = g_cam && g_cam->frameBuffer ? (float)g_cam->frameBuffer->width : 1024.0f, H = g_cam && g_cam->frameBuffer ? (float)g_cam->frameBuffer->height : 768.0f, s = H / 768.0f;
		rm8UiSetRootPos(u, std::max(0.0f, std::min(x, W - 133.0f * s)), std::max(0.0f, y - 80.0f * s));
	}
	rm8UiSetTooltip("");
	rm8UiShow(u, true);
	return true;
}
}	// namespace

void rm8CardsHudInit(Rm8Ui *main)
{
	g_main = main; g_hover = -1;
	if(!main) return;
	for(int k = 0; k < 8; k++){
		rm8UiGetRect(main, 11 + k, g_base[k][0], g_base[k][1], g_base[k][2], g_base[k][3]);
		rm8UiSetPicFile(main, 11 + k, ""); rm8UiSetText(main, 19 + k, ""); rm8UiSetEnabled(main, 31 + k, false);
	}
}

void rm8CardsHudUpdate(Rm8Ui *main, Camera *cam)
{
	g_cam = cam;
	if(!main || main != g_main) return;
	int cur = rm8TurnCurrent();
	if(cur < 0) return;
	// [V] Role_RefreshDeckUi @0x4812c0: the deck of the role whose turn it is; a card with the ready flag is not shown
	bool enabled = (rm8TurnHuman(cur) && rm8TurnState(cur) == 6) || g_sell || g_swap.on;	// [V] Game_ToggleCharacterVisibility @0x4a6f90: state 6, or the card shop's sell mode / a pending swap (DAT_00640958, Cls_5e24f8)
	int hover = -1;
	for(int k = 0; k < 8; k++){
		int id = rm8CardAt(cur, k); const Def *d = id && !rm8CardReady(cur, k) ? def(id) : nullptr;
		rm8UiSetPicFile(main, 11 + k, d ? d->tex : "");
		rm8UiSetEnabled(main, 31 + k, d && enabled);
		if(d && enabled && rm8UiHot(main, 31 + k)) hover = k;
	}
	// [A] test hook RM8_CARD_HOVER=k: slot k looks hovered (the mouse tooltip also needs RM8_TIP_DELAY=0 in the one-shot render)
	if(const char *th = getenv("RM8_CARD_HOVER")){
		int k = atoi(th);
		if(k >= 0 && k < 8 && rm8CardAt(cur, k) && !rm8CardReady(cur, k)){
			hover = k;
			float sx, sy; rm8UiHudScale(sx, sy);
			rm8UiPointerMove((g_base[k][0] + g_base[k][2] * 0.5f) * sx, (g_base[k][1] + g_base[k][3] * 0.5f) * sy);
		}
	}
	if(hover != g_hover){
		if(g_hover >= 0) restoreSlot(g_hover);
		g_hover = hover;
		if(hover >= 0){ const Def *d = def(rm8CardAt(cur, hover)); if(d) enlargeSlot(hover, *d); else g_hover = -1; }
		if(g_hover < 0) rm8UiSetTooltip("");
	}
}

void rm8CardsHudRender(Camera *cam)
{
	g_cam = cam;
	if(!rm8CardsPicking()) return;
	uint32 zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE), atf = GetRenderState(ALPHATESTFUNC), cull = GetRenderState(CULLMODE);
	SetRenderState(ZTESTENABLE, 0); SetRenderState(ZWRITEENABLE, 0); SetRenderState(ALPHATESTFUNC, ALPHAALWAYS); SetRenderState(CULLMODE, CULLNONE);
	SetRenderState(VERTEXALPHA, 1); SetRenderState(SRCBLEND, BLENDSRCALPHA); SetRenderState(DESTBLEND, BLENDINVSRCALPHA);
	SetRenderStatePtr(TEXTURERASTER, nullptr);
	float s = (float)cam->frameBuffer->height / 768.0f, r = 9.0f * s;
	for(const Cand &c : candidates(g_pick.kind, g_pick.flags, g_pick.seat)){	// the valid targets: yellow diamonds
		float sx, sy; if(!project(c.wp, sx, sy)) continue;
		fillQuad(cam, sx - r - 2, sy - r - 2, sx + r + 2, sy + r + 2, 0xff000000);
		fillQuad(cam, sx - r, sy - r, sx + r, sy + r, 0xffffe030);
	}
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(ALPHATESTFUNC, atf); SetRenderState(CULLMODE, cull);
}

void rm8CardsSetSellMode(bool on){ g_sell = on; }

bool rm8CardsOnSlotClick(int k)	// [V] Card_OnSelected @0x4e9340 (param 1)
{
	int cur = rm8TurnCurrent();
	const Def *d = def(rm8CardAt(cur, k));
	if(!d) return false;
	if(g_swap.on && cur == g_swap.seat){ swapFinish(true, k); return true; }	// [V] Card_OnSelected @0x4e9340: not state 6, no shop: Card_HandleSwap(slot)
	if(g_sell){	// [V] Role_SellCardForTickets @0x488cf0: the card goes, Role_AddTicket(price / 2) (at most 9999 points: Role_AddTicket @0x489ed0)
		if(getenv("RM8_DEBUG_CARDS")) rm8Log("cards: seat %d sells card %d (%s) for %d points (had %ld)", cur, d->id, d->name.c_str(), d->price / 2, rm8GamePoints(cur));
		rm8CardRemoveAt(cur, k); rm8GameAddPoints(cur, std::min<long>(d->price / 2, 9999 - rm8GamePoints(cur))); return true;
	}
	if(rm8TurnHuman(cur) && rm8TurnState(cur) == 6){
		if(d->type == 2) return false;			// [V] Role_TryUseCardInSlot @0x488c80: passive cards cannot be used
		rm8TurnEnqueue(cur, 2, k, 0); rm8TurnSetState(cur, 8);
		return true;
	}
	return false;
}

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_cards.inc"

// ---- hand-full swap, right click, touch hold, camera, test driver
bool rm8CardsBeginSwap(int seat, int cardId, int emote)
{
	if(g_swap.on || g_throw.on || !rm8TurnHuman(seat) || rm8CardCount(seat) < rm8CardMax() || !def(cardId)) return false;
	Rm8Ui *u = getcardUi();
	if(!u) return false;
	// [V] Card_LoadAndShowCard @0x4ae790: the card's name (label 1) and picture (card\<texName>, label 2), the strip (widget 3) shown; the role waits in state 9 (CPlayerTag_SetStatusCode9)
	rm8UiSetText(u, 1, def(cardId)->name); rm8UiSetPicFile(u, 2, def(cardId)->tex); rm8UiSetVisible(u, 3, true);
	rm8UiOnClick(u, [](int){ swapFinish(false, -1); });	// [A] a click on the card window gives the new card up
	rm8UiShow(u, true);
	g_swap.on = true; g_swap.seat = seat; g_swap.card = cardId; g_swap.emote = emote;
	rm8TurnSetState(seat, 9);
	float p[3]; if(rm8RolesGetPos(seat, p)) rm8TurnFocus(p);
	if(const char *e = getenv("RM8_CARD_AUTO")){ int v = atoi(e); rm8Log("cards: auto swap of slot %d for card %d", v, cardId); swapFinish(true, v); }	// test hook
	return true;
}

void rm8CardsSetCamera(Camera *cam){ g_cam = cam; }

// the cancel button of the prompt: the card flow queues no 0x1C behind the selection, so the prompt is closed here (it stayed on screen)
bool rm8CardsCancelPick(){ if(!rm8CardsPicking()) return false; int s = g_pick.seat; rm8HudCloseModal(); g_pick.active = false; rm8TurnModalResult(s, 0); return true; }

bool rm8CardsOnRightClick(float, float)
{
	if(g_throw.on){ closeThrow(); return true; }	// [V] the dialog's MoveOut: leaving it closes it
	if(g_hover >= 0 && openThrow(g_hover)) return true;	// [V] Card_OnSelected event 3 (right button on the card)
	// [V] CGamePlayScene vf11 @0x4a2cf0: the first role in state 7: +0x228 = 0, +0x224 = 1 = the dialog's result 0
	if(rm8CardsPicking()){ int s = g_pick.seat; rm8HudCloseModal(); g_pick.active = false; rm8TurnModalResult(s, 0); return true; }
	for(const char *n : {"dice", "rob", "chsplayer"}){
		Rm8Ui *u = rm8UiLoad(n, RM8UI_DIALOG);
		if(u && rm8UiVisible(u) && g_dlgSeat >= 0 && rm8TurnState(g_dlgSeat) == 7){ rm8UiShow(u, false); rm8TurnModalResult(g_dlgSeat, 0); return true; }
	}
	return false;
}

namespace {
bool slotRect(int k, float &cx, float &cy)
{
	float x, y, w, h;
	if(!g_main || !rm8UiGetScreenRect(g_main, 31 + k, x, y, w, h)) return false;
	cx = x + w * 0.5f; cy = y + h * 0.5f;
	return true;
}
// [A] touch: a finger held on a card for 1.4 s (the inspection starts at 0.5 s) opens the card menu - the right button of the mouse has no equivalent on a phone
void touchHold()
{
	static int slot = -1; static std::chrono::steady_clock::time_point t0;
	int h = -1;
	if(g_main && rm8UiPressIsTouch()) for(int k = 0; k < 8; k++) if(rm8UiHot(g_main, 31 + k)){ h = k; break; }
	if(h < 0){ slot = -1; return; }
	if(h != slot){ slot = h; t0 = std::chrono::steady_clock::now(); return; }
	if(std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count() >= 1.4f){ slot = -1; g_hover = h; openThrow(h); }
}

// [A] test driver RM8_CARD_PTR="step;step;...": the steps go through the real pointer functions (rm8UiPointerMove / Down / Up, then the board tap of rm8SkipTap =
// rm8CardsOnTap, rm8CardsOnRightClick), one per 0.4 s of game time, each when its precondition holds:
//   slot:N  tap card slot N while the human waits (state 6)        cand:N  tap the Nth candidate of a board selection (projected position)
//   dlg:N   tap widget id N of the visible card dialog             swap:N  tap card slot N during the hand-full swap
//   rclick:N right-click slot N (the card menu)                    cancel  right-click during a selection / dialog      hold:N  finger held on slot N for 1.7 s (wall clock)
//   menu:N  tap button N of the card menu
struct PtrStep { std::string kind; int n; };
void tapAt(float x, float y, bool touch)
{
	rm8UiPointerMove(x, y);
	bool onUi = rm8UiPointerDown(x, y, touch);
	if(!onUi) rm8CardsOnTap(x, y);
	rm8UiPointerUp(x, y);
}
void driver(float dt)
{
	static const char *e = getenv("RM8_CARD_PTR");
	static std::vector<PtrStep> steps; static size_t idx = 0; static float cool = 0; static bool parsed = false;
	if(!e) return;
	if(!parsed){
		parsed = true;
		std::stringstream ss(e); std::string part;
		while(std::getline(ss, part, ';')){ size_t c = part.find(':'); steps.push_back({part.substr(0, c), c == std::string::npos ? 0 : atoi(part.c_str() + c + 1)}); }
	}
	if(idx >= steps.size()) return;
	cool -= dt;
	if(cool > 0) return;
	int cur = rm8TurnCurrent();
	const PtrStep &st = steps[idx];
	float x = 0, y = 0; bool done = false;
	auto visibleDlg = [&](const char *name, int id){ Rm8Ui *u = rm8UiLoad(name, RM8UI_DIALOG); float w, h, rx, ry; if(u && rm8UiVisible(u) && rm8UiGetScreenRect(u, id, rx, ry, w, h)){ x = rx + w / 2; y = ry + h / 2; return true; } return false; };
	bool waiting = rm8TurnHuman(cur) && rm8TurnState(cur) == 6 && !g_throw.on;
	if(st.kind == "ownall"){	// test: every plot belongs to seat N % 10 with level 2 (N / 10 when N >= 10: ownall:51 = seat 1, level 5)
		for(int t = 0; t < rm8GameTerraCount(); t++) if(rm8GameTerraType(t) != 0) rm8GameSetTerra(t, st.n % 10, st.n >= 10 ? st.n / 10 : 2, -1);
		done = true;
	}else if(st.kind == "report"){	// test: how many plots still have an owner / a building
		int own = 0, lv = 0; for(int t = 0; t < rm8GameTerraCount(); t++){ if(rm8GameTerraOwner(t) >= 0) own++; if(rm8GameTerraLevel(t) > 0) lv++; }
		rm8Log("cards: ptr report: %d plots owned, %d with a building", own, lv);
		done = true;
	}else if(st.kind == "goto" && waiting){	// test: put the human on road node N
		{ int nb[8], nn = rm8TurnLinks(st.n, nb, 8); rm8TurnPlaceAt(cur, st.n, nn > 0 ? nb[0] : st.n); } done = true;
		{ int t = rm8GameNodeTerra(st.n); rm8Log("cards: ptr goto node %d: plot %d type %d owner %d level %d", st.n, t, t >= 0 ? rm8GameTerraType(t) : -1, t >= 0 ? rm8GameTerraOwner(t) : -1, t >= 0 ? rm8GameTerraLevel(t) : -1); }
	}else if(st.kind == "near" && waiting){	// test: put the human on a road node next to the node of seat N (so that the role is inside the card range)
		int nb[8], nn = rm8TurnLinks(rm8TurnNode(st.n), nb, 8), to = nn > 0 ? nb[0] : rm8TurnNode(st.n), nb2[8], n2 = rm8TurnLinks(to, nb2, 8);
		rm8TurnPlaceAt(cur, to, n2 > 0 ? nb2[0] : to); done = true;
		rm8Log("cards: ptr near seat %d: the human stands on node %d", st.n, to);
	}else if(st.kind == "recv" && waiting){	// test: the human is given card N through message 4 (events.cpp receiveCard; a full hand opens the swap dialog)
		rm8TurnEnqueue(cur, 4, st.n, 1); rm8TurnEnqueue(cur, 100, 0, 0); rm8TurnSetState(cur, 8); done = true;
	}else if(st.kind == "sell" && g_sell && rm8CardAt(cur, st.n) && slotRect(st.n, x, y)){	// test: tap card slot N while the card shop sells
		tapAt(x, y, false); done = true;
	}else if(st.kind == "wait"){ cool = (float)st.n; done = true; idx++; return; }
	else if(st.kind == "slot" && waiting && rm8CardAt(cur, st.n) && slotRect(st.n, x, y)){ tapAt(x, y, false); done = true; }
	else if(st.kind == "swap" && g_swap.on && slotRect(st.n, x, y)){ tapAt(x, y, false); done = true; }
	else if(st.kind == "rclick" && waiting && rm8CardAt(cur, st.n) && slotRect(st.n, x, y)){ rm8UiPointerMove(x, y); g_hover = st.n; rm8CardsOnRightClick(x, y); done = true; }
	else if(st.kind == "cancel" && (rm8CardsPicking() || g_throw.on || visibleDlg("dice", 1) || visibleDlg("rob", 1) || visibleDlg("chsplayer", 1))){ rm8CardsOnRightClick(0, 0); done = true; }
	else if(st.kind == "hold" && waiting && rm8CardAt(cur, st.n) && slotRect(st.n, x, y)){
		rm8UiPointerMove(x, y); rm8UiPointerDown(x, y, true);
		touchHold();	// first call records the slot, the second (after the wall time) opens the menu
		std::this_thread::sleep_for(std::chrono::milliseconds(1700));
		touchHold();
		rm8UiPointerUp(x, y); done = true;
	}
	else if(st.kind == "menu" && g_throw.on && visibleDlg("throwcard", st.n)){ tapAt(x, y, false); done = true; }
	else if(st.kind == "dlg" && (visibleDlg("dice", st.n) || visibleDlg("rob", st.n) || visibleDlg("chsplayer", st.n) || visibleDlg("getcard", st.n) || visibleDlg("okcancel", st.n))){ tapAt(x, y, false); done = true; }
	else if((st.kind == "candrole" || st.kind == "candnear") && rm8CardsPicking()){	// candrole: the board point (kind 2) where seat N stands, or the role itself (kind 1); candnear: a point next to seat N
		float W = g_cam && g_cam->frameBuffer ? (float)g_cam->frameBuffer->width : 1024.0f, H = g_cam && g_cam->frameBuffer ? (float)g_cam->frameBuffer->height : 768.0f;
		for(const Cand &k : candidates(g_pick.kind, g_pick.flags, g_pick.seat)){
			std::vector<int> nb1 = neighbourhood(k.idx, 1);
			bool match = g_pick.kind == 1 ? k.idx == st.n : st.kind == "candrole" ? k.idx == rm8TurnNode(st.n) : k.idx != rm8TurnNode(st.n) && std::find(nb1.begin(), nb1.end(), rm8TurnNode(st.n)) != nb1.end();
			if(match && project(k.wp, x, y) && x > 20 && x < W - 20 && y > 120 && y < H - 160 && rm8UiHitName(x, y).empty()){
				rm8Log("cards: ptr tap on candidate %d (%s seat %d) at %.0f,%.0f", k.idx, st.kind.c_str(), st.n, x, y); tapAt(x, y, false); done = true; break;
			}
		}
	}
	else if(st.kind == "cand" && rm8CardsPicking()){
		std::vector<Cand> c = candidates(g_pick.kind, g_pick.flags, g_pick.seat), vis;	// the visible ones (a finger can only tap what is on the screen)
		float W = g_cam && g_cam->frameBuffer ? (float)g_cam->frameBuffer->width : 1024.0f, H = g_cam && g_cam->frameBuffer ? (float)g_cam->frameBuffer->height : 768.0f;
		for(const Cand &k : c){ float px, py; if(project(k.wp, px, py) && px > 20 && px < W - 20 && py > 120 && py < H - 160) vis.push_back(k); }
		if(!vis.empty()){
			const Cand &k = vis[std::min<size_t>(st.n, vis.size() - 1)];
			project(k.wp, x, y);
			rm8Log("cards: ptr tap on candidate %d (%d of %d visible, %d in all) at %.0f,%.0f", k.idx, st.n, (int)vis.size(), (int)c.size(), x, y); tapAt(x, y, false); done = true;
		}
	}
	if(done){ rm8Log("cards: ptr step %s:%d done", st.kind.c_str(), st.n); idx++; cool = 0.4f; }
	else{ static float stall = 0; stall += dt; if(stall > 3.0f){ stall = 0; rm8Log("cards: ptr step %s:%d waits (cur %d human %d state %d, picking %d, throw %d, swap %d)", st.kind.c_str(), st.n, cur, (int)rm8TurnHuman(cur), rm8TurnState(cur), (int)rm8CardsPicking(), (int)g_throw.on, (int)g_swap.on); } }
}
}	// namespace

void rm8CardsUpdate(float dt)
{
	if(g_snapSeat >= 0 && g_snapT >= 0){	// [A] RM8_DEBUG_CARDS: the state after a card was used (a human back at the dice, or 8 s later)
		g_snapT += dt;
		if((rm8TurnHuman(g_snapSeat) && rm8TurnState(g_snapSeat) == 6 && g_snapT > 1.0f) || g_snapT > 8.0f){ snapshot("after"); g_snapSeat = -1; g_snapT = -1; }
	}
	touchHold();
	if(getenv("RM8_CARD_PTR")){ if(g_main && g_cam) rm8CardsHudUpdate(g_main, g_cam); driver(dt); }	// the bar's pictures / enabled flags normally update with the render
}
