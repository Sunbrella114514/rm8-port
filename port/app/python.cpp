// Embedded CPython 2.4.6 running the original game scripts (Pysrc\*.pyc): computer decisions (AI_xx), later card
// effects (CardPy_xx) and cutscenes (daXX_N). The game modules Role / StPoint / Terra / Street / Npc / Stock /
// CardPython / Movie / CTime are provided here (docs/python_api.md, docs/rules_land.md section 4).
// [V] AI_CallScript @0x4f7810 -> Python_CallFunction @0x5019a0: fn(PyInt(param)), result PyInt_AsLong, 0 when missing.
// [A] Terra handles are index + 1 (the game passes object addresses); functions the port does not implement yet
//     return 0 (listed in kStubs).
#define register		/* the 2.4 headers use the C "register" keyword (removed in C++17) */
#include "Python.h"
#undef register
#include "turn.h"
#include "cards.h"
#include "stock.h"
#include "npc.h"
#include "ui.h"
#include "movie.h"
#include "story.h"
#include <map>
#include <vector>
#include <string>

namespace {

bool g_ready = false;
std::map<std::string, PyObject*> g_mods;

int terraOf(long h){ return (int)h - 1; }
long terraHandle(int t){ return t < 0 ? 0 : t + 1; }

PyObject *ret(long v){ return PyInt_FromLong(v); }
long arg0(PyObject *args, long def = 0){ long v = def; if(PyTuple_Size(args) > 0){ PyObject *o = PyTuple_GetItem(args, 0); if(PyInt_Check(o)) v = PyInt_AsLong(o); } return v; }
long arg1(PyObject *args, long def = 0){ long v = def; if(PyTuple_Size(args) > 1){ PyObject *o = PyTuple_GetItem(args, 1); if(PyInt_Check(o)) v = PyInt_AsLong(o); } return v; }

// ---- Role
PyObject *Role_CurRole(PyObject *, PyObject *){ return ret(rm8TurnCurrent()); }
PyObject *Role_Shape(PyObject *, PyObject *a){ return ret(rm8NpcShape((int)arg0(a))); }	// possession (npc.cpp)
PyObject *Role_Cash(PyObject *, PyObject *a){ return ret(rm8GameCash((int)arg0(a))); }
PyObject *Role_Fund(PyObject *, PyObject *a){ return ret(rm8GameDeposit((int)arg0(a))); }
PyObject *Role_PriceIndex(PyObject *, PyObject *){ return ret(rm8GamePI()); }
PyObject *Role_GameDay(PyObject *, PyObject *){ return ret(rm8GameDay()); }
PyObject *Role_Sum(PyObject *, PyObject *){ return ret(rm8TurnRoleCount()); }
PyObject *Role_Exist(PyObject *, PyObject *a){ int r = (int)arg0(a, -1); return ret(r >= 0 && r < rm8TurnRoleCount() && rm8TurnStatus(r) != 7); }
PyObject *Role_Broken(PyObject *, PyObject *a){ int r = (int)arg0(a, -1); return ret(r >= 0 && r < rm8TurnRoleCount() && rm8TurnStatus(r) == 7); }
PyObject *Role_StPoint(PyObject *, PyObject *a){ long r = arg0(a, -1); int n = r >= 0 && r < rm8TurnRoleCount() ? rm8TurnNode((int)r) : -1; return ret(n >= 0 ? n + 1 : 0); }	// [A] StPoint handle = node + 1
// ---- Terra
PyObject *Terra_Owner(PyObject *, PyObject *a){ return ret(rm8GameTerraOwner(terraOf(arg0(a)))); }
PyObject *Terra_Level(PyObject *, PyObject *a){ return ret(rm8GameTerraLevel(terraOf(arg0(a)))); }
PyObject *Terra_Type(PyObject *, PyObject *a){ return ret(rm8GameTerraType(terraOf(arg0(a)))); }
PyObject *Terra_Street(PyObject *, PyObject *a){ return ret(rm8GameTerraStreet(terraOf(arg0(a)))); }
PyObject *Terra_Charge(PyObject *, PyObject *a){ return ret(rm8GameTerraCharge(terraOf(arg0(a)))); }
PyObject *Terra_Rent(PyObject *, PyObject *a){ return ret(rm8GameTerraRent(terraOf(arg0(a)), arg1(a) != 0)); }
PyObject *Terra_Sum(PyObject *, PyObject *){ return ret(rm8GameTerraCount()); }
PyObject *Terra_Terra(PyObject *, PyObject *a){ return ret(terraHandle((int)arg0(a, -1))); }
// ---- StPoint
PyObject *StPoint_Terra(PyObject *, PyObject *a){ return ret(terraHandle(rm8GameNodeTerra((int)arg0(a, 0) - 1))); }
PyObject *StPoint_Sum(PyObject *, PyObject *){ return ret(rm8GameNodeCount()); }
PyObject *StPoint_No(PyObject *, PyObject *a){ long h = arg0(a); return ret(h >= 1 ? h - 1 : -1); }
PyObject *Stub(PyObject *, PyObject *){ return ret(0); }

// ---- cards / turn-flow engine functions (cards.cpp is the other end). [V] unless noted a py_* queues a message on the role given as
// its first argument (Role_EnqueueMsg is called on that role: disassembly of py_Role_Pause @0x4fe660 / py_Role_LostFocus @0x4fe710)
// [A] StPoint handles are road node index + 1 (the game passes object addresses); 0 = none
long argN(PyObject *args, int i, long def = 0)
{
	if(PyTuple_Size(args) <= i) return def;
	PyObject *o = PyTuple_GetItem(args, i);
	if(PyInt_Check(o)) return PyInt_AsLong(o);
	if(PyLong_Check(o)) return PyLong_AsLong(o);
	return def;
}
bool okRole(long r){ return r >= 0 && r < rm8TurnRoleCount(); }
PyObject *none(){ Py_INCREF(Py_None); return Py_None; }
void enq(long r, int cmd, long a = 0, long b = 0){ if(okRole(r)) rm8TurnEnqueue((int)r, cmd, (int)a, (int)b); }
int nodeOf(long h){ return h >= 1 && h <= rm8GameNodeCount() ? (int)h - 1 : -1; }

PyObject *Role_Terra(PyObject *, PyObject *a){ long r = argN(a, 0, -1); return ret(okRole(r) ? terraHandle(rm8GameNodeTerra(rm8TurnNode((int)r))) : 0); }
// py_Role_StPoints @0x4fdc80: breadth-first over the road links, `range` layers from the role's node; dir 0 all, 1 only ahead (the next node),
// 2 not ahead; with dir != 0 nodes with 3+ links are not expanded; returns the nodes after the start (the tuple (0,) if none)
PyObject *Role_StPoints(PyObject *, PyObject *a)
{
	long r = argN(a, 0, -1), range = argN(a, 1), dir = argN(a, 2);
	std::vector<int> list;
	if(okRole(r)){
		list.push_back(rm8TurnNode((int)r));
		int to = rm8TurnNodeTo((int)r);
		size_t start = 0, end = 1;
		for(long step = 0; step < range && step < 32; step++){
			for(size_t i = start; i < end; i++){
				int nb[16], n = rm8TurnLinks(list[i], nb, 16);
				if(dir != 0 && n >= 3) continue;
				for(int k = 0; k < n; k++){
					bool add = i != 0 || (dir == 1 ? nb[k] == to : (dir != 2 || nb[k] != to));
					if(!add) continue;
					bool seen = false;
					for(size_t j = 0; j < end; j++) if(list[j] == nb[k]){ seen = true; break; }
					if(!seen) list.push_back(nb[k]);
				}
			}
			start = end; end = list.size();
		}
	}
	if(list.size() < 2){ PyObject *t = PyTuple_New(1); PyTuple_SetItem(t, 0, ret(0)); return t; }
	PyObject *t = PyTuple_New(list.size() - 1);
	for(size_t k = 1; k < list.size(); k++) PyTuple_SetItem(t, k - 1, ret(list[k] + 1));
	return t;
}
PyObject *Role_TrfTool(PyObject *, PyObject *a){ long r = argN(a, 0, -1); return ret(okRole(r) ? rm8TurnDiceMax((int)r) : 0); }
PyObject *Role_ChgTrfTool(PyObject *, PyObject *a){ long r = argN(a, 0, -1); if(okRole(r)) rm8TurnSetVehicle((int)r, (int)argN(a, 1)); return none(); }	// py_Role_ChgTrfTool @0x4fe0c0: direct Role_SetTrfTool
PyObject *Role_Tortoise(PyObject *, PyObject *a){ long r = argN(a, 0, -1); return ret(okRole(r) && rm8CardsTurtle((int)r) > 0); }
PyObject *Role_SetTrts(PyObject *, PyObject *a){ long r = argN(a, 0, -1); if(okRole(r)) rm8CardsSetTurtle((int)r, (int)argN(a, 1)); return none(); }
PyObject *Role_Settle(PyObject *, PyObject *a)	// py_Role_Settle @0x4fe2b0: stay flag; the current role also queues 0x25(0)
{
	long r = argN(a, 0, -1);
	if(okRole(r)){ rm8CardsSettle((int)r); if(rm8TurnCurrent() == r) enq(r, 0x25, 0, 0); }
	return none();
}
PyObject *Role_Speak(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x5A, argN(a, 1), 0); return none(); }
PyObject *Role_Wait(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 100); return none(); }
PyObject *Role_Go(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x1a, argN(a, 1)); return none(); }
PyObject *Role_Turn(PyObject *, PyObject *a){ long r = argN(a, 0, -1); if(okRole(r)) rm8TurnTurnAround((int)r); return none(); }	// vf16, direct
PyObject *Role_Pause(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x65, argN(a, 1)); return none(); }
PyObject *Role_LostFocus(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x2e, argN(a, 1) + 1, argN(a, 2)); return none(); }
PyObject *Role_Hypnosis(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x28, argN(a, 1)); return none(); }
PyObject *Role_PlayPrt(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x2c, argN(a, 1), argN(a, 2)); return none(); }
PyObject *Role_Become(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x24, argN(a, 1)); return none(); }
PyObject *Role_Prison(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x19, argN(a, 1)); return none(); }
PyObject *Role_Hospital(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x18, argN(a, 1)); return none(); }
PyObject *Role_ChgPos(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x3d, argN(a, 1), argN(a, 2)); return none(); }
PyObject *Role_Bubble(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x3e); return none(); }
PyObject *Role_HasBubble(PyObject *, PyObject *a){ long r = argN(a, 0, -1); return ret(okRole(r) && rm8CardsHasBubble((int)r)); }
PyObject *Role_Bodyguard(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x45); return none(); }
PyObject *Role_HasBdygrd(PyObject *, PyObject *a){ long r = argN(a, 0, -1); return ret(okRole(r) && rm8CardsHasBodyguard((int)r)); }	// py_Role_HasBdygrd @0x4ff3e0: vf+0x64
PyObject *Role_Latent(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x46); return none(); }
PyObject *Role_Seal(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x59, argN(a, 1)); return none(); }
PyObject *Role_ReceiveCard(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 4, argN(a, 1), argN(a, 2)); return none(); }	// a = card array index (Id - 1)
PyObject *Role_TimeBombDis(PyObject *, PyObject *a){ enq(argN(a, 0, -1), 0x51); return none(); }
PyObject *Role_TimeBomb(PyObject *, PyObject *a){ return PyFloat_FromDouble((double)rm8NpcTimeBomb(argN(a, 0, -1))); }	// [V] +0x288 time bomb counter (npc.cpp)
PyObject *Role_InScn(PyObject *, PyObject *a)	// py_Role_InScn @0x4ff710: can move normally (status < 3), not invisible, projected inside the view
{
	long r = argN(a, 0, -1); float p[3];
	return ret(okRole(r) && rm8TurnStatus((int)r) < 3 && rm8GameInvisible((int)r) < 1 && rm8RolesGetPos((int)r, p) && rm8CardsPosInView(p));
}
PyObject *Role_CardSum(PyObject *, PyObject *a){ long r = argN(a, 0, -1); return ret(okRole(r) ? rm8CardCount((int)r) : 0); }
PyObject *Role_Card(PyObject *, PyObject *a){ long r = argN(a, 0, -1); return ret(okRole(r) ? rm8CardAt((int)r, (int)argN(a, 1)) : 0); }
PyObject *Role_HasCard(PyObject *, PyObject *a){ long r = argN(a, 0, -1); return ret(okRole(r) && rm8CardFind((int)r, (int)argN(a, 1)) >= 0); }
PyObject *Role_CardLevel(PyObject *, PyObject *a){ return ret(rm8CardLevel((int)argN(a, 0))); }
PyObject *Role_Ticket(PyObject *, PyObject *a){ long r = argN(a, 0, -1); return ret(okRole(r) ? rm8GamePoints((int)r) : 0); }
PyObject *Role_GameDate(PyObject *, PyObject *){ return ret(rm8GameDay()); }	// py_Role_GameDate @0x4ffbf0: g_GameState + 0x14
PyObject *Role_GameDffty(PyObject *, PyObject *){ return ret(rm8CardsDifficulty); }	// py_Role_GameDffty @0x4ffc70: init.txt difficuty
PyObject *Role_Average(PyObject *, PyObject *a)	// py_Role_Average @0x4fe870: both cash values meet, or (target invalid) every role that is not bankrupt gets the mean cash
{
	long r = argN(a, 0, -1), t = argN(a, 1, -1);
	if(!okRole(r)) return none();
	if(okRole(t)){
		long m = (rm8GameCash((int)r) + rm8GameCash((int)t)) / 2;
		rm8GameSetMoney((int)r, m, rm8GameDeposit((int)r)); rm8GameSetMoney((int)t, m, rm8GameDeposit((int)t));
	}else{
		long sum = 0; int n = 0;
		for(int s = 0; s < rm8TurnRoleCount(); s++) if(rm8TurnStatus(s) != 7){ sum += rm8GameCash(s); n++; }
		if(n) for(int s = 0; s < rm8TurnRoleCount(); s++) if(rm8TurnStatus(s) != 7) rm8GameSetMoney(s, sum / n, rm8GameDeposit(s));
	}
	return none();
}
// py_Role_OnEvent @0x4feb50: Role_TriggerLandEvent(ev), then (param != 0) message 0x3b (restore the card). [H] the restore is for a failed
// trigger; the stock events (94 diamond, 95 red, 96 black card) go to stock.cpp
PyObject *Role_OnEvent(PyObject *, PyObject *a)
{
	long r = argN(a, 0, -1), ev = argN(a, 1), p = argN(a, 2);
	if(okRole(r)){
		bool ok = ev >= 94 && ev <= 96 && rm8StockEvent((int)r, (int)ev, (int)p);
		if(!ok && p != 0) enq(r, 0x3b);
	}
	return none();
}
PyObject *Role_Grab(PyObject *, PyObject *a)	// py_Role_Grab @0x4fed70
{
	long g = argN(a, 0, -1), t = argN(a, 1, -1), kind = argN(a, 2), n = argN(a, 3);
	if(!okRole(g) || !okRole(t)) return none();
	if(kind == 1){	// money: the target pays (cash first), the grabber gets it in cash, text str532
		rm8GameSubMoney((int)t, n, false); rm8GameAddMoney((int)g, n, false);
		enq(g, 0x1B, rm8GamePushText(rm8StrFmt(532, rm8TurnName((int)g), rm8TurnName((int)t), (int)n)), 0);
	}else if(kind == 4){	// cards: n == 1 one card (human: the card list of the target, computer: automatic), else all (0x43)
		if(n == 1){
			if(rm8TurnHuman((int)g)){ enq(g, 1, t, 0x220083); enq(g, 0x1C); enq(g, 6, t, (int)0x80000000); }
			else enq(g, 6, t, 9);
		}else enq(g, 0x43, t + 1);
	}
	return none();
}
// ---- Terra / Street / StPoint / Npc
PyObject *Terra_TermSum(PyObject *, PyObject *){ return ret(rm8GameTerraCount()); }
PyObject *Terra_Buy(PyObject *, PyObject *a){ long h = argN(a, 0), r = argN(a, 1, -1); if(h && okRole(r)) enq(r, 9, terraOf(h), 1); return none(); }
PyObject *Terra_Build(PyObject *, PyObject *a)	// py_Terra_Build @0x500860: houses and built plots at once; an empty commercial plot asks a human (business dialog)
{
	long h = argN(a, 0), r = argN(a, 1, -1);
	if(!h || !okRole(r)) return none();
	int t = terraOf(h);
	if(rm8GameTerraType(t) == 1 || rm8GameTerraLevel(t) != 0 || !rm8TurnHuman((int)r)) enq(r, 0xC, t, 1);
	else { enq(r, 1, rm8GamePushText(""), 0x81); enq(r, 0x1C); enq(r, 0xC, t, (int)0x80000000); }
	return none();
}
PyObject *Terra_Break(PyObject *, PyObject *a){ long h = argN(a, 0), r = argN(a, 1, -1); if(h && okRole(r)) enq(r, 0xD, terraOf(h), 1); return none(); }
PyObject *Terra_Exchng(PyObject *, PyObject *a)	// py_Terra_Exchng @0x500be0: message 0xE (the role's own plot, the target)
{
	long h = argN(a, 0), r = argN(a, 1, -1);
	if(okRole(r) && h){ int mine = rm8GameNodeTerra(rm8TurnNode((int)r)); if(mine >= 0) enq(r, 0xE, mine, terraOf(h)); }
	return none();
}
PyObject *Terra_Dragon(PyObject *, PyObject *a){ long h = argN(a, 0), r = argN(a, 1, -1); if(okRole(r)) enq(r, 0x31, terraOf(h), 0); return none(); }
PyObject *Terra_Bomb(PyObject *, PyObject *a){ long p = argN(a, 0), r = argN(a, 1, -1), k = argN(a, 2); if(okRole(r)) enq(r, k == 1 ? 0x36 : 0x37, nodeOf(p), 0); return none(); }
PyObject *Terra_InScn(PyObject *, PyObject *a)
{
	float p[3]; int t = terraOf(argN(a, 0));
	return ret(rm8GameTerraPos(t, p) && rm8CardsPosInView(p));
}
PyObject *Street_Close(PyObject *, PyObject *a){ enq(argN(a, 2, -1), 0x2F, argN(a, 0), argN(a, 1)); return none(); }
PyObject *Street_Flow(PyObject *, PyObject *a){ enq(argN(a, 2, -1), 0x30, argN(a, 0), argN(a, 1)); return none(); }
PyObject *Street_Build(PyObject *, PyObject *a){ enq(argN(a, 1, -1), 0x10, argN(a, 0)); return none(); }
PyObject *Street_Buy(PyObject *, PyObject *a){ enq(argN(a, 1, -1), 0x44, argN(a, 0)); return none(); }
PyObject *StPoint_Point(PyObject *, PyObject *a){ long i = argN(a, 0, -1); return ret(i >= 0 && i < rm8GameNodeCount() ? i + 1 : 0); }
PyObject *StPoint_Event(PyObject *, PyObject *a){ int n = nodeOf(argN(a, 0)); int e = n >= 0 ? rm8GameNodeEvent(n) : 0; return ret(e > 0 ? e : -1); }
// [A] NPC numbers in the scripts (the game's NPC array index, py_Npc_* @0x500160..) are npc.h handles - 1; -1 = none
PyObject *StPoint_Npc(PyObject *, PyObject *a){ int n = nodeOf(argN(a, 0)); int h = n >= 0 ? rm8NpcAt(n) : 0; return ret(h > 0 ? h - 1 : -1); }	// py_StPoint_Npc @0x501160
PyObject *StPoint_Wonder(PyObject *, PyObject *){ return ret(-1); }
PyObject *StPoint_InScn(PyObject *, PyObject *a){ float p[3]; int n = nodeOf(argN(a, 0)); return ret(n >= 0 && rm8TurnNodePos(n, p) && rm8CardsPosInView(p)); }
int npcNode(long no){ for(int n = 0, N = rm8GameNodeCount(); n < N; n++) if(rm8NpcAt(n) == no + 1) return n; return -1; }
PyObject *Npc_ID(PyObject *, PyObject *a){ long no = argN(a, 0, -1); int id = no >= 0 ? rm8NpcId((int)no + 1) : 0; return ret(id > 0 ? id : -1); }	// py_Npc_ID @0x500340
PyObject *Npc_GetNpc(PyObject *, PyObject *a){ long no = argN(a, 0, -1); return ret(no >= 0 && rm8NpcId((int)no + 1) > 0 ? no + 1 : 0); }	// py_Npc_GetNpc @0x5002f0 (the object, 0 = none)
PyObject *Npc_Goodness(PyObject *, PyObject *a)	// py_Npc_Goodness @0x500520: the gods 1 4 5 and the board items 10 11
{
	long no = argN(a, 0, -1); int id = no >= 0 ? rm8NpcId((int)no + 1) : 0;
	return ret(id == 1 || id == 4 || id == 5 || id == 10 || id == 11);
}
PyObject *Npc_InScn(PyObject *, PyObject *a){ float p[3]; int n = npcNode(argN(a, 0, -1)); return ret(n >= 0 && rm8TurnNodePos(n, p) && rm8CardsPosInView(p)); }	// py_Npc_InScn @0x500430
PyObject *Npc_StPoint(PyObject *, PyObject *a){ int n = npcNode(argN(a, 0, -1)); return ret(n >= 0 ? n + 1 : 0); }	// py_Npc_StPoint @0x5004b0
PyObject *Npc_Disappear(PyObject *, PyObject *a){ long no = argN(a, 0, -1); if(no >= 0) rm8NpcRemove((int)no + 1); return none(); }	// py_Npc_Disappear @0x5003b0
// py_Npc_Create @0x500160 (npc id, road point, owner role): the NPC object, sound 0xC / 0x10 / 0x11 for roadblock / mine / bomb, the owner (+0x70).
// [A] the robot doll (15, queues 0x32 / 0x65 / 0x1e in the game) and the other ids are rm8NpcCreate's business; a failed creation
// queues 0x3b (the card comes back) for the current role
PyObject *Npc_Create(PyObject *, PyObject *a)
{
	long id = argN(a, 0), loc = argN(a, 1), owner = argN(a, 2, -1);
	if(rm8StoryNpcPlaced(rm8TurnCurrent(), (int)id)) return none();	// [V] py_Npc_Create @0x500160: CPlayerTag__OnDiceEvent @0x491270 (ECX = the current role) returns 1 -> the NPC is NOT created (story.cpp)
	if(!rm8NpcCreate((int)id, nodeOf(loc), (int)owner)) enq(rm8TurnCurrent(), 0x3b);
	return none();
}

