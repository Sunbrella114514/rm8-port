// Magic house (event 0xD, "magicHouse.ui"), after the task object 0x4B2F60 (vtable 0x5E47D0; the name Court_InitContainer is wrong: it is the magic
// house; vf16 Cls_5e47d0__vf16_Init @0x4cb400 = MagicHouse_InitializeRandom @0x4caa00 + CUi_magicHouse_InitUI @0x4caaf0, vf1 Update @0x4cb580, spin
// Cls_5e47d0__vf1_Tick @0x4cb890, quit vf11 @0x4cb4c0, effects MagicHouse_ApplyMagicEffect @0x4cdb10, texts MagicHouse_GenerateDescription @0x4d12f0) and
// the computer path Auction_Vendue_Process @0x4f84c0 (the name is wrong, it is the magic house AI). Swap message 0x27 Role_SwapAssetsWithRole @0x48bf70,
// 0x35 Role_StartEffectOnRandomOwnTerras @0x487780.
// [V] four distinct effect numbers 0..11 (10 % of them +12 from difficulty 3 on) are shown; the player picks a role, the selector spins over the four rows and
// slows down; the effect where it stops is applied to the chosen role. Effect n: 0 +5000 cash, 1 -5000, 2 +10 % deposit, 3 -10 % deposit, 4 / 5 the role's
// stocks limit up / down 3 days, 6 a free storey on an own residential plot, 7 demolish a storey, 8 / 9 +-50 tickets, 10 a card, 11 lose a card,
// 12 swap cash with the poorest, 13 lose half the cash, 14 swap deposit with the lowest, 15 lose half the deposit, 16 swap stocks, 17 lose half the stocks,
// 18 swap lands with a random role, 19 half the lands (rounded up) are blown up, 20 swap tickets, 21 lose all tickets, 22 swap cards, 23 lose all cards.
// Effects 4 / 5 / 16 / 17 use rm8StockForceHoldings / rm8StockSwapHoldings / rm8StockHalveHoldings (stock.h). [A] No explosion for 19.
#include "event_int.h"
#include "audio.h"
#include "stock.h"
#include "present.h"
#include "boardfx.h"
#include "rules_plus.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace ev {

namespace {

int32_t statOf(int kind, int seat)	// [N] int32_t
{
	switch(kind){
	case 0: return rm8GameCash(seat);
	case 1: return rm8GameDeposit(seat);
	case 3: return rm8GamePoints(seat);
	case 4: return rm8StockValue(seat);
	case 5: return rm8CardCount(seat);
	}
	return 0;
}

// [V] MagicHouse_SelectPoorest @0x4cbb10 and its siblings: the active role with the smallest statistic; ties without the current role pick a random one
int lowest(int kind, int cur)
{
	int best = -1;
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		if(rm8TurnStatus(s) == 7) continue;
		if(best < 0 || statOf(kind, s) < statOf(kind, best)) best = s;
	}
	if(best < 0) return -1;
	std::vector<int> ties;
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(rm8TurnStatus(s) != 7 && s != cur && statOf(kind, s) == statOf(kind, best)) ties.push_back(s);
	return ties.empty() ? best : ties[rnd() % ties.size()];
}
int randomRole(int cur)	// MagicHouse_SelectRandomRole @0x4cc5d0 (FUN_0048c5a0)
{
	std::vector<int> o;
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(rm8TurnStatus(s) != 7 && s != cur) o.push_back(s);
	return o.empty() ? -1 : o[rnd() % o.size()];
}

// [V] Role_CountOwnTerras(kind, lo, hi): kind 0 residential, 9 all; the plot's level in [lo, hi]
std::vector<int> ownLands(int seat, bool residentialOnly, int lo, int hi)
{
	std::vector<int> v;
	for(int t = 0; t < rm8GameTerraCount(); t++){
		if(rm8GameTerraOwner(t) != seat) continue;
		if(residentialOnly && rm8GameTerraType(t) != 1) continue;
		int l = rm8GameTerraLevel(t);
		if(l >= lo && l <= hi) v.push_back(t);
	}
	return v;
}

