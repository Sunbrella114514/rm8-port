// Court (event 0xE, "court.ui"), after the task object VenduePlace_InitContainer @0x4b3260 (vtable 0x5E4860, the name is wrong: it is the court;
// vf16 CUi_court__vf16_OnLoad @0x4d4de0, vf1 Update CUi_court__vf1_Update @0x4d5a60, vf11 @0x4d62d0, Court_ProcessAccusal @0x4d6510) and
// the computer path AI_Event_Court @0x4f8810. Bail / prison: message 0x5d CPlayerTag__OnPrisonBailDeposit @0x492ac0, 0x5e @0x492c90.
// [V] state machine (+0x90): 0 welcome -> 1 pick the defendant -> 2 pick the charge -> 3 (0.3 s) charge text -> 6 verdict -> 7 end.
// [A] the OrthoCam of the original is replaced by the event_npc.cpp presenter raster (judge article 0x411, the two dolls = roleinfo artId1).
#include "event_int.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace ev {

namespace {

// the charges (+0x7c): 1 "Com" (contempt of the computer: 5 days), 2 "Card" (card abuse: 3 days), 3 "Land" (forced rent: 5000)
int chargeNameStr(int c){ return c == 1 ? 4030 : c == 2 ? 4032 : 4031; }	// [V] Label2 / Label1 / Label3 texts (loader @0x4d4de0)

std::vector<int> g_pic, g_name;	// dynamic RolePic%d / RoleName%d widgets, created once (the dialog is cached)

struct CourtTask : Task {
	float timer = 0;		// [V] +8
	int state = 0;			// [V] +0x90
	int accused = -1;		// [V] +0x78
	int charge = 0;			// [V] +0x7c
	int voice = 0;			// [V] +0x74
	Npc *judge = nullptr, *accuserDoll = nullptr, *accusedDoll = nullptr;
	std::vector<int> picOf;		// widget id -> seat (index = position in g_pic)
	int hover = 0;

	bool busy(){ return voice > 0 && rm8SndPlaying(voice); }
	// [V] vf13 CUi_court__vf13 @0x4D7950: not while the player picks (state +0x90 == 1 or 2, no voice +0x74); else timer (+8) = 0 and the voice stopped
	bool skip() override { if((state == 1 || state == 2) && !busy()) return false; timer = 0; if(voice){ rm8SndStop(voice); voice = 0; } return true; }
	void txt(const std::string &s){ rm8UiSetTextByName(ui, "bobo", wrap(s, 7)); }
	void say(const std::string &line, bool speakIt = true)	// "text@sound"
	{
		int snd = 0; std::string t = plain(line, &snd);
		txt(t);
		if(speakIt){ rm8SndStop(voice); voice = snd ? rm8SndPlay(snd) : 0; }
	}
	void say(int strId){ say(rm8Str(strId)); }

