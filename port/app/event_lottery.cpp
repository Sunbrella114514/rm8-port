// Lottery: buying a number (event 6, letou.ui) and the monthly draw (event 0x62, letouErnie.ui), after the task objects Lottery_InitContainer
// @0x4b2de0 (vtable 0x5E4788; vf16 CLotteryEvent__vf16_Init @0x4c71a0, Update @0x4c7790, click Letou_OnLandEvent @0x4c7020, quit vf11 @0x4c79a0,
// Letou_ShowGoodbye @0x4c79f0) and 0x4B30E0 (vtable 0x5E4818; vf16 CUi_letouErnie__vf16_Init @0x4c8bb0, Update @0x4c9420, spin
// CUi_letouErnie__vf1_Tick @0x4c99f0, Letou_Ernie_ProcessWinner @0x4c9c60), the manager DAT_006403d0 (Stock_StdVector__ctor @0x4c7c10) and the
// computer path Lottery_BuyTicket @0x4f8290.
// [V] the manager: 32 numbers (Stock_ClearAndInit @0x4c8180 pushes 1..32), the numbers every role holds (4 vectors), the prize pool +0x50
// (starts at price index * 5000: Stock_InitPriceByDifficulty @0x4c7d40); Stock_BuyCard @0x4c8200: cash >= 1000, the number must be free,
// pool += 1000, message 0x15(1000) takes the money.
// [V] the draw page borders (LetouErnie_DrawSelectionBorders @0x4c84b0: role colour rings, the 4 px highlight and its two-number trail) and the sounds of the spin
// (Tick @0x4c99f0: 0x87 per number at volume 0.4, 0x8d at the stop) are in DrawTask below; taken numbers show the buyer's portrait.
#include "event_int.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

