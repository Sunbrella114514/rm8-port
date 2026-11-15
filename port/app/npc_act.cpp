// NPC interactions: what happens when a role stops on (or passes) an NPC, the obstacles, the time bomb, the robot doll, seals, the
// possessed police / thief / dog, the gods' possession sequence. [V] = read from the decompile (docs/npc_port.md).
//
// [V] A role that stops on a node queues 0x23 (NPC_HandleInteract @0x49dfb0) then 0x1D (CPlayerTag__OnStopAtTerra @0x485c00). The NPC
// classes answer in vf+0x74 (OnRoleClick): 1 = the role waits in state 10 until the NPC finishes (Role_LeaveState10 @0x486110), 2 / 3 =
// handled, the messages the NPC queued run. A walking role passes NpcMgr_CheckPassNode @0x49e090 on every node (vf+0x7C): only the
// roadblock answers (Unk_ProxyVirtualCall_vf31 @0x49cec0 -> OnRoleClick of the walking role).
#include "npc_int.h"
#include "news.h"
#include "story.h"
#include "collect.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

using namespace rw;

namespace npc {

namespace {

// ---- text helpers: the game's strings carry "text@sound" (sample id of Sound/snd0000.dat)
std::string plain(const std::string &s, int *snd)
{
	size_t at = s.rfind('@');
	if(at != std::string::npos && at + 1 < s.size() && isdigit((unsigned char)s[at + 1])){ if(snd) *snd = atoi(s.c_str() + at + 1); return s.substr(0, at); }
	if(snd) *snd = 0;
	return s;
}
// [V] Role_QueueTextMsg @0x484b70 (0x1B info box); the sound after '@' is played with the box (Role_ShowInfoDialog @0x488ae0 -> Anim_PlaySound)
void queueText(int seat, const std::string &raw)
{
	int snd; std::string t = plain(raw, &snd);
	if(snd > 0) rm8TurnEnqueue(seat, 0x53, snd, -1);
	rm8TurnEnqueue(seat, 0x1B, rm8GamePushText(t), 0);
}

// [V] Role_EnqueueMsgPairWithCurrent @0x4b8a00: the subject gets 0x2E(current + 1, 1) at the front, the current role 0x2E(subject + 1, 0) at `pos`
void pairWithCurrent(int subject, int pos)
{
	int cur = rm8TurnCurrent();
	if(subject < 0 || cur < 0 || cur == subject) return;
	rm8TurnEnqueueAt(subject, 0, 0x2e, cur + 1, 1);
	rm8TurnEnqueueAt(cur, pos, 0x2e, subject + 1, 0);
}

bool validSeat(int s){ return s >= 0 && s < rm8TurnRoleCount(); }

// ---- timed tasks: a role in state 9 (an event task runs: effect, news) until it is over
struct Task { int seat; int fx; float t; bool armed; std::vector<int> terras; int type; };
std::vector<Task> g_tasks;
void startTask(int seat, int fx, float secs, const std::vector<int> &terras = std::vector<int>(), int type = -1)
{
	if(!validSeat(seat)) return;
	g_tasks.push_back({seat, fx, secs, false, terras, type});	// armed at the end of the frame (the walk code sets state 8 after a pass hook)
}
void updateTasks(float dt)
{
	for(size_t i = 0; i < g_tasks.size();){
		Task &k = g_tasks[i];
		if(!k.armed){ k.armed = true; rm8TurnSetState(k.seat, 9); i++; continue; }
		k.t -= dt;
		bool fxDone = k.fx == 0 || rm8FxFinished(k.fx);
		if(k.t <= 0 && fxDone){
			if(k.fx) rm8FxStop(k.fx);
			// [V] Cls_5e6a78__vf4 @0x4f47d0 (the missile / nuke task, 2.0 s): Building_SuccessBuild @0x4f4800 (type 0) = BuildObj_ResetAll(1, 1) on every plot of the
			// blast area (the buildings go, the owner stays), Building_FinishWithoutSuccess @0x4f4890 (type 1) = Land_ResetBuildAndOwner (the owner goes too)
			for(int t : k.terras) rm8GameSetTerra(t, k.type == 1 ? -1 : -2, 0, 0);
			if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: blast task ends, %d plots reset (type %d)", g_clock, (int)k.terras.size(), k.type);
			if(rm8TurnState(k.seat) == 9) rm8TurnSetState(k.seat, 8);	// [V] Role_UpdateStatusTransition @0x4860d0
			g_tasks.erase(g_tasks.begin() + i);
		}else i++;
	}
}

// ---- the news task for the board characters (News_OnUpdate @0x4b4da0 -> News_OnSpecialEventUpdate @0x4b4e40, mode 3)
// a: 0..8 police (News_EventMinigame @0x4b5ef0, strings 418..426, effect arts 6201..6209), 9 thief (News_EventThief @0x4b7bc0, str 428, art 6210),
// 10 lucky find (News_EventLuckyFind @0x4b7d10, str 431, art 6211), 11 chest (News_EventTreasureChest @0x4b7e50, str 432, art 6212)
bool newsEvent(int seat, int a, int subject)
{
	if(!validSeat(subject) || !validSeat(seat)) return false;
	if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: news %d for seat %d (queue owner %d)", g_clock, a, subject, seat);
	// the news module (news.cpp, docs/news.md): the TV scene with the anchor and the 3D picture (art 6201 + a), the caption + voice, the effect on the
	// subject (prison 3 days / half the points / two cards / 250 points), the hand-over pair and the 3.8 s task (state 9)
	return rm8NewsStart(seat, a, subject);
}

// ---- blast: [V] CPlayerTag__OnHolidayCardUse @0x48de10 (mine, time bomb, missiles share it)
std::vector<int> neighbourhood(int node, int radius)	// [V] Terra_BuildNeighborList @0x49fd70
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
// [V] Card__FilterSpecialCards @0x48dbe0: the victim's cards 16..19 (mine, time bomb, missile, nuke) are lost; each adds a hospital day
int filterBombCards(int seat)
{
	int n = 0;
	for(int k = 0; k < rm8CardMax(); k++){ int c = rm8CardAt(seat, k); if(c >= 16 && c <= 19){ rm8CardRemoveAt(seat, k); n++; } }
	return n;
}
// type (the 5th argument of CPlayerTag__OnHolidayCardUse, +0x28 of the task): 0 = Building_SuccessBuild, the buildings of the plots in the blast area go but the owners stay (missile, and [V] the mine @0x481df0
// case 0x60 / the time bomb @0x48e400, which pass 0 too: radius 0 = the plot of the node), 1 = Building_FinishWithoutSuccess, the owners go as well (nuke); both when the 2.0 s task ends. -1 = nothing is destroyed (unused)
void blast(int actor, int node, int art, int snd, int radius, int baseDays, int ignoreBubble = -1, int type = -1)
{
	std::vector<int> nodes = neighbourhood(node, radius);
	std::vector<int> v;
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		if(rm8TurnStatus(s) >= 3) continue;
		if(std::find(nodes.begin(), nodes.end(), rm8TurnNode(s)) == nodes.end()) continue;
		if(radius > 0 && rm8StoryActive() && s == 0 && rm8TurnCharId(0) == 12){	// [V] CPlayerTag__OnSendGodCard @0x490df0 (called with the role in the area): the story hero 酷娃 confiscates the missile
			int ty = type > 0 ? 1 : 0;	// emote 0x3f / 0x41, a wait, the text str 570 / 571 on her own queue; the blast is cancelled
			if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: the %s of seat %d is confiscated by the story hero 酷娃 (str %d)", g_clock, ty ? "nuke" : "missile", actor, 570 + ty);
			rm8TurnEnqueueAt(0, 0, 0x5a, 0x3f + ty * 2, 0); rm8TurnEnqueueAt(0, 1, 0x65, 30, 0); rm8TurnEnqueueAt(0, 2, 0x1b, rm8GamePushText(rm8Str(570 + ty)), 0);
			if(actor != 0){ rm8TurnEnqueueAt(0, 3, 0x2e, actor + 1, 0); rm8TurnEnqueueAt(actor, 0, 0x2e, 1, 0); }	// [V] @0x490df0: 0x2e(user + 1) at position 3 of her queue, 0x2e(her seat + 1) at position 0 of the user's: control goes to her for the text and comes back
			return;
		}
		if(rm8CardsHasBodyguard(s)) continue;	// [V] vf+0x64 (the bodyguard of 莎拉) protects too
		if(rm8CardsHasBubble(s) && s != ignoreBubble) continue;	// the bubble protects (and bursts, 0x62)
		if(s == actor) v.insert(v.begin(), s); else v.push_back(s);	// [V] the actor takes slot 0
	}
	if(v.empty()) rm8TurnEnqueueAt(actor, 0, 0x1e, 0, 0);
	else{
		bool handoff = false;
		for(int k = (int)v.size() - 1; k >= 0; k--){
			int days = filterBombCards(v[k]) + baseDays;
			if(k > 0){ rm8TurnEnqueue(v[k], 0x18, days, 0); rm8TurnEnqueue(v[k], 0x2e, v[k - 1] + 1, 0); handoff = true; }
			else if(v[0] == actor){ rm8TurnClearQueue(actor); rm8TurnEnqueue(actor, 0x18, days, 0); }
			else{ rm8TurnEnqueue(v[0], 0x18, days, 0); rm8TurnEnqueue(v[0], 0x2e, actor + 1, 0); handoff = true; }
		}
		if(handoff){ rm8TurnEnqueueAt(actor, 0, 0x2e, v.back() + 1, 0); rm8TurnEnqueueAt(actor, 1, 0x1e, 0, 0); }
		for(int s : v) rm8TurnEnqueueAt(actor, 0, 0x81, 1, s + 1);	// the vehicle is lost (Role_SetTrfTool(1))
	}
	rm8TurnEnqueueAt(actor, 0, 0x62, node, radius);
	// [V] TerraMgr_AnalyzeRoleStocks @0x4f22e0: the effect task (state 9) with the effect article and its sample
	float p[3]; rm8TurnNodePos(node, p); p[1] += radius > 0 ? 0.0f : 30.0f;
	rm8TurnFocus(p);
	int fx = rm8FxPlay(art, p, 0, 0, true, snd, 1.0f, radius > 0 ? 2.0f : 4.0f * waitScale(), true);	// [V] u_Building_PlayEffectAndSound @0x4f4670: the article at the node, action 0 once, the sample; the task lasts 2.0 s
	std::vector<int> terras;
	if(type >= 0) for(int n : nodes){ int t = rm8GameNodeTerra(n); if(t >= 0 && std::find(terras.begin(), terras.end(), t) == terras.end()) terras.push_back(t); }	// [V] TerraMgr_AnalyzeRoleStocks @0x4f22e0: the distinct plots
	if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: blast at node %d (art %d, radius %d, base %d days) %d victims, effect %d", g_clock, node, art, radius, baseDays, (int)v.size(), fx);
	startTask(actor, fx, 2.0f, terras, type);	// [V] Cls_5e6a78__vf1 @0x4f4780: 2.0 s (raw dt) for the missiles, the mine and the time bomb alike; [A] the mine / bomb effect (radius 0) is kept playing until it ends
}

// ---- the robot doll [V] MiniGame_StartWithRole @0x49d250 (message 0x32, CPlayerTag__OnQandaEvent @0x48da00), Cls_5e2e48__OnUpdate @0x49d180
void dollAdvance(Obj &o, float dist);
bool dollStart(int seat)
{
	int from = rm8TurnNode(seat), to = rm8TurnNodeTo(seat);
	if(from < 0 || to < 0) return false;
	Obj *o = createObj(15);
	if(!o) return false;
	o->actor = seat; o->steps = 10; o->from = from; o->to = to; o->pos = nodeP(from); o->state = 3; o->scaleF = 1.0f;
	facing(*o, nodeP(from), nodeP(to));		// [V] Path_SetPoints @0x47b670: vf+0x2c(from, to, 0) = Path_CalcAngle without the spin
	startAnim(*o, 0, false);
	dollAdvance(*o, 20.0f);		// [V] vf+0x44(20.0)
	rm8SndPlay(14);			// [V] vf+0x50(0xe) PlaySoundOnce
	rm8TurnSetState(seat, 10);
	if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: the robot doll starts from node %d towards %d for seat %d", g_clock, from, to, seat);
	return true;
}
void dollRemoveAt(Obj &doll, int node)
{
	Obj *t = at(node);
	if(t && t != &doll){
		if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: the robot doll removes %s on node %d", g_clock, t->def->name.c_str(), node);
		finalize(*t);	// [V] Unk_CallVirtualMethodIfValid @0x49d2f0: node +0x10 -> vf+0x68 Finalize (gods, board characters, obstacles alike)
	}
}
// [V] Path_AnimateAlongRoute @0x47b730: vf18 (OnLose @0x49d380: clears the NPC on the node just reached) at every node with steps left, vf19 (OnWin @0x49d320) on the last
void dollAdvance(Obj &o, float dist)
{
	V3d t = nodeP(o.to);
	V3d v = {t.x - o.pos.x, t.y - o.pos.y, t.z - o.pos.z};
	float L = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
	for(;;){
		if(L > dist){ o.pos.x += v.x / L * dist; o.pos.y += v.y / L * dist; o.pos.z += v.z / L * dist; return; }
		o.pos = t;
		int nn = rm8TurnSelectNext(o.from, o.to);
		o.from = o.to; o.to = nn;
		dist -= L;
		t = nodeP(o.to);
		v = {t.x - o.pos.x, t.y - o.pos.y, t.z - o.pos.z};
		L = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
		facing(o, nodeP(o.from), t, true);	// [V] Path_AnimateAlongRoute @0x47b730: vf+0x2c(from, to, 1) = Path_CalcAngle with the turn animation
		if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: doll node %d -> %d (steps %d)", g_clock, o.from, o.to, o.steps - 1);
		if(--o.steps < 1 || L < 1e-4f){
			dollRemoveAt(o, o.from);
			finalize(o);		// [V] Cls_5e2e48__OnWin: the doll's Finalize -> state 5, the role leaves state 10
			return;
		}
		dollRemoveAt(o, o.from);
	}
}

// ---- god possession [V] Cls_5e33a8__OnClick @0x49bb40, Cls_5e33a8__Animate @0x49bce0, BeginCollection / BeginAttach / BeginBless
void collectDone(void *ctx, int amount);
// [V] Cls_5e33a8__OnClick @0x49bb40: the cloud effect (Anim_CreateSprite(0x17ea = 6122, node + 0x14, anim 0, .., sample 0x1e): 4 s x wait scale, the particle art has no
// animation callback; the handle: the click skip releases it, rm8NpcSkip), the god takes the role (+0x88), state 1, stage 1; NPC_HandleInteract returns 1: the
// dispatcher @0x481df0 sets the role state 10 and Role_ClearFlag1C8 (the role is not drawn until Role_LeaveState10 / Role_SetPossession)
int godClick(int seat, Obj &o)
{
	if(o.state != 0) return 0;
	o.fx = rm8FxPlayAtRole(seat, 0x17ea, 0x1e);
	o.actor = seat; o.state = 1; o.stage = 1; o.timer = 0; o.snd = 0;
	return 1;
}
// [V] Cls_5e33a8__Animate @0x49bce0, called every frame while the timer (+0x90) is 0 (Cls_5e2b50__Update @0x49b350 -> OnSelect @0x49b8d0)
void godAnimate(Obj &o, float dt)
{
	switch(o.stage){
	case 1:	// [V] while the cloud lives (+0x54 >= 0): the god grows from 0.7 to its npcinfo scale at 0.2 / s (+0x74 = 0.7 by CalcScale @0x49bc70, Mesh_SetScale);
		// when it is gone: full scale, Mesh_SetTexture(3, 1) = the appearing animation once (+0x5c busy until its callback), the NPC's sample (npcinfo sound, Anim_PlaySound)
		if(o.fx == 0 || rm8FxFinished(o.fx)){
			if(o.fx){ rm8FxStop(o.fx); o.fx = 0; }
			o.scaleF = 1.0f;
			startAnim(o, 3, true);
			if(o.def->sound > 0) o.snd = rm8SndPlay(o.def->sound);
			o.stage = 2;
		}else if(o.scaleF < 1.0f) o.scaleF = std::min(1.0f, o.scaleF + dt * 0.2f);
		break;
	case 2:	// [V] animation 3 over: stand (2, loop), Anim_PlaySound(string 400 + id voice @5301..: stops the entrance sample first), the text is drawn by
		// Cls_5e33a8__OnUpdate @0x49b910 while stage 3; the timer = 2.0 x wait scale (Anim_ApplyDifficultyScale)
		if(o.busy) break;
		startAnim(o, 2, false);
		{
			int snd; std::string t = plain(rm8Str(400 + o.id), &snd);
			if(o.snd) rm8SndStop(o.snd);
			o.snd = snd > 0 ? rm8SndPlay(snd) : 0;
			greetingShow(t);
		}
		o.timer = 2.0f * waitScale();
		o.stage = 3;
		break;
	case 3:	// [V] timer over, no animation busy (+0x5c) and the greeting voice has ended (+0x50 == 0): Role_SetPossession(id), then the god's own effect (vf+0x98);
		// Finalize unless the reel dialog started
		if(o.busy) break;
		if(o.snd && rm8SndPlaying(o.snd)) break;
		o.snd = 0;
		greetingHide();
		if(o.actor >= 0){
			int seat = o.actor;
			rm8NpcBecome(seat, o.id, 0);
			bool wait = false;
			switch(o.id){
			case 1: case 2: {	// [V] BeginCollection (str408 / str410) + the reel dialog; the amount from the other roles / to them
				wait = true; o.state = 2; o.collectPending = true;
				reelOpen(rm8Str(o.id == 1 ? 408 : 410), rm8TurnHuman(seat), collectDone, &o);
				break; }
			case 3: {	// [V] BeginAttach @0x49c280: str411, half of the cards are lost (message 5, a = -1, b = count)
				int half = rm8CardCount(seat) / 2;
				if(half > 0){ queueText(seat, rm8Str(411)); rm8TurnEnqueue(seat, 5, -1, half); }
				break; }
			case 4:	// [V] BeginBless @0x49c310: str413, two cards (message 4, a = -1, b = 1)
				queueText(seat, rm8Str(413));
				rm8TurnEnqueue(seat, 4, -1, 1); rm8TurnEnqueue(seat, 4, -1, 1);
				break;
			default: break;	// 5 土地公, 6 糊塗神: nothing at once (the land rules / sleepwalking act later)
			}
			if(wait) return;
		}
		finalize(o);
		break;
	}
}

// [V] Cls_5e3588__OnConfirm @0x49bf50 (財神) / Cls_5e34e8__OnConfirm @0x49c120 (窮神): the amount shown by the reels, from / to every other role
void collectDone(void *ctx, int amount)
{
	Obj *o = (Obj*)ctx;
	bool found = false; for(Obj *p : g_objs) found |= p == o;
	if(!found) return;
	if(o->state == 2 && o->actor >= 0){
		int me = o->actor, n = 0;
		for(int s = 0; s < rm8TurnRoleCount(); s++){
			if(s == me || rm8TurnStatus(s) == 7) continue;
			if(o->id == 1) rm8GameSubMoney(s, amount, false); else rm8GameAddMoney(s, amount, false);
			n++;
		}
		if(o->id == 1){ rm8GameAddMoney(me, (long)amount * n, false); rm8PresentMoney(me, (long)amount * n, true); }
		else{ rm8GameSubMoney(me, (long)amount * n, false); rm8PresentMoney(me, (long)amount * n, false); }	// [V] Role_SubMoney(.., 0, 1) @0x49c120: with the emote
		if(getenv("RM8_DEBUG_NPC")) rm8Log("npc: %s %d x %d (seat %d)", o->id == 1 ? "財神 collects" : "窮神 pays", amount, n, me);
	}
	o->collectPending = false;
	finalize(*o);
}

// ---- board characters / obstacles: OnRoleClick of each class
int boardClick(int seat, Obj &o)
{
	if(o.state != 0) return 0;
	switch(o.id){
	case 7: case 8: {	// [V] Cls_5e2be8__OnRoleClick @0x49c390 + Cls_5e3310__Roll @0x49c480 / Cls_5e3278__Roll @0x49c620: Finalize, the news (police: rand() & 7, thief: 9)
		finalize(o); o.actor = seat;
		rm8TurnClearQueue(seat);
		newsEvent(seat, o.id == 7 ? rnd() % 8 : 9, seat);
		return 2; }
	case 10: case 11:	// [V] Cls_5e3148__OnRoleClick @0x49c730 / Cls_5e30a8__OnRoleClick @0x49c800: sample 0x41 / 0x42, then the news 10 / 11
		finalize(o);
		rm8TurnEnqueueAt(seat, 0, 0x53, o.id == 10 ? 0x41 : 0x42, 0);
		rm8TurnEnqueueAt(seat, 1, 0x3c, o.id, seat + 1);
		return 2;
	case 9: {	// [V] Cls_5e31e0__OnRoleClick @0x49ca80 + BeginAttach @0x49caf0
		o.actor = seat; o.state = 1;
		if(rm8TurnDiceMax(seat) < 3){	// on foot or a motorbike: bitten, hospital 2 days
			finalize(o);
			rm8TurnClearQueue(seat);
			rm8TurnEnqueue(seat, 0x2c, 0x1788, 0x3f);
			rm8TurnEnqueue(seat, 0x18, 2, 0);
			rm8StoryDogBite(seat);	// [V] Cls_5e31e0__BeginAttach @0x49caf0: 烏咪's puzzle piece 2 (story layer 0)
			return 2;
		}
		// in a car: the dog is thrown away (0.8 s), the role waits in state 9 meanwhile
		rm8TurnSetState(seat, 9);
		rm8SndPlay(0x40);
		startAnim(o, 5, false);
		o.stage = 1; o.t = 0;
		{
			int from = rm8TurnNode(seat), to = rm8TurnNodeTo(seat), other = to, nb[16], c = rm8TurnLinks(from, nb, 16);
			for(int k = 0; k < c; k++) if(nb[k] != to){ other = nb[k]; break; }
			V3d a = nodeP(from), b = nodeP(other), d = {a.x - b.x, a.y - b.y, a.z - b.z};
			float l = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z); if(l > 1e-4f){ d.x /= l; d.y /= l; d.z /= l; }
			o.dir = d;
		}
		return 2; }
	case 12:	// [V] Cls_5e3010__OnRoleClick @0x49ce50: Finalize, sample 0x46 + 60 frames, speak 0x34, the walk stops
		finalize(o);
		rm8TurnEnqueueAt(seat, 0, 0x53, 0x46, 0x3c);
		rm8TurnEnqueueAt(seat, 1, 0x5A, 0x34, 0);
		return 2;
	case 13:	// [V] Cls_5e2f78__OnRoleClick @0x49cf50: the mine goes off (0x60) unless a bubble protects
		finalize(o);
		if(!rm8CardsHasBubble(seat)){ rm8TurnClearQueue(seat); rm8TurnEnqueue(seat, 0x60, 0, 0); }
		else{
			rm8TurnEnqueueAt(seat, 0, 0x2c, 0x1775, 0x43);
			rm8TurnEnqueueAt(seat, 1, 0x3f, 0, 0);
			rm8TurnEnqueueAt(seat, 2, 0x65, 0x1e, 0);
		}
		return 2;
	case 14:	// [V] Cls_5e2ee0__OnRoleClick @0x49d000: sample 0x11 + 60 frames, then the time bomb (0x3A, 36)
		finalize(o);
		rm8TurnEnqueueAt(seat, 0, 0x53, 0x11, 0x3c);
		rm8TurnEnqueueAt(seat, 1, 0x3a, 0x24, 0);
		return 3;
	case 17:	// [V] Cls_5e2b50__OnRoleClick @0x49d100: only the first seat (the story hero), sample 0x41, then 0x58(piece)
		if(seat != 0) return 0;
		rm8TurnEnqueueAt(seat, 0, 0x53, 0x41, 0);
		rm8TurnEnqueueAt(seat, 1, 0x58, o.param, 0);
		finalize(o);
		return 2;
	case 23:	// [V] Cls_5e2db0__OnRoleClick @0x49cd70: the egg: effect 0x1772, then a random shape 100..103
		o.fx = rm8FxPlayAtRole(seat, 0x1772, 0);
		o.actor = seat; o.state = 1;
		return 1;
	}
	return 0;
}

}	// namespace

