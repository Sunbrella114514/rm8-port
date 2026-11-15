// Player characters ("roles") on the board, after CPlayerTag (vtable 0x5e1f00) and its mesh-list base class.
// Facts marked [V] are read from the game code (docs/roles.md); [A] are approximations / demo logic.
//
// [V] A character is 4 paper-doll clumps (one per facing, roleinfo artId order fl fr br bl = model index 0..3).
//     Each body part is a textured quad on an HAnim node; the active clump is the one for the walking direction.
// [V] Placement (Anim_PlayMovieClip @0x47bc10, Mesh_AccumulateScale @0x47aa30, Mesh_Rotate @0x47a790):
//     scale = roleinfo scale (1.3 1.6 1) in model space, then yaw = camera yaw - 180 (camera yaw is 135 on the
//     board), pivot = clump frame position; drawn translated by roleinfo offset (Role_DrawModelPasses @0x485e40)
//     with alpha test >= 196 (render states 0x1d = 7, 0x1e = 0xc4).
// [V] Facing (Path_CalcAngle @0x47bfa0): a = acos(dir.x) in whole degrees, 360 - a when dir.z > 0;
//     s = ((360 - camYaw + a) % 360 + 20) / 45 (8 sectors); model = sector rule with hysteresis below.
//     Changing to a model that differs in one bit of the index spins the doll (Anim_RotateSmooth @0x47bcd0):
//     540 deg/s * speed/100, model swap at 90 deg (+-180 jump), stop at 360; movement waits while spinning.
// [V] Walking (Path_AnimateAlongRoute @0x47b730): distance dt * speed (roleinfo speed1 for walking) toward the
//     next node; at a node: snap, choose the next node (Path_SelectRandomNext @0x47b8e0: random among links that
//     are not the previous node, not the dead-end branch, and have > 1 link; back to the previous node if none),
//     re-aim, count one step; the last step ends on the node. While moving dt is scaled by
//     ((init.txt speed - 1) / 4 + 1) (CPlayerTag__vf3_Update @0x481500).
// [V] Animations (CPlayerTag vf5 Role_SetAnimPose -> Article_SelectByTypeAndSetProperties): 0 walk (loop),
//     1 dice throw (once), 2 stand (loop); role.bin sub-list 3 swaps the "face" (or "head") texture per animation
//     at given clip times (Article_Update @0x448540 -> 0x44bdc0); material parts are named by user data "name".
// [V] Body colour textures A%02d_body_<f|b>_%02d (Asset_LoadCharacterTexture @0x4934a0): front for models 0,1.
// [V] Turn flow (docs/turn_flow.md): per-role message queue {cmd,a,b} and state machine (+0x1B8: 0 idle, 1 walk,
//     3 dice, 5 wait, 6 waiting for the human, 8 dispatch one message per frame); dice (dice.cpp); same-node
//     spreading (Game_MoveRoleToPosition @0x4975f0, Game_UpdateRoleDirection @0x497a10, tween @0x4818d0);
//     camera focus snaps (Camera_SetFocusPoint @0x41d850); turn arrow art 6114 only in state 6.
// [A] Rules are not in yet: land events (0x1d/0x23/0x12/0x11) do nothing, computers always throw all dice.
#include "turn.h"
#include "events.h"
#include "cards.h"
#include "present.h"
#include "boardfx.h"
#include "stock.h"
#include "npc.h"
#include "news.h"
#include "skip.h"
#include "story.h"
#include "audio.h"
#include "save.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace rw;

int rm8GameSpeed = 4;

namespace {

struct FaceKey { float t; std::string part, tex; };
struct ArtDef { std::string dff, texdir; std::map<int, std::string> anims; std::map<int, std::vector<FaceKey>> faces; };
struct RoleDef { int id = 0; float scale[3] = {1, 1, 1}, offset[3] = {0, 0, 0}, speed[3] = {100, 100, 100}; int art[12] = {}; float dice[3][3] = {}; int sex = 0; std::string pic, ai, name; int wlkSnd[3] = {0, 0, 0}; };
std::string g_arrowDff, g_arrowTex, g_arrowAnm;
std::map<int, ArtDef> g_arts;
std::map<int, RoleDef> g_roles;
std::map<std::string, Texture*> g_faceTex;

// [V] camera wrapper +0x0C = 135 on the board (read from the running game); the port follows its own camera:
// yaw = atan2(-at.z, -at.x) (game camera at = (0.54, -0.64, -0.54) gives 135), see rm8RolesSetCamera
float CAM_YAW = 135.0f;

struct Model {
	int art = 0;
	Clump *clump = nullptr;
	HAnimHierarchy *hier = nullptr;
	Matrix base;
	V3d p0 = {0, 0, 0};
	std::map<int, Animation*> anims;
	std::map<std::string, std::vector<Material*>> parts;	// material user data "name" -> materials
};

struct Role {
	const RoleDef *def = nullptr;
	int seat = 0, color = 0;
	Model m[4];
	Clump *shadow = nullptr; Matrix shBase;	// [V] blob shadow: art 6307 mend\mnd_07 (Role_CreateAvatar @0x480b30)
	int cur = 0;				// facing model 0 fl, 1 fr, 2 br, 3 bl
	int anim = 2; bool once = false;
	std::string face;			// texture key of the last face swap applied
	V3d pos = {0, 0, 0};
	float yaw = 0;			// [V] Mesh +0x1C minus the camera part (camYaw - 180): the spin angle
	float speed = 100;			// [V] this[0xd]
	int from = -1, to = -1, steps = 0;
	int state = 0;				// [V] +0x1B8 turn state (see header)
	bool spinning = false; int spinTarget = 0; float spinAngle = 0, spinRate = 0;
	bool human = false;			// [V] +0x1B0 == 0
	bool autoPlay = false;			// [V] +0x1B0 == 2: a human seat taken over by the computer (Role_EnableAutoPlay @0x48a4d0); human is false meanwhile, so every computer path applies (docs/autoplay.md)
	bool intro = false;			// [V] +0x1B4 == 8: placed at the game start (vf24 @0x483110), the first 0x68 plays the introduction (Role_LeaveStatus8 @0x483540)
	int status = 0;				// [V] +0x1B4 (7 bankrupt)
	int diceMax = 1, diceCount = 1;		// [V] +0x22C max dice (= vehicle), +0x230 dice this throw
	bool diceOn[3] = {true, false, false}, dicePrevOn[3] = {true, false, false}; int dicePrev = 1;	// [V] the slot flags +0x23C..+0x244 (slot 1 is always on; slots 2 / 3 are the go panel's toggles), their copy +0x248.. and the previous count +0x234 (Dice_SetCount @0x47da00)
	int sndH = 0;				// [V] +0x48 walking loop sample handle
	struct Msg { int cmd, a, b; };
	std::vector<Msg> queue;			// [V] +0x5D0
	float waitT = 0;			// [V] +0x1E8
	float idleT = 0, idleGap = 15; bool special = false;	// [V] +0x1EC/+0x1F0 idle action, +0x5C special action playing
	bool tw = false; float twScale = 1, twTScale = 1; V3d twOff = {0, 0, 0}, twTOff = {0, 0, 0};	// [V] +0x1F8..+0x218
};

struct Graph {
	std::vector<std::vector<int>> links;
	std::vector<int> deadEnd;	// [V] node +0x30
};

struct Follower;
void followerPush(Role &r);	// [V] vf26 / vf18 of the classes 0x5e27d8 / 0x5e2918: u_Path_PushItemAndSetFlags(role +0x44) (the follower companion, below)
std::vector<Role> g_role;
bool g_placedOnly = false;	// characters placed for a comparison shot: no demo turns
Graph g_graph;
Rm8Scene *g_scene = nullptr;
std::vector<Rm8RoadNode> g_nodes;
uint32 g_seed = 1;
int rnd(){ g_seed = g_seed * 214013u + 2531011u; return (g_seed >> 16) & 0x7fff; }	// MSVC rand()

// ---- road graph: TileGrid_InitializeNeighbors @0x4f1430, u_Path_FindEndpointElement @0x4f0280
void buildGraph(const Rm8Scene &s)
{
	g_nodes = s.rnodes;
	int n = (int)g_nodes.size();
	std::map<std::pair<int,int>, int> at;
	for(int i = 0; i < n; i++) at[{g_nodes[i].tx, g_nodes[i].ty}] = i;
	auto find = [&](int x, int y){ auto it = at.find({x, y}); return it == at.end() ? -1 : it->second; };
	static const int ORTH[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};			// up, down, left, right
	static const int DIAG[4][4] = {{-1, -1, 0, 2}, {1, 1, 1, 3}, {1, -1, 0, 3}, {-1, 1, 1, 2}};	// dx dy, blocking orthogonals
	g_graph.links.assign(n, {}); g_graph.deadEnd.assign(n, -1);
	for(int i = 0; i < n; i++){
		int x = g_nodes[i].tx, y = g_nodes[i].ty;
		if(x <= 0 || y <= 0 || x >= s.MW - 1 || y >= s.MH - 1) continue;	// only tiles with all 8 neighbours
		int o[4];
		for(int k = 0; k < 4; k++){ o[k] = find(x + ORTH[k][0], y + ORTH[k][1]); if(o[k] >= 0) g_graph.links[i].push_back(o[k]); }
		for(auto &d : DIAG){
			int q = find(x + d[0], y + d[1]);
			if(q >= 0 && o[d[2]] < 0 && o[d[3]] < 0) g_graph.links[i].push_back(q);
		}
	}
	for(int i = 0; i < n; i++){
		if(g_graph.links[i].size() <= 2) continue;
		for(int first : g_graph.links[i]){
			int prev = i, c = first; bool dead = false;
			for(int guard = 0; guard < n; guard++){
				const auto &L = g_graph.links[c];
				if(L.size() < 2){ dead = true; break; }
				if(L.size() > 2 || c == i) break;
				int nx = L[0] != prev ? L[0] : L[1];
				prev = c; c = nx;
			}
			if(dead){ g_graph.deadEnd[i] = first; break; }
		}
	}
}

V3d nodePos(int i){ return {g_nodes[i].x, g_nodes[i].y, g_nodes[i].z}; }

int selectNext(int prev, int cur)
{
	std::vector<int> cand;
	for(int q : g_graph.links[cur])
		if(q != g_graph.deadEnd[cur] && q != prev && g_graph.links[q].size() > 1) cand.push_back(q);
	if(cand.empty()) return prev;
	return cand[rnd() % cand.size()];
}

Texture *faceTexture(const std::string &key)
{
	auto it = g_faceTex.find(key);
	if(it != g_faceTex.end()) return it->second;
	Texture *t = rm8PackTexture(key);
	if(!t) rm8Log("warning: role texture %s not found", key.c_str());
	return g_faceTex[key] = t;
}

void setPart(Model &m, const std::string &part, Texture *t)
{
	if(!t) return;
	auto it = m.parts.find(part);
	if(it == m.parts.end()) return;
	for(Material *mat : it->second) mat->setTexture(t);
}

// [V] Role_SetAnimPose = vf5 @0x48a270: without a possession and with a vehicle > 1 (bike / car) the logical action 0..14 is looked up in the table 0x5E1DD0 to get the animation number of the vehicle
// model set (Act21..Act26: its list is not ordered like the walking set's Act01..Act14): 0 walk -> 1, 1 dice -> 0, 2 stand -> 5, 3 idle -> 5, 4 / 5 -> 2, 6 / 7 -> 3, 8 / 9 -> 4, 10 -> 6, 11..14 -> 5.
// Without this table the bike's "ride" played Act21 = the dice throw, and its stand / idle pose played the wrong action (docs/turn_flow.md 5, BUGS #23)
int vehicleAnim(const Role &r, int a)
{
	static const int T[15] = {1, 0, 5, 5, 2, 2, 3, 3, 4, 4, 6, 5, 5, 5, 5};
	if(r.diceMax <= 1 || rm8GameShape(r.seat) != 0 || a < 0 || a > 14) return a;
	return T[a];
}

// face swaps: the last key of this animation whose time has passed
void applyFace(Role &r, float t)
{
	Model &m = r.m[r.cur];
	auto it = g_arts[m.art].faces.find(vehicleAnim(r, r.anim));
	if(it == g_arts[m.art].faces.end()) return;
	const FaceKey *k = nullptr;
	for(const FaceKey &f : it->second) if(f.t <= t + 1e-4f && (!k || f.t >= k->t)) k = &f;
	if(!k || k->tex == r.face) return;
	setPart(m, k->part, faceTexture(k->tex));
	r.face = k->tex;
}

// [V] Mesh_SetTexture @0x47a570 -> Article_SelectByTypeAndSetProperties(anim, count): count 0 loops, 1 plays once
void playAnim(Role &r, int anim, bool once)
{
	r.anim = anim; r.once = once; r.face.clear();
	Model &m = r.m[r.cur];
	static const bool dbgA = getenv("RM8_DEBUG_TURNANIM") != nullptr;
	if(dbgA) rm8Log("anim: seat %d logical %d -> %d (vehicle %d)", r.seat, anim, vehicleAnim(r, anim), r.diceMax);
	auto it = m.anims.find(vehicleAnim(r, anim));
	if(!m.hier || it == m.anims.end() || !it->second) return;
	m.hier->interpolator->setCurrentAnim(it->second);
	applyFace(r, 0);
}

// [V] Mesh_SelectItem @0x47adf0: copy the transform to the new model and restart the current animation (looping)
void selectModel(Role &r, int idx)
{
	r.cur = idx;
	playAnim(r, r.anim, false);
}

void calcAngle(Role &r, V3d a, V3d b, bool allowSpin)
{
	V3d d = {b.x - a.x, b.y - a.y, b.z - a.z};
	float len = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z);
	if(len < 1e-4f) return;
	d.x /= len; d.y /= len; d.z /= len;
	int ang = (int)lroundf(acosf(std::max(-1.0f, std::min(1.0f, d.x))) / 3.1416f * 180.0f);
	if(d.z > 0) ang = 360 - ang;
	ang = ((360 - (int)CAM_YAW) + ang) % 360;	// [V] ftol(camYaw) (truncation)
	unsigned sector = (unsigned)((ang + 20) / 45);
	if(sector >= 8) sector = 0;
	uint32 mask = 1u << (sector * 4);
	int side = (((unsigned)r.cur % 3 != 0) && (mask & 0x0fffff00u)) || (mask & 0x00fff000u) ? 1 : 0;
	int idx = sector < 5 ? side : 3 - side;
	if(idx == r.cur) return;
	if(!allowSpin || (idx % 2 != r.cur % 2 && idx / 2 != r.cur / 2)){ selectModel(r, idx); return; }
	r.spinTarget = idx; r.spinning = true; r.spinAngle = 0;
	r.spinRate = (r.speed / 100.0f) * ((idx - r.cur == 1 || idx - r.cur == -3) ? 540.0f : -540.0f);
}

