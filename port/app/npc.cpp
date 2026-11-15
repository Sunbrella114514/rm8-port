// NPC objects on the board: data, placement, spawning, animation, rendering, possession figures. [V] = read from the decompile
// (docs/npc_port.md has the function table), [A] = approximation, [H] = hypothesis.
//
// [V] The NPC manager (*(0x632db4), NpcMgr @0x4071c0) owns an array of NPC objects (class family 0x5e2b50 .. 0x5e3628, all
// derived from the Path/Mesh base like the player characters). Npc_SpawnAll @0x49e2d0 (Game_InitGame @0x4a1090) creates one object
// per npcinfo record with type < 2 (ids 1..11: 6 gods, police / thief / dog, lucky card, chest) in the unplaced state 6, then
// Event_OnNpcEnable @0x49d9c0 places them: every god in state 4 / 6 on its own free node and ONE random board character
// (7..11). Event_OnNpcEnable runs again on day 1 of every month (Game_OnTurnStart @0x4a3b50). Nothing walks by itself:
// gods and board characters stand on their node until a role stops there (message 0x23 -> NPC_HandleInteract @0x49dfb0).
#include "npc_int.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace rw;

namespace npc {

std::map<int, Def> g_defs;
std::vector<int> g_order;			// npcinfo order (the NPC array order of the first objects)
std::map<int, Art> g_arts;
int g_arrowArt[8] = {0, 0, 0, 0, 0, 0, 0, 0};
std::vector<Obj*> g_objs;
std::map<int, Obj*> g_at;			// road node -> NPC ([V] node +0x10)
Rm8Scene *g_s = nullptr;
Camera *g_cam = nullptr;
float g_camYaw = 135.0f;			// [V] camera wrapper +0x0C (same derivation as roles.cpp)
int g_storyPuzzles[2] = {0, 0};
std::vector<Seat> g_seat;
float g_clock = 0;

int rnd(){ return rm8GameRand(); }
float waitScale(){ return 1.0f - std::min((rm8GameSpeed - 1) / 10.0f, 1.0f); }
V3d nodeP(int node){ float p[3] = {0, 0, 0}; rm8TurnNodePos(node, p); return {p[0], p[1], p[2]}; }
Seat &S(int seat){ if(seat >= (int)g_seat.size()) g_seat.resize(seat + 1); return g_seat[seat < 0 ? 0 : seat]; }

// ================================================================ data
bool loadDefs(const std::string &path)
{
	std::ifstream f(path);
	if(!f){ rm8Log("no %s (run scripts/export_npcs.py)", path.c_str()); return false; }
	g_defs.clear(); g_order.clear(); g_arts.clear();
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "npc"){
			Def d; ss >> d.id >> d.type >> d.date >> d.good >> d.sound >> d.scale[0] >> d.scale[1] >> d.scale[2] >> d.artNum;
			for(int k = 0; k < d.artNum && k < 4; k++) ss >> d.art[k];
			for(int k = 4; k < d.artNum; k++){ int dummy; ss >> dummy; }
			ss >> d.name;
			g_defs[d.id] = d; g_order.push_back(d.id);
		}else if(kw == "model"){ int a; ss >> a; ss >> g_arts[a].dff >> g_arts[a].texdir; }
		else if(kw == "anim"){ int a, i; std::string p; ss >> a >> i >> p; g_arts[a].anims[i] = p; }
		else if(kw == "hook"){ int a, h, e; ss >> a >> h >> e; g_arts[a].hooks.push_back({h, e}); }
		else if(kw == "arrow"){ int s, a; ss >> s >> a; if(s >= 0 && s < 8) g_arrowArt[s] = a; }
	}
	rm8Log("npcs: %d records, %d models", (int)g_defs.size(), (int)g_arts.size());
	return !g_defs.empty();
}

