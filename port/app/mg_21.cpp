// Minigame 21 (ltlgame_21.dll): Pirate / 插海盗 - one player hides in a barrel as the pirate, the others stab wooden swords into its 12 holes in turns.
// Transliteration of the DLL on the mg:: host services.  Tags: [V] verified in the decompile / disassembly (DLL addresses), [H] hypothesis, [A] port adaptation.
//
// Map of the DLL (names from out/ghidra_lg_named/ltlgame_21.dll; the game object is 0x898 bytes, its sub-object +4 is the state machine):
//   Game_Init @10005060, GetResult @100031d0, Update @100048e0, Draw @10004c10, KeyDown @10004da0, MouseButton @100029a0, KeyUp / MouseMove = no-ops (10001d80 / 10001d90),
//   state machine: SetState @10006420 (timer jump table @10006498), Tick @100064e0, timeout handler @10002390 (jump table @10002944), NextTurn @100043e0,
//   Intro_OnConfirm @10004540, State9_SeatAnim @100042a0, animation command queue: Update @100045a0, OnArrive @10003b10, PushIntro @10004090, PushSeatMove @10004160,
//   barrel: Reset @10001360, Init @100013d0, InitTrapHole @10001000, PickRandomUnusedHole @100010c0, StepAngle @100011b0, BoxToHole @10001260, IsHoleHidden @100012a0,
//   IsFirstStab @10001310, Stab @10001950, SetDirection @10001aa0, AutoStep @10001af0, RotateAnim @10001b70, UpdateSwords @10001650, DrawFront @10001730, DrawOverlay @10001860,
//   DrawSwordsLeft @10001030, LayoutHitBoxes @10001da0, HitTest @10001ea0, DoStab @10001fa0, OnMouseClick @100022c0, PirateEmerge @10002030, Player_* @10005b20..10005cf0 (ctor),
//   HUD @10006530..10006b80 (LoadSprites @10006760), Fx timer slots @10006c10 / @10006df0, Sound_Play @10006140 (id map).
//
// Who is who (checked in the disassembly, the docs lg_21.md section 9 is wrong about this): the players are the board's role articles (paper dolls, role.bin type 0,
//   art = role model of the "br" view = iconId - 1, replaced by the "fl" / "fr" view (+1 / +2) for the pirate in the barrel / the winner), the pirate god that picks the
//   victim is art 503 (gods\d38_act01, a 3D model, not a sprite), the barrel is art 0x711, the six swords 0x712..0x717 (12 holes: swords 0..5 and the same six turned 180 degrees),
//   art 0x713 (a sword) is the "swords left" column and the two crossed swords over the pirate's HUD portrait, 0x70f / 0x70e are the green X (safe stab) / red O (the pirate was
//   hit) marks, 0x84b / 0x84c the Ready / Go banners, 0x70d / 0x710 / 0x719 / 0x718 the arrow buttons, 0x709..0x70c the background tiles, 0x71a the candle flame.
// iconId (descriptor +0x4c) = role +0x308 = (role +0xf8)+0x308 = art[0] (fl view) of the roleinfo (Role_SetTrfTool @0x491200 copies art[set * 4 + k]); roles.txt shows it is
//   12 * (charId - 1) + 3 for the roles 1..16, which is what Rm8MiniPlayer::charId gives.  Voice ids = 0x25c / 0x25d / 0x260 + (iconId / 12) * 200.  The colour / outfit
//   index (descriptor +0x50 = role +0x1ac = Rm8MiniPlayer::outfit, 4 = swimsuit, art + 0xc0 @10005d37, no colour textures) comes from the host (event_mini.cpp).
// Result structure (GetResult @100031d0, entries in the order of the host's table, seat = descriptor id): defender D wins (the swords ran out): D score (n - 1) * 30, rating 2;
//   everybody else -30.  Somebody P hits the trap: P +30 (flagB = 1 if it was the first valid stab that drew a pool hole, IsFirstStab @10001310), D -30, flagA = 1, rating 0
//   (rating 2 is written into D's entry by P's entry and stays if D's entry comes first), the others 0.  n = 2: 30 / -30; n = 3: 60 / -30 / -30 or 30 / -30 / 0;
//   n = 4: 90 / -30 x 3 or 30 / -30 / 0 / 0.
//
// Test hooks of this file (environment, all optional, they only replace random choices or press the normal OK button):
//   RM8_MG21_TRAP=n      n = 0..11: the trap hole of the barrel (DLL: rand() % 12 @10001000)
//   RM8_MG21_PIRATE=i    i = player index: the pirate (DLL: rand() % count in OnArrive @10003b10 / PushIntro @10004090)
//   RM8_MG21_HIT=n       the n-th valid stab draws the trap hole (the pirate is hit); RM8_MG21_NEVER=1 the draw never returns the trap hole (sword-exhausted ending)
//   RM8_MG21_AUTOSTART=1 the rules panel is confirmed at once (same code path as the OK button, 10004540)
//   RM8_MG21_LOG=1       log state changes, stabs and results to stderr
//   RM8_SCENES=dir       directory with roles.txt / npcs.txt (the role / god models, see "local models" below)
//   RM8_MG21_NETBOT=s    [N] netplay: the local player chooses after s seconds (a random visible hole, sometimes a barrel turn first) - through the referee as a click would
//   RM8_MG21_NETMUTE=1   [N] netplay: the local choices are never sent (a frozen player: the host's per-turn time-out RM8_NET_MINI_TURN_TIMEOUT decides)
//
// [N] N4 netplay (docs/netplay.md section 9.3): every device of a netplay game runs this game (mg::net21().on). The referee of the event task (event_mini.cpp,
//     lockstep) draws the pirate and every stab's outcome and resolves the computer / time-out choices; this file shows its actions (mg::net21Acts) in order,
//     the local player's clicks / keys / time-out only send a choice (RM8C_MINI_ACT) and are shown when the action comes back. pl[i].ai = not played on this device.
//
// [A] FPU: the DLL runs with the x87 precision of Direct3D (24 bit mantissa): all float arithmetic here is single precision, which is what makes e.g. the exact
//     compare "target == S * 400" of OnArrive @10003e99 hold.
// [A] The DLL calls srand(timeGetTime()) before most of its rand() calls (@10001000, @100010c0, @10001950, @10002210); the port draws from the one MSVC sequence of
//     mg::rand15 (seeded per game, RM8_MINI_SEED), the distribution is the same.
#include "mg_core.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
using mg::V3;

inline float fbits(uint32_t u){ float f; memcpy(&f, &u, 4); return f; }
inline int ftol(float f){ return (int)f; }	// [V] FUN_10023cf0 = __ftol (truncation)
bool g_log = false;
#define LOG21(...) do{ if(g_log){ fprintf(stderr, "[mg21 %.2f] ", mg::now()); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } }while(0)

// ---- constants of the data section
const int NHOLES = 12;				// [V] DAT_10039040
const float BARREL_X = 4.0f;			// [V] 0x10039044
const float BARREL_Y_INIT = -25.0f;		// [V] 0x10039048 as stored in the file; Barrel_Init @10001438 patches it to -26 for scale != 1 (always here)
const float BARREL_Y = -26.0f;			// [V] 0xc1d00000 (written at 10001438, every later read sees it)
const float BARREL_Z = 500.0f;			// [V] 0x1003904c
const float SW_X = -160.0f, SW_Y = 2.0f, SW_Z = 500.0f;	// [V] 0x10039050 / 54 / 58: the "swords left" column
const float SCALE_K = fbits(0x3aa3d70a);	// [V] 0x100322a0 = 0.00125 (scale = width * 0.00125)
const float CAM_AT_Z = 1.0f;			// [V] host +0x2c: the camera's view direction (0,0,1) at the identity frame; +0x28 the camera position (0,0,0)

struct Cmd {			// [V] the 0x34 byte animation command (13 dwords), see PushIntro @10004090
	V3 p;			// +0x00 world position of the moving article
	float speed = 0;	// +0x0c
	int handle = 0;		// +0x10 article moved
	float tx = 0, ty = 0;	// +0x14 / +0x18 target (screen pixels)
	float sx = 0, sy = 0;	// +0x1c / +0x20 start (screen pixels)
	float gx = 0, gy = 0;	// [V] +0x24 / +0x28: the current screen position, rewritten by every Update step (@100046ad: the second project call writes it); PushIntro leaves it unset (stack residue) but Update overwrites it before any use; OnArrive copies it into the start (@10003c5c)
	bool chained = false;	// +0x2c
	int next = 0;		// +0x30 chained command (only its handle is used: that article follows the position)
};

// =========================================================================================================================
// local models: the players are role articles (art = role model "br" view, role.bin type 0 paper dolls) and the pirate god is art 503 (gods\d38).  mg_3d only
// knows the arts of sprites.txt (1500..2200), so these are loaded here from scenes/roles.txt and scenes/npcs.txt (model / anim / face lines, the format of
// roles.cpp) with the same operations as mg_3d.cpp (post-concat translate / rotate / scale about the frame position, animation records, hold after n plays).
// Handles: > 0 = mg::md*, < 0 = local.  [A] the scenes directory is searched (RM8_SCENES, ./scenes, ../scenes ...) because mg:: does not hand it to the games.
// =========================================================================================================================
namespace lm {
using namespace rw;
struct FaceKey { float t; std::string part, tex; };
struct ArtDef { std::string dff, texdir; std::map<int, std::string> anims; std::map<int, std::vector<FaceKey>> faces; };
std::map<int, ArtDef> g_art;
bool g_loaded = false;

bool readDefs(const std::string &path)
{
	std::ifstream f(path);
	if(!f) return false;
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "model"){ int a; ss >> a; ss >> g_art[a].dff >> g_art[a].texdir; }
		else if(kw == "anim"){ int a, i; std::string p; ss >> a >> i >> p; g_art[a].anims[i] = p; }
		else if(kw == "face"){ int a, i; FaceKey k; ss >> a >> i >> k.t >> k.part >> k.tex; g_art[a].faces[i].push_back(k); }
	}
	return true;
}
void load();
bool has(int art){ load(); return g_art.count(art) > 0; }
void load()
{
	if(g_loaded) return;
	g_loaded = true;
	std::vector<std::string> dirs;
	if(!mg::scenesRoot().empty()) dirs.push_back(mg::scenesRoot());
	if(const char *e = getenv("RM8_SCENES")) dirs.push_back(e);
	for(const char *p : {"scenes", "../scenes", "port/scenes", "../port/scenes", "../../scenes"}) dirs.push_back(p);
	for(const std::string &d : dirs){
		if(readDefs(d + "/roles.txt")){ readDefs(d + "/npcs.txt"); rm8Log("minigame 21: role / god models from %s", d.c_str()); return; }
	}
	rm8Log("minigame 21: roles.txt not found (set RM8_SCENES): the players are drawn as portraits");
}