	CourtTask(int s)
	{
		seat = s;
		ui = rm8UiLoad("court", RM8UI_DIALOG);
		if(!ui) return;
		timer = 1.6f * waitScale();
		uiTapOnly(ui, {"quit", "Com", "Card", "Land"});
		// [V] the defendants: every other active role as a RolePic%d portrait (64x64 at y 0x28) and a name label "%s\n%dP" (y 0x6a)
		std::vector<int> targets;
		for(int k = 0; k < rm8TurnRoleCount(); k++) if(k != seat && rm8TurnStatus(k) != 7) targets.push_back(k);
		const float PW = 209, PX = 177, PY = 14;	// rolePlane size / position in the dialog root (Plane1 +3, rolePlane +11)
		for(size_t k = 0; k < targets.size() && k < 8; k++){
			if(g_pic.size() <= k){
				g_pic.push_back(rm8UiAddWidget(ui, "button", 0, 0, 0, 64, 64, -1, 0, 16, 9));
				g_name.push_back(rm8UiAddWidget(ui, "label", 0, 0, 0, 64, 32, -1, 0, 14, 9));
				rm8UiSetIgnore(ui, g_name.back(), true);
			}
			int n = (int)targets.size();
			float x = PX + (k * PW) / n - 32 + (PW / n) / 2;	// [V] (index * width) / count - 0x20 + (width / count) / 2
			rm8UiSetRect(ui, g_pic[k], x, PY + 40, 64, 64);
			rm8UiSetRect(ui, g_name[k], x, PY + 106, 64, 32);
			rm8UiSetPicFile(ui, g_pic[k], std::string("prt\\npc\\") + rm8TurnPic(targets[k]) + "_12.tga");
			rm8UiSetText(ui, g_name[k], std::string(rm8TurnName(targets[k])) + "\\" + std::to_string(targets[k] + 1) + "P");
			rm8UiSetVisible(ui, g_pic[k], true); rm8UiSetVisible(ui, g_name[k], true);
			rm8UiSetEnabled(ui, g_pic[k], true);
		}
		picOf = targets;
		for(size_t k = targets.size(); k < g_pic.size(); k++){ rm8UiSetVisible(ui, g_pic[k], false); rm8UiSetVisible(ui, g_name[k], false); }
		rm8UiSetVisibleN(ui, "accusalPlane", false); rm8UiSetVisibleN(ui, "rolePlane", true);
		rm8UiSetTextByName(ui, "Label2", wrap(plain(rm8Str(4030)), 2)); rm8UiSetTextByName(ui, "Label1", wrap(plain(rm8Str(4032)), 2)); rm8UiSetTextByName(ui, "Label3", wrap(plain(rm8Str(4031)), 2));	// [A] the 41 px labels wrap after two characters
		judge = npcCreate(0x411, ui, "judge");
		accuserDoll = npcCreate(0, ui, "accuserPic", rm8TurnCharId(seat), rm8TurnOutfit(seat));
		npcPlay(judge, 0); npcPlay(accuserDoll, 2, true);
		say(4001);
		rm8UiOnClick(ui, [this](int id){ tap(id); });
		rm8UiShow(ui, true);
		if(targets.empty()){ state = 7; timer = 0; }
	}
	~CourtTask() override
	{
		npcDestroy(judge); npcDestroy(accuserDoll); npcDestroy(accusedDoll);
		if(voice) rm8SndStop(voice);
		for(size_t k = 0; k < g_pic.size(); k++){ rm8UiSetVisible(ui, g_pic[k], false); rm8UiSetVisible(ui, g_name[k], false); }
		rm8UiShow(ui, false); rm8UiOnClick(ui, nullptr);
	}
	bool ok() const { return ui != nullptr; }
	bool ready() const override { return state == 1 || state == 2; }

	// [V] Court_OnAccuserSelected @0x4d4c90 (state 1) + Court_SwitchToChargesPanel @0x4d6440
	void pick(int target)
	{
		accused = target;
		rm8SndPlay(0x6f);
		state = 2;
		rm8SndStop(voice); voice = 0;
		rm8UiSetVisibleN(ui, "rolePlane", false);
		for(size_t k = 0; k < g_pic.size(); k++){ rm8UiSetVisible(ui, g_pic[k], false); rm8UiSetVisible(ui, g_name[k], false); }
		rm8UiSetVisibleN(ui, "accusalPlane", true);
		say(4003); npcPlay(judge, 2);
		accusedDoll = npcCreate(0, ui, "accusedPic", rm8TurnCharId(accused), rm8TurnOutfit(accused));
		npcPlay(accusedDoll, 2, true);
	}