// ================================================================ models
bool loadMdl(Mdl &m, int art)
{
	auto it = g_arts.find(art);
	if(it == g_arts.end() || !g_s || !g_s->world) return false;
	m.clump = rm8LoadClump(it->second.dff, it->second.texdir);
	if(!m.clump){ rm8Log("warning: npc model %s not found", it->second.dff.c_str()); return false; }
	m.base = m.clump->getFrame()->matrix; m.base.flags = 0;
	m.p0 = m.base.pos;
	m.hier = HAnimHierarchy::find(m.clump->getFrame());
	if(m.hier) m.hier->attach();
	// [A] skinned parts (the ambulance's doctor / nurse, 275 vertices each) need their hierarchy set, as scene.cpp setupAnim does for the map articles; without it the GL skin
	// matrices are the bind pose against the animated frames and the figure is torn / stretched
	if(m.hier) FORLIST(lnk, m.clump->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		if(a->geometry && Skin::get(a->geometry)){
			if(getenv("RM8_DEBUG_NPC")) rm8Log("npc model %d: skinned atomic, %d bones, hierarchy nodes %d (gl limit %d)", art, Skin::get(a->geometry)->numBones, m.hier->numNodes, gl3::skinMaxBones);
			if(!getenv("RM8_NO_NPCSKIN")) Skin::setHierarchy(a, m.hier);
		}
	}
	if(m.hier){ m.cpu = rm8CpuSkinPrepare(m.clump, m.hier); if(getenv("RM8_DEBUG_NPC") && !m.cpu.atoms.empty()) rm8Log("npc model %d: %d skinned atomics go to the CPU (bone limit)", art, (int)m.cpu.atoms.size()); }
	for(auto &a : it->second.anims){
		Animation *an = rm8LoadAnim(a.second);
		if(an && m.hier && an->getNumNodes() == m.hier->numNodes) m.anims[a.first] = an;
	}
	g_s->world->addClump(m.clump);
	// [V] Atomic_ParseUserDataFlags @0x45c370: the "[@n]" frames carry the article's particle effects (role.bin particle table, as the map articles' hooks)
	if(getenv("RM8_DEBUG_NPC_FLAGS")){ int n = 0; FORLIST(lnk, m.clump->atomics){ Atomic *a = Atomic::fromClump(lnk); UserDataExtension *ud = a->getFrame() ? UserDataExtension::get(a->getFrame()) : nullptr; std::string u; for(int i = 0; ud && i < ud->getCount(); i++){ UserDataArray *arr = ud->get(i); if(arr && arr->datatype == USERDATASTRING && arr->numElements > 0) u += std::string(arr->getString(0)) + " "; } Material *mat = a->geometry && a->geometry->matList.numMaterials ? a->geometry->matList.materials[0] : nullptr; rm8Log("art %d atomic %d user data '%s' verts %d alpha %d tex %s r %.1f flags 0x%x fmt 0x%x", art, n++, u.c_str(), a->geometry ? a->geometry->numVertices : 0, mat ? mat->color.alpha : -1, mat && mat->texture ? mat->texture->name : "-", a->geometry ? a->geometry->morphTargets[0].boundingSphere.radius : 0.0f, a->geometry ? a->geometry->flags : 0u, 0u); } }
	for(auto &h : it->second.hooks){ int e = rm8PrtAttachH(h.second, m.clump, h.first); if(e >= 0) m.prt.push_back(e); }
	return true;
}

void freeMdl(Mdl &m)
{
	for(int e : m.prt) rm8PrtStopH(e);
	if(m.clump){ if(g_s && g_s->world) g_s->world->removeClump(m.clump); m.clump->destroy(); }
	m = Mdl();
}

// advances the animation of a model and writes the node matrices (the same as roles.cpp updateAnim); true when a once animation ended
bool stepAnim(Mdl &m, float dt, bool once)
{
	if(!m.hier || !m.hier->interpolator->currentAnim) return true;
	AnimInterpolator *ip = m.hier->interpolator;
	float step = dt;
	if(once) step = std::min(step, std::max(0.0f, ip->currentAnim->duration - ip->currentTime - 1e-4f));
	ip->addTime(step);
	for(int i = 0; i < m.hier->numNodes; i++){
		Frame *f = m.hier->nodeInfo[i].frame;
		if(!f) continue;
		ip->applyCB(&f->matrix, ip->getInterpFrame(i));
		f->updateObjects();
	}
	m.hier->updateMatrices();
	if(!m.cpu.atoms.empty()) rm8CpuSkinApply(m.cpu, m.hier);
	return ip->currentTime >= ip->currentAnim->duration - 2e-4f;
}

void setAnim(Mdl &m, int anim, bool once)
{
	auto it = m.anims.find(anim);
	if(!m.hier || it == m.anims.end()) return;
	m.hier->interpolator->setCurrentAnim(it->second);
	stepAnim(m, 0, once);
}

// paper doll placement: [V] Anim_PlayMovieClip @0x47bc10 / Mesh_AccumulateScale @0x47aa30 / Mesh_Rotate @0x47a790 (as roles.cpp placeModel)
void placeMdl(Mdl &m, V3d pos, V3d sc, float yaw, V3d off)
{
	if(!m.clump) return;
	Frame *f = m.clump->getFrame();
	V3d np0 = {-m.p0.x, -m.p0.y, -m.p0.z}, ay = {0, 1, 0};
	V3d p1 = {m.p0.x + pos.x + off.x, m.p0.y + pos.y + off.y, m.p0.z + pos.z + off.z};
	f->transform(&m.base, COMBINEREPLACE);
	f->translate(&np0, COMBINEPOSTCONCAT); f->scale(&sc, COMBINEPOSTCONCAT);
	f->rotate(&ay, g_camYaw - 180.0f + yaw, COMBINEPOSTCONCAT); f->translate(&p1, COMBINEPOSTCONCAT);
}

// [V] Path_CalcAngle @0x47bfa0 (sector part, the same as roles.cpp calcAngle): 0..7
int sectorOf(V3d a, V3d b)
{
	V3d d = {b.x - a.x, b.y - a.y, b.z - a.z};
	float len = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z);
	if(len < 1e-4f) return 0;
	d.x /= len;
	int ang = (int)lroundf(acosf(std::max(-1.0f, std::min(1.0f, d.x))) / 3.1416f * 180.0f);
	if(d.z > 0) ang = 360 - ang;
	ang = ((360 - (int)g_camYaw) + ang) % 360;
	unsigned sector = (unsigned)((ang + 20) / 45);
	return sector >= 8 ? 0 : (int)sector;
}
// the facing model (0 fl, 1 fr, 2 br, 3 bl) for a walking direction; cur = the model in use (hysteresis); nm = the model count
int facingModel(int cur, int nm, V3d a, V3d b)
{
	V3d d = {b.x - a.x, b.y - a.y, b.z - a.z};
	if(d.x*d.x + d.y*d.y + d.z*d.z < 1e-8f) return cur;
	unsigned sector = (unsigned)sectorOf(a, b);
	uint32 mask = 1u << (sector * 4);
	int side = (((unsigned)cur % 3 != 0) && (mask & 0x0fffff00u)) || (mask & 0x00fff000u) ? 1 : 0;
	return (nm < 4 || sector < 5) ? side : 3 - side;	// [V] @0x47bfa0: (count < 4) || (sector < 5) -> side
}

