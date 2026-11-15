// Board feedback, see boardfx.h and docs/boardfx.md. Facts marked [V] are read from the decompile (function address given),
// [H] hypotheses, [A] port adaptations.
#include "boardfx.h"
#include "audio.h"
#include "present.h"
#include "npc.h"
#include "story.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <map>
#include <vector>

using namespace rw;

namespace {

Rm8Scene *g_s = nullptr;			// the scene the tables below were built for
std::vector<int> g_nodeIcon;			// road node -> index into Rm8Scene::icons (-1 none)
std::vector<unsigned> g_mask;			// [V] icon object +0x10: bit per seat standing on / approaching the icon

struct Playing { Clump *c; int action; float prev; };	// a clicked article running its action (the callback at +0x518 returns it to action 0)
std::vector<Playing> g_playing;

const bool g_trace = getenv("RM8_BOARDFX_TRACE") != nullptr;	// debugging: log icon / click / build / cutscene events
float g_time = 0;								// seconds of scene time (trace only)

uint32 g_seed = 20240917u;
int rnd(){ g_seed = g_seed * 214013u + 2531011u; return (g_seed >> 16) & 0x7fff; }	// MSVC rand(); own stream so taps do not disturb the rules RNG

void snd(int id){ if(g_trace) rm8Log("boardfx: sound 0x%X t=%.2f", id, g_time); rm8SndPlay(id, 1); }

struct IconRun { int action = -1; float prev = -1; int seat = -1, node = -1; bool pending = false, lit = false; };	// the icon's running action and the animation time already checked for its timed events
std::vector<IconRun> g_iconRun;
std::map<std::string, Texture*> g_iconTex;		// texture cache of the glow swaps (kept for the run)

// [A] a scene.txt exported before the `icon` / `itex` / `ifx` lines existed (an old copy on the phone) would silently give no icon feedback at all:
// rebuild the icon table from the event articles themselves. role.bin gives every event art eve_<name>.dff the same layout [V]: actions 0 / 2 =
// eve_<name>_01.anm (press), 1 / 3 = eve_<name>_02.anm (pop); texture events eve_<name>_d.tga (lit) while pressed, eve_<name>.tga after the pop
// (times 0 / 0.05 / 0.08); particle effect 14 on frame [@1] at 0.035 s of action 0 (scripts/export_scene.py art_action_events).
void deriveIcons(Rm8Scene &s)
{
	if(s.rnodes.empty()) return;
	bool madeIcons = false;
	for(size_t i = 0; i < s.clumps.size() && i < s.clumpNames.size(); i++){
		std::string n = s.clumpNames[i];
		for(char &ch : n) ch = (char)tolower((unsigned char)ch);
		size_t p = n.rfind("event\\eve_");
		if(p == std::string::npos || n.size() < 4 || n.compare(n.size() - 4, 4, ".dff") != 0) continue;
		Clump *c = s.clumps[i];
		Rm8Icon *ic = nullptr;
		for(Rm8Icon &x : s.icons) if(x.clump == c){ ic = &x; break; }
		if(!ic){	// no `icon` line: the node of the tile the article stands on
			const V3d &q = c->getFrame()->getLTM()->pos;
			int best = -1; float bd = 30.0f;
			for(size_t k = 0; k < s.rnodes.size(); k++){
				float d = hypotf(s.rnodes[k].x - q.x, s.rnodes[k].z - q.z);
				if(d < bd){ bd = d; best = (int)k; }
			}
			if(best < 0) continue;
			Rm8Icon x; x.tx = s.rnodes[best].tx; x.ty = s.rnodes[best].ty; x.clump = c;
			std::string base = s.clumpNames[i].substr(0, s.clumpNames[i].size() - 4);
			x.anims[0] = x.anims[2] = base + "_01.anm"; x.anims[1] = x.anims[3] = base + "_02.anm";
			s.icons.push_back(x); ic = &s.icons.back(); madeIcons = true;
		}
		if(ic->tex.empty() && ic->fx.empty()){
			std::string base = s.clumpNames[i].substr(0, s.clumpNames[i].size() - 4);
			std::string lit = base + "_d.tga", dark = base + ".tga";
			for(char &ch : lit) ch = (char)tolower((unsigned char)ch);
			for(char &ch : dark) ch = (char)tolower((unsigned char)ch);
			ic->tex = {{1, 0.0f, "map", lit}, {1, 0.05f, "map", dark}, {0, 0.08f, "map", lit}, {2, 0.08f, "map", lit}};
			ic->fx = {{0, 0.035f, 14, 1, 0}};
		}
	}
	if(madeIcons) rm8Log("boardfx: WARNING scene %s has no icon lines (old scene.txt on the device?): derived %d icons from the event articles", s.mapId.c_str(), (int)s.icons.size());
}

void bind(Rm8Scene &s)
{
	g_s = &s;
	deriveIcons(s);
	g_nodeIcon.assign(s.rnodes.size(), -1);
	g_mask.assign(s.icons.size(), 0u);
	g_iconRun.assign(s.icons.size(), IconRun());
	g_playing.clear();
	std::map<std::pair<int,int>, int> byTile;
	for(size_t i = 0; i < s.icons.size(); i++) byTile[{s.icons[i].tx, s.icons[i].ty}] = (int)i;
	int mapped = 0;
	for(size_t n = 0; n < s.rnodes.size(); n++){
		auto it = byTile.find({s.rnodes[n].tx, s.rnodes[n].ty});
		if(it != byTile.end()){ g_nodeIcon[n] = it->second; mapped++; }
	}
	rm8Log("boardfx: map %s: %d event icons, %d of %d road nodes carry one", s.mapId.c_str(), (int)s.icons.size(), mapped, (int)s.rnodes.size());
}

float dist3(float ax, float ay, float az, float bx, float by, float bz){ float x = ax - bx, y = ay - by, z = az - bz; return sqrtf(x*x + y*y + z*z); }

// [V] role.bin timed material event (UI_LoadMaterial @0x44BDC0 via Article_Update @0x448540): load the texture and set it on the icon's material
// (the "map" material = the hexagon face). [A] every material still carrying an eve_ texture is switched (the materials have no names in librw)
void setIconTexture(Clump *c, const std::string &key)
{
	auto it = g_iconTex.find(key);
	if(it == g_iconTex.end()) it = g_iconTex.emplace(key, rm8PackTexture(key)).first;
	Texture *t = it->second;
	if(!t){ if(g_trace) rm8Log("boardfx: no texture %s", key.c_str()); return; }
	FORLIST(lnk, c->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		if(!g) continue;
		for(int i = 0; i < g->matList.numMaterials; i++){
			Material *m = g->matList.materials[i];
			if(m && m->texture && !strncmp(m->texture->name, "eve_", 4)) m->setTexture(t);
		}
	}
}

// [V] vf23 (CArticleMgr +0x5C) with count 1: play the icon action once and hold the last frame
void iconAction(int icon, int action, int seat = -1, int node = -1)
{
	const Rm8Icon &ic = g_s->icons[icon];
	auto it = ic.anims.find(action);
	if(it == ic.anims.end() || !ic.clump) return;
	if(g_trace) rm8Log("boardfx: icon (%d,%d) action %d (%s) t=%.2f", ic.tx, ic.ty, action, it->second.c_str(), g_time);
	if(rm8SceneArticleAnim(*g_s, ic.clump, it->second, true)){ g_iconRun[icon].action = action; g_iconRun[icon].prev = -1.0f; g_iconRun[icon].seat = seat; g_iconRun[icon].node = node; g_iconRun[icon].pending = false; }
}

// per frame: [V] Article_Update @0x448540 runs the timed events of the playing action when its time passes them
void iconEventsUpdate(Rm8Scene &s)
{
	for(size_t i = 0; i < g_iconRun.size() && i < s.icons.size(); i++){
		IconRun &r = g_iconRun[i];
		if(r.action < 0) continue;
		const Rm8Icon &ic = s.icons[i];
		const Rm8ArtAnim *aa = nullptr;
		for(const Rm8ArtAnim &x : s.anims) if(x.clump == ic.clump){ aa = &x; break; }
		if(!aa || !aa->hier->interpolator->currentAnim){ r.action = -1; continue; }
		float t = aa->hier->interpolator->currentTime;
		// [A] (user report) the bright texture and the particle effect belong to a role that has STOPPED on the icon and pressed it down completely: the press of action 0 (the last step,
		// started 40 units before the node) only arms them (pending, below); the pass-by press (action 2) never lights the icon, and the pop (action 1) darkens it again
		// (its "lit at 0.0" event only counts when the icon is lit)
		for(const Rm8IconTex &e : ic.tex)
			if(e.action == r.action && e.time > r.prev && e.time <= t && e.action == 1){
				bool litTex = e.texture.size() > 6 && e.texture.compare(e.texture.size() - 6, 6, "_d.tga") == 0;
				if(litTex && !r.lit) continue;
				if(g_trace) rm8Log("boardfx: icon (%d,%d) texture %s t=%.2f", ic.tx, ic.ty, e.texture.c_str(), g_time);
				setIconTexture(ic.clump, e.texture); r.lit = litTex;
			}
		r.prev = t;
		if(t >= aa->hier->interpolator->currentAnim->duration - 2e-4f){	// finished: the last frame stays
			if(r.action == 0) r.pending = true;
			r.action = -1;
		}
	}
	for(size_t i = 0; i < g_iconRun.size() && i < s.icons.size(); i++){
		IconRun &r = g_iconRun[i];
		if(!r.pending) continue;
		if(r.node < 0 || r.node >= (int)s.rnodes.size() || !(g_mask[i] & (1u << (r.seat & 31)))){ r.pending = false; continue; }	// the role left again before it stopped
		const Rm8RoadNode &n = s.rnodes[r.node];
		float rp[3];
		bool stopped = !rm8RolesGetPos(r.seat, rp) || (rm8TurnState(r.seat) != 1 && dist3(n.x, n.y, n.z, rp[0], rp[1], rp[2]) < 12.0f);	// state 1 = walking
		if(!stopped) continue;
		r.pending = false;
		const Rm8Icon &ic = s.icons[i];
		for(const Rm8IconTex &e : ic.tex)
			if(e.action == 0){ if(g_trace) rm8Log("boardfx: icon (%d,%d) texture %s (seat %d stopped, press complete) t=%.2f", ic.tx, ic.ty, e.texture.c_str(), r.seat, g_time); setIconTexture(ic.clump, e.texture); r.lit = true; }
		for(const Rm8IconFx &e : ic.fx)
			if(e.action == 0){
				int h = rm8PrtAttachH(e.effect, ic.clump, e.hook);
				if(g_trace) rm8Log("boardfx: icon (%d,%d) effect %d on [@%d] -> %s (seat %d stopped, press complete) t=%.2f", ic.tx, ic.ty, e.effect, e.hook, h >= 0 ? "ok" : "failed", r.seat, g_time);
			}
	}
}

}	// namespace

