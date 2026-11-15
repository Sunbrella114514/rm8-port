// Particle effects (PrtEffTable), after docs/particles.md: the game uses RenderWare's RpPrtStd/RpPrtAdv/RpPTank
// plug-ins; this is a re-implementation of the parts the 26 effects use (kind 0, simple_mode 0, box/sphere/none
// shapes, 2-key and multi-key colours, UV stepping), with the RW random sequence (per-emitter LCG, seed 1) and
// update order, so the emitted particles match scripts/prteff.py (the reference simulator) frame by frame.
// [V] facts are cited in docs/particles.md by address; [A] marks the port's own choices.
#include "scene.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace rw;

namespace {

struct UVFrame { float u0, v0, u1, v1; int ms; };
struct ColorKey { float t, tb; float c[4], cb[4]; };
struct EffDef {
	int id = 0, shape = 0, maxPrt = 0, num = 0, numb = 0, hasSize = 0, hasRot = 0, src = 5, dst = 6, valpha = 1, suse = 0, nkeys = 0;
	float gap = 0, gapb = 0, life = 0, lifeb = 0, vel = 0, velb = 0, dir[3] = {}, dirb[3] = {}, force[3] = {}, emt[3] = {};
	float ss[2] = {}, se[2] = {}, ssb[2] = {}, seb[2] = {}, rs = 0, rsb = 0, re = 0, reb = 0, dur = 0, sr = 0, srg = 0;
	std::string tex;
	std::vector<UVFrame> uv;
	std::vector<ColorKey> keys;
};
std::map<int, EffDef> g_defs;
bool g_defsLoaded = false;

inline float f32(double x){ return (float)x; }

struct Rng {
	uint32 seed = 1;
	// [V] 0x59F0E0: s = s*0xBB38435 + 0x3619636B; r = 2*s/2^32 - 1
	double next(){ seed = seed * 0xBB38435u + 0x3619636Bu; return seed * 2.3283064365386963e-10 * 2.0 - 1.0; }
	double nextSigned01(){ seed = seed * 0xBB38435u + 0x3619636Bu; return (double)(int32)seed * 2.3283064365386963e-10 + 0.5; }
};

struct Seg { float t; float start[4], delta[4]; };
struct UVStep { float t; float uv[4]; };
struct Particle {
	float cur = 0, end = 0, inv = 0;
	float vel[3], pos[3];
	bool hasSize = false; float sizeStart[2], sizeDelta[2], size[2];
	bool hasCol = false; float colStart[4], colDelta[4]; uint8 color[4] = {255, 255, 255, 255};
	bool hasRot = false; float rotStart = 0, rotDelta = 0, rot = 0;
	float uvStart[4], uvDelta[4], uv[4];
	int mcIdx = -1; std::vector<Seg> mc;
	int stIdx = -1; std::vector<UVStep> st;
};

struct Mat { float right[3], up[3], at[3], pos[3]; };
void matVec(const Mat &m, const float v[3], float o[3]){ for(int i = 0; i < 3; i++) o[i] = v[0]*m.right[i] + v[1]*m.up[i] + v[2]*m.at[i]; }
void matPoint(const Mat &m, const float v[3], float o[3]){ matVec(m, v, o); for(int i = 0; i < 3; i++) o[i] += m.pos[i]; }
void normalize3(float v[3]){
	double l2 = (double)v[0]*v[0] + (double)v[1]*v[1] + (double)v[2]*v[2];
	if(l2 == 0.0){ v[0] = v[1] = v[2] = 0; return; }	// [V] RwV3dNormalize 0x520970: zero stays zero
	double k = 1.0 / sqrt(l2);
	for(int i = 0; i < 3; i++) v[i] = (float)(v[i] * k);
}

struct Emitter {
	const EffDef *e = nullptr;
	Frame *hook = nullptr;		// [V] 0x46CBF0: the hook frame's LTM is copied every frame
	Mat ltm;
	Rng rng;
	float curTime = 0, emitTime = 0, elapsed = 0;
	bool emitOn = true;
	std::vector<Particle> prt;
	Texture *tex = nullptr;
	float texStart[4], texEnd[4];
	std::vector<UVStep> stepItems;	// time fraction + frame uv
	std::vector<ColorKey> mcItems;	// middle colour keys
	float colS[4], colE[4], colSB[4] = {}, colEB[4] = {};
	bool hasColor = false;

