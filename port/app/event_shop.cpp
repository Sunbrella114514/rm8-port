// Card shop (event 1), after the task object GameModeMgr_InitContainer (vtable 0x5E4A10; its names are wrong, it is the card shop):
// loader Cls_5e4a10__vf16__Setup @0x4e6180 (= CardShop_ResetState @0x4e59f0 + vf16__Init @0x4e5aa0), Update @0x4e6240, buying
// CardShop_OnCardPurchase @0x4e53b0, closing CardShop_OnCloseDialog @0x4e6c30, price colours @0x4e6d00, price CardShop_CalcCardPrice
// @0x4e6f90; computer: Card_BuySell @0x4fc8d0 with Card_SelectCheap @0x4fd1b0.
// [A] not drawn: the 3D vending machine / clerk (article 0x41f rendered into the "NPC" plane) and the hop of the "hideCard" label
//     (the original sets its Y to rand() every frame during the 0.6 s the card travels).
#include "event_int.h"
#include "audio.h"
#include <cstdio>
#include <algorithm>

namespace ev {

namespace {

const int SLOTS = 12;

struct ShopTask : Task {
	float timer = 0;		// [V] +4
	int phase = 0;			// [V] +0x64: 0 welcome, 1 hint, 2 shopping, 6 closing
	int slot[SLOTS];		// [V] +0x1C: card indices (id - 1), Card_SelectByDifficulty(1) each
	bool sold[SLOTS] = {};
	int pending = -1;		// [V] +0x180: the slot being bought
	float pendT = 0;		// [V] +0x17C: 0.6 s card flight
	float hx = 0, hy0 = 0, hw = 0, hh = 0, hy = 0;	// the falling card widget "hideCard": start rect and current Y
	float flashT = 0;		// [V] +0x184: 0.6 s "cannot buy" flash of Plane1 (picture 0x208 / 0x207)
	int voice = 0;
	int hover = -1;
	Npc *npc = nullptr;		// [V] article 0x41F (event_npc.cpp)

	void txt(const char *n, const std::string &s){ rm8UiSetTextByName(ui, n, s); }
	bool voiceBusy(){ return voice > 0 && rm8SndPlaying(voice); }
	// [V] vf13 Cls_5e4a10__vf13 @0x4E6A20: not while shopping (phase +0x64 == 2, no voice +0x50); else timer (+4) = 0 and the voice stopped
	bool skip() override { if(phase == 2 && !voiceBusy()) return false; timer = 0; if(voice){ rm8SndStop(voice); voice = 0; } return true; }
	void speak(int strId, bool voiceToo = true)
	{
		int snd = 0; std::string t = plain(rm8Str(strId), &snd);
		txt("bobo", wrap(t, 11));
		if(voiceToo){ rm8SndStop(voice); voice = snd ? rm8SndPlay(snd) : 0; }
	}
	static std::string nm(const char *base, int i){ char b[32]; snprintf(b, sizeof b, "%s%d", base, i); return b; }
	// [V] CardShop_CalcCardPrice @0x4e6f90: the card's ticket price; the card wonder (event_wonder.cpp): the owner pays 80 %, the others 120 %
	int price(int s){ return rm8EventCardPrice(seat, cardPrice(slot[s] + 1)); }
	// [V] CardShop_UpdatePriceColors @0x4e6d00
	void colors()
	{
		for(int i = 0; i < SLOTS; i++){
			unsigned c = rm8GamePoints(seat) < price(i) ? 0xff9b9b9b : 0xff000000;
			rm8UiSetColorN(ui, nm("cardPrice", i).c_str(), c); rm8UiSetColorN(ui, nm("Label", i).c_str(), c);
		}
	}
	void tickets(){ txt("ticket", std::to_string(rm8GamePoints(seat))); }