static void buildsClear();	// below
static void ghostClear();
static void plotsClear();
static void glowClear();
void rm8BoardFxClear(){ g_s = nullptr; g_nodeIcon.clear(); g_mask.clear(); g_iconRun.clear(); g_playing.clear(); buildsClear(); ghostClear(); plotsClear(); glowClear(); }

// ---- event icons -------------------------------------------------------------------------------------------------------
// [V] Role_StepAlongPath @0x485860 per frame while walking: Depart on the node left, Approach on the node ahead (final = steps == 1);
// [V] the same pair around teleports (OnCheckBankruptcy @0x48E920, SnapToNodePos @0x48CFD0, Game_TurnNextRole @0x4971A0 with final = 0)
void rm8BoardFxIconApproach(int seat, int node, const float pos[3], bool final)
{
	if(!g_s || node < 0 || node >= (int)g_nodeIcon.size() || g_nodeIcon[node] < 0 || !pos) return;
	int ic = g_nodeIcon[node];
	unsigned bit = 1u << (seat & 31);
	if(g_trace && final) rm8Log("boardfx: final approach seat %d icon %d mask 0x%x", seat, ic, g_mask[ic]);
	if(g_mask[ic] & bit) return;				// [V] @0x4EE3C0: already counted for this seat
	const Rm8RoadNode &n = g_s->rnodes[node];		// [A] the icon sits on the node (export_scene.py: tile centre)
	if(dist3(n.x, n.y, n.z, pos[0], pos[1], pos[2]) >= 40.0f) return;	// [V] RwV3dLength < 40.0
	if(g_mask[ic] == 0){					// [V] nobody on the icon yet
		if(!final) iconAction(ic, 2, seat, node);			// [V] vf23(2, 1, 0): a pass-by press, no sound
		else { iconAction(ic, 0, seat, node); snd(0x7D); }	// [V] vf23(0, 1, 0) + CAudioMgr_PlaySample(0x7D, ..., loops 1)
	}
	g_mask[ic] |= bit;
}

void rm8BoardFxIconDepart(int seat, int node, const float pos[3])
{
	if(!g_s || node < 0 || node >= (int)g_nodeIcon.size() || g_nodeIcon[node] < 0) return;
	int ic = g_nodeIcon[node];
	unsigned bit = 1u << (seat & 31);
	if(!(g_mask[ic] & bit)) return;
	const Rm8RoadNode &n = g_s->rnodes[node];
	float d = pos ? dist3(n.x, n.y, n.z, pos[0], pos[1], pos[2]) : 0.0f;	// [V] a null position counts as distance 0
	if(d > 10.0f){						// [V] 10.0 < distance
		g_mask[ic] &= ~bit;
		if(g_mask[ic] == 0) iconAction(ic, 1);		// [V] vf23(1, 1, 0): the icon pops back up
	}
}

// ---- clicks on the scenery ---------------------------------------------------------------------------------------------
namespace {

struct Seg { V3d a, b; };

float len(const V3d &p){ return length(p); }

// [V] Atomic_MergeWorldBoundingSphere: union of the atomics' world spheres (the article sphere), radius * 1.5 for map articles
// (CArticleMgr_CreateArticleFromRecord @0x43CB90)
bool clumpSphere(Clump *c, Sphere &out)
{
	bool any = false;
	FORLIST(lnk, c->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		if(!a->geometry || !a->geometry->numMorphTargets) continue;
		const Matrix *m = a->getFrame()->getLTM();
		Sphere g = a->geometry->morphTargets[0].boundingSphere, w;
		V3d::transformPoints(&w.center, &g.center, 1, m);
		float sx = len(m->right), sy = len(m->up), sz = len(m->at);
		w.radius = g.radius * std::max(sx, std::max(sy, sz));
		if(!any){ out = w; any = true; continue; }
		V3d d = sub(w.center, out.center); float L = len(d);
		if(L + w.radius <= out.radius) continue;
		if(L + out.radius <= w.radius){ out = w; continue; }
		float R = (L + out.radius + w.radius) * 0.5f, k = (R - out.radius) / L;
		out.center = {out.center.x + d.x * k, out.center.y + d.y * k, out.center.z + d.z * k};
		out.radius = R;
	}
	if(any) out.radius *= 1.5f;
	return any;
}

// [V] Rw_LineSphereIntersect: does the segment touch the sphere
bool segSphere(const Seg &s, const Sphere &sp)
{
	V3d d = sub(s.b, s.a); float L2 = dot(d, d);
	float t = L2 > 0 ? std::max(0.0f, std::min(1.0f, dot(sub(sp.center, s.a), d) / L2)) : 0.0f;
	V3d p = {s.a.x + d.x * t, s.a.y + d.y * t, s.a.z + d.z * t};
	return len(sub(p, sp.center)) <= sp.radius;
}

// [V] Geom_RayIntersect over the clump's atomics: any triangle of the (bind pose) geometry crossed by the segment [H: double sided]
bool segClump(const Seg &s, Clump *c)
{
	V3d d = sub(s.b, s.a);
	FORLIST(lnk, c->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		Geometry *g = a->geometry;
		if(!g || !g->numMorphTargets || !g->triangles) continue;
		const Matrix *m = a->getFrame()->getLTM();
		std::vector<V3d> w(g->numVertices);
		V3d::transformPoints(w.data(), g->morphTargets[0].vertices, g->numVertices, m);
		for(int t = 0; t < g->numTriangles; t++){
			const V3d &p0 = w[g->triangles[t].v[0]], &p1 = w[g->triangles[t].v[1]], &p2 = w[g->triangles[t].v[2]];
			V3d e1 = sub(p1, p0), e2 = sub(p2, p0), pv = cross(d, e2);
			float det = dot(e1, pv);
			if(fabsf(det) < 1e-9f) continue;
			float inv = 1.0f / det;
			V3d tv = sub(s.a, p0);
			float u = dot(tv, pv) * inv; if(u < 0 || u > 1) continue;
			V3d qv = cross(tv, e1);
			float v = dot(d, qv) * inv; if(v < 0 || u + v > 1) continue;
			float tt = dot(e2, qv) * inv;
			if(tt >= 0 && tt <= 1) return true;
		}
	}
	return false;
}

}	// namespace