namespace ev {

namespace {

std::vector<int> g_avail;			// free numbers (+0x44..+0x48)
std::vector<int> g_held[8];			// numbers per seat (+4 + seat * 0x10)
int32_t g_pool = 5000;				// [N] +0x50
bool g_init = false;

void ensure(){ if(g_init) return; g_init = true; g_avail.clear(); for(int i = 1; i <= 32; i++) g_avail.push_back(i); for(auto &h : g_held) h.clear(); g_pool = 5000; }

int ownerOf(int number){ for(int s = 0; s < 8; s++) for(int n : g_held[s]) if(n == number) return s; return -1; }

// [V] Stock_BuyCard @0x4c8200
bool buyNumber(int seat, int number)
{
	ensure();
	if(rm8GameCash(seat) < 1000) return false;
	auto it = std::find(g_avail.begin(), g_avail.end(), number);
	if(it == g_avail.end()) return false;
	g_avail.erase(it);
	g_held[seat].push_back(number);
	rm8TurnEnqueue(seat, 0x15, 1000, 0);	// Role_SubMoney_NoEmote(1000, cash)
	g_pool += 1000;
	return true;
}

std::string key(int seat){ return std::string("prt\\npc\\") + rm8TurnPic(seat) + "_12.tga"; }

// the 32 number cells: text = the number, taken ones show the buyer's portrait (callbacks 0x12 Letou_DrawRoleTexturedRect @0x4c6c60)
// [V] Stock_FindCard @0x4c6e40 (callback 0x13 of numPlane): after the portrait a 3 px frame (rings 1..3 inset) in the buyer's colour (role +0x1F4, Role_CreateAvatar @0x480b30)
struct Frames { Rm8Ui *ui = nullptr; int id[33][4] = {}; };
Frames g_fr[2];
void paintFrames(Rm8Ui *ui, float ox, float oy, int slot)
{
	static const unsigned col[4] = {0xFFE63A43, 0xFFEAC716, 0xFF3344EB, 0xFF18E98C};
	Frames &f = g_fr[slot];
	if(f.ui != ui){ f = Frames(); f.ui = ui; }
	for(int i = 1; i <= 32; i++){
		char nm[8]; snprintf(nm, sizeof nm, "%d", i);
		int o = ownerOf(i); float x, y, w, h;
		bool on = o >= 0 && rm8UiGetRectN(ui, nm, x, y, w, h);
		if(on && !f.id[i][0]) for(int k = 0; k < 4; k++){ f.id[i][k] = rm8UiAddWidget(ui, "plane", 0, 0, 0, 1, 1, -1, 0xffffffff, 16, 14); rm8UiSetIgnore(ui, f.id[i][k], true); }
		if(!f.id[i][0]) continue;
		for(int k = 0; k < 4; k++) rm8UiSetVisible(ui, f.id[i][k], on);
		if(!on) continue;
		float X = ox + x + 1, Y = oy + y + 1, W = w - 2, H = h - 2, t = 3;	// rings 1..3 inset by 1
		unsigned c = col[o & 3];
		rm8UiSetRect(ui, f.id[i][0], X, Y, W, t); rm8UiSetRect(ui, f.id[i][1], X, Y + H - t, W, t);
		rm8UiSetRect(ui, f.id[i][2], X, Y + t, t, H - 2 * t); rm8UiSetRect(ui, f.id[i][3], X + W - t, Y + t, t, H - 2 * t);
		for(int k = 0; k < 4; k++) rm8UiSetBColor(ui, f.id[i][k], c);
	}
}

void paintCells(Rm8Ui *ui, bool enableFree, float ox = 239, float oy = 88, int slot = 0)	// ox / oy: numPlane's position in the root (Letou: (110 + 129, 85 + 3))
{
	paintFrames(ui, ox, oy, slot);
	for(int i = 1; i <= 32; i++){
		char nm[8]; snprintf(nm, sizeof nm, "%d", i);
		int o = ownerOf(i);
		rm8UiSetTextByName(ui, nm, nm);	// [V] the number stays under the portrait (label text is drawn after the 0x12 callback)
		if(o >= 0) rm8UiSetPicFileN(ui, nm, key(o)); else rm8UiSetPicFileN(ui, nm, "");
		rm8UiSetEnabledN(ui, nm, o < 0 && enableFree);
		rm8UiSetIgnoreN(ui, nm, !(o < 0 && enableFree));
	}
}

// ---- buying
struct LetouTask : Task {
	float timer = 0;	// [V] +8
	int state = 0;		// [V] +0x34
	bool canBuy = true;	// [V] +0x30
	int voice = 0;
	Npc *npc = nullptr;
	bool busy(){ return voice > 0 && rm8SndPlaying(voice); }
	void say(int strId, bool play = true)
	{
		int snd = 0; std::string t = plain(rm8Str(strId), &snd);
		rm8UiSetTextByName(ui, "Label", wrap(t, 19.1f));
		if(play){ rm8SndStop(voice); voice = snd ? rm8SndPlay(snd) : 0; }
	}
	LetouTask(int s)
	{
		seat = s; ensure();
		ui = rm8UiLoad("Letou", RM8UI_DIALOG);
		if(!ui) return;
		timer = 1.6f * waitScale();
		rm8UiSetFontSizeN(ui, "Label", 15);	// [A] the original line of 19 full-width characters fits the 286 px label in one row (GDI cell 15 px at font 16)
		rm8UiSetTextByName(ui, "bonus", "$" + std::to_string(g_pool));
		rm8UiSetPicN(ui, "bonus", -1);	// [V] the prize label's picture 3 is prt04.tga with a mask name (uvlisttable use_mask_name 1): the original shows only the cream box of J206_1 behind the text; the unmasked yellow-on-black flame must not be drawn
		paintCells(ui, true);
		uiTapOnly(ui, {"Quit"});
		for(int i = 1; i <= 32; i++){ char nm[8]; snprintf(nm, sizeof nm, "%d", i); if(ownerOf(i) < 0) rm8UiSetIgnoreN(ui, nm, false); }
		npc = npcCreate(0x41B, ui, "NPC"); npcPlay(npc, 0);
		say(4801);
		rm8UiOnClick(ui, [this](int id){ tap(id); });
		rm8UiShow(ui, true);
	}
	~LetouTask() override { npcDestroy(npc); if(voice) rm8SndStop(voice); rm8UiShow(ui, false); rm8UiOnClick(ui, nullptr); }
	bool ok() const { return ui != nullptr; }
	bool ready() const override { return state >= 1 && state <= 2 && canBuy; }