bool spinUpdate(Role &r, float dt)
{
	if(!r.spinning) return false;
	float d = dt * r.spinRate;
	r.spinAngle += d;
	if(r.spinTarget == r.cur){
		if(fabsf(r.spinAngle) >= 360.0f){
			float over = fabsf(r.spinAngle) - 360.0f;
			d = d <= 0 ? over + d : d - over;
			r.spinning = false;
		}
	}else if(fabsf(r.spinAngle) >= 90.0f){
		if(r.spinRate <= 0){ d -= 180.0f; r.spinAngle -= 180.0f; } else { d += 180.0f; r.spinAngle += 180.0f; }
		selectModel(r, r.spinTarget);
	}
	r.yaw = fmodf(r.yaw + d, 360.0f);
	return true;
}

void enqueue(Role &r, int cmd, int a = 0, int b = 0){ r.queue.push_back({cmd, a, b}); }	// [V] Role_EnqueueMsg @0x484890
// [V] vf21 CPlayerTag__vf21_StopSoundAndImage @0x48b260 (the image part is the bomb picture): stop the walking loop
void stopWalkSound(Role &r){ if(r.sndH){ rm8SndStop(r.sndH); r.sndH = 0; } }
// [V] vf20 CPlayerTag__vf20_PlaySound @0x48b1f0, called by BeginMoveSteps @0x484f90 with vehicle - 1 (0 while possessed):
// roleinfo wlkSndId[i] as CAudioMgr_PlaySample(id,0,0,0,1000,0,1.0,0) = looped, only when no handle is held
void startWalkSound(Role &r)
{
	if(r.sndH || !r.def) return;
	int w = rm8GameShape(r.seat) == 0 ? std::max(0, std::min(2, r.diceMax - 1)) : 0;
	if(r.def->wlkSnd[w] > 0) r.sndH = rm8SndPlay(r.def->wlkSnd[w], 0);
}
// [V] vf19 CPlayerTag__OnStopAtTerra @0x485c00: stand, state 8, NPC interaction then landing event on the node
void onStop(Role &r){ playAnim(r, 2, false); stopWalkSound(r); r.state = 8; if(rm8NpcOnStop(r.seat)) return; rm8PresentOnStop(r.seat); enqueue(r, 0x23, r.from); enqueue(r, 0x1d, r.from); }

void animateAlongRoute(Role &r, float dist)
{
	V3d t = nodePos(r.to);
	V3d v = {t.x - r.pos.x, t.y - r.pos.y, t.z - r.pos.z};
	float L = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
	for(;;){
		if(L > dist){
			r.pos.x += v.x / L * dist; r.pos.y += v.y / L * dist; r.pos.z += v.z / L * dist;
			return;
		}
		r.pos = t;
		int nn = selectNext(r.from, r.to);
		r.from = r.to; r.to = nn;
		dist -= L;
		t = nodePos(r.to);
		v = {t.x - r.pos.x, t.y - r.pos.y, t.z - r.pos.z};
		L = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
		calcAngle(r, nodePos(r.from), t, true);
		if(--r.steps < 1) break;
		{ int np = rm8NpcPass(r.seat, r.from); if(np){ playAnim(r, 2, false); stopWalkSound(r); r.state = 8; if(np == 2){ r.steps = 0; onStop(r); } return; } }	// [V] vf18 @0x485a20: time bomb, roadblock, possessed police / thief / dog (npc_act.cpp)
		if(g_nodes[r.from].event == 5 && rm8EventPass(r.seat, r.from)){ playAnim(r, 2, false); return; }	// [V] vf18 @0x485a20: passing the bank with steps left -> event 99
		followerPush(r);	// [V] Cls_5e27d8_OnOther @0x47deb0: vf18 returned 0 -> the follower gets the new target node
		if(L < 1e-4f) break;
	}
	onStop(r);
}

void updateAnim(Role &r, float dt)
{
	Model &m = r.m[r.cur];
	if(!m.hier || !m.hier->interpolator->currentAnim) return;
	AnimInterpolator *ip = m.hier->interpolator;
	float step = dt;
	if(r.once) step = std::min(step, std::max(0.0f, ip->currentAnim->duration - ip->currentTime - 1e-4f));
	ip->addTime(step);
	for(int i = 0; i < m.hier->numNodes; i++){
		Frame *f = m.hier->nodeInfo[i].frame;
		if(!f) continue;
		ip->applyCB(&f->matrix, ip->getInterpFrame(i));
		f->updateObjects();
	}
	m.hier->updateMatrices();
	applyFace(r, ip->currentTime);
}

void placeModel(Role &r)
{
	Model &m = r.m[r.cur];
	if(!m.clump) return;
	Frame *f = m.clump->getFrame();
	float shp[3]; const float *sc = rm8NpcRoleShape(r.seat, shp) ? shp : r.def->scale, *of = r.def->offset;	// [V] CPlayerTag__GetStatusShape @0x490bf0: the possessing NPC's scale
	// [V] model scale = status shape x the same-node tween scale (CPlayerTag__vf28_Update)
	V3d np0 = {-m.p0.x, -m.p0.y, -m.p0.z}, s = {sc[0] * r.twScale, sc[1] * r.twScale, sc[2] * r.twScale}, ay = {0, 1, 0};
	V3d p1 = {m.p0.x + r.pos.x + of[0], m.p0.y + r.pos.y + of[1], m.p0.z + r.pos.z + of[2]};
	f->transform(&m.base, COMBINEREPLACE);
	f->translate(&np0, COMBINEPOSTCONCAT); f->scale(&s, COMBINEPOSTCONCAT);
	f->rotate(&ay, CAM_YAW - 180.0f + r.yaw, COMBINEPOSTCONCAT); f->translate(&p1, COMBINEPOSTCONCAT);
	if(r.shadow){	// [V] CPlayerTag__vf3_Update @0x481500: shadow mesh at the character position + (0, 1, 0), no offset, no
		// rotation, scale (sx/shape.x, sx/shape.y, sx/shape.z) with sx = the character's x scale (the game divides the
		// x scale by every axis: (1, 0.8125, 1.3) for scale 1.3 1.6 1), about the shadow frame position
		Frame *sf = r.shadow->getFrame();
		V3d p0 = r.shBase.pos, np0 = {-p0.x, -p0.y, -p0.z};
		V3d ss = {r.twScale, sc[0] * r.twScale / sc[1], sc[0] * r.twScale / sc[2]};
		V3d d = {p0.x + r.pos.x, p0.y + r.pos.y + 1.0f, p0.z + r.pos.z};
		sf->transform(&r.shBase, COMBINEREPLACE);
		sf->translate(&np0, COMBINEPOSTCONCAT); sf->scale(&ss, COMBINEPOSTCONCAT); sf->translate(&d, COMBINEPOSTCONCAT);
	}
}

void loadShadow(Rm8Scene &s, Role &r)
{
	r.shadow = rm8LoadClump("article\\mend\\mnd_07.dff", "article\\mend\\");
	if(!r.shadow){ rm8Log("warning: no shadow model"); return; }
	r.shBase = r.shadow->getFrame()->matrix; r.shBase.flags = 0;
	s.world->addClump(r.shadow);
}

bool loadModel(Rm8Scene &s, Model &m, int art, int charId, int color, bool front)
{
	// [V] outfit 4 (swimsuit): u_Role_CopyTrfToolValues @0x491200 takes artId + 0xC0 (the A%02dx models) for every model set; Role_SpawnCharacterModel @0x48b410
	// only calls Asset_LoadCharacterTexture for outfit < 4, so these models keep their own textures (a%02dx_body_f_%02d etc.)
	bool swim = false;
	if(color == 4){ if(g_arts.count(art + 192)){ art += 192; swim = true; } else color = 0; }	// [A] no swimsuit model in an old roles.txt: the plain model
	auto it = g_arts.find(art);
	if(it == g_arts.end()){ rm8Log("warning: no model for art %d", art); return false; }
	m.art = art;
	m.clump = rm8LoadClump(it->second.dff, it->second.texdir);
	if(!m.clump){ rm8Log("warning: cannot load %s", it->second.dff.c_str()); return false; }
	m.base = m.clump->getFrame()->matrix; m.base.flags = 0;
	m.p0 = m.base.pos;
	m.hier = HAnimHierarchy::find(m.clump->getFrame());
	if(m.hier){ m.hier->attach(); }
	for(auto &a : it->second.anims){
		Animation *an = rm8LoadAnim(a.second);
		if(an && m.hier && an->getNumNodes() == m.hier->numNodes) m.anims[a.first] = an;
	}
	FORLIST(lnk, m.clump->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		for(int i = 0; i < g->matList.numMaterials; i++){
			Material *mat = g->matList.materials[i];
			int k = UserDataArray::materialFindIndex(mat, "name");
			if(k < 0) continue;
			UserDataArray *ua = UserDataArray::materialGet(mat, k);
			if(ua && ua->datatype == USERDATASTRING && ua->numElements > 0 && ua->getString(0))
				m.parts[ua->getString(0)].push_back(mat);
		}
	}
	// [V] Asset_LoadCharacterTexture: body colour per seat colour; "other"/"hair"/"toujin" for some characters
	char key[160];
	const char *fb = front ? "f" : "b";
	if(swim || charId <= 0){ s.world->addClump(m.clump); return true; }	// the swimsuit models and the follower models (A13b monster, D46a guard) keep the textures of their materials
	snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_body_%s_%02d.tga", charId, charId, fb, color);
	setPart(m, "body", faceTexture(key));
	const char *extra = (charId == 2 || charId == 15) ? "other" : (charId == 4 || charId == 9 || charId == 11 || charId == 13) ? "hair" :
	                    (charId == 7 || charId == 10) ? "toujin" : nullptr;
	if(extra){ snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_%s_%s_%02d.tga", charId, charId, extra, fb, color); setPart(m, extra, faceTexture(key)); }
	s.world->addClump(m.clump);
	return true;
}

// ---- turn flow (docs/turn_flow.md section 2)
int g_cur = 0;				// [V] g_RoleMgr +0x2c: seat whose turn it is
std::vector<int> g_reg;			// [V] g_RoleMgr[4]: road node each seat is registered on (-1 none)
V3d g_focus = {0, 0, 0}; int g_focusSerial = 0;
Camera *g_cam = nullptr;
struct Arrow { Clump *clump = nullptr; HAnimHierarchy *hier = nullptr; Matrix base; V3d p0 = {0, 0, 0}; V3d pos = {0, 0, 0}; } g_arrow;

void setFocus(V3d p){ g_focus = p; g_focusSerial++; }	// [V] Camera_SetFocusPoint: instant, camera = p - 800 at

float waitScale(){ return 1.0f - std::min((rm8GameSpeed - 1) / 10.0f, 1.0f); }	// [V] @0x47b3b0

// [V] 4.3 tween toward the target offset/scale at 100 units/s; the last frame overshoots (applies step, not L)
bool tweenUpdate(Role &r, float dt)
{
	if(!r.tw) return false;
	V3d d = {r.twTOff.x - r.twOff.x, r.twTOff.y - r.twOff.y, r.twTOff.z - r.twOff.z};
	float L = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z), step = dt * 100.0f;
	float sc = L > 0 ? (100.0f / L) * (r.twTScale - r.twScale) * dt + r.twScale : r.twTScale;
	V3d n = L > 0 ? V3d{d.x / L, d.y / L, d.z / L} : V3d{0, 0, 0};
	r.pos.x += n.x * step; r.pos.y += n.y * step; r.pos.z += n.z * step;
	if(step < L){ r.twOff.x += n.x * step; r.twOff.y += n.y * step; r.twOff.z += n.z * step; r.twScale = sc; }
	else { r.twOff = r.twTOff; r.twScale = r.twTScale; r.tw = false; }
	return true;
}
// [V] Role_SetMovePath @0x4816a0
void setMovePath(Role &r, V3d off, float scale){ r.twTOff = off; r.twTScale = scale; r.tw = true; }
bool hasMovePath(const Role &r){ return r.twTOff.x != 0 || r.twTOff.y != 0 || r.twTOff.z != 0; }	// @0x481840
// [V] Role_CheckPathValid @0x4816f0: an offset role goes back to the centre at scale 1
bool checkPathValid(Role &r){ if(!hasMovePath(r)) return false; setMovePath(r, {0, 0, 0}, 1.0f); return true; }

void camAxes(V3d &F, V3d &R)
{
	F = {0.54f, 0, -0.54f}; R = {-0.707f, 0, -0.707f};
	if(g_cam){ const Matrix *m = g_cam->getFrame()->getLTM(); F = {m->at.x, 0, m->at.z}; R = m->right; }
	float l = sqrtf(F.x*F.x + F.z*F.z); if(l > 0){ F.x /= l; F.z /= l; }
}

float dist3(V3d a, V3d b){ return sqrtf((a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) + (a.z-b.z)*(a.z-b.z)); }

// [V] 4.1 Game_MoveRoleToPosition: approaching a node
void moveRoleToPosition(Role &r, int node, V3d pos, bool final)
{
	if(node < 0 || node == g_reg[r.seat]) return;
	if(dist3(nodePos(node), pos) > 20.0f) return;
	V3d F, R; camAxes(F, R);
	int n = (int)g_role.size(), k = 0;
	for(int i = 1; i < n; i++){
		Role &j = g_role[(r.seat + i) % n];
		if(j.from != node) continue;
		if(final){
			float o = 20.0f - 20.0f * k;
			setMovePath(j, {o*R.x - 15*F.x, o*R.y, o*R.z - 15*F.z}, 0.5f);
			g_reg[j.seat] = -1; k++;
		}else if(!hasMovePath(j)){ setMovePath(j, {15*F.x, 0, 15*F.z}, 1.0f); break; }
	}
	g_reg[r.seat] = node;
}

// [V] 4.2 Game_UpdateRoleDirection: leaving a node
void updateRoleDirection(Role &r, int node, V3d pos)
{
	if(node < 0 || node != g_reg[r.seat]) return;
	if(dist3(nodePos(node), pos) <= 15.0f) return;
	int n = (int)g_role.size(); Role *shrunk = nullptr; bool restored = false;
	for(int i = 1; i < n && !restored; i++){
		Role &j = g_role[(r.seat + i) % n];
		if(j.from != node) continue;
		if(j.twTScale != 1.0f){ if(!shrunk) shrunk = &j; continue; }
		restored = checkPathValid(j);
	}
	if(!restored && shrunk){ checkPathValid(*shrunk); g_reg[shrunk->seat] = node; }
	g_reg[r.seat] = -1;
}

