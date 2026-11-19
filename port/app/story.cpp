// Story mode runtime, see story.h and docs/story_mode.md. [V] = verified in the decompile (address), [M] meaning inferred, [H] hypothesis, [A] port adaptation.
#include "story.h"
#include "scene.h"
#include "collect.h"
#include "cards.h"
#include "present.h"
#include "npc.h"
#include "audio.h"
#include "ui.h"
#include "events.h"
#include "stock.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>

namespace {

std::map<int, std::vector<Rm8PassEntry>> g_pass;		// hero -> entries (file order)
std::map<int, std::pair<std::vector<std::string>, std::vector<std::string>>> g_movies;	// hero -> (start scripts, end scripts)
std::map<int, Rm8Drama> g_drama;
std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> g_missions;
bool g_loaded = false;

// run state of the board that is on (the original reads Singleton_GetPtrArray_00632de8 = setup block each time)
bool g_onBoard = false, g_story = false, g_rescued = false, g_confirmed = false;
std::map<int, std::string> g_stageDir;	// scenes/stages.txt: stage id -> scene folder
std::map<int, int> g_stageClime;		// stage id -> climate (stage.bin +0x210)
float g_testT = 0; bool g_testWon = false, g_testLost = false;	// test hooks
int g_hero = 0;			// seat 0's role Id
int g_stage = 0;		// setup+4 (map id)
int g_level = 0;		// setup+8
Rm8StoryAction g_action = RM8STORY_NONE;

// the 0x7e win scene [V] CPlayerTag__OnMovieIntro @0x492e40: effect 6000 at the role, music 103, 14 s * wait scale, role state 0xD
struct WinScene { bool on = false; int seat = 0, fx = 0; float t = 0, dur = 0; } g_win;
float waitScale(){ return 1.0f - std::min((rm8GameSpeed - 1) / 10.0f, 1.0f); }	// [V] Anim_ApplyDifficultyScale @0x47b3b0

const std::vector<Rm8PassEntry> g_noPass;
void resetSeatStory();
}
static bool monsterHit(int seat, int terra);	// namespace

Rm8StoryHost &rm8StoryHost(){ static Rm8StoryHost h; return h; }

// ======================================================================== data
bool rm8StoryLoad(const std::string &root)
{
	std::ifstream f(root + "/story.txt");
	if(!f){ rm8Log("story: no %s/story.txt (run scripts/export_story.py)", root.c_str()); return false; }
	g_pass.clear(); g_movies.clear(); g_drama.clear(); g_missions.clear();
	std::string line; Rm8Drama *cur = nullptr; int remain = 0;
	while(std::getline(f, line)){
		if(!line.empty() && line.back() == '\r') line.pop_back();
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "pass"){
			Rm8PassEntry e;
			ss >> e.role >> e.stage >> e.level >> e.opp[0] >> e.opp[1] >> e.opp[2] >> e.startCard[0] >> e.startCard[1] >> e.startCash >> e.startFund >> e.startTicket
			   >> e.startPic[0] >> e.startPic[1] >> e.rewardCash >> e.rewardTicket >> e.rewardCard[0] >> e.rewardCard[1] >> e.rewardCard[2] >> e.rewardCard[3]
			   >> e.rewardPic[0] >> e.rewardPic[1];
			if(ss) g_pass[e.role].push_back(e);
		}else if(kw == "movie"){
			int role, n; std::string w; ss >> role >> w >> n;
			auto &m = g_movies[role];
			for(int i = 0; i < n; i++){ ss >> w; m.first.push_back(w); }
			ss >> w >> n;
			for(int i = 0; i < n; i++){ ss >> w; m.second.push_back(w); }
		}else if(kw == "mission"){
			int r, l, p, c; if(ss >> r >> l >> p >> c) g_missions[{r, l}].push_back({p, c});
		}else if(kw == "drama"){
			int id, n; if(ss >> id >> n){ cur = &g_drama[id]; cur->lines.clear(); remain = n; }
		}else if(kw == "dl" && cur && remain > 0){
			int rl; ss >> rl; std::string t; std::getline(ss, t);
			if(!t.empty() && t[0] == ' ') t.erase(0, 1);
			cur->lines.push_back({rl, t}); remain--;
		}
	}
	g_stageDir.clear();
	{ std::ifstream sf(root + "/stages.txt"); std::string l2; while(std::getline(sf, l2)){ std::istringstream s2(l2); std::string kw; int id, clime = 0; std::string dir; if(s2 >> kw >> id >> dir >> clime && kw == "stage"){ g_stageDir[id] = dir; g_stageClime[id] = clime; } } }
	size_t n = 0; for(auto &kv : g_pass) n += kv.second.size();
	rm8Log("story: %d pass entries of %d heroes, %d movie sets, %d dramas, %d mission lists", (int)n, (int)g_pass.size(), (int)g_movies.size(), (int)g_drama.size(), (int)g_missions.size());
	g_loaded = !g_pass.empty();
	return g_loaded;
}
bool rm8StoryLoaded(){ return g_loaded; }
std::string rm8StoryMapDir(int id){ auto it = g_stageDir.find(id); return it == g_stageDir.end() ? std::string() : it->second; }
const std::vector<Rm8PassEntry> &rm8StoryPass(int role){ auto it = g_pass.find(role); return it == g_pass.end() ? g_noPass : it->second; }
const Rm8PassEntry *rm8StoryFindPass(int role, int stage)
{
	for(const Rm8PassEntry &e : rm8StoryPass(role)) if(e.stage == stage) return &e;	// [V] the first entry with this stage
	return nullptr;
}
std::vector<std::string> rm8StoryMovieScripts(int role, int kind)
{
	auto it = g_movies.find(role);
	if(it == g_movies.end()) return {};
	return kind == 0 ? it->second.first : it->second.second;
}
const Rm8Drama *rm8StoryDrama(int id){ auto it = g_drama.find(id); return it == g_drama.end() ? nullptr : &it->second; }
const std::vector<std::pair<int, int>> &rm8StoryMissions(int role, int level)
{
	static const std::vector<std::pair<int, int>> none;
	auto it = g_missions.find({role, level});
	return it == g_missions.end() ? none : it->second;
}

// ======================================================================== menu side
int rm8StoryRunLevel(){ return rm8Box().stage; }
int rm8StorySetupLevel(){ return g_level; }

// [V] Game_UpdateRecordStats @0x4abe20 (named so by statistics: it keeps the maximum of the run level and raises the profile stage of the hero)
void rm8StoryUpdateRecordStats(int level)
{
	Rm8Box &b = rm8Box();
	if(b.stage < level){ b.stage = level; rm8BoxSave(); }
	if(g_hero >= 1 && g_hero <= 16) rm8ProfileSetMaxLevel(g_hero, level);	// [V] Event_SetMaxLevel(role of seat 0, level) @0x4aa250 (only raises, saves gamedate)
}