int genOne()	// [V] MagicHouse_GenerateOneNumber @0x4cba90
{
	int n = rnd() % 12;
	if(difficulty() >= 3 && rnd() % 10 < 1) n += 12;
	static std::vector<int> forced; static size_t fk = 0; static bool init = false;	// test hook RM8_MAGIC_EFFECT="4,5,16,17": the numbers come from this list in turn
	if(!init){ init = true; if(const char *e = getenv("RM8_MAGIC_EFFECT")){ for(const char *c = e; *c;){ forced.push_back(atoi(c)); while(*c && *c != ',') c++; if(*c) c++; } } }
	if(!forced.empty()) n = forced[fk++ % forced.size()];
	return n;
}

// [R] rules_plus B3: how much an effect hurts (badScore) / helps (goodScore) the role it is cast on, roughly in money; 0 = nothing happens / not a plain effect (the swaps 12 14 16 18 20 22 depend on who
// is poorest and are not rated). Odd numbers are the bad ones (as in the effect table above), the heavy ones 13..23 only exist from difficulty 3 on.
int32_t badScore(int v, int s)	// [N] int32_t
{
	int32_t PI = rm8GamePI(), cards = rm8CardCount(s), pts = rm8GamePoints(s), stock = rm8StockValue(s);	// [N] int32_t
	int lands = (int)ownLands(s, false, 1, 5).size();
	switch(v){
	case 1: return rm8GameCash(s) >= 1000 ? 5000 : 0;
	case 3: return (int32_t)(rm8GameDeposit(s) * 0.1f);	// [N] int32_t
	case 5: return stock > 0 ? stock / 5 : 0;
	case 7: return lands > 0 ? 8000 * PI : 0;
	case 9: return std::min(pts, 50) * 100;	// [N] int32_t
	case 11: return cards > 0 ? 3000 : 0;
	case 13: return (int32_t)(rm8GameCash(s) * 0.5f);	// [N] int32_t
	case 15: return (int32_t)(rm8GameDeposit(s) * 0.5f);
	case 17: return stock / 2;
	case 19: return (int32_t)(lands / 2 + lands % 2) * 15000 * PI;	// [N] int32_t
	case 21: return pts * 100;
	case 23: return cards * 3000;
	}
	return 0;
}
int32_t goodScore(int v, int s)	// [N] int32_t
{
	int32_t PI = rm8GamePI(), stock = rm8StockValue(s);	// [N] int32_t
	switch(v){
	case 0: return 5000;
	case 2: return (int32_t)(rm8GameDeposit(s) * 0.1f);	// [N] int32_t
	case 4: return stock > 0 ? stock / 5 : 0;
	case 6: return ownLands(s, true, 0, 4).empty() ? 0 : 10000 * PI;
	case 8: return 5000;
	case 10: return rm8CardCount(s) < rm8CardMax() ? 3000 : 0;
	}
	return 0;
}