static bool tapImpl(Camera *cam, float sx, float sy, bool gate)
{
	if(!g_s || !cam || !cam->frameBuffer) return false;
	Rm8Scene &s = *g_s;
	// [V] CGamePlayScene__OnKeyInput @0x4A28B0 -> Game_HandleCardClickInGame @0x4A2940: the pick only runs while the current role is in state 6
	if(gate && rm8TurnRoleCount() > 0){
		int cur = rm8TurnCurrent();
		if(!rm8TurnHuman(cur) || rm8TurnState(cur) != 6) return false;
	}
	// pixel ray: the inverse of the projection used by cards.cpp (screen x = (0.5 - xc / (2 vw.x zc)) W)
	const Matrix *m = cam->getFrame()->getLTM();
	float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height;
	float kx = (0.5f - sx / W) * 2.0f * cam->viewWindow.x, ky = (0.5f - sy / H) * 2.0f * cam->viewWindow.y;
	V3d dir = {m->at.x + m->right.x * kx + m->up.x * ky, m->at.y + m->right.y * kx + m->up.y * ky, m->at.z + m->right.z * kx + m->up.z * ky};
	float dl = len(dir); if(dl < 1e-6f) return false;
	dir = {dir.x / dl, dir.y / dl, dir.z / dl};
	Seg seg = {{m->pos.x + dir.x * cam->nearPlane, m->pos.y + dir.y * cam->nearPlane, m->pos.z + dir.z * cam->nearPlane},
	           {m->pos.x + dir.x * cam->farPlane, m->pos.y + dir.y * cam->farPlane, m->pos.z + dir.z * cam->farPlane}};
	// [V] vf28 @0x43E0E0: articles whose sphere the ray touches, sorted by the distance to the sphere centre (at most 0x32); the first one whose
	// geometry the ray crosses is the selection
	struct Cand { float d; Clump *c; };
	std::vector<Cand> cands;
	for(Clump *c : s.clumps){
		Sphere sp; if(!clumpSphere(c, sp) || !segSphere(seg, sp)) continue;
		cands.push_back({len(sub(sp.center, seg.a)), c});
	}
	std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b){ return a.d < b.d; });
	if(cands.size() > 50) cands.resize(50);
	Clump *hit = nullptr;
	for(const Cand &k : cands) if(segClump(seg, k.c)){ hit = k.c; break; }
	if(!hit) return false;
	// [V] Game_OnTileClickWithCard @0x4a4a10 (kind 5, article): the chain-store sign (art 0x45b, an "aown" article) never plays its click action or shows text; in the story the
	// click runs CPlayerTag__OnChainStoreEvent (story.cpp), and a used-up sign is switched off (CArticleMgr vf38 -> +0xC4 = 0)
	if(s.artOwner.count(hit)){
		if(rm8StoryChainStoreClick()){ rm8SceneSetArticleActive(s, hit, false); return true; }
		return false;
	}
	// [V] vf29 @0x43E9C0 (mouse release): only when the article sits in action 0 and has more than one action: action 1 + rand % (count - 1)
	auto it = s.artActs.find(hit);
	if(it == s.artActs.end()) return false;
	for(const Playing &p : g_playing) if(p.c == hit) return false;
	const Rm8ArtActs &aa = it->second;
	int count = (int)aa.anims.size();
	if(count < 2) return false;
	int act = 1 + rnd() % (count - 1);
	auto an = aa.anims.find(act);
	if(an == aa.anims.end()) return false;
	if(!rm8SceneArticleAnim(s, hit, an->second, true)) return false;	// [V] vf23(act, 1, 0): once, holding the last frame until the callback
	g_playing.push_back({hit, act, -1.0f});
	if(g_trace) rm8Log("boardfx: tap -> article action %d (%s)", act, an->second.c_str());
	return true;
}

bool rm8BoardFxTap(Camera *cam, float sx, float sy){ return tapImpl(cam, sx, sy, true); }

Camera *g_cam = nullptr;	// the main camera (rm8BoardFxSetCamera), for the test hook below

// debugging: RM8_BOARDFX_TAP=<seconds> taps (without the state-6 gate) the first scenery article with several actions at that time and then
// every 2.5 s the next one, at the screen position of its bounding sphere centre
static void testTaps(Rm8Scene &s, float dt)
{
	static const char *envC = getenv("RM8_CHAIN_TAP");	// debugging: RM8_CHAIN_TAP=<seconds> taps the chain-store sign (an article with an owner role) through the real pick (state 6 gate) at that time and every 2.5 s
	static float tc = 0, nextC = -1;
	if(envC && g_cam && g_cam->frameBuffer && dt > 0){
		if(nextC < 0) nextC = (float)atof(envC);
		tc += dt;
		if(tc >= nextC){
			nextC = tc + 2.5f;
			for(auto &kv : s.artOwner){
				Sphere sp; if(!clumpSphere(kv.first, sp)) continue;
				const Matrix *m = g_cam->getFrame()->getLTM();
				V3d d = sub(sp.center, m->pos);
				float xc = dot(d, m->right), yc = dot(d, m->up), zc = dot(d, m->at);
				if(zc < 1.0f) continue;
				float W = (float)g_cam->frameBuffer->width, H = (float)g_cam->frameBuffer->height;
				float sx = (0.5f - xc / (2.0f * g_cam->viewWindow.x * zc)) * W, sy = (0.5f - yc / (2.0f * g_cam->viewWindow.y * zc)) * H;
				bool r;
				if(sx < 0 || sy < 0 || sx > W || sy > H){	// off screen at the moment: run the click's effect directly (the pick itself is the same code as any article click)
					r = s.artOwner.count(kv.first) && rm8StoryChainStoreClick();
					if(r) rm8SceneSetArticleActive(s, kv.first, false);
					rm8Log("boardfx: chain-store sign off screen (%.0f,%.0f): direct click", sx, sy);
				}else r = tapImpl(g_cam, sx, sy, true);
				rm8Log("boardfx: chain-store tap at (%.0f,%.0f) owner %d -> %s", sx, sy, kv.second, r ? "used" : "nothing");
				break;
			}
		}
	}
	static const char *env = getenv("RM8_BOARDFX_TAP");
	static float t = 0, next = -1; static size_t k = 0;
	if(!env || !g_cam || !g_cam->frameBuffer || dt <= 0) return;
	if(next < 0) next = (float)atof(env);
	t += dt;
	if(t < next) return;
	next = t + 2.5f;
	std::vector<Clump*> list; for(auto &kv : s.artActs) list.push_back(kv.first);
	if(list.empty()) return;
	Clump *c = list[k++ % list.size()];
	Sphere sp; if(!clumpSphere(c, sp)) return;
	const Matrix *m = g_cam->getFrame()->getLTM();
	V3d d = sub(sp.center, m->pos);
	float xc = dot(d, m->right), yc = dot(d, m->up), zc = dot(d, m->at);
	if(zc < 1.0f) return;
	float W = (float)g_cam->frameBuffer->width, H = (float)g_cam->frameBuffer->height;
	float sx = (0.5f - xc / (2.0f * g_cam->viewWindow.x * zc)) * W, sy = (0.5f - yc / (2.0f * g_cam->viewWindow.y * zc)) * H;
	bool r = tapImpl(g_cam, sx, sy, false);
	rm8Log("boardfx: test tap at (%.0f,%.0f) article %zu of %zu -> %s", sx, sy, k, list.size(), r ? "reacted" : "no reaction");
}

