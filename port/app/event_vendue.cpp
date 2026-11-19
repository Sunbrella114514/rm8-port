// The land auction: the public one (event 0x50) and the bankruptcy auction (event 0x51), vendue.ui. After the task object created by CreateGameObject
// @0x4b1bc0 (case 0x50 / 0x51: Wonder_InitContainer @0x4b3910 = vtable 0x5E4980 "CUi_vendue"; args: plot, Terra_GetBuildingModelId art, anim 2 for a residential
// plot else 0, base price = Terra_CalcPrice(1) / 2, flag +0x5c = 1 for 0x51), loader CUi_vendue__vf16__Init @0x4e2fc0, Update vf1 @0x4e4020, skip vf13 @0x4e4f50
// and the helpers Vendue_UpdatePrice @0x4e2e70, Vendue_OnBidAccepted @0x4e2230, Vendue_OnNoBidders @0x4e2140, Vendue_AIBid @0x4e22c0, Vendue_ShowOptions @0x4e25e0,
// Vendue_NextBidder @0x4e27f0, Vendue_CheckWinner @0x4e2a60, Vendue_FindNextBidder @0x4e2c20, Vendue_CountActivePlayers @0x4e4dc0; the buttons call
// Wonder_OnNumInput @0x4e1ae0 ("100" / "500" / "1000" x price index) and Wonder_OnUIEvent @0x4e1a90 (pass).
// [V] the sale: the winner's queue gets 0x41(plot, price) (Role_PayAndTakeTerra @0x487c20: cash first, no emote, Land_SetOwner) and, when the winner is not the role whose turn
// it is, 0x2E hand-offs (current -> winner at the front, winner -> current right behind the payment).
// [A] not drawn: the original's ortho-camera view of the auctioneer / the item is a perspective camera here (event_npc.cpp); the portraits' frames are drawn with planes.
#include "event_int.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

namespace ev {

namespace {

// [V] role +0x1F4 colours (Role_CreateAvatar @0x480b30: red, yellow, blue, green); seats 4.. get grey
unsigned seatColor(int s){ static const unsigned c[4] = {0xFFE63A43, 0xFFEAC716, 0xFF3344EB, 0xFF18E98C}; return s >= 0 && s < 4 ? c[s] : 0xFFB0B0B0; }

// the dynamic widgets (portrait, frames, name, cash) are created once per UI and reused by every auction
struct Slot { int pic = 0, name = 0, cash = 0, in[4] = {0, 0, 0, 0}, out[4] = {0, 0, 0, 0}; };
struct Slots { Rm8Ui *ui = nullptr; Slot s[8]; } g_slots;

struct VendueTask : Task {
	int terra = -1;			// [V] +0x50
	bool bankrupt = false;		// [V] +0x5c
	int32_t base = 1, price = 1;	// [N] [V] +0x54 / +0x58
	float timer = 0;		// [V] +8
	int state = 0;			// [V] +0x4c
	int last = -1, cur = -1;	// [V] +0x60 (last bidder, -1 none) / +0x64 (the role that decides now)
	float aiTimer = 0;		// [V] +0x70
	bool passed[8] = {false, false, false, false, false, false, false, false};	// [V] +0x68..
	int voice = 0;			// [V] +0x6c
	bool done = false;
	float blink = 0;
	Npc *npc = nullptr, *item = nullptr;
	bool busy(){ return voice > 0 && rm8SndPlaying(voice); }
	int inputSeat() const override { return cur >= 0 ? cur : seat; }	// [N] netplay: the bidder whose turn it is decides
	int n(){ return std::min(rm8TurnRoleCount(), 8); }

	bool elig(int s) const { return s >= 0 && s < rm8TurnRoleCount() && s < 8 && rm8TurnStatus(s) != 7 && !(bankrupt && s == seat); }	// [V] +0x5c: the bankrupt role (the current one) does not bid
	int32_t pi() const { return std::max(1, rm8GamePI()); }	// [N] [V] g_GameState +0x30
	void say(const std::string &line, bool play = true)
	{
		int snd = 0; std::string t = plain(line, &snd);
		rm8UiSetTextByName(ui, "bobo", wrap(t, 9));
		if(play){ rm8SndStop(voice); voice = snd ? rm8SndPlay(snd) : 0; }
	}
	void say(int id, bool play = true){ say(rm8Str(id), play); }
	void priceText()	// [V] Vendue_UpdatePrice @0x4e2e70 / the loader: str 6101 "<str#6101>：%d" in the label curPrice and the auctioneer's speech label
	{
		std::string t = plainFmt(6101, (int)price);
		rm8UiSetTextByName(ui, "curPrice", t);
		rm8UiSetTextByName(ui, "bobo", wrap(t, 9));
	}