	void tap(int id)
	{
		if(id == 0){ reset(); return; }					// quit: [V] Court_OnClickEventHandler -> Court_ResetState @0x4d81d0
		if(state == 1 && id >= 5000){
			for(size_t k = 0; k < g_pic.size(); k++) if(g_pic[k] == id && k < picOf.size()){ pick(picOf[k]); return; }
		}else if(state == 2 && charge == 0 && (id == 1 || id == 3 || id == 5)){	// [V] Court_StartTrial @0x4d43b0
			charge = id == 1 ? 1 : id == 3 ? 2 : 3;
			state = 3; timer = 0.3f * waitScale();
		}
	}
	int idOf(const std::string &n) override { int k = -1; if(sscanf(n.c_str(), "role%d", &k) == 1) for(size_t j = 0; j < picOf.size(); j++) if(picOf[j] == k && j < g_pic.size()) return g_pic[j]; return -1; }
	void click(const std::string &n) override
	{
		if(n == "quit") tap(0);
		else if(n == "Com") tap(1); else if(n == "Card") tap(3); else if(n == "Land") tap(5);
		else { int k = -1; if(sscanf(n.c_str(), "RolePic%d", &k) == 1 && k >= 0) for(size_t j = 0; j < picOf.size(); j++) if(picOf[j] == k && j < g_pic.size()) tap(g_pic[j]); }
	}

	void reset()	// [V] Court_ResetState: stop the voice, no wait, state 7
	{
		rm8SndStop(voice); voice = 0; timer = 0; state = 7;
	}

	// [V] Court_ProcessAccusal @0x4d6510
	void verdict()
	{
		rm8UiSetVisibleN(ui, "accusalPlane", false);
		int r = rnd() % 100;
		const std::string acc = rm8TurnName(seat), def = rm8TurnName(accused);
		bool win = charge == 2 ? r < 70 : r < 60;
		int days = charge == 2 ? 3 : 5;
		if(charge == 1 || charge == 2){
			if(win){
				say(plainFmt(4005, def.c_str(), days) + "@4004");
				rm8TurnEnqueue(accused, 0x5d, days, seat + 1);			// the defendant goes to prison
				if(seat != accused){ rm8TurnEnqueue(seat, 0x2e, accused + 1, 0); rm8TurnEnqueue(accused, 0x2e, seat + 1, 1); }
			}else{
				say(plainFmt(4006, acc.c_str(), days) + "@4005");
				rm8TurnEnqueue(seat, 0x5d, days, 0);				// the accuser goes instead
			}
		}else{
			if(win) say(plainFmt(4028, def.c_str(), 5000) + "@4004");
			else say(plainFmt(4029, acc.c_str(), 5000) + "@4005");
			rm8TurnEnqueue(win ? accused : seat, 0x16, 5000, 0);			// [V] the loser pays 5000 in cash (flat, as the code)
		}
		npcPlay(judge, 4);
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: court: seat %d accuses seat %d (charge %d): %s", seat, accused, charge, win ? "success" : "failure");
	}

	// [V] CUi_court__vf1_Update @0x4d5a60
	bool update(float dt) override
	{
		if(state == 2){	// [V] MouseOn callbacks 5 / 6 of the charge buttons: the description / the prompt
			int h = rm8UiHotN(ui, "Com") ? 1 : rm8UiHotN(ui, "Card") ? 2 : rm8UiHotN(ui, "Land") ? 3 : 0;
			if(h != hover){
				hover = h;
				if(h == 0) txt(plain(rm8Str(4003)));
				else txt(plain(rm8Str(h == 1 ? 4033 : h == 2 ? 4035 : 4034)));
			}
		}
		timer -= dt;
		if(timer > 0 || busy()) return false;
		timer = 1.6f * waitScale();
		if(state == 0){ state = 1; say(4002); npcPlay(judge, 1); }
		else if(state == 3){
			say(plainFmt(4004, rm8TurnName(seat), rm8TurnName(accused), plain(rm8Str(chargeNameStr(charge))).c_str()));	// "%s控告%s%s"
			npcPlay(judge, 0); state = 6;
		}else if(state == 6){ verdict(); state = 7; timer = 2.0f * waitScale(); }
		else if(state == 7) return true;
		return false;
	}
};

}	// namespace

Task *makeCourtTask(int seat)
{
	CourtTask *t = new CourtTask(seat);
	if(!t->ok()){ delete t; return nullptr; }
	return t;
}