// per frame: [V] Article_Update @0x448540 fires the role.bin sound events of the running action when its time passes (callback
// Vehicle_UpdatePos @0x43A280 plays the sample, loops 1); [V] the callback at +0x518 (thunk_FUN_00445860 = select action 0, count 0) returns
// the article to its idle loop when the click action has finished
// ---- see-through: [V] RoleMgr_UpdateAll @0x495BF0 (every flagged update): CArticleMgr vf26 @0x4403F0 clears the glow flag of all articles, then for every role with status < 3 that is drawn
// vf27 @0x4404A0 projects 5 points around (role position + (0, 30, 0)) (centre, and +-20 along the camera's right x +-30 up), keeps the ones inside the window (30 < x < W - 30,
// 30 < y < H - 100) and casts the segment camera -> point against the sphere and the geometry of every scenery article (+0xC4 != 0, i.e. the articles of the map file): a hit sets
// the flag (+0xC8). Art_UpdateGlowAlpha @0x441740 moves +0xCC toward 0.4 (flag set) / 1.0 and CArticleMgr__vf5_SetRenderMode @0x43B210 mode 3 draws the ones below 1.0 translucent.
// Only when init.txt's `character` (InitTxt_GetParam(5)) > 4. [A] the test runs 15 times a second, the fade rate (1 / s) is a guess [H].
namespace {
struct Glow { Clump *c = nullptr; Sphere sp; float a = 1.0f; bool hit = false; };
std::vector<Glow> g_glow; Rm8Scene *g_glowFor = nullptr; float g_glowAcc = 0;

void glowBind(Rm8Scene &s)
{
	g_glow.clear(); g_glowFor = &s;
	for(Clump *c : s.mapArticles){ Glow g; g.c = c; if(clumpSphere(c, g.sp)) g_glow.push_back(g); }
}
bool glowProject(Camera *cam, const V3d &p, float &sx, float &sy)
{
	const Matrix *m = cam->getFrame()->getLTM();
	V3d d = sub(p, m->pos);
	float xc = dot(d, m->right), yc = dot(d, m->up), zc = dot(d, m->at);
	if(zc < 1.0f) return false;
	float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height;
	sx = (0.5f - xc / (2.0f * cam->viewWindow.x * zc)) * W; sy = (0.5f - yc / (2.0f * cam->viewWindow.y * zc)) * H;
	float k = H / 600.0f;	// [A] the margins are pixels of the original 800 x 600 window: scaled to the real size (the phone's 1904 px high HUD bar is ~12 %, not 100 px)
	return sx > 30.0f * k && sx < W - 30.0f * k && sy > 30.0f * k && sy < H - 100.0f * k;
}
void glowTest(Camera *cam)
{
	for(Glow &g : g_glow) g.hit = false;
	const Matrix *m = cam->getFrame()->getLTM();
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		float rp[3];
		if(rm8TurnStatus(s) >= 3 || rm8NpcRoleHidden(s) || rm8PresentRoleHidden(s) || !rm8RolesGetPos(s, rp)) continue;
		for(int k = 0; k < 5; k++){
			V3d p = {rp[0], rp[1] + 30.0f, rp[2]};
			if(k > 0){ float rx = (((k - 1) & 1) ? -20.0f : 20.0f), uy = (((k - 1) / 2) ? -30.0f : 30.0f); p = {p.x + m->right.x * rx, p.y + m->right.y * rx + uy, p.z + m->right.z * rx}; }
			float sx, sy;
			if(!glowProject(cam, p, sx, sy)) continue;
			if(getenv("RM8_GLOW_DEBUG")){ int ns = 0; Seg sg = {m->pos, p}; for(Glow &g : g_glow) ns += segSphere(sg, g.sp); rm8Log("glow: seat %d point %d at (%.0f,%.0f): %d spheres touched", s, k, sx, sy, ns); }
			Seg seg = {m->pos, p};
			for(Glow &g : g_glow) if(!g.hit && segSphere(seg, g.sp) && segClump(seg, g.c)) { g.hit = true; if(getenv("RM8_GLOW_DEBUG") && g_s) for(size_t q = 0; q < g_s->clumps.size(); q++) if(g_s->clumps[q] == g.c) rm8Log("glow hit: %s", g_s->clumpNames[q].c_str()); }
		}
	}
	if(g_trace){ static int last = -1; int n = 0; for(const Glow &g : g_glow) n += g.hit; if(n != last){ last = n; rm8Log("boardfx: see-through: %d scenery articles between the camera and a role (%d tested) t=%.2f", n, (int)g_glow.size(), g_time); } }
}
void glowUpdate(Rm8Scene &s, float dt)
{
	static const bool off = getenv("RM8_NO_SEETHROUGH") != nullptr;
	if(off) return;
	if(g_glowFor != &s) glowBind(s);
	if(!g_cam || !g_cam->frameBuffer || rm8TurnRoleCount() == 0) return;
	g_glowAcc += dt;
	if(g_glowAcc >= 1.0f / 15.0f){ g_glowAcc = 0; glowTest(g_cam); }
	for(Glow &g : g_glow){
		float t = g.hit ? 0.4f : 1.0f, a = g.a;
		if(a < t) a = std::min(t, a + dt); else if(a > t) a = std::max(t, a - dt);
		if(a != g.a){ g.a = a; rm8SceneSetArticleOpacity(g.c, a); }
	}
}
}	// namespace

static void glowClear(){ g_glow.clear(); g_glowFor = nullptr; }
void rm8BoardFxBuildsUpdate(float dt);
void rm8BoardFxGhostUpdate(float dt);
void rm8BoardFxTestFx(float dt);
void rm8BoardFxPlotUpdate(float dt);
void rm8BoardFxTestMsgs(float dt);
void rm8BoardFxUpdate(Rm8Scene &s, float dt)
{
	if(g_s != &s || g_nodeIcon.size() != s.rnodes.size() || g_mask.size() != s.icons.size() || g_iconRun.size() != s.icons.size()) bind(s);
	testTaps(s, dt);
	g_time += dt;
	iconEventsUpdate(s);
	glowUpdate(s, dt);
	for(size_t i = 0; i < g_playing.size();){
		Playing &p = g_playing[i];
		const Rm8ArtAnim *aa = nullptr;
		for(const Rm8ArtAnim &x : s.anims) if(x.clump == p.c){ aa = &x; break; }
		auto it = s.artActs.find(p.c);
		if(!aa || it == s.artActs.end() || !aa->hier->interpolator->currentAnim){ g_playing.erase(g_playing.begin() + i); continue; }
		AnimInterpolator *ip = aa->hier->interpolator;
		float t = ip->currentTime;
		for(const Rm8ArtSound &e : it->second.sounds)
			if(e.action == p.action && e.time > p.prev && e.time <= t) snd(e.sound);	// flag 1 = play unconditionally [V @0x43A280]
		p.prev = t;
		if(t >= ip->currentAnim->duration - 2e-4f){
			rm8SceneArticleAnim(s, p.c, it->second.anims[0], false);
			g_playing.erase(g_playing.begin() + i);
			continue;
		}
		i++;
	}
	if(dt > 0){ rm8BoardFxBuildsUpdate(dt); rm8BoardFxGhostUpdate(dt); rm8BoardFxPlotUpdate(dt); rm8BoardFxTestMsgs(dt); rm8BoardFxTestFx(dt); }
}

// ---- land rules -----------------------------------------------------------------------------------------------------
void rm8BoardFxLandTaken(){ snd(1); }
void rm8BoardFxRentPaid(){ snd(0x7B); }