	VendueTask(int s, int t, bool bk)
	{
		seat = s; terra = t; bankrupt = bk;
		ui = rm8UiLoad("vendue", RM8UI_DIALOG);
		if(!ui) return;
		base = std::max(1, rm8GameAuctionBase(t)); price = base / 2;	// [N] [V] +0x54 = Terra_CalcPrice(1) / 2; the loader: +0x58 = +0x54 / 2
		last = -1; timer = 1.6f * waitScale();
		if(pi() > 1){	// [V] the loader: the button captions "100 X %d"
			const int amt[3] = {100, 500, 1000}; const char *nm[3] = {"100", "500", "1000"};
			for(int i = 0; i < 3; i++){ char b[32]; snprintf(b, sizeof b, "%d X %d", amt[i], (int)pi()); rm8UiSetTextByName(ui, nm[i], b); }
		}
		else { rm8UiSetTextByName(ui, "100", "100"); rm8UiSetTextByName(ui, "500", "500"); rm8UiSetTextByName(ui, "1000", "1000"); }
		uiTapOnly(ui, {"pass", "100", "500", "1000"});
		for(const char *b : {"pass", "100", "500", "1000"}) rm8UiSetAlignN(ui, b, 1, 1);	// [A] the original button class centres its caption (the port's .ui loader defaults to top left)
		// [V] the presenter (art 0x415) and the item (the plot's building art, or art 0x27c = an empty plot): "NPC" / "Item"
		npc = npcCreate(0x415, ui, "NPC");
		std::string dff, td, an;
		if(rm8GameTerraModelDef(t, dff, td, an)) item = npcCreateModel(dff, td, an, ui, "Item");
		else item = npcCreate(0x27c, ui, "Item");
		if(item){ npcPlay(item, 0, true); npcSetSpin(item, 160.0f, 1.6f); }	// [V] Vendue_RotateImage(dt * 160) @0x4e4fe0
		buildRoles();
		cur = -1; nextBidder();	// [V] +0x64 = -1, then Vendue_NextBidder
		priceText();		// [V] the loader writes the price into curPrice
		if(!bankrupt){ state = 1; say(4301); npcPlay(npc, 0); }	// [V] str 4301 + its voice, action 0
		else { state = 0; say(4309, false); npcPlay(npc, 0); }	// [V] str 4309 "<str#4309>！", action 0, no voice
		rm8UiOnClickName(ui, [this](const std::string &nm){ click(nm); });
		rm8UiShow(ui, true);
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: %s auction of plot %d: base %d start %d", bankrupt ? "bankruptcy" : "public", t, base, price);
	}
	~VendueTask() override
	{
		npcDestroy(npc); npcDestroy(item);
		if(voice) rm8SndStop(voice);
		if(g_slots.ui == ui) for(Slot &sl : g_slots.s){ const int ids[2] = {sl.pic, sl.name}; for(int id : ids) rm8UiSetVisible(ui, id, false); rm8UiSetVisible(ui, sl.cash, false); for(int k = 0; k < 4; k++){ rm8UiSetVisible(ui, sl.in[k], false); rm8UiSetVisible(ui, sl.out[k], false); } }
		rm8UiShow(ui, false); rm8UiOnClickName(ui, nullptr);
	}
	bool ok() const { return ui != nullptr; }
	bool ready() const override { return state == 4 && cur >= 0 && rm8TurnHuman(cur); }