// ================================================================ exported to npc.cpp
int interact(int seat, Obj &o)
{
	if(o.def->type == 0) return godClick(seat, o);
	return boardClick(seat, o);
}

void actUpdate(Obj &o, float dt)
{
	if(o.state == 1 && o.def->type == 0){	// [V] Cls_5e2b50__Update @0x49b350: timer, then vf+0x88 (Cls_5e33a8__OnSelect) -> Animate
		o.timer -= dt;
		if(o.timer <= 0){ o.timer = 0; godAnimate(o, dt); }
	}else if(o.state == 1 && o.id == 9 && o.stage == 1){	// [V] Cls_5e31e0__OnUpdate @0x49c940: 0.8 s flight away from the car
		o.t += dt;
		if(o.t < 0.8f){
			float dy = o.t >= 0.5f ? -150.0f * dt : 100.0f * dt;
			o.pos.x += o.dir.x * 300.0f * dt; o.pos.y += dy; o.pos.z += o.dir.z * 300.0f * dt;
		}else{
			int seat = o.actor;
			finalize(o);
			if(validSeat(seat) && rm8TurnState(seat) == 9) rm8TurnSetState(seat, 8);
		}
	}else if(o.state == 1 && o.id == 23){	// [V] Cls_5e2db0__OnUpdateInner @0x49cde0: the effect is over -> Role_SetPossession(100 + (rand() % 10) / 3)
		if(o.fx == 0 || rm8FxFinished(o.fx)){
			if(o.fx) rm8FxStop(o.fx);
			if(o.actor >= 0) rm8NpcBecome(o.actor, 100 + (rnd() % 10) / 3, 0);
			finalize(o);
		}
	}else if(o.state == 3){	// [V] Cls_5e2e48__OnUpdate @0x49d180: the robot doll walks 100 units / s
		spinUpdate(o, dt);
		V3d p = o.pos;
		float pp[3] = {p.x, p.y, p.z}; rm8TurnFocus(pp);
		dollAdvance(o, dt * 100.0f);
	}
}

