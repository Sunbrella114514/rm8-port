// Minigame events 0x12..0x17 on the board (the CMiniGameHost task, docs/minigames/port.md), joined to the event framework.
// [V] Game_UseCard(0x12..0x17) -> CMiniGameHost_ctor @0x50dd20 (event task); vf16 @0x50ddd0 builds the player list (every role that is
//     not bankrupt, the current role first, type 2 human / 1 computer) and runs the DLL; vf1 waits for its end; MiniGameHost_Cleanup
//     @0x50e300 hands out the rewards. A computer landing (AI_OnLandEvent @0x4f79c0) uses MiniGame_ExecuteGame @0x4fcb10 instead, except
//     game 0x15 (pirate) while a human plays: then it falls through to the same task as a human.
// [N] N4 netplay (docs/netplay.md section 9.3): the task exists on every device, but the minigame itself runs only on the device of the acting seat's player
//     (full screen, local input, its own audio and its own random numbers; the other seats of the minigame are its own AI there). Every device's simulation
//     waits in the task until the result command RM8C_MINI_RESULT is applied at a frame; then every device hands out the same rewards through applyReward.
//     插海盗 (0x15) is played by everybody together, as in the original: the referee (Ref21) runs its rules in the lockstep from RM8C_MINI_ACT commands (the
//     human players' choices from their own devices, the computer seats' and late players' choices from the host), every device shows the same game.
#include "event_int.h"
#include "minigame.h"
#include "present.h"
#include "collect.h"
#include "audio.h"
#include "rules_plus.h"
#include "net.h"
#include <cstdlib>
#include <cstdio>
#include <algorithm>