	// [V] the role pictures under "rolePlane" (245, 270, 327 x 145 in the dialog root): portrait 64 x 64 at y 0, name "%s\n%dP" at y 68, cash (str 6102 + "\n" + cash) at y 100
	void buildRoles()
	{
		if(g_slots.ui != ui){ g_slots = Slots(); g_slots.ui = ui; }
		int cnt = 0; for(int s = 0; s < n(); s++) if(elig(s)) cnt++;
		cnt = std::max(cnt, 1);
		const float RX = 245, RY = 270, RW = 327;
		for(int s = 0; s < 8; s++){
			Slot &sl = g_slots.s[s];
			bool mk = !sl.pic;
			if(mk){
				for(int k = 0; k < 4; k++){ sl.out[k] = rm8UiAddWidget(ui, "plane", 0, 0, 0, 4, 4, -1, 0xFFFFFFFF, 16, 12); rm8UiSetIgnore(ui, sl.out[k], true); }
				sl.pic = rm8UiAddWidget(ui, "plane", 0, 0, 0, 64, 64, -1, 0, 16, 10); rm8UiSetIgnore(ui, sl.pic, true);
				for(int k = 0; k < 4; k++){ sl.in[k] = rm8UiAddWidget(ui, "plane", 0, 0, 0, 4, 4, -1, 0xFFFFFFFF, 16, 11); rm8UiSetIgnore(ui, sl.in[k], true); }
				sl.name = rm8UiAddWidget(ui, "label", 0, 0, 0, 80, 32, -1, 0, 14, 12); rm8UiSetIgnore(ui, sl.name, true);
				sl.cash = rm8UiAddWidget(ui, "label", 0, 0, 0, 80, 32, -1, 0, 14, 12); rm8UiSetIgnore(ui, sl.cash, true);
				rm8UiSetAlignN(ui, ("dyn" + std::to_string(sl.name)).c_str(), 1, 1); rm8UiSetAlignN(ui, ("dyn" + std::to_string(sl.cash)).c_str(), 1, 1);
			}
		}
		int k = 0;
		for(int s = 0; s < 8; s++){
			Slot &sl = g_slots.s[s];
			bool on = s < n() && elig(s);
			rm8UiSetVisible(ui, sl.pic, on); rm8UiSetVisible(ui, sl.name, on); rm8UiSetVisible(ui, sl.cash, on);
			for(int j = 0; j < 4; j++){ rm8UiSetVisible(ui, sl.in[j], on); rm8UiSetVisible(ui, sl.out[j], false); }
			if(!on) continue;
			float slotW = (float)(int)(RW / cnt), cx = RX + (int)((k * RW) / cnt) + slotW / 2;	// [V] local_120 * local_c / local_170 + (local_c / local_170) / 2
			float x = cx - 32, y = RY;
			rm8UiSetRect(ui, sl.pic, x, y, 64, 64);
			rm8UiSetPicFile(ui, sl.pic, std::string("prt\\npc\\") + rm8TurnPic(s) + "_12.tga");
			// the 4 px frame inside the portrait rectangle (Ui_DrawRolePictureFrameAnimated @0x4e1bd0) and the outer one of the winner
			rm8UiSetRect(ui, sl.in[0], x, y, 64, 4); rm8UiSetRect(ui, sl.in[1], x, y + 60, 64, 4); rm8UiSetRect(ui, sl.in[2], x, y + 4, 4, 56); rm8UiSetRect(ui, sl.in[3], x + 60, y + 4, 4, 56);
			rm8UiSetRect(ui, sl.out[0], x - 4, y - 4, 72, 3); rm8UiSetRect(ui, sl.out[1], x - 4, y + 65, 72, 3); rm8UiSetRect(ui, sl.out[2], x - 4, y - 1, 3, 66); rm8UiSetRect(ui, sl.out[3], x + 65, y - 1, 3, 66);
			rm8UiSetRect(ui, sl.name, cx - slotW / 2, y + 68, slotW, 32);
			rm8UiSetText(ui, sl.name, std::string(rm8TurnName(s)) + "\\" + std::to_string(s + 1) + "P");
			rm8UiSetRect(ui, sl.cash, cx - slotW / 2, y + 100, slotW, 32);
			rm8UiSetText(ui, sl.cash, rm8Str(6102) + "\\" + std::to_string(rm8GameCash(s)));
			k++;
		}
		colourFrames();
	}
	void colourFrames()	// [V] Ui_DrawRolePictureFrameAnimated: the deciding role flashes white / its colour every 100 ms in state 4; after the hammer the winner's outer frame flashes cyan / white
	{
		bool flash = ((int)(blink * 10.0f)) % 2 == 0;
		for(int s = 0; s < 8; s++){
			Slot &sl = g_slots.s[s];
			if(!sl.pic || s >= n() || !elig(s)) continue;
			unsigned c = seatColor(s);
			if(state == 4 && s == cur && flash) c = 0xFFFFFFFF;
			for(int j = 0; j < 4; j++) rm8UiSetBColor(ui, sl.in[j], c);
			bool win = state > 4 && state != 7 && s == last;
			for(int j = 0; j < 4; j++){ rm8UiSetVisible(ui, sl.out[j], win); if(win) rm8UiSetBColor(ui, sl.out[j], flash ? 0xFF00FFFF : 0xFFFFFFFF); }
		}
	}

