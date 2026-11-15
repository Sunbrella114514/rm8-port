// Menu scene animations, see menu_anim.h and docs/menus.md section 7. [V] = verified in the decompile (address), [A] = port adaptation.
//
// The original menu scene (u_CApp_SwitchToGameScene @0x405650, vtable 0x5E0048, CGameScene__vf16_Init @0x509d70) owns four animated layers; every menu object
// forwards to them:
//  * rings: Cls_5e8270 (DAT_0064b648), three copies of picture 152 turning about Z (10 / -20 / 30 deg/s), updated by the scene (vf1 @0x50a180) and drawn from the
//    post-draw callback of widget 100 (0x508db0 -> scene vf17 @0x50a130 -> Cls_5e8270 vf2 @0x50cdb0), i.e. right after that widget, below the buttons;
//  * glow pictures: the white outline pictures laid over the background hexagons (Sprite_CreateMenuItemPicture @0x5073c0 in every UI vf12), alpha swinging
//    0..80 (Cls_5e7588 vf1 @0x507230), drawn in the same callback before the rings;
//  * pulsing circles (mainmenu / single / mult only): Cls_5e7bf8 @0x50b1f0, four copies of picture 458 at widgets 16..13, alpha 0 -> 200 -> 0 one after the
//    other every 2 s;
//  * floating bubbles (DAT_0064b1d4, Cls_5e8230 @0x50ba60) and flying characters (DAT_0064b1d0, Cls_5e8638 @0x50c000), mainmenu / single / mult only, drawn after
//    the UI (scene vf2(1) @0x50a1f0).
// The role menu draws the 3D character of every seat (Role_CreateMeshObject @0x505410, Cls_5e8140_RenderScene @0x505270) after the UI too.
#include "menu_anim.h"
#include "ui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <ctime>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace rw;