// ---- hospital / prison transfer show (npc.h rm8NpcTransfer): [V] Stock_InitializeObject_Type1 @0x4a83c0 (police car art 0x526 = 1318 Mys118, scale 0.5, times 1.8 / 5.0 s,
// siren sample 0x47) / Type2 @0x4a84f0 (ambulance art 0x527 = 1319 Mys119, 0.5, 2.8 / 7.5 s, sample 0x48): Cls_5e3af0__vf16 @0x4b1090 loads the mesh (Mesh_Rotate camYaw - 180,
// Mesh_SetScale 0.5), RoleAnimation_Start @0x4b11d0 puts it on the role (Mesh_SetTranslation), Mesh_SetTexture(0, 1) = animation 0 once, plays the sample with loop 0
// (until the end); RoleAnimation_UpdateFrame @0x4b1370 advances the mesh and the clock: at the first time RoleAnimation_TriggerAction @0x4b1470 = role vf+0x6c
// (Role_ClearFlag1C8 + Role_ResnapToNode @0x48ced0: the role is hidden), at the second the object's vf18 @0x4b1500 / @0x4b15d0 stops the sample, calls the role's
// SendToPrison (vf+0x74) / SendToHospital (vf+0x70) and Camera_SetFocusPoint(role); Game_OnUpdate @0x4a17b0 then runs Game_EndTurn @0x4a48d0 (role states 9 -> 8)
struct Transfer { int seat = -1, kind = 0, days = 0, phase = 0, snd = 0; Mdl m; float t = 0, t1 = 0, t2 = 0; V3d pos = {0, 0, 0}; void (*commit)(int, int, int) = nullptr; };
std::vector<Transfer*> g_transfers;