// ---- Stock module and the stock functions of Role (stock.cpp) [V] py_Stock_* @0x501550..0x501760, py_Role_Stock* @0x4ffca0..0x4fff30
PyObject *Stock_Sum(PyObject *, PyObject *){ return ret(rm8StockCount()); }
PyObject *Stock_FlowSum(PyObject *, PyObject *a){ return ret(rm8StockFlow((int)arg0(a, -1))); }
PyObject *Stock_CurPrice(PyObject *, PyObject *a){ return PyFloat_FromDouble(rm8StockCur((int)arg0(a, -1))); }
PyObject *Stock_AvrPrice(PyObject *, PyObject *a){ return PyFloat_FromDouble(rm8StockAvr((int)arg0(a, -1))); }
PyObject *Stock_TradeEnable(PyObject *, PyObject *a){ return ret(rm8StockTradeEnable((int)arg0(a, -1))); }
PyObject *Stock_Open(PyObject *, PyObject *){ return ret(rm8StockOpen()); }
PyObject *Role_Stock(PyObject *, PyObject *a){ return ret(rm8StockHoldStock((int)arg0(a, -1), (int)arg1(a, -1))); }
PyObject *Role_StockSum(PyObject *, PyObject *a){ return ret(rm8StockHoldCount((int)arg0(a, -1))); }
PyObject *Role_StockPrice(PyObject *, PyObject *a){ return PyFloat_FromDouble(rm8StockHoldPrice((int)arg0(a, -1), (int)arg1(a, -1))); }
PyObject *Role_StockValue(PyObject *, PyObject *a){ return ret(rm8StockHoldValue((int)arg0(a, -1), (int)arg1(a, -1))); }