	// [V] Vendue_ShowOptions @0x4e25e0: pass is enabled for a human, the bids by its cash against price + PI * 100 / 500 / 1000; nothing for a computer
	void showOptions()
	{
		bool hum = cur >= 0 && rm8TurnHuman(cur);
		int32_t cash = cur >= 0 ? rm8GameCash(cur) : 0;	// [N] int32_t
		rm8UiSetEnabledN(ui, "pass", hum);
		rm8UiSetEnabledN(ui, "100", hum && cash >= pi() * 100 + price);
		rm8UiSetEnabledN(ui, "500", hum && cash >= pi() * 500 + price);
		rm8UiSetEnabledN(ui, "1000", hum && cash >= pi() * 1000 + price);
	}
	int nextBidder()	// [V] Vendue_NextBidder @0x4e27f0
	{
		int N = std::max(1, n());
		for(int guard = 0; guard < 2 * N + 2; guard++){
			cur = (cur + 1) % N;
			if(elig(cur) && !passed[cur]){
				showOptions();
				if(!rm8TurnHuman(cur)) aiTimer = 2.0f;	// [V] +0x70 = 2.0 for a computer
				return cur;
			}
		}
		return -1;
	}
	bool checkWinner()	// [V] Vendue_CheckWinner @0x4e2a60: every active role has passed
	{
		int pass = 0, act = 0;
		for(int s = 0; s < n(); s++){ if(!elig(s)) continue; act++; if(passed[s]) pass++; }
		return pass == act;
	}
	bool findNextBidder()	// [V] Vendue_FindNextBidder @0x4e2c20: nobody else (not passed, not the last bidder) can afford price + PI * 100, and someone has bid
	{
		int c = 0;
		for(int s = 0; s < n(); s++) if(elig(s) && !passed[s] && s != last && pi() * 100 + price <= rm8GameCash(s)) c++;
		return c == 0 && last >= 0;
	}
	void onNoBidders()	// [V] Vendue_OnNoBidders @0x4e2140 (pass)
	{
		int who = cur;
		if(cur >= 0) passed[cur] = true;
		rm8SndPlay(0x93);
		if(checkWinner()){ timer = 0.2f * waitScale(); state = 7; }
		else if(findNextBidder()) state = 5;	// [V] no timer reload here
		else if(nextBidder() < 0){ timer = 0.2f * waitScale(); state = 7; }
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: auction: seat %d passes (price %d, last bidder %d) -> state %d, next %d", who, price, last, state, cur);
	}
	void onBid(int32_t inc)	// [N] [V] Vendue_OnBidAccepted @0x4e2230
	{
		price += inc; last = cur;	// [V] Vendue_UpdatePrice: +0x58 += inc, +0x60 = +0x64
		priceText();
		rm8SndPlay(0x6f);
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: auction: seat %d bids +%d -> %d", cur, inc, price);
		if(findNextBidder()){ timer = 0.2f * waitScale(); state = 5; }
		else nextBidder();
	}
	void aiBid()	// [V] Vendue_AIBid @0x4e22c0
	{
		float ratio = (float)price / (float)base;
		int32_t cash = rm8GameCash(cur), P = pi();	// [N] int32_t
		auto bid = [&](int32_t inc){ if(ratio <= (float)(rnd() % 10 + 1)) onBid(P * inc); else onNoBidders(); };
		if(cash < P * 100 + price) onNoBidders();
		else if(cash < P * 500 + price){
			if(rnd() % 100 < 0x5b) bid(100); else onNoBidders();
		}else if(cash < P * 1000 + price){
			int r = rnd() % 100;
			if(r < 0x47) bid(500); else if(r < 0x5b) bid(100); else onNoBidders();
		}else{
			int r = rnd() % 100;
			if(r < 0x3d) bid(1000); else if(r < 0x51) bid(500); else if(r < 0x5b) bid(100); else onNoBidders();
		}
	}