struct Mdl {
	bool used = false, fallback = false;
	int art = 0;
	Clump *clump = nullptr;
	HAnimHierarchy *hier = nullptr;
	std::map<int, Animation *> anims;
	std::map<std::string, std::vector<Material *>> parts;
	V3d lastPos = {0, 0, 0};
	int times = 0, left = 0, anim = 0;
	bool holding = false;
	std::string face;
};
std::vector<Mdl> g_m(1);
World *g_world = nullptr;
std::map<std::string, Texture *> g_tex;

Texture *tex(const std::string &key)
{
	auto it = g_tex.find(key);
	if(it != g_tex.end()) return it->second;
	Texture *t = rm8PackTexture(key);
	if(!t) rm8Log("minigame 21: texture %s not found", key.c_str());
	return g_tex[key] = t;
}
void setPart(Mdl &m, const std::string &part, Texture *t)
{
	if(!t) return;
	auto it = m.parts.find(part);
	if(it == m.parts.end()) return;
	for(Material *mat : it->second) mat->setTexture(t);
}
void applyFace(Mdl &m, float t)	// the role.bin face swaps of the playing animation (as roles.cpp applyFace)
{
	auto d = g_art.find(m.art);
	if(d == g_art.end()) return;
	auto it = d->second.faces.find(m.anim);
	if(it == d->second.faces.end()) return;
	const FaceKey *k = nullptr;
	for(const FaceKey &f : it->second) if(f.t <= t + 1e-4f && (!k || f.t >= k->t)) k = &f;
	if(!k || k->tex == m.face) return;
	setPart(m, k->part, tex(k->tex));
	m.face = k->tex;
}
// same stepping as mg_3d.cpp: [V] article +0x398 = 0 loops forever, n > 0 = n plays then holds (CArticle_UpdateAnimationTime @0x4476e0)
void step(Mdl &m, float dt)
{
	if(!m.hier) return;
	AnimInterpolator *ip = m.hier->interpolator;
	if(!ip->currentAnim) return;
	float dur = ip->currentAnim->duration;
	if(m.holding) dt = 0;
	else if(dur > 0){
		float t = ip->currentTime + dt;
		if(t >= dur){
			if(m.times == 0){ ip->addTime(std::max(0.0f, dur - ip->currentTime - 1e-4f)); ip->setCurrentAnim(ip->currentAnim); dt = std::fmod(t, dur); }
			else if(--m.left <= 0){ m.holding = true; dt = std::max(0.0f, dur - ip->currentTime - 1e-4f); }
			else { ip->addTime(std::max(0.0f, dur - ip->currentTime - 1e-4f)); ip->setCurrentAnim(ip->currentAnim); dt = std::fmod(t, dur); }
		}
	}
	ip->addTime(dt);
	for(int i = 0; i < m.hier->numNodes; i++){
		Frame *f = m.hier->nodeInfo[i].frame;
		if(!f) continue;
		ip->applyCB(&f->matrix, ip->getInterpFrame(i));
		f->updateObjects();
	}
	m.hier->updateMatrices();
	applyFace(m, ip->currentTime);
}
Mdl *get(int h){ h = -h; return h > 0 && h < (int)g_m.size() && g_m[h].used ? &g_m[h] : nullptr; }

int create(int art)
{
	load();
	Mdl m; m.used = true; m.art = art;
	auto it = g_art.find(art);
	if(it != g_art.end()) m.clump = rm8LoadClump(it->second.dff, it->second.texdir);
	if(!m.clump){ m.fallback = true; }
	else{
		m.hier = HAnimHierarchy::find(m.clump->getFrame());
		if(m.hier) m.hier->attach();
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
				if(ua && ua->datatype == USERDATASTRING && ua->numElements > 0 && ua->getString(0)) m.parts[ua->getString(0)].push_back(mat);
			}
		}
		if(!m.anims.empty() && m.hier){ m.hier->interpolator->setCurrentAnim(m.anims.begin()->second); m.anim = m.anims.begin()->first; step(m, 0); }
		if(!g_world){
			g_world = World::create();
			Light *amb = Light::create(Light::AMBIENT); amb->setColor(0.75f, 0.75f, 0.75f); g_world->addLight(amb);
			Light *dir = Light::create(Light::DIRECTIONAL); dir->setColor(0.55f, 0.55f, 0.55f);
			Frame *lf = Frame::create(); V3d d = {0.3f, -0.6f, 0.75f}, up = {0, 1, 0};
			Matrix lm; lm.setIdentity(); lm.lookAt(d, up); lf->transform(&lm, COMBINEREPLACE); dir->setFrame(lf); g_world->addLight(dir);
		}
		g_world->addClump(m.clump);
	}
	for(size_t i = 1; i < g_m.size(); i++) if(!g_m[i].used){ g_m[i] = m; return -(int)i; }
	g_m.push_back(m);
	return -((int)g_m.size() - 1);
}
void destroy(int h)
{
	Mdl *m = get(h);
	if(!m) return;
	if(m->clump){ if(g_world) g_world->removeClump(m->clump); m->clump->destroy(); m->clump = nullptr; }
	m->used = false;
}
void pos(int h, V3 p)	// [V] CArticleMgr vf40 @0x43FB00 type 4: translate by (new - old), post-concat
{
	Mdl *m = get(h);
	if(!m) return;
	if(m->clump){
		V3d d = {p.x - m->lastPos.x, p.y - m->lastPos.y, p.z - m->lastPos.z};
		m->clump->getFrame()->translate(&d, COMBINEPOSTCONCAT);
	}
	m->lastPos = {p.x, p.y, p.z};
}
void scale(int h, V3 s)	// [V] CArticleMgr vf35 @0x43F3B0
{
	Mdl *m = get(h);
	if(!m || !m->clump) return;
	Frame *f = m->clump->getFrame();
	V3d p = f->matrix.pos, np = {-p.x, -p.y, -p.z}, sc = {s.x, s.y, s.z};
	f->translate(&np, COMBINEPOSTCONCAT); f->scale(&sc, COMBINEPOSTCONCAT); f->translate(&p, COMBINEPOSTCONCAT);
}
void rot(int h, V3 axis, float deg)	// [V] CMiniGameHost_2 vf27 @0x50F8F0
{
	Mdl *m = get(h);
	if(!m || !m->clump) return;
	Frame *f = m->clump->getFrame();
	V3d p = f->matrix.pos, np = {-p.x, -p.y, -p.z}, ax = {axis.x, axis.y, axis.z};
	f->translate(&np, COMBINEPOSTCONCAT); f->rotate(&ax, deg, COMBINEPOSTCONCAT); f->translate(&p, COMBINEPOSTCONCAT);
}
void action(int h, int a, int times)	// [V] +0x58: animation record index, times 0 = loop
{
	Mdl *m = get(h);
	if(!m || !m->hier) return;
	auto it = m->anims.find(a);
	if(it == m->anims.end()) return;
	m->hier->interpolator->setCurrentAnim(it->second);
	m->anim = a; m->face.clear();
	m->times = times < 0 ? 1 : times; m->left = m->times; m->holding = false;
	step(*m, 0);
}
void update(int h, float dt){ if(Mdl *m = get(h)) step(*m, dt); }
bool engineReady(){ return rw::engine->currentCamera && rw::engine->currentCamera->world; }	// [A] inside a real 3D frame (not the headless step)
void draw(int h)
{
	Mdl *m = get(h);
	if(!m || !m->clump || !engineReady()) return;
	mg::gfxFlush();
	uint32 zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE), atf = GetRenderState(ALPHATESTFUNC), atr = GetRenderState(ALPHATESTREF), cull = GetRenderState(CULLMODE);
	SetRenderState(ZTESTENABLE, 1); SetRenderState(ZWRITEENABLE, 1); SetRenderState(CULLMODE, CULLBACK);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196);	// [H] like mg_3d.cpp mdDraw / the board characters
	World *prev = engine->currentWorld;
	engine->currentWorld = g_world;
	m->clump->render();
	engine->currentWorld = prev;
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(CULLMODE, cull);
	SetRenderState(ALPHATESTFUNC, atf); SetRenderState(ALPHATESTREF, atr);
	mg::gfxRestore2D();
}
// Asset_LoadCharacterTexture @0x4934a0: the body (and "other" / "hair" / "toujin") textures of character k, colour c, front (flag 1) or back
void charTex(int h, int k, int color, bool front)
{
	Mdl *m = get(h);
	if(!m || !m->clump) return;
	char key[160];
	const char *fb = front ? "f" : "b";
	snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_body_%s_%02d.tga", k, k, fb, color);
	setPart(*m, "body", tex(key));
	const char *extra = (k == 2 || k == 15) ? "other" : (k == 4 || k == 9 || k == 11 || k == 13) ? "hair" : (k == 7 || k == 10) ? "toujin" : nullptr;
	if(extra){ snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_%s_%s_%02d.tga", k, k, extra, fb, color); setPart(*m, extra, tex(key)); }
}
void freeAll(){ for(size_t i = 1; i < g_m.size(); i++) destroy(-(int)i); }
}	// namespace lm