struct Fn { const char *mod, *name; PyCFunction f; };
const Fn kImplStock[] = {
	{"Stock", "Sum", Stock_Sum}, {"Stock", "FlowSum", Stock_FlowSum}, {"Stock", "CurPrice", Stock_CurPrice}, {"Stock", "AvrPrice", Stock_AvrPrice},
	{"Stock", "TradeEnable", Stock_TradeEnable}, {"Stock", "Open", Stock_Open},
	{"Role", "Stock", Role_Stock}, {"Role", "StockSum", Role_StockSum}, {"Role", "StockPrice", Role_StockPrice}, {"Role", "StockValue", Role_StockValue},
};
const Fn kImpl[] = {
	{"Role", "CurRole", Role_CurRole}, {"Role", "Shape", Role_Shape}, {"Role", "Cash", Role_Cash}, {"Role", "Fund", Role_Fund},
	{"Role", "PriceIndex", Role_PriceIndex}, {"Role", "GameDay", Role_GameDay}, {"Role", "Sum", Role_Sum}, {"Role", "Exist", Role_Exist},
	{"Role", "Broken", Role_Broken}, {"Role", "StPoint", Role_StPoint},
	{"Terra", "Owner", Terra_Owner}, {"Terra", "Level", Terra_Level}, {"Terra", "Type", Terra_Type}, {"Terra", "Street", Terra_Street},
	{"Terra", "Charge", Terra_Charge}, {"Terra", "Rent", Terra_Rent}, {"Terra", "Sum", Terra_Sum}, {"Terra", "Terra", Terra_Terra},
	{"StPoint", "Terra", StPoint_Terra}, {"StPoint", "Sum", StPoint_Sum}, {"StPoint", "No", StPoint_No},
};
// cards / turn flow functions (cards.cpp); implemented names are not registered again as stubs
const Fn kImpl2[] = {
	{"Role", "Terra", Role_Terra}, {"Role", "StPoints", Role_StPoints}, {"Role", "TrfTool", Role_TrfTool}, {"Role", "ChgTrfTool", Role_ChgTrfTool},
	{"Role", "Tortoise", Role_Tortoise}, {"Role", "SetTrts", Role_SetTrts}, {"Role", "Settle", Role_Settle}, {"Role", "Speak", Role_Speak},
	{"Role", "Wait", Role_Wait}, {"Role", "Go", Role_Go}, {"Role", "Turn", Role_Turn}, {"Role", "Pause", Role_Pause}, {"Role", "LostFocus", Role_LostFocus},
	{"Role", "Hypnosis", Role_Hypnosis}, {"Role", "PlayPrt", Role_PlayPrt}, {"Role", "Become", Role_Become}, {"Role", "Prison", Role_Prison},
	{"Role", "Hospital", Role_Hospital}, {"Role", "ChgPos", Role_ChgPos}, {"Role", "Bubble", Role_Bubble}, {"Role", "HasBubble", Role_HasBubble},
	{"Role", "Bodyguard", Role_Bodyguard}, {"Role", "HasBdygrd", Role_HasBdygrd}, {"Role", "Latent", Role_Latent}, {"Role", "Seal", Role_Seal},
	{"Role", "ReceiveCard", Role_ReceiveCard}, {"Role", "TimeBombDis", Role_TimeBombDis}, {"Role", "TimeBomb", Role_TimeBomb}, {"Role", "InScn", Role_InScn},
	{"Role", "CardSum", Role_CardSum}, {"Role", "Card", Role_Card}, {"Role", "HasCard", Role_HasCard}, {"Role", "CardLevel", Role_CardLevel},
	{"Role", "Ticket", Role_Ticket}, {"Role", "GameDate", Role_GameDate}, {"Role", "GameDffty", Role_GameDffty}, {"Role", "Average", Role_Average},
	{"Role", "OnEvent", Role_OnEvent}, {"Role", "Grab", Role_Grab},
	{"Terra", "TermSum", Terra_TermSum}, {"Terra", "Buy", Terra_Buy}, {"Terra", "Build", Terra_Build}, {"Terra", "Break", Terra_Break},
	{"Terra", "Exchng", Terra_Exchng}, {"Terra", "Dragon", Terra_Dragon}, {"Terra", "Bomb", Terra_Bomb}, {"Terra", "InScn", Terra_InScn},
	{"Street", "Close", Street_Close}, {"Street", "Flow", Street_Flow}, {"Street", "Build", Street_Build}, {"Street", "Buy", Street_Buy},
	{"StPoint", "Point", StPoint_Point}, {"StPoint", "Event", StPoint_Event}, {"StPoint", "Npc", StPoint_Npc}, {"StPoint", "Wonder", StPoint_Wonder},
	{"StPoint", "InScn", StPoint_InScn}, {"Npc", "ID", Npc_ID}, {"Npc", "Create", Npc_Create}, {"Npc", "GetNpc", Npc_GetNpc},
	{"Npc", "Goodness", Npc_Goodness}, {"Npc", "InScn", Npc_InScn}, {"Npc", "StPoint", Npc_StPoint}, {"Npc", "Disappear", Npc_Disappear},
};
// ---- Movie / CTime modules (cutscenes daXX_N.pyc, movie.cpp is the other end) [V] PyArg_Parse formats at the addresses of movie.h (docs/story_mode.md 2.4)
double argD(PyObject *args, int i, double def = 0)
{
	if(PyTuple_Size(args) <= i) return def;
	PyObject *o = PyTuple_GetItem(args, i);
	if(PyFloat_Check(o)) return PyFloat_AsDouble(o);
	if(PyInt_Check(o)) return (double)PyInt_AsLong(o);
	if(PyLong_Check(o)) return PyLong_AsDouble(o);
	return def;
}
float argF(PyObject *a, int i){ return (float)argD(a, i); }
int argI(PyObject *a, int i){ return (int)argN(a, i, 0); }
std::string argS(PyObject *args, int i){ if(PyTuple_Size(args) <= i) return ""; PyObject *o = PyTuple_GetItem(args, i); return PyString_Check(o) ? PyString_AsString(o) : ""; }
#define MV(name, body) PyObject *Mv_##name(PyObject *, PyObject *a){ (void)a; body; return none(); }
#define MVH(name, body) PyObject *Mv_##name(PyObject *, PyObject *a){ (void)a; return ret(body); }
MVH(RoleCreate, rm8MvRoleCreate(argI(a, 0), argF(a, 1), argF(a, 2), argF(a, 3), argI(a, 4)))
MV(RoleDel, rm8MvRoleDel(argI(a, 0)))
MV(RoleMove, rm8MvRoleMove(argI(a, 0), argF(a, 1), argF(a, 2), argF(a, 3)))
MV(RoleRotate, rm8MvRoleRotate(argI(a, 0), argI(a, 1)))
MV(RoleFace, rm8MvRoleFace(argI(a, 0), argI(a, 1)))
MV(RoleSetAnim, rm8MvRoleSetAnim(argI(a, 0), argI(a, 1), argI(a, 2)))
MV(RoleSpeak, rm8MvRoleSpeak(argI(a, 0), argI(a, 1)))
MV(RoleStopSpeak, rm8MvRoleStopSpeak(argI(a, 0)))
MV(RoleSetPos, rm8MvRoleSetPos(argI(a, 0), argF(a, 1), argF(a, 2), argF(a, 3)))
MV(RoleSetSpeed, rm8MvRoleSetSpeed(argI(a, 0), argF(a, 1)))
MV(RoleSetScale, rm8MvRoleSetScale(argI(a, 0), argF(a, 1), argF(a, 2), argF(a, 3)))
MVH(ArtCreate, rm8MvArtCreate(argI(a, 0), argF(a, 1), argF(a, 2), argF(a, 3)))
MV(ArtSetAnim, rm8MvArtSetAnim(argI(a, 0), argI(a, 1), argI(a, 2)))
MV(ArtDel, rm8MvArtDel(argI(a, 0)))
MV(ArtMove, rm8MvArtMove(argI(a, 0), argF(a, 1), argF(a, 2), argF(a, 3)))
MV(ArtRotate, rm8MvArtRotate(argI(a, 0), argF(a, 1)))
MV(ArtSpeak, rm8MvArtSpeak(argI(a, 0), argI(a, 1)))
MV(ArtStopSpeak, rm8MvArtStopSpeak(argI(a, 0)))
MV(ArtSetPos, rm8MvArtSetPos(argI(a, 0), argF(a, 1), argF(a, 2), argF(a, 3)))
MV(ArtSetSpeed, rm8MvArtSetSpeed(argI(a, 0), argF(a, 1)))
MV(ArtSetScale, rm8MvArtSetScale(argI(a, 0), argF(a, 1), argF(a, 2), argF(a, 3)))
MV(PlaySound, rm8MvPlaySound(argI(a, 0), argI(a, 1)))
MV(PlayMusic, rm8MvPlayMusic(argI(a, 0)))
MV(SetBackPic, rm8MvSetPic(false, argS(a, 0)))
MV(SetFrontPic, rm8MvSetPic(true, argS(a, 0)))
MV(DelBackPic, rm8MvDelPic(false))
MV(DelFrontPic, rm8MvDelPic(true))
MV(SpeakAside, rm8MvSpeakAside(argI(a, 0)))
MV(StopSpeakAside, rm8MvStopSpeakAside())
MV(SetCameraPos, rm8MvSetCamera(argF(a, 0), argF(a, 1), argF(a, 2), argF(a, 3), argF(a, 4)))	// args 4 / 5 = pitch / yaw (Camera_SetOrientationFromAngles @0x41b1f0)
MV(QuitMovie, rm8MvQuit())
MV(Wait, rm8MvWait(argF(a, 0)))
const Fn kImplMovie[] = {
	{"Movie", "RoleCreate", Mv_RoleCreate}, {"Movie", "RoleDel", Mv_RoleDel}, {"Movie", "RoleMove", Mv_RoleMove}, {"Movie", "RoleRotate", Mv_RoleRotate},
	{"Movie", "RoleFace", Mv_RoleFace}, {"Movie", "RoleSetAnim", Mv_RoleSetAnim}, {"Movie", "RoleSpeak", Mv_RoleSpeak}, {"Movie", "RoleStopSpeak", Mv_RoleStopSpeak},
	{"Movie", "RoleSetPos", Mv_RoleSetPos}, {"Movie", "RoleSetSpeed", Mv_RoleSetSpeed}, {"Movie", "RoleSetScale", Mv_RoleSetScale},
	{"Movie", "ArtCreate", Mv_ArtCreate}, {"Movie", "ArtSetAnim", Mv_ArtSetAnim}, {"Movie", "ArtDel", Mv_ArtDel}, {"Movie", "ArtMove", Mv_ArtMove},
	{"Movie", "ArtRotate", Mv_ArtRotate}, {"Movie", "ArtSpeak", Mv_ArtSpeak}, {"Movie", "ArtStopSpeak", Mv_ArtStopSpeak}, {"Movie", "ArtSetPos", Mv_ArtSetPos},
	{"Movie", "ArtSetSpeed", Mv_ArtSetSpeed}, {"Movie", "ArtSetScale", Mv_ArtSetScale},
	{"Movie", "PlaySound", Mv_PlaySound}, {"Movie", "PlayMusic", Mv_PlayMusic}, {"Movie", "SetBackPic", Mv_SetBackPic}, {"Movie", "SetFrontPic", Mv_SetFrontPic},
	{"Movie", "DelBackPic", Mv_DelBackPic}, {"Movie", "DelFrontPic", Mv_DelFrontPic}, {"Movie", "SpeakAside", Mv_SpeakAside}, {"Movie", "StopSpeakAside", Mv_StopSpeakAside},
	{"Movie", "SetCameraPos", Mv_SetCameraPos}, {"Movie", "QuitMovie", Mv_QuitMovie},
	{"CTime", "Wait", Mv_Wait},
};
// the running script: module.rolemove() and its next ([V] SpeakAside_InitPython @0x5141b0: import, rolemove attribute, call, `next` attribute)
PyObject *g_movGen = nullptr, *g_movNext = nullptr;