	ShopTask(int s)
	{
		seat = s;
		ui = rm8UiLoad("cardShop", RM8UI_DIALOG);
		if(!ui) return;
		// [V] CardShop_ResetState + vf16__Init
		timer = 1.6f * waitScale();
		for(int i = 0; i < SLOTS; i++){ slot[i] = cardSelectByDifficulty(1); if(slot[i] < 0) slot[i] = 0; }
		npc = npcCreate(0x41F, ui, "NPC"); npcPlay(npc, 0);
		speak(5201);
		for(int i = 0; i < SLOTS; i++){
			int id = slot[i] + 1;
			char key[64]; snprintf(key, sizeof key, "prt\\card\\D%02d.tga", id);
			rm8UiSetVisibleN(ui, nm("card", i).c_str(), true); rm8UiSetVisibleN(ui, nm("Label", i).c_str(), true);
			rm8UiSetPicFileN(ui, nm("card", i).c_str(), key);
			txt(nm("card", i).c_str(), "");
			txt(nm("cardPrice", i).c_str(), rm8CardName(id));		// the card name (card +4)
			txt(nm("Label", i).c_str(), std::to_string(price(i)));	// the price
		}
		tickets(); colors();
		uiTapOnly(ui, {"Exit"});
		for(int i = 0; i < SLOTS; i++){ rm8UiSetIgnoreN(ui, nm("card", i).c_str(), false); rm8UiSetEnabledN(ui, nm("card", i).c_str(), true); }	// [V] vf16__Init @0x4e5aa0: UiLib_Widget_SetEnabled(1) on every card (the .ui has Enabled=false)
		rm8UiOnClickName(ui, [this](const std::string &n){ click(n); });
		rm8UiShow(ui, true);
		rm8CardsSetSellMode(true);	// [V] DAT_00640958 = the shop task: a click on a card of the bar sells it for half its price (Card_OnSelected @0x4e9340 -> Role_SellCardForTickets @0x488cf0)
	}
	~ShopTask() override { rm8CardsSetSellMode(false); npcDestroy(npc); if(voice) rm8SndStop(voice); rm8UiShow(ui, false); rm8UiOnClickName(ui, nullptr); }
	bool ok() const { return ui != nullptr; }
	bool ready() const override { return phase == 2; }

	// [V] CardShop_OnCloseDialog @0x4e6c30 (arg 1.3: the Exit button, 1.2: vf11)
	void closeDialog(float scale)
	{
		if(phase == 6) return;
		speak(5205); npcPlay(npc, 4);
		timer = scale * waitScale();
		phase = 6;
	}

	void click(const std::string &n) override
	{
		if(getenv("RM8_DEBUG_EVENTS")){ int k = -1; sscanf(n.c_str(), "card%d", &k); rm8Log("events: shop tap %s (phase %d%s, points %ld)", n.c_str(), phase, k >= 0 && k < SLOTS ? (std::string(", card ") + std::to_string(slot[k] + 1) + " price " + std::to_string(price(k))).c_str() : "", rm8GamePoints(seat)); }
		if(n == "Exit"){ closeDialog(1.3f); return; }
		int i = -1;
		if(sscanf(n.c_str(), "card%d", &i) != 1) return;	// [V] CardShop_OnCardSelect @0x4e51b0: only the "card%d" widgets
		if(i < 0 || i >= SLOTS || sold[i] || phase >= 6) return;
		purchase(i);
	}

	// [V] CardShop_OnCardPurchase @0x4e53b0: only while no card is in flight
	void purchase(int i)
	{
		if(pending >= 0) return;
		if(voice){ rm8SndStop(voice); voice = 0; }
		if(rm8GamePoints(seat) < price(i)){ speak(5204); npcPlay(npc, 3); flashT = 0.6f; return; }		// 0x5150: not enough tickets
		if(rm8CardCount(seat) >= 8){ speak(5203); npcPlay(npc, 2); flashT = 0.6f; return; }			// 0x514C: hand full (the slot count is 8)
		// UpdateUIDisplay(i): the card, its name and price disappear; the card flies for 0.6 s in "hideCard"
		char key[64]; snprintf(key, sizeof key, "prt\\card\\D%02d.tga", slot[i] + 1);
		rm8UiSetVisibleN(ui, nm("card", i).c_str(), false);
		txt(nm("cardPrice", i).c_str(), "");
		rm8UiSetVisibleN(ui, nm("Label", i).c_str(), false);
		rm8UiSetPicFileN(ui, "hideCard", key);
		rm8UiGetRectN(ui, "hideCard", hx, hy0, hw, hh); hy = hy0;	// [V] +0x74 = its start Y (the dispenser slot), +0x78 = the current Y (loader @0x4e5aa0)
		rm8UiSetVisibleN(ui, "hideCard", true);
		rm8SndPlay(0x8D);
		flashT = 0; pendT = 0.6f; pending = i;
	}

