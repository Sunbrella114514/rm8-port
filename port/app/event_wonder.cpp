// The wonders (event 0x5A money / 0x5B land / 0x5C card wonder, wonder.ui) and their effects. After the task object created by CreateGameObject @0x4b1bc0
// (MagicHouse_InitContainer @0x4b3790 = vtable 0x5E4938 "Cls_5e4938", +0xC = wonder index), loader Cls_5e4938__vf16__Init @0x4e05d0, Update vf1 @0x4e09c0,
// Wonder_OnPurchase @0x4e11a0, skip vf13 @0x4e15b0, and the manager DAT_006403bc (Wonder_Manager_Init @0x4e1670: 3 records of 0x24 bytes: +0 index,
// +4 active, +8 owner (-1), +0xC.. one share (float) per role, +0x1C cost 50000, +0x20 rate 0.1), Wonder_SetActive @0x4e17a0, Wonder_CheckConstruction @0x4e1800,
// Wonder_CalcInvestmentCost @0x4e18d0.
// [V] a visit invests cost * rate = 5000 from the cash of the role (not enough cash: str 210); each investment adds 0.1 to its share; when the shares leave
// less than 0.001 of the whole the role becomes the owner (str 206 + the description 207 / 209 / 208). The effects: money wonder (0): the monthly deposit interest of the owner
// is 20 % (deposit / 5), the others' 5 % (deposit / 20) [Balance_ShowDepositInfo @0x4afa40]; land wonder (1): the owner pays 20 % less rent and gets 20 % more [Role_ProcessArriveLand
// @0x4868e0]; card wonder (2): the owner buys cards at 80 %, the others at 120 % [CardShop_CalcCardPrice @0x4e6f90].
// [V] the building on the site: message 0x33 (u_Role_StartEventAtStPoint @0x486430 -> u_TerraObj_StartStaged @0x4ee690) plays the effect art 0x17E7 (sample 0x7C) and at its end
// TerraObj_AdvanceStageModel @0x4ee700 shows the next stage when stage <= percent / 25 (stages 1..4 = building.bin ids 5 / 6 / 7 level 1..4; stage 5 = the map's wonder art, stage.bin).
// [A] not drawn: the pad colour of the finished wonder (Terrain_SetColorFromRGB); the plot objects are not Terra plots of the port.
#include "event_int.h"
#include "audio.h"
#include "present.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>

namespace ev {

namespace {

// ---- state (plain data, the save / load module reads it through wonderState())
struct Rec { bool active = false; int owner = -1; float share[8] = {0, 0, 0, 0, 0, 0, 0, 0}; int cost = 50000; float rate = 0.1f; int stage = 0; int percent = 0; };
struct Mgr { Rec w[3]; } g_m;

// ---- the map's data (scenes/<map>/wonders.txt, scripts/export_wonders.py)
struct Site { int idx, tx, ty; };
struct Plot { int idx; float pos[3], yaw; };
struct Final { int art; std::string dff, texdir, anm; };
std::string g_dir, g_map;
bool g_loaded = false;
std::vector<Site> g_sites;
Plot g_plot[3]; bool g_hasPlot[3] = {false, false, false};
Final g_final[3]; bool g_hasFinal[3] = {false, false, false};
rw::Clump *g_model[3] = {nullptr, nullptr, nullptr};
int g_modelStage[3] = {-1, -1, -1};

void dropModels(){ for(int i = 0; i < 3; i++){ if(g_model[i]) rm8GameRemoveModel(g_model[i]); g_model[i] = nullptr; g_modelStage[i] = -1; } }

void ensure()
{
	std::string id = rm8GameMapId();
	if(g_loaded && id == g_map) return;
	dropModels();
	g_m = Mgr(); g_sites.clear(); for(int i = 0; i < 3; i++){ g_hasPlot[i] = g_hasFinal[i] = false; }
	g_loaded = true; g_map = id;
	if(id.empty() || g_dir.empty()) return;
	std::ifstream f(g_dir + "/" + id + "/wonders.txt");
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "wsite"){ Site s; if(ss >> s.idx >> s.tx >> s.ty) g_sites.push_back(s); }
		else if(kw == "wplot"){ int idx, type, n, ax, ay; Plot p; if(ss >> idx >> type >> n >> p.pos[0] >> p.pos[1] >> p.pos[2] >> p.yaw >> ax >> ay && idx >= 0 && idx < 3){ p.idx = idx; g_plot[idx] = p; g_hasPlot[idx] = true; } }
		else if(kw == "wfinal"){ Final fi; int idx; if(ss >> idx >> fi.art >> fi.dff >> fi.texdir >> fi.anm && idx >= 0 && idx < 3){ g_final[idx] = fi; g_hasFinal[idx] = true; } }
	}
	// [V] BuildObj_CreateWonder @0x4f1820 / CTerraObj InitWonder @0x4ee5d0: a wonder with a plot on the map is active from the start (Wonder_SetActive)
	for(const Site &s : g_sites) if(s.idx >= 0 && s.idx < 3) g_m.w[s.idx].active = true;
	rm8Log("wonders: map %s: %d sites", id.c_str(), (int)g_sites.size());
}