	void init(const EffDef *d){
		e = d;
		const auto &fr = d->uv;
		int nStep = (int)fr.size() - 1;
		texStart[0] = fr[0].u0; texStart[1] = fr[0].v0; texStart[2] = fr[0].u1; texStart[3] = fr[0].v1;
		texEnd[0] = fr[nStep].u0; texEnd[1] = fr[nStep].v0; texEnd[2] = fr[nStep].u1; texEnd[3] = fr[nStep].v1;
		if(nStep > 0){	// [V] UV step table: time_i = sum(ms[0..i-1]) / sum(ms[0..n-2]); the last frame is never shown
			double total = 0; for(int i = 0; i < nStep; i++) total += fr[i].ms;
			double acc = 0;
			for(int i = 0; i < nStep; i++){ UVStep s; s.t = f32(acc / total); s.uv[0] = fr[i].u0; s.uv[1] = fr[i].v0; s.uv[2] = fr[i].u1; s.uv[3] = fr[i].v1; stepItems.push_back(s); acc += fr[i].ms; }
		}
		int nk = d->nkeys;
		hasColor = nk >= 2 && (int)d->keys.size() >= nk;
		if(hasColor){
			for(int i = 0; i < 4; i++){ colS[i] = d->keys[0].c[i]; colE[i] = d->keys[nk-1].c[i]; }
			if(nk >= 3) for(int i = 0; i < 4; i++){ colSB[i] = d->keys[0].cb[i]; colEB[i] = d->keys[nk-1].cb[i]; }	// [V] n=2: biases not read
			for(int k = 1; k < nk - 1; k++) mcItems.push_back(d->keys[k]);
		}
	}

	// [V] 0x59F0E0 standard emission (random order: life, dir xyz, speed, pos xyz, size, colour, rotation, uv x8)
	int emitStandard(){
		uint32 s0 = rng.seed;
		double r = rng.next();
		int n = e->num + (int)truncf(f32(r * (double)e->numb));
		int room = e->maxPrt - (int)prt.size();
		if(n > room) n = room;
		if(n <= 0){ rng.seed = s0; return 0; }
		for(int k = 0; k < n; k++){
			Particle p;
			p.end = f32(rng.next() * e->lifeb + e->life);
			p.inv = p.end != 0.0f ? f32(1.0 / p.end) : INFINITY;
			float d[3]; for(int i = 0; i < 3; i++) d[i] = f32(rng.next() * e->dirb[i] + e->dir[i]);
			normalize3(d); float dw[3]; matVec(ltm, d, dw);
			double sp = rng.next() * e->velb + e->vel;
			for(int i = 0; i < 3; i++) p.vel[i] = f32(sp * dw[i]);
			float lp[3]; for(int i = 0; i < 3; i++) lp[i] = f32(rng.next() * e->emt[i] + 0.0);
			matPoint(ltm, lp, p.pos);
			if(e->hasSize){
				float ss[2], se[2];
				for(int i = 0; i < 2; i++) ss[i] = f32(rng.next() * e->ssb[i] + e->ss[i]);
				for(int i = 0; i < 2; i++) se[i] = f32(rng.next() * e->seb[i] + e->se[i]);
				p.hasSize = true;
				for(int i = 0; i < 2; i++){ p.sizeStart[i] = ss[i]; p.sizeDelta[i] = f32(se[i] - ss[i]); p.size[i] = ss[i]; }
			}else{ p.size[0] = e->ss[0]; p.size[1] = e->ss[1]; }
			if(hasColor){
				float cs[4], ce[4];
				for(int i = 0; i < 4; i++) cs[i] = f32(rng.next() * colSB[i] + colS[i]);
				for(int i = 0; i < 4; i++) ce[i] = f32(rng.next() * colEB[i] + colE[i]);
				p.hasCol = true;
				for(int i = 0; i < 4; i++){ p.colStart[i] = cs[i]; p.colDelta[i] = f32(ce[i] - cs[i]); p.color[i] = (uint8)((int)truncf(cs[i]) & 0xFF); }
			}
			if(e->hasRot){
				float rs = f32(rng.next() * e->rsb + e->rs);
				const float PI_F = 3.1415927f;
				if(rs > PI_F) rs = f32(rs - 6.2831855);
				if(rs < -PI_F) rs = f32(rs + 6.2831855);
				float re = f32(rng.next() * e->reb + e->re);
				p.hasRot = true; p.rotStart = rs; p.rotDelta = f32(re - rs); p.rot = rs;
			}
			float us[4], ue[4];
			for(int i = 0; i < 4; i++) us[i] = f32(rng.next() * 0.0 + texStart[i]);	// biases are 0 but still drawn
			for(int i = 0; i < 4; i++) ue[i] = f32(rng.next() * 0.0 + texEnd[i]);
			for(int i = 0; i < 4; i++){ p.uvStart[i] = us[i]; p.uvDelta[i] = f32(ue[i] - us[i]); p.uv[i] = us[i]; }
			prt.push_back(std::move(p));
		}
		return n;
	}