// [V] MagicHouse_ApplyMagicEffect @0x4cdb10 + MagicHouse_GenerateDescription @0x4d12f0 (str 3905 + effect). The messages go to the chosen role's queue, the two
// "meeting" hand-offs (0x2e) only when it is not the current role
std::string applyEffect(int cur, int tgt, int value)
{
	auto enq = [&](int cmd, int a, int b){ rm8TurnEnqueue(tgt, cmd, a, b); };
	const std::string tn = rm8TurnName(tgt);
	if(rm8RulesOn() && tgt != cur && badScore(value, tgt) > 0) rm8HateNote(tgt, cur, rm8PlusTuning().hateMagic, "bad magic house effect");	// [R] B3
	std::string desc;
	if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: magic house: effect %d: stock value of seat %d before: %d", value, tgt, rm8StockValue(tgt));
	bool meet = true;
	int other = -1;
	auto two = [&](int kind, bool byRandom){
		other = byRandom ? randomRole(cur) : lowest(kind == 4 ? 4 : kind == 3 ? 3 : kind == 5 ? 5 : kind, cur);
		if(other >= 0) enq(0x27, other + 1, kind);
		desc = plainFmt(3905 + value, tn.c_str(), other >= 0 ? rm8TurnName(other) : "");
	};
	switch(value){
	case 0: enq(0x17, 5000, 0); desc = plainFmt(3905, tn.c_str()); break;
	case 1: enq(0x16, 5000, 0); desc = plainFmt(3906, tn.c_str()); break;
	case 2: enq(0x17, (int)(rm8GameDeposit(tgt) * 0.1f), 1); desc = plainFmt(3907, tn.c_str()); break;
	case 3: enq(0x16, (int)(rm8GameDeposit(tgt) * 0.1f), 1); desc = plainFmt(3908, tn.c_str()); break;
	case 4: case 5:	// [V] Util_Apply2DTransform(role, +-10.0, 3) at once (no 0x2e hand-offs): every held stock limit up / down for 3 days
		desc = plainFmt(3905 + value, tn.c_str());
		rm8StockForceHoldings(tgt, value == 4 ? 10.0f : -10.0f, 3);
		meet = false;
		break;
	case 6: case 7: {
		std::vector<int> l = value == 6 ? ownLands(tgt, true, 0, 4) : ownLands(tgt, false, 1, 5);
		desc = plainFmt(3905 + value, tn.c_str());
		if(l.empty()){ meet = false; break; }
		enq(value == 6 ? 0xC : 0xD, l[rnd() % l.size()], 1);	// [V] a == 0 picks a random own plot (FindRandomProperty); the port passes the plot
		break; }
	case 8: enq(7, 50, 0); desc = plainFmt(3913, tn.c_str()); break;
	case 9: enq(8, 50, 0); desc = plainFmt(3914, tn.c_str()); break;
	case 10: {
		int idx = cardSelectByDifficulty(0);
		enq(4, idx, 1);
		desc = plainFmt(3915, tn.c_str(), idx >= 0 && rm8CardName(idx + 1) ? rm8CardName(idx + 1) : "");
		break; }
	case 11:
		desc = plainFmt(3929, tn.c_str());
		if(rm8CardCount(tgt) > 0) enq(5, rnd() % rm8CardCount(tgt), 1); else meet = false;	// [A] the slot is chosen here (the original removes a random card when the message runs)
		break;
	case 12: two(0, false); break;
	case 13: enq(0x16, (int)(rm8GameCash(tgt) * 0.5f), 0); desc = plainFmt(3918, tn.c_str()); break;
	case 14: two(1, false); break;
	case 15: enq(0x16, (int)(rm8GameDeposit(tgt) * 0.5f), 1); desc = plainFmt(3920, tn.c_str()); break;
	case 16: two(4, false); break;
	case 17: desc = plainFmt(3922, tn.c_str()); rm8StockHalveHoldings(tgt); break;	// [V] Util_ApplyScoreChange(role, 0.5) at once, then the hand-offs
	case 18: two(2, true); break;
	case 19: { int n = (int)ownLands(tgt, false, 1, 5).size(); enq(0x35, 0, n / 2 + n % 2); desc = plainFmt(3924, tn.c_str()); break; }
	case 20: two(3, false); break;
	case 21: enq(8, (int)rm8GamePoints(tgt), 0); desc = plainFmt(3926, tn.c_str()); break;
	case 22: two(5, false); break;
	case 23: enq(5, -1, rm8CardCount(tgt)); desc = plainFmt(3928, tn.c_str()); break;
	}
	if(meet && cur != tgt){	// [V] the two characters turn to each other
		rm8TurnEnqueue(cur, 0x2e, tgt + 1, 0);
		rm8TurnEnqueue(tgt, 0x2e, cur + 1, 1);
	}
	if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: magic house: seat %d casts effect %d on seat %d: %s (stock value now %d)", cur, value, tgt, desc.c_str(), rm8StockValue(tgt));
	return desc;
}

