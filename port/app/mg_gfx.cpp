// Minigame host services: sprites (articles with a uvlisttable picture), 2D drawing, sound, strings, the lg_intro.ui panel.
// Everything is drawn in the game's 1024x768 space and mapped into the frame buffer (letterboxed, uniform scale).
#include "mg_core.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>

using namespace rw;

namespace mg {

// ---- MSVC rand() ([V] the DLLs link the CRT: seed * 214013 + 2531011, bits 16..30)
static unsigned g_seed = 1;
void srand15(unsigned s){ g_seed = s; }
int rand15(){ g_seed = g_seed * 214013u + 2531011u; return (g_seed >> 16) & 0x7fff; }

namespace {

// ---- sprite definitions (scripts/export_minigame.py)
struct Frame { float u0, v0, u1, v1; int ms, ti; };
struct Def { int art = 0, uvl = 0; float w = 0, h = 0; int loopStart = 0, loopCount = 0; std::vector<std::string> tex; std::vector<Frame> fr; };
std::map<int, Def> g_defs;

struct TexEnt { Texture *tex = nullptr; int w = 0, h = 0; std::vector<uint8> alpha; };
std::map<std::string, TexEnt> g_tex;

TexEnt *getTex(const std::string &key)
{
	auto it = g_tex.find(key);
	if(it != g_tex.end()) return it->second.tex ? &it->second : nullptr;
	TexEnt &e = g_tex[key];
	std::vector<uint8> buf;
	if(!rm8PackRead(key, buf)){ rm8Log("minigame: texture %s missing", key.c_str()); return nullptr; }
	Image *img = readTGAMemory(buf.data(), (uint32)buf.size());
	if(!img) return nullptr;
	img->convertTo32();
	e.w = img->width; e.h = img->height;
	e.alpha.resize((size_t)e.w * e.h);
	bool hasA = true;	// 24 bit TGAs are expanded with alpha 255 by convertTo32
	for(int y = 0; y < e.h; y++) for(int x = 0; x < e.w; x++) e.alpha[(size_t)y * e.w + x] = hasA ? img->pixels[y * img->stride + x * 4 + 3] : 255;
	e.tex = Texture::create(Raster::createFromImage(img));
	e.tex->setFilter(Texture::LINEAR);
	img->destroy();
	return &e;
}

// ---- sprite objects: the article's Sprite (0xe4 bytes, Sprite_Init @0x461E70) and its animation state (14 words, Article_AdvanceAnimFrame @0x468090)
struct Spr {
	bool used = false;
	const Def *d = nullptr;
	float w = 0, h = 0;			// pixel size (Sprite_GetPixelSize)
	float px = 0, py = 0;			// centre position (SetPosition)
	float sx = 1, sy = 1, rotZ = 0;		// Sprite_SetScale / Sprite_SetZRotation
	uint32_t color = 0xffffffff;
	// animation state: current uv / texture (state words 0..3, 13), mode(+0x10), idx(+0x1c), loops(+0x20), start time
	float suv[4];	int sti = 0;
	int state = 1, idx = 0, loops = 0; double start = 0, clock = 0;
	float vuv[4];	int vti = 0;		// the vertex uvs / texture, refreshed by Sprite_Update when state == 1
};
std::vector<Spr> g_spr(1);	// handle = index (0 unused)

// ---- drawing state
int g_sw = 1280, g_sh = 720;
float g_sc = 1, g_ox = 0, g_oy = 0;
std::vector<gl3::Im2DVertex> g_bv; std::vector<uint16> g_bi;
Raster *g_bRaster = nullptr; bool g_bOpen = false;
float g_z, g_rz;
uint32 g_savedZt, g_savedZw, g_savedAtf, g_savedCull, g_savedAtr;
bool g_inFrame = false;

void flush()
{
	if(!g_bi.empty()){
		SetRenderStatePtr(TEXTURERASTER, g_bRaster);
		SetRenderState(TEXTUREFILTER, Texture::LINEAR);
		im2d::RenderIndexedPrimitive(PRIMTYPETRILIST, g_bv.data(), (int32)g_bv.size(), g_bi.data(), (int32)g_bi.size());
	}
	g_bv.clear(); g_bi.clear();
}
void setTex(Raster *r)
{
	if(g_bOpen && r == g_bRaster) return;
	flush(); g_bRaster = r; g_bOpen = true;
}
// quad with corners in virtual coordinates (tl, tr, bl, br) and the uv rectangle
void quad4(const float px[4], const float py[4], float u0, float v0, float u1, float v1, uint32_t argb)
{
	if(g_bv.size() + 4 > 65000) flush();
	uint16 b = (uint16)g_bv.size();
	float pu[4] = {u0, u1, u0, u1}, pv[4] = {v0, v0, v1, v1};
	for(int q = 0; q < 4; q++){
		gl3::Im2DVertex v;
		v.setScreenX(g_ox + px[q] * g_sc); v.setScreenY(g_oy + py[q] * g_sc); v.setScreenZ(g_z);
		v.setCameraZ(1.0f / g_rz); v.setRecipCameraZ(g_rz);
		v.setColor((argb >> 16) & 255, (argb >> 8) & 255, argb & 255, argb >> 24); v.setU(pu[q], g_rz); v.setV(pv[q], g_rz);
		g_bv.push_back(v);
	}
	uint16 idx[6] = {b, uint16(b + 1), uint16(b + 2), uint16(b + 2), uint16(b + 1), uint16(b + 3)};
	g_bi.insert(g_bi.end(), idx, idx + 6);
}

Spr *get(int h){ return h > 0 && h < (int)g_spr.size() && g_spr[h].used ? &g_spr[h] : nullptr; }

// Article_AdvanceAnimFrame @0x468090 (the random start / random restart flag is unused by the minigame sprites)
void advance(Spr &s)
{
	if(s.state == 2) return;
	const Def &d = *s.d;
	int n = (int)d.fr.size();
	if(s.idx < 0 || s.idx >= n) return;
	if((double)d.fr[s.idx].ms < s.clock - s.start){
		if(s.idx == n - 1){
			if(s.loops < d.loopCount - 1 || d.loopCount == 0){
				s.idx = d.loopStart; const Frame &f = d.fr[s.idx];
				s.suv[0] = f.u0; s.suv[1] = f.v0; s.suv[2] = f.u1; s.suv[3] = f.v1; s.sti = f.ti;
				s.state = 1; s.start = s.clock;
				if(s.loops != 0) s.loops++;
			} else s.state = 2;
		}else{
			s.idx++; const Frame &f = d.fr[s.idx];
			s.suv[0] = f.u0; s.suv[1] = f.v0; s.suv[2] = f.u1; s.suv[3] = f.v1; s.sti = f.ti;
			s.state = 1; s.start = s.clock;
		}
	}else s.state = 0;
}

}	// namespace

bool loadSprites(const std::string &path)
{
	std::ifstream f(path);
	if(!f) return false;
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; Def d; int nt = 0, nf = 0;
		ss >> kw;
		if(kw != "spr") continue;
		ss >> d.art >> d.uvl >> d.w >> d.h >> d.loopStart >> d.loopCount >> nt;
		for(int i = 0; i < nt; i++){ std::string t; ss >> t; std::replace(t.begin(), t.end(), '?', ' '); d.tex.push_back(t); }
		ss >> nf;
		for(int i = 0; i < nf; i++){ Frame fr; ss >> fr.u0 >> fr.v0 >> fr.u1 >> fr.v1 >> fr.ms >> fr.ti; d.fr.push_back(fr); }
		if(d.fr.empty() || d.tex.empty()) continue;
		g_defs[d.art] = d;
	}
	return !g_defs.empty();
}