	// [V] 0x5A2D60 sphere
	void emitSphere(size_t first){
		for(size_t k = first; k < prt.size(); k++){
			Particle &p = prt[k];
			double phi = rng.next() * 3.1415925;
			double z = rng.next();
			double s = sqrt(std::max(0.0, 1.0 - z * z));
			double q = rng.nextSigned01();
			double R = q * e->srg + e->sr;
			float lp[3] = {(float)(R * cos(phi) * s), (float)(R * sin(phi) * s), (float)(R * z)};
			matPoint(ltm, lp, p.pos);
			if(e->suse == 1){
				float d[3] = {lp[0], lp[1], lp[2]}; normalize3(d); float dw[3]; matVec(ltm, d, dw);
				double sp = rng.next() * e->velb + e->vel;
				for(int i = 0; i < 3; i++) p.vel[i] = f32(sp * dw[i]);
			}
		}
	}

	// [V] 0x5A4140 multi-segment colour (one unused draw at entry)
	void emitMulticolor(size_t first){
		rng.next();
		int n = (int)mcItems.size();
		for(size_t k = first; k < prt.size(); k++){
			Particle &p = prt[k];
			double tPrev = rng.next() * mcItems[0].tb + mcItems[0].t;
			double cPrev[4]; for(int i = 0; i < 4; i++) cPrev[i] = rng.next() * mcItems[0].cb[i] + mcItems[0].c[i];
			double t0 = tPrev, c0[4]; memcpy(c0, cPrev, sizeof c0);
			p.mc.clear();
			for(int j = 1; j < n; j++){
				double tk = rng.next() * mcItems[j].tb + mcItems[j].t;
				Seg sg; sg.t = f32(tPrev * p.end);
				double inv = tk - tPrev > 0.0 ? 1.0 / (tk - tPrev) : 0.0;
				double ck[4]; for(int i = 0; i < 4; i++) ck[i] = rng.next() * mcItems[j].cb[i] + mcItems[j].c[i];
				for(int i = 0; i < 4; i++){ double sl = (ck[i] - cPrev[i]) * inv; sg.start[i] = f32(cPrev[i] - tPrev * sl); sg.delta[i] = f32(sl); }
				p.mc.push_back(sg);
				tPrev = tk; memcpy(cPrev, ck, sizeof cPrev);
			}
			Seg sg; sg.t = f32(tPrev * p.end);
			double inv = 1.0 - tPrev > 0.0 ? 1.0 / (1.0 - tPrev) : 0.0;
			for(int i = 0; i < 4; i++){
				double endc = (double)p.colStart[i] + p.colDelta[i];
				double sl = (endc - cPrev[i]) * inv;
				sg.start[i] = f32(cPrev[i] - tPrev * sl); sg.delta[i] = f32(sl);
			}
			p.mc.push_back(sg);
			for(int i = 0; i < 4; i++) p.colDelta[i] = f32((c0[i] - p.colStart[i]) * (1.0 / t0));
			p.mcIdx = 0;
		}
	}