bool implemented(const std::string &q)
{
	size_t d = q.find('.');
	std::string m = q.substr(0, d), n = q.substr(d + 1);
	for(const Fn &f : kImplMovie) if(m == f.mod && n == f.name) return true;
	for(const Fn &f : kImpl) if(m == f.mod && n == f.name) return true;
	for(const Fn &f : kImpl2) if(m == f.mod && n == f.name) return true;
	for(const Fn &f : kImplStock) if(m == f.mod && n == f.name) return true;
	return false;
}
// names used by the scripts that are not implemented yet: return 0
const char *kStubs[] = {
	"Role.Average", "Role.Become", "Role.Bodyguard", "Role.Borrow", "Role.Bubble", "Role.Card", "Role.CardLevel", "Role.CardSum",
	"Role.ChgPos", "Role.ChgTrfTool", "Role.GameDate", "Role.GameDffty", "Role.Go", "Role.Grab", "Role.HasBdygrd", "Role.HasBubble",
	"Role.HasCard", "Role.Hypnosis", "Role.InScn", "Role.Latent", "Role.LostFocus", "Role.OnEvent", "Role.Pause", "Role.PaybackDay",
	"Role.PlayPrt", "Role.Prison", "Role.ReceiveCard", "Role.Seal", "Role.SetTrts", "Role.Settle", "Role.Speak", "Role.StPoints",
	"Role.Stock", "Role.StockPrice", "Role.StockSum", "Role.StockValue", "Role.Terra", "Role.Ticket", "Role.TimeBomb",
	"Role.TimeBombDis", "Role.Tortoise", "Role.TrfTool", "Role.Turn", "Role.Wait",
	"StPoint.Event", "StPoint.InScn", "StPoint.Npc", "StPoint.Point",
	"Stock.AvrPrice", "Stock.CurPrice", "Stock.FlowSum", "Stock.Open", "Stock.Sum", "Stock.TradeEnable",
	"Street.Build", "Street.Buy", "Street.Close", "Street.Flow",
	"Terra.Bomb", "Terra.Break", "Terra.Build", "Terra.Buy", "Terra.Dragon", "Terra.Exchng", "Terra.InScn", "Terra.TermSum",
	"Npc.Create", "Npc.GetNpc", "Npc.Goodness", "Npc.ID", "Npc.InScn",
	"CTime.Wait",
};
const char *kModules[] = {"Role", "StPoint", "Terra", "Street", "Npc", "Stock", "Movie", "CTime"};