// ---- followers: the class of 伊格魯 (vtable 0x5e27d8) keeps a companion, the "monster" A13b (art 499 500 498 497, Cls_5e27d8_CreateMovieActor @0x47e1e0), for the whole game; the class of
// 莎拉 (0x5e2918) shares the same overrides, its companion is the bodyguard D46a (art 521 522 520 519, Path_Create @0x480260) that only exists while the guard counter runs (vf32 / vf33,
// cards.cpp). The companion is a Cls_5e1b90 path object (Path_UpdatePosition @0x47ee20) at role +0x5f0: pure presentation, no rule reads it except vf25 (莎拉: "the guard exists").
// [H] the field names below are inferred from the code, the behaviour of each branch was read:
//  route (+0x4c / +0x58): the nodes the role has still to pass - pushed at the start of a walk (vf26 @0x47de00) and at every node with steps left (vf18 @0x47deb0); walk (+0x84) = following it
//  Path_AnimateAlongPath @0x47f0b0: toward the next node at speed * dt; the last node is approached only to 20 units; speed 100 walking / 200 running (Movie_SelectAnimByDistance @0x47fc30: run
//  when more than 80 units behind the role, walk again under 40); a 0.3 s pause (+0x94) after every stop; the game speed factor ((speed - 1) / 4 + 1) scales its time
//  Path_LinkPoints @0x47faa0 (vf15 / vf37 on the role): the companion stands 20 units behind the role's node, on a neighbour that is not the role's target, facing the node
//  Path_SetWaypoint @0x47fba0 (vf36 = the game-start debut, @0x47e3b0): the companion starts 300 units to the right of the camera and walks in to where it stood (waypoint mode, drawn although the role is hidden)
//  hospital / prison (vf28 / vf29): Path_ResetTrail @0x47fe30 hides it (+0x90); vf5 commands >= 4 (expressions) of the role are forwarded (伊格魯 only), the monster plays them half of the time when idle
//  hotel (vf34 / vf35 with 0, messages 0x13 / 0x14 @0x47df00 / 0x47df50): Path_SetNormalResult @0x47fd40 -> Path_AnimateBetweenPoints @0x47f380: it walks to the middle of (its own first node, the building) and hides there (+0x90);
//   Path_SetReversedResult @0x47fd80 -> Path_AnimateAlongEdge @0x47f510: visible again, walks back to the standing place 20 units from the role's node. The role walks the same way (state 0xb / 0xc, below)
//  turn around (vf16 @0x47de40, Role.Turn): when the role's new target node is the node the companion comes from, the old target becomes its retreat node (+0x5c): Path_MoveTowardWaypoint @0x47f6b0 walks it
//   through the role's node and on to the other side (backing away, facing the role) until it is 20 units from the node
//  vf27 @0x47dfa0 (the ambulance / police car / UFO scene carries the role off): Path_SetSpeedVector @0x47fdd0 = the camera's right vector, after 2 s Path_TranslateByVelocity @0x47f8b0 (run after 60 units)
struct Follower {
	bool on = false, bodyguard = false;
	Role r;				// models / facing / animation reuse the character code (def == nullptr)
	std::vector<int> route;
	bool walk = false, wpMode = false, vis = false, hidden = false, inited = false;
	V3d wp = {0, 0, 0};
	float delay = 0.3f;
	bool expr = false;		// an expression forwarded from the role is playing (once)
	int lastStatus = 0;
	int retreat = -1;		// +0x5c: the node to walk around to after a turn around of the role (MoveTowardWaypoint)
	int hotel = 0;			// +0x60 / +0x64: 1 into the hotel (AnimateBetweenPoints), 2 out of it (AnimateAlongEdge)
	V3d bldg = {0, 0, 0};		// +0x60 -> the building's position (+0x88)
	bool vel = false; V3d velV = {0, 0, 0}; float velDist = 0;	// +0x68 / +0x6c: the velocity vector and the distance covered
};
std::map<int, Follower> g_fol;		// by seat
unsigned g_fseed = 12345;
int frand(){ g_fseed = g_fseed * 214013u + 2531011u; return (g_fseed >> 16) & 0x7fff; }	// [A] the companion's own random stream: it is pure presentation and not part of the saved game state (save_roundtrip.py)

Follower *followerOf(int seat){ auto it = g_fol.find(seat); return it != g_fol.end() && it->second.on ? &it->second : nullptr; }

void followerStopAnim(Follower &f){ playAnim(f.r, f.bodyguard ? 1 : 2, false); f.expr = false; }	// vf5(2): the guard has two actions only (Cls_5e1ca8 vf5 clamps 0..2 to 0..1)
void followerAnim(Follower &f, int act, bool once)	// vf5 of Cls_5e1b90 @0x47fe50 / Cls_5e1ca8 @0x480560
{
	(void)once;
	if(f.bodyguard){ if(act >= 0 && act <= 2) playAnim(f.r, std::min(act, 1), false); return; }
	if(act < 4 || act == 14){ playAnim(f.r, act, false); f.expr = false; }
	else if(!f.inited && (frand() & 1)){ playAnim(f.r, act, true); f.expr = true; }
}
// Path_CheckInitializedOnce @0x47f970
bool followerInit(Follower &f){ if(f.inited) return false; followerAnim(f, 0, false); f.r.speed = 100.0f; f.inited = true; return true; }
// Path_ResetAnimation @0x47f9d0: stand, no walk / retreat / hotel mode, the velocity vector freed, the 0.3 s start delay
void followerReset(Follower &f){ followerStopAnim(f); f.walk = false; f.retreat = -1; f.hotel = 0; f.vel = false; f.inited = false; f.delay = 0.3f; }
// [A] what the callers of Path_LinkPoints used to do (stand, not walking, delay): the modes of ResetAnimation are not touched, as in the original LinkPoints (@0x47faa0 only clears +0x84)
void followerSettle(Follower &f){ followerStopAnim(f); f.walk = false; f.inited = false; f.delay = 0.3f; }

// Path_LinkPoints @0x47faa0 as called by vf15 / Path_Create: a neighbour of `node` other than `avoid` (the first link, when every link is `avoid`), the follower between the two
void followerLink(Follower &f, int node, int avoid)
{
	if(node < 0 || node >= (int)g_graph.links.size()) return;
	int nb = -1;
	for(int q : g_graph.links[node]) if(q != avoid){ nb = q; break; }
	if(nb < 0 && !g_graph.links[node].empty()) nb = g_graph.links[node][0];
	if(nb < 0) return;
	V3d a = nodePos(nb), b = nodePos(node), d = {a.x - b.x, a.y - b.y, a.z - b.z};
	float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z); if(l > 1e-4f){ d.x /= l; d.y /= l; d.z /= l; }
	f.r.from = nb; f.r.to = node;
	f.r.pos = {b.x + d.x * 20.0f, b.y + d.y * 20.0f, b.z + d.z * 20.0f};
	calcAngle(f.r, a, b, false);
	f.route.clear(); f.walk = false;
}

void followerDestroy(Follower &f)
{
	if(g_scene && g_scene->world) for(Model &m : f.r.m) if(m.clump){ g_scene->world->removeClump(m.clump); m.clump->destroy(); m.clump = nullptr; }
	f.on = false;
}

void followerCreate(const Role &role, bool bodyguard)
{
	if(!g_scene || role.from < 0) return;
	Follower &f = g_fol[role.seat];
	if(f.on) followerDestroy(f);
	f = Follower();
	static const int MONSTER[4] = {499, 500, 498, 497}, GUARD[4] = {521, 522, 520, 519};	// [V] DAT_006044ec / DAT_006044fc: fl fr br bl
	const int *art = bodyguard ? GUARD : MONSTER;
	if(!g_arts.count(art[0])) return;	// an old roles.txt without the follower models
	bool ok = true;
	for(int j = 0; j < 4; j++) ok &= loadModel(*g_scene, f.r.m[j], art[j], 0, 0, j < 2);
	if(!ok){ for(Model &m : f.r.m) if(m.clump){ g_scene->world->removeClump(m.clump); m.clump->destroy(); m.clump = nullptr; } return; }
	f.on = true; f.bodyguard = bodyguard;
	f.r.def = nullptr; f.r.seat = role.seat; f.r.speed = role.def->speed[0];
	f.lastStatus = role.status;
	followerLink(f, role.from, role.to);
	f.r.cur = 0; playAnim(f.r, bodyguard ? 1 : 2, false);
	calcAngle(f.r, nodePos(f.r.from), nodePos(f.r.to), false);
	updateAnim(f.r, 0);
}

void followerPush(Role &r)
{
	Follower *f = followerOf(r.seat);
	if(!f || r.to < 0) return;
	f->route.push_back(r.to); f->walk = true; f->vis = false;	// u_Path_PushItemAndSetFlags @0x47fa60
}

void followerPlace(Follower &f)
{
	Model &m = f.r.m[f.r.cur];
	if(!m.clump) return;
	Frame *fr = m.clump->getFrame();
	V3d np0 = {-m.p0.x, -m.p0.y, -m.p0.z}, s = {1.3f, 1.6f, 1.0f}, ay = {0, 1, 0};	// roleinfo scale (+0x338) of the character: 1.3 1.6 1
	auto it = g_roles.find(f.bodyguard ? 16 : 13);
	if(it != g_roles.end()) s = {it->second.scale[0], it->second.scale[1], it->second.scale[2]};
	V3d p1 = {m.p0.x + f.r.pos.x, m.p0.y + f.r.pos.y, m.p0.z + f.r.pos.z};
	fr->transform(&m.base, COMBINEREPLACE);
	fr->translate(&np0, COMBINEPOSTCONCAT); fr->scale(&s, COMBINEPOSTCONCAT);
	fr->rotate(&ay, CAM_YAW - 180.0f + f.r.yaw, COMBINEPOSTCONCAT); fr->translate(&p1, COMBINEPOSTCONCAT);
}

static void fMove(Role &r, V3d d, float step)	// Mesh_AccumulateTranslation_0047a6a0(normalize(d) * step)
{
	float L = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
	if(L > 1e-4f){ r.pos.x += d.x / L * step; r.pos.y += d.y / L * step; r.pos.z += d.z / L * step; }
}
// Path_MoveTowardWaypoint @0x47f6b0 (retreat node +0x5c): from == retreat: back away from the role's node until 20 units from it; else walk to the role's node and then switch to the retreat node
void followerMoveToward(Follower &f, float step)
{
	Role &r = f.r;
	followerInit(f);
	V3d A = nodePos(r.from), B = nodePos(r.to);
	if(r.from == f.retreat){
		float len = dist3(r.pos, B);
		if(20.0f <= step + len){ step = 20.0f - len; followerReset(f); calcAngle(r, A, B, true); }
		fMove(r, {A.x - r.pos.x, A.y - r.pos.y, A.z - r.pos.z}, step);
	}else{
		V3d d = {B.x - r.pos.x, B.y - r.pos.y, B.z - r.pos.z};
		float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
		if(len <= step){ r.from = f.retreat; step = len; }
		fMove(r, d, step);
	}
}
// Path_AnimateBetweenPoints @0x47f380: toward the middle of (the companion's first node, the building); there it hides (+0x90 = 1)
void followerBetweenPoints(Follower &f, float step)
{
	Role &r = f.r;
	if(followerInit(f)) calcAngle(r, r.pos, f.bldg, true);
	V3d A = nodePos(r.from);
	V3d d = {A.x + (f.bldg.x - A.x) * 0.5f - r.pos.x, A.y + (f.bldg.y - A.y) * 0.5f - r.pos.y, A.z + (f.bldg.z - A.z) * 0.5f - r.pos.z};
	float L = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
	if(L <= step){ followerReset(f); f.hidden = true; step = L; }
	fMove(r, d, step);
}
// Path_AnimateAlongEdge @0x47f510: toward the standing place (20 units from the second node toward the first), facing as the decompile calls vf+0x2c (target -> position)
void followerAlongEdge(Follower &f, float step)
{
	Role &r = f.r;
	V3d A = nodePos(r.from), B = nodePos(r.to), n = {A.x - B.x, A.y - B.y, A.z - B.z};
	float nl = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z); if(nl > 1e-4f){ n.x /= nl; n.y /= nl; n.z /= nl; }
	V3d tg = {B.x + n.x * 20.0f, B.y + n.y * 20.0f, B.z + n.z * 20.0f};
	if(followerInit(f)) calcAngle(r, tg, r.pos, true);
	V3d d = {tg.x - r.pos.x, tg.y - r.pos.y, tg.z - r.pos.z};
	float L = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
	if(L <= step){ followerReset(f); calcAngle(r, A, B, true); step = L; }
	fMove(r, d, step);
}
// Path_TranslateByVelocity @0x47f8b0: the velocity vector (unit length) times the step; after 60 units it runs (animation 14, speed 200)
void followerTranslateByVelocity(Follower &f, float step)
{
	Role &r = f.r;
	followerInit(f);
	r.pos.x += f.velV.x * step; r.pos.y += f.velV.y * step; r.pos.z += f.velV.z * step;
	f.velDist += step;
	if(f.velDist >= 60.0f && r.speed < 200.0f){ followerAnim(f, 14, false); r.speed = 200.0f; }
}