void rm8StoryNewRole(int role)
{
	g_hero = role; g_level = 0; g_stage = 0; g_confirmed = false;
	rm8BoxClear();		// [V] Game_ClearBinaryData @0x4abcc0: the only caller is the role click of chsRoleMenu @0x5065f0
	rm8Log("story: new hero %d, box cleared", role);
}

int rm8StoryCandidates(int role, int out[4])
{
	if(role < 1 || role > 16) return 0;	// [A] no hero: nothing to look up (the empty-layer walk below would raise the run level to 3 for nothing, bug 49)
	g_hero = role;
	int L = std::min(rm8Box().stage, 3);	// [V] 0x503510: L = min(run.level, 3)
	for(int guard = 0; guard < 6; guard++){
		int n = 0;
		for(const Rm8PassEntry &e : rm8StoryPass(role)) if(e.level == L && n < 4) out[n++] = e.stage;
		if(n > 0) return n;
		if(L >= 3) return 0;
		L++;
		rm8StoryUpdateRecordStats(L);	// [V] an empty layer (忍太郎 has no layer 2) moves on and records it
	}
	return 0;
}

bool rm8StoryCanGoBack(){ return rm8Box().stage <= 0; }	// [V] OnBack @0x5042a0 returns 0 when run.level > 0
void rm8StoryMenuBack(){ rm8BoxMergePending(); }	// [V] ShowGameMenu @0x504310 -> u_Obj_ApplyPendingChanges @0x4ac020

bool rm8StoryOnConfirm(Rm8Setup &st)
{
	if(st.map < 1 || st.seats.empty()) return false;	// [V] 0x503e90: setup+4 must be > 0
	st.seats.resize(1);					// [V] the seat array is cut down to the first seat
	int role = st.seats[0].charId;
	g_hero = role; g_stage = st.map; g_confirmed = true;
	const Rm8PassEntry *e = rm8StoryFindPass(role, st.map);
	if(!e){ rm8Log("story: hero %d has no pass entry for map %d", role, st.map); return true; }
	g_level = e->level;					// [V] setup+8 = entry+4
	for(int k = 0; k < 3; k++){
		int r = e->opp[k];
		if(r < 0){ Rm8Seat s; s.charId = rand() % 15 + 1; s.outfit = 0; s.computer = true; st.seats.push_back(s); break; }	// [V] rand() % 15 + 1, and the loop ends
		if(r > 0 && r < 17){ Rm8Seat s; s.charId = r; s.outfit = 0; s.computer = true; st.seats.push_back(s); }
	}
	rm8Log("story: hero %d map %d layer %d, %d opponents", role, st.map, g_level, (int)st.seats.size() - 1);
	return true;
}

// ======================================================================== board side
void rm8StoryBegin(const Rm8Setup &st)
{
	resetSeatStory();
	g_onBoard = true; g_story = st.story && g_confirmed; g_action = RM8STORY_NONE; g_win = WinScene(); g_rescued = false;
	g_testT = 0; g_testWon = g_testLost = false;
	rm8PresentSetStory(g_story);	// [V] the role-meeting dialogues only exist in the story mode
	g_stage = st.map;
	g_hero = st.seats.empty() ? 0 : st.seats[0].charId;
	if(!g_story){ rm8NpcStory(nullptr); return; }
	const Rm8PassEntry *e = rm8StoryFindPass(g_hero, g_stage);
	if(e) g_level = e->level;
	int pic[2] = {0, 0};
	if(e) for(int k = 0; k < 2; k++) if(e->startPic[k] > 0 && !rm8PuzzleHas(g_hero, e->startPic[k])) pic[k] = e->startPic[k];	// [V] Event_ProcessLanding @0x49db20: owned pieces are not placed
	rm8NpcStory(pic);
	rm8Log("story: board start, hero %d map %d layer %d, puzzle pieces on the map %d %d", g_hero, g_stage, g_level, pic[0], pic[1]);
}

bool rm8StoryActive(){ return g_onBoard && g_story; }
bool rm8StoryConfirmed(){ return g_confirmed; }
int rm8StoryHero(){ return g_hero; }
bool rm8StoryOnBoard(){ return g_onBoard; }

void rm8StoryApplyStart()
{
	if(!g_onBoard || !g_story) return;
	Rm8Box &b = rm8Box();
	int nCarried = 0;
	for(int i = 0; i < 8; i++) if(b.cards[1][i] > 0){ rm8CardAdd(0, b.cards[1][i]); nCarried++; }	// [V] 0x4962a0: the 8 carried cards -> Role_AddCardToDeck
	int cc = b.cash[1], ct = b.points[1];
	if(const Rm8PassEntry *e = rm8StoryFindPass(rm8TurnCharId(0), g_stage)){
		for(int k = 0; k < 2; k++) if(e->startCard[k] != 0) rm8CardAdd(0, e->startCard[k]);	// [V] start_card[0..1] != 0
		rm8GameSetMoney(0, cc + e->startCash, e->startFund);					// [V] Role_SetMoney(0, carried + start_cash), (1, start_fund)
		rm8GameAddPoints(0, std::min(ct + e->startTicket, 9999) - (int)rm8GamePoints(0));	// [V] Role_SetTicketAmount(carried + start_ticket) [M] capped like Role_InitState
		rm8Log("story: start values cash %d (carried %d), deposit %d, tickets %d (carried %d), start cards %d %d + %d carried cards, deck %d", cc + e->startCash, cc, e->startFund, ct + e->startTicket, ct, e->startCard[0], e->startCard[1], nCarried, rm8CardCount(0));
	}
	if(const char *tc = getenv("RM8_STORY_CASH")){ int c = 0, d = 0; if(sscanf(tc, "%d,%d", &c, &d) >= 1) rm8GameSetMoney(0, c, d); }	// [A] test hook "<cash>[,<deposit>]": the hero's money after the start values (the day > 9 events of 大老千 need 250000 / 300000)
	b.cash[1] = b.points[1] = 0; memset(b.cards[1], 0, sizeof b.cards[1]);			// [V] u_Obj_ResetSlotArrays @0x4abfd0 whether or not the entry was found
	rm8BoxSave();
}

static void queueText(int seat, int &pos, const std::string &t){ rm8TurnEnqueueAt(seat, pos++, 0x1B, rm8GamePushText(t), 0); }