int spNew(int art)
{
	auto it = g_defs.find(art);
	if(it == g_defs.end()){ rm8Log("minigame: unknown sprite article %d", art); return 0; }
	Spr s; s.used = true; s.d = &it->second; s.w = s.d->w; s.h = s.d->h;
	const Frame &f = s.d->fr[0];
	s.suv[0] = s.vuv[0] = f.u0; s.suv[1] = s.vuv[1] = f.v0; s.suv[2] = s.vuv[2] = f.u1; s.suv[3] = s.vuv[3] = f.v1;
	s.sti = s.vti = f.ti;
	s.px = s.w / 2; s.py = s.h / 2;	// created at (0,0): the centre is half the size
	for(size_t i = 1; i < g_spr.size(); i++) if(!g_spr[i].used){ g_spr[i] = s; return (int)i; }
	g_spr.push_back(s);
	return (int)g_spr.size() - 1;
}
void spFree(int h){ if(Spr *s = get(h)) s->used = false; }
void spPos(int h, V3 p){ if(Spr *s = get(h)){ s->px = p.x; s->py = p.y; } }
void spScale(int h, V3 v){ if(Spr *s = get(h)){ s->sx = v.x; s->sy = v.y; } }
void spRotZ(int h, float deg){ if(Spr *s = get(h)) s->rotZ = deg; }
void spColor(int h, uint32_t argb){ if(Spr *s = get(h)) s->color = argb; }
void spUpdate(int h, float dt)
{
	Spr *s = get(h);
	if(!s) return;
	s->clock += dt * 1000.0;	// [A] the game reads timeGetTime(); the port advances a per-sprite clock by the DLL's own dt
	advance(*s);
	if(s->state == 1){ memcpy(s->vuv, s->suv, sizeof s->vuv); s->vti = s->sti; }
}
void spSize(int h, float &w, float &hh){ Spr *s = get(h); w = s ? s->w : 0; hh = s ? s->h : 0; }
int spFrame(int h){ Spr *s = get(h); return s ? s->idx : 0; }
void spSetFrame(int h, int f){ if(Spr *s = get(h)) s->idx = f; }
int spLoops(int h){ Spr *s = get(h); return s ? s->loops : 0; }
void spSetLoops(int h, int n){ if(Spr *s = get(h)) s->loops = n; }
int spState(int h){ Spr *s = get(h); return s ? s->state : 0; }
void spSetState(int h, int st){ if(Spr *s = get(h)) s->state = st; }

