// Minigame 3D layer: model articles (dff + anm, article record +0xc != 1: CArticleDFF_Load @0x455BD0 type 4) for games 19 and 21, the camera the DLLs
// see and the world->screen projection (host +0x1c). Like the host, a model is a clump whose frame is moved with RwFrameTranslate / Rotate / Scale
// in post-concat order (CArticleMgr vf40 @0x43FB00, vf35 @0x43F3B0, CMiniGameHost_2 vf27 @0x50F8F0).
#include "mg_core.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>

using namespace rw;

namespace mg {

namespace {
struct MdlDef { std::string dff, td; std::map<int, std::string> anims; };
std::map<int, MdlDef> g_mdefs;

struct Mdl {
	bool used = false;
	Clump *clump = nullptr;
	HAnimHierarchy *hier = nullptr;
	std::map<int, Animation *> anims;
	V3d lastPos = {0, 0, 0};
	int times = 0;		// [V] article +0x398: 0 = loop forever, n > 0 = n plays then hold (CArticle_UpdateAnimationTime @0x4476e0)
	int left = 0;		// +0x3a8 remaining plays
	bool holding = false;
	bool visible = true;
};
std::vector<Mdl> g_mdl(1);
World *g_world = nullptr;
bool g_camOn = false;
// saved camera state
Matrix g_savedFrame; V2d g_savedVw; float g_savedNear, g_savedFar; World *g_savedWorld = nullptr; bool g_haveSaved = false;
const float VWX = 0.3f, VWY = 0.225f;	// [V] the game camera's view window (4:3), read from the game (port README section 7)

Mdl *get(int h){ return h > 0 && h < (int)g_mdl.size() && g_mdl[h].used ? &g_mdl[h] : nullptr; }

void step(Mdl &m, float dt)
{
	if(!m.hier) return;
	AnimInterpolator *ip = m.hier->interpolator;
	if(!ip->currentAnim) return;
	float dur = ip->currentAnim->duration;
	if(m.holding){ dt = 0; }
	else if(dur > 0){
		float t = ip->currentTime + dt;
		if(t >= dur){
			if(m.times == 0) { ip->addTime(std::max(0.0f, dur - ip->currentTime - 1e-4f)); ip->setCurrentAnim(ip->currentAnim); dt = std::fmod(t, dur); }
			else {
				if(--m.left <= 0){ m.holding = true; dt = std::max(0.0f, dur - ip->currentTime - 1e-4f); }
				else { ip->addTime(std::max(0.0f, dur - ip->currentTime - 1e-4f)); ip->setCurrentAnim(ip->currentAnim); dt = std::fmod(t, dur); }
			}
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
}
}	// namespace

bool loadModels(const std::string &path)
{
	std::ifstream f(path);
	if(!f) return false;
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; int art, n;
		ss >> kw;
		if(kw != "mdl") continue;
		MdlDef d; ss >> art >> d.dff >> d.td >> n;
		for(int i = 0; i < n; i++){ int idx; std::string p; ss >> idx >> p; d.anims[idx] = "article\\" + p; }
		d.dff = "article\\" + d.dff;
		if(d.td != "-") d.td = "article\\" + d.td; else d.td.clear();
		g_mdefs[art] = d;
	}
	return !g_mdefs.empty();
}

int mdNew(int art)
{
	auto it = g_mdefs.find(art);
	if(it == g_mdefs.end()){ rm8Log("minigame: unknown model article %d", art); return 0; }
	Mdl m; m.used = true;
	m.clump = rm8LoadClump(it->second.dff, it->second.td);
	if(!m.clump){ rm8Log("minigame: cannot load %s", it->second.dff.c_str()); return 0; }
	m.hier = HAnimHierarchy::find(m.clump->getFrame());
	if(m.hier) m.hier->attach();
	for(auto &a : it->second.anims){
		Animation *an = rm8LoadAnim(a.second);
		if(an && m.hier && an->getNumNodes() == m.hier->numNodes) m.anims[a.first] = an;
		else rm8Log("minigame: animation %s unusable", a.second.c_str());
	}
	if(!m.anims.empty() && m.hier){ m.hier->interpolator->setCurrentAnim(m.anims.begin()->second); step(m, 0); }	// pose at t = 0
	m.lastPos = {0, 0, 0};
	if(!g_world){
		g_world = World::create();
		Light *amb = Light::create(Light::AMBIENT); amb->setColor(0.75f, 0.75f, 0.75f); g_world->addLight(amb);
		Light *dir = Light::create(Light::DIRECTIONAL); dir->setColor(0.55f, 0.55f, 0.55f);
		Frame *lf = Frame::create(); V3d d = {0.3f, -0.6f, 0.75f}, up = {0, 1, 0};
		Matrix lm; lm.setIdentity(); lm.lookAt(d, up); lf->transform(&lm, COMBINEREPLACE); dir->setFrame(lf); g_world->addLight(dir);
	}
	g_world->addClump(m.clump);
	for(size_t i = 1; i < g_mdl.size(); i++) if(!g_mdl[i].used){ g_mdl[i] = m; return (int)i; }
	g_mdl.push_back(m);
	return (int)g_mdl.size() - 1;
}

void mdFree(int h)
{
	Mdl *m = get(h);
	if(!m) return;
	if(m->clump){ if(g_world) g_world->removeClump(m->clump); m->clump->destroy(); m->clump = nullptr; }
	m->used = false;
}

// [V] CArticleMgr vf40 @0x43FB00 type 4: Rw_FrameTranslate(frame, new - old, post-concat)
void mdPos(int h, V3 p)
{
	Mdl *m = get(h);
	if(!m) return;
	V3d d = {p.x - m->lastPos.x, p.y - m->lastPos.y, p.z - m->lastPos.z};
	m->clump->getFrame()->translate(&d, COMBINEPOSTCONCAT);
	m->lastPos = {p.x, p.y, p.z};
}

// [V] CArticleMgr vf35 @0x43F3B0 type 4: translate(-pos), scale, translate(+pos) about the frame position (+0x40..+0x48)
void mdScale(int h, V3 s)
{
	Mdl *m = get(h);
	if(!m) return;
	Frame *f = m->clump->getFrame();
	V3d p = f->matrix.pos, np = {-p.x, -p.y, -p.z}, sc = {s.x, s.y, s.z};
	f->translate(&np, COMBINEPOSTCONCAT); f->scale(&sc, COMBINEPOSTCONCAT); f->translate(&p, COMBINEPOSTCONCAT);
}

// [V] CMiniGameHost_2 vf27 @0x50F8F0 (+0x6c): translate(-pos), rotate(axis, angle), translate(+pos)
void mdRot(int h, V3 axis, float deg)
{
	Mdl *m = get(h);
	if(!m) return;
	Frame *f = m->clump->getFrame();
	V3d p = f->matrix.pos, np = {-p.x, -p.y, -p.z}, ax = {axis.x, axis.y, axis.z};
	f->translate(&np, COMBINEPOSTCONCAT); f->rotate(&ax, deg, COMBINEPOSTCONCAT); f->translate(&p, COMBINEPOSTCONCAT);
}

// [V] CMiniGameHost_2 vf28 @0x50FA00 (+0x70): Rw_FrameRotate(frame, axis, angle, post-concat) without re-centring
void mdRotFrame(int h, V3 axis, float deg)
{
	Mdl *m = get(h);
	if(!m) return;
	V3d ax = {axis.x, axis.y, axis.z};
	m->clump->getFrame()->rotate(&ax, deg, COMBINEPOSTCONCAT);
}

// [V] +0x58 CMiniGameHost_2 vf22 @0x50F800 -> CArticleMgr vf23 @0x440D90 -> Article_SelectByTypeAndSetProperties @0x445860: action = animation record index, times -> +0x398
// (0 = loop forever; records give start time 0 and default count 1). Only type 4 (3D) articles react.
void mdAction(int h, int action, int times)
{
	Mdl *m = get(h);
	if(!m || !m->hier) return;
	auto it = m->anims.find(action);
	if(it == m->anims.end()) return;
	AnimInterpolator *ip = m->hier->interpolator;
	ip->setCurrentAnim(it->second);
	m->times = times < 0 ? 1 : times; m->left = m->times; m->holding = false;
	step(*m, 0);
}

void mdUpdate(int h, float dt){ if(Mdl *m = get(h)) step(*m, dt); }	// +0x78

bool mdAnimDone(int h){ Mdl *m = get(h); return m && m->holding; }
float mdAnimTime(int h){ Mdl *m = get(h); return m && m->hier && m->hier->interpolator->currentAnim ? m->hier->interpolator->currentTime : 0.0f; }
float mdAnimDuration(int h){ Mdl *m = get(h); return m && m->hier && m->hier->interpolator->currentAnim ? m->hier->interpolator->currentAnim->duration : 0.0f; }
void mdVisible(int h, bool v){ if(Mdl *m = get(h)) m->visible = v; }

void mdDraw(int h)
{
	Mdl *m = get(h);
	if(!m || !m->visible || !g_camOn) return;
	gfxFlush();
	uint32 zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE), atf = GetRenderState(ALPHATESTFUNC), atr = GetRenderState(ALPHATESTREF), cull = GetRenderState(CULLMODE);
	SetRenderState(ZTESTENABLE, 1); SetRenderState(ZWRITEENABLE, 1); SetRenderState(CULLMODE, CULLBACK);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196);	// [H] like the board characters (Mesh_DrawWithAlphaTest)
	engine->currentWorld = g_world;
	m->clump->render();
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(CULLMODE, cull);
	SetRenderState(ALPHATESTFUNC, atf); SetRenderState(ALPHATESTREF, atr);
	gfxRestore2D();
}