struct MagicTask : Task {
	float timer = 0;			// [V] +4
	int state = 0;				// [V] +0x4c
	int target = -1;			// [V] +0x54
	int slot[4];				// [V] +0x5c..+0x68 the effect numbers
	float speed, decel, pos;		// [V] +0x38, +0x3c, +0x48
	int index = 0, last = 0;		// [V] +0x40, +0x44
	int voice = 0, marker = 0;
	Npc *npc = nullptr;
	bool busy(){ return voice > 0 && rm8SndPlaying(voice); }
	void say(const std::string &line, bool play = true)
	{
		int snd = 0; std::string t = plain(line, &snd);
		rm8UiSetTextByName(ui, "bobo", wrap(t, 15));
		if(play){ rm8SndStop(voice); voice = snd ? rm8SndPlay(snd) : 0; }
	}
	void say(int id){ say(rm8Str(id)); }
	static std::string pn(const char *base, int i){ char b[32]; snprintf(b, sizeof b, "%s%d", base, i); return b; }

	MagicTask(int s)
	{
		seat = s;
		ui = rm8UiLoad("MagicHouse", RM8UI_DIALOG);
		if(!ui) return;
		// [V] MagicHouse_InitializeRandom @0x4caa00
		index = rnd() % 4; last = index; pos = (float)index;
		speed = rnd() / 32767.0f * 8.0f + 12.0f; decel = rnd() / 32767.0f * 2.0f + 2.0f;
		timer = 1.6f * waitScale();
		slot[0] = genOne();
		do slot[1] = genOne(); while(slot[1] == slot[0]);
		do slot[2] = genOne(); while(slot[2] == slot[0] || slot[2] == slot[1]);
		do slot[3] = genOne(); while(slot[3] == slot[0] || slot[3] == slot[1] || slot[3] == slot[2]);
		for(int i = 0; i < 4; i++){	// the four rows: names of the effects (str 5601.. in the order of the effect numbers: 0..7, then 8..)
			int v = slot[i], id = v < 8 ? 5601 + v : v < 20 ? 5611 + (v - 8) : 5625 + (v - 20);	// [V] the string ids of ApplyMagicEffect's switch
			rm8UiSetTextByName(ui, pn("Magic", i).c_str(), plain(rm8Str(id)));
		}
		for(int i = 0; i < 4; i++){	// [V] the roles: portrait and "%s\n%dP"
			bool on = i < rm8TurnRoleCount() && rm8TurnStatus(i) != 7;
			rm8UiSetVisibleN(ui, pn("RolePic", i).c_str(), on); rm8UiSetVisibleN(ui, pn("RoleName", i).c_str(), on);
			if(!on) continue;
			rm8UiSetPicFileN(ui, pn("RolePic", i).c_str(), std::string("prt\\npc\\") + rm8TurnPic(i) + "_12.tga");
			rm8UiSetEnabledN(ui, pn("RolePic", i).c_str(), true);
			rm8UiSetTextByName(ui, pn("RoleName", i).c_str(), std::string(rm8TurnName(i)) + "\\" + std::to_string(i + 1) + "P");
		}
		uiTapOnly(ui, {"Quit", "RolePic0", "RolePic1", "RolePic2", "RolePic3"});
		if(!marker) marker = rm8UiAddWidget(ui, "plane", 0, 0, 0, 177, 32, -1, 0x70ffffff, 16, 9);
		rm8UiSetIgnore(ui, marker, true); rm8UiSetVisible(ui, marker, false);
		npc = npcCreate(0x412, ui, "NPC"); npcPlay(npc, 0);
		say(3901);
		rm8UiOnClickName(ui, [this](const std::string &n){ click(n); });
		rm8UiShow(ui, true);
	}
	~MagicTask() override { npcDestroy(npc); if(voice) rm8SndStop(voice); rm8UiSetVisible(ui, marker, false); rm8UiShow(ui, false); rm8UiOnClickName(ui, nullptr); }
	bool ok() const { return ui != nullptr; }
	bool ready() const override { return state == 1; }
	void showMarker()
	{
		float x, y, w, h;
		if(!rm8UiGetRectN(ui, pn("Magic", index).c_str(), x, y, w, h)) return;
		rm8UiSetRect(ui, marker, 188 + 48 + x, 18 + 44 + y, w, h);	// MagicTable (48, 44) in MagicBg (188, 18) in the root
		rm8UiSetVisible(ui, marker, true);
	}
	void click(const std::string &n) override
	{
		if(n == "Quit"){	// [V] vf11 @0x4cb4c0: only before the spell
			if(state < 2){ rm8SndStop(voice); voice = 0; timer = 0; say(rm8Str(3904), false); npcPlay(npc, 3); state = 6; }
			return;
		}
		int k = -1;
		if(sscanf(n.c_str(), "RolePic%d", &k) == 1 && state == 1 && k >= 0 && k < rm8TurnRoleCount() && rm8TurnStatus(k) != 7){	// [V] MagicHouse_ShowRoleSelection @0x4ca940
			rm8SndStop(voice); voice = 0; rm8SndPlay(0x8a);
			target = k; state = 2;
		}
	}
	bool update(float dt) override
	{
		if(state == 3 && target >= 0){	// [V] Cls_5e47d0__vf1_Tick @0x4cb890
			speed -= dt * decel;
			if(speed < 0) speed = 0;
			pos += dt * speed;
			if(pos >= 4.0f) pos = 0;
			index = std::max(0, std::min(3, (int)pos));
			if(index != last){ last = index; showMarker(); rm8SndPlay(0x8b); }
			if(speed <= 0){ state = 4; timer = 1.6f * waitScale(); }
			return false;
		}
		timer -= dt;
		if(timer > 0 || busy()) return false;
		timer = 1.6f * waitScale();
		if(state == 0){ state = 1; say(3902); npcPlay(npc, 1); }
		else if(state == 2){ say(3903); npcPlay(npc, 2); showMarker(); state = 3; }
		else if(state == 4){ say(applyEffect(seat, target, slot[index]), false); state = 5; }
		else if(state == 5){ rm8UiSetVisible(ui, marker, false); say(3904); npcPlay(npc, 3); state = 6; }
		else if(state == 6) return true;
		return false;
	}
};

}	// namespace