bool spHit(int h, float x, float y)
{
	Spr *s = get(h);
	if(!s) return false;
	// inverse of translate(-c) scale rotZ translate(+c) translate(offset): point -> sprite space
	float dx = x - s->px, dy = y - s->py;
	float r = -s->rotZ * 3.14159265f / 180.0f, cs = cosf(r), sn = sinf(r);
	float rx = dx * cs - dy * sn, ry = dx * sn + dy * cs;
	if(s->sx == 0 || s->sy == 0) return false;
	float lx = rx / s->sx + s->w / 2, ly = ry / s->sy + s->h / 2;
	int ix = (int)lx, iy = (int)ly;
	if(ix < 0 || iy < 0 || ix > (int)s->w || iy > (int)s->h) return false;	// Rect_ContainsPoint is inclusive
	TexEnt *t = getTex("prt\\" + s->d->tex[std::min((int)s->d->tex.size() - 1, s->sti)]);
	if(!t) return true;
	// [H] Raster_PixelAlphaAbove(raster, x, y, 0x32): the texel under the point (uv rectangle of the current frame)
	int tx = (int)((s->suv[0] + lx / s->w * (s->suv[2] - s->suv[0])) * t->w);
	int ty = (int)((s->suv[1] + ly / s->h * (s->suv[3] - s->suv[1])) * t->h);
	if(tx < 0 || ty < 0 || tx >= t->w || ty >= t->h) return false;
	return t->alpha[(size_t)ty * t->w + tx] > 0x32;
}

void spDraw(int h)
{
	Spr *s = get(h);
	if(!s || !g_inFrame) return;
	if(s->state == 2) return;	// [V] Sprite_Render @0x462580 draws only for state 0 / 1
	TexEnt *t = getTex("prt\\" + s->d->tex[std::min((int)s->d->tex.size() - 1, s->vti)]);
	if(!t) return;
	setTex(t->tex->raster);
	float hw = s->w * s->sx / 2, hh = s->h * s->sy / 2;
	float r = s->rotZ * 3.14159265f / 180.0f, cs = cosf(r), sn = sinf(r);
	float cx[4] = {-hw, hw, -hw, hw}, cy[4] = {-hh, -hh, hh, hh}, px[4], py[4];
	for(int i = 0; i < 4; i++){ px[i] = s->px + cx[i] * cs - cy[i] * sn; py[i] = s->py + cx[i] * sn + cy[i] * cs; }
	// [A] sub-rectangle edges are inset by half a texel: with the 1.28 scale the bilinear filter would otherwise bleed the neighbouring texels of the
	// atlas into a seam between tiles (D3D9 sampled at pixel centres)
	float u0 = s->vuv[0], v0 = s->vuv[1], u1 = s->vuv[2], v1 = s->vuv[3];
	if(u0 > 0) u0 += 0.5f / t->w; if(v0 > 0) v0 += 0.5f / t->h;
	if(u1 < 1) u1 -= 0.5f / t->w; if(v1 < 1) v1 -= 0.5f / t->h;
	quad4(px, py, u0, v0, u1, v1, s->color);
}