	// [V] 0x5A3870 UV stepping (one unused draw at entry)
	void emitUVStep(size_t first){
		rng.next();
		for(size_t k = first; k < prt.size(); k++){
			Particle &p = prt[k];
			p.st.clear();
			for(const UVStep &it : stepItems){
				UVStep s;
				double tt = rng.next() * 0.0 + it.t;
				double a0 = rng.next() * 0.0 + it.uv[0], a1 = rng.next() * 0.0 + it.uv[1];
				double b0 = rng.next() * 0.0 + it.uv[2], b1 = rng.next() * 0.0 + it.uv[3];
				s.t = f32(tt * p.end); s.uv[0] = f32(a0); s.uv[1] = f32(a1); s.uv[2] = f32(b0); s.uv[3] = f32(b1);
				p.st.push_back(s);
			}
			p.stIdx = 0;
			for(int i = 0; i < 4; i++) p.uvDelta[i] = 0;
		}
	}

	// [V] §3.2 order: game timer, currTime += dt, particle update (multicolour, uv step, standard), emit, schedule
	void step(float dt){
		if(hook){
			const Matrix *m = hook->getLTM();
			memcpy(ltm.right, &m->right, 12); memcpy(ltm.up, &m->up, 12); memcpy(ltm.at, &m->at, 12); memcpy(ltm.pos, &m->pos, 12);
		}
		if(e->dur > 0.001f){ if(elapsed <= e->dur) elapsed = f32(elapsed + dt); else emitOn = false; }
		curTime = f32(curTime + dt);
		size_t w = 0;
		for(size_t k = 0; k < prt.size(); k++){
			Particle &p = prt[k];
			if(!(p.cur < p.end)) continue;
			float nt = dt + p.cur;
			if(p.mcIdx >= 0 && p.mcIdx < (int)p.mc.size() && p.mc[p.mcIdx].t <= nt){
				memcpy(p.colStart, p.mc[p.mcIdx].start, 16); memcpy(p.colDelta, p.mc[p.mcIdx].delta, 16); p.mcIdx++;
			}
			if(p.stIdx >= 0 && p.stIdx < (int)p.st.size() && p.st[p.stIdx].t <= nt){
				memcpy(p.uvStart, p.st[p.stIdx].uv, 16); p.stIdx++;
			}
			float t = nt * p.inv;
			p.cur = nt;
			if(t > 1.0f) t = 1.0f;
			for(int i = 0; i < 3; i++) p.vel[i] = f32(dt * e->force[i] + p.vel[i]);
			for(int i = 0; i < 3; i++) p.pos[i] = f32(dt * p.vel[i] + p.pos[i]);
			if(p.hasSize) for(int i = 0; i < 2; i++) p.size[i] = f32(t * p.sizeDelta[i] + p.sizeStart[i]);
			if(p.hasCol) for(int i = 0; i < 4; i++) p.color[i] = (uint8)((int)truncf(f32(t * p.colDelta[i] + p.colStart[i])) & 0xFF);
			for(int i = 0; i < 4; i++) p.uv[i] = f32(t * p.uvDelta[i] + p.uvStart[i]);
			if(p.hasRot) p.rot = f32(t * p.rotDelta + p.rotStart);
			if(w != k) prt[w] = std::move(p);
			w++;
		}
		prt.resize(w);
		if(emitOn && emitTime <= curTime){
			size_t first = prt.size();
			if(emitStandard() > 0){
				if(e->shape == 2) emitSphere(first);
				if(!mcItems.empty()) emitMulticolor(first);
				if(!stepItems.empty()) emitUVStep(first);
			}
		}
		if(emitTime <= curTime){
			double r = rng.next();
			emitTime = f32(r * e->gapb + e->gap + curTime);
		}
		if(e->dur > 0.001f && e->dur < elapsed) emitOn = false;
	}
};

std::vector<Emitter> g_em;

std::vector<float> nums(const std::string &s){
	std::vector<float> v; std::stringstream ss(s); std::string t;
	while(std::getline(ss, t, ',')) v.push_back((float)atof(t.c_str()));
	return v;
}

}	// namespace