int roleCount(){ return rm8TurnRoleCount(); }

// [V] Wonder_CheckConstruction @0x4e1800: the owner's seat, -1 none (or inactive), -2 when the owner is bankrupt
int check(int idx)
{
	ensure();
	if(idx < 0 || idx > 2) return -1;
	const Rec &r = g_m.w[idx];
	if(!r.active) return -1;
	if(r.owner >= 0 && r.owner < roleCount() && rm8TurnStatus(r.owner) == 7) return -2;
	return r.owner;
}

// [V] Wonder_CalcInvestmentCost @0x4e18d0: the amount paid (0 = nothing: bankrupt role, inactive wonder, not enough cash)
int invest(int idx, int seat)
{
	ensure();
	if(idx < 0 || idx > 2 || seat < 0 || seat >= roleCount() || rm8TurnStatus(seat) == 7) return 0;
	Rec &r = g_m.w[idx];
	if(!r.active) return 0;
	int amount = (int)std::floor((float)r.cost * r.rate + 0.5f);	// Float_RoundToInt
	if(rm8GameCash(seat) < amount) return 0;
	rm8GameSubMoney(seat, amount, false);	// [V] Bank_WithdrawMoney(amount, 0)
	r.share[std::min(seat, 7)] += r.rate;
	float left = 1.0f;
	for(int k = 0; k < roleCount() && k < 8; k++) left -= r.share[k];
	if(left < 0.001f) r.owner = seat;
	int pct = 0; for(int k = 0; k < roleCount() && k < 8; k++) pct += (int)std::floor(r.share[k] * 100.0f + 0.5f);
	r.percent = pct;
	if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: wonder %d: seat %d invests %d (%d%% done, owner %d)", idx, seat, amount, pct, r.owner);
	return amount;
}

// ---- the building on the site
void showStage(int idx)	// [V] TerraObj_AdvanceStageModel @0x4ee700: stage 1..4 = building.bin art of id 5 + idx level `stage`; stage 5 the map's wonder art
{
	Rec &r = g_m.w[idx];
	if(!g_hasPlot[idx] || g_modelStage[idx] == r.stage) return;
	if(g_model[idx]){ rm8GameRemoveModel(g_model[idx]); g_model[idx] = nullptr; }
	g_modelStage[idx] = r.stage;
	if(r.stage <= 0) return;
	const Plot &p = g_plot[idx];
	if(r.stage >= 5){
		if(g_hasFinal[idx]) g_model[idx] = rm8GameAddModel(g_final[idx].dff, g_final[idx].texdir, p.pos, p.yaw, g_final[idx].anm, true);
	}else{
		g_model[idx] = rm8GameAddArt(rm8GameBuildingArt(5 + idx, r.stage), p.pos, p.yaw, 0);
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: wonder %d stage %d model art %d -> %p", idx, r.stage, rm8GameBuildingArt(5 + idx, r.stage), (void*)g_model[idx]);
	}
}