// ---- build / upgrade animation ---------------------------------------------------------------------------------------
// [V] Role_BuildOnLand @0x487380 -> NPC_CreateOrLandEnter @0x4EEEE0 -> NPC_CreateAndDisplay @0x4F4B50: a task object (vtable 0x5E6618) with
// the role in state 9. Cls_5e6618__vf1_UpdateTimer @0x4F4E40 / NPC_PlayAnimOrComplete @0x4F4CC0 / NPC_CompleteInteraction @0x4F4D70.
namespace {

const float DEFAULT_AT[3] = {0.54f, -0.64f, -0.54f};	// [V] README section 7: the game camera's view direction on the board
float g_camAt[3] = {DEFAULT_AT[0], DEFAULT_AT[1], DEFAULT_AT[2]};
float g_camYaw = 135.0f;

// [V] Anim_CreateSprite @0x47B090 / Building_GetPosTowardCamera @0x4EFAD0: the effect article sits at the position pulled 20 units toward the
// camera (pos - 20 * view direction), rotated by camera yaw - 180 about Y, plays action 0 once; the sample is played at the same time
Clump *spawnEffect(int art, const float pos[3], float yOff)
{
	if(!g_s) return nullptr;
	auto d = g_s->arts.find(art);
	if(d == g_s->arts.end()){ rm8Log("boardfx: no model for effect art %d", art); return nullptr; }
	auto a = d->second.anims.find(0);
	float p[3] = {pos[0] - g_camAt[0] * 20.0f, pos[1] + yOff - g_camAt[1] * 20.0f, pos[2] - g_camAt[2] * 20.0f}, one[3] = {1, 1, 1};
	return rm8SceneAddArticle(*g_s, d->second.dff, d->second.texdir, p, one, g_camYaw - 180.0f, a == d->second.anims.end() ? "-" : a->second, "-", "once");
}

bool clipDone(Clump *c)	// a "once" animation reached its last frame (the original's finish callback)
{
	if(!g_s) return true;
	for(const Rm8ArtAnim &x : g_s->anims) if(x.clump == c){ const AnimInterpolator *ip = x.hier->interpolator; return !ip->currentAnim || ip->currentTime >= ip->currentAnim->duration - 2e-4f; }
	return true;	// no animation: nothing to wait for
}

bool playAction(const Rm8BuildModel &m, int action, bool once)
{
	if(!g_s || !m.clump) return false;
	auto d = g_s->arts.find(m.art);
	if(d == g_s->arts.end()) return false;
	auto a = d->second.anims.find(action);
	return a != d->second.anims.end() && rm8SceneArticleAnim(*g_s, m.clump, a->second, once);
}

struct BuildTask {
	int seat = 0; float pos[3] = {0, 0, 0}; bool resid = true; int oldLevel = 0;
	Rm8BuildModel model;				// the model on the plot (the old one until Complete, then the new one)
	std::function<Rm8BuildModel()> apply;		// Land_OnEnter: writes the level and swaps the model
	int eff = 0;					// art 6001 (eff\Eff_10) as an effect-player handle (present.cpp: its role.bin timed smoke event 10 at 0.9 s)
	int state = 0; float timer = 0;			// [V] +0x18 state (0 wait, 1 old model animating, 2 new model animating), +0x1C timer
	bool oldPlaying = false, newPlaying = false, finished = false;
	int sample = 0;					// [V] +0x20 the sample 2 handle (Cls_5e6618__vf13_Cleanup stops it)
};
std::vector<BuildTask> g_builds;

void complete(BuildTask &b)	// [V] NPC_CompleteInteraction @0x4F4D70
{
	b.state = 2;
	if(g_trace) rm8Log("boardfx: build complete (level written)");
	b.model = b.apply();				// Land_OnEnter(kind, level, -1): the new level and model
	b.newPlaying = playAction(b.model, b.resid ? 0 : 2, true);	// [V] vf23(residential 0 / commercial 2, 1, callback)
	if(b.model.clump) snd(0x7A);			// [V] sample 0x7A (only when the new model exists)
	else b.finished = true;				// [V] iVar1 < 0: vf16 at once
}

void finalizeBuild(BuildTask &b)	// [V] Building_UpdateBuildingVisual @0x4F4A40
{
	playAction(b.model, b.resid ? 2 : 0, false);	// [V] residential with a level (always after a build): action 2, otherwise 0, looped (count 0)
	b.finished = true;				// [V] +0xC = 0x80000000
}

}	// namespace

void rm8BoardFxSetCamera(Camera *cam)
{
	if(!cam) return;
	g_cam = cam;
	const V3d &at = cam->getFrame()->getLTM()->at;
	if(fabsf(at.x) + fabsf(at.z) < 1e-4f) return;
	g_camAt[0] = at.x; g_camAt[1] = at.y; g_camAt[2] = at.z;
	float y = atan2f(-at.z, -at.x) * 180.0f / 3.14159265f;	// same rule as roles.cpp: the game camera at (0.54, -0.64, -0.54) gives 135
	if(y < 0) y += 360.0f;
	g_camYaw = y;
}

float rm8BoardFxCamYaw(){ return g_camYaw; }

bool rm8BoardFxBuildStart(int seat, const float pos[3], bool residential, int oldLevel, Rm8BuildModel oldModel, std::function<Rm8BuildModel()> apply)
{
	if(!g_s || !apply || g_s->arts.find(6001) == g_s->arts.end()) return false;
	BuildTask b; b.seat = seat; b.resid = residential; b.oldLevel = oldLevel; b.model = oldModel; b.apply = apply;
	for(int k = 0; k < 3; k++) b.pos[k] = pos[k];
	{	// [V] Building_GetPosTowardCamera @0x4EFA80 + Mesh yaw camera - 180, action 0 once; through the effect player so the role.bin timed particle event (smoke, effect 10 at 0.9 s on [@1]) runs
		float ep[3] = {pos[0] - g_camAt[0] * 20.0f, pos[1] - g_camAt[1] * 20.0f, pos[2] - g_camAt[2] * 20.0f};
		b.eff = rm8FxPlay(6001, ep, g_camYaw - 180.0f, 0, true, 0, 1.0f, 0, false);
	}
	if(!b.eff) return false;
	rm8TurnFocus(pos);				// [V] NPC_CreateAndDisplay @0x4F4B50 -> vf+0x44 = Obj_CallHandlerOnField4 @0x4F4AD0: Camera_SetFocusPoint(plot + 0x88): the camera moves to the plot
	if(g_trace) rm8Log("boardfx: build start seat %d (old level %d) at t=%.2f", seat, oldLevel, g_time);
	if(g_trace) rm8Log("boardfx: sound 0x2 t=%.2f", g_time);
	b.sample = rm8SndPlay(2, 1);		// [V] sample 2 with the effect (stopped by Cls_5e6618__vf13_Cleanup @0x4F4EC0 only when cut short: rm8BoardFxBuildSkip)
	rm8TurnSetState(seat, 9);			// [V] Role_BuildOnLand: role state 9 while the task runs
	g_builds.push_back(b);
	return true;
}

static void buildsClear(){ g_builds.clear(); }