void alphaTest(int thr)
{
	flush();
	if(thr >= 1 && thr <= 255){ SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, thr); }
	else SetRenderState(ALPHATESTFUNC, ALPHAALWAYS);
}

void drawTexQuad(const std::string &key, float x, float y, float w, float hgt, uint32_t argb)
{
	if(!g_inFrame) return;
	TexEnt *t = getTex(key);
	if(!t) return;
	setTex(t->tex->raster);
	float px[4] = {x, x + w, x, x + w}, py[4] = {y, y, y + hgt, y + hgt};
	quad4(px, py, 0, 0, 1, 1, argb);
}

void drawRect(float x, float y, float w, float hgt, uint32_t argb)
{
	if(!g_inFrame) return;
	setTex(nullptr);
	float px[4] = {x, x + w, x, x + w}, py[4] = {y, y, y + hgt, y + hgt};
	quad4(px, py, 0, 0, 1, 1, argb);
}

// ---- frame handling (used by minigame.cpp)
void gfxSetScreen(int sw, int sh)
{
	g_sw = sw; g_sh = sh;
	g_sc = std::min(sw / (float)W, sh / (float)H);
	g_ox = (sw - W * g_sc) / 2; g_oy = (sh - H * g_sc) / 2;
}
void gfxRestore2D()
{
	SetRenderState(ZWRITEENABLE, 0); SetRenderState(ZTESTENABLE, 0); SetRenderState(ALPHATESTFUNC, ALPHAALWAYS); SetRenderState(CULLMODE, CULLNONE);
	SetRenderState(VERTEXALPHA, 1); SetRenderState(SRCBLEND, BLENDSRCALPHA); SetRenderState(DESTBLEND, BLENDINVSRCALPHA);
	SetRenderState(TEXTUREFILTER, Texture::LINEAR); SetRenderState(TEXTUREADDRESS, Texture::CLAMP);
}
void gfxBegin(Camera *cam, int sw, int sh)
{
	gfxSetScreen(sw, sh);
	g_z = im2d::GetNearZ(); g_rz = 1.0f / cam->nearPlane;
	g_savedZt = GetRenderState(ZTESTENABLE); g_savedZw = GetRenderState(ZWRITEENABLE); g_savedAtf = GetRenderState(ALPHATESTFUNC);
	g_savedAtr = GetRenderState(ALPHATESTREF); g_savedCull = GetRenderState(CULLMODE);
	gfxRestore2D();
	g_bOpen = false; g_inFrame = true;
}
void gfxEnd()
{
	flush(); g_bOpen = false; g_inFrame = false;
	SetRenderState(ZTESTENABLE, g_savedZt); SetRenderState(ZWRITEENABLE, g_savedZw); SetRenderState(ALPHATESTFUNC, g_savedAtf);
	SetRenderState(ALPHATESTREF, g_savedAtr); SetRenderState(CULLMODE, g_savedCull);
}
void gfxFlush(){ flush(); }
void gfxMap(float &ox, float &oy, float &sc){ ox = g_ox; oy = g_oy; sc = g_sc; }
void gfxTri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t argb)
{
	if(!g_inFrame) return;
	setTex(nullptr);
	if(g_bv.size() + 3 > 65000) flush();
	uint16 b = (uint16)g_bv.size();
	float px[3] = {x0, x1, x2}, py[3] = {y0, y1, y2};
	for(int q = 0; q < 3; q++){
		gl3::Im2DVertex v;
		v.setScreenX(g_ox + px[q] * g_sc); v.setScreenY(g_oy + py[q] * g_sc); v.setScreenZ(g_z);
		v.setCameraZ(1.0f / g_rz); v.setRecipCameraZ(g_rz);
		v.setColor((argb >> 16) & 255, (argb >> 8) & 255, argb & 255, argb >> 24); v.setU(0, g_rz); v.setV(0, g_rz);
		g_bv.push_back(v);
	}
	uint16 idx[3] = {b, uint16(b + 1), uint16(b + 2)};
	g_bi.insert(g_bi.end(), idx, idx + 3);
}
void freeAllSprites(){ for(auto &s : g_spr) s.used = false; freeAllModels(); }

// ---- sound / music / strings
int snd(int id, float vol, int loops){ return rm8SndPlay(id, loops, vol > 1.0f ? 1.0f : vol); }
void sndStop(int handle){ rm8SndStop(handle); }
void music(int track){ rm8MusicPlay(track); }
const std::string &str(int id){ return rm8Str(id); }

}	// namespace mg