// ---- the task
struct WonderTask : Task {
	int idx;
	float timer = 0;		// [V] +8
	int state = 0;			// [V] +0x20: 1 completed, 5 done
	int owner0 = -1;		// [V] +0x124: the owner when the task began
	bool done = false;
	std::string name;		// [V] +0x24
	void say(const std::string &s){ rm8UiSetTextByName(ui, "bobo", s); }
	WonderTask(int s, int i)
	{
		seat = s; idx = i;
		ensure();
		owner0 = g_m.w[idx].owner;
		if(owner0 >= 0) return;		// [H] a built wonder starts nothing (the original's loader leaves without a dialog)
		ui = rm8UiLoad("wonder", RM8UI_DIALOG);
		if(!ui) return;
		timer = 1.6f * waitScale();
		uiTapOnly(ui, {});
		g_m.w[idx].active = true;	// [V] Wonder_SetActive
		name = plain(rm8Str(201 + idx));
		rm8UiShow(ui, true);
		purchase();
	}
	~WonderTask() override { if(ui){ rm8UiShow(ui, false); } }
	bool ok() const { return ui != nullptr; }
	void purchase()	// [V] Wonder_OnPurchase @0x4e11a0
	{
		int cost = invest(idx, seat);
		if(cost < 1){ say(plain(rm8Str(210))); state = 5; return; }
		say(plainFmt(204, rm8TurnName(seat), cost));
		rm8TurnEnqueue(seat, 0x33, seat, idx);	// [V] the building effect (u_Role_StartEventAtStPoint)
		timer = 1.8f * waitScale();
		rm8SndPlay(0x90);
		state = (owner0 == -1 && check(idx) >= 0) ? 1 : 5;
	}
	bool skip() override { timer = 0; return true; }	// [V] vf13 @0x4e15b0: unconditional
	bool update(float dt) override
	{
		timer -= dt;
		if(timer > 0) return false;
		timer = 0;
		timer = 2.6f * waitScale();
		if(state == 1){
			std::string t = plainFmt(206, name.c_str(), rm8TurnName(seat));	// [V] str 206 + "\n" + the description
			int d = idx == 0 ? 207 : idx == 1 ? 209 : 208;
			t += "\\" + plainFmt(d, rm8TurnName(seat));
			say(t);
			rm8TurnEnqueue(seat, 0x34, seat, idx);	// [V] (no handler in Role_DispatchQueuedMsg)
			state = 5;
			timer = 4.6f * waitScale();
		}else if(state == 5) done = true;
		return done;
	}
};