// ---- handle dispatch: article ids 0x711..0x717 (barrel, swords) are mg_3d models, everything else is local
int mNew(int art){ return (art >= 0x711 && art <= 0x717) ? mg::mdNew(art) : lm::create(art); }
void mFree(int h){ if(h > 0) mg::mdFree(h); else if(h < 0) lm::destroy(h); }
void mPos(int h, V3 p){ if(h > 0) mg::mdPos(h, p); else if(h < 0) lm::pos(h, p); }
void mScale(int h, V3 s){ if(h > 0) mg::mdScale(h, s); else if(h < 0) lm::scale(h, s); }
void mRot(int h, V3 a, float d){ if(h > 0) mg::mdRot(h, a, d); else if(h < 0) lm::rot(h, a, d); }
void mAction(int h, int a, int t){ if(h > 0) mg::mdAction(h, a, t); else if(h < 0) lm::action(h, a, t); }
void mUpdate(int h, float dt){ if(h > 0) mg::mdUpdate(h, dt); else if(h < 0) lm::update(h, dt); }
// [V] The host's 2D quads (Sprite_Render @0x462580, vf60 @0x5105b0) set only blend / filter / texture states: depth test and depth WRITE stay at the RenderWare defaults (on),
// at the near-plane z of Im2D.  A sprite therefore hides the models drawn after it wherever its alpha passes the test (host +0x3c = 192), which is what the Ready / Go banners
// and the HUD plate rely on, and why the DLL clears the depth buffer (host +0x38) after the HUD tiles and before the god / overlay.  [A] mg_gfx.cpp draws 2D without depth, so
// this file switches it on for its own draw (and again after every model draw, which ends with gfxRestore2D).
void aTest(){ mg::alphaTest(0xc0); }	// [V] host +0x3c (0xc0) (CMiniGameHost_2_vf15 @0x50F3C0: render state 0x1e = 192, 0x1d = 7 = ALPHAGREATEREQUAL)
void depth2D()
{
	if(!lm::engineReady()) return;
	mg::gfxFlush();
	rw::SetRenderState(rw::ZTESTENABLE, 1); rw::SetRenderState(rw::ZWRITEENABLE, 1);
}
void mDraw(int h)
{
	if(h > 0) mg::mdDraw(h); else if(h < 0) lm::draw(h);
	aTest(); depth2D();	// the model draws end with gfxRestore2D: restore the state the DLL's host keeps
}

// host +0x38 (CMiniGameHost_2_vf14 @0x50F390 = RwCameraClear(camera, 0, 2)): clears the depth buffer; what is drawn after it is not occluded by what was drawn before
void zClear()
{
	mg::gfxFlush();
	if(lm::engineReady()){ rw::RGBA c = {0, 0, 0, 0}; rw::engine->currentCamera->clear(&c, rw::Camera::CLEARZ); }
}

// =========================================================================================================================
// game
// =========================================================================================================================
struct Player {			// [V] 0x7c bytes (ctor @10005cf0)
	int id = 0;		// +0 descriptor id (role +0x1a8), echoed back in the result
	int model = 0;		// +8 article handle
	float hx = 0, hy = 0, hw = 0, hh = 0;	// +0xc.. the portrait box of the HUD
	uint32_t hcol = 0xffffffff;		// +0x1c r,g,b,a bytes
	std::string portrait;			// +0x20 texture (descriptor +0x48)
	bool active = false;			// +0x3d (the turn marker)
	V3 pos;					// +0x40
	bool win = false, lose = false, ai = false;	// +0x58 / +0x59 / +0x5a
	int icon = 0;				// +0x5c article id of the model (role "br" view = icon id - 1)
	int seat = 0;				// +0x60
	int target = -1, dir = 0;		// +0x64 / +0x68 the hole / direction chosen by the computer (or by the time-out)
	int color = -1;				// +0x6c descriptor +0x50 (-1 = no descriptor)
	int snd[3] = {0, 0, 0};			// +0x70 / +0x74 / +0x78 voice ids
};

struct Box { float x = 0, y = 0, w = 0, h = 0; };

struct G21 : MgGame {
	// ---- game object
	std::vector<Player> pl;			// +0x11c / +0x270
	int n = 0;
	float S = 1.28f;			// +0x140 scale = width * 0.00125
	float W = 1024, H = 768;		// +0x810 / +0x814
	int state = 0;				// state machine +4 (the game object's +8)
	float timer = 0;			// +0x14 (state machine +0x10)
	bool endFlag = false;			// +0x13f
	bool hoverL = false, hoverR = false, btn = false;	// +0x13d / +0x13e / +0x13c
	int defender = -1;			// +0x144
	int cur = 0;				// +0x274
	bool finished = false;			// +0x800
	float fade = 0;				// +0x870
	int countdown = 6;			// +0x890
	bool tmo = false;			// +0x895 time-out flag (the computer plays the turn of a human)
	bool canConfirm = true;			// +0x896
	bool godFirst = true;			// +0x88c
	bool seatFirst = true;			// +0x88d
	bool snd16d = true;			// +0x894
	float t7fc = 0;				// +0x7fc
	V3 seat[4];				// +0x830 + 12 * k: the seats around the barrel
	Box box[6];				// +0x2c + 0x28 * k: the six hit boxes
	std::deque<Cmd> queue;			// +0x878
	std::vector<int> seq;			// +0x81c hidden key sequence
	bool force82c = false; int force1c = 0;	// +0x82c / +0x1c
	// sprites / models
	int sprFx1 = 0, sprFx2 = 0;		// +0x124 (0x70f) / +0x128 (0x70e): the symbols shown by the delayed-callback slot
	int sprLh = 0, sprL = 0, sprRh = 0, sprR = 0;	// +0x12c (0x70d) / +0x130 (0x710) / +0x134 (0x719) / +0x138 (0x718) the arrow buttons (hover / normal)
	int sprBan1 = 0, sprBan2 = 0, sprBan3 = 0, sprBan4 = 0;	// +0x860 (0x84b Ready) / +0x864 (0x84c Go) / +0x868 (0x84d) / +0x86c (0x84e)
	int god = 0;				// +0x874 (art 503)
	// delayed callback slot of the Fx object (@10006c10 / @10006df0)
	int fxSprite = 0; float fxDelay = 0; bool fxOn = false; int fxCb = -1;	// +0x4e8 / +0x4ec / +0x4f0 / +0x4f4
	// HUD (@10006760)
	int hudBg[5] = {};			// +0x6c.. 0x709 0x70a 0x70b 0x70c 0x71a (the last one is animated)
	int hudPlateW = 0, hudPlateR = 0;	// +0x14 (0x83f) / +0x18 (0x84a)
	int hudDigW[10] = {}, hudDigR[10] = {};	// +0x1c / +0x44
	V3 hudDigPos;				// +0x80
	// barrel (+0x148)
	struct Barrel {
		bool poolUsed[NHOLES] = {};	// +8 + 8k
		int trap = 0;			// +0x64
		int model = 0;			// +0x68
		int swordsIcon = 0, ov1 = 0, ov2 = 0;	// +0x6c / +0x70 / +0x74 (art 0x713 x3)
		int sword[NHOLES] = {};		// +0x78 + 12 k
		bool clicked[NHOLES] = {};	// +0x7c + 12 k
		float angle = 180.0f;		// +0x108
		float speed = 180.0f;		// +0x10c
		bool dir = false;		// +0x110
		bool rotating = false;		// +0x111
		float acc = 0;			// +0x114
		int remaining = 8;		// +0x120 (= game +0x268)
	} bar;
	bool introShown = false;
	bool forcedPirate = false; int forcePirate = 0;
	int forceTrap = -1;
	int hitAt = 0, stabs = 0; bool neverHit = false;
	// [N] N4 netplay (minigame.h Rm8Mini21Net, docs/netplay.md section 9.3): every device runs this game; the pirate, every stab's outcome and the turns that
	// are not played on this device come from the referee of the event task (event_mini.cpp) through mg::net21Acts(), in order. pl[i].ai = "not played here".
	bool net = false;
	size_t netPos = 0;		// the next referee action to show
	int netTurn = 0;		// the stabs shown so far (= the referee's turn number of the current turn)
	int netOutcome = -1;		// the referee's outcome of the stab being shown
	bool netWait = false;		// the local player's choice went out (it comes back as a referee action)
	int netRotOut = 0;		// barrel turns of the local player sent and not shown yet
	bool netReady = false;		// the 0.7 s pause of a turn played elsewhere is over
	bool netAutoPending = false;	// a computer choice turns the barrel first, then stabs (autoStep)
	int netDueSent = -1;
	float netIntroT = 0;
	float netBot = -1, netBotT = 0; bool netBotTurned = false;	// test hook RM8_MG21_NETBOT=<s>: the local player chooses after <s> seconds (sometimes turns the barrel first)

	// ---------------------------------------------------------------------------------------------------------------
	int rnd(){ return mg::rand15(); }
	void snd(int id, int loops = 1)		// Sound_Play @10006140: game id -> host id [V], volume 100.0 (this+0x1c), position (300,300,0) ignored
	{
		int h;
		switch(id){
		case 0x163: h = 0x15a; break;
		case 0x164: h = 0x12d; break; case 0x165: h = 0x12e; break; case 0x166: h = 0x12f; break; case 0x167: h = 0x130; break;
		case 0x168: h = 0x131; break; case 0x169: h = 0x132; break; case 0x16a: h = 0x133; break; case 0x16b: h = 0x134; break;
		case 0x16c: h = 0x135; break; case 0x16d: h = 0x165; break;
		default: h = id;	// 0x152 / 0x153 / 0x154 and the voices pass through
		}
		int hnd = mg::snd(h, 100.0f, loops);
		if(loops == 0 && hnd) loopSnd.push_back(hnd);	// [V] 10006140: handles of looping samples are recorded for Sound_StopAll @100060a0
	}
	std::vector<int> loopSnd;
	void stopSounds(){ for(int h : loopSnd) mg::sndStop(h); loopSnd.clear(); }
	void voice(int id){ mg::snd(id, 100.0f, 1); }	// direct host +0x114 calls (the voices): volume 100.0, loops 1