bool rm8PrtLoadDefs(const std::string &path)
{
	if(g_defsLoaded) return !g_defs.empty();
	g_defsLoaded = true;
	std::ifstream f(path);
	if(!f){ rm8Log("no %s (run scripts/export_particles.py)", path.c_str()); return false; }
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; int id; ss >> kw >> id;
		EffDef &d = g_defs[id]; d.id = id;
		if(kw == "eff"){
			std::string k, v;
			while(ss >> k >> v){
				std::vector<float> n = nums(v);
				auto f1 = [&](float &x){ x = n.empty() ? 0 : n[0]; };
				auto fn = [&](float *x, int c){ for(int i = 0; i < c && i < (int)n.size(); i++) x[i] = n[i]; };
				auto i1 = [&](int &x){ x = n.empty() ? 0 : (int)n[0]; };
				if(k == "shape") i1(d.shape); else if(k == "max") i1(d.maxPrt); else if(k == "num") i1(d.num); else if(k == "numb") i1(d.numb);
				else if(k == "gap") f1(d.gap); else if(k == "gapb") f1(d.gapb); else if(k == "life") f1(d.life); else if(k == "lifeb") f1(d.lifeb);
				else if(k == "vel") f1(d.vel); else if(k == "velb") f1(d.velb); else if(k == "dir") fn(d.dir, 3); else if(k == "dirb") fn(d.dirb, 3);
				else if(k == "force") fn(d.force, 3); else if(k == "emt") fn(d.emt, 3); else if(k == "ss") fn(d.ss, 2); else if(k == "se") fn(d.se, 2);
				else if(k == "ssb") fn(d.ssb, 2); else if(k == "seb") fn(d.seb, 2); else if(k == "hassize") i1(d.hasSize);
				else if(k == "rs") f1(d.rs); else if(k == "rsb") f1(d.rsb); else if(k == "re") f1(d.re); else if(k == "reb") f1(d.reb); else if(k == "hasrot") i1(d.hasRot);
				else if(k == "src") i1(d.src); else if(k == "dst") i1(d.dst); else if(k == "valpha") i1(d.valpha); else if(k == "dur") f1(d.dur);
				else if(k == "sr") f1(d.sr); else if(k == "srg") f1(d.srg); else if(k == "suse") i1(d.suse); else if(k == "nkeys") i1(d.nkeys);
				else if(k == "tex") d.tex = v;
			}
		}else if(kw == "uv"){
			int n; ss >> n; d.uv.resize(n);
			for(UVFrame &u : d.uv) ss >> u.u0 >> u.v0 >> u.u1 >> u.v1 >> u.ms;
		}else if(kw == "key"){
			ColorKey k; ss >> k.t >> k.tb; for(float &c : k.c) ss >> c; for(float &c : k.cb) ss >> c; d.keys.push_back(k);
		}
	}
	rm8Log("particles: %d effects", (int)g_defs.size());
	return !g_defs.empty();
}