Task *makeMagicTask(int seat)
{
	MagicTask *t = new MagicTask(seat);
	if(!t->ok()){ delete t; return nullptr; }
	return t;
}

// [V] Auction_Vendue_Process @0x4f84c0: a computer picks a random other active role as the target and one of the four numbers by rand() & 3; the description
// goes to the front of its queue (always returns 1)
bool aiMagic(int seat)
{
	int tgt = randomRole(seat);
	if(tgt < 0) return true;
	int slot[4];
	slot[0] = genOne();
	do slot[1] = genOne(); while(slot[1] == slot[0]);
	do slot[2] = genOne(); while(slot[2] == slot[0] || slot[2] == slot[1]);
	do slot[3] = genOne(); while(slot[3] == slot[0] || slot[3] == slot[1] || slot[3] == slot[2]);
	int value = slot[rnd() & 3];
	if(rm8RulesOn()){	// [R] rules_plus B3 (the original draws above are all made: the shared random stream is used as before)
		const Rm8PlusTuning &pt = rm8PlusTuning();
		bool done = false;
		int hl[8], hn = rm8HateList(seat, hl);
		for(int i = 0; i < hn && !done; i++){	// the hated role gets the worst of the four effects
			int h = hl[i], best = -1; int32_t bs = 0;	// [N] int32_t
			if(rm8TurnStatus(h) == 7) continue;
			for(int k = 0; k < 4; k++){ int32_t sc = badScore(slot[k], h); if(sc > bs){ bs = sc; best = k; } }	// [N] int32_t
			if(best >= 0 && rm8HateAct(seat, h, "casts the worst magic effect on it")){ tgt = h; value = slot[best]; done = true; rm8Log("rules+: magic house: seat %d (%s) casts effect %d (harm ~%d) on the hated seat %d (%s)", seat, rm8TurnName(seat), value, bs, h, rm8TurnName(h)); }
		}
		if(!done && rm8PlusRand() % 100 < pt.magicSelfGoodPct[rm8RulesLevel() - 1]){	// otherwise: sometimes the best good effect for itself instead of a random one for somebody else
			int best = -1; int32_t bs = 0;	// [N] int32_t
			for(int k = 0; k < 4; k++){ int32_t sc = goodScore(slot[k], seat); if(sc > bs){ bs = sc; best = k; } }
			if(best >= 0){ tgt = seat; value = slot[best]; rm8Log("rules+: magic house: seat %d (%s) takes the good effect %d for itself (worth ~%d)", seat, rm8TurnName(seat), value, bs); }
		}
	}
	std::string d = applyEffect(seat, tgt, value);
	queueTextAt(seat, 0, d);
	return true;
}