	// ---- players -------------------------------------------------------------------------------------------------
	void playerCtor(Player &p, int index, const Rm8MiniPlayer *d)	// @10005cf0
	{
		float cz = 0 + CAM_AT_Z * 440.0f;	// +0x28 camera position z (0) + +0x2c view direction z * [0x100322b8] = 440
		if(d){
			p.id = d->seat;
			int k = std::max(1, std::min(16, d->charId));
			int iconId = 12 * (k - 1) + 3;	// [V] descriptor +0x4c = role +0x308 = (role +0xf8)+0x308 = art[0] (fl view) of the roleinfo = 12 * (charId - 1) + 3 (roles.txt, roles 1..16)
			p.icon = iconId - 1;		// [V] 10005d29: the "br" view
			p.color = std::max(0, std::min(4, d->outfit));	// [V] 10005d11: descriptor +0x50 = role +0x1ac (colour / outfit index, 4 = swimsuit)
			if(p.color == 4 && !lm::has(p.icon + 0xc0)) p.color = 0;	// [A] no swimsuit model in an old roles.txt: the plain model (like roles.cpp loadModel)
			if(p.color == 4){ p.icon += 0xc0; p.model = mNew(p.icon); }	// [V] 10005d37
			else{ p.model = mNew(p.icon); lm::charTex(p.model, iconId / 12 + 1, p.color, false); }	// [V] 10005d6a..d81: vf32 (model, iconId / 12 + 1, +0x50, 0): the BACK textures (the ctor pushes 0, SetIcon @10005c85 pushes 1)
			p.portrait = "prt\\npc\\" + d->pic + "_01.tga";	// [A] descriptor +0x48 = portrait texture (the guide's key)
			p.ai = !d->human;		// [V] 10005de9: +0x5a = (type == 1)
			p.snd[0] = (iconId / 12) * 200 + 0x25c; p.snd[1] = (iconId / 12) * 200 + 0x25d; p.snd[2] = (iconId / 12) * 200 + 0x260;	// [V] 10005e05..
		}
		p.hx = (float)(index * 0x50 + 0x14) * S;	// [V] 10005d8b: (index * 80 + 20) * scale
		p.hy = S * 20.0f; p.hw = p.hh = S * 65.0f;
		p.hcol = 0xffffffff;
		p.active = false;
		mAction(p.model, 2, 0);			// [V] 10005f4f: action 2, loop (stand)
		mRot(p.model, {0, 1, 0}, 180.0f);	// [V] 10005f7d: turned 180 degrees about y
		p.pos = V3(120.0f - 50.0f * index, -90.0f, cz);	// [V] Init passes (120, -90, 0) minus 50 per player (1003907c) as x, y; z = 0 + at.z * 440 @10005f9a
		mPos(p.model, p.pos);
	}
	void setIcon(Player &p, int icon)	// LG21_Player_SetIcon @10005c30
	{
		if(p.model){ mFree(p.model); p.model = 0; }
		p.icon = icon;
		p.model = mNew(icon);
		if(p.color != -1 && p.color != 4) lm::charTex(p.model, icon / 12 + 1, p.color, true);	// [V] 10005c7a..ca1
		mRot(p.model, {0, 1, 0}, 180.0f);
		mPos(p.model, p.pos);
	}

	// ---- barrel --------------------------------------------------------------------------------------------------
	bool isHidden(int hole)	// IsHoleHidden @100012a0: not among the six holes from (angle / 90) * 3 on
	{
		int a = ftol(bar.angle) / 90 * 3;
		for(int j = 0; j < 6; j++) if((a + j) % NHOLES == hole) return false;
		return true;
	}
	int boxToHole(int b){ return (b + ftol(bar.angle) / 90 * 3) % NHOLES; }	// @10001260
	int pickUnused()	// PickRandomUnusedHole @100010c0
	{
		int r = rnd() % NHOLES, h = r;
		if(bar.clicked[r]){
			for(int i = 0; i < NHOLES; i++){ h = (h + 1) % NHOLES; if(!bar.clicked[h]) return h; }
			if(h == r) h = -1;
		}
		return h;
	}
	void stepAngle()	// @100011b0
	{
		if(bar.angle == 0.0f){ bar.angle = bar.dir ? 90.0f : 270.0f; return; }
		if(bar.angle != 270.0f){ bar.angle += bar.dir ? 90.0f : -90.0f; return; }
		bar.angle = bar.dir ? 0.0f : 180.0f;
	}
	bool firstStab()	// IsFirstStab @10001310: exactly one pool entry used
	{
		if(net) return stabs == 1;	// [N] the referee's pool (one draw per valid stab)
		int c = 0; for(int k = 0; k < NHOLES; k++) if(bar.poolUsed[k]) c++;
		return c == 1;
	}
	int stab(int hole)	// LG21_Barrel_Stab @10001950: -1 invalid, 0 safe, 1 the pirate is hit
	{
		if(hole < 0 || hole > 11 || bar.clicked[hole]) return -1;
		bar.clicked[hole] = true;
		mAction(bar.sword[hole], 0, 1);		// [V] 10001978: +0x58 (sword, 0, 1)
		snd(0x153);				// [V] 1000198e
		if(net){	// [N] N4: the referee drew this stab's pool hole (the same on every device)
			int r = netOutcome; netOutcome = -1; stabs++;
			LOG21("net: stab %d outcome %d", stabs, r);
			if(r == 1) return 1;
			bar.remaining--;
			return 0;
		}
		if(force82c){
			if(force1c == 1) return 1;	// [V] 100019b7: hidden sequence: the stab hits
			if(force1c == 2){ bar.remaining--; return 0; }	// [V] 100019d9: ... or is safe
		}
		int k = rnd() % NHOLES;
		stabs++;
		if(hitAt > 0 && stabs == hitAt && !bar.poolUsed[bar.trap]) k = bar.trap;	// test hook RM8_MG21_HIT: this stab draws the trap hole
		else if(neverHit && k == bar.trap) k = (k + 1) % NHOLES;	// test hook RM8_MG21_NEVER: the trap hole is skipped by the draw (it is never reached while sword are left)
		if(bar.poolUsed[k]){
			int i = 0;
			do{
				bool done = false;
				for(int s = 0; s < 6; s++){ k = (k + 1) % NHOLES; if(!bar.poolUsed[k]){ done = true; break; } }
				if(done) break;
				i += 6;
			}while(i < NHOLES);
		}
		bar.poolUsed[k] = true;
		if(k == bar.trap) return 1;
		bar.remaining--;
		return 0;
	}
	void setDirection(bool d)	// @10001aa0: direction 1 = left = negative speed
	{
		bar.dir = d;
		if(d){ if(bar.speed > 0.0f) bar.speed = -bar.speed; }
		else{ if(bar.speed < 0.0f) bar.speed = -bar.speed; }
		LOG21("barrel direction %s (speed %.0f)", d ? "left" : "right", bar.speed);
		snd(0x162);
	}
	void autoStep()	// @10001af0
	{
		if(net){ if(!netAutoPending) return; }	// [N] only a computer choice of the referee goes on by itself (a remote player's turn of the barrel does not stab)
		else if(!pl[cur].ai && !tmo) return;
		int t = pl[cur].target;
		if(isHidden(t)){ setDirection(pl[cur].dir != 0); bar.rotating = true; return; }
		doStab(t);
	}
	void rotateAnim(float dt)	// RotateAnim @10001b70
	{
		const V3 axis(0, 1, 0);
		mPos(bar.model, V3(0, 0, 0));
		mRot(bar.model, axis, bar.speed * dt);
		mPos(bar.model, V3(BARREL_X, BARREL_Y, BARREL_Z));
		for(int k = 0; k < NHOLES; k++){ mPos(bar.sword[k], V3(0, 0, 0)); mRot(bar.sword[k], axis, bar.speed * dt); }
		for(int k = 0; k < NHOLES; k++) mPos(bar.sword[k], V3(BARREL_X, BARREL_Y, BARREL_Z));
		bar.acc = bar.speed * dt + bar.acc;
		if(bar.acc > 90.0f || bar.acc < -90.0f){
			bar.acc = 0; bar.rotating = false;
			stepAngle();
			LOG21("barrel angle now %.0f", bar.angle);
			autoStep();
		}
	}
	void layoutBoxes(float px, float py)	// LayoutHitBoxes @10001da0
	{
		float w = S * 60.0f;
		for(int k = 0; k < 6; k++){
			box[k].w = box[k].h = w;
			box[k].x = k < 3 ? px - w : px;
			box[k].y = py - (float)(3 - k % 3) * S * 50.0f;
		}
		float d = S * 5.0f;
		box[0].y += d; box[1].y += d; box[3].y += d; box[4].y += d;
	}
	int hitTest(float x, float y)	// HitTest @10001ea0 (strict inequalities)
	{
		for(int k = 0; k < 6; k++)
			if(box[k].x < x && x < box[k].w + box[k].x && box[k].y < y && y < box[k].h + box[k].y) return k;
		return -1;
	}
	void mousePos(float &x, float &y){ mg::mouse(x, y); x = (float)(int)x; y = (float)(int)y; }	// host +0x128 gives ints

	// ---- game rules ---------------------------------------------------------------------------------------------
	void markDefenderWins()	// @10001f00
	{
		for(int i = 0; i < n; i++) if(i != defender) pl[i].lose = true;
		pl[defender].win = true;
	}
	void markPopperWins(){ pl[cur].win = true; pl[defender].lose = true; }	// @10001f50
	void doStab(int hole)	// LG21_Game_DoStab @10001fa0
	{
		netAutoPending = false;
		int r = stab(hole);
		LOG21("stab hole %d by player %d -> %d (remaining %d)", hole, cur, r, bar.remaining);
		if(r == 1){ finished = true; markPopperWins(); setState(6); return; }
		if(r == 0){
			finished = false;
			if(bar.remaining <= 0){ finished = true; markDefenderWins(); }	// IsSwordsExhausted @10001240
		}
		setState(6);
	}
	bool onMouseClick()	// LG21_Game_OnMouseClick @100022c0
	{
		if(state != 4) return false;
		float x, y; mousePos(x, y);
		int b = hitTest(x, y);
		if(b == -1) return false;
		int r = stab(boxToHole(b));
		LOG21("click box %d -> hole %d -> %d (remaining %d)", b, boxToHole(b), r, bar.remaining);
		if(r == -1) return false;
		if(r == 1){ finished = true; markPopperWins(); }
		else if(r == 0){
			finished = false;
			if(bar.remaining <= 0){ finished = true; markDefenderWins(); }
		}
		setState(6);
		mg::showCursor(false);
		return true;
	}
	void pirateEmerge()	// LG21_Game_PirateEmerge @10002030
	{
		Player &d = pl[defender];
		setIcon(d, d.icon + 1);				// [V] 10002030: the "fl" (front) view
		mScale(d.model, V3(fbits(0x3f733333), 1.0f, 1.0f));	// [V] (0.95, 1, 1)
		float y = BARREL_Y + 30.0f, x = BARREL_X - 2.0f;	// [V] 10002100..: (4 - 2, -26 + 30, 500)
		int ic = d.icon;
		if(ic < 0x118){
			if(ic == 0x117 || ic == 0x57){ y += 8.0f; x += 6.0f; }
			else if(ic == 0x93 || ic == 0x153){ y += 5.0f; }
			else if(ic == 0x9f){ y += 3.0f; }
		}else if(ic == 0x153){ y += 5.0f; }
		else if(ic == 0x15f){ y += 3.0f; }
		d.pos = V3(x, y, BARREL_Z);
		d.seat = n - 1;
		mAction(d.model, 8, 0);
	}