// [V] CPlayerTag__OnTreasurechest @0x48f9d0 (0x7d): pass entry of (this role, setup+4): reward pieces (0x58 at the front), reward cash / tickets into the
// box with their texts (str 7251 / 7252), reward cards into the box (str 7253); then 0x7a. [A] the card picture of the text entry is not shown.
static void treasureChest(int seat)
{
	if(g_story){
		int pos = 0;
		if(const Rm8PassEntry *e = rm8StoryFindPass(rm8TurnCharId(seat), g_stage)){
			for(int k = 0; k < 2; k++)
				if(e->rewardPic[k] > 0 && !rm8PuzzleHas(rm8TurnCharId(seat), e->rewardPic[k])) rm8TurnEnqueueAt(seat, pos++, 0x58, e->rewardPic[k], 0);
			int cards[4] = {0, 0, 0, 0}, nc = 0;
			Rm8Box &b = rm8Box();
			if(e->rewardCash > 0){ b.cash[0] += e->rewardCash; queueText(seat, pos, rm8StrFmt(7251, e->rewardCash)); }
			if(e->rewardTicket > 0){ b.points[0] += e->rewardTicket; queueText(seat, pos, rm8StrFmt(7252, e->rewardTicket)); }
			for(int k = 0; k < 4; k++) if(e->rewardCard[k] > 0){
				int id = e->rewardCard[k];
				bool stored = rm8BoxStoreCard(id);	// [V] CardArray_FindSlotAndAdd: a full box loses the card, the text still appears
				(void)stored; cards[nc++] = id;
				queueText(seat, pos, rm8StrFmt(7253, rm8CardName(id)));
			}
			rm8BoxSave();
			rm8Log("story: reward cash %d tickets %d cards %d,%d,%d,%d pieces %d,%d", e->rewardCash, e->rewardTicket, e->rewardCard[0], e->rewardCard[1], e->rewardCard[2], e->rewardCard[3], e->rewardPic[0], e->rewardPic[1]);
			(void)cards; (void)nc;
		}
	}
	rm8TurnEnqueue(seat, 0x7a, 0, 0);
}

// [V] Game_OnStageSelect @0x4a1390 (0x7a): game state 4; with the pass entry of (seat 0's role, setup+4): newLevel = level + 1, and when
// run.level < newLevel: Game_UpdateRecordStats(newLevel) and, above 3, game state 5
static void stageSelect()
{
	Rm8StoryAction act = RM8STORY_MAP_MENU;	// [V] state 4: free game -> SwitchToGameScene(3) (map menu), story -> (4) (map menu, announcing)
	if(g_story){
		if(const Rm8PassEntry *e = rm8StoryFindPass(rm8TurnCharId(0), g_stage)){
			int nl = e->level + 1;
			if(rm8Box().stage < nl){
				rm8StoryUpdateRecordStats(nl);
				if(nl > 3) act = RM8STORY_ENDING;
			}
		}
	}
	g_action = act;
	rm8Log("story: stage select -> %s (run level %d)", act == RM8STORY_ENDING ? "ending" : "map menu", rm8Box().stage);
}

// [V] Role_QueueNpcDialog @0x48c700: the first speaker (not the narrator 0x13) is the left one, the second the right one: 0x54(left, right); then a line each
// (left 0x55, right 0x56, a third speaker 0x56 with b = Id, text b = -1 otherwise); the narrator's line ends the list; 0x1C closes the box; a narrator
// line follows as 0x65(Role_GetWaitFrames(3) = 30 frames) + a text box. Returns the next queue position.
int rm8StoryQueueDrama(int seat, int id, int pos)
{
	const Rm8Drama *d = rm8StoryDrama(id);
	if(!d) return pos;
	rm8Log("story: drama %d (%d lines) queued for seat %d at position %d", id, (int)d->lines.size(), seat, pos);
	int left = -1, right = -1; std::string narration; bool hasNarration = false;
	for(auto &l : d->lines){
		if(l.first == 0x13) continue;
		if(left < 0) left = l.first;
		else if(left != l.first && right < 0) right = l.first;
	}
	if(left >= 0) rm8TurnEnqueueAt(seat, pos++, 0x54, left, right);
	for(auto &l : d->lines){
		if(l.first == 0x13){ narration = l.second; hasNarration = true; break; }
		int t = rm8GamePushText(l.second);
		if(l.first == left) rm8TurnEnqueueAt(seat, pos++, 0x55, t, -1);
		else if(l.first == right) rm8TurnEnqueueAt(seat, pos++, 0x56, t, -1);
		else rm8TurnEnqueueAt(seat, pos++, 0x56, t, l.first);
	}
	if(left >= 0) rm8TurnEnqueueAt(seat, pos++, 0x1C, 0, 0);
	if(hasNarration){ rm8TurnEnqueueAt(seat, pos++, 0x65, 30, 0); rm8TurnEnqueueAt(seat, pos++, 0x1B, rm8GamePushText(narration), 0); }
	return pos;
}

bool rm8StoryMessage(int me, int cmd, int a, int b)
{
	if(!g_onBoard) return false;	// [A] the viewer / demo runs (no menus, no rm8StoryBegin) keep game.cpp's old handling of 0x74 / 0x75
	switch(cmd){
	case 0x74:	// [V] CPlayerTag__AskContinueNextRound @0x48f8e0
		if(!g_story){ g_action = RM8STORY_MAP_MENU; return true; }	// free game: game state 3
		// [V] 金泡菜 (11) at layer 0 who still lacks piece 3 gets it first (0x58(3))
		if(rm8TurnCharId(me) == 11 && g_level == 0 && !rm8PuzzleHas(11, 3)) rm8TurnEnqueue(me, 0x58, 3, 0);
		rm8TurnEnqueue(me, 1, rm8GamePushText(rm8Str(93)), 0x80);	// str93 "<str#93>？" okcancel
		rm8TurnEnqueue(me, 0x1C, 0, 0);
		rm8TurnEnqueue(me, 0x76, 0, (int)0x80000000);		// the answer is written into b
		return true;
	case 0x75:	// [V] 0x75(a, b): only b != 0 runs CPlayerTag__OnParkCleanup @0x48ff70
		if(b != 0){
			rm8TurnClearQueue(me);
			rm8TurnEnqueue(me, 0x7e, 0, 0); rm8TurnEnqueue(me, 0x7d, 0, 0);
			if(g_story && rm8TurnCharId(me) == 4 && g_level == 1){	// [V] 忍太郎 at layer 1: 0x7b, 0x2c(0x8f6), drama 9 at position 4
				rm8TurnEnqueue(me, 0x7b, 0, 0); rm8TurnEnqueue(me, 0x2c, 0x8f6, 0);
				rm8StoryQueueDrama(me, 9, 4);
			}
		}
		return true;
	case 0x76:	// [V] 0 -> Game__ExitMainLoop @0x490010 (state 3); else CPlayerTag__OnTurnEnd @0x490030: speak 0x1B, 0x77
		if(b == 0){ g_action = g_story ? RM8STORY_ROLE_MENU : RM8STORY_MAP_MENU; }
		else { rm8RoleSay(me, 0x1B); rm8TurnEnqueue(me, 0x77, 0, 0); }
		return true;
	case 0x77: g_action = RM8STORY_RESTART; return true;	// [V] game state 2: Game_Cleanup, Game_LoadAllConfigTables, Game_InitGame
	case 0x7a: stageSelect(); return true;
	case 0x7b: case 0x7c: return true;	// [M] Role_ClearFlag1C8 / SetFlag1C8: a flag of the role that no ported rule reads
	case 0x7d: treasureChest(me); return true;
	case 0x7e: {	// [V] CPlayerTag__OnMovieIntro @0x492e40
		float p[3];
		if(!rm8RolesGetPos(me, p)){ return true; }
		rm8TurnFocus(p);
		g_win.fx = rm8FxPlayAtRole(me, 6000, 0);	// [V] Anim_CreateSprite(6000, role pos, action 0, count 1, no sample)
		rm8MusicPlay(103, false);			// [V] CDigitalAudio_StopMusic(0) + CDigitalAudio_PlayBGMFaded(0x67)
		g_win.on = true; g_win.seat = me; g_win.t = 0; g_win.dur = 14.0f * waitScale();	// [V] Anim_ApplyDifficultyScale(14.0)
		rm8TurnSetState(me, 9);			// [A] the original's state 0xD (nothing dispatches until the timer ends)
		return true; }
	}
	return false;
}