void selectModel(Obj &o, int idx)	// [V] Mesh_SelectItem @0x47adf0: the other model continues the current animation
{
	o.cur = idx;
	if(o.anim >= 0) setAnim(o.m[o.cur], o.anim, o.once);
}

// [V] Path_CalcAngle @0x47bfa0 (only with more than one model): spin = param_3: a change to a neighbouring model turns the figure (speed / 100 * 540 degrees / s,
// the model is swapped at 90 degrees), a change to the opposite model is immediate
void facing(Obj &o, V3d a, V3d b, bool spin)
{
	if(o.nm < 2) return;
	int idx = facingModel(o.cur, o.nm, a, b);
	if(idx == o.cur) return;
	if(!spin || (idx % 2 != o.cur % 2 && idx / 2 != o.cur / 2)){ selectModel(o, idx); return; }
	o.spinTarget = idx; o.spinning = true; o.spinAngle = 0;
	o.spinRate = (idx - o.cur == 1 || idx - o.cur == -3) ? 540.0f : -540.0f;	// this[0xd] = 100 for the NPC objects
}

bool spinUpdate(Obj &o, float dt)
{
	if(!o.spinning) return false;
	float d = dt * o.spinRate;
	o.spinAngle += d;
	if(o.spinTarget == o.cur){
		if(fabsf(o.spinAngle) >= 360.0f){
			float over = fabsf(o.spinAngle) - 360.0f;
			d = d <= 0 ? over + d : d - over;
			o.spinning = false;
		}
	}else if(fabsf(o.spinAngle) >= 90.0f){
		if(o.spinRate <= 0){ d -= 180.0f; o.spinAngle -= 180.0f; } else { d += 180.0f; o.spinAngle += 180.0f; }
		selectModel(o, o.spinTarget);
	}
	o.yaw = fmodf(o.yaw + d, 360.0f);
	return true;
}

// ================================================================ objects
Obj *byHandle(int h){ return h >= 1 && h <= (int)g_objs.size() ? g_objs[h - 1] : nullptr; }
int handleOf(const Obj *o){ for(size_t i = 0; i < g_objs.size(); i++) if(g_objs[i] == o) return (int)i + 1; return 0; }
Obj *at(int node){ auto it = g_at.find(node); return it == g_at.end() ? nullptr : it->second; }

Obj *createObj(int id)
{
	auto it = g_defs.find(id);
	if(it == g_defs.end() || !g_s) return nullptr;
	Obj *o = new Obj; o->id = id; o->def = &it->second;
	int n = std::min(it->second.artNum, 4);
	if(n == 2 && it->second.art[0] == it->second.art[1]) n = 1;	// the two identical models of the board items
	for(int k = 0; k < n && o->nm == k; k++) if(loadMdl(o->m[k], it->second.art[k])) o->nm++;
	o->shadow = rm8LoadClump("article\\mend\\mnd_07.dff", "article\\mend\\");	// [V] CPlayerTag / NPC shadow mesh art 0x18a3 (Cls_5e2b50__CardCheck @0x49b220)
	if(o->shadow){ o->shBase = o->shadow->getFrame()->matrix; o->shBase.flags = 0; g_s->world->addClump(o->shadow); }
	g_objs.push_back(o);
	return o;
}

void destroyObj(Obj *o)
{
	for(Mdl &m : o->m) freeMdl(m);
	if(o->shadow){ if(g_s && g_s->world) g_s->world->removeClump(o->shadow); o->shadow->destroy(); }
	if(o->node >= 0 && at(o->node) == o) g_at.erase(o->node);
	delete o;
}

void startAnim(Obj &o, int anim, bool once)
{
	o.anim = anim; o.once = once; o.busy = false;
	if(o.nm == 0) return;
	Mdl &m = o.m[o.cur];
	if(!m.hier || !m.anims.count(anim)) return;
	setAnim(m, anim, once);
	o.busy = once;
}

bool animDone(const Obj &o){ return !o.busy; }

void placeObj(Obj &o)
{
	if(o.nm == 0) return;
	const float *sc = o.def->scale;
	V3d s = {sc[0] * o.scaleF, sc[1] * o.scaleF, sc[2] * o.scaleF};
	placeMdl(o.m[o.cur], o.pos, s, o.yaw, {0, 0, 0});
	if(o.shadow){	// [V] Cls_5e2b50__OnLand @0x49b570: Sprite_SetPositionScale(pos + (0, 1, 0), 0.9 0.9 0.9)
		Frame *sf = o.shadow->getFrame();
		V3d p0 = o.shBase.pos, np0 = {-p0.x, -p0.y, -p0.z}, ss = {0.9f, 0.9f, 0.9f};
		V3d d = {p0.x + o.pos.x, p0.y + o.pos.y + 1.0f, p0.z + o.pos.z};
		sf->transform(&o.shBase, COMBINEREPLACE);
		sf->translate(&np0, COMBINEPOSTCONCAT); sf->scale(&ss, COMBINEPOSTCONCAT); sf->translate(&d, COMBINEPOSTCONCAT);
	}
}