	// ---- state machine ------------------------------------------------------------------------------------------
	void setState(int s)	// LG21_SetState @10006420
	{
		state = s;
		float t = 0;
		switch(s){
		case 1: t = 10.0f; break; case 3: t = 2.0f; break; case 4: t = 7.0f; break; case 6: t = fbits(0x3f333333); break;
		case 7: t = 1.0f; break; case 9: t = 2.0f; break; case 10: t = 2.5f; break; case 11: t = fbits(0x3f333333); break;
		default: return;	// 2, 5, 8: no timer
		}
		if(t > 0.0f) timer = t;	// [V] 10006400: the timer is only set for a positive value
		LOG21("state %d (timer %.2f)", s, timer);
	}
	void tick(float dt)	// State_Tick @100064e0
	{
		if(state == 0) return;
		if(timer > 0.0f){ timer = timer - dt; return; }
		if(timer < 0.0f){ timer = 0; onTimeout(state); }
	}
	void schedule(int spr, float delay, int cb){ fxSprite = spr; fxDelay = delay; fxOn = true; fxCb = cb; }	// Fx_ScheduleCallback @10006c10
	void fxUpdate(float dt)	// Fx_Update @10006df0 (only the delayed callback slot is ever used, the ten timer slots stay inactive)
	{
		if(fxOn){
			fxDelay = fxDelay - dt;
			if(fxDelay < 0.0f){
				fxOn = false; fxDelay = 0;
				if(fxCb >= 0){ if(fxCb == 0) nextTurn(); fxCb = -1; }
			}
		}
	}
	// the computer's choice of hole (@10002508 and the others): returns after starting the rotation or the stab
	void computerPlay()
	{
		int t = pickUnused();
		pl[cur].target = t;
		if(isHidden(t)){
			int d = rnd() % 2;			// [V] call 10023ccc & 1
			pl[cur].dir = d;
			setDirection(d != 0);
			bar.rotating = true;
		}else doStab(t);
	}
	void onTimeout(int s)	// the handler @10002390 (jump table @10002944 for the states 3..0xb)
	{
		switch(s){
		case 3: setState(9); break;
		case 4:	// @1000259a: the human did not choose in 7 s
			if(net){	// [N] N4: the choice goes to the referee (the hole under the pointer, else the computer's choice), it comes back as an action
				if(netWait || pl[cur].ai) break;
				tmo = true;
				mg::showCursor(false);
				float x, y; mousePos(x, y);
				int b = hitTest(x, y);
				if(b != -1 && !bar.clicked[boxToHole(b)] && !bar.rotating && netRotOut == 0) netSend(RM8M21_STAB, boxToHole(b));
				else netSend(RM8M21_AUTO, 0);
				netWait = true;
				break;
			}
			if(onMouseClick()) break;		// the mouse may be on a hole: take it
			tmo = true;
			mg::showCursor(false);
			computerPlay();
			break;
		case 6: stateSix(); break;
		case 7: stateSeven(); break;
		case 9:	// @100023c3: the first turn
		{
			fade = 0;
			setState(4);
			if(defender == 0){
				if(n > 1){ pl[1].active = true; cur = 1; }
				mAction(pl[1 < n ? 1 : 0].model, 4, 0);
			}else{
				if(n > 0){ pl[0].active = true; cur = 0; }
				mAction(pl[0].model, 4, 0);
			}
			if(net){	// [N] a turn played elsewhere waits for the referee (the pause of a computer turn)
				if(pl[cur].ai){ mg::showCursor(false); setState(11); }
				else mg::showCursor(true);
				break;
			}
			if(pl[cur].ai){
				mg::showCursor(false);
				computerPlay();
			}
			if(!pl[cur].ai) mg::showCursor(true);
			break;
		}
		case 10: endFlag = true; mg::showCursor(true); break;	// @10002925
		case 11:	// @10002508: the computer's turn after 0.7 s
			if(net){	// [N] the referee's action of this turn is shown from now on (netPump); the host may decide a computer seat's stab now
				netReady = true;
				if(netDueSent != netTurn && mg::net21().botDue){ netDueSent = netTurn; mg::net21().botDue(netTurn); }
				break;
			}
			mg::showCursor(false); computerPlay(); break;
		default: break;
		}
	}
	void stateSix()	// @1000262f: 0.7 s after a stab
	{
		if(finished){
			if(pl[defender].win){	// the pirate won
				snd(0x16a);
				voice(pl[defender].snd[0]);
				snd(0x16b, 2);
				mAction(pl[defender].model, 4, 0);
				schedule(sprFx1, 1.0f, -1);
				for(int i = 0; i < n; i++){
					if(i == defender) continue;
					mAction(pl[i].model, 7, pl[i].icon == 0x92 ? 1 : 0);	// [V] 10002706: action 7, played once for icon 0x92 else looped
				}
				setState(10);
			}else{		// the pirate was hit
				snd(0x154);
				schedule(sprFx2, 1.0f, -1);
				snd(0x155);
				setState(7);
			}
			return;
		}
		schedule(sprFx1, fbits(0x3fd9999a), 0);	// [V] 10002788: 1.7 s, then NextTurn (callback 0)
		snd(0x16a);
		if(rnd() % 100 < 50){			// Rand50 @10002210
			voice(pl[cur].snd[1]);
			mAction(pl[defender].model, 3, 1);
		}
		mAction(pl[cur].model, 6, 1);
	}
	void stateSeven()	// @1000282e: the pirate has popped out, the winner cheers
	{
		snd(0x169);
		setIcon(pl[cur], pl[cur].icon + 2);	// [V] 1000286f: view +2 (the "fr" view)
		mAction(pl[cur].model, 5, 2);
		for(int i = 0; i < n; i++) if(i != cur) mAction(pl[i].model, 0xb, 0);
		voice(pl[cur].snd[2]);
		snd(0x16b, 0);			// loops until the end (the DLL records the handle and stops it in ReleaseResources)
		setState(10);
	}
	void nextTurn()	// LG21_Game_NextTurn @100043e0
	{
		tmo = false;
		netWait = false; netRotOut = 0; netReady = false; netAutoPending = false; netBotT = 0; netBotTurned = false;	// [N]
		pl[cur].active = false;
		mAction(pl[cur].model, 3, 0);
		mAction(pl[defender].model, 8, 1);
		cur = (cur + 1) % n;
		if(cur == defender) cur = (cur + 1) % n;
		pl[cur].active = true;
		mAction(pl[cur].model, 4, 0);
		for(int i = 0; i < n; i++){
			if(i == defender) continue;
			int s = pl[i].seat - 1;
			if(s < 0) s = n - 2;
			pl[i].seat = s;
			pushSeatMove(i);
		}
		if(pl[cur].ai){ setState(11); return; }
		countdown = 6;
		mg::showCursor(true);
		setState(4);
	}

	// ---- animation command queue ---------------------------------------------------------------------------------
	void pushIntro()	// @10004090
	{
		Cmd c;
		c.p = V3(0, 0, 450.0f);
		c.handle = god;
		float sx, sy; mg::project(c.p, sx, sy); c.sx = sx; c.sy = sy;
		int r = forcedPirate ? forcePirate % n : rnd() % n;
		mg::project(pl[r].pos, c.tx, c.ty);
		c.speed = 100.0f;
		// +0x24 / +0x28 stay unset here (stack residue in the DLL): the first Update step writes them (see Cmd)
		queue.push_back(c);
	}
	void pushSeatMove(int i)	// @10004160
	{
		Cmd c;
		c.handle = pl[i].model;
		c.p = pl[i].pos;
		float sx, sy; mg::project(c.p, sx, sy); c.sx = sx; c.sy = sy;
		mg::project(seat[pl[i].seat], c.tx, c.ty);
		c.speed = 200.0f;
		queue.push_back(c);
	}
	void snapSeats(const Cmd &c)	// the arrival code of the states 4, 6, 9, 0xb (@10003b2e / @10003feb): the seat is reached
	{
		for(int i = 0; i < n; i++){
			if(i == defender || c.handle != pl[i].model) continue;
			pl[i].pos = seat[pl[i].seat];
			mPos(pl[i].model, pl[i].pos);
		}
	}
	void onArrive(Cmd c)	// LG21_AnimCmd_OnArrive @10003b10
	{
		if(state == 9 || state == 6 || state == 11 || state == 4){ snapSeats(c); return; }
		if(state != 1) return;
		if(timer > 5.0f){	// [V] 10003bec: still more than 5 s left: the god flies on to another random player
			int i = forcedPirate ? forcePirate % n : rnd() % n;
			if(i == defender && !forcedPirate) i = (i + 1) % n;
			mg::project(pl[i].pos, c.tx, c.ty);
			c.sx = c.gx; c.sy = c.gy;
			c.speed = 70.0f;
			queue.push_back(c);
			defender = i;
			if(godFirst){ mAction(god, 3, 0); godFirst = false; }
			return;
		}
		if(c.next == 0 && !c.chained && c.handle == god){	// [V] 10003cc4: the god settles on the pirate and grabs him
			if(net && defender != mg::net21().defender){	// [N] N4: the referee's pirate: the god flies on to it first (the flights before are this device's own show)
				int i = std::max(0, std::min(n - 1, mg::net21().defender));
				mg::project(pl[i].pos, c.tx, c.ty);
				c.sx = c.gx; c.sy = c.gy;
				c.speed = 70.0f;
				queue.push_back(c);
				defender = i;
				if(godFirst){ mAction(god, 3, 0); godFirst = false; }
				LOG21("net: the god flies on to the referee's pirate %d", i);
				return;
			}
			c.speed = 30.0f;
			float oy = c.ty;
			c.sx = c.tx; c.sy = oy;
			c.ty = oy + 10.0f;
			c.chained = true;
			c.next = pl[defender].model;
			mAction(god, 1, 0);
			queue.push_back(c);
			voice(pl[defender].snd[0]);
			LOG21("pirate is player %d", defender);
			return;
		}
		if(c.next == 0 && !c.chained && c.handle != god){	// [V] 10003dd1: the pirate has reached the barrel
			mPos(pl[defender].model, pl[defender].pos);
			voice(pl[defender].snd[1]);
			setState(3);
			return;
		}
		float K = S * 400.0f, Y = S * fbits(0xc37a0000);	// [V] 10003e99: (512, -320) above the screen
		if(c.tx != K || c.ty != Y){
			c.tx = K; c.speed = 200.0f; c.ty = Y;
			queue.push_back(c);
			return;
		}
		if(c.handle != god) return;
		pirateEmerge();
		Cmd c2 = c;
		c2.p.z = 500.0f;
		c2.sx = c.tx; c2.sy = c.ty;
		mg::project(pl[defender].pos, c2.tx, c2.ty);
		c2.speed = 150.0f;
		c2.handle = pl[defender].model;
		c2.chained = false; c2.next = 0;
		queue.push_back(c2);
	}
	void updateQueue(float dt)	// LG21_AnimQueue_Update @100045a0: the commands are taken from the back, those not arrived are put back in the original order
	{
		if(queue.empty()) return;
		std::deque<Cmd> tmp;
		bool bail = false;
		int guard = 0;
		do{
			if(++guard > 4000) break;	// [A] safety only (the DLL has no limit)
			Cmd c = queue.back(); queue.pop_back();
			if(c.handle == 0){ bail = true; break; }	// [V] 10004625
			float psx, psy;
			mg::project(c.p, psx, psy);
			V3 v(c.tx - psx, c.ty - psy, 0);
			float len = sqrtf(v.x * v.x + v.y * v.y);	// host +0xc8 = RwV3dNormalize
			if(len > 0.0f){ v.x /= len; v.y /= len; } else v = V3(0, 0, 0);
			c.p.x = c.p.x - c.speed * v.x * dt;		// [V] 10004670: world x / y move against the screen direction (x is mirrored)
			c.p.y = c.p.y - c.speed * v.y * dt;
			mg::project(c.p, psx, psy);
			c.gx = psx; c.gy = psy;	// [V] 100046ad: the projected position is stored in +0x24 / +0x28 of the command
			float dTravel = sqrtf((c.sx - psx) * (c.sx - psx) + (c.sy - psy) * (c.sy - psy));
			float dTotal = sqrtf((c.tx - c.sx) * (c.tx - c.sx) + (c.ty - c.sy) * (c.ty - c.sy));
			mPos(c.handle, c.p);
			if(c.next != 0 && c.chained) mPos(c.next, c.p);	// [V] 100047ad
			if(dTotal > dTravel) tmp.push_back(c);
			else onArrive(c);
		}while(!queue.empty());
		if(bail) return;
		while(!tmp.empty()){ Cmd c = tmp.back(); tmp.pop_back(); queue.push_back(c); }
	}