// Cls_5e27d8__vf3_Movie @0x47dc80: after the role's own update the companion's Path_UpdatePosition, then Movie_SelectAnimByDistance
void followerUpdate(Role &role, float dt)
{
	Follower *pf = followerOf(role.seat);
	if(!pf) return;
	Follower &f = *pf; Role &r = f.r;
	if(role.status == 7){ followerDestroy(f); return; }
	// hospital / prison (vf28 / vf29): Path_ResetTrail @0x47fe30 = Path_ResetAnimation + hidden; the hotel (3) and the vanished status (6) do not touch it (the role is not drawn, so neither is the companion: vf4 @0x47dcf0)
	if(role.status == 4 || role.status == 5){ if(!f.hidden){ followerReset(f); f.hidden = true; } }
	else if(role.status < 3 && (f.lastStatus == 4 || f.lastStatus == 5 || f.lastStatus == 6)){ f.hidden = false; followerLink(f, role.from, role.to); followerSettle(f); }	// vf37 @0x47e090: the role is placed again (+0x90 = 0, Path_LinkPoints)
	f.lastStatus = role.status;
	float dtS = dt * ((rm8GameSpeed - 1) / 4.0f + 1.0f);
	if(!spinUpdate(r, dtS)){
		float step = dtS * r.speed;
		if(f.retreat >= 0) followerMoveToward(f, step);		// Path_UpdatePosition @0x47ee20: +0x5c first, without the start delay
		else if(f.walk || f.wpMode || f.hotel || f.vel){
			f.delay -= dtS;
			if(f.delay <= 0){
				f.delay = 0;
				if(!f.walk && f.hotel == 1) followerBetweenPoints(f, step);
				else if(!f.walk && f.hotel == 2) followerAlongEdge(f, step);
				else if(!f.walk && !f.wpMode) followerTranslateByVelocity(f, step);
				else if(f.walk){	// Path_AnimateAlongPath @0x47f0b0
					followerInit(f);
					V3d t = nodePos(r.to), d = {t.x - r.pos.x, t.y - r.pos.y, t.z - r.pos.z};
					float L = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
					while(L < step && !f.route.empty()){
						r.pos = t; r.from = r.to; r.to = f.route.front(); f.route.erase(f.route.begin());
						step -= L; t = nodePos(r.to); d = {t.x - r.pos.x, t.y - r.pos.y, t.z - r.pos.z};
						L = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
						calcAngle(r, nodePos(r.from), t, true);
					}
					if(f.route.empty() && L - 20.0f < step){ followerReset(f); step = L - 20.0f; }	// stops 20 units before the last node
					if(L > 1e-4f){ r.pos.x += d.x / L * step; r.pos.y += d.y / L * step; r.pos.z += d.z / L * step; }
				}else{	// Path_AnimateToWaypoint @0x47f2a0
					V3d d = {f.wp.x - r.pos.x, f.wp.y - r.pos.y, f.wp.z - r.pos.z};
					float L = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
					if(L < step){ followerReset(f); f.wpMode = false; calcAngle(r, nodePos(r.from), nodePos(r.to), true); step = L; }
					if(L > 1e-4f){ r.pos.x += d.x / L * step; r.pos.y += d.y / L * step; r.pos.z += d.z / L * step; }
				}
			}
		}
		if(f.inited && !f.wpMode){	// Movie_SelectAnimByDistance @0x47fc30
			float dd = dist3(r.pos, role.pos);
			if(dd > 80.0f && r.speed < 200.0f){ followerAnim(f, 14, false); r.speed = 200.0f; }
			else if(dd < 40.0f && r.speed >= 200.0f){ followerAnim(f, 0, false); r.speed = 100.0f; }
		}
	}
	if(f.expr && r.once){	// a forwarded expression finished: back to standing
		Model &m = r.m[r.cur]; AnimInterpolator *ip = m.hier ? m.hier->interpolator : nullptr;
		if(!ip || !ip->currentAnim || ip->currentTime >= ip->currentAnim->duration - 2e-4f) followerStopAnim(f);
	}
	updateAnim(r, dtS);
	followerPlace(f);
	static const bool dbg = getenv("RM8_DEBUG_FOLLOW") != nullptr;	// test hook: one line a second per companion
	if(dbg){ static float acc = 0; acc += dt; if(acc >= 1.0f){ acc = 0; rm8Log("follower seat %d (%s): role (%.0f,%.0f) state %d status %d, follower (%.0f,%.0f) walk %d route %d speed %.0f anim %d dist %.0f hidden %d hotel %d retreat %d vel %d", role.seat, f.bodyguard ? "guard" : "monster", role.pos.x, role.pos.z, role.state, role.status, r.pos.x, r.pos.z, f.walk, (int)f.route.size(), r.speed, r.anim, dist3(r.pos, role.pos), f.hidden, f.hotel, f.retreat, (int)f.vel); } }
}

// ---- walking into / out of a hotel (messages 0x13 / 0x14): CPlayerTag__EnterHotelStatus @0x4852e0 / LeaveHotelStatus @0x4853a0 with 0 start the walk (state 0xb / 0xc), Role_StepTowardBuilding @0x4854b0 /
// Role_StepTowardTerraNode @0x4856d0 move the role at speed / 2 (not scaled by the game speed: only state 1 is) toward the middle of (its node, the building) / back to the node while its
// size goes down to half / up again, and the calls with 1 finish: status 3 + one day (cards.cpp) / the node placement. The companion starts its own walk at the same time (Cls_5e27d8_OnStandby @0x47df00 / OnMove @0x47df50)
static bool hotelBuilding(const Role &r, V3d &b)
{
	int t = rm8GameNodeTerra(r.from); float bp[3];
	if(t < 0 || !rm8GameTerraPos(t, bp)) return false;
	b = {bp[0], bp[1], bp[2]};
	return true;
}
static void followerHotelStart(Role &r, bool enter)
{
	Follower *f = followerOf(r.seat); V3d b;
	if(!f || !hotelBuilding(r, b)) return;	// param_1 != 0: the node's building object
	f->hotel = enter ? 1 : 2; f->bldg = b; f->delay = 0.3f;	// Path_SetNormalResult / Path_SetReversedResult: +0x94 = 0.3
	if(!enter) f->hidden = false;				// the reversed one clears +0x90
}
static void hotelArrive(Role &r)	// vf34(1): the role is in the hotel
{
	playAnim(r, 2, false); stopWalkSound(r);
	r.state = 8;
	rm8CardsEnterHotel(r.seat);	// status 3, +0x1D8 = 1 day
}
static void hotelBack(Role &r)	// vf35(1): the role is back on its node
{
	r.twScale = 1.0f;
	V3d n = nodePos(r.from); r.pos = n;
	calcAngle(r, nodePos(r.from), r.to >= 0 ? nodePos(r.to) : n, false);
	if(Follower *fo = followerOf(r.seat)){ followerLink(*fo, r.from, r.to); followerSettle(*fo); }	// vf15 (+0x3c): Path_SetPoints + the companion's Path_LinkPoints
	playAnim(r, 2, false); stopWalkSound(r);
	if(r.def) r.speed = r.def->speed[std::max(0, std::min(2, r.diceMax - 1))];	// [V] +0x34 = the speed of the vehicle (table +0x380)
	r.state = 8;
}
static void stepTowardBuilding(Role &r, float dt)	// state 0xb
{
	V3d b;
	if(!hotelBuilding(r, b)){ hotelArrive(r); return; }	// [A] the original does nothing without a building object (the role would stay in state 0xb)
	float step = dt * r.speed / 2.0f;
	V3d n = nodePos(r.from), t = {n.x + (b.x - n.x) * 0.5f, n.y + (b.y - n.y) * 0.5f, n.z + (b.z - n.z) * 0.5f};
	V3d d = {t.x - r.pos.x, t.y - r.pos.y, t.z - r.pos.z};
	float L = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z), f;
	bool arrive = !(L > step);
	if(!arrive){ f = std::min((L - step) / 50.0f + 0.5f, 1.0f); if(L > 1e-4f){ d.x = d.x / L * step; d.y = d.y / L * step; d.z = d.z / L * step; } }
	else f = 0.5f;
	if(arrive) hotelArrive(r);
	r.pos.x += d.x; r.pos.y += d.y; r.pos.z += d.z;
	r.twScale = f;			// Mesh_SetScale(shape * f)
	updateRoleDirection(r, r.from, r.pos);
}
static void stepTowardNode(Role &r, float dt)	// state 0xc
{
	float step = dt * r.speed / 2.0f;
	V3d t = nodePos(r.from), d = {t.x - r.pos.x, t.y - r.pos.y, t.z - r.pos.z};
	float L = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z), f;
	bool arrive = !(L > step);
	if(!arrive){ f = 1.0f - (L - step) / 50.0f; if(!(f > 0.5f)) f = 0.5f; if(L > 1e-4f){ d.x = d.x / L * step; d.y = d.y / L * step; d.z = d.z / L * step; } }
	else f = 1.0f;
	r.pos.x += d.x; r.pos.y += d.y; r.pos.z += d.z;
	r.twScale = f;
	moveRoleToPosition(r, r.from, r.pos, true);
	if(arrive) hotelBack(r);
}

// [V] the dice block at role +0x22C (class of Game_SetPlayerCount_0047d930 / Dice_SetCount / Game_SetDiceValue_0047dac0 / State_SyncFields)
// Game_SetPlayerCount_0047d930 @0x47d930 (Role_InitState @0x480d30, Role_SetTrfTool @0x48a0c0): max = count = n, slots 1..n on, the others off
static void diceSetMax(Role &r, int n)
{
	n = std::max(1, std::min(3, n));
	r.diceMax = r.diceCount = n;
	for(int k = 0; k < 3; k++) r.diceOn[k] = k < n;
}
// Dice_SetCount @0x47da00: only while n <= max; the old count and flags are kept for State_SyncFields, slots 1..n on, the others off
static void diceSetCount(Role &r, int n)
{
	if(n > r.diceMax) return;
	r.dicePrev = r.diceCount; r.diceCount = n;
	for(int k = 0; k < 3; k++){ r.dicePrevOn[k] = r.diceOn[k]; r.diceOn[k] = k < n; }
}
// Game_SetDiceValue_0047dac0 @0x47dac0: slot 2..max set to on / off; the count = 1 + the flags of slots 2 and 3, at most max
static bool diceSetSlot(Role &r, int slot, bool on)
{
	if(slot < 2 || slot > r.diceMax) return false;
	r.diceOn[slot - 1] = on;
	int n = 1; for(int k = 1; k < 3; k++) if(r.diceOn[k]) n++;
	r.diceCount = std::min(n, r.diceMax);
	return true;
}
// State_SyncFields @0x47d9c0 (u_CPlayerTag_SyncGameState @0x485020, the end of the event die): back to the count and flags before Dice_SetCount
static void diceSync(Role &r)
{
	if(r.dicePrev > r.diceMax) return;
	r.diceCount = r.dicePrev; r.diceOn[1] = r.dicePrevOn[1]; r.diceOn[2] = r.dicePrevOn[2];
}

// [V] Role_StartRollDice @0x484e50
void startRollDice(Role &r, int force)
{
	if(force == 0 && rm8CardsTurtleRoll(r.seat)) force = 1;	// [V] @0x484e50: turtle days > 0 and no forced roll: days - 1, the roll is forced to 1
	if(force == 0 && r.human){ static const char *fd = getenv("RM8_FORCE_DIE"); if(fd) force = atoi(fd); }	// test hook: the human's roll is this value (one die)
	setFocus(r.pos);
	const float *dp = r.def->dice[std::max(0, std::min(2, r.diceCount - 1))];	// index = dice count, not vehicle
	float d[3] = {dp[0] * ((unsigned)r.cur % 3 != 0 ? -1.0f : 1.0f), dp[1], dp[2]};
	float p[3] = {r.pos.x, r.pos.y, r.pos.z};
	rm8SaveBoundary(3);	// test hook RM8_SAVE_DUMPT (save.cpp): the record before every roll
	static const bool logDice = getenv("RM8_DEBUG_DICE") != nullptr;
	if(logDice) rm8Log("dice: seat %d throws %d dice (slots %d%d%d, max %d, forced %d)", r.seat, force >= 1 && force <= 6 ? 1 : r.diceCount, (int)r.diceOn[0], (int)r.diceOn[1], (int)r.diceOn[2], r.diceMax, force);
	rm8DiceStart(p, d, r.diceCount, force);
	r.state = 3;
	r.special = false;
	playAnim(r, 1, true);
	r.queue.clear();
}

// [V] Role_StartDiceSceneCount1 @0x484d70 (called by the event dialogs' dice buttons through Ui_OnEvent_StartDiceScene @0x4e8a80 / Hotel_CheckVisitor @0x4f7550 while the role is in state 9):
// one die (Dice_SetCount(1)) thrown like the board dice, state 4; the result goes to the task (scene vf12 -> Hotel_InitiateStay @0x4f7610 / the ticket center's OnTimer) and the role is back in state 9
static int g_evDie = 0;
void startEventDie(Role &r)
{
	if(r.state == 4) return;
	setFocus(r.pos);
	const float *dp = r.def->dice[0];
	float d[3] = {dp[0] * ((unsigned)r.cur % 3 != 0 ? -1.0f : 1.0f), dp[1], dp[2]};
	float p[3] = {r.pos.x, r.pos.y, r.pos.z};
	diceSetCount(r, 1);	// [V] Dice_SetCount(1): the old count and flags come back at the end (case 4 below)
	rm8DiceStart(p, d, 1, 0);
	r.state = 4; g_evDie = 0;
	playAnim(r, 1, true);
}

// [V] CPlayerTag__BeginMoveSteps @0x484f90
void beginMoveSteps(Role &r, int n){ if(n <= 0) return; rm8StoryNoteSteps(r.seat, n); r.steps = n; r.state = 1; playAnim(r, 0, false); startWalkSound(r); followerPush(r); }	// [V] Cls_5e27d8_OnPosChange @0x47de00: vf26 -> push the target node

// [V] Role_BeforeRollDice @0x484c80 (no cards/missions yet): human waits in state 6, computer throws (DiceNum = all)
void beforeRollDice(Role &r)
{
	if(rm8StoryMissionCheck(r.seat)){ enqueue(r, 100); r.state = 8; return; }	// [V] Role_BeforeRollDice @0x484c80: a puzzle piece condition holds -> 0x58 queued, then 100 again
	if(rm8GameShape(r.seat) == 6){ rm8NpcSleepwalk(r.seat); r.state = 8; return; }	// [V] Role_BeforeRollDice @0x484c80: possessed by 糊塗神 -> Role_ShowSleepwalkAndRoll
	if(r.human){ r.state = 6; rm8SaveBoundary(1); return; }	// [A] save.cpp: pending manual saves are written while the human waits for the dice
	if(rm8CardsAiUsePlanned(r.seat)){ r.state = 8; return; }	// [V] Role_UseCardBeforeRoll @0x4fbab0
	diceSetCount(r, r.diceMax);	// [A] the computer always throws all its dice (the AI.DiceNum answer is not called yet); Dice_SetCount(n) resets the slot flags
	startRollDice(r, 0);
}

// [V] Role_StayOrBeforeRoll @0x484610 (message 0x25 and Role_EnableAutoPlay): the stay flag, then (computer, a != 0) the stock trade (a == 1: 0x25(2) again) and the card plan, then Role_BeforeRollDice
void stayOrBeforeRoll(Role &r, int a)
{
	if(rm8CardsStayCheck(r.seat)) return;
	if(!r.human && a == 1 && rm8StockAiTrade(r.seat)){ enqueue(r, 0x25, 2); return; }	// [V] Role_StayOrBeforeRoll @0x484610: AI_StockTrading @0x4f9310 first, then 0x25(2) again
	if(!r.human && a != 0 && rm8CardsAiStart(r.seat)){ r.state = 8; return; }
	beforeRollDice(r);
}

// [V] Role_EnableAutoPlay @0x48a4d0: not bankrupt (+0x1B4 != 7) and +0x1B0 == 0 -> +0x1B0 = 2; in state 6 Role_StayOrBeforeRoll(1) runs at once
static bool autoEnable(Role &r)
{
	if(r.status == 7 || !r.human) return false;
	r.human = false; r.autoPlay = true;
	if(r.state == 6){
		stayOrBeforeRoll(r, 1);
		if(r.state == 6 && !r.queue.empty()) r.state = 8;	// [A] a stock trade queues its text and 0x25(2) while the role is still in state 6, which the original never dispatches
	}
	return true;
}
// [V] Role_DisableAutoPlay @0x48a520: not bankrupt, +0x1B0 == 2 and state != 9 -> +0x1B0 = 0 (the next Role_BeforeRollDice waits for the human again)
static bool autoDisable(Role &r)
{
	if(r.status == 7 || !r.autoPlay || r.state == 9) return false;
	r.autoPlay = false; r.human = true;
	return true;
}
// event icons (boardfx.cpp): [V] Article_PlayApproachEffect @0x4EE3C0 / Article_PlayDepartEffect @0x4EE4F0
void fxApproach(const Role &r, int node, bool final){ float p[3] = {r.pos.x, r.pos.y, r.pos.z}; rm8BoardFxIconApproach(r.seat, node, p, final); }
void fxDepart(const Role &r, int node){ float p[3] = {r.pos.x, r.pos.y, r.pos.z}; rm8BoardFxIconDepart(r.seat, node, p); }