namespace {

// ---- model tables (scenes/roles.txt + npcs.txt: model / anim / face lines, the role records)
struct FaceKey { float t; std::string part, tex; };
struct ArtDef { std::string dff, texdir; std::map<int, std::string> anims; std::map<int, std::vector<FaceKey>> faces; };
std::map<int, ArtDef> g_arts;
std::map<int, int> g_roleArt;	// role id -> art id of model 0 (front left): the roleinfo +0x308 of Cls_5e8140_LoadAndDisplayRoles @0x505fb0
std::string g_root;
bool g_tables = false;

void loadTables()
{
	if(g_tables) return;
	g_tables = true;
	for(const char *fn : {"roles.txt", "npcs.txt"}){
		std::ifstream f(g_root + "/" + fn); std::string line;
		while(std::getline(f, line)){
			std::istringstream ss(line); std::string kw; ss >> kw;
			if(kw == "model"){ int a; ss >> a; ss >> g_arts[a].dff >> g_arts[a].texdir; }
			else if(kw == "anim"){ int a, i; std::string p; ss >> a >> i >> p; g_arts[a].anims[i] = p; }
			else if(kw == "face"){ int a, i; FaceKey k; ss >> a >> i >> k.t >> k.part >> k.tex; g_arts[a].faces[i].push_back(k); }
			else if(kw == "role"){ std::vector<std::string> tk; std::string w; std::istringstream s2(line); while(s2 >> w) tk.push_back(w); if(tk.size() > 11) g_roleArt[atoi(tk[1].c_str())] = atoi(tk[11].c_str()); }
		}
	}
	rm8Log("menu anim: %d models, %d roles", (int)g_arts.size(), (int)g_roleArt.size());
}

unsigned g_seed = 12345;
int rnd(){ g_seed = g_seed * 214013u + 2531011u; return (g_seed >> 16) & 0x7fff; }	// MSVC rand(): the original draws its random numbers with it

// ---- a paper-doll model (the characters are textured quads on an animated skeleton, roles.cpp)
std::map<std::string, Texture*> g_texCache;
Texture *tex(const std::string &key)
{
	auto it = g_texCache.find(key);
	if(it != g_texCache.end()) return it->second;
	Texture *t = rm8PackTexture(key);
	if(!t) rm8Log("menu anim: texture %s missing", key.c_str());
	return g_texCache[key] = t;
}

struct Doll {
	int art = 0;
	Clump *clump = nullptr;
	HAnimHierarchy *hier = nullptr;
	Matrix base;
	V3d p0 = {0, 0, 0};
	std::map<int, Animation*> anims;
	std::map<std::string, std::vector<Material*>> parts;
	int anim = -1;
	std::string face;
};

void setPart(Doll &d, const std::string &part, Texture *t)
{
	if(!t) return;
	auto it = d.parts.find(part);
	if(it == d.parts.end()) return;
	for(Material *m : it->second) m->setTexture(t);
}

// [V] role.bin sub-list 3: the "face" texture of an animation changes at given times (roles.cpp applyFace)
void applyFace(Doll &d, float t)
{
	auto ad = g_arts.find(d.art);
	if(ad == g_arts.end()) return;
	auto it = ad->second.faces.find(d.anim);
	if(it == ad->second.faces.end()) return;
	const FaceKey *k = nullptr;
	for(const FaceKey &f : it->second) if(f.t <= t + 1e-4f && (!k || f.t >= k->t)) k = &f;
	if(!k || k->tex == d.face) return;
	setPart(d, k->part, tex(k->tex));
	d.face = k->tex;
}

// outfit >= 0: the body colour texture of the seat (Asset_LoadCharacterTexture @0x4934a0)
Doll *loadDoll(int art, int charId, int outfit)
{
	loadTables();
	auto it = g_arts.find(art);
	if(it == g_arts.end() || it->second.dff.empty()){ rm8Log("menu anim: no model for art %d", art); return nullptr; }
	Clump *c = rm8LoadClump(it->second.dff, it->second.texdir);
	if(!c){ rm8Log("menu anim: cannot load %s", it->second.dff.c_str()); return nullptr; }
	Doll *d = new Doll;
	d->art = art; d->clump = c;
	d->base = c->getFrame()->matrix; d->base.flags = 0; d->p0 = d->base.pos;
	d->hier = HAnimHierarchy::find(c->getFrame());
	if(d->hier) d->hier->attach();
	for(auto &a : it->second.anims){
		Animation *an = rm8LoadAnim(a.second);
		if(an && d->hier && an->getNumNodes() == d->hier->numNodes) d->anims[a.first] = an;
	}
	FORLIST(lnk, c->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		for(int i = 0; i < g->matList.numMaterials; i++){
			Material *mat = g->matList.materials[i];
			int k = UserDataArray::materialFindIndex(mat, "name");
			if(k < 0) continue;
			UserDataArray *ua = UserDataArray::materialGet(mat, k);
			if(ua && ua->datatype == USERDATASTRING && ua->numElements > 0 && ua->getString(0)) d->parts[ua->getString(0)].push_back(mat);
		}
	}
	if(outfit >= 0 && charId > 0){
		char key[160];
		snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_body_f_%02d.tga", charId, charId, outfit);
		setPart(*d, "body", tex(key));
		const char *extra = (charId == 2 || charId == 15) ? "other" : (charId == 4 || charId == 9 || charId == 11 || charId == 13) ? "hair" : (charId == 7 || charId == 10) ? "toujin" : nullptr;
		if(extra){ snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_%s_f_%02d.tga", charId, charId, extra, outfit); setPart(*d, extra, tex(key)); }
	}
	return d;
}

// [V] Mesh_SetTexture @0x47a570 -> Article_SelectByTypeAndSetProperties(anim, 0, 0): the animation loops
void playDoll(Doll &d, int anim)
{
	d.anim = anim; d.face.clear();
	auto it = d.anims.find(anim);
	if(!d.hier || it == d.anims.end() || !it->second) return;
	d.hier->interpolator->setCurrentAnim(it->second);
	applyFace(d, 0);
}

// [V] Mesh_InitWindow @0x47a3e0 (called with the frame time): advance the animation
void updateDoll(Doll &d, float dt)
{
	if(!d.hier || !d.hier->interpolator->currentAnim) return;
	AnimInterpolator *ip = d.hier->interpolator;
	ip->addTime(dt);
	for(int i = 0; i < d.hier->numNodes; i++){
		Frame *f = d.hier->nodeInfo[i].frame;
		if(!f) continue;
		ip->applyCB(&f->matrix, ip->getInterpFrame(i));
		f->updateObjects();
	}
	d.hier->updateMatrices();
	applyFace(d, ip->currentTime);
}

// [V] Mesh_SetScale / Mesh_Rotate @0x47a790 (180 degrees at creation, Role_CreateMeshObject @0x505410 and CGameScene_LoadWonders @0x50bda0) / Mesh_SetTranslation
void placeDoll(Doll &d, V3d pos, V3d sc)
{
	Frame *f = d.clump->getFrame();
	V3d np0 = {-d.p0.x, -d.p0.y, -d.p0.z}, ay = {0, 1, 0}, p1 = {d.p0.x + pos.x, d.p0.y + pos.y, d.p0.z + pos.z};
	f->transform(&d.base, COMBINEREPLACE);
	f->translate(&np0, COMBINEPOSTCONCAT); f->scale(&sc, COMBINEPOSTCONCAT);
	f->rotate(&ay, 180.0f, COMBINEPOSTCONCAT); f->translate(&p1, COMBINEPOSTCONCAT);
}

// ---- per-screen configuration: widget 100 / 101 (41 / 42 in the role menu) get their glow picture, the post-draw callback of widget 100 fixes the draw order
struct UiCfg { const char *name; int widx; int ids[2]; int pics[2]; bool full; };	// widx: Index of widget 100 in the .ui; full: circles, bubbles and flying characters
const UiCfg UICFG[] = {
	{"mainmenu", 2, {100, 101}, {262, 263}, true},		// [V] CUi_mainmenu vf12 @0x5084c0: widgets 100 / 101, pictures 0x106 / 0x107
	{"single", 2, {100, 101}, {262, 263}, true},		// [V] CUi_single_vf12 @0x50d630 (single and mult)
	{"mult", 2, {100, 101}, {262, 263}, true},
	{"chsMapMenu", 1, {100, 101}, {988, 989}, false},	// [V] default UI vf12 @0x5093d0: pictures 0x3dc / 0x3dd (map, picmenu, finalMenu)
	{"chsRoleMenu", 2, {41, 42}, {923, 924}, false},	// [V] CUi_chsRoleMenu vf12 @0x505700: widgets 0x29 / 0x2a, pictures 0x39b / 0x39c
	{"optionMenu", 2, {100, 101}, {1028, 1029}, false},	// [V] @0x50a8e0: 0x404 / 0x405
	{"ltgameMenu", 2, {100, 101}, {1023, 1024}, false},	// [V] @0x508160: 0x3ff / 0x400
	{"slmenu", 2, {100, 101}, {1003, 1002}, false},		// [V] @0x50ceb0: 0x3eb - i
	{"picmenu", 3, {100, 101}, {988, 989}, false},
	{"boxmenu", 3, {100, 101}, {1026, 1027}, false},	// [V] @0x501c30: 0x402 / 0x403
	{"finalMenu", 2, {100, 101}, {988, 989}, false},
};

struct Deco { int ring[3] = {0, 0, 0}, glow[2] = {0, 0}, circ[4] = {0, 0, 0, 0}; };
std::map<Rm8Ui*, Deco> g_deco;
const UiCfg *g_cfg = nullptr;
Rm8Ui *g_ui = nullptr;
Deco *g_cur = nullptr;

// ---- rings [V] CGameScene_InitSprites @0x50cb00 / Cls_5e8270_vf1 @0x50ccb0: picture 0x98 at (0,-5) (480,300) (660,-150) scaled 1.2 / 0.8 / 0.5 about its centre
const float RING_POS[3][2] = {{0, -5}, {480, 300}, {660, -150}}, RING_SCALE[3] = {1.2f, 0.8f, 0.5f}, RING_SPEED[3] = {10, -20, 30};	// [V] tables 0x5e8680 / 0x5e8698 / 0x5e86b0
float g_ringAng[3] = {0, 0, 0};

// ---- glow [V] Cls_5e7588 @0x501d10 / vf1 @0x507230: v starts at 255 going down; v += ftol(dt * 100 * dir); reflected at 0x50 (down) and 0 (up). [A] 60 units / s: the original steps
// a whole number per frame (its frame limiter keeps the frame time at ~15 ms)
float g_gv = 255, g_gdir = -1;
const float GLOW_RATE = 60.0f;

// ---- circles [V] Cls_5e7bf8 vf1 @0x50b1f0
struct Circles { int state = 0; float timer = 0, v[4] = {0, 0, 0, 0}, dir[4] = {1, 1, 1, 1}; } g_circ;

// ---- bubbles [V] Particle_InitRandom @0x50b420 / Sprite_UpdateMovement @0x50b680 / Cls_5e8230_vf1 @0x50ba60 (design units 1024 x 768)
struct Bubble { float x0, y0, cx, cy, t, tn, tx, ty, vx, vy, scale; };
std::vector<Bubble> g_bub;
float g_btimer = 1.0f;	// [V] _DAT_00604c0c: starts at 1.0 in the exe image
void randVec(float &x, float &y){ x = (float)(rnd() % 300) + 10.0f; y = (float)(rnd() % 300) + 10.0f; }	// [V] Unk_RandomVectorXY @0x50b8f0 (scale = width / 1024 = 1)

void bubSpawn()
{
	Bubble b;
	if((rnd() & 1) == 0){ b.x0 = (float)(rnd() % 400); b.y0 = (float)(-rnd() % 100); }
	else { b.x0 = (float)(-rnd() % 100); b.y0 = (float)(rnd() % 300); }
	b.cx = b.x0; b.cy = b.y0; b.t = 0;
	b.scale = (float)(rnd() % 0x5a + 0x1e) / 100.0f;
	b.tn = (float)(rnd() % 0x1e + 10) / 60.0f;
	randVec(b.tx, b.ty); randVec(b.vx, b.vy);
	g_bub.push_back(b);
}

void bubUpdate(float dt)
{
	size_t n = g_bub.size();
	for(size_t i = 0; i < g_bub.size();){
		Bubble &b = g_bub[i];
		b.cx += dt * b.vx; b.cy += dt * b.vy;
		auto chase = [&](float target, float v){
			float d = target - v;
			return d <= 0 ? std::max(d, -300.0f * dt) : std::min(d, 300.0f * dt);
		};
		float sx = chase(b.tx, b.vx), sy = chase(b.ty, b.vy);
		b.vx += sx; b.vy += sy;
		b.t += dt;
		if(b.t >= b.tn){ b.tn = (float)(rnd() % 0x3c + 0x1e) / 60.0f; randVec(b.tx, b.ty); }	// [V] +0x14 is never reset: after the first interval a new target comes every frame
		if(b.cx < 1024 + 50 && b.cy < 768 + 50) i++; else g_bub.erase(g_bub.begin() + i);
	}
	g_btimer += dt;
	if(g_btimer >= 0.2f && n < 16){ g_btimer = 0; bubSpawn(); }
}

// ---- flying characters [V] CGameScene_LoadWonders @0x50bda0 (table 0x5e8450: art, animation, mode, scale, speed), Cls_5e8638 vf1 @0x50c000
struct FlyDef { int art, anim, mode; float scale; };
const FlyDef FLY[21] = {
	{7, 1, 0, 1.5f}, {0x17, 1, 0, 1.5f}, {0x1b, 0, 0, 1.5f}, {0x27, 0, 0, 1.5f}, {0x33, 0, 0, 1.5f}, {0x47, 1, 0, 1.5f}, {0x4b, 0, 0, 1.5f}, {0x5b, 1, 0, 1.5f},
	{0x6b, 1, 0, 1.5f}, {0x6f, 0, 0, 1.5f}, {0x7f, 1, 0, 1.5f}, {0x87, 0, 0, 1.5f}, {0x9b, 1, 0, 1.5f}, {0xa3, 1, 0, 1.5f}, {0xab, 0, 0, 1.5f}, {0xbb, 1, 0, 1.5f},	// the 16 roles
	{0x1f5, 0, 1, 1.1f}, {0x1f7, 0, 1, 1.1f}, {0x1fd, 0, 0, 1.2f}, {0x201, 0, 0, 1.1f}, {0x205, 0, 0, 1.1f},	// the gods (mode 1: after x = 140 they rise)
};
const float FLY_SPEED = 50.0f, FLY_START[3] = {-280, -170, 800};	// [V] Particle_ActivateRandomIdle @0x50c530 sets (-280, -170, 800); the speed is the table's 0x42480000
struct Flyer { int t; V3d pos; float a, b; };
std::vector<Doll*> g_flyDoll(21, nullptr);
std::vector<bool> g_flyTried(21, false);
std::vector<Flyer> g_fly;
std::vector<int> g_flyRecent;
float g_flyTimer = 1.0f, g_flySign = 1.0f, g_flyThr = 0;	// [V] +0x34 starts at 1.0 (Particle_ResetAll), _DAT_00604c10 = 1.0, _DAT_0064c450 = rand % 15 + 5
bool g_flyOn = false;

Doll *flyDoll(int t)
{
	if(!g_flyTried[t]){
		g_flyTried[t] = true;
		g_flyDoll[t] = loadDoll(FLY[t].art, 0, -1);
		if(g_flyDoll[t]) playDoll(*g_flyDoll[t], FLY[t].anim);
	}
	return g_flyDoll[t];
}

void flyReset(){ g_fly.clear(); g_flyRecent.clear(); g_flyTimer = 1.0f; g_flyThr = (float)(rnd() % 15 + 5); }	// [V] Particle_ResetAll @0x50c930

void flyActivate()	// [V] Particle_ActivateRandomIdle @0x50c530: a random one that is neither flying nor among the last six finished
{
	std::vector<int> cand;
	for(int t = 0; t < 21; t++){
		bool busy = false;
		for(const Flyer &f : g_fly) if(f.t == t) busy = true;
		for(int r : g_flyRecent) if(r == t) busy = true;
		if(!busy) cand.push_back(t);
	}
	if(cand.empty()) return;
	int t = cand[rnd() % cand.size()];
	if(!flyDoll(t)) return;
	Flyer f; f.t = t; f.pos = {FLY_START[0], FLY_START[1], FLY_START[2]}; f.a = f.b = 0;
	g_fly.push_back(f);
}

void flyUpdate(float dt)
{
	for(size_t i = 0; i < g_fly.size();){
		Flyer &f = g_fly[i];
		const FlyDef &def = FLY[f.t];
		if(Doll *d = g_flyDoll[f.t]) updateDoll(*d, dt);
		bool keep;
		if(def.mode == 0 || f.pos.x < 140.0f){	// across the screen towards the left (the camera's x axis points left)
			f.pos.x += dt * FLY_SPEED;
			keep = f.pos.x <= -FLY_START[0];
		}else{	// the gods: after x = 140 they float up with a little sway
			float st = dt * 20.0f;
			if(f.a >= 20.0f){
				st *= g_flySign;
				f.b += st;
				if(g_flySign <= 0){ if(f.b < g_flyThr){ g_flySign = 1.0f; g_flyThr = (float)(rnd() % 15 + 5); } }
				else if(g_flyThr < f.b){ g_flySign = -1.0f; g_flyThr = -(float)(rnd() % 15 + 5); }
			}else f.a += st;
			f.pos.x += st;
			f.pos.y += dt * 70.0f;
			keep = f.pos.y <= 100.0f - FLY_START[1];
		}
		if(Doll *d = g_flyDoll[f.t]) placeDoll(*d, f.pos, {def.scale, def.scale, 1.0f});
		if(keep){ i++; continue; }
		// [V] ObjectPool_RecycleItem @0x50c870: into the "recent" list (at most six)
		if(g_flyRecent.size() > 5) g_flyRecent.erase(g_flyRecent.begin());
		g_flyRecent.push_back(f.t);
		g_fly.erase(g_fly.begin() + i);
	}
	g_flyTimer -= dt;
	if(g_flyTimer <= 0){ g_flyTimer = 3.0f; flyActivate(); }
	// [A] the original creates all 21 models with the scene; here one more per frame while the menu runs (no loading hitch)
	for(int t = 0; t < 21; t++) if(!g_flyTried[t]){ flyDoll(t); break; }
}

// ---- role menu seats [V] Role_CreateMeshObject @0x505410: model 0 of the role, animation 2 (stand), turned 180 degrees, at DAT_005e77c8[seat]
const float SEAT_POS[4][3] = {{116, -70, 500}, {78, -90, 500}, {36, -70, 500}, {-6, -90, 500}};
std::map<std::pair<int, int>, Doll*> g_seatCache;
std::vector<Doll*> g_seatDoll;
bool g_seatOk = true;

// ---- 3D pass: an identity camera with the board camera's view window (0.3 x 0.225 for 4:3) [V] Camera_SaveViewState @0x41b2e0 resets the camera frame in the menus
void render3D(Camera *cam, const std::vector<Doll*> &dolls)
{
	if(dolls.empty()) return;
	float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height, Hb = H * 4.0f / 3.0f;
	cam->endUpdate();
	Matrix savedFrame = cam->getFrame()->matrix; V2d savedVw = cam->viewWindow; float savedNear = cam->nearPlane, savedFar = cam->farPlane;
	V2d vw = {0.3f * W / Hb, 0.225f};	// [A] the 4:3 area of the UI keeps the original's proportions on wide screens
	cam->setViewWindow(&vw); cam->setNearPlane(10.0f); cam->setFarPlane(5000.0f);
	Matrix id; id.setIdentity(); cam->getFrame()->transform(&id, COMBINEREPLACE);
	RGBA none = {0, 0, 0, 0};
	cam->clear(&none, Camera::CLEARZ);	// [V] RwCameraClear(z) before the meshes (@0x50c440, @0x505270)
	cam->beginUpdate();
	uint32 fn = GetRenderState(ALPHATESTFUNC), ref = GetRenderState(ALPHATESTREF), zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE);
	SetRenderState(ZTESTENABLE, 1); SetRenderState(ZWRITEENABLE, 1);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196);	// [V] states 0x1d = 7, 0x1e = 0xc4
	for(Doll *d : dolls) d->clump->render();
	SetRenderState(ALPHATESTFUNC, fn); SetRenderState(ALPHATESTREF, ref); SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw);
	cam->endUpdate();
	cam->getFrame()->transform(&savedFrame, COMBINEREPLACE);
	cam->setViewWindow(&savedVw); cam->setNearPlane(savedNear); cam->setFarPlane(savedFar);
	cam->beginUpdate();
}

}	// namespace