// [V] AI_Event_Court @0x4f8810
bool aiCourt(int seat)
{
	std::vector<int> others;
	for(int k = 0; k < rm8TurnRoleCount(); k++) if(k != seat && rm8TurnStatus(k) != 7) others.push_back(k);
	if(others.empty()) return false;
	int def = others[rnd() % others.size()];
	long c = 0;
	rm8PyCallAI(rm8TurnAiScript(seat), "Count", 0, c);	// [V] Python Count: 0 contempt, 1 card abuse, 2 forced rent, anything else: no charge
	if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: court: computer seat %d: Count -> %ld, defendant seat %d", seat, c, def);
	if(c < 0 || c > 2) return false;
	const std::string acc = rm8TurnName(seat), dn = rm8TurnName(def);
	const std::string line = plainFmt(4004, acc.c_str(), dn.c_str(), plain(rm8Str(chargeNameStr((int)c + 1))).c_str());
	bool win = (long)(rnd() % 100) < (c == 1 ? 70 : 60);
	if(c == 2){
		long amount = 5000L * rm8GamePI();
		queueTextAt(seat, 0, line);
		if(win){
			queueTextAt(seat, 1, plainFmt(4028, dn.c_str(), (int)amount));
			rm8TurnEnqueueAt(seat, 2, 0x2e, def + 1, 0);
			rm8TurnEnqueue(def, 0x16, (int)amount, 0); rm8TurnEnqueue(def, 0x2e, seat + 1, 0);
		}else{
			queueTextAt(seat, 1, plainFmt(4029, acc.c_str(), (int)amount));
			rm8TurnEnqueueAt(seat, 2, 0x16, (int)amount, 0);
		}
		return true;
	}
	int days = c == 1 ? 3 : 5;
	if(win){
		queueTextAt(seat, 0, line);
		queueTextAt(seat, 1, plainFmt(4005, dn.c_str(), days));
		rm8TurnEnqueueAt(seat, 2, 0x2e, def + 1, 0);
		rm8TurnEnqueue(def, 0x5d, days, 0); rm8TurnEnqueue(def, 0x2e, seat + 1, 0);
	}else{
		rm8TurnClearQueue(seat);
		queueText(seat, line);
		queueText(seat, plainFmt(4006, acc.c_str(), days));
		rm8TurnEnqueue(seat, 0x5d, days, 0);
	}
	return true;
}

// [V] message 0x5d CPlayerTag__OnPrisonBailDeposit @0x492ac0 (a = days, b = the arresting role + 1 or 0): bail = days * 2000 * price index
// when the role can pay it: the human is asked (str 4025), the computer pays 50 % of the time if cash + deposit >= 30000 * PI; otherwise
// message 0x19 (prison). 0x5e @0x492c90 pays the bail (b != 0) and drops the queued 0x19.
static int g_bailDays[8], g_bailBy[8];
void bailMessage(int seat, int cmd, int a, int b)
{
	if(seat < 0 || seat >= 8) return;
	if(cmd == 0x5d){
		long bail = (long)a * 2000 * rm8GamePI(), wealth = rm8GameCash(seat) + rm8GameDeposit(seat);
		g_bailDays[seat] = a; g_bailBy[seat] = b;
		if(bail < wealth){
			if(rm8TurnHuman(seat)){
				rm8TurnEnqueueAt(seat, 0, 1, rm8GamePushText(plainFmt(4025, (int)bail)), 0x80);		// okcancel
				rm8TurnEnqueueAt(seat, 1, 0x1C, 0, 0);
				rm8TurnEnqueueAt(seat, 2, 0x5e, (int)bail, (int)0x80000000);
				return;
			}
			if(wealth >= 30000L * rm8GamePI() && (rnd() & 1)){
				queueTextAt(seat, 0, plainFmt(4027, rm8TurnName(seat)));
				rm8GameSubMoney(seat, bail, false);
				return;
			}
		}
		rm8TurnEnqueueAt(seat, 0, 0x19, a, b);
	}else{	// 0x5e: b != 0 = the answer "pay"
		if(b == 0){ rm8TurnEnqueueAt(seat, 0, 0x19, g_bailDays[seat], g_bailBy[seat]); return; }
		queueTextAt(seat, 0, plainFmt(4027, rm8TurnName(seat)));
		rm8GameSubMoney(seat, a, false);
	}
}

}	// namespace ev
