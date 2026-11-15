// Cutscene engine, see movie.h and docs/story_mode.md section 2. [V] = verified in the decompile (address), [M] meaning inferred, [H] hypothesis, [A] port adaptation.
#include "movie.h"
#include "audio.h"
#include "ui.h"
#include "present.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
#if __has_include(<SDL.h>)
#include <SDL.h>
#define RM8_MOVIE_SDL 1
#endif

using namespace rw;

namespace {

// ======================================================================== data
struct MArt { std::string dff, texdir, uva; std::map<int, std::string> anims; };
std::map<int, MArt> g_arts;			// scenes/movie_arts.txt (role.bin records of the arts the scripts create)
// paper dolls: roles.txt role / model / anim / face lines (the same data roles.cpp uses)
struct FaceKey { float t; std::string part, tex; };
struct DArt { std::string dff, texdir; std::map<int, std::string> anims; std::map<int, std::vector<FaceKey>> faces; };
struct DRole { int id = 0; float scale[3] = {1, 1, 1}, speed[3] = {100, 100, 100}; int art[12] = {}; };
std::map<int, DArt> g_dart;
std::map<int, DRole> g_drole;				// roleinfo Id -> data
bool g_inited = false;

// librw has no setCurrentTime: restarting the animation puts the time at 0
void seekTime(AnimInterpolator *ip, float t){ Animation *a = ip->currentAnim; if(!a) return; ip->setCurrentAnim(a); if(t > 0) ip->addTime(t); }
float waitScale(){ return 1.0f - std::min((rm8GameSpeed - 1) / 10.0f, 1.0f); }	// [V] ScaledWait @0x47b3b0
float scaledWait(float s){ return s * waitScale(); }

// ======================================================================== objects
struct Obj {
	int h = 0;
	bool doll = false;
	// article (clump in the scene) -------------------------------------------------
	int art = 0; Clump *clump = nullptr; int fx = 0;	// fx: an effect article of present.cpp (particles, 6000..6399) instead of a clump
	Matrix base; V3d p0 = {0, 0, 0};
	float pos[3] = {0, 0, 0}, target[3] = {0, 0, 0}, speed = 100, scale[3] = {1, 1, 1}, rot = 180, radius = 0;	// rot = the original's +0x3C angle (180 at creation)
	bool animated = false; std::string animKey;		// the clump has an animation entry in the scene
	int animIdx = -1, count = 0; float elapsed = 0, dur = 0; bool held = false;
	// paper doll -------------------------------------------------------------------
	struct Model { int art = 0; Clump *clump = nullptr; HAnimHierarchy *hier = nullptr; Matrix base; V3d p0 = {0, 0, 0}; std::map<int, Animation*> anims; std::map<std::string, std::vector<Material*>> parts; } m[4];
	const DRole *role = nullptr;
	int cur = 0, anim = 2; bool once = false; std::string face;
	bool spinning = false; int spinTarget = 0; float spinAngle = 0, spinRate = 0, yaw = 0;
	// the doll's scale: roleinfo +0x338 at creation (Mesh_AccumulateScale in Anim_PlayMovieClip @0x47bc10), RoleSetScale REPLACES it: [V] Mesh_SetScale @0x47a9e0 divides the target by the recorded
	// scale (+0x10) and accumulates the ratio = an absolute scale (checked against the user's screenshot of the original ending stage, da17_2: the foreground heroes are small at the bottom edge)
	// bubble -----------------------------------------------------------------------
	bool speaking = false; int slot = -1;
};

Rm8Scene *g_sc = nullptr; Camera *g_cam = nullptr;
std::vector<Obj*> g_objs;
std::vector<Light*> g_lights;
int g_nextH = 1;
float g_camYaw = 0;			// the b of SetCameraPos [V] camera wrapper +0xC (the dolls turn by it)

struct Seg {
	bool active = false; std::string name; float timer = 0; int steps = 0; bool quit = false;
} g_seg;
std::vector<std::string> g_queue;	// scripts still to play (front first)
std::function<void()> g_done;
bool g_active = false;
float g_total = 0, g_lastSkip = -10;
Rm8Ui *g_aside = nullptr, *g_bubbles = nullptr;

Obj *find(int h){ for(Obj *o : g_objs) if(o->h == h) return o; return nullptr; }

// ---- paper doll models (copy of roles.cpp's loader; the movie has no seat colour: colour 0)
Texture *faceTex(const std::string &key){ static std::map<std::string, Texture*> c; auto it = c.find(key); if(it != c.end()) return it->second; Texture *t = rm8PackTexture(key); return c[key] = t; }
void setPart(Obj::Model &m, const std::string &part, Texture *t)
{
	if(!t) return;
	auto it = m.parts.find(part);
	if(it == m.parts.end()) return;
	for(Material *mat : it->second) mat->setTexture(t);
}
bool loadDollModel(Obj::Model &m, int art, int charId, bool front)
{
	auto it = g_dart.find(art);
	if(it == g_dart.end()) return false;
	m.art = art;
	m.clump = rm8LoadClump(it->second.dff, it->second.texdir);
	if(!m.clump) return false;
	m.base = m.clump->getFrame()->matrix; m.base.flags = 0; m.p0 = m.base.pos;
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
	char key[160]; const char *fb = front ? "f" : "b";
	snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_body_%s_%02d.tga", charId, charId, fb, 0);
	setPart(m, "body", faceTex(key));
	const char *extra = (charId == 2 || charId == 15) ? "other" : (charId == 4 || charId == 9 || charId == 11 || charId == 13) ? "hair" : (charId == 7 || charId == 10) ? "toujin" : nullptr;
	if(extra){ snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_%s_%s_%02d.tga", charId, charId, extra, fb, 0); setPart(m, extra, faceTex(key)); }
	g_sc->world->addClump(m.clump);
	return true;
}

void dollPlay(Obj &o, int anim, int count)
{
	o.anim = anim; o.once = count == 1; o.count = count; o.elapsed = 0; o.held = false; o.face.clear();
	Obj::Model &m = o.m[o.cur];
	auto it = m.anims.find(anim);
	if(!m.hier || it == m.anims.end() || !it->second) return;
	m.hier->interpolator->setCurrentAnim(it->second);
	o.dur = it->second->duration;
}
void dollFace(Obj &o, float t)	// [V] face texture swaps of role.bin sub-list 3 (applyFace in roles.cpp)
{
	Obj::Model &m = o.m[o.cur];
	auto ai = g_dart.find(m.art);
	if(ai == g_dart.end()) return;
	auto it = ai->second.faces.find(o.anim);
	if(it == ai->second.faces.end()) return;
	const FaceKey *k = nullptr;
	for(const FaceKey &f : it->second) if(f.t <= t + 1e-4f && (!k || f.t >= k->t)) k = &f;
	if(!k || k->tex == o.face) return;
	setPart(m, k->part, faceTex(k->tex));
	o.face = k->tex;
}
void dollSelect(Obj &o, int idx){ o.cur = idx; int a = o.anim, c = o.count; dollPlay(o, a, c); }	// [V] Mesh_SelectItem @0x47adf0: the animation restarts on the new model
// [V] Path_SelectTarget @0x47bea0 (dir, mode): adjacent models turn (Mesh_UpdateRotation @0x47bcd0: 540 deg/s * speed / 100, the model swaps at 90 deg),
// others (and mode 1 = RoleFace) switch at once
void dollSetDir(Obj &o, int idx, bool spin)
{
	idx &= 3;
	if(idx == o.cur) return;
	if(!spin || (idx % 2 != o.cur % 2 && idx / 2 != o.cur / 2)){ dollSelect(o, idx); return; }
	o.spinTarget = idx; o.spinning = true; o.spinAngle = 0;
	o.spinRate = (o.speed / 100.0f) * ((idx - o.cur == 1 || idx - o.cur == -3) ? 540.0f : -540.0f);
}
bool dollSpin(Obj &o, float dt)
{
	if(!o.spinning) return false;
	float d = dt * o.spinRate;
	o.spinAngle += d;
	if(o.spinTarget == o.cur){
		if(fabsf(o.spinAngle) >= 360.0f){ float over = fabsf(o.spinAngle) - 360.0f; d = d <= 0 ? over + d : d - over; o.spinning = false; }
	}else if(fabsf(o.spinAngle) >= 90.0f){
		if(o.spinRate <= 0){ d -= 180.0f; o.spinAngle -= 180.0f; } else { d += 180.0f; o.spinAngle += 180.0f; }
		dollSelect(o, o.spinTarget);
	}
	o.yaw = fmodf(o.yaw + d, 360.0f);
	return true;
}
void dollPlace(Obj &o)	// [V] Anim_PlayMovieClip @0x47bc10: scale = roleinfo scale x the script's, yaw = camera yaw - 180 (+ the spin), pivot = the clump frame position
{
	Obj::Model &m = o.m[o.cur];
	if(!m.clump) return;
	Frame *f = m.clump->getFrame();
	V3d np0 = {-m.p0.x, -m.p0.y, -m.p0.z}, s = {o.scale[0], o.scale[1], o.scale[2]}, ay = {0, 1, 0};
	V3d p1 = {m.p0.x + o.pos[0], m.p0.y + o.pos[1], m.p0.z + o.pos[2]};
	f->transform(&m.base, COMBINEREPLACE);
	f->translate(&np0, COMBINEPOSTCONCAT); f->scale(&s, COMBINEPOSTCONCAT);
	f->rotate(&ay, g_camYaw - 180.0f + o.yaw, COMBINEPOSTCONCAT); f->translate(&p1, COMBINEPOSTCONCAT);
}
void dollAnimate(Obj &o, float dt)
{
	Obj::Model &m = o.m[o.cur];
	if(!m.hier || !m.hier->interpolator->currentAnim) return;
	AnimInterpolator *ip = m.hier->interpolator;
	float step = dt;
	if(o.once || o.held) step = std::min(step, std::max(0.0f, ip->currentAnim->duration - ip->currentTime - 1e-4f));
	else if(o.count > 1){	// [H] count n: n plays, then the last frame stays (the original's meaning of the third RoleSetAnim argument is not read in detail)
		o.elapsed += dt;
		if(o.elapsed >= o.count * ip->currentAnim->duration){ o.held = true; seekTime(ip, ip->currentAnim->duration - 1e-4f); step = 0; }
	}
	ip->addTime(step);
	for(int i = 0; i < m.hier->numNodes; i++){
		Frame *f = m.hier->nodeInfo[i].frame;
		if(!f) continue;
		ip->applyCB(&f->matrix, ip->getInterpFrame(i));
		f->updateObjects();
	}
	m.hier->updateMatrices();
	dollFace(o, ip->currentTime);
}

// ---- articles
// [V] FUN_0043cb90 as in rm8SceneAddArticle: T(-P0), S, R_y, T(P0 + pos) on the clump frame; the rotation of the original is -(angle) (CArticle_SetRotationAngle @0x512380)
void artPlace(Obj &o)
{
	if(!o.clump) return;
	Frame *f = o.clump->getFrame();
	V3d np0 = {-o.p0.x, -o.p0.y, -o.p0.z}, sc = {o.scale[0], o.scale[1], o.scale[2]}, ay = {0, 1, 0};
	V3d p1 = {o.p0.x + o.pos[0], o.p0.y + o.pos[1], o.p0.z + o.pos[2]};
	f->transform(&o.base, COMBINEREPLACE);
	f->translate(&np0, COMBINEPOSTCONCAT); f->scale(&sc, COMBINEPOSTCONCAT);
	f->rotate(&ay, -o.rot, COMBINEPOSTCONCAT); f->translate(&p1, COMBINEPOSTCONCAT);
}
// union radius of the atomics' spheres at the clump's own frame (scale 1): the bubble sits at y + 2.2 * radius * scale [V @0x512150]
float clumpRadius(Clump *c)
{
	std::vector<V3d> ctr; std::vector<float> rad;
	FORLIST(lnk, c->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		if(!a->geometry || a->geometry->numMorphTargets < 1) continue;
		Sphere s = a->geometry->morphTargets[0].boundingSphere;
		const Matrix *m = a->getFrame()->getLTM();
		V3d w = {s.center.x * m->right.x + s.center.y * m->up.x + s.center.z * m->at.x + m->pos.x,
		         s.center.x * m->right.y + s.center.y * m->up.y + s.center.z * m->at.y + m->pos.y,
		         s.center.x * m->right.z + s.center.y * m->up.z + s.center.z * m->at.z + m->pos.z};
		ctr.push_back(w); rad.push_back(s.radius * sqrtf(m->right.x * m->right.x + m->right.y * m->right.y + m->right.z * m->right.z));
	}
	if(ctr.empty()) return 0;
	V3d mid = {0, 0, 0};
	for(const V3d &v : ctr){ mid.x += v.x; mid.y += v.y; mid.z += v.z; }
	mid.x /= ctr.size(); mid.y /= ctr.size(); mid.z /= ctr.size();
	float r = 0;
	for(size_t i = 0; i < ctr.size(); i++) r = std::max(r, sqrtf((ctr[i].x - mid.x) * (ctr[i].x - mid.x) + (ctr[i].y - mid.y) * (ctr[i].y - mid.y) + (ctr[i].z - mid.z) * (ctr[i].z - mid.z)) + rad[i]);
	return r;
}

// the animation entry of the clump in the scene
Rm8ArtAnim *artAnimEntry(Obj &o)
{
	if(!g_sc || !o.clump) return nullptr;
	for(Rm8ArtAnim &aa : g_sc->anims) if(aa.clump == o.clump) return &aa;
	return nullptr;
}

void bubbleRelease(Obj &o);
void freeObj(Obj *o)
{
	bubbleRelease(*o);
	if(o->doll){
		for(auto &m : o->m) if(m.clump){ g_sc->world->removeClump(m.clump); m.clump->destroy(); m.clump = nullptr; }
	}else{
		if(o->fx) rm8FxStop(o->fx);
		else if(o->clump) rm8SceneRemoveArticle(*g_sc, o->clump);
	}
	delete o;
}

// ---- bubbles / subtitles
Rm8Ui *bubbleUi(){ if(!g_bubbles){ g_bubbles = rm8UiCreateCanvas("mbubble", RM8UI_HUD); if(g_bubbles) rm8UiShow(g_bubbles, true); } return g_bubbles; }
struct BubbleSlot { int plane = 0, text = 0, shadow = 0; bool used = false; };
std::vector<BubbleSlot> g_pool;		// widgets of the bubbles, reused between objects and scripts
void bubbleRelease(Obj &o)
{
	if(o.slot >= 0 && o.slot < (int)g_pool.size()){ g_pool[o.slot].used = false; if(g_bubbles) rm8UiSetVisible(g_bubbles, g_pool[o.slot].plane, false); }
	o.slot = -1; o.speaking = false;
}
void bubbleSet(Obj &o, int strId, bool on)
{
	Rm8Ui *u = bubbleUi(); if(!u) return;
	if(!on){ bubbleRelease(o); return; }
	if(o.slot < 0){
		for(size_t i = 0; i < g_pool.size(); i++) if(!g_pool[i].used){ o.slot = (int)i; break; }
		if(o.slot < 0){	// [V] movieWord.ui: Plane 256x128 picture 1087, label "word" (15,10,227,108) white over "word2" (16,11) as the shadow
			BubbleSlot b;
			b.plane = rm8UiAddWidget(u, "plane", 0, 0, 0, 256, 128, 1087);
			b.shadow = rm8UiAddWidget(u, "label", b.plane, 16, 11, 227, 108, -1, 0, 16, 1);
			b.text = rm8UiAddWidget(u, "label", b.plane, 15, 10, 227, 108, -1, 0, 16, 2);
			char nm[32];
			snprintf(nm, sizeof nm, "dyn%d", b.shadow); rm8UiSetColorN(u, nm, 0xff000000); rm8UiSetAlignN(u, nm, 0, 1);
			snprintf(nm, sizeof nm, "dyn%d", b.text); rm8UiSetColorN(u, nm, 0xffffffff); rm8UiSetAlignN(u, nm, 0, 1);
			g_pool.push_back(b); o.slot = (int)g_pool.size() - 1;
		}
		g_pool[o.slot].used = true;
	}
	BubbleSlot &b = g_pool[o.slot];
	std::string t = rm8UiWrapText(rm8Str(strId), 227.0f, 16);
	rm8UiSetText(u, b.text, t); rm8UiSetText(u, b.shadow, t);
	rm8UiSetVisible(u, b.plane, true);
	o.speaking = true;
}
bool project(Camera *cam, const float p[3], float &sx, float &sy)	// RW cameras have right pointing to screen left (present.cpp)
{
	const Matrix *m = cam->getFrame()->getLTM();
	V3d d = {p[0] - m->pos.x, p[1] - m->pos.y, p[2] - m->pos.z};
	float xc = d.x * m->right.x + d.y * m->right.y + d.z * m->right.z;
	float yc = d.x * m->up.x + d.y * m->up.y + d.z * m->up.z;
	float zc = d.x * m->at.x + d.y * m->at.y + d.z * m->at.z;
	if(zc < 1.0f) return false;
	float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height;
	sx = (0.5f - xc / (2.0f * cam->viewWindow.x * zc)) * W;
	sy = (0.5f - yc / (2.0f * cam->viewWindow.y * zc)) * H;
	return true;
}
// [A] the bar is its own root (Top 640): a dialog-mode centred box (the HUD mode would put the root at the screen origin)
Rm8Ui *asideUi(){ if(!g_aside){ g_aside = rm8UiLoad("SpeakAside", RM8UI_DIALOG); } return g_aside; }

// ======================================================================== segments
void clearSegmentObjects()
{
	for(Obj *o : g_objs) freeObj(o);
	g_objs.clear();
	if(Rm8Ui *u = asideUi()) rm8UiShow(u, false);
	rm8FxStopAll();
}

void endSegment()	// [V] Movie_ReleaseAndRestoreView @0x515960: samples stopped (the music goes on), camera restored
{
	if(!g_seg.active) return;
	rm8PyMovieClose();
	clearSegmentObjects();
	rm8SndStopAll();
	g_seg = Seg();
	g_camYaw = 0;
}

void finishAll()
{
	endSegment();
	rm8SndStopAll(); rm8MusicStop();	// [V] Movie_ShutdownEngine @0x515850
	for(Light *l : g_lights){ Frame *f = l->getFrame(); g_sc->world->removeLight(l); l->destroy(); if(f) f->destroy(); }
	g_sc->lights.erase(std::remove_if(g_sc->lights.begin(), g_sc->lights.end(), [](Light *l){ return std::find(g_lights.begin(), g_lights.end(), l) != g_lights.end(); }), g_sc->lights.end());
	g_lights.clear();
	rm8PresentSetScene(nullptr);
	rm8UiHideAll();
	g_active = false;
	auto done = g_done; g_done = nullptr;
	g_sc = nullptr; g_cam = nullptr;
	if(done) done();
}

void startNext()
{
	g_seg = Seg();
	g_seg.name = g_queue.front(); g_queue.erase(g_queue.begin());
	g_seg.timer = scaledWait(1.6f);	// [V] SpeakAside_InitPython @0x5141b0: the first next() comes ScaledWait(1.6) s after the script was loaded
	g_seg.active = true;
	if(!rm8PyMovieOpen(g_seg.name.c_str())){ rm8Log("movie: cannot open script %s", g_seg.name.c_str()); g_seg.quit = true; }
	rm8Log("movie: script %s starts", g_seg.name.c_str());
}

}	// namespace

// ======================================================================== public
bool rm8MovieInit(const std::string &root)
{
	g_arts.clear(); g_dart.clear(); g_drole.clear();
	std::ifstream f(root + "/movie_arts.txt");
	std::string line;
	while(f && std::getline(f, line)){
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw != "mart") continue;
		int id; MArt a; std::string tok;
		ss >> id >> a.dff >> a.texdir >> a.uva;
		while(ss >> tok){ size_t c = tok.find(':'); if(c != std::string::npos) a.anims[atoi(tok.c_str())] = tok.substr(c + 1); }
		g_arts[id] = a;
	}
	std::ifstream r(root + "/roles.txt");
	while(r && std::getline(r, line)){
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "role"){
			DRole d; float off[3]; int dice[9]; int sex; std::string pic, ai, name;
			ss >> d.id; for(float &v : d.scale) ss >> v; for(float &v : off) ss >> v; for(float &v : d.speed) ss >> v; for(int &a : d.art) ss >> a;
			g_drole[d.id] = d; (void)dice; (void)sex;
		}else if(kw == "model"){ int a; ss >> a; ss >> g_dart[a].dff >> g_dart[a].texdir; }
		else if(kw == "anim"){ int a, i; std::string p; ss >> a >> i >> p; g_dart[a].anims[i] = p; }
		else if(kw == "face"){ int a, i; FaceKey k; ss >> a >> i >> k.t >> k.part >> k.tex; g_dart[a].faces[i].push_back(k); }
	}
	rm8PrtLoadDefs(root + "/particles.txt");	// the particle arts (6120..6124 snow / sparkles of the scripts): normally loaded with the first map
	g_inited = !g_arts.empty() && !g_drole.empty();
	rm8Log("movie: %d arts, %d paper-doll roles", (int)g_arts.size(), (int)g_drole.size());
	return g_inited;
}
bool rm8MovieAvailable(){ return g_inited; }
bool rm8MovieActive(){ return g_active; }
float rm8MovieElapsed(){ return g_total; }
const char *rm8MovieCurrent(){ return g_seg.active ? g_seg.name.c_str() : ""; }