// ---- plot effect tasks (docs/effects.md) -------------------------------------------------------------------------------------
// The scene tasks of the property effects (g_GameState + 0x34, vtables 0x5E6618 .. 0x5E6A78): the role is in state 9 until the task has finished
// (Game_EndTurn @0x4A48D0: 9 -> 8), and every change of the plot's data (level, owner) happens at the original's moment, not when the message arrives.
// The effect articles are placed like Building_GetPosTowardCamera @0x4EFA80: the plot position pulled 20 units toward the camera, yaw = camera yaw - 180.
namespace {

int fxAtPlot(int art, const float pos[3], float scale, int sound)	// [V] CArticleMgr vf+0x40 (pos, art) + vf23(0, 1, callback) + Building_GetPosTowardCamera
{
	float p[3] = {pos[0] - g_camAt[0] * 20.0f, pos[1] - g_camAt[1] * 20.0f, pos[2] - g_camAt[2] * 20.0f};
	return rm8FxPlay(art, p, g_camYaw - 180.0f, 0, true, sound, scale, 0, false);
}
void fxDrop(int &h){ if(h){ rm8FxStop(h); h = 0; } }
void taskEnd(int seat){ if(rm8TurnState(seat) == 9) rm8TurnSetState(seat, 8); }	// [V] Game_EndTurn @0x4A48D0 -> Role_UpdateStatusTransition @0x4860D0
bool plotClip(int terra, Rm8BuildModel &m){ return rm8GameTerraModel(terra, m.clump, m.art); }

// ---- the demolition: [V] NPC_InitDisplay @0x4F4F20 (class 0x5E66B8): art 0x1772 = 6002 eff\Eff_01 (scale 0.5 on a residential plot) + sample 3, the building plays action 1 (residential) /
// 3 once; when the effect's animation has ended (callback vf4 @0x4F5100) BuildLevel_StepDecrement @0x4EF860: level - 1 (0: the building goes, the owner's sign stays)
struct DemoTask { int seat = 0, terra = 0; bool resid = true; int fx = 0; bool done = false; };
std::vector<DemoTask> g_demos;

void demoFinish(DemoTask &d)	// vf4: the effect article is released, the level written
{
	fxDrop(d.fx);
	int lv = rm8GameTerraLevel(d.terra);
	if(lv > 0) rm8GameSetTerra(d.terra, -2, lv - 1, -1);	// [V] Land_OnEnter(type, level - 1, -1) (0: BuildObj_ResetAll(1, 1): the owner's sign)
	if(g_trace) rm8Log("boardfx: demolition of plot %d done (level %d -> %d) t=%.2f", d.terra, lv, std::max(0, lv - 1), g_time);
	d.done = true;
}

// ---- blinking plots: [V] Cls_5e6938 (the wizard, @0x4F5D50), Cls_5e67f8 (Land_ClearOwner, @0x4F57A0), Cls_5e6848 (Land_SetOwner + sample 1, @0x4F58D0), Cls_5e6898 (the exchange,
// @0x4F5600 + sample 4 @0x4F5690), Cls_5e69d8 (a whole street, @0x4F4130 + sample 1 @0x4F4240): the pads (alpha 0x60) and buildings (dark grey) are dimmed at once,
// every 0.4 s the counter 2, 3, 4 undoes / repeats the dimming (counter & 1), at 5 (1.6 s) the end action runs
struct BlinkTask { int seat = 0, counter = 1, sample = 0; float timer = 0; std::vector<int> plots; std::function<void()> onEnd; };
std::vector<BlinkTask> g_blinks;

void blinkApply(const std::vector<int> &plots, bool on)	// BuildObj_SetAlpha(on): pad alpha 0x60 / 0xFF, the building tinted (0x60, 0x60, 0x60) / restored
{
	for(int t : plots){
		if(g_s) rm8SceneSetTerraAlpha(*g_s, t, on ? 0x60 : 0xFF);
		Rm8BuildModel m; if(plotClip(t, m)) rm8SceneTintArticle(m.clump, on ? 0x60 : 255);
	}
}

// ---- the angel card: [V] TerraMgr_GetBuildableRoles @0x4F2040 -> Building_AnimateMultiple @0x4F3B20 (class 0x5E6988): every plot of the street with an owner and level < 5 gets
// art 0x17E9 = 6121 (particle 22) and its old model plays action 1 / 3; the first one's end (callback vf3 @0x4F3DB0) starts Building_ProcessAnimation @0x4F3DE0: every plot
// gets level + 1 (Land_OnEnter, an empty commercial plot a random building 2..4) and the new model plays action 0 / 2; the first new animation's end finishes the task
struct AngelTask { int seat = 0, phase = 0, lead = -1; std::vector<int> plots; std::vector<int> fx; int sample = 0; };
std::vector<AngelTask> g_angels;

// ---- the crash sequence: [V] Role_StartEffectOnRandomOwnTerras @0x487780 -> Cls_5e6a28 @0x4F4380: per plot: the camera moves to it, art 0x178C = 6028 eff\crash (scale 0.5 on a
// residential plot) + sample 0x67, 1.5 s later BuildObj_ResetAll(1, 1), 1.0 s later the next plot
struct CrashTask { int seat = 0, state = 1; float timer = 0; std::vector<int> plots; size_t idx = 0; int fx = 0; };
std::vector<CrashTask> g_crashes;

// ---- the monster: [V] u_NPC_Chest_Construct @0x4EF000 -> NPC_TreasureChest_Init @0x4F51C0 (class 0x5E6758): art 0x1774 = 6004 eff\Eff_07 + sample 6, 1.5 s later
// (Cls_5e6758__vf1 @0x4F52E0) BuildObj_ResetAll(1, 1); the task ends with the effect's animation (callback vf4 @0x4F5330 destroys it and resets again)
struct MonsterTask { int seat = 0, terra = 0, fx = 0; float timer = 0; bool reset = false; };
std::vector<MonsterTask> g_monsters;

void plotTasksClear(){ for(DemoTask &d : g_demos) fxDrop(d.fx); for(AngelTask &a : g_angels) for(int h : a.fx) rm8FxStop(h); for(CrashTask &c : g_crashes) fxDrop(c.fx); for(MonsterTask &m : g_monsters) fxDrop(m.fx); g_demos.clear(); g_blinks.clear(); g_angels.clear(); g_crashes.clear(); g_monsters.clear(); }

}	// namespace

static void plotsClear(){ plotTasksClear(); }

bool rm8BoardFxDemolish(int seat, int terra)
{
	Rm8BuildModel m;
	if(!g_s || rm8GameTerraLevel(terra) <= 0 || !plotClip(terra, m)) return false;	// [V] NPC_InitDisplay: no level or no article: nothing to animate (the caller steps the level at once)
	float p[3]; if(!rm8GameTerraPos(terra, p)) return false;
	DemoTask d; d.seat = seat; d.terra = terra; d.resid = rm8GameTerraType(terra) == 1;
	d.fx = fxAtPlot(6002, p, d.resid ? 0.5f : 1.0f, 3);	// [V] art 6002, sample 3 (the handle is not kept: only the cut-short path would stop it)
	if(!d.fx) return false;
	playAction(m, d.resid ? 1 : 3, true);			// [V] vf23(building, action 1 / 3, once, no callback)
	rm8TurnFocus(p);
	rm8TurnSetState(seat, 9);
	if(g_trace) rm8Log("boardfx: demolition of plot %d starts (seat %d, level %d) t=%.2f", terra, seat, rm8GameTerraLevel(terra), g_time);
	g_demos.push_back(d);
	return true;
}

bool rm8BoardFxBlink(int seat, const std::vector<int> &plots, int endSample, std::function<void()> onEnd)
{
	if(!g_s || plots.empty()) return false;
	BlinkTask b; b.seat = seat; b.plots = plots; b.onEnd = onEnd; b.sample = endSample;
	blinkApply(b.plots, true);	// [V] BuildObj_SetAlpha(1) at once
	rm8TurnSetState(seat, 9);
	if(g_trace) rm8Log("boardfx: %d plot(s) blink (seat %d) t=%.2f", (int)plots.size(), seat, g_time);
	g_blinks.push_back(b);
	return true;
}

bool rm8BoardFxAngelBuild(int seat, int street)
{
	if(!g_s) return false;
	AngelTask a; a.seat = seat;
	for(int t = 0; t < rm8GameTerraCount() && a.plots.size() < 63; t++)
		if(rm8GameTerraStreet(t) == street && rm8GameTerraOwner(t) >= 0 && rm8GameTerraLevel(t) < 5) a.plots.push_back(t);	// [V] +0x50 == street, +0x4C >= 0, +0x44 < 5
	if(a.plots.empty()) return false;
	for(size_t i = 0; i < a.plots.size(); i++){
		int t = a.plots[i]; float p[3]; Rm8BuildModel m;
		if(!rm8GameTerraPos(t, p) || !plotClip(t, m)) continue;
		int h = fxAtPlot(6121, p, 1.0f, 0); if(h) a.fx.push_back(h);	// [V] vf+0x40(art 0x17E9)
		bool res = rm8GameTerraType(t) == 1;
		bool playing = playAction(m, res ? 1 : 3, true);			// [V] vf23(old model, 1 / 3, once)
		if(a.lead < 0 && playing){ a.lead = (int)i; snd(0x1E); }	// [V] the first one gets the callback and the sample 0x1E
	}
	float p0[3]; if(rm8GameTerraPos(a.plots[0], p0)) rm8TurnFocus(p0);
	rm8TurnSetState(seat, 9);
	if(g_trace) rm8Log("boardfx: angel build on street %d: %d plots, lead %d t=%.2f", street, (int)a.plots.size(), a.lead, g_time);
	g_angels.push_back(a);
	return true;
}

bool rm8BoardFxCrash(int seat, const std::vector<int> &plots)
{
	if(!g_s || plots.empty()) return false;
	CrashTask c; c.seat = seat; c.plots = plots;
	rm8TurnSetState(seat, 9);
	g_crashes.push_back(c);
	return true;
}

bool rm8BoardFxMonster(int seat, int terra)
{
	float p[3];
	if(!g_s || !rm8GameTerraPos(terra, p)) return false;
	MonsterTask m; m.seat = seat; m.terra = terra;
	m.fx = fxAtPlot(6004, p, 1.0f, 6);	// [V] art 0x1774, sample 6
	if(!m.fx) return false;
	rm8TurnFocus(p);
	rm8TurnSetState(seat, 9);
	g_monsters.push_back(m);
	return true;
}