std::vector<char> g_iconNode, g_exitRoad;
// board facts for findFreeNode: [V] node +8 != 0 = the road node owns an event icon (BuildObj_CreateEvent @0x4f19e0: the tile event > 0, or - event <= 0 - the plot word
// 100..103 -> event 15 / 16 / 13 / 14; the icon lines of scene.txt are exactly these); [H] exit roads = the dead-end branches of the prison / hospital
// (u_Path_FindEndpointElement @0x4f0280 -> node +0x30, Path_SelectRandomNext @0x47b8e0 never enters them): every node of the chain from the junction's first
// dead-end link to the dead end. The decompile of FindAvailableNPC has no such test - the event icons (events 7 / 8 and the 10 / 11 / 12 pickups along the roads)
// already keep the NPCs off the exit roads of 14 of the 15 maps; download201 has one free 2-link node (26,45) on its prison road, which this rule excludes.
void buildNodeInfo()
{
	int N = rm8GameNodeCount();
	g_iconNode.assign(N, 0); g_exitRoad.assign(N, 0);
	if(!g_s) return;
	for(int i = 0; i < N; i++) if(rm8GameNodeEvent(i) > 0) g_iconNode[i] = 1;
	for(const Rm8Icon &ic : g_s->icons) for(int i = 0; i < N; i++) if(g_s->rnodes[i].tx == ic.tx && g_s->rnodes[i].ty == ic.ty){ g_iconNode[i] = 1; break; }
	for(int i = 0; i < N; i++){
		int L[16]; int nl = rm8TurnLinks(i, L, 16);
		if(nl <= 2) continue;
		for(int k = 0; k < nl; k++){	// the first link whose chain of 2-link nodes ends in a node with < 2 links
			std::vector<int> chain; int prev = i, c = L[k]; bool dead = false;
			for(int guard = 0; guard < N; guard++){
				int M[16]; int mc = rm8TurnLinks(c, M, 16);
				chain.push_back(c);
				if(mc < 2){ dead = true; break; }
				if(mc > 2 || c == i) break;
				int nx = M[0] != prev ? M[0] : M[1];
				prev = c; c = nx;
			}
			if(dead){ for(int q : chain) g_exitRoad[q] = 1; break; }
		}
	}
}

// [V] TerraMgr_FindAvailableNPC @0x4f2610: road nodes with no event icon (+8), exactly 2 links (+0x2c), not the hospital / prison node (the manager's +0x40 / +0x44:
// Land_CreateObject_f1320 @0x4f1320 stores the tile of event 8 / 7 there) and no NPC / living role within graph distance 2 (Graph_BFS_FindNeighbors @0x4f27f0 +
// Find_NthOwned @0x4f0510); the first 128 in node order, one picked with rand() % count. [H] plus the exit roads (buildNodeInfo)
int findFreeNode()
{
	int N = rm8GameNodeCount();
	if((int)g_iconNode.size() != N) buildNodeInfo();
	std::vector<int> cand;
	for(int i = 0; i < N && (int)cand.size() < 128; i++){
		if(g_iconNode[i] || rm8GameNodeEvent(i) == 7 || rm8GameNodeEvent(i) == 8 || g_exitRoad[i]) continue;
		int L[16]; int nl = rm8TurnLinks(i, L, 16);
		if(nl != 2) continue;
		std::vector<int> set; set.push_back(i);
		size_t lo = 0;
		for(int layer = 0; layer < 2; layer++){
			size_t hi = set.size();
			for(size_t k = lo; k < hi; k++){
				int nb[16]; int c = rm8TurnLinks(set[k], nb, 16);
				for(int j = 0; j < c; j++) if(std::find(set.begin(), set.end(), nb[j]) == set.end()) set.push_back(nb[j]);
			}
			lo = hi;
		}
		bool ok = true;
		for(int q : set){
			if(at(q)){ ok = false; break; }
			for(int s = 0; s < rm8TurnRoleCount() && ok; s++) if(rm8TurnStatus(s) != 7 && rm8TurnNode(s) == q) ok = false;
			if(!ok) break;
		}
		if(ok) cand.push_back(i);
	}
	return cand.empty() ? -1 : cand[rnd() % cand.size()];
}

// [V] vf+0x64 Cls_5e2b50__OnLand @0x49b570: needs a free node and state 4 / 6; vf+0x8c CalcScale (0.7 x config scale for the gods and 7..9, @0x49bc70 /
// 0x49c410 / 0x49c5b0 / 0x49c8d0, none for the others), vf+0x90 animation (2 loop for the figures, 0 for the items, none for mine / bomb),
// Path_SetPoints(node, link 0)
bool onLand(Obj &o, int node)
{
	if(node < 0 || at(node) || (o.state != 4 && o.state != 6)) return false;
	int L[16]; int nl = rm8TurnLinks(node, L, 16);
	o.pos = nodeP(node);
	bool figure = o.def->type == 0 || (o.id >= 7 && o.id <= 9);
	o.scaleF = figure ? 0.7f : 1.0f;
	o.node = node; o.state = 0; o.stage = 0; o.actor = -1; o.timer = 0; o.collectPending = false; o.busy = false; o.cur = 0;
	g_at[node] = &o;
	if(getenv("RM8_DEBUG_NPC")) rm8Log("npc: %s placed on node %d", o.def->name.c_str(), node);
	if(nl > 0) facing(o, o.pos, nodeP(L[0]));
	if(figure) startAnim(o, 2, false);
	else if(o.id == 10 || o.id == 11 || o.id == 12 || o.id == 17) startAnim(o, 0, false);
	else o.anim = -1;
	placeObj(o);
	return true;
}