static void testHooks(float dt)
{
	static const char *w = getenv("RM8_STORY_WIN"), *l = getenv("RM8_STORY_LOSE"), *ans = getenv("RM8_STORY_ANSWER");
	if(!g_onBoard || rm8TurnRoleCount() < 1) return;
	g_testT += dt;
	if(w && !g_testWon && g_testT >= atof(w)){	// the survivor's queue is cleared and holds 0x75(0, 1) [V] Role_CheckBankruptcy @0x48ee10 (ECX of the 0x75 call = the surviving human)
		g_testWon = true;
		rm8TurnClearQueue(0); rm8TurnEnqueue(0, 0x75, 0, 1); rm8TurnSetState(0, 8);
		rm8Log("story test: forced win");
	}
	{	// RM8_STORY_LOSE=<s>[,<s2>...]: the hero's money is gone at those game seconds (the second one: 金泡菜's bankruptcy after the rescue)
		static std::vector<float> lt; static bool lp = false; static size_t lk = 0;
		if(l && !lp){ lp = true; for(const char *q = l; *q;){ lt.push_back((float)atof(q)); while(*q && *q != ',') q++; if(*q == ',') q++; } }
		if(l && lk < lt.size() && g_testT >= lt[lk]){ lk++; rm8GameSetMoney(0, 0, 0); rm8Log("story test: the hero loses all money"); }
	}
	{	// [A] RM8_STORY_NPCPLACE="12,13,14,15": from 3 s on, every 2 s the hero places the next obstacle (what py_Npc_Create asks CPlayerTag__OnDiceEvent first; the card scripts need a target pick)
		static const char *np = getenv("RM8_STORY_NPCPLACE"); static size_t k = 0; static float nt = 3.0f;
		if(np && g_testT >= nt && rm8TurnCurrent() == 0){
			std::vector<int> ids; for(const char *q = np; *q;){ ids.push_back(atoi(q)); while(*q && *q != ',') q++; if(*q == ',') q++; }
			if(k < ids.size()){ nt = g_testT + 2.0f; bool r = rm8StoryNpcPlaced(rm8TurnCurrent(), ids[k]); rm8Log("story test: place npc %d -> %s", ids[k], r ? "story event (not created)" : "normal"); k++; }
		}
	}
	{	// [A] RM8_STORY_HOSP=<s>[,<s>...]: at those game seconds the hero gets message 0x18(2) (the hospital, as the dog bite queues it)
		static const char *hp = getenv("RM8_STORY_HOSP"); static size_t k = 0; static std::vector<float> ts; static bool parsed = false;
		if(hp && !parsed){ parsed = true; for(const char *q = hp; *q;){ ts.push_back((float)atof(q)); while(*q && *q != ',') q++; if(*q == ',') q++; } }
		if(hp && k < ts.size() && g_testT >= ts[k]){ k++; rm8TurnEnqueue(0, 0x18, 2, 0); rm8Log("story test: the hero is sent to the hospital"); }
	}
	if(ans && rm8TurnState(0) == 7 && g_testT > 1){ rm8Log("story test: answering the dialog with %s", ans); rm8TurnModalResult(0, atoi(ans)); }
}

bool rm8StoryDebugStart(Rm8Setup &st)
{
	const char *e = getenv("RM8_STORY");
	if(!e || !*e || !g_loaded) return false;
	int role = 0, map = 0;
	sscanf(e, "%d,%d", &role, &map);
	if(role < 1 || role > 16) return false;
	st = Rm8Setup(); st.story = true;
	Rm8Seat s; s.charId = role; s.outfit = 0; s.computer = false; st.seats.push_back(s);
	rm8StoryNewRole(role);
	int c[4], n = rm8StoryCandidates(role, c);
	if(map < 1 && n > 0) map = c[0];
	st.map = map;
	return rm8StoryOnConfirm(st);
}

// [V] state 0xD of the role (Role_TickWaitTimer, then at state 8: CDigitalAudio_StopBGMWithFade + ManageBGMTransition(-5) = the board music resumes, MovieClip_List_Destroy
// releases the effect sprite) @0x481500 state switch; [A] the board playlist restarts
static void endWin()
{
	if(g_win.fx) rm8FxStop(g_win.fx);
	int seat = g_win.seat;
	g_win = WinScene();
	rm8MusicResumeBoard();
	rm8TurnSetState(seat, 8);
}
// [V] Role_StopMoveAndIdle @0x484790 also ends the movie-clip wait states 0xD / 0xE (docs/skip.md): a click skips the 14 s win scene
bool rm8StoryWinSkip(int seat)
{
	if(!g_win.on || g_win.seat != seat) return false;
	endWin();
	return true;
}

void rm8StoryUpdate(float dt)
{
	testHooks(dt);
	if(!g_win.on) return;
	g_win.t += dt;
	if(g_win.t < g_win.dur) return;
	endWin();
}