void rm8MenuAnimInit(const std::string &scenesRoot){ g_root = scenesRoot; g_tables = false; }

void rm8MenuAnimReset()
{
	for(float &a : g_ringAng) a = 0;
	g_gv = 255; g_gdir = -1;
	g_bub.clear(); g_btimer = 1.0f;
	g_seed = (unsigned)time(nullptr) * 2654435761u;
	flyReset();
}

void rm8MenuAnimLeave()
{
	flyReset();	// [V] the closing menu object calls Particle_ResetAll (mainmenu @0x508a20, single / mult @0x50da60)
	g_seatDoll.clear();
	g_cfg = nullptr; g_cur = nullptr; g_ui = nullptr;
}

void rm8MenuAnimEnter(Rm8Ui *u)
{
	g_cfg = nullptr; g_cur = nullptr; g_ui = u;
	if(!u) return;
	const std::string &name = rm8UiName(u);
	for(const UiCfg &c : UICFG) if(name == c.name) g_cfg = &c;
	if(!g_cfg) return;
	Deco &d = g_deco[u];
	if(!d.ring[0]){
		// the draw position of widget 100's post-draw callback: just above that widget's Index. Dynamic widgets with the same Index lie below the .ui's own ones, and the ones
		// created first lie above the later ones (ui.cpp sort), so: rings first (drawn after the glow pictures), then the glow pictures
		int idx = g_cfg->widx + 1;
		for(int i = 0; i < 3; i++){
			float s = RING_SCALE[i], w = 512 * s;
			d.ring[i] = rm8UiAddWidget(u, "plane", 0, RING_POS[i][0] + 256 * (1 - s), RING_POS[i][1] + 256 * (1 - s), w, w, 152, 0, 16, idx);
			rm8UiSetIgnore(u, d.ring[i], true);	// decoration: never takes the pointer (docs/menus.md)
		}
		for(int i = 0; i < 2; i++){
			float x, y, w, h, pw, ph;
			if(!rm8UiGetRect(u, g_cfg->ids[i], x, y, w, h) || !rm8UiPicSize(g_cfg->pics[i], pw, ph)) continue;	// [V] Sprite_CreateMenuItemPicture: the widget's position, the picture's size
			d.glow[i] = rm8UiAddWidget(u, "plane", 0, x, y, pw, ph, g_cfg->pics[i], 0, 16, idx);
			rm8UiSetIgnore(u, d.glow[i], true); rm8UiSetTint(u, d.glow[i], 0x00ffffff);
		}
		if(g_cfg->full) for(int c = 0; c < 4; c++){
			float x, y, w, h;
			if(!rm8UiGetRect(u, 16 - c, x, y, w, h)) continue;	// [V] Ui_InitMenuSprites @0x50b020: widgets 16 - c, picture 0x1ca
			d.circ[c] = rm8UiAddWidget(u, "plane", 0, x, y, 64, 64, 458, 0, 16, 10);
			rm8UiSetIgnore(u, d.circ[c], true); rm8UiSetTint(u, d.circ[c], 0x00ffffff);
		}
	}
	g_cur = &d;
	g_circ = Circles();
}