void actClear()
{
	g_tasks.clear(); greetingHide(); reelClear();
	for(Transfer *x : g_transfers){ rm8SndStop(x->snd); freeMdl(x->m); delete x; }
	g_transfers.clear();
}

// [A] the vehicle is a plain article (Cls_5e3af0__vf2_RenderMesh @0x4b1420 -> u_Mesh_Update, not the character pass): the scene's alpha test (low reference), not the
// characters' >= 196, so the translucent siren domes show; the flags of the atomics ([$*)] ...) are handled by rm8RenderArticleClump
void transferRender()
{
	if(g_transfers.empty()) return;
	uint32 fn = GetRenderState(ALPHATESTFUNC), ref = GetRenderState(ALPHATESTREF);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 10);
	for(Transfer *x : g_transfers) if(x->m.clump) rm8RenderArticleClump(x->m.clump);
	SetRenderState(ALPHATESTFUNC, fn); SetRenderState(ALPHATESTREF, ref);
}

void transferUpdate(float dt)
{
	for(size_t i = 0; i < g_transfers.size();){
		Transfer *x = g_transfers[i];
		x->t += dt;
		stepAnim(x->m, dt, true);
		placeMdl(x->m, x->pos, {0.5f, 0.5f, 0.5f}, 0, {0, 0, 0});
		if(x->phase == 0 && x->t >= x->t1){
			x->phase = 1;
			S(x->seat).hidden = true;	// [V] vf+0x6c: the role is carried off
			rm8TurnFollowerRunOff(x->seat);	// [V] the class of 伊格魯 / 莎拉 @0x47dfa0: the companion runs off to the camera's right
			if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: transfer %d: seat %d is picked up", g_clock, x->kind, x->seat);
		}
		if(x->phase == 1 && x->t >= x->t2){
			int seat = x->seat;
			rm8SndStop(x->snd);
			if(x->commit) x->commit(seat, x->kind, x->days);
			S(seat).hidden = false;		// [V] SendToHospital / SendToPrison end with Role_SetFlag1C8
			float p[3]; if(rm8RolesGetPos(seat, p)) rm8TurnFocus(p);
			if(rm8TurnState(seat) == 9) rm8TurnSetState(seat, 8);	// [V] Game_EndTurn: Role_UpdateStatusTransition @0x4860d0
			if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: transfer %d: seat %d arrives (%d days)", g_clock, x->kind, seat, x->days);
			freeMdl(x->m); delete x; g_transfers.erase(g_transfers.begin() + i);
			continue;
		}
		i++;
	}
}
void actNewGame()
{
	g_tasks.clear();
	// test hooks (docs/npc_port.md): RM8_NPC_PLACE="id@k,id@k" puts NPC id on the node k steps ahead of seat 0 (k = 1: the node it walks to first), RM8_NPC_MOVE="seat@k", RM8_NPC_SEAL=id, RM8_NPC_BOMB="seat:counter",
	// RM8_NPC_BECOME="seat:shape[:days]", RM8_NPC_VEHICLE="seat:vehicle", RM8_NPC_GO=n makes seat 0 roll n at the start
	auto ahead = [](int k){	// the node k steps ahead of seat 0: 1 = the node it walks to first, 0 = its own node
		int from = rm8TurnNode(0), to = rm8TurnNodeTo(0), prev = from, cur = to;
		for(int i = 1; i < k; i++){ int nb[16], c = rm8TurnLinks(cur, nb, 16), nx = prev; for(int j = 0; j < c; j++) if(nb[j] != prev){ nx = nb[j]; break; } prev = cur; cur = nx; }
		return k <= 0 ? from : cur;
	};
	if(const char *e = getenv("RM8_NPC_PLACE")){
		std::string s = e; size_t p = 0;
		while(p < s.size()){
			size_t q = s.find(',', p); if(q == std::string::npos) q = s.size();
			int id = 0, k = 1; sscanf(s.substr(p, q - p).c_str(), "%d@%d", &id, &k);
			p = q + 1;
			int cur = ahead(k);
			if(Obj *x = at(cur)) finalize(*x);
			if(g_defs.find(id) == g_defs.end()) continue;
			Obj *o = nullptr;
			if(id <= 11){ for(Obj *x : g_objs) if(x->id == id){ o = x; break; } if(o && o->state == 0) finalize(*o); }
			else o = createObj(id);
			if(o && id == 17) o->param = 5;	// the puzzle piece number
			if(o && onLand(*o, cur)) rm8Log("npc test: %d placed on node %d (%d steps ahead of seat 0)", id, cur, k);
		}
	}
	if(const char *e = getenv("RM8_NPC_AT")){	// [A] "seat:node" (bug 44 test hook): that seat stands on that road node index
		int seat = 0, n = 0;
		if(sscanf(e, "%d:%d", &seat, &n) == 2 && validSeat(seat)){ int nb[16], c = rm8TurnLinks(n, nb, 16); rm8TurnPlaceAt(seat, n, c > 0 ? nb[0] : n); rm8Log("npc test: seat %d placed on node %d", seat, n); }
	}
	if(const char *e = getenv("RM8_NPC_MOVE")){	// "seat@k": that seat stands k steps ahead of seat 0
		int seat = 1, k = 2;
		if(sscanf(e, "%d@%d", &seat, &k) == 2 && validSeat(seat)){ int n = ahead(k), nb[16], c = rm8TurnLinks(n, nb, 16); rm8TurnPlaceAt(seat, n, c > 0 ? nb[0] : n); rm8Log("npc test: seat %d moved to node %d", seat, n); }
	}
	if(const char *e = getenv("RM8_NPC_BECOME")){
		int seat = 0, shape = 0, days = 0;
		if(sscanf(e, "%d:%d:%d", &seat, &shape, &days) >= 2){ rm8NpcBecome(seat, shape, days); rm8Log("npc test: seat %d becomes %d for %d days", seat, shape, days); }
	}
	if(const char *e = getenv("RM8_NPC_SEAL")){	// NPC id: seat 0 seals it at the start (message 0x59, Role.Seal)
		for(size_t i = 0; i < g_objs.size(); i++) if(g_objs[i]->id == atoi(e)){ rm8TurnEnqueue(0, 0x59, (int)i, 0); break; }
	}
	if(const char *e = getenv("RM8_NPC_BOMB")){	// "seat:counter": a time bomb on that seat
		int seat = 0, n = 36;
		if(sscanf(e, "%d:%d", &seat, &n) == 2 && validSeat(seat)) S(seat).bomb = n;
	}
	if(const char *e = getenv("RM8_NPC_VEHICLE")){	// "seat:vehicle" (1 walk, 2 bike, 3 car)
		int seat = 0, v = 1;
		if(sscanf(e, "%d:%d", &seat, &v) == 2 && validSeat(seat)) rm8TurnSetVehicle(seat, v);
	}
	if(getenv("RM8_NPC_DOLL")) rm8TurnEnqueue(0, 0x32, 0, 0);	// the robot doll starts at once for seat 0
	if(const char *e = getenv("RM8_NPC_SPAM")) spamEnable(atoi(e));
	if(const char *e = getenv("RM8_NPC_GO")) rm8TurnEnqueue(0, 0x1a, atoi(e), 0);
	if(const char *e = getenv("RM8_NPC_MISSILE")){ int actor = atoi(e); bool nuke = strchr(e, 'n') != nullptr; rm8TurnEnqueue(actor, nuke ? 0x37 : 0x36, rm8TurnNode(0), 0); }	// test hook "actor[n]": that seat fires a missile (nuke with n) at the node of seat 0 at the start (the story hero 酷娃 confiscates it)
}