// [V] hook = n of a frame whose user data string is "[@n]..." (the whole flag word must equal n: no other flag chars)
static Frame *findHook(Frame *f, int hook)
{
	UserDataExtension *ud = UserDataExtension::get(f);
	for(int i = 0; ud && i < ud->getCount(); i++){
		UserDataArray *a = ud->get(i);
		if(!a || a->datatype != USERDATASTRING || a->numElements < 1 || !a->getString(0)) continue;
		const char *s = a->getString(0);
		if(s[0] == '[' && s[1] == '@' && s[2] && s[3] == ']'){
			int v = (s[2] >= '0' && s[2] <= '9') ? s[2] - '0' : (s[2] | 0x20) - 'a' + 10;
			if(v == hook) return f;
		}
	}
	for(Frame *c = f->child; c; c = c->next) if(Frame *r = findHook(c, hook)) return r;
	return nullptr;
}

static Emitter *spawn(int eff)
{
	auto it = g_defs.find(eff);
	if(it == g_defs.end() || it->second.uv.empty()){ rm8Log("warning: particle effect %d not defined", eff); return nullptr; }
	Emitter em; em.init(&it->second);
	em.tex = it->second.dst == BLENDINVSRCALPHA ? rm8PackTextureLumaAlpha(it->second.tex) : rm8PackTexture(it->second.tex);	// [V] 0x467D40: only frame 0's texture is bound; [A] alpha from the colour for the sprites without one (scene.cpp)
	if(!em.tex) rm8Log("warning: particle texture %s not found", it->second.tex.c_str());
	memset(&em.ltm, 0, sizeof em.ltm); em.ltm.right[0] = em.ltm.up[1] = em.ltm.at[2] = 1;
	g_em.push_back(std::move(em));
	return &g_em.back();
}

bool rm8PrtAttach(int eff, Clump *parent, int hook)
{
	Frame *f = parent ? findHook(parent->getFrame(), hook) : nullptr;
	if(!f){ rm8Log("warning: particle %d: no [@%d] frame", eff, hook); return false; }
	Emitter *e = spawn(eff);
	if(e) e->hook = f;
	return e != nullptr;
}

bool rm8PrtPlace(int eff, const float pos[3])
{
	Emitter *e = spawn(eff);
	if(e) memcpy(e->ltm.pos, pos, 12);
	return e != nullptr;
}

// [A] handle versions for the effect player (present.cpp): the handle is the emitter index; a stopped emitter lets its live particles fade out
int rm8PrtPlaceH(int eff, const float pos[3])
{
	Emitter *e = spawn(eff);
	if(!e) return -1;
	memcpy(e->ltm.pos, pos, 12);
	return (int)g_em.size() - 1;
}

int rm8PrtAttachH(int eff, Clump *parent, int hook)
{
	Frame *f = parent ? findHook(parent->getFrame(), hook) : nullptr;
	if(!f) return -1;
	Emitter *e = spawn(eff);
	if(!e) return -1;
	e->hook = f;
	return (int)g_em.size() - 1;
}

void rm8PrtStopH(int idx)
{
	if(idx < 0 || idx >= (int)g_em.size()) return;
	g_em[idx].emitOn = false; g_em[idx].hook = nullptr;	// the hook's clump may be destroyed next
}

void rm8PrtUpdate(float dt)
{
	static const bool off = getenv("RM8_NO_PARTICLES") != nullptr;	// comparison switch
	if(off || dt <= 0.0f) return;	// [A] the pose-only update at load (dt 0) is not a game update
	for(Emitter &e : g_em) e.step(dt);
}

void rm8PrtClear()
{
	for(Emitter &e : g_em) if(e.tex) e.tex->destroy();
	g_em.clear();
}

int rm8PrtCount(){ int n = 0; for(Emitter &e : g_em) n += (int)e.prt.size(); return n; }