// [V] Game_TurnNextRole @0x4971a0 (no bankruptcy / new day yet)
static bool g_firstPick = false;	// the game start hands the turn to seat 0 without a new day
void turnNextRole()
{
	int n = (int)g_role.size();
	if(!n) return;
	for(int k = 0; k < n; k++){	// [V] skip bankrupt seats; wrapping to seat 0 starts a new day
		g_cur = (g_cur + 1) % n;
		if(g_cur == 0 && !g_firstPick){ rm8SaveBoundary(0); rm8GameOnNewRound(); }	// [V] Game_TurnNextRole @0x4971a0: the turn passes the last seat -> Game_SwitchRecordSlot (auto save) before the new day (save.cpp)
		if(g_role[g_cur].status != 7) break;
	}
	{ int lead = rm8GameTakeLeader(); if(lead >= 0 && lead < n) g_cur = lead; }	// [V] Game_TurnNextRole @0x4971a0: Game_FindLeaderByWinCondition returned the winner of the end date -> the turn index
	Role &r = g_role[g_cur];
	r.queue.erase(std::remove_if(r.queue.begin(), r.queue.end(), [](const Role::Msg &m){ return m.cmd == 0x5A; }), r.queue.end());	// [V] Role_RemoveQueuedMsgCmd5A @0x484ad0 (Game_TurnNextRole @0x4971a0)
	if(hasMovePath(r)){	// back to the centre and push the others aside
		V3d off = r.twTOff;
		checkPathValid(r);
		moveRoleToPosition(r, r.from, {r.pos.x - off.x, r.pos.y - off.y, r.pos.z - off.z}, true);
	}
	g_arrow.pos = nodePos(r.from); g_arrow.pos.y += 10.0f;	// [V] arrow at the node + (0,10,0)
	enqueue(r, 0x68);
	if(r.state == 0) r.state = 8;
	rm8SaveBoundary(2);	// test hook RM8_SAVE_DUMPT (save.cpp): the record at every turn start
}

// [V] Role_TickWaitTimer @0x4850b0 (and Role_StopMoveAndIdle @0x484790) call vtable[0] of the sub-object at +0x4C = Cls_5e1ef8__vf0 @0x48a330: when the
// expression flag +0x5C is set (vf5 with an action >= 3 and != 10, @0x48a270) it plays vf5(2, 0, 0) (stand) and clears the flag. The expression itself loops
// (vf5(n, 0, 0): count 0, no callback), so it lasts exactly as long as the wait that follows it (the 180 frame text box)
void endExpression(Role &r){ if(r.anim >= 3 && r.anim != 10){ if(getenv("RM8_DEBUG_PRESENT")) rm8Log("present: seat %d expression %d ends -> stand", r.seat, r.anim); r.special = false; playAnim(r, 2, false); } }

void dispatch(Role &r)
{
	// [V] Role_DispatchQueuedMsg: bankruptcy is checked for the role whose turn it is, before taking a message
	if(r.seat == g_cur && rm8GameCheckBankruptcy(r.seat)) return;
	if(r.queue.empty()){	// [V] state 8 with nothing queued: Role_CheckMissionsByCharacter, wait 1 s, then end the turn
		rm8StoryMissionCheck(r.seat);
		enqueue(r, 0x65, 60); enqueue(r, 0x66);
		return;
	}
	Role::Msg m = r.queue.front(); r.queue.erase(r.queue.begin());
	switch(m.cmd){
	case 0x66: r.state = 0; turnNextRole(); break;			// [V] Role_SkipTurnToNextRole
	case 0x68:
		setFocus(r.pos);
		if(r.intro){	// [V] Role_LeaveStatus8 @0x483540 with status 8: status 0, camera on the role, Role_ClearFlag1C8 (the doll is hidden), then
			r.intro = false;	// 0x57 (the debut model, Role_SpawnCharacterModel @0x48b410), 0x7C (Role_SetFlag1C8: the doll is back), 0x5A(0x1C) (the line)
			rm8PresentIntroBegin(r.seat);
			enqueue(r, 0x57); enqueue(r, 0x7c); enqueue(r, 0x5A, 0x1c);
		}else enqueue(r, 0x67, 0);	// [V] Role_LeaveStatus8
		break;
	case 0x67:	// [V] Role_RunTurnStartStep: 0 focus ... 6, then 9 dispatches on the status
		if(m.a == 0){ setFocus(r.pos); rm8CardsTurnStart(r.seat, 0); }	// [V] step 0: Role_DecrementField1E4 @0x48ed70 (invisible turns)
		if(m.a == 2) rm8StoryTurnStart(r.seat);			// [V] step 2: Role_RunStoryScriptEvents @0x483f40 + Role_CheckMissionsByCharacter @0x4835e0 (story.cpp)
		if(m.a == 3) rm8CardsTurnStart(r.seat, 3);			// [V] step 3: vf33 (the bodyguard counter of 莎拉)
		if(m.a == 4) rm8CardsTurnStart(r.seat, 4);			// [V] step 4: the bubble counter
		if(m.a == 5) rm8EventTurnStart(r.seat);			// [V] step 5: loan due @0x483170
		if(m.a < 6) enqueue(r, 0x67, m.a + 1);
		else if(m.a == 6){ rm8NpcTurnStep(r.seat); enqueue(r, 0x67, 9); }	// [V] step 6: Role_TickPossessionExpire @0x483350 (possession days, road bully)
		else if(rm8GameShape(r.seat) == 6 && r.status == 0){ r.state = 8; if(!rm8CardsStayCheck(r.seat)){ rm8GameNoteBadDay(r.seat); rm8NpcSleepwalk(r.seat); } }	// [V] step 9, status 0, no stay flag, possessed by 糊塗神: Role_ShowSleepwalkAndRoll @0x48b140
		else if((r.status == 1 || r.status == 3 || r.status == 4 || r.status == 5) && (rm8GameNoteBadDay(r.seat), true) && rm8CardsStatusStep(r.seat)) r.state = 8;	// [V] +0x340++ for status 1 / 3 / 4 / 5 before the tick
		else if(rm8CardsStatusStep(r.seat)) r.state = 8;		// [V] step 9: sleep / hospital / prison countdown (@0x48aff0 / 0x48a820 / 0x48ac70)
		else { r.state = 8; enqueue(r, 0x25, 1); }
		break;
	case 0x25:	// [V] Role_StayOrBeforeRoll @0x484610: stay flag, then (computer, a != 0) Role_UseSpecialCard, then Role_BeforeRollDice
		stayOrBeforeRoll(r, m.a); break;
	case 100: beforeRollDice(r); break;				// [V] Role_BeforeRollDice(0)
	case 0x79: if(m.b != 0 && autoEnable(r)) rm8Log("autoplay: seat %d is played by the computer (supply dialog confirmed)", r.seat); break;	// [V] dispatcher case 0x79 @0x481df0: result (b) != 0 -> Role_EnableAutoPlay
	case 0x1a: startRollDice(r, m.a); break;
	case 0x26: if(r.steps > 0) beginMoveSteps(r, r.steps); break;	// [V] continue the walk: vf26 = CPlayerTag__BeginMoveSteps(steps left)
	case 0x65: r.waitT = m.a / 60.0f * waitScale(); r.state = 5; if(getenv("RM8_DEBUG_HUD")) rm8Log("turn: seat %d wait %.2f s", r.seat, r.waitT); break;	// [V] wait frames/60 s
	default: rm8GameMessage(r.seat, m.cmd, m.a, m.b); break;	// rules (game.cpp)
	}
}

// [V] Role_TickIdleAnim @0x48a3f0: every rand()%10+10 s the idle action 3 once, then back to 2
void tickIdle(Role &r, float dt)
{
	if(r.special){
		Model &m = r.m[r.cur];
		AnimInterpolator *ip = m.hier ? m.hier->interpolator : nullptr;
		if(!ip || !ip->currentAnim || ip->currentTime >= ip->currentAnim->duration - 2e-4f){ r.special = false; playAnim(r, 2, false); }
		return;
	}
	if(r.anim >= 3 && r.anim != 10) return;	// [V] +0x5C: an expression pose (anim >= 3) is playing, no idle action (Role_TickIdleAnim @0x48a3f0, vf5 @0x48a270)
	r.idleT += dt;
	if(r.idleT >= r.idleGap){ r.idleT = 0; r.idleGap = (float)(rnd() % 10 + 10); r.special = true; playAnim(r, 3, true); }
}

// [V] Role_StepAlongPath @0x485860
void stepAlongPath(Role &r, float dt)
{
	if(r.steps < 1){ r.state = 8; return; }
	int old = r.from;
	animateAlongRoute(r, dt * r.speed);
	fxDepart(r, old);
	updateRoleDirection(r, old, r.pos);
	if(r.steps == 0){ fxApproach(r, r.from, true); moveRoleToPosition(r, r.from, r.pos, true); }
	else {
		if(r.from != old){ fxDepart(r, r.from); updateRoleDirection(r, r.from, r.pos); }
		fxApproach(r, r.to, r.steps == 1);
		moveRoleToPosition(r, r.to, r.pos, r.steps == 1);
	}
	setFocus(r.pos);
}

void mainUpdate(Role &r, float dt)
{
	switch(r.state){
	case 0: case 6: tickIdle(r, dt); break;
	case 1: stepAlongPath(r, dt); break;
	case 3: { int sum = rm8DiceUpdate(dt, g_cam); if(sum > 0){ if(getenv("RM8_DEBUG_DICE")) rm8Log("dice: seat %d rolled %d", r.seat, sum); beginMoveSteps(r, sum); } } break;
	case 0xb: stepTowardBuilding(r, dt); break;	// [V] Role_StepTowardBuilding @0x4854b0: walking into the hotel
	case 0xc: stepTowardNode(r, dt); break;	// [V] Role_StepTowardTerraNode @0x4856d0: walking out of it
	case 4: { int sum = rm8DiceUpdate(dt, g_cam); if(sum > 0){ diceSync(r); g_evDie = sum; r.state = 9; playAnim(r, 2, false); } } break;	// [V] the event die: u_CPlayerTag_SyncGameState @0x485020 restores the dice count / flags (State_SyncFields), the result goes to the task, state 9 again
	case 5: r.waitT -= dt; if(r.waitT <= 0 && !rm8PresentSeatFxBusy(r.seat)){ r.state = 8; endExpression(r); if(getenv("RM8_DEBUG_HUD")) rm8Log("turn: seat %d wait ends", r.seat); } break;	// [V] Role_TickWaitTimer @0x4850b0: also waits for the role's effect / debut model handles (+0x50 / +0x54)
	case 7: break;	// [V] waiting for a dialog (Role_OpenModalUi)
	case 9: break;	// [V] an event task is running
	case 10: break;	// [V] an NPC interaction runs (Role_LeaveState10 @0x486110 sets state 8)
	case 8: dispatch(r); break;
	default: r.state = 8; break;
	}
}

void placeArrow()
{
	if(!g_arrow.clump) return;
	Frame *f = g_arrow.clump->getFrame();
	V3d np0 = {-g_arrow.p0.x, -g_arrow.p0.y, -g_arrow.p0.z}, s = {1.0f, 1.2f, 1.0f}, ay = {0, 1, 0};	// [V] scale (1, 1.2, 1)
	V3d p1 = {g_arrow.p0.x + g_arrow.pos.x, g_arrow.p0.y + g_arrow.pos.y, g_arrow.p0.z + g_arrow.pos.z};
	f->transform(&g_arrow.base, COMBINEREPLACE);
	f->translate(&np0, COMBINEPOSTCONCAT); f->scale(&s, COMBINEPOSTCONCAT);
	f->rotate(&ay, CAM_YAW - 180.0f, COMBINEPOSTCONCAT); f->translate(&p1, COMBINEPOSTCONCAT);
}

}	// namespace

bool rm8RolesLoadDefs(const std::string &path)
{
	std::ifstream f(path);
	if(!f){ rm8Log("no %s (run scripts/export_roles.py)", path.c_str()); return false; }
	g_arts.clear(); g_roles.clear();
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "role"){ RoleDef d; ss >> d.id; for(float &v : d.scale) ss >> v; for(float &v : d.offset) ss >> v; for(float &v : d.speed) ss >> v; for(int &a : d.art) ss >> a;
			for(auto &dp : d.dice) for(float &v : dp) ss >> v; ss >> d.sex >> d.pic >> d.ai >> d.name; g_roles[d.id] = d; }
		else if(kw == "arrow") ss >> g_arrowDff >> g_arrowTex >> g_arrowAnm;
		else if(kw == "snd"){ int id; ss >> id; auto it = g_roles.find(id); if(it != g_roles.end()) for(int &v : it->second.wlkSnd) ss >> v; }	// walking sample per vehicle (after its role line)
		else if(kw == "model"){ int a; ss >> a; ss >> g_arts[a].dff >> g_arts[a].texdir; }
		else if(kw == "anim"){ int a, i; std::string p; ss >> a >> i >> p; g_arts[a].anims[i] = p; }
		else if(kw == "face"){ int a, i; FaceKey k; ss >> a >> i >> k.t >> k.part >> k.tex; g_arts[a].faces[i].push_back(k); }
	}
	rm8Log("roles: %d characters, %d models", (int)g_roles.size(), (int)g_arts.size());
	rm8PresentLoadDefs(path);	// presentation line types (speech tables, effect arts)
	{ size_t sl = path.find_last_of("/\\"); rm8NpcLoadDefs((sl == std::string::npos ? std::string() : path.substr(0, sl + 1)) + "npcs.txt"); }	// NPCs (scripts/export_npcs.py)
	return !g_roles.empty();
}

void rm8RolesClear(Rm8Scene &s)
{
	rm8PresentSetScene(nullptr);
	rm8NpcClear(s);
	rm8GameClear(s);
	for(auto &kv : g_fol) for(Model &m : kv.second.r.m) if(m.clump){ if(s.world) s.world->removeClump(m.clump); m.clump->destroy(); m.clump = nullptr; }
	g_fol.clear();
	for(Role &r : g_role){
		stopWalkSound(r);
		for(Model &m : r.m) if(m.clump){ if(s.world) s.world->removeClump(m.clump); m.clump->destroy(); }
		if(r.shadow){ if(s.world) s.world->removeClump(r.shadow); r.shadow->destroy(); }
	}
	if(g_arrow.clump){ if(s.world) s.world->removeClump(g_arrow.clump); g_arrow.clump->destroy(); }
	g_arrow = Arrow();
	g_role.clear();
	g_reg.clear();
	g_cur = 0;
	g_placedOnly = false;
	g_nodes.clear();
	rm8DiceClear();
}