void rm8MenuAnimUpdate(float dt)
{
	if(!g_cfg || !g_cur) return;
	dt = std::min(dt, 0.1f);
	// rings
	for(int i = 0; i < 3; i++){
		float &a = g_ringAng[i];
		a += dt * RING_SPEED[i];
		if(a > 360.0f) a -= 360.0f; else if(a < -360.0f) a += 360.0f;	// [V] Cls_5e8270_vf1: wrapped into (-360, 360]
		rm8UiSetRot(g_ui, g_cur->ring[i], a);
	}
	// glow
	g_gv += dt * GLOW_RATE * g_gdir;
	if(g_gv > 80.0f){ g_gv = 160.0f - g_gv; g_gdir = -1; }
	if(g_gv < 0.0f){ g_gv = -g_gv; g_gdir = 1; }
	unsigned ga = (unsigned)std::max(0.0f, std::min(255.0f, g_gv));
	for(int i = 0; i < 2; i++) if(g_cur->glow[i]) rm8UiSetTint(g_ui, g_cur->glow[i], (ga << 24) | 0xffffff);
	if(g_cfg->full){
		// pulsing circles: one after the other along the ring, 2 s apart [V] 0x50b1f0
		Circles &c = g_circ;
		if(c.state == 0){
			c.timer += dt;
			if(c.timer >= 2.0f){ c.timer = 0; c.state = 1; c.v[0] = 1.0f; c.dir[0] = 1.0f; }
		}else for(int k = 0; k < 4; k++){
			if(c.v[k] > 0.0f){
				c.v[k] += dt * 600.0f * c.dir[k];
				if(c.v[k] >= 0.0f){
					if(c.v[k] >= 200.0f){
						c.v[k] = 400.0f - c.v[k]; c.dir[k] = -1.0f;
						if(k < 3){ c.v[k + 1] = 1.0f; c.dir[k + 1] = 1.0f; }
					}
				}else{
					c.v[k] = 0; c.dir[k] = 1.0f;
					if(k == 3) c.state = 0;
				}
			}
		}
		for(int k = 0; k < 4; k++) if(g_cur->circ[k]) rm8UiSetTint(g_ui, g_cur->circ[k], ((unsigned)std::max(0.0f, std::min(255.0f, c.v[k])) << 24) | 0xffffff);
		flyUpdate(dt);
		bubUpdate(dt);
	}
	for(size_t k = 0; k < g_seatDoll.size() && k < 4; k++) if(g_seatDoll[k]){
		updateDoll(*g_seatDoll[k], dt);
		placeDoll(*g_seatDoll[k], {SEAT_POS[k][0], SEAT_POS[k][1], SEAT_POS[k][2]}, {1, 1, 1});
	}
}