bool rm8MovieStart(Rm8Scene &sc, Camera *cam, const std::vector<std::string> &scripts, std::function<void()> onDone)
{
	if(g_active) rm8MovieAbort();
	if(!g_inited || scripts.empty() || !sc.world || !cam){ if(onDone) onDone(); return false; }
	g_sc = &sc; g_cam = cam; g_queue = scripts; g_done = onDone; g_active = true; g_total = 0; g_lastSkip = -10;
	rm8UiHideAll();
	rm8MusicStop();	// [V] Movie_InitEngine @0x515780: CDigitalAudio_StopMusic(0)
	rm8PresentSetScene(&sc);
	// [A] default lights (the original's stage lighting is not read: librw defaults as in rm8_view)
	Light *amb = Light::create(Light::AMBIENT); amb->setColor(0.75f, 0.75f, 0.75f); sc.world->addLight(amb); sc.lights.push_back(amb); g_lights.push_back(amb);
	Light *dir = Light::create(Light::DIRECTIONAL); dir->setColor(0.5f, 0.5f, 0.5f);
	Frame *df = Frame::create(); V3d ax = {1, 0, 0}; df->rotate(&ax, 40.0f, COMBINEREPLACE); dir->setFrame(df);
	sc.world->addLight(dir); sc.lights.push_back(dir); g_lights.push_back(dir);
	cam->setNearPlane(58.0f); cam->setFarPlane(5800.0f);	// [V] the game camera: near 58, far 5800 (scripts/game_camera.py)
	rm8MvSetCamera(0, 0, 0, 0, 0);
	startNext();
	return true;
}

