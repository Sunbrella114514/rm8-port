// The commercial buildings of the board: the chain store (market.ui) and the hotel (hotel.ui) of another role, the institute (graduate.ui) of the own role, entered by
// Role_OnArriveCommercialBuilding @0x486260 (Game_SelectCard @0x4a40a0 -> the building object's vf16). The building objects (Card_CreateUI @0x4f5de0: vtables 0x5E6D28 institute,
// 0x5E6C48 chain store, 0x5E6BD8 hotel) share Hotel_BeforeVisitor @0x4f6ba0 (vf16), Hotel_ProcessVisitor @0x4f6c80 (vf1), Hotel_CheckVisitor @0x4f7550 (vf10),
// Hotel_InitiateStay @0x4f7610 (vf12 = the die result), Card_LoadDescription @0x4f60a0 / Card_LoadDescriptionDynamic @0x4f6170 (text + voice + presenter action, timer 2.0);
// the chain store's base price is the sum of the owner's chain store levels * 1000 * PI (@0x4f76d0), the hotel's level * 2000 * PI (@0x4f7720), the hotel's vf27 @0x4f7750 queues
// message 0x13 (CPlayerTag__EnterHotelStatus @0x4852e0: status 3 for one day, cards.cpp). The institute: vf16 Cls_5e6d28__vf16_StartInstituteResearch @0x4f6360, Update @0x4f6630,
// TrySelectCard @0x4f6770 (the ok button), CheckAndTransition @0x4f6800 (cancel), SetAndLoadCard @0x4f6830 (a card button), the daily tick Institute_DisplaySuccess @0x4f6890.
// [V] the dice button is the go.ui single-die panel (Hotel_BeforeVisitor -> UI_CreateMinimap @0x4e7310 moves the go object to the dialog's widget 2 = "Button", whose own picture is hidden,
// Ui_MarketHotel_HideWidget2AndBindCallback @0x4e7370); it starts the single-die scene (Role_StartDiceSceneCount1 @0x484d70, real 3D die, roles.cpp state 4) and reads its result.
// The walk of the role into / out of the hotel (states 0xB / 0xC) is roles.cpp rm8TurnHotelWalk.
#include "event_int.h"
#include "audio.h"
#include "story.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <algorithm>

namespace ev {

namespace {

// ---- the research of the institutes (plain data, saved by the save module): [V] building object +0x14 (card index, -1 none) / +0x18 (days left)
struct Research { int card = -1, days = 0; };
std::map<int, Research> g_res;	// plot index -> research

// [V] the five choices: card ids at 0x5E6DE4 and the days at 0x5E6DF4
const int RES_CARD[5] = {2, 9, 17, 15, 19};
const int RES_DAYS[5] = {2, 3, 5, 7, 10};

// ---- chain store / hotel
struct BuildTask : Task {
	bool hotel; int terra = -1, owner = -1, level = 1;
	float timer = 0;		// [V] +0xC
	int state = 0;			// [V] +0x14
	int die = 0;			// [V] +0x18
	long fee = 0;			// [V] +0x1C
	int voice = 0;			// [V] +0x10
	bool done = false, shown = false;
	Npc *npc = nullptr;
	bool busy(){ return voice > 0 && rm8SndPlaying(voice); }
	// [V] Card_LoadDescription @0x4f60a0: text + voice (str id + the "@" sound) + the presenter's action, timer 2.0
	void describe(const std::string &line, int action, bool voiceToo = true)
	{
		int snd = 0; std::string t = plain(line, &snd);
		rm8UiSetTextByName(ui, "Label", wrap(t, 13));
		npcPlay(npc, action);
		if(voiceToo){ rm8SndStop(voice); voice = snd ? rm8SndPlay(snd) : 0; }
		timer = 2.0f * waitScale();
	}
	int sid(int n) const { return (hotel ? 5100 : 5000) + n; }	// [V] vf25: param + 5000 / + 0x13EC (5100)
	long base() const	// [V] vf17: the chain store = the sum of the owner's chain store levels * 1000 * PI; the hotel = its level * 2000 * PI
	{
		long pi = std::max(1L, rm8GamePI());
		if(hotel) return (long)level * 2000 * pi;
		long sum = 0;
		for(int t = 0; t < rm8GameTerraCount(); t++) if(rm8GameTerraOwner(t) == owner && rm8GameTerraType(t) == 2 && rm8GameTerraBuild(t) == 3) sum += rm8GameTerraLevel(t);
		return sum * 1000 * pi;
	}
	BuildTask(int s, int t, bool h)
	{
		seat = s; terra = t; hotel = h;
		owner = rm8GameTerraOwner(t); level = rm8GameTerraLevel(t);
		ui = rm8UiLoad(h ? "hotel" : "market", RM8UI_DIALOG);
		if(!ui) return;
		uiTapOnly(ui, {"Button"});
		rm8UiSetAlignN(ui, "Label", 1, 1); rm8UiSetAlignN(ui, "Button", 1, 1);

		rm8UiSetEnabledN(ui, "Button", true);
		npc = npcCreate(h ? 0x41E : 0x41D, ui, "Title");
		rm8UiOnClickName(ui, [this](const std::string &n){ click(n); });
		// [V] Hotel_BeforeVisitor @0x4f6ba0: the first text (5001 / 5101), action 0, state 1
		describe(rm8Str(sid(1)), 0);
		timer = 2.0f * waitScale();
		state = 1;
		rm8UiShow(ui, true); shown = true;
		if(rm8TurnHuman(s)) showDieGo(this, "Button");	// [V] the go panel (one die) replaces the dice button
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: %s of seat %d (plot %d level %d) visited by seat %d: base %ld", h ? "hotel" : "chain store", owner, t, level, s, base());
	}
	~BuildTask() override { hideDieGo(this); npcDestroy(npc); if(voice) rm8SndStop(voice); rm8UiShow(ui, false); rm8UiOnClickName(ui, nullptr); }
	bool ok() const { return ui != nullptr; }
	bool ready() const override { return state == 1 || state == 2 || state == 10; }
	bool skip() override { if(state == 20) return false; timer = 0; if(voice){ rm8SndStop(voice); voice = 0; } return true; }
	// [H] vf13 = Card_UpdateVisualization @0x4f5ff0: timer 0, the voice stopped (skip() above; not while the die rolls)