void actUpdateGlobal(float dt){ updateTasks(dt); reelUpdate(dt); transferUpdate(dt); }

}	// namespace npc

using namespace npc;

// ================================================================ turn flow
int rm8NpcInteract(int seat, int node)
{
	if(!g_s || !validSeat(seat)) return 0;
	Obj *o = at(node);
	if(!o) return 0;
	int shape = rm8GameShape(seat);
	if(shape == 0x66 || (shape == 0x65 && o->def->type == 0)) return 0;	// [V] NPC_HandleInteract @0x49dfb0: the frog (101) ignores gods, 102 everything
	if(o->state != 0) return 0;
	int r = interact(seat, *o);
	static const bool dbg = getenv("RM8_DEBUG_NPC") != nullptr;
	if(dbg) rm8Log("npc [%.2f]: seat %d (char %d) meets %s (id %d) on node %d -> %d", g_clock, seat, rm8TurnCharId(seat), o->def->name.c_str(), o->id, node, r);
	return r;
}

// [V] u_CPlayerTag_UpdateTurnCountdown @0x485a20, in this order: the time bomb, the NPCs (roadblock), the possessed police / thief / dog
static int pickAtNode(int seat, bool otherOnly = true)	// [V] CPlayerTag__GetRandomBankruptRole @0x48ebb0(2, ..): another role, status <= 2, visible, on the same node
{
	std::vector<int> c;
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		if(s == seat || rm8TurnStatus(s) > 2 || rm8GameInvisible(s) > 0) continue;
		if(rm8TurnNode(s) != rm8TurnNode(seat)) continue;
		c.push_back(s);
	}
	return c.empty() ? -1 : c[rnd() % c.size()];
}