void rm8MenuAnimRender(Camera *cam)
{
	if(!g_cfg || !g_cur) return;
	std::vector<Doll*> dolls;
	if(g_cfg->full) for(const Flyer &f : g_fly) if(g_flyDoll[f.t]) dolls.push_back(g_flyDoll[f.t]);
	for(size_t k = 0; k < g_seatDoll.size() && k < 4; k++) if(g_seatDoll[k]) dolls.push_back(g_seatDoll[k]);
	render3D(cam, dolls);
	float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height, S = H / 768.0f, bx = (W - 1024.0f * S) / 2;
	rm8UiOverlayBegin(cam);
	if(g_cfg->full) for(const Bubble &b : g_bub){
		float sz = 64.0f * b.scale * S;
		rm8UiOverlayPic(925, bx + (b.cx + 32.0f) * S, (b.cy + 32.0f) * S, sz, sz, 0, 0xffffffff);	// [V] top-left at (cx, cy), 64 x 64, scaled about the centre
	}
	if(bx > 0.5f){	// [A] wide screens: what leaves the 4:3 area of the menu does not show beside it
		rm8UiOverlayRect(0, 0, bx, H, 0xff000000);
		rm8UiOverlayRect(bx + 1024.0f * S, 0, W - bx - 1024.0f * S, H, 0xff000000);
	}
	rm8UiOverlayEnd();
}