std::map<std::string, std::vector<PyMethodDef>> g_tables;	// kept alive for the interpreter

void initModules()
{
	for(const char *m : kModules) g_tables[m];
	for(const Fn &f : kImpl) g_tables[f.mod].push_back({(char*)f.name, f.f, METH_VARARGS, nullptr});
	for(const Fn &f : kImpl2) g_tables[f.mod].push_back({(char*)f.name, f.f, METH_VARARGS, nullptr});
	for(const Fn &f : kImplStock) g_tables[f.mod].push_back({(char*)f.name, f.f, METH_VARARGS, nullptr});
	for(const Fn &f : kImplMovie) g_tables[f.mod].push_back({(char*)f.name, f.f, METH_VARARGS, nullptr});
	static std::vector<std::string> keep; keep.reserve(1024);	// [A] the stub names must not move (SSO strings): no reallocation
	for(const char *s : kStubs){
		std::string q = s; size_t d = q.find('.');
		if(implemented(q)) continue;
		keep.push_back(q.substr(d + 1));
		g_tables[q.substr(0, d)].push_back({(char*)keep.back().c_str(), Stub, METH_VARARGS, nullptr});
	}
	for(auto &t : g_tables) t.second.push_back({nullptr, nullptr, 0, nullptr});
}

#define MODINIT(N) void init_##N(){ Py_InitModule((char*)#N, g_tables[#N].data()); }
MODINIT(Role) MODINIT(StPoint) MODINIT(Terra) MODINIT(Street) MODINIT(Npc) MODINIT(Stock) MODINIT(Movie) MODINIT(CTime)