	void click(const std::string &name) override	// [V] Wonder_OnNumInput @0x4e1ae0 / Wonder_OnUIEvent @0x4e1a90: only in state 4
	{
		if(state != 4 || cur < 0 || !rm8TurnHuman(cur)) return;
		int32_t cash = rm8GameCash(cur);	// [N] int32_t
		if(name == "pass"){ onNoBidders(); return; }
		int32_t inc = name == "100" ? 100 : name == "500" ? 500 : name == "1000" ? 1000 : 0;	// [N] int32_t
		if(!inc || cash < pi() * inc + price) return;	// [V] a disabled button
		onBid(pi() * inc);
	}
	bool skip() override	// [V] CUi_vendue__vf13 @0x4e4f50: timer 0, the voice stopped; always "handled"
	{
		timer = 0;
		if(voice){ rm8SndStop(voice); voice = 0; }
		return true;
	}
	int idOf(const std::string &) override { return -1; }

	bool update(float dt) override
	{
		blink += dt;
		colourFrames();
		if(state == 4 && cur >= 0 && !rm8TurnHuman(cur)){	// [V] a computer decides 2 s after it became the bidder
			aiTimer -= dt;
			if(aiTimer < 0) aiBid();
		}
		timer -= dt;
		if(timer > 0) return false;
		timer = 0;
		if(busy()) return false;
		timer = 1.6f * waitScale();
		switch(state){
		case 0: state = 1; say(4301); npcPlay(npc, 0); break;
		case 1:{
			state = 2;
			// [V] a commercial plot: str 4310 with str 14 (empty) / its Land_GetName; else str 4302 with the street name
			bool com = rm8GameTerraType(terra) == 2;
			say(plainFmt(com ? 4310 : 4302, rm8GameTerraTitle(terra).c_str()), false);
			npcPlay(npc, 1);
			break; }
		case 2: state = 3; say(plainFmt(4303, (int)price), false); npcPlay(npc, 2); break;	// [V] str 4303 "<str#4303>%d元"
		case 3: state = 4; say(4304); rm8SndPlay(0x8e); npcPlay(npc, 3); showOptions(); break;	// [V] str 4304 "請出價！" + voice + sample 0x8e
		case 5:{
			say(4306); rm8SndPlay(0x8f); npcPlay(npc, 4);	// [V] str 4306 "成交！" + voice + sample 0x8f
			rm8TurnEnqueueAt(last, 0, 0x41, terra, (int)price);	// [V] Role_EnqueueMsgAt(0, 0x41, plot, price) on the winner
			int curRole = rm8TurnCurrent();
			if(curRole != last && curRole >= 0){	// [V] the roles turn to each other (0x2E)
				rm8TurnEnqueueAt(curRole, 0, 0x2E, last + 1, 0);
				rm8TurnEnqueueAt(last, 1, 0x2E, curRole + 1, 1);
			}
			if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: auction: plot %d sold to seat %d for %d", terra, last, price);
			state = 6;
			break; }
		case 6: say(4307); npcPlay(npc, 5); state = 10; break;	// [V] str 4307 "<str#4307>！" + voice
		case 7: say(4308, false); state = 10; break;		// [V] str 4308 "<str#4308>！"
		case 10: done = true; break;
		default: break;
		}
		return done;
	}
};

}	// namespace

// [V] Game_UseCard(0x50 / 0x51, plot) via Role_TriggerEvent50or51 @0x487bc0 (message 0x40; kind b == 1 = 0x51)
Task *makeVendueTask(int seat, int terra, bool bankruptcy)
{
	if(terra < 0 || terra >= rm8GameTerraCount()) return nullptr;
	VendueTask *t = new VendueTask(seat, terra, bankruptcy);
	if(!t->ok()){ delete t; return nullptr; }
	return t;
}

}	// namespace ev