// [V] Role_RunPoliceThiefDogAction @0x48bd00: the possessed police arrests, the thief steals half of the tickets, the dog bites (hospital 2 days)
static bool policeAction(int seat)
{
	int shape = rm8GameShape(seat);
	if(shape < 7 || shape > 9) return false;
	int tgt = pickAtNode(seat);
	if(tgt < 0) return false;
	rm8TurnClearQueue(seat);
	if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: seat %d (possessed by %d) meets seat %d on node %d", g_clock, seat, shape, tgt, rm8TurnNode(seat));
	if(shape == 7){
		if(rm8GameShape(tgt) == 8) rm8TurnEnqueue(seat, 0x3c, 8, tgt + 1);	// the thief caught by the police: str426
		else rm8TurnEnqueue(seat, 0x3c, rnd() % 8, tgt + 1);
	}else if(shape == 8){
		long half = rm8GamePoints(tgt) / 2;
		rm8GameAddPoints(seat, std::min(half, 9999L - std::min(9999L, rm8GamePoints(seat))));	// Role_AddTicket(half, 0), tickets <= 9999
		rm8TurnEnqueue(seat, 0x3c, 9, tgt + 1);
		rm8TurnEnqueue(seat, 0x47, 0, 0);
	}else{
		rm8TurnEnqueue(seat, 0x42, 0xe, 0); rm8TurnEnqueue(seat, 0x2c, 0x177d, 0x3f); rm8TurnEnqueue(seat, 0x42, 2, 0);
		rm8TurnEnqueue(seat, 0x2e, tgt + 1, 1);
		rm8TurnEnqueue(tgt, 0x18, 2, 0);
		rm8StoryDogBite(tgt);	// [V] @0x48bd00 dog case: Event_GetItemData(10, 2) == 0 -> 0x58(2) for the bitten 烏咪
		rm8TurnEnqueue(tgt, 0x2e, seat + 1, 0);
	}
	return true;
}