void rm8BoardFxPlotUpdate(float dt)
{
	for(size_t i = 0; i < g_demos.size();){
		DemoTask &d = g_demos[i];
		if(!d.done && rm8FxFinished(d.fx)) demoFinish(d);	// [V] the effect's callback vf4 @0x4F5100
		if(d.done){ taskEnd(d.seat); g_demos.erase(g_demos.begin() + i); continue; }
		i++;
	}
	for(size_t i = 0; i < g_blinks.size();){
		BlinkTask &b = g_blinks[i];
		b.timer += dt;
		if(b.timer >= 0.4f){
			b.timer = 0; b.counter++;
			if(b.counter < 5) blinkApply(b.plots, (b.counter & 1) != 0);	// [V] counter 2: off, 3: on, 4: off
			else{
				blinkApply(b.plots, false);
				if(b.sample > 0) snd(b.sample);			// [V] sample 1 (owner set) / 4 (exchange)
				if(b.onEnd) b.onEnd();					// [V] the vf3 / vf16 of the class: Land_SetOwner / Land_ClearOwner
				if(g_trace) rm8Log("boardfx: blink ends (seat %d) t=%.2f", b.seat, g_time);
				taskEnd(b.seat); g_blinks.erase(g_blinks.begin() + i); continue;
			}
		}
		i++;
	}
	for(size_t i = 0; i < g_angels.size();){
		AngelTask &a = g_angels[i];
		bool fin = false;
		for(size_t k = 0; k < a.fx.size();){ if(rm8FxFinished(a.fx[k])){ rm8FxStop(a.fx[k]); a.fx.erase(a.fx.begin() + k); } else k++; }	// [V] vf4 ClearElements
		if(a.phase == 0){
			Rm8BuildModel lead; bool leadDone = a.lead < 0 || !plotClip(a.plots[a.lead], lead) || clipDone(lead.clump);
			if(leadDone){	// [V] Building_ProcessAnimation @0x4F3DE0
				a.phase = 1; a.lead = -1;
				for(size_t k = 0; k < a.plots.size(); k++){
					int t = a.plots[k], lv = rm8GameTerraLevel(t) + 1;
					if(lv >= 6) continue;
					bool res = rm8GameTerraType(t) == 1;
					int build = res ? 1 : (lv == 1 ? rm8GameRand() % 3 + 2 : rm8GameTerraBuild(t));
					rm8GameSetTerra(t, -2, lv, build);							// [V] Land_OnEnter(type, level + 1, -1)
					Rm8BuildModel m;
					if(plotClip(t, m) && playAction(m, res ? 0 : 2, true) && a.lead < 0) a.lead = (int)k;	// [V] new model: action 0 / 2 once, the first with the callback
				}
				if(g_trace) rm8Log("boardfx: angel levels written, lead %d t=%.2f", a.lead, g_time);
				if(a.lead < 0) fin = true;
			}
		}else{
			Rm8BuildModel m; if(a.lead < 0 || !plotClip(a.plots[a.lead], m) || clipDone(m.clump)) fin = true;
		}
		if(fin){	// [V] Cls_5e6988__vf16 @0x4F36D0: the task ends ([A] the models go on with their idle loop)
			for(int t : a.plots){ Rm8BuildModel m; if(plotClip(t, m)) playAction(m, rm8GameTerraType(t) == 1 ? 2 : 0, false); }
			for(int h : a.fx) rm8FxStop(h);
			if(g_trace) rm8Log("boardfx: angel build done t=%.2f", g_time);
			taskEnd(a.seat); g_angels.erase(g_angels.begin() + i); continue;
		}
		i++;
	}
	for(size_t i = 0; i < g_crashes.size();){
		CrashTask &c = g_crashes[i];
		if(c.idx >= c.plots.size()){ fxDrop(c.fx); taskEnd(c.seat); g_crashes.erase(g_crashes.begin() + i); continue; }
		int t = c.plots[c.idx]; float p[3];
		if(c.state == 1){ if(rm8GameTerraPos(t, p)) rm8TurnFocus(p); c.state = 2; }	// [V] vf+0x44(plot + 0x88): the camera
		else if(c.state == 2){ if(rm8GameTerraPos(t, p)) c.fx = fxAtPlot(6028, p, rm8GameTerraType(t) == 1 ? 0.5f : 1.0f, 0x67); c.timer = 0; c.state = 3; }	// [V] Building_SpawnEffectArticle @0x4F4590
		else if(c.state == 3){ c.timer += dt; if(c.timer >= 1.5f){ rm8GameSetTerra(t, -2, 0, -1); c.timer = 0; c.state = 4; } }	// [V] BuildObj_ResetAll(1, 1)
		else if(c.state == 4){ c.timer += dt; if(c.timer >= 1.0f){ fxDrop(c.fx); c.idx++; c.state = 1; } }
		i++;
	}
	for(size_t i = 0; i < g_monsters.size();){
		MonsterTask &m = g_monsters[i];
		m.timer += dt;
		if(!m.reset && m.timer >= 1.5f){ rm8GameSetTerra(m.terra, -2, 0, -1); m.reset = true; }	// [V] Cls_5e6758__vf1: NPC_Roadblock_Handle -> BuildObj_ResetAll(1, 1)
		if(rm8FxFinished(m.fx)){	// [V] the effect's callback vf4 @0x4F5330: destroys it, resets again, task done
			if(!m.reset) rm8GameSetTerra(m.terra, -2, 0, -1);
			fxDrop(m.fx); taskEnd(m.seat); g_monsters.erase(g_monsters.begin() + i); continue;
		}
		i++;
	}
}

// [V] Cls_5e6618__vf13_Cleanup @0x4F4EC0 (scene vf13 @0x4A3060 while this task is the scene task, docs/skip.md): vf4 @0x4F4A00 destroys the effect article,
// sample 2 stops, state < 2 -> NPC_CompleteInteraction @0x4F4D70 (Land_OnEnter writes the new level, new model action + sample 0x7A), then vf16
// Building_UpdateBuildingVisual @0x4F4A40 (idle action, +0xC = done). Always 1 while the task exists; the task ends on its next update.
bool rm8BoardFxBuildSkip()
{
	if(g_builds.empty()){	// [V] Cls_5e66b8__vf13 @0x4F50E0 (the demolition task, scene vf13): vf4 at once - the effect is released and the level written - returns 1
		if(g_demos.empty()) return false;
		for(DemoTask &d : g_demos) if(!d.done) demoFinish(d);
		return true;
	}
	BuildTask &b = g_builds.front();
	if(b.eff){ rm8FxStop(b.eff); b.eff = 0; }
	if(b.sample){ rm8SndStop(b.sample); b.sample = 0; }
	if(b.state < 2) complete(b);
	if(!b.finished) finalizeBuild(b);
	return true;
}

void rm8BoardFxBuildsUpdate(float dt)
{
	for(size_t i = 0; i < g_builds.size();){
		BuildTask &b = g_builds[i];
		if(b.eff && rm8FxFinished(b.eff)){ rm8FxStop(b.eff); b.eff = 0; }	// [V] callback vf4 @0x4F4A00 destroys the effect article
		if(b.state == 0){
			b.timer += dt;
			if(b.timer >= 1.5f){		// [V] 1.5 s: NPC_PlayAnimOrComplete @0x4F4CC0
				b.state = 1; b.timer = 0;
				bool old = b.model.clump && b.oldLevel >= 1 && b.oldLevel <= 4;	// [V] the old model only animates for levels 1..4
				b.oldPlaying = old && playAction(b.model, b.resid ? 1 : 3, true);	// [V] action 1 (residential) / 3 once
				if(!b.oldPlaying) complete(b);
			}
		}else if(b.state == 1){
			if(!b.oldPlaying || clipDone(b.model.clump)) complete(b);
		}else if(b.state == 2 && !b.finished){
			if(!b.newPlaying || clipDone(b.model.clump)) finalizeBuild(b);
		}
		if(b.finished && !b.eff){			// [V] Cls_5e6618__vf1: done when the effect article is gone and +0xC has bit 31
			if(g_trace) rm8Log("boardfx: build task finished");
			rm8TurnSetState(b.seat, 8);		// [V] Game_EndTurn @0x4A48D0: role 9 -> 8
			g_builds.erase(g_builds.begin() + i);
			continue;
		}
		i++;
	}
}