void rm8MovieAbort()
{
	if(!g_active) return;
	g_done = nullptr; g_queue.clear();
	finishAll();
}

static void skipNow()
{
	if(!g_active || !g_seg.active || g_total - g_lastSkip < 0.5f) return;
	g_lastSkip = g_total;
	rm8Log("movie: %s skipped at %.1f s", g_seg.name.c_str(), g_total);
	endSegment();
	if(g_queue.empty()) finishAll(); else startNext();
}

void rm8MovieSkip()
{
	static const bool noSkip = getenv("RM8_MOVIE_NOSKIP") != nullptr;	// test hook: stray window events must not end a scripted test run
	if(!noSkip) skipNow();
}

void rm8MovieSkipAll()
{
	if(!g_active) return;
	g_queue.clear();
	g_lastSkip = -10;
	skipNow();
}

void rm8MovieUpdate(float dt)
{
	if(!g_active) return;
	static const float testSpeed = getenv("RM8_MOVIE_SPEED") ? (float)atof(getenv("RM8_MOVIE_SPEED")) : 1.0f;	// test hook: run the clock faster
	dt *= testSpeed;
	g_total += dt;
	if(!g_seg.active){ if(g_queue.empty()) finishAll(); else startNext(); return; }
	// the script object's per-frame update [V] CUi_SpeakAside_vf1 @0x514a60
	for(Obj *o : g_objs){
		if(o->doll){
			float d[3] = {o->target[0] - o->pos[0], o->target[1] - o->pos[1], o->target[2] - o->pos[2]};
			float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), step = o->speed * dt;	// [V] Mesh_MoveTowardTarget @0x511d20: straight line at +0x34 units / s
			if(L > 0){ float k = L <= step ? 1.0f : step / L; for(int i = 0; i < 3; i++) o->pos[i] += d[i] * k; }
			dollSpin(*o, dt);
			dollAnimate(*o, dt);
			dollPlace(*o);
		}else if(o->clump){
			float d[3] = {o->target[0] - o->pos[0], o->target[1] - o->pos[1], o->target[2] - o->pos[2]};
			float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), step = o->speed * dt;	// [V] SpeakAside_MoveArticleToTarget @0x512150 (+0x38, default 100)
			if(L > 0){ float k = L <= step ? 1.0f : step / L; for(int i = 0; i < 3; i++) o->pos[i] += d[i] * k; artPlace(*o); }
			if(o->animated && o->count > 1 && !o->held){	// [H] n plays, then the last frame stays
				o->elapsed += dt;
				if(o->elapsed >= o->count * o->dur){
					if(Rm8ArtAnim *aa = artAnimEntry(*o)){ aa->once = true; seekTime(aa->hier->interpolator, aa->hier->interpolator->currentAnim->duration - 1e-4f); }
					o->held = true;
				}
			}
		}
	}
	rm8UpdateScene(*g_sc, dt);	// article animations, UV animations, particles (rm8FxPlay effects)
	rm8PresentUpdate(dt);		// the effect players' lifetimes
	if(g_seg.quit){ endSegment(); if(g_queue.empty()) finishAll(); else startNext(); return; }	// [V] QuitMovie: +0x38 = 0x80000000, the manager moves on
	g_seg.timer -= dt;
	if(g_seg.timer <= 0){
		g_seg.timer = scaledWait(1.6f);	// [V] CUi_SpeakAside_vf1: reset, then next(); a CTime.Wait inside overrides it
		int r = rm8PyMovieStep();
		g_seg.steps++;
		if(r != 1 || g_seg.steps >= 100) g_seg.quit = true;	// [V] StopIteration / error / 100 resumes end the script (the original keeps calling a finished generator)
	}
}