int rm8NpcPass(int seat, int node)
{
	if(!g_s || !validSeat(seat)) return 0;
	Seat &s = S(seat);
	if(s.bomb > 0){
		if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: seat %d passes node %d, bomb %d", g_clock, seat, node, s.bomb - 1);
		if(--s.bomb < 1){	// [V] the counter ran out: CPlayerTag__OnUseTargetCard @0x48e400 (blast radius 0, base 7 days, sample 0x45)
			s.bomb = 0;
			rm8TurnClearQueue(seat);
			blast(seat, rm8TurnNode(seat), 0x1775, 0x45, 0, 7, seat, 0);
			return 1;
		}
		int tgt = pickAtNode(seat);	// [V] GetRandomBankruptRole(2, 0): the bomb jumps to / is swapped with another role on this node
		if(tgt >= 0){
			Seat &t = S(tgt);
			if(t.bomb > 0) std::swap(t.bomb, s.bomb);
			else{ t.bomb = s.bomb; s.bomb = 0; }
		}
	}
	Obj *o = at(node);
	if(o && o->id == 12 && o->state == 0){
		if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: seat %d is stopped by a roadblock on node %d", g_clock, seat, node);
		interact(seat, *o); return 2;
	}	// [V] the roadblock stops the walk (Role_CancelMoveSteps @0x48b6b0)
	if(policeAction(seat)){ rm8TurnEnqueue(seat, 0x26, 0, 0); return 1; }	// [V] the walk resumes with 0x26
	return 0;
}

bool rm8NpcOnStop(int seat)
{
	if(!g_s || !validSeat(seat)) return false;
	Seat &s = S(seat);
	if(s.bomb > 0 && --s.bomb < 1){	// [V] OnStopAtTerra: the counter runs out on the last node too
		s.bomb = 0;
		blast(seat, rm8TurnNode(seat), 0x1775, 0x45, 0, 7, seat, 0);
		return true;
	}
	if(policeAction(seat)) rm8TurnEnqueue(seat, 0x1e, 0, 0);
	return false;
}

// ---- queued messages of the NPC module
bool rm8NpcMessage(int seat, int cmd, int a, int b)
{
	if(!g_s) return false;
	switch(cmd){
	case 0x23: {	// [V] Role_DispatchQueuedMsg @0x481df0: NPC_HandleInteract == 1 -> state 10; else a wait when the node has an event icon
		int r = rm8NpcInteract(seat, a);
		if(r == 1){ rm8TurnSetState(seat, 10); S(seat).hidden = true; }	// [V] state 10 + Role_ClearFlag1C8 @0x48a490
		else if(rm8GameNodeEvent(a) != 0) rm8TurnEnqueueAt(seat, 0, 0x65, 0x1e, 0);	// Role_GetWaitFrames(4) = 30
		return true; }
	case 0x1e: { float p[3]; if(rm8RolesGetPos(seat, p)) rm8TurnFocus(p); return true; }	// [V] Camera_SetFocusPoint(role position)
	case 0x42: rm8TurnPlayAnim(seat, a, b != 0); return true;	// [V] vf5(a, b, 0)
	case 0x32: if(!dollStart(seat)) rm8Log("npc: the robot doll could not start"); return true;	// [V] CPlayerTag__OnQandaEvent @0x48da00
	case 0x3a: {	// [V] CPlayerTag__InitializeTarget(a, 1) @0x48e2d0: the time bomb (counter a), speak 0x3D
		if(a > 0){ S(seat).bomb = a; rm8RoleSayAt(seat, 0, 0x3d); }
		return true; }
	case 0x51: S(seat).bomb = 0; return true;	// [V] CPlayerTag__CleanupTarget @0x48e440 (Role.TimeBombDis)
	case 0x3c: newsEvent(seat, a, b - 1); return true;	// [V] CPlayerTag__OnLotteryEvent @0x48e7e0 -> News_OnUpdate(a, seat b - 1)
	case 0x58: {	// [V] CPlayerTag__OnPuzzleCardUse @0x491830: the puzzle piece a (1..9)
		if(a > 0 && a < 10){
			rm8PuzzleCollect(seat, a);	// the text entry (str434 with the piece picture) is queued there
		}
		return true; }
	case 0x60: {	// [V] message 0x60 @0x481df0: CPlayerTag__OnHolidayCardUse(from, 0x1775, 0x43, 0, 0, 3) then speak 0x3B (the mine)
		blast(seat, rm8TurnNode(seat), 0x1775, 0x43, 0, 3, -1, 0);
		rm8RoleSayAt(seat, 0, 0x3b);
		return true; }
	case 0x59: {	// [V] CPlayerTag__OnWonderConstruction @0x492930 (Role.Seal): a = NPC index; b = 0 the seal effect, b = 1 the NPC leaves
		Obj *o = byHandle(a + 1);
		if(!o) return true;
		if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: seal %s (index %d) step %d, on node %d", g_clock, o->def->name.c_str(), a, b, o->node);
		if(b == 0){
			if(o->node >= 0){ float p[3]; rm8TurnNodePos(o->node, p); rm8TurnFocus(p); p[1] += 30.0f;
				rm8FxPlay(0x177c, p, 0, 0, true, 0x23, 1.0f, 4.0f * waitScale(), true); }
			rm8TurnEnqueueAt(seat, 0, 0x65, 0x3c, 0);
			rm8TurnEnqueueAt(seat, 1, 0x59, a, 1);
		}else{
			finalize(*o);
			rm8TurnEnqueueAt(seat, 0, 0x65, 0x1e, 0);
			rm8TurnEnqueueAt(seat, 1, 0x1e, 0, 0);
		}
		return true; }
	case 0x45:	// [V] message 0x45 (Role.Bodyguard, card 46) = vf+0x80 CPlayerTag__vf15_ShowState @0x48ecf0: clear the queue, effect 0x1787 (6023 D46tB), hospital 3 days.
		// HasBodyguard (vf25 @0x4f6230) is 0 for every role class, so no NPC code ever sees a bodyguard
		rm8TurnClearQueue(seat); rm8TurnEnqueue(seat, 0x2c, 0x1787, 0); rm8TurnEnqueue(seat, 0x18, 3, 0);
		return true;
	case 0x81:	// [V] message 0x81 @0x481df0: Role_SetTrfTool(a) of the role b - 1 (the blasts: vehicle 1)
		if(b > 0 && b - 1 < rm8TurnRoleCount()) rm8TurnSetVehicle(b - 1, a);
		return true;
	}
	return false;
}