	void click(const std::string &n) override
	{
		if((n != "Button" && n != "go") || !(state == 1 || state == 2 || state == 10) || rm8TurnHuman(seat) == false) return;	// [V] Hotel_CheckVisitor @0x4f7550: states 1, 2, 10
		if(rm8TurnEventDieStart(seat)){	// [V] Ui_OnEvent_StartDiceScene @0x4e8a80 -> Role_StartDiceSceneCount1 @0x484d70: the real 3D die; only the go panel is hidden (@0x484d70), the dialog and the clerk stay while it rolls
			hideDieGo(this);
			timer = 0; if(voice){ rm8SndStop(voice); voice = 0; }
			state = 20;
			return;
		}
		roll();
	}
	void roll()	// [A] the single-die scene's result -> Hotel_InitiateStay @0x4f7610
	{
		die = rnd() % 6 + 1;
		timer = 0; if(voice){ rm8SndStop(voice); voice = 0; }	// [V] vf13: timer 0, the voice stopped
		hideDieGo(this);
		state = 3;
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: %s: die %d", hotel ? "hotel" : "chain store", die);
	}
	bool update(float dt) override
	{
		if(state == 20){	// the die rolls: its value arrives from roles.cpp (the role is back in state 9)
			int v = rm8TurnEventDieTake(seat);
			if(!v) return false;
			die = v; rm8UiShow(ui, true);
			state = 3; timer = 0.3f * waitScale();
			if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: %s: 3D die %d", hotel ? "hotel" : "chain store", die);
			return false;
		}
		timer -= dt;
		if(timer > 0) return false;
		timer = 0;
		if(busy()) return false;
		timer = 1.2f * waitScale();
		switch(state){
		case 1: describe(rm8Str(sid(2)), 1); state = 2; break;			// [V] "請選擇…" action 1
		case 2: state = 10; break;
		case 10: if(!rm8TurnHuman(seat)) roll(); break;				// [V] a computer: vf12(rand() % 6 + 1, 1)
		case 3: {									// [V] the base price and the die: str 5003 / 5103 (die, base), action 1, no voice of its own
			long b = base(); fee = b;
			describe(plainFmt(sid(3), die, (int)b), 1, false);
			state = 4; break; }
		case 4:										// [V] fee = base * die: str 5004 / 5104, action 2 + voice
			fee *= die;
			describe(plainFmt(sid(4), (int)fee), 2);
			state = 0xB; break;
		case 0xB: {									// [V] the adjustments of the visitor (the current role)
			int sh = rm8GameShape(seat);
			auto note = [&](int strId){ rm8UiShow(ui, false); rm8HudInfo(plain(rm8Str(strId))); shown = false; };
			if(rm8CardsHasBodyguard(seat)){ fee = 0; note(556); }
			else if(sh == 1){ fee = 0; note(407); }
			else if(sh == 2){ fee <<= 1; note(409); }
			else if(rm8StoryActive() && seat == 0 && rm8TurnCharId(seat) == 2){ fee = fee * 7 / 10; note(53); }	// [V] 萊娜's 7/10 VIP discount
			state = 5; break; }
		case 5:										// [V] pay (cash first, with the emote), the owner gets it on the deposit; str 5005 / 5105, action 3
			if(fee > 0){
				rm8TurnEnqueue(seat, 0x16, (int)fee, 0);			// Role_SubMoney(fee, cash first, emote)
				if(owner >= 0) rm8GameAddMoney(owner, fee, true);			// Role_AddMoney(fee, deposit, no emote)
			}
			rm8HudCloseModal();
			describe(rm8Str(sid(5)), 3);
			state = 6; break;
		case 6:
			if(hotel) rm8TurnEnqueue(seat, 0x13, 0, 0);				// [V] Cls_5e6bd8__vf27 @0x4f7750: the stay
			done = true; break;
		default: break;
		}
		return done;
	}
};

// ---- the institute
struct InstituteTask : Task {
	int terra = -1, level = 1;
	float timer = 0;	// [V] +0xC
	int state = 0;		// [V] +0x20
	int sel = 0;		// [V] +0x1C: the chosen button 1..5
	int voice = 0;
	bool done = false;
	int hover = -2;
	Npc *npc = nullptr;
	bool busy(){ return voice > 0 && rm8SndPlaying(voice); }
	void describe(int strId, int action)	// [V] Card_LoadDescription: text (str 4101..4103) + voice + action, timer 2.0
	{
		int snd = 0; std::string t = plain(rm8Str(strId), &snd);
		rm8UiSetTextByName(ui, "Label2", wrap(t, 20));
		npcPlay(npc, action);
		rm8SndStop(voice); voice = snd ? rm8SndPlay(snd) : 0;
		timer = 2.0f * waitScale();
	}
	static const char *btn(int i){ static const char *n[5] = {"Button21", "Button22", "Button23", "Button24", "Button2"}; return n[i]; }
	std::string cardKey(int id){ char k[64]; snprintf(k, sizeof k, "prt\\card\\D%02d.tga", id); return k; }
	std::string desc(int i){ return plainFmt(5901 + i, RES_DAYS[i]); }	// [V] UI_ShowGraduate @0x4e7060: str 5901 + i with the research days
	int cardIndexOf(int id) const { for(int i = 0; i < 5; i++) if(RES_CARD[i] == id) return i; return -1; }
	void showResearch()	// [V] Institute_LoadCardImage @0x4f6a20: the picture of the card now being researched
	{
		Research &r = g_res[terra];
		if(r.card >= 0) rm8UiSetPicFileN(ui, "Title2", cardKey(r.card + 1)); else rm8UiSetPicFileN(ui, "Title2", "");
	}
	InstituteTask(int s, int t)
	{
		seat = s; terra = t; level = std::max(1, std::min(5, rm8GameTerraLevel(t)));
		if(!rm8TurnHuman(s)){ aiPick(); done = true; return; }	// [V] the computer branch of vf16: a random card, str 263, state 5
		ui = rm8UiLoad("graduate", RM8UI_DIALOG);
		if(!ui) return;
		uiTapOnly(ui, {"Button", "Button1", "Button21", "Button22", "Button23", "Button24", "Button2", "Title2"});
		rm8UiSetAlignN(ui, "Label2", 1, 1); rm8UiSetAlignN(ui, "Label", 1, 1);
		rm8UiSetTextByName(ui, "Label", "");
		for(int i = 0; i < 5; i++){	// [V] the choices up to the building's level show the card pictures, the others are hidden
			bool on = i < level;
			rm8UiSetVisibleN(ui, btn(i), on); rm8UiSetEnabledN(ui, btn(i), on);
			if(on) rm8UiSetPicFileN(ui, btn(i), cardKey(RES_CARD[i]));
		}
		showResearch();
		npc = npcCreate(0x413, ui, "Title");
		rm8UiOnClickName(ui, [this](const std::string &n){ click(n); });
		describe(4101, 0);		// [V] vf21(0x1005, 0): str 4101
		state = 1;
		rm8UiShow(ui, true);
	}
	~InstituteTask() override { if(ui){ npcDestroy(npc); if(voice) rm8SndStop(voice); rm8UiShow(ui, false); rm8UiOnClickName(ui, nullptr); } }
	bool ok() const { return done || ui != nullptr; }
	bool ready() const override { return state == 2; }
	void aiPick()	// [V] Cls_5e6d28__vf16 (computer): sel = rand() % level + 1, TrySelectCard, str 263 "%s<str#263>\研發%s" queued at the front
	{
		int s = rnd() % level;
		select(s);
		int id = RES_CARD[s];
		queueTextAt(seat, 0, plainFmt(263, rm8TurnName(seat), rm8CardName(id) ? rm8CardName(id) : ""));
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: institute (plot %d): seat %d researches card %d", terra, seat, id);
	}
	void select(int i)	// [V] Cls_5e6d28__vf10_TrySelectCard @0x4f6770: another card than the current one: +0x14 = its index, +0x18 = its days
	{
		Research &r = g_res[terra];
		int idx = RES_CARD[i] - 1;
		if(idx != r.card){ r.card = idx; r.days = RES_DAYS[i]; }
	}
	bool skip() override { timer = 0; if(voice){ rm8SndStop(voice); voice = 0; } return true; }
	void click(const std::string &n) override
	{
		if(state != 2) return;
		for(int i = 0; i < 5; i++) if(n == btn(i) && i < level){	// [V] Graduate_OnUIEvent @0x4e71a0 -> vf12(i + 1): the chosen card's picture in Title2
			sel = i + 1;
			rm8UiSetPicFileN(ui, "Title2", cardKey(RES_CARD[i]));
			return;
		}
		if(n == "Button"){ if(sel >= 1 && sel <= 5){ select(sel - 1); state = 3; } return; }	// [V] ok: TrySelectCard (needs a choice)
		if(n == "Button1"){ state = 3; return; }	// [V] cancel: CheckAndTransition
	}
	bool update(float dt) override
	{
		if(done) return true;
		if(state == 2){	// [V] MouseOn callbacks 5 / 6: the card's description in the label (UI_ShowGraduate / UI_HideGraduate)
			int h = -1;
			for(int i = 0; i < level && h < 0; i++) if(rm8UiHotN(ui, btn(i))) h = i;
			if(h < 0 && rm8UiHotN(ui, "Title2")){ int ci = cardIndexOf(g_res[terra].card + 1); if(ci >= 0) h = ci; }
			if(h != hover){ hover = h; rm8UiSetTextByName(ui, "Label", h >= 0 ? wrap(desc(h), 20) : ""); }
		}
		if(state == 5) return true;
		timer -= dt;
		if(timer > 0) return false;
		timer = 0;
		if(busy()) return false;
		timer = 1.2f * waitScale();
		if(state == 1){ describe(4102, 1); state = 2; }		// [V] 0x1006: "<str#4102>" action 1
		else if(state == 3){ describe(4103, 2); state = 4; }		// [V] 0x1007: "<str#4103>！" action 2
		else if(state == 4){ state = 5; }
		return state == 5;
	}
};

}	// namespace

Task *makeBuildingTask(int seat, int terra)
{
	if(terra < 0 || terra >= rm8GameTerraCount() || rm8GameTerraType(terra) != 2) return nullptr;
	int b = rm8GameTerraBuild(terra);
	if(b == 3 || b == 4){
		BuildTask *t = new BuildTask(seat, terra, b == 4);
		if(!t->ok()){ delete t; return nullptr; }
		return t;
	}
	if(b == 2){
		InstituteTask *t = new InstituteTask(seat, terra);
		if(!t->ok()){ delete t; return nullptr; }
		return t;
	}
	return nullptr;
}

// [V] Institute_DisplaySuccess @0x4f6890 for every institute (TurnStart_UpdateTerrain @0x4f11f0, once per new day): days - 1; when none are left the owner gets the card
// ("<str#4104>" + the card name as a text, message 4 (card, 1)) and the research is cleared
void instituteTick()
{
	for(auto &kv : g_res){
		Research &r = kv.second;
		int t = kv.first;
		if(r.card < 0) continue;
		if(--r.days < 1){
			int owner = rm8GameTerraOwner(t);
			if(owner >= 0 && owner < rm8TurnRoleCount() && rm8GameTerraBuild(t) == 2){
				const char *nm = rm8CardName(r.card + 1);
				queueText(owner, plain(rm8Str(4104)) + "\\\\" + (nm ? nm : ""));
				rm8TurnEnqueue(owner, 4, r.card, 1);
				if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: institute (plot %d) finished card %d for seat %d", t, r.card + 1, owner);
			}
			r.card = -1; r.days = 0;
		}
	}
}

void instituteClear(){ g_res.clear(); }

// the research state for the save module: plot -> {card index, days left}
bool instituteGet(int terra, int &card, int &days){ auto it = g_res.find(terra); if(it == g_res.end()) return false; card = it->second.card; days = it->second.days; return true; }
void instituteSet(int terra, int card, int days){ g_res[terra].card = card; g_res[terra].days = days; }

}	// namespace ev

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_build.inc"