void logError(const char *what)
{
	PyObject *t, *v, *tb; PyErr_Fetch(&t, &v, &tb);
	PyObject *s = v ? PyObject_Str(v) : nullptr;
	rm8Log("python: %s: %s", what, s && PyString_Check(s) ? PyString_AsString(s) : "?");
	Py_XDECREF(s); Py_XDECREF(t); Py_XDECREF(v); Py_XDECREF(tb);
}

}	// namespace

extern "C" void rm8_py_set_path(const char *p);

bool rm8PyInit(const std::string &dir)
{
	if(g_ready) return true;
	rm8_py_set_path(dir.c_str());
	Py_NoSiteFlag = 1; Py_IgnoreEnvironmentFlag = 1; Py_FrozenFlag = 1;
	initModules();
	PyImport_AppendInittab((char*)"Role", init_Role); PyImport_AppendInittab((char*)"StPoint", init_StPoint);
	PyImport_AppendInittab((char*)"Terra", init_Terra); PyImport_AppendInittab((char*)"Street", init_Street);
	PyImport_AppendInittab((char*)"Npc", init_Npc); PyImport_AppendInittab((char*)"Stock", init_Stock);
	PyImport_AppendInittab((char*)"Movie", init_Movie);
	PyImport_AppendInittab((char*)"CTime", init_CTime);
	Py_Initialize();
	PyObject *r = PyImport_ImportModule((char*)"random");
	if(!r){ logError("import random"); return false; }
	Py_DECREF(r);
	g_ready = true;
	rm8Log("python %s ready, scripts from %s", Py_GetVersion(), dir.c_str());
	return true;
}