// [V] Role_CheckBankruptcy @0x48ee10, story mode and seat 0 only (+0x1a8 == 0), before the general case:
//   金泡菜 (11): the first time (+0x380 counter == 1) - text str91, drama 16 at position 1, cash set to 100000 (Bank_SetMaxMoney(100000, 0)), 0x47 after the
//   drama; the role is not bankrupt (returns 0). Later bankruptcies take the general path.
//   莎拉 (16): bankruptcy is a win - queue cleared, text str91, drama 19 (narrator) at position 1, then 0x75(0, 1); returns 1.
// Returns -1 = general case, 0 = rescued (not bankrupt), 1 = handled (bankrupt, nothing more to queue).
int rm8StoryBankruptSpecial(int seat)
{
	if(!g_onBoard || !g_story || seat != 0) return -1;
	int role = rm8TurnCharId(seat);
	if(role == 11){
		if(g_rescued) return -1;
		g_rescued = true;
		rm8TurnEnqueueAt(seat, 0, 0x1B, rm8GamePushText(rm8StrFmt(91, rm8TurnName(seat))), 0);
		int pos = rm8StoryQueueDrama(seat, 16, 1);
		rm8GameSetMoney(seat, 100000, rm8GameDeposit(seat));
		rm8TurnEnqueueAt(seat, pos, 0x47, 0, 0);
		rm8Log("story: 金泡菜 is rescued by his father (cash 100000)");
		return 0;
	}
	if(role == 16){
		rm8TurnClearQueue(seat);
		rm8TurnEnqueueAt(seat, 0, 0x1B, rm8GamePushText(rm8StrFmt(91, rm8TurnName(seat))), 0);
		int pos = rm8StoryQueueDrama(seat, 19, 1);
		rm8TurnEnqueueAt(seat, pos, 0x75, 0, 1);
		rm8Log("story: 莎拉's bankruptcy is a win");
		return 1;
	}
	return -1;
}


// ======================================================================== story rule exceptions (docs/story_mode.md 1.11 / 1.12 / 1.13)
// Only the hero (seat 0) and only in the story mode, like every one of the original's checks (Singleton +0 != 0, +0x1a8 == 0).
namespace {
struct SeatStory {
	int park = 0;		// +0x378 park visits (-1 = used)
	bool hospital = false;	// +0x5c0: has been in the hospital [H] (set by CPlayerTag__SendToHospital @0x48a570 -> this[0x170] = 1)
	bool steps18 = false;	// +0x5cc: a roll of 18 (CPlayerTag__BeginMoveSteps @0x484f90)
	bool usedCard[64] = {false};	// +0x4c0 + id: the card was used (Role_FinishCardUse @0x4896e0)
	bool debt = false;	// +0x374 (大老千): the day-10 collector was handled
	int meet = 0;		// +0x374 (錢夫人): meetings with <q#1> on map 5
	bool chest = false;	// +0x37c: a park chest was built (舞美拉)
	int firstHosp = 0;	// +0x384 莎拉: hospital / prison visits
};
std::vector<SeatStory> g_ss;
SeatStory &ss(int seat){ if((int)g_ss.size() <= seat) g_ss.resize(seat + 1); return g_ss[std::max(0, seat)]; }
bool g_dogPiece = false;
void resetSeatStory(){ g_ss.clear(); g_dogPiece = false; }
bool heroOnly(int seat){ return g_onBoard && g_story && seat == 0; }

int32_t netWorth(int seat)	// [N] [V] Role_CalculateNetCash(1) @0x490140: deposit + cash - loan + stock value + sum of the plot prices (Terra_CalcPrice(1))
{
	if(rm8TurnStatus(seat) == 7) return 0;
	int32_t v = rm8Add32(rm8Sub32(rm8Add32(rm8GameDeposit(seat), rm8GameCash(seat)), rm8EventLoan(seat)), rm8StockValue(seat));	// [N] int32_t; 32-bit wrap like the original's adds
	for(int t = 0, N = rm8GameTerraCount(); t < N; t++) if(rm8GameTerraOwner(t) == seat) v = rm8Add32(v, rm8GameTerraPrice(t));
	return v;
}
// [V] Role_CountOwnTerras @0x48c430: kind 0 residential, 1 any commercial, 2 / 3 / 4 institute / chain store / hotel, 9 all; level in [lo, hi]
int countOwn(int seat, int kind, int lo, int hi)
{
	int n = 0;
	for(int t = 0, N = rm8GameTerraCount(); t < N; t++){
		if(rm8GameTerraOwner(t) != seat) continue;
		int ty = rm8GameTerraType(t), b = rm8GameTerraBuild(t), lv = rm8GameTerraLevel(t);
		bool ok = kind == 9 || (kind == 0 && ty == 1) || (kind == 1 && ty > 1) || (kind >= 2 && kind <= 4 && ty == 2 && b == kind);
		if(ok && lv >= lo && lv <= hi) n++;
	}
	return n;
}
bool ownsWholeStreet(int seat)	// [V] CPlayerTag__CheckTownOwnerCondition @0x491930: some street whose every plot is the role's (and which has plots)
{
	std::map<int, std::pair<int, int>> streets;	// street -> (plots, owned)
	for(int t = 0, N = rm8GameTerraCount(); t < N; t++){
		int st = rm8GameTerraStreet(t); if(st <= 0) continue;
		auto &p = streets[st]; p.first++; if(rm8GameTerraOwner(t) == seat) p.second++;
	}
	for(auto &kv : streets) if(kv.second.first > 0 && kv.second.first == kv.second.second) return true;
	return false;
}
// [V] Role_CheckPuzzleCondition = CPlayerTag__OnStockMarketAccess @0x491a10 (conditions in docs 1.11)
bool condition(int seat, int cond)
{
	SeatStory &S = ss(seat);
	if(cond >= 100) return cond - 100 < 64 && S.usedCard[cond - 100];	// [V] role+0x330 + cond * 4 = role+0x4c0[cond - 100]
	switch(cond){
	case 1: return netWorth(seat) > 999999;
	case 2: return rm8GamePoints(seat) >= 500;
	case 3: return rm8StockValue(seat) > 200000;
	case 4: return rm8GameDeposit(seat) > 500000;
	case 5: return rm8StockValue(seat) > 500000;
	case 6: return ownsWholeStreet(seat);
	case 7: return countOwn(seat, 9, 5, 5) > 4;
	case 8: { for(int t = 0, N = rm8GameTerraCount(); t < N; t++) if(rm8GameTerraType(t) == 2 && rm8GameTerraOwner(t) != seat) return false; return true; }	// [V] @0x491a10: every type-2 plot is the role's (true also when there is none: the loop runs to the end)
	case 9: return countOwn(seat, 2, 5, 5) != 0;
	case 10: return countOwn(seat, 3, 5, 5) != 0;
	case 11: return countOwn(seat, 4, 5, 5) != 0;
	case 12: return S.hospital;
	case 13: return countOwn(seat, 0, 5, 5) > 4;
	case 14: return countOwn(seat, 1, 5, 5) > 0;
	case 15: return S.park > 0;
	case 16: for(int i = 0; i < 3; i++) if(rm8EventWonderOwnerRaw(i) == seat) return true; return false;	// [V] the miracle table @0x6403bc: any of the 3 entries is the role's (+0x1a8)
	case 17: return S.steps18;
	case 18: return countOwn(seat, 0, 5, 5) > 0;
	case 19: return countOwn(seat, 0, 5, 5) > 1;
	}
	return false;
}
}	// namespace