namespace ev {

namespace {

// [V] the reward order of MiniGameHost_Cleanup @0x50e300 / MiniGame_ExecuteGame @0x4fcb10
void applyReward(int game, const Rm8MiniReward &r)
{
	if(r.seat < 0 || r.seat >= rm8TurnRoleCount()) return;
	rm8Log("events: minigame %d seat %d: tickets %+d, %d card(s), hospital %d", game, r.seat, r.ticketsDelta, (int)r.cards.size(), r.hospitalDays);
	if(r.ticketsDelta > 0) addTickets(r.seat, r.ticketsDelta);
	else if(r.ticketsDelta < 0) subTickets(r.seat, -r.ticketsDelta);
	if(r.textId > 0) queueText(r.seat, plainFmt(r.textId, rm8TurnName(r.seat), r.textArg));
	for(int idx : r.cards) rm8TurnEnqueue(r.seat, 4, idx, r.cardEmote);	// [V] Role_EnqueueMsg(4, card index, 0): the card of a minigame comes without the emote (0x50E9DD ..); game 21's card has 1 (0x50E7AA)
	if(r.hospitalDays > 0) rm8TurnEnqueue(r.seat, 0x18, r.hospitalDays, 0);
	if(r.reaction > 0) rm8RoleSay(r.seat, r.reaction);	// [V] FUN_00484890(n) = Role.Speak class n [H]
	if(r.bestScore >= 0 && (!rm8NetActive() || rm8NetOwnsSeat(r.seat))) rm8ProfileSetQuest(game - 0x11, r.bestScore);	// [V] Event_SetQuestData(event - 0x11, score) @0x4aa5d0; [N] netplay: the record (a local profile, not game state) only on the player's own device
}

// [N] N4: the result as the text of RM8C_MINI_RESULT: "<count>|<seat>,<score>,<flagA>,<rating>,<flagB>|..." = the GetResult structure of the DLL (0x50E300 reads
// nothing else). Every device derives the rewards (tickets, cards, hospital, text, reaction, record) from it with the same rm8MiniRewards.
std::string encodeResult(const Rm8MiniResult &r)
{
	std::string s = std::to_string(r.count);
	char b[96];
	for(int i = 0; i < r.count && i < 8; i++){ snprintf(b, sizeof b, "|%d,%d,%d,%d,%d", r.p[i].seat, r.p[i].score, r.p[i].flagA, r.p[i].rating, r.p[i].flagB); s += b; }
	return s;
}
// strict parse + check against the task's players (every device runs it on the same text: the outcome is the same everywhere; 64-bit strtoll on both platforms)
bool decodeResult(const std::string &s, int game, const std::vector<Rm8MiniPlayer> &pl, Rm8MiniResult &r)
{
	r = Rm8MiniResult(); r.game = game;
	const char *p = s.c_str(); char *e = nullptr;
	long long n = strtoll(p, &e, 10);
	if(e == p || n < 1 || n > 8 || n > (long long)std::max<size_t>(pl.size(), 2)) return false;
	p = e; r.count = (int)n;
	for(int i = 0; i < r.count; i++){
		if(*p != '|') return false;
		p++;
		long long v[5];
		for(int k = 0; k < 5; k++){
			if(k){ if(*p != ',') return false; p++; }
			v[k] = strtoll(p, &e, 10);
			if(e == p || v[k] < -100000 || v[k] > 100000) return false;
			p = e;
		}
		bool known = false; for(const Rm8MiniPlayer &q : pl) if(q.seat == (int)v[0]) known = true;
		bool filler = v[0] < 0 || v[0] >= rm8TurnRoleCount();	// a filler entry of the game (22 pads to 2 entries with seat -1): no reward (applyReward skips it)
		if(!known && !filler) return false;
		if(!filler) for(int j = 0; j < i; j++) if(r.p[j].seat == (int)v[0]) return false;
		Rm8MiniPlayerResult &q = r.p[i];
		q.seat = (int)v[0]; q.score = (int)std::max(-9999LL, std::min(9999LL, v[1])); q.flagA = (int)v[2]; q.rating = (int)std::max(0LL, std::min(9LL, v[3])); q.flagB = (int)v[4];
	}
	return *p == 0 && r.p[0].seat == pl[0].seat;	// the acting seat comes first (0x50E300 rewards p[0] for games 18..20 / 22 / 23)
}

struct MiniTask;
MiniTask *g_netMini = nullptr;	// [N] the netplay minigame task that waits for its result command

// [N] N4: the referee of 插海盗 (game 21) in a netplay game, where every device plays it together. It is the rules half of the DLL (LG21 Barrel_Stab @10001950,
// PickRandomUnusedHole @100010c0, StepAngle @100011b0, IsHoleHidden @100012a0, NextTurn @100043e0, GetResult @100031d0) on its own MSVC rand() stream, seeded
// with the task's seed (drawn from the rules stream at the landing frame: the same number on every device, = a seed the host hands out). It runs in the lockstep:
// every device applies the same actions (RM8C_MINI_ACT) at the same frames, so the pirate, every stab and the result are the same everywhere. The game on each
// device (mg_21.cpp) only shows its actions; the visual randomness there (the god's flights, voices) stays local.
struct Ref21 {
	unsigned st = 1;
	int r15(){ st = st * 214013u + 2531011u; return (int)((st >> 16) & 0x7fff); }
	int n = 0, defender = 0, trap = 0, cur = 0, turn = 0, remaining = 0, angle = 180, stabs = 0, hitAt = 0;
	bool clicked[12] = {}, pool[12] = {}, finished = false, never = false;
	bool win[4] = {}, lose[4] = {};
	std::vector<Rm8Mini21Act> acts;
	void init(unsigned seed, int count)
	{
		st = seed;
		n = std::max(1, std::min(4, count));	// as G21::init (the seat table holds four)
		defender = r15() % n;			// [V] the god's last pick (OnArrive @10003b10: rand() % count)
		trap = r15() % 12;			// [V] InitTrapHole @10001000
		if(const char *e = getenv("RM8_MG21_PIRATE")) defender = std::abs(atoi(e)) % n;	// test hooks (as mg_21.cpp)
		if(const char *e = getenv("RM8_MG21_TRAP")) trap = std::abs(atoi(e)) % 12;
		if(const char *e = getenv("RM8_MG21_HIT")) hitAt = atoi(e);
		never = getenv("RM8_MG21_NEVER") != nullptr;
		remaining = n == 2 ? 3 : n == 3 ? 6 : n == 4 ? 9 : -1;	// [V] 100052a6
		cur = defender == 0 ? (n > 1 ? 1 : 0) : 0;		// [V] @100023c3
	}
	bool hidden(int hole) const { int a = angle / 90 * 3; for(int j = 0; j < 6; j++) if((a + j) % 12 == hole) return false; return true; }
	void step(bool left){ if(angle == 0){ angle = left ? 90 : 270; return; } if(angle != 270){ angle += left ? 90 : -90; return; } angle = left ? 0 : 180; }
	int pickUnused(){ int r = r15() % 12, h = r; if(clicked[r]){ for(int i = 0; i < 12; i++){ h = (h + 1) % 12; if(!clicked[h]) return h; } if(h == r) h = -1; } return h; }
	Rm8Mini21Act rot(int dir)
	{
		step(dir != 0);
		Rm8Mini21Act a; a.turn = turn; a.kind = RM8M21_ROT; a.dir = dir != 0; a.angle = angle;
		acts.push_back(a);
		return a;
	}
	Rm8Mini21Act stab(int kind, int hole)
	{
		Rm8Mini21Act a; a.turn = turn; a.angle = angle;
		if(kind == RM8M21_STAB && (hole < 0 || hole > 11 || clicked[hole] || hidden(hole))) kind = RM8M21_AUTO;	// not a valid choice (cannot come from the game): the computer's
		a.kind = kind;
		if(kind == RM8M21_AUTO){	// [V] the computer's turn (@10002508 / the time-out @1000259a): a random unused hole, the barrel turns one random way until it shows
			hole = pickUnused();
			if(hole < 0) for(hole = 0; hole < 11 && clicked[hole]; hole++){}
			if(hidden(hole)){ a.dir = r15() % 2; for(int g = 0; g < 4 && hidden(hole); g++) step(a.dir != 0); }
		}
		a.hole = hole; clicked[hole] = true;
		// [V] Barrel_Stab @10001950: the outcome does not depend on the hole: a random pool hole (the next unused one from rand() % 12) is the trap or not
		int k = r15() % 12;
		stabs++;
		if(hitAt > 0 && stabs == hitAt && !pool[trap]) k = trap;
		else if(never){ for(int g = 0; g < 12 && (k == trap || pool[k]); g++) k = (k + 1) % 12; }
		if(pool[k]){
			int i = 0;
			do{
				bool done = false;
				for(int s = 0; s < 6; s++){ k = (k + 1) % 12; if(!pool[k]){ done = true; break; } }
				if(done) break;
				i += 6;
			}while(i < 12);
		}
		pool[k] = true;
		if(k == trap){ a.outcome = 1; finished = true; win[cur] = true; lose[defender] = true; }	// @10001f50
		else{
			remaining--;
			if(remaining <= 0){ finished = true; for(int i = 0; i < n; i++) if(i != defender) lose[i] = true; win[defender] = true; }	// @10001f00
		}
		acts.push_back(a);
		if(!finished){ turn++; cur = (cur + 1) % n; if(cur == defender) cur = (cur + 1) % n; }	// NextTurn @100043e0
		return a;
	}
	void result(Rm8MiniResult &o, const std::vector<Rm8MiniPlayer> &pl) const	// GetResult @100031d0 (the same code as mg_21.cpp)
	{
		o = Rm8MiniResult(); o.game = 21; o.count = n;
		bool first = stabs == 1;	// IsFirstStab @10001310: one pool entry used
		for(int i = 0; i < n; i++){
			Rm8MiniPlayerResult &r = o.p[i];
			r.seat = i < (int)pl.size() ? pl[i].seat : -1;
			if(win[i]){
				if(i == defender){ o.p[defender].score = n * 30 - 30; o.p[defender].rating = 2; }
				else{ r.score = 30; if(first) r.flagB = 1; o.p[defender].rating = 2; }
			}else if(lose[i]){ r.score = -30; if(i == defender){ r.flagA = 1; r.rating = 0; } }
			else r.score = 0;
		}
	}
};

struct MiniTask : Task {
	bool started = false, finished = false;
	float doneT = 0;
	// [N] N4 netplay
	int game = 0;
	bool net = false, local = false, sent = false, have = false;
	int resMode = 1; std::string resText;
	uint32_t waited = 0; unsigned seed = 0, serial = 0;
	std::vector<Rm8MiniPlayer> pl;
	// [N] N4 game 21 with everybody (group): the referee, the game shown here, the end handshake
	bool group = false, shows = false, startFailed = false, doneSent = false;
	Ref21 ref;
	uint32_t turnAt = 0, finishAt = 0;
	int autoSent = -1, dueTurn = -1;
	std::vector<int> done;
	MiniTask(int seat, int game_) : game(game_)
	{
		Rm8MiniPlayer me; me.seat = seat; me.human = rm8TurnHuman(seat); me.charId = rm8TurnCharId(seat); me.outfit = rm8TurnOutfit(seat); me.pic = rm8TurnPic(seat); me.name = rm8TurnName(seat);
		pl.push_back(me);
		for(int k = 0; k < rm8TurnRoleCount(); k++){
			if(k == seat || rm8TurnStatus(k) == 7) continue;
			Rm8MiniPlayer p; p.seat = k; p.human = rm8TurnHuman(k); p.charId = rm8TurnCharId(k); p.outfit = rm8TurnOutfit(k); p.pic = rm8TurnPic(k); p.name = rm8TurnName(k);
			pl.push_back(p);
		}
		if(rm8NetActive()){	// [N] N4
			net = true; serial = rm8NetFrame() + 1u;	// the task's id = its frame (the same on every device, also after a reload + replay; the host's decision bookkeeping, the actions, the test shots)
			group = game == 0x15;	// 插海盗: every human plays on his own device, the computer seats are decided by the host (the task only exists with a human in it)
			if(!group) for(size_t i = 1; i < pl.size(); i++) pl[i].human = false;	// a one-player game: only the acting seat's player plays (on his device); the rest is the game's AI there
			seed = (unsigned)rnd() + 1u;	// drawn on every device (the rules stream stays in step, as in single player); the game's own seed
			if(group){ ref.init(seed, (int)pl.size()); local = true; }
			else local = rm8NetOwnsSeat(seat);
			g_netMini = this;
			if(group){
				std::string who; for(int i = 0; i < ref.n; i++){ char b[48]; snprintf(b, sizeof b, "%s%d%s", i ? " " : "", pl[i].seat, pl[i].human ? "h" : "c"); who += b; }
				rm8Log("events: [N] minigame 21 of seat %d: played by every device together (seats %s), referee seed %u: pirate = player %d (seat %d), first turn player %d",
					seat, who.c_str(), seed, ref.defender, pl[ref.defender].seat, ref.cur);
			}else rm8Log("events: [N] minigame %d of seat %d (%d player(s)): played on %s, every device waits for the result", game, seat, (int)pl.size(), local ? "this device" : "the player's device");
			return;	// the local start comes in update() (once this device runs live: not while it replays to the host's frame)
		}
		rm8UiPushHidden();	// [V] Movie_HideMainUI (CMiniGameHost_vf16 @0x50ddd0): the board HUD and dialogs are hidden while the game runs
		started = rm8MiniStart(game, pl, (unsigned)rnd() + 1u);
		if(!started) rm8UiPopHidden();
		if(!started) rm8Log("events: minigame %d could not start (scenes/minigame data?)", game);
	}
	~MiniTask() override
	{
		if(g_netMini == this) g_netMini = nullptr;
		bool running = group ? shows : (started && !sent);
		if(net && running && rm8MiniActive()){ rm8MiniAbort(); rm8UiPopHidden(); rm8MusicResumeBoard(); }	// [N] the board goes under a running local minigame (session end, a resync)
		if(group) rm8Mini21NetClear();
	}
	bool update(float dt) override
	{
		if(net) return group ? groupUpdate(dt) : netUpdate(dt);
		if(!started) return true;
		if(!rm8MiniDone()) return false;
		if((doneT += dt) < 0.5f) return false;	// [A] the last frame stays 0.5 s like the standalone host
		Rm8MiniResult res;
		bool ok = rm8MiniFinish(res);
		rm8UiPopHidden();
		rm8MusicResumeBoard();	// [V] MiniGameHost_Cleanup @0x50e300: the minigame track stops, the board music returns
		if(ok) for(const Rm8MiniReward &r : rm8MiniRewards(res)) applyReward(ev, r);
		return true;
	}
	// [N] N4: every device runs this at the same frames; only the player's device has a local minigame
	bool netUpdate(float dt)
	{
		waited++;
		if(have){	// the result command was applied at this frame (net.cpp): the same rewards on every device, the task ends now
			if(started && !sent && rm8MiniActive()){	// the host decided while this device still played (it timed out / the connection was lost): the local game is dropped
				rm8MiniAbort(); rm8UiPopHidden(); rm8MusicResumeBoard();
				rm8NetUiNotice("主机已判定这局小游戏的结果（等待超时或连接中断，改由电脑决定）", 6);
			}
			if(g_netMini == this) g_netMini = nullptr;
			Rm8MiniResult res;
			if(resMode == 0 && decodeResult(resText, game, pl, res)){
				rm8Log("events: [N] minigame %d of seat %d: the player's result %s after %u frames", game, seat, resText.c_str(), waited);
				for(const Rm8MiniReward &r : rm8MiniRewards(res)) applyReward(ev, r);
			}else{
				rm8Log("events: [N] minigame %d of seat %d: the computer logic decides (%s) after %u frames", game, seat, resMode == 0 ? "invalid result" : "the player is away / took too long", waited);
				aiMiniGame(seat, ev, true, false);	// the computer's result for the human's seat (no rules_plus B6 low score: that is for the computer seats only)
			}
			return true;
		}
		if(!local || sent) return false;
		if(!started){
			if(!rm8NetMiniMayStart()) return false;	// still replaying to the host's frame / the host's decision may be on its way
			rm8UiPushHidden();
			started = rm8MiniStart(game, pl, seed);
			if(!started){ rm8UiPopHidden(); rm8Log("events: minigame %d could not start (scenes/minigame data?): the computer logic decides", game); sent = true; rm8NetInput(RM8C_MINI_RESULT, game, 1); }
			return false;
		}
		if(!rm8MiniDone()) return false;
		if((doneT += dt) < 0.5f) return false;	// [A] as in single player
		Rm8MiniResult res;
		bool ok = rm8MiniFinish(res);
		rm8UiPopHidden();
		rm8MusicResumeBoard();
		sent = true;
		std::string enc = ok ? encodeResult(res) : std::string();
		rm8Log("events: [N] minigame %d of seat %d finished here: result %s (sent, applied when it comes back at a frame)", game, seat, ok ? enc.c_str() : "none");
		rm8NetInput(RM8C_MINI_RESULT, game, ok ? 0 : 1, enc);
		return false;
	}
	// ---- [N] N4 game 21 with everybody ----------------------------------------------------------------------------------------------------------------
	void sendAct(int kind, int turn, int arg)
	{
		char b[48]; snprintf(b, sizeof b, "%u,%d,%d", serial, turn, arg);
		rm8NetInput(RM8C_MINI_ACT, game, kind, b);
	}
	void endShow(const char *why)	// the game shown here goes (it ended, or the result is applied now): back to the board
	{
		if(!shows) return;
		Rm8MiniResult vis;
		bool ok = rm8MiniActive() && rm8MiniDone() && rm8MiniFinish(vis);
		if(!ok && rm8MiniActive()) rm8MiniAbort();
		rm8UiPopHidden(); rm8MusicResumeBoard();
		shows = false;
		rm8Mini21NetClear();
		if(ok){	// a check: the game here showed the referee's result
			Rm8MiniResult res; ref.result(res, pl);
			std::string a = encodeResult(vis), b = encodeResult(res);
			rm8Log("events: [N] minigame 21: the game here %s (%s)%s", why, a.c_str(), a == b ? ", = the referee's result" : (", NOT the referee's " + b).c_str());
		}else rm8Log("events: [N] minigame 21: the game here %s (not finished)", why);
	}
	bool groupUpdate(float dt)
	{
		waited++;
		if(have){	// RM8M21_FINAL at this frame: every device hands out the referee's result now (a game still shown here goes)
			endShow("closes for the result");
			if(g_netMini == this) g_netMini = nullptr;
			Rm8MiniResult res; ref.result(res, pl);
			rm8Log("events: [N] minigame 21 (everybody): the referee's result %s after %u frames (%d stabs)", encodeResult(res).c_str(), waited, ref.stabs);
			for(const Rm8MiniReward &r : rm8MiniRewards(res)) applyReward(ev, r);
			return true;
		}
		bool live = rm8NetMiniMayStart();
		if(!started && !ref.finished && live){	// the game here (also later: a device that caught up shows the actions so far, faster)
			started = true;
			Rm8Mini21Net nn; nn.on = true; nn.defender = ref.defender;
			nn.send = [this](int k, int t, int a){ sendAct(k, t, a); };
			nn.botDue = [this](int t){ dueTurn = t; };
			rm8Mini21NetSet(nn);
			for(const Rm8Mini21Act &a : ref.acts) rm8Mini21NetPush(a);
			std::vector<Rm8MiniPlayer> vp = pl;
			std::string mine;
			for(Rm8MiniPlayer &p : vp){ p.human = p.human && rm8NetOwnsSeat(p.seat); if(p.human) mine += " " + std::to_string(p.seat); }	// "human" = played on this device
			rm8UiPushHidden();
			shows = rm8MiniStart(game, vp, seed);
			if(!shows){ rm8UiPopHidden(); rm8Mini21NetClear(); startFailed = true; rm8Log("events: minigame 21 could not start here (scenes/minigame data?): this device's turns are the computer's"); }
			else rm8Log("events: [N] minigame 21 shown here from frame %u (%d action(s) so far), seats played here:%s", waited, (int)ref.acts.size(), mine.empty() ? " none" : mine.c_str());
		}
		if(shows && rm8MiniDone() && (doneT += dt) >= 0.5f) endShow("ended");
		if(!shows && live && !ref.finished && ref.cur < (int)pl.size() && pl[ref.cur].human && rm8NetOwnsSeat(pl[ref.cur].seat) && autoSent != ref.turn){	// a turn of this device that it cannot show: the computer's choice
			autoSent = ref.turn; sendAct(RM8M21_AUTO, ref.turn, 0);
			rm8Log("events: [N] minigame 21: turn %d of seat %d is not shown here: the computer's choice", ref.turn, pl[ref.cur].seat);
		}
		if(ref.finished && !doneSent && !shows && live){ doneSent = true; sendAct(RM8M21_DONE, ref.turn, 0); }	// the host applies the result once the players have seen it
		return false;
	}
	bool act(int kind, int peer, int turn, int arg)	// at its frame on every device (net.cpp apply)
	{
		Rm8Mini21Act a;
		switch(kind){
		case RM8M21_ROT: case RM8M21_STAB: case RM8M21_AUTO:
			if(ref.finished || turn != ref.turn) return false;
			if(kind != RM8M21_AUTO && !pl[ref.cur].human) return false;
			{
				int who = ref.cur, before = ref.turn;
				a = kind == RM8M21_ROT ? ref.rot(arg) : ref.stab(kind, arg);
				if(kind == RM8M21_ROT) rm8Log("events: [N] minigame 21: turn %d seat %d turns the barrel %s (angle %d)", a.turn, pl[who].seat, a.dir ? "left" : "right", a.angle);
				else rm8Log("events: [N] minigame 21: turn %d seat %d (%s) %s hole %d (angle %d%s) -> %s%s", a.turn, pl[who].seat, pl[who].human ? "human" : "computer", a.kind == RM8M21_AUTO ? "computer choice" : "stabs",
					a.hole, a.angle, a.kind == RM8M21_AUTO && a.dir ? ", turned left" : "", a.outcome ? "the pirate is hit" : "safe", ref.finished ? " - the game is over" : "");
				if(ref.turn != before) turnAt = waited;
				if(ref.finished) finishAt = waited;
			}
			if(shows) rm8Mini21NetPush(a);
			return true;
		case RM8M21_DONE:
			if(!ref.finished) return false;
			if(std::find(done.begin(), done.end(), peer) == done.end()) done.push_back(peer);
			return true;
		case RM8M21_FINAL:
			if(!ref.finished) return false;
			have = true;
			return true;
		default: return false;
		}
	}
};

}	// namespace

Task *makeMiniTask(int seat, int ev){ return new MiniTask(seat, ev); }

// [V] MiniGame_ExecuteGame @0x4fcb10: false = not handled (game 0x15 with a human in the game: the task runs instead)
bool aiMiniGame(int seat, int ev, bool forceComputer, bool plusLow)
{
	if(ev == 0x15 && !forceComputer) for(int k = 0; k < rm8TurnRoleCount(); k++) if(rm8TurnStatus(k) != 7 && rm8TurnHuman(k)) return false;

	Rm8MiniReward r = rm8MiniComputer(ev, difficulty(), [](){ return rm8GameRand(); });
	if(plusLow && rm8RulesOn() && r.ticketsDelta > 0){	// [R] rules_plus B6: with a chance that depends on the difficulty the result is a low one (never above the original; 插海盗 21 is not touched); [N] only for the computer seats
		int sc = rm8RulesMiniScore(ev, r.ticketsDelta);
		if(sc != r.ticketsDelta){ r.ticketsDelta = sc; r.textArg = sc; }
	}
	r.seat = seat;
	applyReward(ev, r);
	return true;
}

}	// namespace ev