bool rm8PyCallAI(const char *script, const char *fn, long param, long &result)
{
	result = 0;
	if(!g_ready || !script) return false;
	PyObject *&m = g_mods[script];
	if(!m){ m = PyImport_ImportModule((char*)script); if(!m){ logError(script); return false; } }
	PyObject *f = PyObject_GetAttrString(m, (char*)fn);
	if(!f){ PyErr_Clear(); return false; }	// [V] missing callback -> 0
	PyObject *v = PyObject_CallFunction(f, (char*)"l", param);
	Py_DECREF(f);
	if(!v){ logError(fn); return false; }
	if(PyInt_Check(v)) result = PyInt_AsLong(v);
	else if(PyLong_Check(v)) result = PyLong_AsLong(v);
	else if(PyFloat_Check(v)) result = (long)PyFloat_AsDouble(v);	// [V] PyInt_AsLong(result) truncates floats (AI SBuySum returns m * ... / (cp * 100), a float)
	Py_DECREF(v);
	return true;
}

// [V] Card_CallPythonCallbackWithParams @0x4ae650 (Execute(param, speakNo[1], speakNo[2])) / Card_CallPythonCallback @0x4ae580 (Competence()):
// the script module CardPy_NN; false if it is missing or raised
bool rm8PyCardCall(const char *module, bool execute, long param, long sa, long sb, long &result)
{
	result = 0;
	if(!g_ready || !module) return false;
	PyObject *&m = g_mods[module];
	if(!m){ m = PyImport_ImportModule((char*)module); if(!m){ logError(module); return false; } }
	PyObject *f = PyObject_GetAttrString(m, execute ? (char*)"Execute" : (char*)"Competence");
	if(!f){ PyErr_Clear(); return false; }
	PyObject *v = execute ? PyObject_CallFunction(f, (char*)"lll", param, sa, sb) : PyObject_CallFunction(f, nullptr);
	Py_DECREF(f);
	if(!v){ logError(module); return false; }
	if(PyInt_Check(v)) result = PyInt_AsLong(v);
	else if(PyLong_Check(v)) result = PyLong_AsLong(v);
	Py_DECREF(v);
	return true;
}


