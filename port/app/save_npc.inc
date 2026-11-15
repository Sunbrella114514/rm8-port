// save / load hooks of npc.cpp (included at the end of npc.cpp). See save.h, docs/save_port.md.
// Section [V] FUN_0049e480: the NPC objects in array order: NPC id, (id 0x11 = puzzle piece: NPC +0x94), NPC +0x68 (state: 0 on the map, 4 gone, 5 deleted, 6 not
// placed), the road node (index into Rm8Scene::rnodes, -1 none). Role record: possession days (+0x1DC), road bully beneficiary / days (+0x1C0 / +0x1E0), time bomb
// counter (+0x288); the possessing god's figure is loaded again through rm8NpcBecome.
#include "save.h"

void rm8SaveNpcWrite(Rm8SaveW &w)
{
	w.i32((int)g_objs.size());
	for(const Obj *o : g_objs){
		w.i32(o->id);
		if(o->id == 0x11) w.i32(o->param);
		w.i32(o->state); w.i32(o->node);
	}
}

bool rm8SaveNpcRead(Rm8SaveR &r)
{
	if(!g_s) return false;
	int n = r.i32();
	if(r.bad || n < 0 || n > 4096) return false;
	actClear();
	for(Obj *o : g_objs) destroyObj(o);
	g_objs.clear(); g_at.clear();
	buildNodeInfo();
	for(int i = 0; i < n; i++){
		int id = r.i32(), param = 0;
		if(id == 0x11) param = r.i32();
		int state = r.i32(), node = r.i32();
		if(r.bad) return false;
		Obj *o = createObj(id);
		if(!o) continue;	// an id this build has no data for: the object is dropped
		o->param = param;
		if(state >= 1 && state <= 3) state = node >= 0 ? 0 : 6;	// an interaction in progress cannot be resumed: the NPC stands on its node again
		if(state == 0 && node >= 0 && node < rm8GameNodeCount()){ o->state = 6; if(!onLand(*o, node)) o->state = 6; }
		else o->state = state == 0 ? 6 : state;
	}
	return true;
}

void rm8SaveNpcRoleGet(int seat, Rm8SaveRole &r)
{
	if(seat < 0) return;
	const Seat &s = S(seat);
	r.possDays = s.possDays; r.bullyBy = s.bullyBy; r.bullyDays = s.bullyDays; r.bomb = s.bomb;
}
void rm8SaveNpcRoleSet(int seat, const Rm8SaveRole &r)
{
	if(seat < 0) return;
	Seat &s = S(seat);
	s.bullyBy = r.bullyBy >= 0 && r.bullyBy < rm8TurnRoleCount() ? r.bullyBy : -1; s.bullyDays = std::max(0, r.bullyDays); s.bomb = std::max(0, r.bomb); s.hidden = false;
	if(r.shape > 0 && r.shape != 16){ rm8NpcBecome(seat, r.shape, r.possDays); }	// loads the god's figure and the possessed speed
	s.possDays = std::max(0, r.possDays);
}

// extension chunk "NPCS": the creating seat of every object (placed roadblocks / mines / bombs credit it)
static void saveNpcExtWrite(Rm8SaveW &w)
{
	w.i32((int)g_objs.size());
	for(const Obj *o : g_objs) w.i32(o->owner);
}
static void saveNpcExtRead(Rm8SaveR &r)
{
	int n = r.i32();
	for(int i = 0; i < n && !r.bad; i++){ int ow = r.i32(); if(i < (int)g_objs.size()) g_objs[i]->owner = ow; }
}
RM8_SAVE_EXT(NPCS, 25, saveNpcExtWrite, saveNpcExtRead);