// [V] host +0x1c (CMiniGameHost_vf7 @0x50F1A0 = Camera_ProjectWorldToScreen @0x41A900): view then projection transform, to pixels (ftol).
// The camera frame is the identity (Camera_SaveViewState @0x41B2E0, called by CMiniGameHost_vf16 @0x50DDD0). RenderWare mirrors x of the view
// (rwCameraBeginUpdate: inv.right.x = -inv.right.x), so world +x is to the screen's left: sx = W/2 - x/z * W/(2 vwx), sy = H/2 - y/z * H/(2 vwy).
bool project(V3 p, float &sx, float &sy)
{
	if(p.z <= 0) return false;
	sx = (float)(int)(W / 2 - p.x / p.z * W / (2 * VWX));
	sy = (float)(int)(H / 2 - p.y / p.z * H / (2 * VWY));
	return true;
}

// ---- camera for the frames that have models (minigame.cpp wraps the game's Draw)
bool models3d(){ for(size_t i = 1; i < g_mdl.size(); i++) if(g_mdl[i].used) return true; return false; }

void camBegin(Camera *cam, int sw, int sh)
{
	g_savedFrame = cam->getFrame()->matrix; g_savedVw = cam->viewWindow; g_savedNear = cam->nearPlane; g_savedFar = cam->farPlane; g_savedWorld = cam->world; g_haveSaved = true;
	float ox, oy, sc; gfxMap(ox, oy, sc);
	float fx = W * sc / sw, fy = H * sc / sh;	// part of the frame buffer covered by the 1024x768 area
	V2d vw = {VWX / fx, VWY / fy};
	cam->setViewWindow(&vw);
	cam->setNearPlane(1.0f); cam->setFarPlane(5000.0f);
	Matrix id; id.setIdentity(); cam->getFrame()->transform(&id, COMBINEREPLACE);
	cam->world = g_world;
	g_camOn = true;
}
void camEnd(Camera *cam)
{
	if(!g_haveSaved) return;
	cam->getFrame()->transform(&g_savedFrame, COMBINEREPLACE);
	cam->setViewWindow(&g_savedVw); cam->setNearPlane(g_savedNear); cam->setFarPlane(g_savedFar); cam->world = g_savedWorld;
	g_haveSaved = false; g_camOn = false;
}
void freeAllModels(){ for(size_t i = 1; i < g_mdl.size(); i++) mdFree((int)i); }

}	// namespace mg