// [V] the message 0x33 -> u_Role_StartEventAtStPoint @0x486430 -> u_TerraObj_StartStaged @0x4ee690 -> NPC_Question_Init @0x4f5b90: the effect article 0x17E7 at the site (sample 0x7C);
// when it ends (Cls_5e68e8__vf3_CreateModel @0x4f5ca0) the next stage if stage <= percent / 25. The role waits in state 9 meanwhile.
struct StageTask : Task {
	int idx, fx = 0; bool advance = false, done = false; float guard = 0;
	StageTask(int s, int i)
	{
		seat = s; idx = i;
		ensure();
		Rec &r = g_m.w[idx];
		advance = r.stage <= r.percent / 25;
		if(g_hasPlot[idx]){
			float p[3] = {g_plot[idx].pos[0], g_plot[idx].pos[1], g_plot[idx].pos[2]};
			rm8TurnFocus(p);
			fx = rm8FxPlay(0x17E7, p, g_plot[idx].yaw, 0, true, 0x7C, 1.0f, 0, true);
			if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: wonder %d effect at (%.0f %.0f %.0f) handle %d", idx, p[0], p[1], p[2], fx);
		}
		if(!fx) done = true;
	}
	bool ok() const { return true; }
	bool skip() override { return false; }
	bool update(float dt) override
	{
		if(!done){ guard += dt; if(rm8FxFinished(fx) || guard > 5.0f) done = true; if(!done) return false; if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: wonder effect ended after %.2f s", guard); }
		if(advance){ Rec &r = g_m.w[idx]; r.stage++; showStage(idx); if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: wonder %d stage %d", idx, r.stage); }
		return true;
	}
};

}	// namespace

void wonderSetDir(const std::string &scenesDir){ g_dir = scenesDir; g_loaded = false; }
void wonderClear(){ dropModels(); g_m = Mgr(); g_loaded = false; g_map.clear(); g_sites.clear(); }

Task *makeWonderTask(int seat, int idx)
{
	WonderTask *t = new WonderTask(seat, idx);
	if(!t->ok()){ delete t; return nullptr; }
	return t;
}

Task *makeWonderStageTask(int seat, int idx)
{
	if(idx < 0 || idx > 2) return nullptr;
	return new StageTask(seat, idx);
}

// [V] Land_GetLevel @0x4f0600: the wonder (0..2) of the road node's plot word, -1 none
int wonderAtNode(int node)
{
	ensure();
	int tx, ty;
	if(!rm8GameNodeTile(node, tx, ty)) return -1;
	for(const Site &s : g_sites) if(s.tx == tx && s.ty == ty) return s.idx;
	return -1;
}

// the manager state for the save module (docs/events.md 9.5): wonder idx -> active, owner (-1), shares per seat, stage, percent; Set rebuilds the site model
bool wonderGet(int idx, bool &active, int &owner, float share[8], int &stage, int &percent)
{
	ensure();
	if(idx < 0 || idx > 2) return false;
	const Rec &r = g_m.w[idx];
	active = r.active; owner = r.owner; stage = r.stage; percent = r.percent;
	for(int k = 0; k < 8; k++) share[k] = r.share[k];
	return true;
}
void wonderSet(int idx, bool active, int owner, const float share[8], int stage, int percent)
{
	ensure();
	if(idx < 0 || idx > 2) return;
	Rec &r = g_m.w[idx];
	r.active = active; r.owner = owner; r.stage = stage; r.percent = percent;
	for(int k = 0; k < 8; k++) r.share[k] = share[k];
	g_modelStage[idx] = -2;	// force the rebuild
	showStage(idx);
}

int wonderOwner(int idx){ return check(idx); }
int wonderOwnerRaw(int idx){ ensure(); return idx >= 0 && idx < 3 ? g_m.w[idx].owner : -1; }

}	// namespace ev

using namespace ev;

// [V] Role_TriggerLandLevelEvent @0x4863f0 (called by Role_OnArriveNode @0x4861a0 for a node without an event icon): the wonder event of the node
bool rm8EventWonderNode(int seat, int node)
{
	int idx = wonderAtNode(node);
	if(idx < 0) return false;
	return rm8EventLand(seat, 0x5A + idx, node);
}
int rm8EventWonderOwner(int idx){ return wonderOwner(idx); }
int rm8EventWonderOwnerRaw(int idx){ return wonderOwnerRaw(idx); }

// [V] Role_ProcessArriveLand @0x4868e0: the land wonder: its owner pays 20 % less (rent - rent * 20 / 100), the owner of the plot being the wonder's owner gets 20 % more
long rm8EventWonderRent(int payer, int plotOwner, long rent)
{
	int w = wonderOwner(1);
	if(w >= 0 && w == payer) return rent - (rent * 20) / 100;
	if(w >= 0 && w == plotOwner) return (rent * 20) / 100 + rent;
	return rent;
}

// [V] Balance_ShowDepositInfo @0x4afa40: deposit / 10; with a money wonder (owner not bankrupt) its owner deposit / 5 and the others deposit / 20
int rm8EventInterestDivisor(int seat)
{
	int w = wonderOwner(0);
	if(w < 0) return 10;
	return w == seat ? 5 : 20;
}

// [V] CardShop_CalcCardPrice @0x4e6f90: with a card wonder (raw owner, even a bankrupt one) the owner pays ticket * 4 / 5, everybody else ticket * 6 / 5
int rm8EventCardPrice(int seat, int ticket)
{
	int w = wonderOwnerRaw(2);
	if(w < 0) return ticket;
	return w == seat ? (ticket << 2) / 5 : (ticket * 6) / 5;
}

void rm8EventSceneClear(){ wonderClear(); }

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_wonder.inc"