	// ---- seat animation of the state 9 (Draw side) ---------------------------------------------------------------
	void state9Draw()	// LG21_State9_SeatAnim @100042a0
	{
		if(timer <= fbits(0x3f0c3b3e)){		// [V] 0.5485103
			if(snd16d){ snd(0x16d); snd16d = false; }
			float v = 1.0f - timer * fbits(0x3fe99ac6);	// 1.8231198
			v = v + v;
			mg::spScale(sprBan2, V3(v, v, 1));
			mg::spDraw(sprBan2);
			return;
		}
		if(seatFirst){
			snd(0x164);
			int s = 0;
			for(int i = 0; i < n; i++) if(i != defender){ pl[i].seat = s++; pushSeatMove(i); }
			snd(0x163);
			seatFirst = false;
		}
		mg::spScale(sprBan1, V3(1, 1, 1));
		mg::spDraw(sprBan1);
	}

	// =============================================================================================================
	bool init(const std::vector<Rm8MiniPlayer> &pls) override
	{
		g_log = getenv("RM8_MG21_LOG") != nullptr;
		if(const char *e = getenv("RM8_MG21_TRAP")) forceTrap = atoi(e) % NHOLES;	// test hooks (see the top of the file)
		if(const char *e = getenv("RM8_MG21_PIRATE")){ forcedPirate = true; forcePirate = atoi(e); }
		if(const char *e = getenv("RM8_MG21_HIT")) hitAt = atoi(e);
		if(getenv("RM8_MG21_NEVER")) neverHit = true;
		net = mg::net21().on;	// [N] N4: the referee decides (its own test hooks); these local ones would only change this device's show
		if(net){ forcedPirate = false; hitAt = 0; neverHit = false; forceTrap = -1; if(const char *e = getenv("RM8_MG21_NETBOT")) netBot = (float)atof(e); }
		W = (float)mg::W; H = (float)mg::H;
		S = W * SCALE_K;					// [V] 100050ec: scale = width * 0.00125
		n = std::max(1, std::min(4, (int)pls.size()));		// [A] the seat table of the DLL (+0x830) holds four seats
		pl.assign(n, Player());
		for(int i = 0; i < n; i++) playerCtor(pl[i], i, &pls[i]);
		bar.remaining = n == 2 ? 3 : n == 3 ? 6 : n == 4 ? 9 : -1;	// [V] 100052a6..: (players - 1) * 3, else -1
		// seat positions [V] 100052c8..: base (4 + 50, -25 - 40, z of player 0), seat k = base + (20 k, -18 k, 5 (n - k))
		{
			float bx = BARREL_X + 50.0f, by = BARREL_Y_INIT - 40.0f, bz = pl[0].pos.z;
			for(int k = 0; k < n; k++) seat[k] = V3(bx + (float)(20 * k), by - (float)(18 * k), bz + (float)(5 * (n - k)));
		}
		for(int i = 0; i < n; i++) pl[i].seat = i;
		hudLoad();
		// barrel (ctor 10001360, Init 100013d0, InitTrapHole 10001000)
		barrelInit();
		bar.trap = forceTrap >= 0 ? forceTrap : rnd() % NHOLES;
		sprFx1 = mg::spNew(0x70f); sprFx2 = mg::spNew(0x70e);	// [V] 100055a9 / 100055bc
		{
			V3 sc(S, S, 1.0f);
			mg::spScale(sprFx1, sc); mg::spScale(sprFx2, sc);
			V3 p(S * 300.0f, S * 200.0f, 0);
			mg::spPos(sprFx2, p); mg::spPos(sprFx1, p);
		}
		float bx, by; mg::project(V3(BARREL_X, BARREL_Y, BARREL_Z), bx, by);	// [V] 1000564c: host +0x1c of (4, -26, 500)
		layoutBoxes(bx, by);
		for(int k = 0; k < 6; k++) LOG21("box %d: x %.1f y %.1f w %.1f h %.1f (barrel centre %.0f %.0f)", k, box[k].x, box[k].y, box[k].w, box[k].h, bx, by);
		{	// arrows [V] 100056a5..100057c1
			V3 big(fbits(0x3fa00000), fbits(0x3fa00000), 1.0f), norm(S, S, 1.0f);
			sprLh = mg::spNew(0x70d); mg::spScale(sprLh, big); mg::spPos(sprLh, V3(S * 335.0f, S * 400.0f, 0)); mg::spSetLoops(sprLh, 0);
			sprL = mg::spNew(0x710); mg::spScale(sprL, norm); mg::spPos(sprL, V3(S * 335.0f, S * 400.0f, 0));
			sprRh = mg::spNew(0x719); mg::spScale(sprRh, big); mg::spPos(sprRh, V3(S * 450.0f, S * 400.0f, 0)); mg::spSetLoops(sprRh, 0);
			sprR = mg::spNew(0x718); mg::spScale(sprR, norm); mg::spPos(sprR, V3(S * 450.0f, S * 400.0f, 0));
		}
		{	// banners [V] 100057c4..100058fd: Ready (0x84b) and Go (0x84c) at the screen centre, 0x84d / 0x84e created but never drawn
			V3 norm(S, S, 1.0f);
			V3 c(W * 0.5f, H * 0.5f, 0);
			sprBan1 = mg::spNew(0x84b); mg::spScale(sprBan1, norm); mg::spPos(sprBan1, c);
			sprBan2 = mg::spNew(0x84c); mg::spScale(sprBan2, norm); mg::spPos(sprBan2, c);
			V3 p3(c.x - S * 160.0f, c.y - S * 50.0f, 0);
			sprBan3 = mg::spNew(0x84d); mg::spScale(sprBan3, norm); mg::spPos(sprBan3, p3);
			V3 p4(p3.x + S * 200.0f, p3.y + S * 25.0f, 0);
			sprBan4 = mg::spNew(0x84e); mg::spScale(sprBan4, norm); mg::spPos(sprBan4, p4);
		}
		// rules panel lg_intro.ui, text string 6954 (0x1b2a) [V] 10005931..1000598c
		introShown = mg::introLoad("lg__lg_intro", 0x1b2a, [this]{ introConfirm(); }) != nullptr;
		mg::showCursor(true);					// [V] 100059b4
		// the god (art 503): turned 180 degrees about y, at (0, 0, 480) = camera + view direction * 480, scale 1.3, action 1 (loop)
		god = mNew(503);
		mRot(god, V3(0, 1, 0), 180.0f);
		mPos(god, V3(0, 0, 0 + CAM_AT_Z * 480.0f));
		mScale(god, V3(fbits(0x3fa66666), fbits(0x3fa66666), 1.0f));
		mAction(god, 1, 0);
		mg::music(0x18);					// [V] 10005aa6: host +0x108 (0x18, 0)
		setState(2);
		if(getenv("RM8_MG21_AUTOSTART") && atoi(getenv("RM8_MG21_AUTOSTART"))) autostart = true;
		return true;
	}
	bool autostart = false;