// [V] vf+0x68: Cls_5e2c80__Finalize @0x49b6c0 (state 4; the actor leaves the NPC state 10, the node link is cleared); Cls_5e2db0__Finalize @0x49cd20
// adds state 5 (deleted) for the obstacles 12 13 14, the robot doll 15, the egg 23 and the puzzle piece 17
void finalize(Obj &o)
{
	o.state = 4;
	if(o.actor >= 0){
		if(rm8TurnState(o.actor) == 10){ rm8TurnSetState(o.actor, 8); S(o.actor).hidden = false; }	// [V] Role_LeaveState10 @0x486110: Role_SetFlag1C8 (the role is drawn again), state 8
		o.actor = -1;
	}
	if(o.node >= 0 && at(o.node) == &o) g_at.erase(o.node);
	o.node = -1;
	if((o.id >= 12 && o.id <= 15) || o.id == 17 || o.id == 23) o.state = 5;
}

// [V] Cls_5e2b50__Update @0x49b350 + vf4 (draw): every object that is not gone animates; the interaction state machines run in npc_act.cpp
void updateAll(float dt)
{
	for(size_t i = 0; i < g_objs.size(); i++){
		Obj &o = *g_objs[i];
		if(o.state >= 4) continue;
		if(o.nm > 0){
			Mdl &m = o.m[o.cur];
			if(stepAnim(m, dt, o.once)) o.busy = false;
		}
		actUpdate(o, dt);
		if(o.state < 4) placeObj(o);
	}
	// [V] PlayerTag_OnDeleteEvent @0x49e110: state 5 objects are removed from the array (later handles shift down)
	for(size_t i = 0; i < g_objs.size();){
		if(g_objs[i]->state == 5){ destroyObj(g_objs[i]); g_objs.erase(g_objs.begin() + i); }
		else i++;
	}
}

void renderAll()
{
	transferRender();
	for(Obj *op : g_objs){
		Obj &o = *op;
		if(o.state >= 4 || o.collectPending || o.nm == 0) continue;	// [V] Cls_5e33a8__OnUpdate @0x49b910: state < 4 and +0x7c == 0
		rm8RenderArticleClump(o.m[o.cur].clump);	// the article path: user-data flags, two passes
		if(o.state == 0 && o.shadow){	// [V] Mesh_DrawWithAlphaTest @0x47bbc0: alpha test ALWAYS (state 0 only)
			uint32 fn = GetRenderState(ALPHATESTFUNC);
			SetRenderState(ALPHATESTFUNC, ALPHAALWAYS);
			o.shadow->render();
			SetRenderState(ALPHATESTFUNC, fn);
		}
	}
}

// [V] Event_OnNpcEnable @0x49d9c0: every type 0 object in state 4 / 6 goes to a free node; of the type 1 objects in state 4 / 6 ONE random one
void onNpcEnable()
{
	std::vector<size_t> pool;
	for(size_t i = 0; i < g_objs.size(); i++){
		Obj *o = g_objs[i];
		if(o->state != 4 && o->state != 6) continue;
		if(o->def->type == 0){ int n = findFreeNode(); if(n >= 0) onLand(*o, n); }
		else if(o->def->type == 1) pool.push_back(i);
	}
	if(!pool.empty()){
		Obj *o = g_objs[pool[rnd() % pool.size()]];
		int n = findFreeNode();
		if(n >= 0) onLand(*o, n);
	}
}

// test hook RM8_NPC_SPAM=n: n more Event_OnNpcEnable rounds right away (every placed god / board character is taken off first), the nodes are logged
void spamEnable(int n)
{
	for(int k = 0; k < n; k++){
		for(Obj *o : g_objs) if(o->def->type < 2 && o->state == 0) finalize(*o);
		onNpcEnable();
		for(Obj *o : g_objs) if(o->def->type < 2 && o->state == 0) rm8Log("npc spam: %d on node %d", o->id, o->node);
	}
}

// ================================================================ possessed role figures
// [V] Role_SetPossession @0x48b6e0: the role's mesh list becomes the NPC's art list (vf1 CreateMeshList(+0x20c, artNum)); Role_FinalizeMovement
// @0x481af0: scale = status shape (CPlayerTag__GetStatusShape @0x490bf0 = npcinfo scale) x the same-node tween scale
struct Vis {
	int shape = 0; const Def *def = nullptr;
	Mdl m[4]; int nm = 0, cur = 0, anim = -1; bool once = false;
	V3d pos = {0, 0, 0}, off = {0, 0, 0}; float yaw = 0, tw = 1;
	int sector = 0;
};
std::vector<Vis> g_vis;
Mdl g_arrow[8]; bool g_arrowTried = false;

Vis *visOf(int seat){ return seat >= 0 && seat < (int)g_vis.size() && g_vis[seat].shape ? &g_vis[seat] : nullptr; }

void freeVis(int seat)
{
	if(seat < 0 || seat >= (int)g_vis.size()) return;
	for(Mdl &m : g_vis[seat].m) freeMdl(m);
	g_vis[seat] = Vis();
}