static unsigned g_humanMask = 0;	// [A] seats played by humans besides the humanSeat argument (menus)
void rm8RolesSetHumans(unsigned mask){ g_humanMask = mask; }
static std::vector<int> g_outfits;	// [A] menu seats' outfits (rm8RolesSetOutfits)
void rm8RolesSetOutfits(const std::vector<int> &o){ g_outfits = o; }

void rm8RolesSpawn(Rm8Scene &s, const std::vector<int> &ids, unsigned seed, int humanSeat)
{
	rm8RolesClear(s);
	g_seed = seed;
	rm8DiceSeed(seed * 7 + 3);
	buildGraph(s);
	std::vector<int> starts;
	for(int i = 0; i < (int)g_nodes.size(); i++) if(g_graph.links[i].size() == 2) starts.push_back(i);
	if(starts.empty()){ rm8Log("roles: no road nodes"); return; }
	for(size_t k = 0; k < ids.size(); k++){
		auto it = g_roles.find(ids[k]);
		if(it == g_roles.end()) continue;
		Role r; r.def = &it->second; r.seat = (int)g_role.size(); r.color = r.seat < (int)g_outfits.size() ? g_outfits[r.seat] : 0; r.speed = r.def->speed[0];
		r.human = r.seat == humanSeat || ((g_humanMask >> r.seat) & 1);	// [A] menu seats (rm8RolesSetHumans)
		if(r.seat == 0 && getenv("RM8_AUTO_SEAT0")){ r.human = false; r.autoPlay = true; }	// test hook (scripts/save_roundtrip.py with auto play): seat 0 starts as a seat played by the computer (+0x1B0 == 2)
		r.intro = getenv("RM8_SKIP_INTRO") == nullptr;	// [V] Game_SelectRandomTerritory @0x496180 places every role with vf24 (status 8); RM8_SKIP_INTRO = test switch
		r.idleGap = (float)(rnd() % 10 + 10);
		bool ok = true;
		for(int j = 0; j < 4; j++) ok &= loadModel(s, r.m[j], r.def->art[j], r.def->id, r.color, j < 2);
		if(!ok){ for(Model &m : r.m) if(m.clump){ s.world->removeClump(m.clump); m.clump->destroy(); } continue; }
		// [V] Path_SetPoints @0x47b670: from = start node, to = next node, face it without spinning
		int st = starts[rnd() % starts.size()];
		r.from = st; r.to = g_graph.links[st][rnd() % 2]; r.pos = nodePos(st);
		r.cur = 0; playAnim(r, 2, false);
		calcAngle(r, nodePos(r.from), nodePos(r.to), false);
		rm8Log("role %d (seat %d%s) at node (%d,%d) pos (%g %g %g) model %d", r.def->id, r.seat, r.human ? ", human" : "", g_nodes[st].tx, g_nodes[st].ty, r.pos.x, r.pos.y, r.pos.z, r.cur);
		loadShadow(s, r);
		g_role.push_back(std::move(r));
	}
	g_reg.assign(g_role.size(), -1);
	g_scene = &s;
	for(Role &r : g_role) if(r.def->id == 13) followerCreate(r, false);	// [V] Game_SpawnNpc @0x495f60: the class of 伊格魯 (vf22 @0x47e1a0 = Role_CreateAvatar + Cls_5e27d8_CreateMovieActor @0x47e1e0)
	if(!g_arrowDff.empty()){	// [V] turn arrow art 6114 (Game_StartNewRound @0x4962a0), animation 0 looped
		g_arrow.clump = rm8LoadClump(g_arrowDff, g_arrowTex);
		if(g_arrow.clump){
			g_arrow.base = g_arrow.clump->getFrame()->matrix; g_arrow.base.flags = 0; g_arrow.p0 = g_arrow.base.pos;
			g_arrow.hier = HAnimHierarchy::find(g_arrow.clump->getFrame());
			Animation *an = g_arrowAnm != "-" ? rm8LoadAnim(g_arrowAnm) : nullptr;
			if(g_arrow.hier && an && an->getNumNodes() == g_arrow.hier->numNodes){ g_arrow.hier->attach(); g_arrow.hier->interpolator->setCurrentAnim(an); }
			else g_arrow.hier = nullptr;
			s.world->addClump(g_arrow.clump);
		}
	}
	for(Role &r : g_role){ updateAnim(r, 0); placeModel(r); g_reg[r.seat] = r.from; }
	g_scene = &s;
	rm8PresentSetScene(&s);
	rm8GameNewGame(s, seed);
	rm8NpcNewGame(s);	// [V] Npc_SpawnAll @0x49e2d0 (Game_InitGame @0x4a1090)
	rm8StoryActivateBuildings(s);	// [V] Game_ActivateCharacterBuilding @0x4a37d0 (Game_InitGame, right after Game_SelectRandomTerritory): the chain-store sign of 詹姆斯's unlock
	if(!g_role.empty()){ g_cur = (int)g_role.size() - 1; g_firstPick = true; turnNextRole(); g_firstPick = false; }	// the first turn goes to seat 0; [V] the introduction round is day 0 and the first real round day 1
	// (Steam record_01 written at the end of the introduction round: day_count 0; Role_RunStoryScriptEvents @0x483f40 tests day == 1 at the first real turn) - the wrap used to count
	// a new day here, so the first real round was day 2 and the day-1 story events (大老千's debt, 孫小美's hint) never ran
	rm8Log("roles: %d characters on %d road nodes", (int)g_role.size(), (int)g_nodes.size());
}

void rm8RolesSetCamera(Camera *cam)
{
	g_cam = cam;
	rm8BoardFxSetCamera(cam);
	rm8NpcSetCamera(cam);
	rm8CardsSetCamera(cam);
	const V3d &at = cam->getFrame()->getLTM()->at;
	if(fabsf(at.x) + fabsf(at.z) < 1e-4f) return;
	float y = atan2f(-at.z, -at.x) * 180.0f / 3.14159265f;
	if(y < 0) y += 360.0f;
	float dy = fabsf(y - CAM_YAW); if(dy > 180.0f) dy = 360.0f - dy;
	CAM_YAW = y;
	// [A] the game camera never turns on the board; when the port's camera does, re-aim standing/walking dolls at once
	if(dy > 1.0f) for(Role &r : g_role) if(!r.spinning && r.from >= 0 && r.to >= 0) calcAngle(r, nodePos(r.from), nodePos(r.to), false);
}

// [V] CPlayerTag__vf3_Update @0x481500 for every character: walking scales dt by the game speed; the same-node tween
// or a spin pauses the state machine; animations always advance
// [A] the body of one role's frame and the tail of the frame (NPCs, cards, news, the arrow); split so that a loaded day-wrap record can finish the frame it was
// written in (save_roles.inc: the record is written in the middle of the role loop)
static int g_loopSeat = -1;	// the role the loop is on (-1 outside the loop); the frame time of the running frame
static float g_frameDt = 0;
static void roleStep(Role &r, float dt, bool skipMain)
{
	float d = dt;
	if(r.state == 1) d *= (rm8GameSpeed - 1) / 4.0f + 1.0f;
	if(!skipMain){
		if(!g_placedOnly && !tweenUpdate(r, d) && !spinUpdate(r, d)) mainUpdate(r, d);
		else if(g_placedOnly) spinUpdate(r, d);
	}
	updateAnim(r, d);
	placeModel(r);
	if(!g_placedOnly && r.def && (r.def->id == 13 || r.def->id == 16)){
		if(r.def->id == 16){	// [V] vf32 @0x4803e0 (Role.Bodyguard) creates the guard, vf33 @0x480420 releases it when the counter ends: here the card module's counter (rm8CardsHasBodyguard)
			static const bool testGuard = getenv("RM8_GUARD_TEST") != nullptr;	// test hook: the guard is there from the start (without the card)
			bool want = rm8CardsHasBodyguard(r.seat) || testGuard; Follower *f = followerOf(r.seat);
			if(want && !f) followerCreate(r, true); else if(!want && f) followerDestroy(*f);
		}
		followerUpdate(r, dt);
	}
	{ float pp[3] = {r.pos.x, r.pos.y, r.pos.z}; rm8NpcRoleUpdate(r.seat, pp, r.cur, r.anim, r.once, r.yaw, r.twScale, r.def->offset, d); }	// possessed: the NPC figure follows
}
static void frameTail(float dt)
{
	rm8NpcUpdate(dt);	// NPC objects (gods, board characters, obstacles)
	rm8CardsUpdate(dt);	// touch hold on a card, the card test driver (cards.cpp)
	rm8NewsUpdate(dt);	// the role-bound news (police / thief / lucky card / chest / festivals) and the test hooks (news.cpp)
	if(g_arrow.hier){
		AnimInterpolator *ip = g_arrow.hier->interpolator;
		ip->addTime(dt);
		for(int i = 0; i < g_arrow.hier->numNodes; i++){ Frame *f = g_arrow.hier->nodeInfo[i].frame; if(f){ ip->applyCB(&f->matrix, ip->getInterpFrame(i)); f->updateObjects(); } }
		g_arrow.hier->updateMatrices();
	}
	placeArrow();
}

void rm8RolesUpdate(Rm8Scene &, float dt)
{
	rm8SkipUpdate(dt);	// test hook (RM8_SKIP_TEST, skip.cpp)
	rm8PresentUpdate(dt);
	g_frameDt = dt;
	{	// test hook RM8_TURN_TEST=<seconds>: seat 0 turns around (Role.Turn, vf16) at that game time; RM8_DEBUG_FOLLOW prints the companion
		static const char *tt = getenv("RM8_TURN_TEST"); static float clk = 0; static bool done = false;
		clk += dt;
		if(tt && !done && clk >= (float)atof(tt)){ done = true; rm8TurnTurnAround(0); }
	}
	for(Role &r : g_role){ g_loopSeat = r.seat; roleStep(r, dt, false); }
	g_loopSeat = -1;
	frameTail(dt);
}

// ---- the time bomb on a role's back ([V] CPlayerTag__InitializeTarget @0x48e2d0 -> Bomb_InitDraw @0x499b40, placed by u_CPlayerTag_UpdateAttachedPosition @0x48e4b0, drawn by
// Billboard_RenderWithNumber @0x499ef0 from CPlayerTag__Render @0x485d00): two 32 x 32 camera facing quads in the world, TimeBomb.tga (the dynamite bundle) at
// role position - 10 * (travel direction) * scale horizontally and height (roleinfo dicePos1 y + 5 - 10 * direction y) * scale, and TimeBomb_01.tga (the display plate) 25 units to
// the side (screen right for the models facing 0 / 3, left for 1 / 2: Billboard_RenderWithNumber(model index % 3)); z test and z write off, the alpha test of the role pass; the
// counter "%2d" in black (font size 16) at the plate's projected centre - 8, - 6 pixels. Drawn before the character when it faces the camera (model index / 2 == 0: the bomb
// hangs behind it), after it when it shows its back.
static void drawDigits(Camera *cam, float x, float y, float k, const char *txt, uint8 r, uint8 g, uint8 b);
static void bombDraw(Role &r, int count)
{
	if(!g_cam || !g_cam->frameBuffer || count <= 0) return;
	static Texture *tb = nullptr, *tp = nullptr; static bool tried = false;
	if(!tried){ tried = true; tb = rm8PackTexture("prt\\npc\\TimeBomb.tga"); tp = rm8PackTexture("prt\\npc\\TimeBomb_01.tga"); if(!tb || !tp) rm8Log("warning: TimeBomb textures missing (%p %p)", (void*)tb, (void*)tp); }
	if(!tb || !tp) return;
	V3d dir = {0, 0, 0};
	if(r.from >= 0 && r.to >= 0 && r.from < (int)g_nodes.size() && r.to < (int)g_nodes.size()){
		V3d a = nodePos(r.from), b = nodePos(r.to);
		dir = {b.x - a.x, b.y - a.y, b.z - a.z};
		float l = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if(l > 1e-4f){ dir.x /= l; dir.y /= l; dir.z /= l; } else dir = {0, 0, 0};
	}
	float sc = r.twScale, hy = r.def ? r.def->dice[0][1] : 30.0f;
	V3d P = {r.pos.x + sc * dir.x * -10.0f, r.pos.y + sc * (hy + 5.0f + dir.y * -10.0f), r.pos.z + sc * dir.z * -10.0f};
	const Matrix *cm = g_cam->getFrame()->getLTM();
	V3d X = {-cm->right.x, -cm->right.y, -cm->right.z}, Y = cm->up;	// screen right / up
	float side = ((unsigned)r.cur % 3 == 0) ? 1.0f : -1.0f;	// -25 * the camera's (screen-left) right vector for model index % 3 == 0: the plate to the screen right
	V3d P2 = {P.x + X.x * 25.0f * side, P.y + X.y * 25.0f * side, P.z + X.z * 25.0f * side};
	uint32 zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE), cull = GetRenderState(CULLMODE), va = GetRenderState(VERTEXALPHA);
	uint32 sb = GetRenderState(SRCBLEND), db = GetRenderState(DESTBLEND);
	SetRenderState(ZTESTENABLE, 0); SetRenderState(ZWRITEENABLE, 0); SetRenderState(CULLMODE, CULLNONE);
	SetRenderState(VERTEXALPHA, 1); SetRenderState(SRCBLEND, BLENDSRCALPHA); SetRenderState(DESTBLEND, BLENDINVSRCALPHA);
	SetRenderState(TEXTUREFILTER, Texture::LINEAR);
	for(int q = 0; q < 2; q++){
		const V3d &C = q == 0 ? P : P2;
		gl3::Im3DVertex v[4]; uint16 idx[6] = {0, 1, 2, 0, 2, 3};
		const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1}, uu[4] = {0, 1, 1, 0}, vv[4] = {1, 1, 0, 0};	// screen right / up, the texture upright
		for(int k = 0; k < 4; k++){
			v[k].setX(C.x + (sx[k] * X.x + sy[k] * Y.x) * 16.0f * sc); v[k].setY(C.y + (sx[k] * X.y + sy[k] * Y.y) * 16.0f * sc); v[k].setZ(C.z + (sx[k] * X.z + sy[k] * Y.z) * 16.0f * sc);
			v[k].setNormalX(0); v[k].setNormalY(1); v[k].setNormalZ(0);
			v[k].setColor(255, 255, 255, 255); v[k].setU(uu[k]); v[k].setV(vv[k]);
		}
		SetRenderStatePtr(TEXTURERASTER, (q == 0 ? tb : tp)->raster);
		im3d::Transform(v, 4, nil, im3d::EVERYTHING);
		im3d::RenderIndexedPrimitive(PRIMTYPETRILIST, idx, 6);
		im3d::End();
	}
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(CULLMODE, cull); SetRenderState(VERTEXALPHA, va); SetRenderState(SRCBLEND, sb); SetRenderState(DESTBLEND, db);
	// the counter on the plate
	const Matrix *m = cm;
	V3d d = {P2.x - m->pos.x, P2.y - m->pos.y, P2.z - m->pos.z};
	float xc = d.x * m->right.x + d.y * m->right.y + d.z * m->right.z, yc = d.x * m->up.x + d.y * m->up.y + d.z * m->up.z, zc = d.x * m->at.x + d.y * m->at.y + d.z * m->at.z;
	if(zc < 1.0f) return;
	float W = (float)g_cam->frameBuffer->width, H = (float)g_cam->frameBuffer->height;
	float px = (0.5f - xc / (2.0f * g_cam->viewWindow.x * zc)) * W, py = (0.5f - yc / (2.0f * g_cam->viewWindow.y * zc)) * H, k = H / 600.0f;
	char txt[8]; snprintf(txt, sizeof txt, "%2d", count);
	drawDigits(g_cam, px - 8.0f * k, py - 6.0f * k, 0.25f * k, txt, 0, 0, 0);
}