	void hudLoad()	// LG21_Hud_LoadSprites @10006760
	{
		V3 sc(S, S, 1.0f);
		static const int bgArt[5] = {0x709, 0x70a, 0x70b, 0x70c, 0x71a};
		for(int i = 0; i < 5; i++){ hudBg[i] = mg::spNew(bgArt[i]); mg::spScale(hudBg[i], sc); }
		float w0, h0, w1, h1, w2, h2, w3, h3;
		mg::spSize(hudBg[0], w0, h0); mg::spSize(hudBg[1], w1, h1); mg::spSize(hudBg[2], w2, h2); mg::spSize(hudBg[3], w3, h3);
		mg::spPos(hudBg[0], V3(w0 * S * 0.5f, h0 * S * 0.5f, 0));		// [V] the four background tiles (512 / 288 wide, 512 / 88 high)
		mg::spPos(hudBg[1], V3((w1 * 0.5f + w0) * S, h1 * S * 0.5f, 0));
		mg::spPos(hudBg[2], V3(w2 * S * 0.5f, (h2 * 0.5f + h0) * S, 0));
		mg::spPos(hudBg[3], V3((w3 * 0.5f + w2) * S, (h3 * 0.5f + h1) * S, 0));
		mg::spPos(hudBg[4], V3(S * fbits(0x4414c000), S * fbits(0x43810000), 0));	// [V] 595, 258
		mg::spSetLoops(hudBg[4], 0); mg::spSetFrame(hudBg[4], 0);
		float cx = W * fbits(0x3f4ccccd) - S * 38.0f + S * 92.0f;			// [V] 100069e8..: the clock plates at (W * 0.8 - 38 s + 92 s, H * 0.2857 + 4 s - 10 s)
		float cy = H * fbits(0x3e924925) + S * 4.0f - S * 10.0f;
		hudPlateW = mg::spNew(0x83f); mg::spScale(hudPlateW, sc); mg::spPos(hudPlateW, V3(cx, cy, 0));
		hudDigPos = V3(cx + S * 40.0f, cy - S * 5.0f, 0);			// [V] 10006a7b..: +0x80
		hudDigW[0] = mg::spNew(0x83e); mg::spScale(hudDigW[0], sc);
		for(int k = 0; k < 9; k++){ hudDigW[k + 1] = mg::spNew(0x835 + k); mg::spScale(hudDigW[k + 1], sc); mg::spPos(hudDigW[k + 1], hudDigPos); }
		hudPlateR = mg::spNew(0x84a); mg::spScale(hudPlateR, sc); mg::spPos(hudPlateR, V3(cx, cy, 0));
		hudDigR[0] = mg::spNew(0x849); mg::spScale(hudDigR[0], sc);
		for(int k = 0; k < 9; k++){ hudDigR[k + 1] = mg::spNew(0x840 + k); mg::spScale(hudDigR[k + 1], sc); mg::spPos(hudDigR[k + 1], hudDigPos); }
	}
	void hudCountdown(bool white, int v)	// ShowCountdownWhite @100066c0 / Red @10006b80 + DrawWhiteDigit @10006660 / DrawRedDigit @100066e0
	{
		mg::spDraw(white ? hudPlateW : hudPlateR);
		int d = v % 10;
		int h = white ? hudDigW[d] : hudDigR[d];
		mg::spPos(h, hudDigPos);
		mg::spDraw(h);
	}

	void barrelInit()	// LG21_Barrel_Init @100013d0 (+ Reset @10001360 values: angle 180, speed 180)
	{
		const V3 pos(BARREL_X, BARREL_Y, BARREL_Z);
		bar.model = mNew(0x711);
		mPos(bar.model, pos);
		for(int k = 0; k < NHOLES / 2; k++){ bar.sword[k] = mNew(0x712 + k); mPos(bar.sword[k], pos); }
		for(int k = 0; k < NHOLES / 2; k++){
			bar.sword[6 + k] = mNew(0x712 + k);
			mRot(bar.sword[6 + k], V3(0, 1, 0), fbits(0x43340000));	// [V] 10001511: the second set is turned 180 degrees about y
			mPos(bar.sword[6 + k], pos);
		}
		for(int k = 0; k < NHOLES; k++) mAction(bar.sword[k], 0, 1);	// [V] 1000154b
		const V3 swordsPos(SW_X, SW_Y, SW_Z);
		bar.swordsIcon = mNew(0x713);				// [V] 1000155b: the "swords left" icon
		mRot(bar.swordsIcon, V3(0, 1, 0), 150.0f);
		mPos(bar.swordsIcon, swordsPos);
		bar.ov1 = mNew(0x713);					// the crossed swords over the pirate's seat (DrawOverlay)
		mRot(bar.ov1, V3(0, 1, 0), 150.0f); mRot(bar.ov1, V3(0, 0, 1), 40.0f); mScale(bar.ov1, V3(fbits(0x3f19999a), fbits(0x3f19999a), 1.0f));
		bar.ov2 = mNew(0x713);
		mRot(bar.ov2, V3(0, 1, 0), 150.0f); mRot(bar.ov2, V3(0, 0, 1), -40.0f); mScale(bar.ov2, V3(fbits(0x3f19999a), fbits(0x3f19999a), 1.0f));
	}

	// ---- update ---------------------------------------------------------------------------------------------------
	void hoverTest()	// the arrow hover block of Update @100049b4..10004a4c (state 4, human turn); state 1 / 9 clear the flags
	{
		float mx, my; mousePos(mx, my);
		if(!pl[cur].ai && state == 4){
			if(mg::spHit(sprL, mx, my)){ hoverL = true; }
			else hoverL = false;
			if(mg::spHit(sprR, mx, my)){ hoverR = true; }
			else hoverR = false;
		}else{ hoverL = false; hoverR = false; }
	}
	bool update(float dt) override
	{
		if(autostart && state == 2){ introConfirm(); autostart = false; }
		if(net){	// [N] N4
			if(state == 2 && ((netIntroT += dt) > 5.0f || netBehind() > 0)) introConfirm();	// [A] the rules panel closes by itself after 5 s (every device runs the game; the turns do not wait for it)
			if(netBehind() >= 2) dt = std::min(0.1f, dt * 2.0f);	// [A] a device that lags behind the referee by two stabs or more catches up at double speed
			if(netBot >= 0 && state == 4 && !pl[cur].ai && !tmo && !netWait && !bar.rotating && netRotOut == 0 && (netBotT += dt) >= netBot){	// test hook RM8_MG21_NETBOT
				netBotT = 0;
				if(!netBotTurned && rnd() % 2){ netBotTurned = true; netRotate(rnd() % 2 != 0); }
				else{
					int c[6], k = 0;
					for(int b = 0; b < 6; b++) if(!bar.clicked[boxToHole(b)]) c[k++] = boxToHole(b);
					if(k){ netSend(RM8M21_STAB, c[rnd() % k]); netWait = true; mg::showCursor(false); }
					else netRotate(true);
				}
			}
		}
		updateQueue(dt);
		t7fc += dt;
		mg::spUpdate(hudBg[4], dt);		// [V] Hud_UpdateAnim @10006530
		if(endFlag) return false;
		fxUpdate(dt);
		switch(state){
		case 1: case 9: case 4:
			if(state == 1) mUpdate(god, dt);	// [V] 1000493d: host +0x78 on the god
			if(state == 1 || state == 9){ fade = fade + dt; if(fade > 1.0f) fade = 1.0f; }	// [V] +0x870
			{
				float mx, my; mousePos(mx, my);
				if(!pl[cur].ai && state == 4){
					if(mg::spHit(sprL, mx, my)){ hoverL = true; if(!btn) mg::spUpdate(sprLh, dt); } else hoverL = false;	// [V] 100049c4..: hit test on the normal arrow, animate the hover one
					if(mg::spHit(sprR, mx, my)){ hoverR = true; if(!btn) mg::spUpdate(sprRh, dt); } else hoverR = false;
					if((float)countdown > timer){	// [V] 10004a4c: the last six seconds tick once each
						if(countdown > 3) snd(0x165);
						else if(countdown > 0) snd(0x166);
						countdown--;
					}
				}else{ hoverL = false; hoverR = false; }
			}
			break;
		case 7:		// [V] 10004ab7: the pirate flies up out of the barrel
		{
			Player &d = pl[defender];
			if(!d.win){
				V3 p(d.pos.x, d.pos.y + 400.0f * S * dt, 500.0f);	// +0x804 = 400 (ctor) * scale * dt
				mPos(d.model, p);
				d.pos = p;
			}
			break;
		}
		case 10:
			fade = dt + dt + fade; if(fade > 1.0f) fade = 1.0f;	// [V] 10004b5d
			break;
		default: break;
		}
		for(int i = 0; i < n; i++) mUpdate(pl[i].model, dt);	// [V] Player_UpdateAnim @10005c00
		tick(dt);
		// Barrel_Update @10001d20
		if(bar.rotating) rotateAnim(dt);
		updateSwords(dt);
		if(net) netPump();	// [N]
		return !endFlag;
	}
	// ---- [N] N4 netplay ----------------------------------------------------------------------------------------------
	int netBehind()	// referee stabs not shown yet
	{
		int k = 0;
		for(const Rm8Mini21Act &a : mg::net21Acts()) if((a.kind == RM8M21_STAB || a.kind == RM8M21_AUTO) && a.turn >= netTurn) k++;
		return k;
	}
	void netSend(int kind, int arg)
	{
		static const bool mute = getenv("RM8_MG21_NETMUTE") != nullptr;	// test hook: the choices of this device never go out (a frozen player: the host's turn time-out decides)
		LOG21("net: send kind %d turn %d arg %d%s", kind, netTurn, arg, mute ? " (muted)" : "");
		if(mg::net21().send && !mute) mg::net21().send(kind, netTurn, arg);
	}
	void netRotate(bool left)	// the local player turns the barrel: one step at a time, shown when the referee's action comes back (every device turns it alike)
	{
		if(netWait || tmo || bar.rotating || netRotOut > 0) return;
		netRotOut++;
		netSend(RM8M21_ROT, left ? 1 : 0);
	}
	void snapAngle(float target)	// the barrel to the referee's angle (only if a turn of the local player was overtaken by the host's decision)
	{
		float d = target - bar.angle;
		const V3 axis(0, 1, 0);
		mPos(bar.model, V3(0, 0, 0)); mRot(bar.model, axis, -d); mPos(bar.model, V3(BARREL_X, BARREL_Y, BARREL_Z));	// model rotation = -(angle change) (rotateAnim / stepAngle)
		for(int k = 0; k < NHOLES; k++){ mPos(bar.sword[k], V3(0, 0, 0)); mRot(bar.sword[k], axis, -d); mPos(bar.sword[k], V3(BARREL_X, BARREL_Y, BARREL_Z)); }
		bar.angle = target;
		LOG21("net: barrel snapped to %.0f", target);
	}
	void netPump()	// show the referee's next action of the current turn
	{
		const std::vector<Rm8Mini21Act> &acts = mg::net21Acts();
		if(state != 4 && state != 11) return;
		if(bar.rotating || netAutoPending) return;
		if(state == 11 && !netReady) return;
		while(netPos < acts.size()){
			const Rm8Mini21Act a = acts[netPos];
			if(a.turn < netTurn){ netPos++; continue; }
			if(a.turn > netTurn) return;
			netPos++;
			if(a.kind == RM8M21_ROT){
				if(netRotOut > 0) netRotOut--;
				setDirection(a.dir != 0); bar.rotating = true;
				LOG21("net: turn %d: barrel %s", a.turn, a.dir ? "left" : "right");
				return;
			}
			netTurn++;
			netWait = false; tmo = true;	// the turn is decided: no more input
			mg::showCursor(false);
			if(ftol(bar.angle) != a.angle) snapAngle((float)a.angle);
			netOutcome = a.outcome;
			pl[cur].target = a.hole; pl[cur].dir = a.dir;
			LOG21("net: turn %d (player %d): %s hole %d -> %d", a.turn, cur, a.kind == RM8M21_AUTO ? "computer" : "stab", a.hole, a.outcome);
			if(a.kind == RM8M21_AUTO && isHidden(a.hole)){ netAutoPending = true; setDirection(a.dir != 0); bar.rotating = true; }	// as computerPlay: the barrel turns until the hole shows, then autoStep stabs
			else doStab(a.hole);
			return;
		}
	}
	void updateSwords(float dt)	// @10001650
	{
		for(int k = 0; k < NHOLES; k++) if(bar.clicked[k]) mUpdate(bar.sword[k], dt);
		float x, y; mousePos(x, y);
		int b = hitTest(x, y);
		if(b != -1) mUpdate(bar.sword[boxToHole(b)], dt);	// the hovered hole's sword advances as well
	}