void rm8MovieRender(Camera *cam)
{
	if(!g_active || !g_sc) return;
	// [V] CUi_SpeakAside_vf2 @0x514840: dolls (alpha test >= 196), then the articles, effects; [A] the two picture quads are not drawn (docs 2.3)
	uint32 fn = GetRenderState(ALPHATESTFUNC), ref = GetRenderState(ALPHATESTREF);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196);
	for(Obj *o : g_objs) if(o->doll && o->m[o->cur].clump) o->m[o->cur].clump->render();
	SetRenderState(ALPHATESTFUNC, fn); SetRenderState(ALPHATESTREF, ref);
	rm8RenderArticles(*g_sc);
	rm8PrtRender(cam);
	// bubbles above the heads / article tops
	if(g_bubbles){
		float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height, k = H / 768.0f;
		for(Obj *o : g_objs){
			if(!o->speaking || o->slot < 0) continue;
			BubbleSlot &bs = g_pool[o->slot];
			float top = o->doll ? 105.0f * (o->scale[1] / 1.6f) : 2.2f * o->radius * o->scale[1];	// [V] articles 2.2 x radius x scale (@0x512150); [H] dolls like present.cpp
			const V3d &q0 = o->doll ? o->m[o->cur].p0 : o->p0;
			float p[3] = {q0.x + o->pos[0], q0.y + o->pos[1] + top, q0.z + o->pos[2]}, sx, sy;
			if(project(cam, p, sx, sy)){
				float bx = sx - 128.0f * k, by = sy - 128.0f * k;
				bx = std::max(0.0f, std::min(bx, W - 256.0f * k)); by = std::max(0.0f, by);
				rm8UiSetRect(g_bubbles, bs.plane, bx * 1024.0f / W, by / k, 256.0f, 128.0f);
				rm8UiSetVisible(g_bubbles, bs.plane, true);
			}else rm8UiSetVisible(g_bubbles, bs.plane, false);
		}
	}
	rm8UiRender(cam);
}