void rm8RolesRender()
{
	if(g_role.empty()) return;
	uint32 fn = GetRenderState(ALPHATESTFUNC), ref = GetRenderState(ALPHATESTREF);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL);
	SetRenderState(ALPHATESTREF, 196);
	static const bool noShadow = getenv("RM8_NO_ROLE_SHADOW") != nullptr;	// comparison switch
	rm8NpcRender();	// NPC objects before the characters
	for(Role &r : g_role){
		if(r.status == 7) continue;	// [V] CPlayerTag__OnRoleBankruptcy @0x48f750 moves a bankrupt role off the board
		if(r.status > 2) continue;
		if(Follower *fo = followerOf(r.seat)){	// [V] Cls_5e27d8__vf4_Movie @0x47dcf0: the companion is drawn with the role (status < 3, the role not hidden) or when its own flag +0x80 is set (the debut run-in)
			bool roleShown = !rm8NpcRoleHidden(r.seat) && !rm8PresentRoleHidden(r.seat);
			if(!fo->hidden && (roleShown || fo->vis) && fo->r.m[fo->r.cur].clump) fo->r.m[fo->r.cur].clump->render();
		}	// [V] CPlayerTag__Render @0x485d00 draws only when vf+0x20 (CanMoveNormally @0x4932e0: status <= 2) holds: hotel 3, hospital 4, prison 5 (inside the building) and vanished 6 are not drawn
		if(rm8NpcRoleHidden(r.seat)) continue;	// [V] the same test needs +0x1C8 != 0: Role_ClearFlag1C8 (a god / egg interaction, the ambulance / police car show) hides the role (npc_act.cpp)
		if(rm8PresentRoleHidden(r.seat)) continue;	// [V] the same flag during the game-start introduction (present.cpp)
		// [V] CPlayerTag__Render: a die held by a back-facing thrower is drawn before the character (hidden behind it)
		if((r.state == 3 || r.state == 4) && r.cur / 2 != 0 && rm8DiceInHand() && g_cam){ rm8DiceRender(g_cam); SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196); }
		int bomb = rm8NpcTimeBomb(r.seat);	// [V] CPlayerTag__Render @0x485d00: the bomb billboard before the character when it faces the camera (model index / 2 == 0), after it otherwise
		if(bomb > 0 && r.cur / 2 == 0){ bombDraw(r, bomb); SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196); }
		if(rm8NpcRoleRender(r.seat)){}	// possessed by 1..9: the NPC figure replaces the role's models
		else if(r.m[r.cur].clump) r.m[r.cur].clump->render();	// atomics in RenderWare order (rm8LoadClump)
		if(bomb > 0 && r.cur / 2 != 0){ bombDraw(r, bomb); SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196); }
		if(r.shadow && !noShadow){	// [V] CPlayerTag__Render -> Mesh_DrawWithAlphaTest @0x47bbc0: alpha test ALWAYS, then >= 196
			SetRenderState(ALPHATESTFUNC, ALPHAALWAYS);
			r.shadow->render();
			SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL);
		}
	}
	// [V] the turn arrow only while the current character waits for the player (state 6), after all characters
	if(g_arrow.clump && !g_placedOnly && g_role[g_cur].state == 6) g_arrow.clump->render();
	SetRenderState(ALPHATESTFUNC, fn);
	SetRenderState(ALPHATESTREF, ref);
}

// [V] after everything 3D: the dice (Dice_DrawIfActive @0x485fd0: front-facing thrower or dice in the air) and the
// 2D step counter while walking (CDice_DrawNumber @0x47cef0: number.tga 4x3 cells of 32x42, horizontally centred,
// 180 px above the screen centre on the game's 800x600 screen; [A] scaled by screen height / 600)
// number.tga digits (4x3 cells of 32x42: "0123" "4567" "89* "), drawn as Im2D quads at screen pixel (x, y), cell scale k
static void drawDigits(Camera *cam, float x, float y, float k, const char *txt, uint8 r, uint8 g, uint8 b)
{
	static Texture *num = nullptr; static bool tried = false;
	if(!tried){ tried = true; num = rm8PackTexture("font\\number.tga"); if(!num) rm8Log("warning: font\\number.tga missing"); }
	if(!num) return;
	static const char *cells = "0123456789* ";
	float rz = 1.0f / cam->nearPlane, z = im2d::GetNearZ();
	uint32 zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE), filt = GetRenderState(TEXTUREFILTER), atf = GetRenderState(ALPHATESTFUNC);
	SetRenderState(ZTESTENABLE, 0); SetRenderState(ZWRITEENABLE, 0); SetRenderState(ALPHATESTFUNC, ALPHAALWAYS);
	SetRenderStatePtr(TEXTURERASTER, num->raster);
	SetRenderState(TEXTUREFILTER, Texture::NEAREST);	// [V] sampler POINT while drawing
	SetRenderState(VERTEXALPHA, 1);
	for(int i = 0; txt[i]; i++){
		const char *cp = strchr(cells, txt[i]); if(!cp) continue;
		int c = (int)(cp - cells), col = c % 4, row = c / 4;
		float u0 = col * 32 / 128.0f, v0 = row * 42 / 128.0f, u1 = (col * 32 + 32) / 128.0f, v1 = (row * 42 + 42) / 128.0f;
		float x0 = x + i * 32 * k, x1 = x0 + 32 * k, y0 = y, y1 = y + 42 * k;
		gl3::Im2DVertex v[4]; uint16 idx[6] = {0, 1, 2, 2, 1, 3};
		float px[4] = {x0, x1, x0, x1}, py[4] = {y0, y0, y1, y1}, pu[4] = {u0, u1, u0, u1}, pv[4] = {v0, v0, v1, v1};
		for(int q = 0; q < 4; q++){
			v[q].setScreenX(px[q]); v[q].setScreenY(py[q]); v[q].setScreenZ(z);
			v[q].setCameraZ(cam->nearPlane); v[q].setRecipCameraZ(rz);
			v[q].setColor(r, g, b, 255); v[q].setU(pu[q], rz); v[q].setV(pv[q], rz);
		}
		im2d::RenderIndexedPrimitive(PRIMTYPETRILIST, v, 4, idx, 6);
	}
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(TEXTUREFILTER, filt); SetRenderState(ALPHATESTFUNC, atf);
}

void rm8RolesRenderLate(Camera *cam)
{
	rm8PresentRenderLate(cam);
	rm8NpcRenderLate(cam);
	rm8NewsRender(cam);	// the news scene: the picture and the anchor in front of the camera (News_InitDialog @0x4b41b0), before the UI
	if(g_role.empty() || g_placedOnly) return;
	Role &r = g_role[g_cur];
	if(rm8DiceActive() && (r.cur / 2 == 0 || !rm8DiceInHand())) rm8DiceRender(cam);
	float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height, k = H / 600.0f;
	if(r.state != 1 || r.steps <= 0) return;
	char txt[16]; snprintf(txt, sizeof txt, "%d", r.steps);
	int n = (int)strlen(txt);
	drawDigits(cam, floorf(W / 2) - 16.0f * n * k, floorf(H / 2 - 180.0f * k), k, txt, 255, 255, 255);	// [V] CDice_DrawNumber
}

void rm8RolesPlace(Rm8Scene &s, int id, const float p[3], int model, int anim)
{
	auto it = g_roles.find(id);
	if(it == g_roles.end()){ rm8Log("roles: no character %d", id); return; }
	if(g_nodes.empty()) buildGraph(s);
	g_placedOnly = true;
	Role r; r.def = &it->second; r.seat = (int)g_role.size(); r.speed = r.def->speed[0];
	for(int j = 0; j < 4; j++) if(!loadModel(s, r.m[j], r.def->art[j], r.def->id, r.color, j < 2)) return;
	r.pos = {p[0], p[1], p[2]}; r.cur = model & 3;
	playAnim(r, anim, false);
	loadShadow(s, r);
	updateAnim(r, 0); placeModel(r);
	g_role.push_back(std::move(r));
	g_reg.push_back(-1);
}

int rm8RolesCurrent(){ return g_role.empty() || g_placedOnly ? -1 : g_cur; }

bool rm8RolesGetPos(int k, float p[3])
{
	if(k < 0 || k >= (int)g_role.size()) return false;
	p[0] = g_role[k].pos.x; p[1] = g_role[k].pos.y; p[2] = g_role[k].pos.z;
	return true;
}

int rm8RolesFocus(float p[3]){ p[0] = g_focus.x; p[1] = g_focus.y; p[2] = g_focus.z; return g_focusSerial; }

bool rm8RolesHumanRoll()
{
	if(rm8NpcPress()) return true;	// the 財神 / 窮神 reel dialog takes the key / click
	if(g_role.empty() || g_placedOnly) return false;
	Role &r = g_role[g_cur];
	if(!r.human || r.state != 6) return false;	// [V] Game_TriggerRollDice @0x4e8340: dice button in state 6
	startRollDice(r, 0);	// [V] Role_StartRollDice(0) throws +0x230 dice as the slot toggles left them (no reset to the vehicle's maximum)
	return true;
}

// ---- turn.h (for game.cpp / python.cpp)
int rm8TurnRoleCount(){ return (int)g_role.size(); }
int rm8TurnCurrent(){ return g_cur; }
static bool okSeat(int k){ return k >= 0 && k < (int)g_role.size(); }
int rm8TurnCharId(int k){ return okSeat(k) ? g_role[k].def->id : 0; }
int rm8TurnSex(int k){ return okSeat(k) ? g_role[k].def->sex : 0; }	// [V] roleinfo +0x304
const char *rm8TurnAiScript(int k){ return okSeat(k) && !g_role[k].def->ai.empty() && g_role[k].def->ai != "-" ? g_role[k].def->ai.c_str() : nullptr; }
bool rm8TurnHuman(int k){ return okSeat(k) && g_role[k].human; }
bool rm8TurnAuto(int k){ return okSeat(k) && g_role[k].autoPlay; }
bool rm8TurnIsPlayer(int k){ return okSeat(k) && (g_role[k].human || g_role[k].autoPlay); }	// [V] +0x1B0 != 1: the counts of Role_CheckBankruptcy @0x48ee10 / Game_TurnNextRole @0x4971a0 treat a taken-over seat like a human