	// [V] Cls_5e4a10__vf1__Update @0x4e6240
	bool update(float dt) override
	{
		tickets();
		{ static long lastPts = -1; if(rm8GamePoints(seat) != lastPts){ lastPts = rm8GamePoints(seat); colors(); } }	// [V] CardShop_UpdatePriceColors @0x4e6d00 after a sale (Card_OnSelected)
		rm8UiSetPicN(ui, "Plane1", flashT > 0 ? 0x208 : 0x207);
		flashT -= dt;
		if(pendT > 0 && pending >= 0){	// [V] Cls_5e4a10__vf1__Update @0x4e6240: while the flight timer (+0x17c, 0.6 s) runs the card widget falls: Y = ftol(Y + height * 2.7 * dt) into the tray (cardOut)
			hy = (float)(int)(hy + hh * 2.7f * dt);
			rm8UiSetRectN(ui, "hideCard", hx, hy, hw, hh);
		}
		if(pendT <= 0){
			if(pending >= 0){	// Card__TriggerApproxCondition @0x4900d0: add the card, then Role_SubTicket(price)
				int id = slot[pending] + 1, p = price(pending);
				if(rm8CardAdd(seat, id)){ subTickets(seat, p); rm8Log("events: seat %d buys card %d for %d", seat, id, p); }
				sold[pending] = true; pending = -1;
				rm8UiSetRectN(ui, "hideCard", hx, hy0, hw, hh); rm8UiSetVisibleN(ui, "hideCard", false);	// [V] SetPosY(start Y) when the card is handed over
				colors();
			}
		}
		pendT -= dt;
		// [V] MouseOn callbacks 5 / 6 (phase 2): the card description (string 6501 + index) in the bubble; leaving shows str 5202
		if(phase == 2){
			int h = -1;
			for(int i = 0; i < SLOTS && h < 0; i++) if(!sold[i] && rm8UiHotN(ui, nm("card", i).c_str())) h = i;
			if(h != hover){
				hover = h;
				if(h >= 0) txt("bobo", wrap(plain(rm8Str(6501 + slot[h])), 11));
				else speak(5202, false);
			}
		}
		timer -= dt;
		if(timer <= 0 && !voiceBusy()){
			timer = 1.6f * waitScale();
			if(phase == 0){ phase = 1; speak(5202); npcPlay(npc, 1); }
			else if(phase == 1){	// [V] Cls_5e4a10__vf1__Update @0x4e6240 state 1: with a card wonder (Wonder_CheckConstruction(2) >= 0) the bubble says str 5206 (the owner) / 5207
				phase = 2;
				if(rm8EventWonderOwner(2) >= 0) txt("bobo", wrap(plain(rm8Str(rm8EventWonderOwner(2) == seat ? 5206 : 5207)), 11));
			}
			else if(phase == 6) return true;
		}
		return false;
	}
};

// [V] Card_SelectCheap @0x4fd1b0 (difficulty = InitTxt_GetParam(7)); the 11 favourites are the table at 0x5E6F00
int selectCheap(int seat)
{
	static const int FAV[11] = {0, 1, 2, 8, 11, 12, 17, 24, 26, 31, 34};
	int diff = difficulty();
	if(!(diff > 1 || rnd() % 3 == 1)) return -1;
	if(diff < 3) return cardSelectByDifficulty(1);
	long tickets = rm8GamePoints(seat);
	int c = -1, tries = 0;
	if(rnd() % 2 == 1){
		int cand[11], n = 0;
		for(int idx : FAV) if(cardPrice(idx + 1) < tickets && !holdsCard(seat, idx + 1)) cand[n++] = idx;
		if(n > 0) c = cand[rnd() % n];
	}
	while(c < 0){
		int idx = cardSelectByDifficulty(1);
		if(idx >= 0 && cardPrice(idx + 1) < tickets && !holdsCard(seat, idx + 1)) c = idx;
		tries++;
		if(tries > 10) return c;
		if(tickets < 30) return c;
	}
	return c;
}

}	// namespace

Task *makeShopTask(int seat)
{
	ShopTask *t = new ShopTask(seat);
	if(!t->ok()){ delete t; return nullptr; }
	return t;
}

// [V] Card_BuySell @0x4fc8d0: the computer buys cards until it holds rand() % 4 + 5 of them, cheaply chosen, while tickets exceed the
// price; str 281 "%s<str#281>" after a 30 frame wait
bool aiCardShop(int seat)
{
	bool bought = false;
	int target = rnd() % 4 + 5;
	while(rm8CardCount(seat) < target){
		int idx = selectCheap(seat);
		if(idx < 0) break;
		int id = idx + 1, p = cardPrice(id);
		if(rm8GamePoints(seat) <= p) break;
		if(!rm8CardAdd(seat, id)) break;
		subTickets(seat, p);
		bought = true;
	}
	if(!bought) return false;
	rm8TurnEnqueueAt(seat, 0, 0x65, 0x1E, 0);
	queueTextAt(seat, 1, plainFmt(281, rm8TurnName(seat)));
	return true;
}

}	// namespace ev