#ifdef RM8_MOVIE_SDL
void rm8MovieTestHooks(int w, int h)
{
	static const char *shots = getenv("RM8_MOVIE_SHOTS"), *skips = getenv("RM8_MOVIE_SKIP");
	static std::vector<float> shotT, skipT; static size_t shotNext = 0, skipNext = 0; static bool parsed = false;
	if(!parsed){
		parsed = true;
		auto parse = [](const char *s, std::vector<float> &v){ while(s && *s){ v.push_back((float)atof(s)); const char *c = strchr(s, ','); s = c ? c + 1 : nullptr; } };
		parse(shots, shotT); parse(skips, skipT);
	}
	if(skipNext < skipT.size() && g_total >= skipT[skipNext]){ skipNext++; skipNow(); }
	if(shotNext >= shotT.size() || g_total < shotT[shotNext]) return;
	float t = shotT[shotNext++];
	typedef void (*ReadPixelsFn)(int, int, int, int, unsigned, unsigned, void*);
	ReadPixelsFn rp = (ReadPixelsFn)SDL_GL_GetProcAddress("glReadPixels");
	if(!rp) return;
	std::vector<unsigned char> px((size_t)w * h * 4);
	rp(0, 0, w, h, 0x1908, 0x1401, px.data());
	Image *img = Image::create(w, h, 32); img->allocate();
	for(int y = 0; y < h; y++) memcpy(img->pixels + (size_t)y * img->stride, &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
	const char *dir = getenv("RM8_MENU_SHOTDIR");
	char path[512]; snprintf(path, sizeof path, "%s/movie_%s_%g.png", dir ? dir : ".", g_seg.name.c_str(), t);
	writePNG(img, path); img->destroy();
	rm8Log("movie shot: %s", path);
}
#else
void rm8MovieTestHooks(int, int){}
#endif

// ======================================================================== the Python API
void rm8MvSetCamera(float x, float y, float z, float pitch, float yaw)
{
	if(!g_cam) return;
	// [V] Camera_SetOrientationFromAngles @0x41b1f0: identity, rotate about the frame's right axis by the pitch, about (0, 1, 0) by the yaw, translate to the position
	Frame *f = g_cam->getFrame();
	Matrix id; id.setIdentity(); f->transform(&id, COMBINEREPLACE);
	V3d right = {1, 0, 0}, up = {0, 1, 0}, p = {x, y, z};
	f->rotate(&right, pitch, COMBINEPOSTCONCAT);
	f->rotate(&up, yaw, COMBINEPOSTCONCAT);
	f->translate(&p, COMBINEPOSTCONCAT);
	g_camYaw = yaw;
	f->updateObjects();
}

int rm8MvRoleCreate(int ri, float x, float y, float z, int type)
{
	if(!g_active || !g_sc) return 0;
	auto it = g_drole.find(ri + 1);	// [V] roleIndex = roleinfo array index = Id - 1
	if(it == g_drole.end() || type == 1){ rm8Log("movie: RoleCreate(%d, type %d): no such role", ri, type); return 0; }
	int set = type == 2 ? 1 : type == 3 ? 2 : 0;
	Obj *o = new Obj; o->doll = true; o->role = &it->second;
	o->pos[0] = o->target[0] = x; o->pos[1] = o->target[1] = y; o->pos[2] = o->target[2] = z;
	o->speed = it->second.speed[set];	// [V] +0x34 = roleinfo walking speed of the set
	for(int k = 0; k < 3; k++) o->scale[k] = it->second.scale[k];

	bool ok = true;
	for(int j = 0; j < 4; j++) ok &= loadDollModel(o->m[j], it->second.art[set * 4 + j], it->second.id, j < 2);
	if(!ok){ for(auto &m : o->m) if(m.clump){ g_sc->world->removeClump(m.clump); m.clump->destroy(); } delete o; rm8Log("movie: RoleCreate(%d): models missing", ri); return 0; }
	o->cur = 0; dollPlay(*o, 2, 0);	// [V] initial action 2 (stand)
	if(getenv("RM8_MOVIE_TRACE")) rm8Log("movie: doll role %d model sphere radius %.1f scale %.2f %.2f %.2f p0 %.1f %.1f %.1f", ri, clumpRadius(o->m[0].clump), it->second.scale[0], it->second.scale[1], it->second.scale[2], o->m[0].p0.x, o->m[0].p0.y, o->m[0].p0.z);
	if(o->m[0].hier && o->m[0].hier->interpolator->currentAnim){ dollAnimate(*o, 0); }
	dollPlace(*o);
	o->h = g_nextH++; g_objs.push_back(o);
	return o->h;
}
void rm8MvRoleDel(int h){ Obj *o = find(h); if(!o || !o->doll) return; g_objs.erase(std::remove(g_objs.begin(), g_objs.end(), o), g_objs.end()); freeObj(o); }
void rm8MvRoleMove(int h, float x, float y, float z){ Obj *o = find(h); if(o && o->doll){ o->target[0] = x; o->target[1] = y; o->target[2] = z; } }
void rm8MvRoleRotate(int h, int dir){ Obj *o = find(h); if(o && o->doll) dollSetDir(*o, dir, true); }
void rm8MvRoleFace(int h, int dir){ Obj *o = find(h); if(o && o->doll) dollSetDir(*o, dir, false); }
void rm8MvRoleSetAnim(int h, int anim, int count){ Obj *o = find(h); if(o && o->doll) dollPlay(*o, anim, count); }
void rm8MvRoleSpeak(int h, int id){ Obj *o = find(h); if(o && o->doll) bubbleSet(*o, id, true); }
void rm8MvRoleStopSpeak(int h){ Obj *o = find(h); if(o && o->doll) bubbleSet(*o, 0, false); }
void rm8MvRoleSetPos(int h, float x, float y, float z){ Obj *o = find(h); if(o && o->doll){ o->pos[0] = o->target[0] = x; o->pos[1] = o->target[1] = y; o->pos[2] = o->target[2] = z; } }
void rm8MvRoleSetSpeed(int h, float s){ Obj *o = find(h); if(o && o->doll) o->speed = s; }
void rm8MvRoleSetScale(int h, float sx, float sy, float sz){ Obj *o = find(h); if(o && o->doll){ o->scale[0] = sx; o->scale[1] = sy; o->scale[2] = sz; } }	// [V] Mesh_SetScale @0x47a9e0: absolute

int rm8MvArtCreate(int art, float x, float y, float z)
{
	if(!g_active || !g_sc) return 0;
	Obj *o = new Obj; o->art = art;
	o->pos[0] = o->target[0] = x; o->pos[1] = o->target[1] = y; o->pos[2] = o->target[2] = z;
	auto it = g_arts.find(art);
	if(it != g_arts.end()){
		const float zero[3] = {0, 0, 0}, one[3] = {1, 1, 1};
		o->clump = rm8SceneAddArticle(*g_sc, it->second.dff, it->second.texdir, zero, one, 0, "-", it->second.uva, "loop");	// created at the identity: its frame is the base matrix
		if(!o->clump){ delete o; return 0; }
		o->base = o->clump->getFrame()->matrix; o->p0 = o->base.pos;
		o->radius = clumpRadius(o->clump);
		artPlace(*o);
	}else{	// effect arts (particles, 6000..6399) of present.cpp
		float p[3] = {x, y, z};
		o->fx = rm8FxPlay(art, p, 0, 0, false, 0, 1.0f, 0, false);
		if(!o->fx){ rm8Log("movie: ArtCreate(%d): unknown art", art); delete o; return 0; }
	}
	o->h = g_nextH++; g_objs.push_back(o);
	return o->h;
}
void rm8MvArtSetAnim(int h, int anim, int count)
{
	Obj *o = find(h);
	if(!o || o->doll || !o->clump) return;
	auto it = g_arts.find(o->art);
	if(it == g_arts.end()) return;
	auto an = it->second.anims.find(anim);
	if(an == it->second.anims.end()) return;	// [V] Article_SelectByTypeAndSetProperties looks the action up by id; unknown ones change nothing
	bool once = count == 1;
	if(!artAnimEntry(*o)){	// a static article has no animation entry in the scene: load it again with the animation
		Clump *old = o->clump;
		const float zero[3] = {0, 0, 0}, one[3] = {1, 1, 1};
		Clump *c = rm8SceneAddArticle(*g_sc, it->second.dff, it->second.texdir, zero, one, 0, an->second, it->second.uva, once ? "once" : "loop");
		if(!c) return;
		rm8SceneRemoveArticle(*g_sc, old);
		o->clump = c; o->base = c->getFrame()->matrix; o->p0 = o->base.pos;
		artPlace(*o);
	}else rm8SceneArticleAnim(*g_sc, o->clump, an->second, once);
	o->animated = artAnimEntry(*o) != nullptr;
	o->animIdx = anim; o->count = count; o->elapsed = 0; o->held = false;
	if(Rm8ArtAnim *aa = artAnimEntry(*o)){ aa->once = once; o->dur = aa->hier->interpolator->currentAnim ? aa->hier->interpolator->currentAnim->duration : 0; seekTime(aa->hier->interpolator, 0); }
}
void rm8MvArtDel(int h){ Obj *o = find(h); if(!o || o->doll) return; g_objs.erase(std::remove(g_objs.begin(), g_objs.end(), o), g_objs.end()); freeObj(o); }
void rm8MvArtMove(int h, float x, float y, float z){ Obj *o = find(h); if(o && !o->doll){ o->target[0] = x; o->target[1] = y; o->target[2] = z; } }
void rm8MvArtRotate(int h, float a){ Obj *o = find(h); if(o && !o->doll){ o->rot = a; artPlace(*o); } }
void rm8MvArtSpeak(int h, int id){ Obj *o = find(h); if(o && !o->doll) bubbleSet(*o, id, true); }
void rm8MvArtStopSpeak(int h){ Obj *o = find(h); if(o && !o->doll) bubbleSet(*o, 0, false); }
void rm8MvArtSetPos(int h, float x, float y, float z)
{
	Obj *o = find(h);
	if(!o || o->doll) return;
	o->pos[0] = o->target[0] = x; o->pos[1] = o->target[1] = y; o->pos[2] = o->target[2] = z;
	if(o->fx){ float p[3] = {x, y, z}; rm8FxMove(o->fx, p); } else artPlace(*o);
}
void rm8MvArtSetSpeed(int h, float s){ Obj *o = find(h); if(o && !o->doll) o->speed = s; }
void rm8MvArtSetScale(int h, float sx, float sy, float sz){ Obj *o = find(h); if(o && !o->doll && o->clump){ o->scale[0] = sx; o->scale[1] = sy; o->scale[2] = sz; artPlace(*o); } }	// [V] CArticle_ApplyScale @0x512460: ratio to the accumulated scale = absolute

void rm8MvPlaySound(int id, int loops){ rm8SndPlay(id, loops); }
void rm8MvPlayMusic(int id){ rm8MusicPlay(id, true); }
void rm8MvSetPic(bool, const std::string &){}	// [A] docs/story_mode.md 2.3: the two textures are all-black / missing, first version draws nothing
void rm8MvDelPic(bool){}

void rm8MvSpeakAside(int id)
{
	Rm8Ui *u = asideUi(); if(!u) return;
	std::string t = rm8UiWrapText(rm8Str(id), 945.0f, 16);
	rm8UiSetTextByName(u, "asideWord", t); rm8UiSetTextByName(u, "asideWord2", t);	// [V] Movie_PlayDialogText @0x515200: both labels, the whole bar shown
	rm8UiShow(u, true);
}
void rm8MvStopSpeakAside(){ if(Rm8Ui *u = asideUi()) rm8UiShow(u, false); }
void rm8MvQuit(){ g_seg.quit = true; }
void rm8MvWait(float s){ g_seg.timer = s; }