// ---- auto play / hosting (托管), docs/autoplay.md
// [V] CGamePlayScene__OnEvent @0x4a1e80, keys 0x81..0x84 (DirectInput F1..F4, table 0x5DF5F0) = seat 0..3: Game_GetPlayerByIndex @0x4a6cf0 gives the seat back when it is
// played by the computer, else Game_GetPlayerAtSeat @0x4a6c30 hands the current role to the computer while it waits for the dice (state 6)
bool rm8AutoKey(int seat)
{
	if(!okSeat(seat) || g_placedOnly) return false;
	if(autoDisable(g_role[seat])){ rm8Log("autoplay: seat %d is played by the human again (key)", seat); return true; }
	Role &c = g_role[g_cur];
	if(c.seat == seat && c.state == 6 && autoEnable(c)){ rm8Log("autoplay: seat %d is played by the computer (key)", seat); return true; }
	return false;
}
// [V] Game_SelectNextPlayer @0x4a6d60, run first for every key of the scene (vf5 @0x4a1e80): while the current role walks (state 1), the first other seat after it that is
// played by the computer is given back, and the key is consumed
bool rm8AutoRelease()
{
	int n = (int)g_role.size();
	if(!n || g_placedOnly || g_role[g_cur].state != 1) return false;
	for(int k = (g_cur + 1) % n; k != g_cur; k = (k + 1) % n)
		if(autoDisable(g_role[k])){ rm8Log("autoplay: seat %d is played by the human again (key while seat %d walks)", k, g_cur); return true; }
	return false;
}
// [V] gsys button 1 (@0x4aa700 -> NPC_SendActOnPlayer2 @0x4aa810 -> CUi_supply__ShowUI @0x4928e0): the gsys task is Game_DispatchCardAction(0x68) @0x4a4420 (state 9, queue
// cleared, 100 queued), the supply dialog goes to the front: 1(0, 0x87), 0x1C, 0x79(result); a confirmed result is Role_EnableAutoPlay, then 100 = Role_BeforeRollDice(0)
bool rm8AutoSupplyOpen()
{
	if(g_role.empty() || g_placedOnly) return false;
	Role &r = g_role[g_cur];
	if(!r.human || r.state != 6) return false;
	r.queue.clear();
	enqueue(r, 100);
	r.queue.insert(r.queue.begin(), {0x79, 0, (int)0x80000000});
	r.queue.insert(r.queue.begin(), {0x1C, 0, 0});
	r.queue.insert(r.queue.begin(), {1, 0, 0x87});
	r.state = 8;
	return true;
}
int rm8TurnNode(int k){ return okSeat(k) ? g_role[k].from : -1; }
int rm8TurnStatus(int k){ return okSeat(k) ? g_role[k].status : 7; }
// [V] messages 0x13 / 0x14 (vf34 / vf35 with 0): the hotel walk (above); cards.cpp / events.cpp
void rm8TurnHotelWalk(int k, bool enter)
{
	if(!okSeat(k)) return;
	Role &r = g_role[k];
	if(r.status == 7) return;
	if(r.from < 0){ if(enter) rm8CardsEnterHotel(k); else r.status = 0; return; }
	V3d b;
	if(enter){
		if(!hotelBuilding(r, b)){ rm8CardsEnterHotel(k); return; }	// [A] no building: the stay starts at once
		if(getenv("RM8_DEBUG_FOLLOW")) rm8Log("hotel walk in: seat %d node %d (%.0f,%.0f,%.0f) role (%.0f,%.0f,%.0f) building (%.0f,%.0f,%.0f)", k, r.from, nodePos(r.from).x, nodePos(r.from).y, nodePos(r.from).z, r.pos.x, r.pos.y, r.pos.z, b.x, b.y, b.z);
		calcAngle(r, nodePos(r.from), b, true);
		playAnim(r, 0, false); startWalkSound(r);
		r.state = 0xb;
		followerHotelStart(r, true);
	}else{
		calcAngle(r, r.pos, nodePos(r.from), false);
		playAnim(r, 0, false); startWalkSound(r);
		r.state = 0xc; r.status = 0;
		followerHotelStart(r, false);
	}
}
// [V] vf27 (@0x48ceb0 hides the role; the class of 伊格魯 / 莎拉 @0x47dfa0 adds Path_SetSpeedVector(the camera's right vector)): the ambulance / police car / UFO scene carries the role off
void rm8TurnFollowerRunOff(int k)
{
	Follower *fo = okSeat(k) ? followerOf(k) : nullptr;
	if(!fo) return;
	V3d F, R; camAxes(F, R);
	fo->vel = true; fo->velV = R; fo->velDist = 0; fo->delay = 2.0f;	// +0x6c = 0, +0x94 = 2.0
}
void rm8TurnSetStatus(int k, int st){ if(okSeat(k)){ if(g_role[k].status == 3 && st != 3 && g_role[k].state != 0xc) g_role[k].twScale = 1.0f; g_role[k].status = st; if(st == 7){ g_role[k].state = 0; updateRoleDirection(g_role[k], g_role[k].from, {1e9f, 0, 1e9f}); } } }
void rm8TurnEnqueue(int k, int cmd, int a, int b){ if(okSeat(k)) enqueue(g_role[k], cmd, a, b); }
void rm8TurnEnqueueAt(int k, int pos, int cmd, int a, int b){
	if(!okSeat(k)) return;
	auto &q = g_role[k].queue; pos = std::max(0, std::min(pos, (int)q.size()));
	q.insert(q.begin() + pos, {cmd, a, b});
}
void rm8TurnClearQueue(int k){ if(okSeat(k)) g_role[k].queue.clear(); }
bool rm8TurnQueueHas(int k, int cmd, bool headOnly)
{
	if(!okSeat(k)) return false;
	for(const Role::Msg &m : g_role[k].queue){ if(m.cmd == cmd) return true; if(headOnly) return false; }
	return false;
}
void rm8TurnSetState(int k, int st){ if(okSeat(k)) g_role[k].state = st; }
int rm8TurnState(int k){ return okSeat(k) ? g_role[k].state : -1; }
int rm8TurnDiceMax(int k){ return okSeat(k) ? g_role[k].diceMax : 1; }
void rm8TurnSetDiceMax(int k, int n){ if(okSeat(k)) diceSetMax(g_role[k], n); }	// [V] start vehicle -> +0x22C (Role_InitState @0x480d30) and Role_SetTrfTool @0x48a0c0: Game_SetPlayerCount_0047d930 (count = max, all slots on)
int rm8TurnDiceCount(int k){ return okSeat(k) ? g_role[k].diceCount : 1; }
bool rm8TurnDiceSlotOn(int k, int slot){ return okSeat(k) && slot >= 1 && slot <= 3 && g_role[k].diceOn[slot - 1]; }
// [V] Ui_ToggleDiceSlotOnSelect @0x4e8410 (widgets 5 / 6 of go.ui, slot = id - 3): only for the current role in state 6; Role_ToggleDiceSlot @0x48a1c0 flips the flag of slot 2 / 3 (Game_SetDiceValue_0047dac0, which also recounts)
bool rm8TurnToggleDiceSlot(int k, int slot)
{
	if(!okSeat(k) || k != g_cur) return false;
	Role &r = g_role[k];
	if(r.state != 6) return false;
	if(!diceSetSlot(r, slot, !r.diceOn[std::max(0, std::min(2, slot - 1))])) return true;
	if(getenv("RM8_DEBUG_DICE")) rm8Log("dice: seat %d slot %d toggled -> %s, %d dice (max %d)", k, slot, r.diceOn[slot - 1] ? "on" : "off", r.diceCount, r.diceMax);
	return true;
}
void rm8TurnPlayAnim(int k, int anim, bool once)
{
	if(!okSeat(k)) return;
	playAnim(g_role[k], anim, once);
	if(anim > 3 && g_role[k].def->id == 13){ if(Follower *fo = followerOf(k)) followerAnim(*fo, anim, once); }	// [V] Cls_5e27d8__vf5_SendCommand @0x47e300: the monster mirrors the expressions
}
// [V] Cls_5e27d8__vf36_InitCharacterPath @0x47e3b0 (the game-start debut, message 0x57): the companion starts 300 units to the camera's right and walks in to its place (Path_SetWaypoint @0x47fba0)
void rm8TurnFollowerDebut(int k)
{
	if(!okSeat(k)) return;
	Follower *fo = followerOf(k);
	if(!fo || fo->bodyguard) return;
	V3d F, R; camAxes(F, R);
	Role &role = g_role[k];
	followerInit(*fo);
	fo->wp = fo->r.pos;
	fo->r.pos = {role.pos.x + R.x * 300.0f, role.pos.y + R.y * 300.0f, role.pos.z + R.z * 300.0f};
	fo->wpMode = true; fo->vis = true; fo->hidden = false; fo->walk = false; fo->route.clear();
	calcAngle(fo->r, nodePos(fo->r.from), nodePos(fo->r.to), false);
}
void rm8TurnFocus(const float p[3]){ setFocus({p[0], p[1], p[2]}); }
bool rm8TurnEventDieStart(int k){ if(!okSeat(k) || g_role[k].state != 9) return false; startEventDie(g_role[k]); return true; }	// [V] Role_StartDiceSceneCount1 @0x484d70 (state 9 only, Hotel_CheckVisitor)
int rm8TurnEventDieTake(int k){ if(!okSeat(k) || g_evDie <= 0) return 0; int v = g_evDie; g_evDie = 0; return v; }
bool rm8TurnEventDieBusy(int k){ return okSeat(k) && g_role[k].state == 4; }
void rm8TurnSkip(int k){ if(okSeat(k) && k == g_cur){ g_role[k].state = 0; turnNextRole(); } }
// [V] Role_TryStopMoveUntilDone @0x4986b0 -> Role_StopMoveAndIdle @0x484790 (scene vf13 @0x4a3060, docs/skip.md): the FIRST character, in seat order,
// that waits (state 5; the original also 0xD / 0xE = movie clip waits, here the bankruptcy cutscene) ends its wait now: timer 0, its voice and effect
// stopped (Anim_StopSample @0x47b030 / Anim_ReleaseSprite @0x47b240), state 8 - the next queued message (e.g. 0x1C hide the box) runs next frame.
// Any seat, human or computer. Returns the seat (timer left in *left) or -1.
int rm8TurnSkipRequest(float *left)
{
	if(g_placedOnly) return -1;
	for(Role &r : g_role){
		if(r.state == 5){
			if(left) *left = std::max(0.0f, r.waitT);
			r.waitT = 0;
			rm8PresentSkipSeat(r.seat);
			r.state = 8; endExpression(r);
			return r.seat;
		}
		if(rm8BoardFxGhostSkip(r.seat)){ if(left) *left = 0; return r.seat; }	// [V] state 0xE: the same function ends the bankruptcy movie wait
		if(rm8StoryWinSkip(r.seat)){ if(left) *left = 0; return r.seat; }	// [V] state 0xD: the story win scene (story.cpp)
	}
	return -1;
}
const char *rm8TurnName(int k){ return okSeat(k) ? g_role[k].def->name.c_str() : ""; }
const char *rm8TurnPic(int k){ return okSeat(k) ? g_role[k].def->pic.c_str() : ""; }
int rm8TurnOutfit(int k){ return okSeat(k) ? g_role[k].color : 0; }
bool rm8TurnRoleLook(int k, float pos[3], float off[3], float scale[3], int &outfit)
{
	if(!okSeat(k)) return false;
	const Role &r = g_role[k];
	pos[0] = r.pos.x; pos[1] = r.pos.y; pos[2] = r.pos.z;
	for(int i = 0; i < 3; i++){ off[i] = r.def->offset[i]; scale[i] = r.def->scale[i]; }
	outfit = r.color;
	return true;
}
void rm8TurnModalResult(int k, int value)
{
	if(!okSeat(k) || g_role[k].state != 7) return;
	for(auto &m : g_role[k].queue) if((unsigned)m.b == 0x80000000u){ m.b = value; break; }
	g_role[k].state = 8;
}

// ---- road / placement access for the cards (cards.cpp)
int rm8TurnNodeTo(int k){ return okSeat(k) ? g_role[k].to : -1; }
int rm8TurnLinks(int node, int *out, int max)
{
	if(node < 0 || node >= (int)g_graph.links.size()) return 0;
	int n = 0;
	for(int q : g_graph.links[node]){ if(n >= max) break; out[n++] = q; }
	return n;
}
bool rm8TurnNodePos(int node, float p[3])
{
	if(node < 0 || node >= (int)g_nodes.size()) return false;
	V3d v = nodePos(node); p[0] = v.x; p[1] = v.y; p[2] = v.z;
	return true;
}
// [V] vf16 Path_ComputeDistance_0047b5f0: to = Path_SelectRandomNext(to, from) (a neighbour of `from` other than the old target), face it
void rm8TurnTurnAround(int k)
{
	if(!okSeat(k)) return;
	Role &r = g_role[k];
	if(r.from < 0 || r.to < 0) return;
	int old = r.to;
	r.to = selectNext(r.to, r.from);
	calcAngle(r, nodePos(r.from), nodePos(r.to), true);
	rm8SndPlay(0xb);	// [V] @0x47b5f0: CAudioMgr_PlaySample(0xb, ..., volume 1.0, loops 1)
	if(Follower *fo = followerOf(r.seat)){	// [V] Cls_5e27d8_OnAngleChange @0x47de40: the new target is the node the companion comes from -> the old target is its retreat node
		if(getenv("RM8_DEBUG_FOLLOW")) rm8Log("turn around: seat %d node %d target %d -> %d, companion comes from %d (%s)", r.seat, r.from, old, r.to, fo->r.from, old != r.to && r.to == fo->r.from ? "retreat" : "no retreat");
		if(old != r.to && r.to == fo->r.from) fo->retreat = old;
	}
}
// [V] vf15 Path_SetPoints @0x47b670 + Game_MovePlayerToPosition (OnCheckBankruptcy @0x48e920): leave the old node, stand on `node`
void rm8TurnPlaceAt(int k, int node, int to)
{
	if(!okSeat(k) || node < 0 || node >= (int)g_nodes.size()) return;
	Role &r = g_role[k];
	{ float far[3] = {0, 100, 0}; rm8BoardFxIconDepart(r.seat, r.from, far); }	// [V] OnCheckBankruptcy @0x48E920: Article_PlayDepartEffect(seat, (0,100,0))
	updateRoleDirection(r, r.from, {1e9f, 0, 1e9f});
	r.from = node; r.to = to >= 0 ? to : node; r.pos = nodePos(node); r.steps = 0;
	if(r.to != r.from) calcAngle(r, nodePos(r.from), nodePos(r.to), false);
	if(Follower *fo = followerOf(r.seat)){ followerLink(*fo, r.from, r.to); followerSettle(*fo); }	// [V] Cls_5e27d8_OnPathSelect @0x47dd60 (vf15): Path_LinkPoints
	fxApproach(r, node, false);
	moveRoleToPosition(r, node, r.pos, true);
	setFocus(r.pos);
}
// [V] CPlayerTag__OnPositionSwap @0x490c60: each role takes the other's (from, to)
void rm8TurnSwapPlaces(int a, int b)
{
	if(!okSeat(a) || !okSeat(b) || a == b) return;
	int fa = g_role[a].from, ta = g_role[a].to, fb = g_role[b].from, tb = g_role[b].to;
	rm8TurnPlaceAt(a, fb, tb); rm8TurnPlaceAt(b, fa, ta);
}

// [V] Role_SetTrfTool @0x48a0c0 (Role.ChgTrfTool): another vehicle (1 walk, 2 bike, 3 car): the dice count, and when not possessed the model set
// (roleinfo artId / artId2 / artId3, Act21 = bike, Act15 = car for character 1) and the walking speed of that vehicle
void rm8TurnSetVehicle(int k, int t)
{
	if(!okSeat(k) || !g_scene) return;
	Role &r = g_role[k];
	t = std::max(1, std::min(3, t));
	if(t == r.diceMax) return;
	rm8TurnSetDiceMax(k, t);
	if(rm8GameShape(k) != 0) return;
	Rm8Scene &s = *g_scene;
	int set = t - 1;
	for(int j = 0; j < 4; j++){
		Model nm;
		if(!loadModel(s, nm, r.def->art[set * 4 + j], r.def->id, r.color, j < 2)){ rm8Log("roles: cannot load vehicle model %d", r.def->art[set * 4 + j]); continue; }
		Model &om = r.m[j];
		if(om.clump){ s.world->removeClump(om.clump); om.clump->destroy(); }
		om = nm;
	}
	r.speed = r.def->speed[set];
	r.face.clear();
	playAnim(r, r.anim, r.once);
	updateAnim(r, 0); placeModel(r);
}

// [V] Path_SelectRandomNext @0x47b8e0 for the robot doll of npc_act.cpp (the same road rules as the characters)
int rm8TurnSelectNext(int prev, int cur){ return cur >= 0 && cur < (int)g_graph.links.size() ? selectNext(prev, cur) : prev; }

// [V] Role_SetPossession @0x48b6e0: possessed -> speed[0]; released -> Role_SetTrfTool's model set and speed of the current vehicle
void rm8TurnPossessSync(int k, bool possessed)
{
	if(!okSeat(k) || !g_scene) return;
	Role &r = g_role[k];
	if(possessed){ r.speed = r.def->speed[0]; return; }
	int set = std::max(0, std::min(2, r.diceMax - 1));
	Rm8Scene &s = *g_scene;
	if(set > 0){
		for(int j = 0; j < 4; j++){
			Model nm;
			if(!loadModel(s, nm, r.def->art[set * 4 + j], r.def->id, r.color, j < 2)) continue;
			Model &om = r.m[j];
			if(om.clump){ s.world->removeClump(om.clump); om.clump->destroy(); }
			om = nm;
		}
		r.face.clear();
		playAnim(r, r.anim, r.once);
		updateAnim(r, 0); placeModel(r);
	}
	r.speed = r.def->speed[set];
}

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_roles.inc"