// [V] Role_CheckMissionsByCharacter @0x4835e0: the first listed (piece, condition) of (hero, layer) whose piece is not owned and whose condition holds queues 0x58(piece)
bool rm8StoryMissionCheck(int seat)
{
	if(!heroOnly(seat) || seat != 0) return false;
	int role = rm8TurnCharId(seat);
	if(rm8TurnStatus(seat) == 4) ss(seat).hospital = true;	// [A] polled: the original sets it when the role is sent to the hospital
	for(auto &pc : rm8StoryMissions(role, g_level)){
		if(rm8PuzzleHas(role, pc.first)) continue;
		if(!condition(seat, pc.second)) continue;
		rm8TurnEnqueue(seat, 0x58, pc.first, 0);
		rm8Log("story: mission piece %d (condition %d) for hero %d", pc.first, pc.second, role);
		return true;
	}
	return false;
}

void rm8StoryNoteSteps(int seat, int steps){ if(heroOnly(seat) && steps == 18) ss(seat).steps18 = true; }	// [V] CPlayerTag__BeginMoveSteps @0x484f90
void rm8StoryCardUsed(int seat, int cardId){ if(seat == 0 && g_onBoard && cardId > 0 && cardId < 64) ss(seat).usedCard[cardId] = true; }	// [V] Role_FinishCardUse @0x4896e0: this+0x4c0[id] = 1

// [V] Role_RunStoryScriptEvents @0x483f40 (turn start step 2)
bool rm8StoryRunScriptEvents(int seat)
{
	if(!heroOnly(seat)) return false;
	int day = rm8GameDay(), role = rm8TurnCharId(seat);
	int y, m, d, wd; rm8GameDate(y, m, d, wd);
	SeatStory &S = ss(seat);
	if(day == 1){
		if(g_level == 0){
			if(role == 1){	// 大老千: the loan is only recorded (Bank_TrySetLoan @0x47ea00), cash and deposit become 50000 (Bank_SetMaxMoney @0x47e800), drama 1 at position 1, 0x47
				rm8EventSetLoan(seat, 150000);
				rm8GameSetMoney(seat, 50000, 50000);
				rm8Log("story: 大老千 starts in debt: loan 150000, cash / deposit 50000");
				rm8StoryQueueDrama(seat, 1, 1);
				rm8TurnEnqueue(seat, 0x47, 0, 0);
				return true;
			}
			if(role == 6){ rm8Log("story: 孫小美 day-1 hint (str 7150 with the p_18_01 portrait)"); rm8TurnEnqueue(seat, 0x1B, rm8GamePushTextPicFile(rm8Str(7150), "prt\\npc\\p_18_01.tga"), 0); return true; }	// [V] @0x483f40: Role_QueueTextMsg(str 7150, picture name npc\p_18_01, -1): the entry carries a picture name -> gtinfo, picture id 2 by file name
		}
	}else if(day > 9){
		if(role == 1 && !S.debt){	// 大老千 at layer 1 / 2: the collector asks for the interest once, when cash + deposit reach 250000 / 300000
			int lim = g_level == 1 ? 250000 : g_level == 2 ? 300000 : 0;
			if(lim){
				if(rm8GameCash(seat) + rm8GameDeposit(seat) >= lim){ rm8StoryQueueDrama(seat, 2, 0); rm8TurnEnqueue(seat, 0x16, 15000, 0); }
				S.debt = true;
				return true;
			}
		}else if(g_level == 0 && wd == 3){	// the unlock of 星期三: 阿土仔 (5) -> 烏咪 (10) -> 金貝貝 (8), each on a Wednesday of layer 0
			int k = role == 5 ? 0 : role == 10 ? 1 : role == 8 ? 2 : -1;
			if(k >= 0 && rm8ProfileSetFlag3(k)){ rm8StoryQueueDrama(seat, 0x1d + (k == 0 ? 0 : k == 1 ? 1 : 2), 0); return true; }	// drama 0x1d / 0x1e / 0x1f
		}
	}
	if(g_level == 0 && role == 7 && S.chest){	// 舞美拉: every park chest counts towards the unlock of 伊格魯 (the 4th makes it available)
		int c = rm8ProfileIncrementCounter();
		if(c < 5){ S.chest = false; rm8StoryQueueDrama(seat, c + 0x18, 0); }
		return true;
	}
	return false;
}

void rm8StoryTurnStart(int seat)	// step 2 of the turn start: Role_RunStoryScriptEvents, then Role_CheckMissionsByCharacter
{
	if(getenv("RM8_DEBUG_STORY")) rm8Log("story: turn start seat %d day %d onBoard %d story %d level %d char %d", seat, rm8GameDay(), (int)g_onBoard, (int)g_story, g_level, rm8TurnCharId(seat));
	if(!heroOnly(seat)) return;
	rm8StoryRunScriptEvents(seat);
	rm8StoryMissionCheck(seat);
	static const bool hit = getenv("RM8_STORY_HIT") != nullptr;	// [A] test hook: at the end of the hero's turn start the monster message 0x31 hits plot 0 as the news would (counts 舞美拉's chests over several days)
	if(hit && !monsterHit(seat, 0)) rm8StoryMonsterBuilt();
}