void possessedCleanup()
{
	for(size_t s = 0; s < g_vis.size(); s++) freeVis((int)s);
	g_vis.clear();
	for(Mdl &m : g_arrow) freeMdl(m);
	g_arrowTried = false;
}

bool loadVis(int seat, const Def &d)
{
	if(seat < 0) return false;
	if((int)g_vis.size() <= seat) g_vis.resize(seat + 1);
	freeVis(seat);
	if(d.id >= 100 || d.id == 16) return false;	// [A] the mini-game forms use role art placeholders: the role keeps its own models
	Vis &v = g_vis[seat];
	int n = std::min(d.artNum, 4);
	for(int k = 0; k < n && v.nm == k; k++) if(loadMdl(v.m[k], d.art[k])) v.nm++;
	if(v.nm == 0){ freeVis(seat); return false; }
	v.shape = d.id; v.def = &d;
	return true;
}

// [V] vf5 CPlayerTag__vf5_SetTextureState @0x48a270 with the possessed table 0x5e1d94: logical 0 / 1 / 2 stay, 3..13 -> 2, 14 -> 3
int possessedAnim(int logical){ return logical <= 2 ? std::max(0, logical) : logical == 14 ? 3 : 2; }

void loadArrows()
{
	if(g_arrowTried) return;
	g_arrowTried = true;
	for(int k = 0; k < 8; k++) if(g_arrowArt[k]) loadMdl(g_arrow[k], g_arrowArt[k]);
}

// debug (RM8_DEBUG_NPC_ARROW): the world direction an arrow model points to (the vertex farthest from the centroid in XZ, after placement) against the
// world direction its sector stands for (Path_CalcAngle @0x47bfa0: ang = (360 - camYaw + theta) % 360, sector = (ang + 20) / 45)
void debugArrow(int k)
{
	Mdl &m = g_arrow[k]; if(!m.clump) return;
	Frame *f = m.clump->getFrame(); const Matrix *M = f->getLTM();
	float cx = 0, cz = 0; int n = 0; std::vector<V3d> pts;
	FORLIST(lnk, m.clump->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		for(int i = 0; i < g->numVertices; i++){ V3d v = g->morphTargets[0].vertices[i], w; V3d::transformPoints(&w, &v, 1, M); pts.push_back(w); cx += w.x; cz += w.z; n++; }
	}
	if(!n) return;
	cx /= n; cz /= n;
	float best = -1; V3d tip = {0, 0, 0};
	for(V3d &w : pts){ float d = (w.x - cx) * (w.x - cx) + (w.z - cz) * (w.z - cz); if(d > best){ best = d; tip = w; } }
	float ax = atan2f(-(tip.z - cz), tip.x - cx) * 180.0f / 3.14159265f; if(ax < 0) ax += 360.0f;	// world direction (cos, -sin) as in Path_CalcAngle
	float th = (float)((int)(45 * k) - 360 + (int)g_camYaw); while(th < 0) th += 360.0f; while(th >= 360.0f) th -= 360.0f;
	rm8Log("arrow debug: sector %d art %d tip direction %.0f deg, sector direction %.0f deg", k, g_arrowArt[k], ax, th);
}

}	// namespace npc

using namespace npc;

// ================================================================ public API
bool rm8NpcLoadDefs(const std::string &p){ return loadDefs(p); }
void rm8NpcSetCamera(Camera *cam)
{
	g_cam = cam;
	const V3d &at = cam->getFrame()->getLTM()->at;
	if(fabsf(at.x) + fabsf(at.z) < 1e-4f) return;
	float y = atan2f(-at.z, -at.x) * 180.0f / 3.14159265f;
	if(y < 0) y += 360.0f;
	g_camYaw = y;
}

int rm8NpcCount(){ return (int)g_objs.size(); }
bool rm8NpcRoleHidden(int seat){ return seat >= 0 && seat < (int)g_seat.size() && g_seat[seat].hidden; }
void rm8NpcSetRoleHidden(int seat, bool hidden){ if(seat >= 0 && seat < (int)g_seat.size()) g_seat[seat].hidden = hidden; }	// Role_ClearFlag1C8 / Role_SetFlag1C8 (the UFO news: RoleAnimation_TriggerAction @0x4B1470)
bool rm8NpcOnMap(int h){ Obj *o = byHandle(h); return o && o->state == 0 && o->nm > 0; }
int rm8NpcNode(int h){ Obj *o = byHandle(h); return o && o->state < 4 ? o->node : -1; }
int rm8NpcAt(int node){ Obj *o = at(node); return o ? handleOf(o) : 0; }
int rm8NpcId(int h){ Obj *o = byHandle(h); return o ? o->id : 0; }
const char *rm8NpcName(int h){ Obj *o = byHandle(h); return o && o->def ? o->def->name.c_str() : ""; }
int rm8NpcShape(int seat){ return rm8GameShape(seat); }
int rm8NpcBully(int owner){ return owner >= 0 && owner < (int)g_seat.size() ? g_seat[owner].bullyBy : -1; }
int rm8NpcTimeBomb(int seat){ return seat >= 0 && seat < (int)g_seat.size() ? g_seat[seat].bomb : 0; }

void rm8NpcStory(const int p[2]){ g_storyPuzzles[0] = p ? p[0] : 0; g_storyPuzzles[1] = p ? p[1] : 0; }