// ---- bankruptcy ---------------------------------------------------------------------------------------------------------
// [V] message 0x7F -> CPlayerTag__OnMovieDrama @0x492F30 (queued by Role_CheckBankruptcy @0x48EE10 after the "...<str#91>" box and the
// expression 0x1A): focus on the role, Anim_CreateSprite(0x1776 = 6006 eff\Glost, role pos, action 0, count 1, sample -1 = none), board
// music stopped and track 0x68 = 104 started, role state 0xE with the wait timer 14 s * (1 - min((speed - 1) / 10, 1)) (Role_TickWaitTimer
// @0x4850B0 -> state 8; the state-8 hook at @0x48...(line 64761 of the decompile) stops track 104 and resumes the board music).
namespace {
struct Ghost { bool on = false; int seat = 0; Clump *c = nullptr; float t = 0, dur = 0; } g_ghost;
}

void rm8BoardFxBankruptcy(int seat)
{
	float p[3];
	if(!g_s || g_ghost.on || !rm8RolesGetPos(seat, p)) return;
	rm8TurnFocus(p);
	g_ghost.c = spawnEffect(6006, p, 30.0f);	// [V] Anim_CreateSprite: y + 30, pulled 20 units toward the camera
	rm8MusicPlay(104, false);			// [V] CDigitalAudio_StopMusic(0) + CDigitalAudio_PlayBGMFaded(0x68, ...): one play of mus0104
	g_ghost.on = true; g_ghost.seat = seat; g_ghost.t = 0;
	if(g_trace) rm8Log("boardfx: bankruptcy cutscene seat %d (%s) at t=%.2f", seat, g_ghost.c ? "ghost" : "no ghost model", g_time);
	float scale = 1.0f - std::min((rm8GameSpeed - 1) / 10.0f, 1.0f);	// [V] Anim_ApplyDifficultyScale_0047b3b0 @0x47B3B0
	g_ghost.dur = 14.0f * scale;
	rm8TurnSetState(seat, 9);			// [A] the port has no state 0xE: 9 = nothing dispatches until we release it
}

static void ghostClear(){ g_ghost = Ghost(); }

void rm8BoardFxGhostUpdate(float dt)
{
	if(!g_ghost.on) return;
	g_ghost.t += dt;
	if(g_ghost.t < g_ghost.dur) return;
	if(g_ghost.c && g_s) rm8SceneRemoveArticle(*g_s, g_ghost.c);
	int seat = g_ghost.seat;
	g_ghost = Ghost();
	if(g_trace) rm8Log("boardfx: bankruptcy cutscene end at t=%.2f", g_time);
	if(g_s && !g_s->mapId.empty()) rm8MusicPlayBoard(g_s->mapId);	// [A] the original resumes the stored position (ManageBGMTransition(-5)); the port restarts the playlist
	rm8TurnSetState(seat, 8);
}

// [V] Role_StopMoveAndIdle @0x484790 for the role state 0xE (the bankruptcy movie wait; docs/skip.md): timer 0, the sprite released, the movie music
// stopped and the board music resumed (the same end as the timer), state 8.
bool rm8BoardFxGhostSkip(int seat)
{
	if(!g_ghost.on || g_ghost.seat != seat) return false;
	g_ghost.t = g_ghost.dur;
	rm8BoardFxGhostUpdate(0);
	return true;
}

void rm8BoardFxCancel(){ g_playing.clear(); buildsClear(); ghostClear(); plotsClear(); }

// debugging: RM8_BOARDFX_TESTFX="<art>:<seconds>" plays that effect article (6001 build, 6002 demolish, 6006 ghost) once, on the character nearest to the
// centre of the screen, at that scene time (offscreen shots use a fixed camera, so the current role may be out of view)
void rm8BoardFxTestFx(float dt)
{
	static const char *env = getenv("RM8_BOARDFX_TESTFX");
	static float t = 0; static bool done = false;
	if(!env || done || !g_cam || !g_cam->frameBuffer) return;
	t += dt;
	int art = 0; float at = 0;
	if(sscanf(env, "%d:%f", &art, &at) != 2 || t < at) return;
	done = true;
	const Matrix *cm = g_cam->getFrame()->getLTM();
	float W = (float)g_cam->frameBuffer->width, H = (float)g_cam->frameBuffer->height, best = 1e9f, p[3] = {0, 0, 0};
	for(int q = 0; q < rm8TurnRoleCount(); q++){
		float rp[3]; if(!rm8RolesGetPos(q, rp)) continue;
		V3d d = sub({rp[0], rp[1], rp[2]}, cm->pos); float zc = dot(d, cm->at); if(zc < 1) continue;
		float sx = (0.5f - dot(d, cm->right) / (2 * g_cam->viewWindow.x * zc)) * W, sy = (0.5f - dot(d, cm->up) / (2 * g_cam->viewWindow.y * zc)) * H;
		float dd = hypotf(sx - W / 2, sy - H / 2);
		if(dd < best){ best = dd; p[0] = rp[0]; p[1] = rp[1]; p[2] = rp[2]; }
	}
	rm8Log("boardfx: test effect art %d -> %s", art, spawnEffect(art, p, 0.0f) ? "ok" : "failed");
}

// debugging: RM8_FX_TEST="<sec>:focus:<x>:<z>;<sec>:set:<plot>:<owner>:<level>[:<build>];<sec>:msg:<seat|-1>:<cmd>:<a>:<b>;..." (docs/effects.md): at that scene time the plot gets owner / level (build 2..4 for a
// commercial one) at once, or the message is put at the front of the seat's queue (-1 = the current role) as soon as that role waits (state 6 / 8). Items run in order.
void rm8BoardFxTestMsgs(float dt)
{
	static const char *env = getenv("RM8_FX_TEST");
	static std::vector<std::string> items; static size_t next = 0; static float t = 0; static bool parsed = false;
	if(!env || dt <= 0) return;
	if(!parsed){ parsed = true; std::string cur; for(const char *q = env;; q++){ if(*q == ';' || !*q){ if(!cur.empty()) items.push_back(cur); cur.clear(); if(!*q) break; } else cur += *q; } }
	static float hold[3] = {0, 0, 0}; static bool holding = false;
	if(holding) rm8TurnFocus(hold);	// "focus": the camera of a RM8_SHOT_FOLLOW shot stays on that point
	if(next >= items.size() || rm8TurnRoleCount() == 0) return;
	t += dt;
	float at = (float)atof(items[next].c_str());
	if(t < at) return;
	size_t c1 = items[next].find(':');
	std::string rest = c1 == std::string::npos ? "" : items[next].substr(c1 + 1);
	long v[6] = {0, 0, 0, 0, 0, 0}; int n = 0;
	for(size_t q = rest.find(':'); q != std::string::npos && n < 6; ){
		size_t e = rest.find(':', q + 1);
		v[n++] = strtol(rest.substr(q + 1, e == std::string::npos ? std::string::npos : e - q - 1).c_str(), nullptr, 0);
		q = e;
	}
	if(!rest.compare(0, 5, "focus")){ hold[0] = (float)v[0]; hold[2] = (float)v[1]; holding = true; rm8Log("fx test [%.2f]: camera held on (%ld, %ld)", t, v[0], v[1]); }
	else if(!rest.compare(0, 3, "set")){
		rm8GameSetTerra((int)v[0], (int)v[1], (int)v[2], n > 3 ? (int)v[3] : -1);
		rm8Log("fx test [%.2f]: plot %ld owner %ld level %ld", t, v[0], v[1], v[2]);
	}else if(!rest.compare(0, 3, "msg")){
		int seat = v[0] < 0 ? rm8TurnCurrent() : (int)v[0];
		if(seat < 0 || seat >= rm8TurnRoleCount() || (rm8TurnState(seat) != 8 && rm8TurnState(seat) != 6)) return;
		rm8TurnEnqueueAt(seat, 0, (int)v[1], (int)v[2], (int)v[3]);
		rm8Log("fx test [%.2f]: seat %d message 0x%lX (%ld, %ld)", t, seat, v[1], v[2], v[3]);
	}
	next++;
}