	void goodbye(float scale, bool play)	// [V] Letou_ShowGoodbye @0x4c79f0
	{
		rm8SndStop(voice); voice = 0;
		timer = scale * waitScale();
		if(play){ int snd = 0; plain(rm8Str(4804), &snd); if(snd) voice = rm8SndPlay(snd); }
		say(4804, false); npcPlay(npc, 3);
	}
	void tap(int id)
	{
		if(id == 0){ canBuy = false; goodbye(0.2f, false); timer = 0; state = 3; return; }	// [V] vf11 @0x4c79a0 (Quit)
		if(id < 1 || id > 32 || !canBuy) return;							// [V] Letou_OnLandEvent @0x4c7020
		if(!buyNumber(seat, id)){ rm8SndPlay(0x71); goodbye(1.8f, true); state = 3; return; }
		rm8SndPlay(0x86);
		canBuy = false;
		goodbye(1.8f, true); state = 3;
		rm8UiSetTextByName(ui, "bonus", "$" + std::to_string(g_pool));
		paintCells(ui, false);
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: lottery: seat %d takes number %d (pool %d)", seat, id, g_pool);
	}
	void click(const std::string &n) override { if(n == "Quit") tap(0); else { int k = atoi(n.c_str()); if(k >= 1 && k <= 32) tap(k); } }
	bool update(float dt) override
	{
		timer -= dt;
		if(timer > 0 || busy()) return false;
		timer = 1.6f * waitScale();
		if(state == 3) return true;
		if(state == 0){ state = 1; say(4802); npcPlay(npc, 1); }
		else if(state == 1){ state = 2; say(4803); npcPlay(npc, 2); }
		return false;
	}
};

// ---- the draw
struct DrawTask : Task {
	float timer = 0;
	int state = 0;			// [V] +0x68: 0 / 1 / 2 intro, 3 spinning, 4 winner, 5 / 6 closing
	float t = 0, speed = 4.8f, accel = 3.0f, tAcc = 4.5f, tDec = 4.0f, pos = 1;	// +0x2c, +0x30, +0x34, +0x38, +0x3c, +0x50
	int number = 1, last = 1;	// +0x48, +0x4c
	bool cleared = false;		// +0x74: somebody won -> all tickets are cleared when the dialog closes
	int voice = 0;
	// [V] LetouErnie_DrawSelectionBorders @0x4c84b0 (states >= 3): the current number (+0x48) gets a 4 px border (rings 1..4 inset) 0xFF7979 that blinks darker
	// (-0x32 per channel, 100 ms parity) once the wheel stopped (+0x6c); behind it the trail: the previous number (+0x58, 0xFF9A79 alpha 0x9B) and the one before (+0x5c, 0xFFCF79 alpha 0x32)
	int ring[3][4] = {};		// widgets of the three borders: trail 2, trail 1, current
	int start = 1, trail1 = 1, trail2 = 1;	// +0x44, +0x58, +0x5c
	bool adv1 = false, adv2 = false, blink = false;	// +0x54, +0x55, +0x6c
	float tm60 = 0.1f, tm64 = 0.16f, clk = 0;	// +0x60, +0x64: after the stop the trail closes up on the current number
	Npc *npc = nullptr;
	bool busy(){ return voice > 0 && rm8SndPlaying(voice); }
	void say(const std::string &line, bool play = true)
	{
		int snd = 0; std::string tx = plain(line, &snd);
		rm8UiSetTextByName(ui, "Bobo", wrap(tx, 20.5f));	// [A] label width 287 at font 14
		if(play){ rm8SndStop(voice); voice = snd ? rm8SndPlay(snd) : 0; }
	}
	void say(int id){ say(rm8Str(id)); }
	DrawTask(int s)
	{
		seat = s; ensure();
		ui = rm8UiLoad("letouErnie", RM8UI_DIALOG);
		if(!ui) return;
		rm8UiSetTextByName(ui, "Bonus", "$" + std::to_string(g_pool));
		paintCells(ui, false, 168, 44, 1);
		uiTapOnly(ui, {});
		for(auto &r : ring) for(int &w : r){ w = rm8UiAddWidget(ui, "plane", 0, 0, 0, 1, 1, -1, 0xffffffff, 16, 9); rm8UiSetIgnore(ui, w, true); rm8UiSetVisible(ui, w, false); }
		npc = npcCreate(0x41B, ui, "NPC"); npcPlay(npc, 4);
		// [V] loader @0x4c8bb0 (mode 0): rand() based spin parameters, the start position rand() % 32 + 1
		auto r = []{ return rnd() / 32767.0f; };
		tAcc = r() * 3 + 3; tDec = r() * 2 + 3; speed = r() * 0.8f + 4.4f; accel = r() * 1.0f + 2.5f;
		number = rnd() % 32 + 1; last = number; pos = (float)number; start = trail1 = trail2 = number;
		timer = 1.6f * waitScale();
		say(4805);
		rm8UiShow(ui, true);
	}
	~DrawTask() override
	{
		if(cleared){ g_init = false; ensure(); }	// [V] Cleanup @0x4c92f0: Stock_ClearAndInit (numbers 1..32 free, tickets dropped); the pool stays reset by ProcessWinner
		npcDestroy(npc); if(voice) rm8SndStop(voice);
		for(auto &r : ring) for(int w : r) rm8UiSetVisible(ui, w, false);
		rm8UiShow(ui, false);
	}
	bool ok() const { return ui != nullptr; }
	// the three borders on their numbers; none before the wheel starts (the callback draws them from state 3 on)
	void showRings()
	{
		const int cell[3] = {trail2, trail1, number};
		unsigned col[3] = {0x32FFCF79u, 0x9BFF9A79u, 0xFFFF7979u};
		if(blink && ((int)(clk * 10.0f)) % 2 == 0) col[2] = 0xFFCD4747u;	// [V] timeGetTime() / 100 % 2 == 0: each colour channel - 0x32
		for(int k = 0; k < 3; k++){
			float x, y, w, h; bool on = state >= 3 && rm8UiGetRect(ui, cell[k], x, y, w, h);	// widget ID == number
			for(int i = 0; i < 4; i++) rm8UiSetVisible(ui, ring[k][i], on);
			if(!on) continue;
			float X = 156 + 12 + x + 1, Y = 37 + 7 + y + 1, W = w - 2, H = h - 2, t = 4;	// numPlane (12, 7) in Plane1 (156, 37) in the root; rings 1..4 inset by 1
			rm8UiSetRect(ui, ring[k][0], X, Y, W, t); rm8UiSetRect(ui, ring[k][1], X, Y + H - t, W, t);
			rm8UiSetRect(ui, ring[k][2], X, Y + t, t, H - 2 * t); rm8UiSetRect(ui, ring[k][3], X + W - t, Y + t, t, H - 2 * t);
			for(int i = 0; i < 4; i++) rm8UiSetBColor(ui, ring[k][i], col[k]);
		}
	}
	// [V] Letou_Ernie_ProcessWinner @0x4c9c60
	void winner()
	{
		int w = ownerOf(number);
		if(w < 0){
			state = 5;
			say(4810); npcPlay(npc, 8);
			g_pool += 5000 * rm8GamePI();	// [N] int32_t
		}else{
			state = 4;
			say(plainFmt(4808, rm8TurnName(w)));
			rm8GameAddMoney(w, g_pool, false);
			if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: lottery draw: number %d, winner seat %d gets %d", number, w, g_pool);
			g_pool = 5000 * rm8GamePI();	// [N] int32_t
			cleared = true;
		}
		rm8UiSetTextByName(ui, "Bonus", "$" + std::to_string(g_pool));
		timer = 1.6f * waitScale();
	}
	bool update(float dt) override
	{
		if(state > 3){	// [V] OnUpdate @0x4c9420: after the stop the trail closes up on the current number
			tm60 -= dt; tm64 -= dt;
			if(tm60 <= 0 && trail1 != number){ trail2 = trail1; trail1 = number; }
			if(tm64 <= 0 && trail2 != number) trail2 = number;
		}
		clk += dt; showRings();
		if(state == 3){	// [V] CUi_letouErnie__vf1_Tick @0x4c99f0
			t += dt;
			if(t < tAcc) speed += dt * accel;
			else if(t >= tDec && t < 1000.0f){ speed -= dt * accel; if(speed < 0) speed = 0; }
			pos += dt * speed;
			if(pos >= 33.0f) pos = 1.0f;
			number = std::max(1, std::min(32, (int)pos));
			if(number != last){ last = number; rm8SndPlay(0x87, 1, 0.4f); }	// [V] a tick per number, sample 0x87 at volume 0.4
			if(speed <= 0){ blink = true; winner(); rm8SndPlay(0x8d, 1, 0.4f); }	// [V] stop: ProcessWinner, then sample 0x8d
			// [V] the trail follows once the wheel moved one / two numbers from its start
			int d = number - start;
			if(!adv1 && (d == 1 || d == -31)) adv1 = true;
			if(!adv2 && (d == 2 || d == -30)) adv2 = true;
			if(adv1){ trail1 = number - 1; if(trail1 == 0) trail1 = 32; }
			if(adv2){ trail2 = trail1 - 1; if(trail2 == 0) trail2 = 32; }
			showRings();
			return false;
		}
		timer -= dt;
		if(timer > 0 || busy()) return false;
		timer = 1.6f * waitScale();
		if(state == 0){ state = 1; say(plainFmt(4806, (int)g_pool) + "@4806"); npcPlay(npc, 5); }
		else if(state == 1){ state = 2; say(4807); npcPlay(npc, 6); }
		else if(state == 2){ state = 3; }
		else if(state == 4){ say(4809); npcPlay(npc, 7); state = 5; }
		else if(state == 5){ say(4811); npcPlay(npc, 9); state = 6; blink = false; }
		else if(state == 6) return true;
		return false;
	}
};

}	// namespace

void lotteryClear(){ g_init = false; ensure(); }

Task *makeLetouTask(int seat)
{
	LetouTask *t = new LetouTask(seat);
	if(!t->ok()){ delete t; return nullptr; }
	return t;
}

Task *makeLetouDrawTask(int seat)
{
	DrawTask *t = new DrawTask(seat);
	if(!t->ok()){ delete t; return nullptr; }
	return t;
}

// [V] Lottery_BuyTicket @0x4f8290: a computer with at least 5000 in cash takes a random free number (pays 1000), str 262 "%s<str#262>%d"
bool aiLottery(int seat)
{
	ensure();
	if(rm8GameCash(seat) < 5000) return false;
	if(!g_avail.empty()){
		int number = g_avail[rnd() % g_avail.size()];
		buyNumber(seat, number);
		queueText(seat, plainFmt(262, rm8TurnName(seat), number));
	}
	return true;
}

// [V] Game_TriggerHolidayEvent @0x4a66b0: when somebody bought a number (fewer than 32 free) the draw (event 0x62) runs for the current role
bool lotteryDrawDue(){ ensure(); return g_avail.size() < 32; }
// [V] News_EventLoseMoney @0x4b53d0 / News_EventBankHack @0x4b5570 add the fine to the prize pool (*(DAT_006403d0 + 0x50) += amount), news.cpp
void lotteryPoolAdd(int32_t v){ ensure(); g_pool = rm8Add32(g_pool, v); }	// [N] int32_t; fines accumulate until a draw is won: the 32-bit wrap, explicit

}	// namespace ev

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_lottery.inc"