// [V] message 0x27 Role_SwapAssetsWithRole @0x48bf70 (this = seat, a = the other role + 1, b = kind: 0 cash, 1 deposit, 2 plots, 3 tickets, 4 stocks, 5 cards)
void swapMessage(int seat, int a, int b)
{
	int o = a - 1;
	if(a == 0 || o == seat || o < 0 || o >= rm8TurnRoleCount()) return;
	switch(b){
	case 0: { int32_t c1 = rm8GameCash(seat), c2 = rm8GameCash(o); rm8GameSetMoney(seat, c2, rm8GameDeposit(seat)); rm8GameSetMoney(o, c1, rm8GameDeposit(o)); break; }	// [N] int32_t
	case 1: { int32_t d1 = rm8GameDeposit(seat), d2 = rm8GameDeposit(o); rm8GameSetMoney(seat, rm8GameCash(seat), d2); rm8GameSetMoney(o, rm8GameCash(o), d1); break; }
	case 2:
		for(int t = 0; t < rm8GameTerraCount(); t++){
			int w = rm8GameTerraOwner(t);
			if(w == seat) rm8GameSetTerra(t, o, -1, -1); else if(w == o) rm8GameSetTerra(t, seat, -1, -1);
		}
		break;
	case 3: { int32_t p1 = rm8GamePoints(seat), p2 = rm8GamePoints(o); rm8GameAddPoints(seat, p2 - p1); rm8GameAddPoints(o, p1 - p2); break; }	// [N] int32_t
	case 4: if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: stock swap seat %d (%d) <-> seat %d (%d)", seat, rm8StockValue(seat), o, rm8StockValue(o)); rm8StockSwapHoldings(seat, o); if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: ... now %d / %d", rm8StockValue(seat), rm8StockValue(o)); break;	// [V] the holdings are exchanged
	case 5: {
		std::vector<int> c1, c2;
		for(int i = 0; i < rm8CardMax(); i++){ if(rm8CardAt(seat, i)) c1.push_back(rm8CardAt(seat, i)); if(rm8CardAt(o, i)) c2.push_back(rm8CardAt(o, i)); }
		for(int id : c1) rm8CardRemoveId(seat, id);
		for(int id : c2) rm8CardRemoveId(o, id);
		for(int id : c2) rm8CardAdd(seat, id);
		for(int id : c1) rm8CardAdd(o, id);
		break; }
	}
	if(rm8PresentChance70()) rm8RoleSayAt(seat, 0, 4);	// [V] 70 %: thunk_FUN_00484930(0, 4)
}

// [V] message 0x35 (a == 0) Role_StartEffectOnRandomOwnTerras @0x487780: b random own plots with a building (up to 128 collected, rand % remaining each, no repeats) go through the crash
// sequence of Cls_5e6a28 @0x4F4380 (per plot: the camera, art 6028 + sample 0x67, the building goes after 1.5 s: boardfx.cpp) with the role in state 9; the emote 5 is queued at the front.
// No plot with a building: nothing happens.
void bombMessage(int seat, int a, int b)
{
	if(a != 0) return;
	std::vector<int> l = ownLands(seat, false, 1, 5), pick;
	for(; b > 0 && !l.empty(); b--){
		int k = rnd() % l.size();
		pick.push_back(l[k]);
		l.erase(l.begin() + k);
	}
	if(pick.empty()) return;
	if(rm8BoardFxCrash(seat, pick)) rm8RoleSayAt(seat, 0, 5);
	else for(int t : pick) rm8GameSetTerra(t, -2, 0, -1);
}

}	// namespace ev