bool rm8MenuAnimSeats(const std::vector<std::pair<int, int>> &seats)
{
	loadTables();
	g_seatDoll.assign(std::min(seats.size(), (size_t)4), nullptr);
	bool ok = true;
	for(size_t k = 0; k < g_seatDoll.size(); k++){
		auto key = seats[k];
		auto it = g_seatCache.find(key);
		if(it == g_seatCache.end()){
			auto ra = g_roleArt.find(key.first);
			// [V] Cls_5e8140_LoadAndDisplayRoles @0x505fb0: outfit 4 -> roleinfo +0x308 + 0xC0 (the A%02dx swimsuit model); Role_CreateMeshObject @0x505410 loads the colour texture only for outfit 0..3
			int art = ra != g_roleArt.end() ? ra->second : 0, outfit = key.second;
			if(outfit == 4){ if(g_arts.count(art + 192)){ art += 192; outfit = -1; } else outfit = 0; }
			Doll *d = ra != g_roleArt.end() ? loadDoll(art, key.first, outfit) : nullptr;
			if(d) playDoll(*d, 2);	// [V] Mesh_SetTexture(2, 0, 0): the standing loop
			it = g_seatCache.insert({key, d}).first;
		}
		g_seatDoll[k] = it->second;
		if(!it->second) ok = false;
		else placeDoll(*it->second, {SEAT_POS[k][0], SEAT_POS[k][1], SEAT_POS[k][2]}, {1, 1, 1});
	}
	return ok && g_cfg && std::string(g_cfg->name) == "chsRoleMenu";
}