// [V] CPlayerTag__OnParkEvent @0x48d310 (misnamed: it is the handler of message 0x31 "the monster hits plot `terra`", enqueued by News_EventMonsterAttack @0x4b7910 and by the
// monster card's Terra.Dragon; str 573 / 574 say the monster card is refused). `seat` = the role whose queue holds the message (ECX). Its counter +0x378 counts the hits it took
// (-1 = used up); the story branches switch on the role of seat 0 (the hero). param_1+0x4c is the owner seat of the plot. Returns true when a story branch replaced the monster
// (the original returns before u_NPC_Chest_Construct: no monster appears and the buildings stay); else the caller builds the monster and calls rm8StoryMonsterBuilt.
static bool monsterHit(int seat, int terra)
{
	if(!g_onBoard || !g_story) return false;
	SeatStory &V = ss(seat);
	if(V.park > -1) V.park++;
	int hero = rm8TurnCharId(0), owner = rm8GameTerraOwner(terra);
	int vis = V.park;
	auto fin = [&](int pos){ rm8TurnEnqueueAt(seat, pos, 0x1E, 0, 0); };	// [V] 0x1e = focus the camera on the role
	auto fx = [&](int art){ rm8TurnEnqueueAt(seat, 0, 0x52, art, terra); };	// [V] Role_EnqueueMsgAt(0, 0x52, art, the plot): the effect GS-1 / GS-2 (arts 0x8df / 0x9c5) on the plot, before the dialogue
	switch(hero){
	case 3:
		if(owner == 0){	// the plot belongs to seat 0 (the hero 錢夫人)
			if(g_level < 3){ fx(0x8df); int p = rm8StoryQueueDrama(seat, 5, 1); fin(p); return true; }
			if(V.park > -1){ fx(0x8df); int p = rm8StoryQueueDrama(seat, 8, 1); fin(p); V.park = -1; return true; }
		}
		break;
	case 6: if(vis == 1 && g_level == 1){ fx(0x9c5); int p = rm8StoryQueueDrama(seat, 10, 1); fin(p); return true; } break;
	case 7: if(vis == 1 && g_level == 3){ int p = rm8StoryQueueDrama(seat, 11, 0); fin(p); } break;	// no return: the monster follows
	case 10:
		if(vis == 1 && g_level == 0){ int p = rm8StoryQueueDrama(seat, 12, 0); fin(p); }
		else if(owner == 0){ rm8TurnEnqueueAt(seat, 0, 0x1B, rm8GamePushText(rm8Str(573)), 0); rm8TurnEnqueueAt(seat, 1, 0x1E, 0, 0); return true; }	// str 0x8f4 / 4: the monster card on the hero's own plot is refused
		break;
	case 11:
		if(vis == 1 && g_level == 1){	// 金泡菜: the monster story ends the stage: drama 13, 0x7b, 0x2c(0x9c5), 0x7c, drama 14, 0x75(0, 1)
			int p = rm8StoryQueueDrama(seat, 13, 0);
			rm8TurnEnqueueAt(seat, p++, 0x7B, 0, 0); rm8TurnEnqueueAt(seat, p++, 0x2C, 0x9c5, 0); rm8TurnEnqueueAt(seat, p++, 0x7C, 0, 0);
			p = rm8StoryQueueDrama(seat, 14, p);
			rm8TurnEnqueueAt(seat, p, 0x75, 0, 1);
			return true;
		}
		break;
	case 13:
		if(vis == 1 && g_level == 0){ int p = rm8StoryQueueDrama(seat, 15, 0); fin(p); return true; }
		rm8TurnEnqueueAt(seat, 0, 0x1B, rm8GamePushText(rm8Str(574)), 0); rm8TurnEnqueueAt(seat, 1, 0x1E, 0, 0);	// str 0x8f8 / 4: every other monster (card) is refused for 伊格魯
		return true;
	case 15:
		if(owner >= 0 && owner < rm8TurnRoleCount() && rm8TurnCharId(owner) == 3){ fx(0x8df); int p = rm8StoryQueueDrama(seat, 17, 1); fin(p); return true; }	// the plot of 錢夫人
		break;
	}
	return false;
}
bool rm8StoryMonsterHit(int seat, int terra)
{
	if(!g_onBoard || !g_story) return false;
	bool r = monsterHit(seat, terra);
	rm8Log("story: the monster hits plot %d (owner %d) on seat %d: hit %d, hero %d layer %d -> %s", terra, rm8GameTerraOwner(terra), seat, ss(seat).park, rm8TurnCharId(0), g_level, r ? "story branch, no monster" : "the monster appears");
	return r;
}
// [V] 0x48d310 tail: u_NPC_Chest_Construct succeeded (the monster is on the plot): the hero's +0x37c = 1 (舞美拉's counter of Role_RunStoryScriptEvents)
void rm8StoryMonsterBuilt(){ if(g_onBoard && g_story) ss(0).chest = true; }

// [V] Role_StartNpcDialogForRole @0x48c950: 錢夫人 (hero) meeting <q#1> on map 5: the first time drama 6, the second drama 7 and the stage is won (0x75); true = handled
bool rm8StoryMeetSpecial(int seat, int other)
{
	if(!heroOnly(seat) || rm8TurnCharId(seat) != 3 || rm8TurnCharId(other) != 9 || g_stage != 5) return false;
	SeatStory &S = ss(seat);
	S.meet++;
	if(S.meet == 1) rm8StoryQueueDrama(seat, 6, 0);
	else { rm8StoryQueueDrama(seat, 7, 0); rm8TurnEnqueue(seat, 0x75, 0, 1); }
	return true;
}

// [V] CPlayerTag__SendToHospital / SendToPrison @0x48a570 / 0x48a9c0: 莎拉's first visit asks "<str#101>？" (str 101) after drama 18; yes = 0x75 (the stage is won).
// Returns true when it queued the dialogue (the caller then skips its own speak 4)
bool rm8StoryHospitalPrison(int seat)
{
	if(!heroOnly(seat) || rm8TurnCharId(seat) != 16) return false;
	if(++ss(seat).firstHosp != 1) return false;
	int p = rm8StoryQueueDrama(seat, 18, 0);
	rm8TurnEnqueueAt(seat, p, 1, rm8GamePushText(rm8Str(101)), 0x80);
	rm8TurnEnqueueAt(seat, p + 1, 0x1C, 0, 0);
	rm8TurnEnqueueAt(seat, p + 2, 0x75, 0, (int)0x80000000);
	return true;
}

// [V] @0x48bd00 (0x69952) / @0x49caf0 (0x78119): story, layer 0 (setup+8 == 0), the victim is seat 0 playing 烏咪 (role 10) and does not own piece 2 yet: Role_EnqueueMsg(0x58, 2) on its queue
// (after the hospital message 0x18(2))
bool rm8StoryDogBite(int victim)
{
	if(!heroOnly(victim) || g_level != 0 || rm8TurnCharId(victim) != 10 || rm8PuzzleHas(10, 2)) return false;
	g_dogPiece = true;	// [A] the original's queue is [0x2c, 0x18, 0x58]; the port's hospital move clears the queue, so the piece is queued after it (rm8StoryDogBiteDone)
	return true;
}
// called by the hospital move (cards.cpp sendToHospital) after its queue clean-up: the 0x58(2) of the dog bite goes to the front of the queue
bool rm8StoryDogBiteDone(int seat)
{
	if(!g_dogPiece || !heroOnly(seat)) return false;
	g_dogPiece = false;
	rm8TurnEnqueueAt(seat, 0, 0x58, 2, 0);
	rm8Log("story: 烏咪 bitten by the dog: puzzle piece 2");
	return true;
}

// [V] Game_TriggerEventByDate @0x4a6780 (story part): after day 9 the hero's park visits < 1 and (role, layer) in {(6,1) (7,3) (10,0) (11,1) (13,0)} start the monster news (id 0x1e)
bool rm8StoryMonsterDue()
{
	if(!g_onBoard || !g_story || rm8GameDay() <= 9 || ss(0).park >= 1) return false;
	int r = rm8TurnCharId(0), l = g_level;
	return (r == 6 && l == 1) || (r == 7 && l == 3) || (r == 10 && l == 0) || (r == 11 && l == 1) || (r == 13 && l == 0);
}