// [V] §3.5: camera-parallel quads, X = -cam.right (screen right), Y = cam.up; full width/height = size;
// rot > 0 turns clockwise on screen; colour = texture x vertex colour; z test on, z write off; blend src/dst per
// effect with vertex alpha; [A] culling off (the doc's advice: the winding cannot be guaranteed)
void rm8PrtRender(Camera *cam)
{
	if(g_em.empty()) return;
	const Matrix *cm = cam->getFrame()->getLTM();
	V3d X = {-cm->right.x, -cm->right.y, -cm->right.z}, Y = cm->up;
	uint32 zw = GetRenderState(ZWRITEENABLE), cull = GetRenderState(CULLMODE), sb = GetRenderState(SRCBLEND), db = GetRenderState(DESTBLEND);
	uint32 va = GetRenderState(VERTEXALPHA), atf = GetRenderState(ALPHATESTFUNC);
	SetRenderState(ZTESTENABLE, 1); SetRenderState(ZWRITEENABLE, 0); SetRenderState(CULLMODE, CULLNONE);
	SetRenderState(ALPHATESTFUNC, ALPHAALWAYS);	// [V] alpha test ALWAYS(8) before the particles (0x4A1A80)
	static std::vector<gl3::Im3DVertex> v; static std::vector<uint16> idx;
	for(Emitter &e : g_em){
		if(e.prt.empty()) continue;
		v.resize(e.prt.size() * 4); idx.resize(e.prt.size() * 6);
		for(size_t k = 0; k < e.prt.size(); k++){
			const Particle &p = e.prt[k];
			float c = cosf(p.rot), s = sinf(p.rot), hw = p.size[0] * 0.5f, hh = p.size[1] * 0.5f;
			V3d A = {(c*X.x - s*Y.x) * hw, (c*X.y - s*Y.y) * hw, (c*X.z - s*Y.z) * hw};
			V3d B = {(c*Y.x + s*X.x) * hh, (c*Y.y + s*X.y) * hh, (c*Y.z + s*X.z) * hh};
			const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
			const float uu[4] = {p.uv[0], p.uv[2], p.uv[2], p.uv[0]}, vv[4] = {p.uv[3], p.uv[3], p.uv[1], p.uv[1]};
			for(int q = 0; q < 4; q++){
				gl3::Im3DVertex &o = v[k*4 + q];
				o.setX(p.pos[0] + sx[q]*A.x + sy[q]*B.x); o.setY(p.pos[1] + sx[q]*A.y + sy[q]*B.y); o.setZ(p.pos[2] + sx[q]*A.z + sy[q]*B.z);
				o.setNormalX(0); o.setNormalY(1); o.setNormalZ(0);
				o.setColor(p.color[0], p.color[1], p.color[2], p.color[3]);
				o.setU(uu[q]); o.setV(vv[q]);
			}
			uint16 b = (uint16)(k * 4);
			uint16 tri[6] = {b, uint16(b + 1), uint16(b + 2), b, uint16(b + 2), uint16(b + 3)};
			memcpy(&idx[k*6], tri, sizeof tri);
		}
		SetRenderStatePtr(TEXTURERASTER, e.tex ? e.tex->raster : nil);
		SetRenderState(TEXTUREFILTER, Texture::LINEAR);
		SetRenderState(TEXTUREADDRESS, Texture::WRAP);
		if(e.e->valpha){ SetRenderState(VERTEXALPHA, 1); SetRenderState(SRCBLEND, e.e->src); SetRenderState(DESTBLEND, e.e->dst); }
		im3d::Transform(v.data(), (int32)v.size(), nil, im3d::EVERYTHING);
		im3d::RenderIndexedPrimitive(PRIMTYPETRILIST, idx.data(), (int32)idx.size());
		im3d::End();
	}
	SetRenderState(ZWRITEENABLE, zw); SetRenderState(CULLMODE, cull); SetRenderState(SRCBLEND, sb); SetRenderState(DESTBLEND, db);
	SetRenderState(VERTEXALPHA, va); SetRenderState(ALPHATESTFUNC, atf);
}