// [N] N4 (events.h)
bool rm8EventMiniWaiting(Rm8MiniWaitInfo *out)
{
	ev::MiniTask *t = ev::g_netMini;
	if(!t || t->have) return false;
	if(out){
		out->seat = t->seat; out->game = t->game; out->frames = t->waited; out->serial = t->serial;
		out->local = t->local; out->playing = t->group ? t->shows : t->started && !t->sent && rm8MiniActive(); out->sent = t->sent; out->resolved = t->have;
		out->group = t->group;
		if(t->group){
			const ev::Ref21 &r = t->ref;
			out->turn = r.turn; out->turnSeat = r.cur < (int)t->pl.size() ? t->pl[r.cur].seat : -1; out->turnHuman = r.cur < (int)t->pl.size() && t->pl[r.cur].human;
			out->finished = r.finished; out->turnFrames = t->waited - t->turnAt; out->finishFrames = r.finished ? t->waited - t->finishAt : 0;
			out->dueTurn = t->startFailed || (t->started && !t->shows) ? r.turn : t->dueTurn;	// a device that does not show the game is always "due"
			out->humanSeats.clear(); for(int i = 0; i < r.n && i < (int)t->pl.size(); i++) if(t->pl[i].human) out->humanSeats.push_back(t->pl[i].seat);
			out->donePeers = t->done;
		}
	}
	return true;
}
bool rm8EventMiniResult(int game, int mode, const std::string &enc)
{
	ev::MiniTask *t = ev::g_netMini;
	if(!t || t->have || t->group || t->game != game) return false;
	t->have = true; t->resMode = mode == 0 ? 0 : 1; t->resText = enc;
	return true;
}
bool rm8EventMiniAct(int game, int kind, int peer, const std::string &s)
{
	ev::MiniTask *t = ev::g_netMini;
	if(!t || t->have || !t->group || t->game != game) return false;
	unsigned ser = 0; int turn = 0, arg = 0;
	if(sscanf(s.c_str(), "%u,%d,%d", &ser, &turn, &arg) != 3 || ser != t->serial) return false;
	return t->act(kind, peer, turn, arg);
}