// [V] CPlayerTag__OnDiceEvent @0x491270 (called by py_Npc_Create @0x500160 with the NPC id): the hero 孫小美 (seat 0) on a map of climate 1 placing the four kinds of obstacle
// (12 roadblock -> bit 0, 13 mine -> bit 2, 14 time bomb -> bit 3, 15 robot doll -> bit 1 of Event_SetFlag2) triggers drama 20 + bit; with all four done role 11 (金泡菜)
// is unlocked and drama 24 follows after a 30 frame wait. True = handled
bool rm8StoryNpcPlaced(int seat, int npcId)
{
	if(!heroOnly(seat) || rm8TurnCharId(seat) != 6) return false;
	auto cl = g_stageClime.find(g_stage);
	if(cl == g_stageClime.end() || cl->second != 1) return false;
	int bit = npcId == 12 ? 0 : npcId == 13 ? 2 : npcId == 14 ? 3 : npcId == 15 ? 1 : -1;
	if(bit < 0 || !rm8ProfileSetFlag2(bit)) return false;
	int p = rm8StoryQueueDrama(seat, bit + 0x14, 0);
	if(rm8ProfileRoleUnlocked(11)){ rm8TurnEnqueueAt(seat, p, 0x65, 30, 0); rm8StoryQueueDrama(seat, 0x18, p + 1); }
	rm8Log("story: obstacle %d placed on a climate-1 map: flag2 bit %d", npcId, bit);
	return true;
}

// ---- 詹姆斯 (15): the chain-store signs (docs/characters.md). Each map 01..11 has one article of art 0x45b (sign\a015, record [17] = an owner role 3 / 4 / 5 / 10);
// it is only on the board of the story hero of that role, while that layer's bit of the profile is not yet set; clicking it sets the bit (all 11 unlock role 15).
// [V] CPlayerTag__GetChainStoreIndex @0x491710: the bit of (hero role, layer): 錢夫人 layers 0..2 -> 7..9, 忍太郎 layer 0 -> 10, 阿土仔 layers 0..3 -> 0..3, 烏咪 layers 0..2 -> 4..6; else -1
static int chainStoreIndex(int role)
{
	if(!g_onBoard || !g_story) return -1;
	switch(role){
	case 3: return g_level < 3 ? g_level + 7 : -1;
	case 4: return g_level == 0 ? 10 : -1;
	case 5: return g_level;
	case 10: return g_level < 3 ? g_level + 4 : -1;
	}
	return -1;
}

// [V] Game_ActivateCharacterBuilding @0x4a37d0 (Game_InitGame @0x4a1090 and Save_LoadRecord @0x4a7ce7, after the roles exist): the article 0x45b is switched on (CArticleMgr vf38
// @0x43f9d0 = its +0xC4 flag) when the setup is a story, the first role's Id equals the article's owner, the index is 0..10 and Event_CheckBitFlag (bit not set yet); else off
void rm8StoryActivateBuildings(Rm8Scene &s)
{
	std::vector<std::pair<rw::Clump*, int>> owned(s.artOwner.begin(), s.artOwner.end());
	for(auto &o : owned){
		int idx = chainStoreIndex(g_hero);
		bool on = g_onBoard && g_story && g_hero == o.second && idx >= 0 && idx < 11 && rm8ProfileBitFree(idx);
		rm8SceneSetArticleActive(s, o.first, on);
		rm8Log("story: chain-store sign (owner %d) %s", o.second, on ? "on the board" : "off");
	}
}

// [V] Game_OnTileClickWithCard @0x4a4a10 (picked kind 5 = an article, story only, current role = seat 0) -> CPlayerTag__OnChainStoreEvent @0x491630: the role must be waiting (state 6),
// the index 0..10, Event_SetBitFlag newly sets it (all 11 set: Event_SetTerrainData(15, 1)), drama 0x20 + index at the front of the queue; indexes 0..3 (阿土仔) also give two cards
// (0x04, -1, 1 twice); then 100 (the roll again) and state 8. true = the sign was used up (the caller switches the article off); a refusal leaves everything as it is
bool rm8StoryChainStoreClick()
{
	if(!g_onBoard || !g_story || rm8TurnCurrent() != 0 || rm8TurnState(0) != 6){
		if(getenv("RM8_CHAIN_TAP")) rm8Log("story: chain-store click refused (board %d story %d current %d state %d)", g_onBoard, g_story, rm8TurnCurrent(), rm8TurnState(0));
		return false;
	}
	int idx = chainStoreIndex(rm8TurnCharId(0));
	if(idx < 0 || idx > 10 || !rm8ProfileSetBit(idx)) return false;
	rm8StoryQueueDrama(0, idx + 0x20, 0);
	if(idx < 4){ rm8TurnEnqueue(0, 4, -1, 1); rm8TurnEnqueue(0, 4, -1, 1); }
	rm8TurnEnqueue(0, 100, 0, 0);
	rm8TurnSetState(0, 8);
	rm8Log("story: chain-store sign of bit %d used (hero %d layer %d)%s", idx, rm8TurnCharId(0), g_level, rm8ProfileRoleUnlocked(15) ? ", role 15 unlocked" : "");
	return true;
}

Rm8StoryAction rm8StoryPeekAction(){ return g_action; }
Rm8StoryAction rm8StoryTakeAction(){ Rm8StoryAction a = g_action; g_action = RM8STORY_NONE; return a; }

bool rm8StoryDispatch()
{
	if(g_action == RM8STORY_NONE) return false;
	Rm8StoryAction a = rm8StoryTakeAction();
	Rm8StoryHost &h = rm8StoryHost();
	g_win = WinScene();
	bool wasStory = g_story;
	if(a != RM8STORY_RESTART) g_onBoard = false;	// the board is unloaded by the host
	rm8Log("story: action %d", (int)a);
	static const bool chain = getenv("RM8_STORY_CHAIN") != nullptr;
	if(a == RM8STORY_RESTART && h.restart) h.restart();
	else if(a == RM8STORY_ROLE_MENU && h.roleMenu) h.roleMenu();
	else if(a == RM8STORY_MAP_MENU && chain && g_story && h.start){	// the host plays the menus: next layer, first candidate
		Rm8Setup st; st.story = true; Rm8Seat s; s.charId = g_hero; s.computer = false; st.seats.push_back(s);
		int c[4], n = rm8StoryCandidates(g_hero, c);
		if(n > 0){ st.map = c[0]; rm8StoryOnConfirm(st); rm8Log("story chain: layer %d, map %d", g_level, st.map); g_testT = 0; g_testWon = g_testLost = false; h.start(st); }
	}
	else if(a == RM8STORY_MAP_MENU && h.mapMenu) h.mapMenu(wasStory);
	else if(a == RM8STORY_ENDING){
		// [V] Game_OnUpdate state 5 @0x4a17b0: Event_SetTerrainData(16, 1) (莎拉), 15 詹姆斯 also unlocks 12 (酷娃), then CApp_PlayMovieRange(role - 1, 1)
		rm8ProfileUnlockRole(16);
		if(g_hero == 15) rm8ProfileUnlockRole(12);
		if(h.ending) h.ending();
	}
	return true;
}

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_story.inc"