	// ---- input ---------------------------------------------------------------------------------------------------
	void introConfirm()	// LG21_Intro_OnConfirm @10004540
	{
		if(state != 2) return;	// [A] the DLL has no check (only the panel button and the keys in state 2 reach it)
		mg::introHide(); introShown = false;
		setState(1);
		mg::showCursor(false);
		pushIntro();
	}
	void hiddenKey(int v)	// the hidden sequence of the keys a / d (the code behind each case of KeyDown @10004da0)
	{
		if(seq.empty()) return;
		if(seq.back() == v){ seq.clear(); return; }	// the same direction twice resets
		seq.push_back(v);
	}
	void keyDown(int code) override
	{
		switch(code){
		case ' ':	// @10004ed2
			if(state == 2 && canConfirm){ introConfirm(); canConfirm = false; }
			break;
		case 'a': case RM8MK_LEFT:	// @10004dca
			if(state == 4 && !pl[cur].ai){ if(net) netRotate(true); else { setDirection(true); bar.rotating = true; } }	// [N] netplay: through the referee
			hiddenKey(1);
			break;
		case 'd': case RM8MK_RIGHT:	// @10004e4b
			if(state == 4 && !pl[cur].ai){ if(net) netRotate(false); else { setDirection(false); bar.rotating = true; } }
			hiddenKey(2);
			break;
		case RM8MK_ENTER:	// @10004efe
			if(state == 2 && canConfirm){ introConfirm(); canConfirm = false; }
			if(seq.empty()){ seq.push_back(0); break; }
			if((int)seq.size() == 11){
				force82c = true;
				if(seq.back() == 2) force1c = 1;
				else if(seq.back() == 1) force1c = 2;
			}else seq.clear();
			break;
		default: break;
		}
	}
	void mouseButton(int flags) override
	{
		if(flags & MG_MOUSE_LDOWN){	// @100029a0
			btn = true;
			// [A] a touch has no hover: the arrows are tested at the tap position first (the DLL relies on the hover flags of the last Update)
			if(state == 4 && !pl[cur].ai && !tmo){	// [V] 100029af..: only in state 4, on a human turn that has not timed out
				hoverTest();
				if(net){	// [N] N4: the choice goes to the referee; the stab / the barrel turn is shown when its action comes back (on every device alike)
					if(netWait || bar.rotating || netRotOut > 0) return;
					float x, y; mousePos(x, y);
					int b = hitTest(x, y);
					if(b != -1){
						int h = boxToHole(b);
						if(!bar.clicked[h]){ netSend(RM8M21_STAB, h); netWait = true; mg::showCursor(false); }
						return;
					}
					if(hoverL){ netRotate(true); snd(0x168); }
					else if(hoverR){ netRotate(false); snd(0x168); }
					return;
				}
				onMouseClick();
				if(hoverL){ setDirection(true); bar.rotating = true; snd(0x168); }	// [V] 100029ed: left arrow: SetDirection(1), SetRotating(1), sound 0x168
				else if(hoverR){ setDirection(false); bar.rotating = true; snd(0x168); }	// [V] 10002a28
			}
		}else if(flags & MG_MOUSE_LUP) btn = false;
	}

	// ---- draw ----------------------------------------------------------------------------------------------------
	void drawPlayer(Player &p)	// LG21_Player_Draw @10005b20
	{
		aTest();
		mDraw(p.model);
		portraitFallback(p);
		float x = p.hx, y = p.hy, w = p.hw, h = p.hh;
		uint32_t col = 0xffffffffu;
		if(p.active){
			col = 0xffff9696u;			// [V] 10005b59: r 0xff, g = b = 0x96 (+0x1c = 0xff, +0x1d = +0x1e = 0x96)
			x = x - w * fbits(0x3dcccccd) * 1.0f;	// double 0.1 (100322b0)
			y = y - h * fbits(0x3dcccccd) * 1.0f;
		}else{
			w = w * 0.8f; h = h * 0.8f;		// double 0.8 (100322a8)
		}
		mg::drawTexQuad(p.portrait, x, y, w, h, col);
	}
	void portraitFallback(Player &p)	// [A] without role models the player is a portrait billboard at its projected position
	{
		if(p.model >= 0) return;
		lm::Mdl *m = lm::get(p.model);
		if(!m || !m->fallback) return;
		float sx, sy; if(!mg::project(V3((float)m->lastPos.x, (float)m->lastPos.y, (float)m->lastPos.z), sx, sy)) return;
		float sz = 110.0f * 440.0f / std::max(1.0f, m->lastPos.z);
		mg::drawTexQuad(p.portrait, sx - sz / 2, sy - sz, sz, sz, 0xffffffffu);
	}
	void drawSwords(){ for(int k = 0; k < NHOLES; k++) if(bar.clicked[k]) mDraw(bar.sword[k]); }	// the swords of the holes already stabbed
	void drawBarrelFront()	// DrawFront @10001730
	{
		aTest();
		mDraw(bar.model);
		if(state == 4){
			drawSwords();
			if(!pl[cur].ai){
				float x, y; mousePos(x, y);
				int b = hitTest(x, y);
				if(b != -1) mDraw(bar.sword[boxToHole(b)]);	// the sword of the hole under the pointer
			}
		}
		drawSwords();
		// DrawSwordsLeft @10001030
		aTest();
		for(int k = 0; k < bar.remaining; k++){
			mPos(bar.swordsIcon, V3(SW_X, SW_Y - (float)(15 * k), SW_Z));
			mDraw(bar.swordsIcon);
		}
	}
	void drawOverlay()	// u_LG21_Barrel_DrawOverlay @10001860
	{
		aTest();
		if(defender == -1) return;
		float x = -400.0f;
		switch(defender){ case 0: x = 120.0f; break; case 1: x = 90.0f; break; case 2: x = 60.0f; break; case 3: x = 30.0f; break; default: break; }
		if(state == 1) return;
		mPos(bar.ov1, V3(x, 63.0f, 500.0f)); mDraw(bar.ov1);
		mPos(bar.ov2, V3(x - 20.0f, 63.0f + 33.0f, 500.0f)); mDraw(bar.ov2);
	}
	void draw() override
	{
		if(endFlag) return;
		aTest(); depth2D();	// the alpha test (192) set by the last frame's draw is still in force in the DLL
		mg::alphaTest(0);	// [A] the four opaque background tiles are drawn without the alpha test: GL samples the tile edge rows bilinearly (D3D9 is texel aligned), which would cut seams
		for(int i = 0; i < 4; i++) mg::spDraw(hudBg[i]);	// [V] Hud_DrawStatic @10006550 (the five sprites in this order)
		aTest();
		mg::spDraw(hudBg[4]);
		zClear();
		for(int i = 0; i < n; i++) drawPlayer(pl[i]);
		if(state == 4){
			if(!pl[cur].ai){
				if(timer >= 3.0f) hudCountdown(true, ftol(timer));	// [V] 10004c99: white from 3 s on, red below
				else hudCountdown(false, ftol(timer));
			}
		}else if(state == 9) state9Draw();
		if(fxOn) mg::spDraw(fxSprite);			// Fx_DrawIfActive @10006bf0
		drawBarrelFront();
		if(state != 1 && state != 2 && state != 9 && state != 3){
			mg::spDraw(hoverL ? sprLh : sprL);
			mg::spDraw(hoverR ? sprRh : sprR);
		}
		if(state == 2) zClear();
		else if(state == 1 && timer > 9.5f) zClear();
		aTest();
		mDraw(god);
		zClear();
		drawOverlay();
	}

	// ---- result --------------------------------------------------------------------------------------------------
	void getResult(Rm8MiniResult &o) override	// LG21_GetResult @100031d0
	{
		aTest();
		stopSounds();
		o.count = n;
		for(int i = 0; i < n; i++) o.p[i].flagB = 0;
		for(int i = 0; i < n; i++){
			Rm8MiniPlayerResult &r = o.p[i];
			r.seat = pl[i].id;
			if(pl[i].win){
				if(i == defender){
					o.p[defender].score = n * 30 - 30;	// [V] 10003264: (count * 30) - 30
					o.p[defender].rating = 2;
				}else{
					r.score = 30;				// [V] 1000328a
					if(firstStab()) r.flagB = 1;		// [V] IsFirstStab @10001310
					o.p[defender].rating = 2;
				}
			}else if(pl[i].lose){
				r.score = -30;					// [V] 100032d1
				if(i == defender){ r.flagA = 1; r.rating = 0; }
			}else r.score = 0;
		}
		for(int i = 0; i < n; i++) LOG21("result %d: seat %d score %d flagA %d rating %d flagB %d", i, o.p[i].seat, o.p[i].score, o.p[i].flagA, o.p[i].rating, o.p[i].flagB);
	}
	~G21() override { stopSounds(); lm::freeAll(); }
};

}	// namespace

MgGame *mgNew21(){ return new G21; }