bool rm8NpcPress(){ return reelPress(); }

// [V] see Transfer: message 0x18 (NPC_CreateRandom_Type2 @0x4a45f0) / 0x19, 0xC9 (Type1 @0x4a4510); the role goes to state 9 (@0x481df0 case 0x18 / 0x19)
bool rm8NpcTransfer(int seat, int kind, int days, void (*commit)(int, int, int))
{
	if(!g_s || !validSeat(seat) || rm8TurnStatus(seat) == 7 || getenv("RM8_NPC_NO_TRANSFER")) return false;
	Transfer *x = new Transfer;
	x->seat = seat; x->kind = kind; x->days = days; x->commit = commit;
	if(!loadMdl(x->m, kind == 1 ? 0x526 : 0x527)){ delete x; return false; }
	float p[3]; if(!rm8RolesGetPos(seat, p)){ freeMdl(x->m); delete x; return false; }
	x->pos = {p[0], p[1], p[2]};
	x->t1 = kind == 1 ? 1.8f : 2.8f; x->t2 = kind == 1 ? 5.0f : 7.5f;
	setAnim(x->m, 0, true);
	x->snd = rm8SndPlay(kind == 1 ? 0x47 : 0x48, 0);	// loops = 0: forever, stopped at the end (RoleAnimation_Start @0x4b11d0)
	rm8TurnFocus(p);
	rm8TurnSetState(seat, 9);
	g_transfers.push_back(x);
	if(getenv("RM8_DEBUG_NPC")) rm8Log("npc [%.2f]: transfer %d for seat %d (%d days) starts", g_clock, kind, seat, days);
	return true;
}

// [V] RoleAnimation_SkipToEnd @0x4B1320 (vf13 of the police car / ambulance / UFO scene tasks, docs/skip.md): state 1 -> the role is carried off at once (vf17 = RoleAnimation_TriggerAction),
// the clock jumps to 20 s so the end (SendToPrison / SendToHospital) follows in the next update
bool rm8NpcTransferSkip()
{
	if(g_transfers.empty()) return false;
	for(Transfer *x : g_transfers){
		if(x->phase == 0){ x->phase = 1; S(x->seat).hidden = true; rm8TurnFollowerRunOff(x->seat); }
		x->t = std::max(x->t, x->t2);
	}
	return true;
}

// [V] NPC vtable +0x60 of the gods (npcinfo 1..6, Cls_5e33a8__OnReady @0x49BAD0; every other NPC class has the stub @0x4F6230): state 1 (interacting) and stage < 3 ->
// timer 0, sample + effect released (Anim_StopSample / Anim_ReleaseSprite), stage 3 at once: the possession and the god's own effect come at the next update.
// The scene vf13 @0x4A3060 reaches it through Collection_FindFirstByVf60 @0x49DEA0, which calls it on every NPC (the stub's 0 / this one's 0 = "not consumed").
// docs/skip.md. Returns true when a god was cut short (the original returns 0 for the click; the port consumes the tap).
bool rm8NpcSkip()
{
	bool any = false;
	for(Obj *o : g_objs){
		if(!o || !o->def || o->def->type != 0 || o->state != 1 || o->stage >= 3) continue;
		o->timer = 0;
		if(o->fx){ rm8FxStop(o->fx); o->fx = 0; }
		if(o->snd){ rm8SndStop(o->snd); o->snd = 0; }	// Anim_StopSample
		o->stage = 3;
		any = true;
	}
	return any;
}

// [V] CPlayerTag__ClearPossessedGod @0x48edc0 (News_EventDispatcher 0x39, Thanksgiving: Role_DoAction @0x4b8880 for every role) without the status days (cards.cpp):
// a role possessed by 2 (窮神) / 3 (衰神) / 6 (糊塗神) loses the possession days (+0x1DC = 0: it leaves at the next turn start), and CleanupTarget @0x48e440 clears the time bomb
void rm8NpcClearPossessed(int seat)
{
	if(!validSeat(seat)) return;
	int sh = rm8GameShape(seat);
	if(sh == 2 || sh == 3 || sh == 6) S(seat).possDays = 0;
	S(seat).bomb = 0;
}

// the missile / nuke cards (cards.cpp, messages 0x36 / 0x37): CPlayerTag__OnHolidayCardUse @0x48de10 with the card's article, sample, radius, hospital days and kind
void rm8NpcBlast(int actor, int node, int art, int snd, int radius, int baseDays, int type)
{
	blast(actor, node, art, snd, radius, baseDays, -1, type);
}