void rm8NpcClear(Rm8Scene &s)
{
	g_s = &s;
	actClear();
	possessedCleanup();
	for(Obj *o : g_objs) destroyObj(o);
	g_objs.clear(); g_at.clear(); g_seat.clear();
	g_s = nullptr;
}

// [V] Npc_SpawnAll @0x49e2d0
void rm8NpcNewGame(Rm8Scene &s)
{
	rm8NpcClear(s);
	g_s = &s;
	g_seat.assign(rm8TurnRoleCount(), Seat());
	g_vis.assign(rm8TurnRoleCount(), Vis());
	buildNodeInfo();
	for(int id : g_order){
		const Def &d = g_defs[id];
		if(d.type < 2) createObj(id);	// [V] config type < 2: *(local_c + 0x200) < 2
	}
	// [V] Event_ProcessLanding @0x49db20 (story mode only): the stage's puzzle pieces that the profile does not have yet
	for(int k = 0; k < 2; k++) if(g_storyPuzzles[k] > 0){
		if(Obj *o = createObj(17)){
			o->param = g_storyPuzzles[k];
			int n = findFreeNode();
			if(n >= 0) onLand(*o, n);
		}
	}
	onNpcEnable();
	actNewGame();
	int placed = 0; for(Obj *o : g_objs) if(o->state == 0) placed++;
	rm8Log("npcs: %d objects, %d placed on the map", (int)g_objs.size(), placed);
}

void rm8NpcMonthStart(){ if(g_s) onNpcEnable(); }

void rm8NpcUpdate(float dt)
{
	if(!g_s) return;
	g_clock += dt;
	updateAll(dt);
	actUpdateGlobal(dt);
}

void rm8NpcRender(){ if(g_s) renderAll(); }

void rm8NpcRenderLate(Camera *cam){ (void)cam; }	// the time bomb is a 3D billboard on the role's back now (roles.cpp bombDraw, [V] Billboard_RenderWithNumber @0x499ef0); the old head-top 2D counter (npc_ui.cpp bombRender) is no longer drawn

int rm8NpcCreate(int id, int node, int owner)
{
	auto it = g_defs.find(id);
	if(!g_s || it == g_defs.end() || it->second.type < 2) return 0;	// [V] Npc_CreateInstance @0x49f0e0: config type >= 2 only
	if(id == 15){	// [V] py_Npc_Create @0x500160: the robot doll queues 0x32, 0x65(0x32), 0x1E for the current role
		int cur = rm8TurnCurrent();
		rm8TurnEnqueue(cur, 0x32, 0, 0); rm8TurnEnqueue(cur, 0x65, 0x32, 0); rm8TurnEnqueue(cur, 0x1e, 0, 0);
		return 1;
	}
	Obj *o = createObj(id);
	if(!o) return 0;
	if(node >= 0 && !onLand(*o, node)) o->state = 5;	// [A] the node is taken: the original keeps an invisible object, the port drops it
	o->owner = owner;
	int snd = id == 12 ? 0xc : id == 13 ? 0x10 : id == 14 ? 0x11 : -1;	// [V] py_Npc_Create: samples for the roadblock / mine / time bomb
	if(snd >= 0) rm8SndPlay(snd);
	return handleOf(o);
}

bool rm8NpcRemove(int h)
{
	Obj *o = byHandle(h);
	if(!o) return false;
	finalize(*o);
	rm8SndPlay(0x23);	// [V] py_Npc_Disappear @0x5003b0
	return true;
}

// ---- possession
// [V] Role_SetPossession @0x48b6e0 (message 0x24)
bool rm8NpcBecome(int seat, int shape, int days)
{
	if(seat < 0 || seat >= rm8TurnRoleCount()) return false;
	Seat &s = S(seat);
	s.hidden = false;	// [V] Role_SetPossession @0x48b6e0 ends with Role_SetFlag1C8: the role is drawn again
	if(shape == 0){
		int old = rm8GameShape(seat);
		if(getenv("RM8_DEBUG_NPC")) rm8Log("npc: seat %d is released from shape %d", seat, old);
		if(old) for(Obj *o : g_objs) if(o->id == old){ if(o->state == 4) o->state = 6; break; }	// [V] Npc_PlayAction @0x49df20 -> vf+0x6c NextState (4 -> 6)
		rm8GameSetShape(seat, 0);
		s.possDays = 0;
		freeVis(seat);
		rm8TurnPossessSync(seat, false);	// [V] the role's own models and speed[vehicle - 1] again
		return true;
	}
	auto it = g_defs.find(shape);
	if(it == g_defs.end()) return false;	// [V] NPC_FindByID == 0: nothing changes
	const Def &d = it->second;
	if(shape == 16){	// [V] the road bully: +0x1C0 = the current role's seat, +0x1E0 = npcinfo date (no shape, no figure)
		s.bullyBy = rm8TurnCurrent(); s.bullyDays = days > 0 ? days : d.date;
		return true;
	}
	static const bool dbg = getenv("RM8_DEBUG_NPC") != nullptr;
	if(dbg) rm8Log("npc [%.2f]: seat %d becomes %s (%d) for %d days", g_clock, seat, d.name.c_str(), shape, days > 0 ? days : d.date);
	rm8GameSetShape(seat, shape);
	s.possDays = days > 0 ? days : d.date;	// [V] +0x1DC = NPC +0x228
	loadVis(seat, d);
	rm8TurnPossessSync(seat, true);		// [V] +0x34 = roleinfo speed[0] while possessed
	return true;
}