// [V] SpeakAside_InitPython @0x5141b0 (the script object): the module of the script, its `rolemove` generator function called once, the generator's `next`
bool rm8PyMovieOpen(const char *module)
{
	rm8PyMovieClose();
	if(!g_ready || !module) return false;
	PyObject *m = PyImport_ImportModule((char*)module);
	if(!m){ logError(module); return false; }
	PyObject *f = PyObject_GetAttrString(m, (char*)"rolemove");
	Py_DECREF(m);
	if(!f){ PyErr_Clear(); rm8Log("python: %s has no rolemove", module); return false; }
	g_movGen = PyObject_CallObject(f, nullptr);
	Py_DECREF(f);
	if(!g_movGen){ logError("rolemove"); return false; }
	g_movNext = PyObject_GetAttrString(g_movGen, (char*)"next");
	if(!g_movNext){ PyErr_Clear(); Py_DECREF(g_movGen); g_movGen = nullptr; return false; }
	return true;
}

// next(): the script runs up to its next `yield None`; StopIteration = the script returned. [V] the original calls it again every frame after an error
// (the object stays at +8 == 0); the port ends the script instead
int rm8PyMovieStep()
{
	if(!g_movNext) return 0;
	PyObject *r = PyObject_CallObject(g_movNext, nullptr);
	if(!r){
		if(PyErr_ExceptionMatches(PyExc_StopIteration)){ PyErr_Clear(); return 0; }
		logError("next"); return -1;
	}
	Py_DECREF(r);
	return 1;
}

void rm8PyMovieClose(){ Py_XDECREF(g_movNext); Py_XDECREF(g_movGen); g_movNext = g_movGen = nullptr; }

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_python.inc"