// [V] Role_TickPossessionExpire @0x483350 (turn start step 6)
void rm8NpcTurnStep(int seat)
{
	if(seat < 0 || seat >= rm8TurnRoleCount()) return;
	Seat &s = S(seat);
	if(s.bullyBy >= 0 && --s.bullyDays < 1){ s.bullyBy = -1; s.bullyDays = 0; }	// [V] +0x1C0 / +0x1E0: the road bully ends
	int shape = rm8GameShape(seat);
	if(shape == 0) return;
	if(s.possDays < 100 && --s.possDays < 1) s.possDays = 0; else return;	// [V] Role_DecPossessionDays @0x48bcb0 (>= 100 = unlimited)
	if(rm8TurnStatus(seat) == 7) return;
	rm8TurnSetState(seat, 8);
	auto it = g_defs.find(shape);
	if(it != g_defs.end()){
		std::string t = rm8StrFmt(433, it->second.name.c_str());	// [V] str433 "%s離開~@073"
		size_t at = t.rfind('@'); int snd = 0;
		if(at != std::string::npos){ snd = atoi(t.c_str() + at + 1); t = t.substr(0, at); }
		if(snd > 0) rm8TurnEnqueue(seat, 0x53, snd, -1);
		rm8TurnEnqueue(seat, 0x1B, rm8GamePushText(t), 0);
	}
	if(rm8TurnStatus(seat) < 3) rm8TurnEnqueue(seat, 0x2c, 0x17ea, 0x1e);	// [V] Role.PlayPrt effect 6122 on the role
	rm8TurnEnqueue(seat, 0x24, 0, 0);					// [V] Role_SetPossession(0)
	if(it != g_defs.end() && it->second.good == 0) rm8RoleSay(seat, 0x18);	// [V] thunk_FUN_00484890(0x18) when the NPC's good flag (+0x22C) is 0
}

// [V] Role_ShowSleepwalkAndRoll @0x48b140: str564 "%s夢遊中\還有%d天~", then the automatic roll 0x1A(0)
bool rm8NpcSleepwalk(int seat)
{
	if(seat < 0 || rm8GameShape(seat) != 6) return false;
	rm8TurnEnqueue(seat, 0x1B, rm8GamePushText(rm8StrFmt(564, rm8TurnName(seat), S(seat).possDays)), 0);
	rm8TurnEnqueue(seat, 0x1a, 0, 0);
	rm8TurnSetState(seat, 8);
	return true;
}

// ---- possessed role figures (roles.cpp hooks)
bool rm8NpcRoleShape(int seat, float scale[3])
{
	Vis *v = visOf(seat);
	if(!v) return false;
	for(int k = 0; k < 3; k++) scale[k] = v->def->scale[k];
	return true;
}

void rm8NpcRoleUpdate(int seat, const float pos[3], int facingIdx, int anim, bool once, float yaw, float tw, const float off[3], float dt)
{
	Vis *v = visOf(seat);
	if(!v) return;
	// [V] Path_CalcAngle @0x47bfa0 only turns a mesh list with more than one model: the single paper doll of a god (1..6) never spins (the port's role keeps its own
	// four models hidden underneath and would hand its spin angle over)
	v->pos = {pos[0], pos[1], pos[2]}; v->off = {off[0], off[1], off[2]}; v->yaw = v->nm >= 2 ? yaw : 0.0f; v->tw = tw;
	int cur = v->nm >= 4 ? std::max(0, std::min(3, facingIdx)) : 0;
	int a = possessedAnim(anim);
	if(cur != v->cur || a != v->anim || once != v->once){ v->cur = cur; v->anim = a; v->once = once; setAnim(v->m[v->cur], a, once); }
	stepAnim(v->m[v->cur], dt, once);
	// the ground arrow: the gods have a single model, so the facing is shown by one of the 8 arrows (CPlayerTag +0x39C, vf14 @0x4915e0 called by Path_CalcAngle)
	int from = rm8TurnNode(seat), to = rm8TurnNodeTo(seat);
	if(from >= 0 && to >= 0 && from != to) v->sector = sectorOf(nodeP(from), nodeP(to));
}

bool rm8NpcRoleRender(int seat)
{
	Vis *v = visOf(seat);
	if(!v || !v->m[v->cur].clump) return false;
	V3d sc = {v->def->scale[0] * v->tw, v->def->scale[1] * v->tw, v->def->scale[2] * v->tw};
	placeMdl(v->m[v->cur], v->pos, sc, v->yaw, v->off);
	rm8RenderArticleClump(v->m[v->cur].clump);
	if(v->shape >= 1 && v->shape <= 6){	// [V] CPlayerTag__RenderNpcModel @0x4915a0 (shape 1..6): the facing arrow, scale (tw, sx tw / sy, sx tw / sz), y + 3
		loadArrows();
		Mdl &a = g_arrow[v->sector & 7];
		if(a.clump){
			float sx = v->def->scale[0] * v->tw;
			V3d as = {v->tw, sx / v->def->scale[1], sx / v->def->scale[2]};
			placeMdl(a, {v->pos.x, v->pos.y + 3.0f, v->pos.z}, as, 0, {0, 0, 0});
			a.clump->render();
		}
	}
	return true;
}

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_npc.inc"
