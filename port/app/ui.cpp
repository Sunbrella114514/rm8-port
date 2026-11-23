// UI library, see ui.h. Facts marked [V] are from docs/ui_system.md (verified there); [A] are port choices.
#include "ui.h"
#include "zh.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <strings.h>
#include <chrono>
#define STB_TRUETYPE_IMPLEMENTATION
#include "../extern/stb/stb_truetype.h"

using namespace rw;

namespace {

struct UvRec { float w = 0, h = 0; std::vector<std::string> tex; struct F { float u0, v0, u1, v1; int ms, ti; }; std::vector<F> fr; };
std::map<int, UvRec> g_uvl;
std::map<int, std::string> g_str;
std::string g_dir;
int g_W = 1280, g_H = 720;

struct Widget {
	std::string name, type, parentName;
	int id = 0, order = 0;
	float x = 0, y = 0, w = 0, h = 0;	// relative to the parent, 1024x768 units
	int pic[4] = {-1, -1, -1, -1};		// Normal MouseOn MouseDown Disable
	std::string picFile;			// SetPicName (texture key)
	Texture *picTex = nullptr;		// a live texture (events.cpp: the 3D presenter rendered offscreen), drawn as is
	bool texExact = false;			// picTex covers the whole widget rect (no aspect margins): a screen-sized 3D layer (balance.cpp)
	int border = 0, picType = 0, index = 0;
	uint32 bcolor = 0, tint = 0xffffffff;	// tint: [A] picture colour / alpha (menus)
	bool enabled = true, visible = true, ignore = false, checkPic = false;
	int inSound = -1, downSound = -1;	// [V] InSound / DownSound sample ids (-1 none), played by buttons
	std::string text; int fontSize = 16; int alignH = 0, alignV = 0; bool multiLine = false;
	uint32 fcolor = 0xff000000, dcolor = 0;	// dcolor: DisableCapFontFColor (used while the widget is disabled, 0 = none) [V] key of the .ui files
	std::vector<Widget*> kids;	// draw order (index ascending, earlier sections on top)
	Widget *parent = nullptr;
	float ax = 0, ay = 0, aw = 0, ah = 0;	// screen rectangle (pixels), computed by layout
	int slice = 0; float sliceW = 0;	// [A] stretched HUD bar: 1 = keep the left end, 2 = the right end (sliceW px unscaled)
	float rot = 0;				// [V] Sprite_SetZRotation @0x462490: picture turned about its centre (degrees, menu_anim.cpp)
	float uv[4] = {0, 0, 1, 1};		// [A] picFile: the part of the texture drawn (u0 v0 u1 v1; the sprite sheet cells of npc_ui.cpp's reel dialog)
	bool dyn = false;			// [N] a runtime widget (rm8UiAddWidget): netplay names it by its rank among the UI's runtime widgets of its type (the IDs differ between devices)
};

}	// namespace

struct Rm8Ui {
	std::string name;
	Rm8UiMode mode = RM8UI_DIALOG;
	std::vector<Widget*> all;
	Widget *root = nullptr;
	bool shown = false;
	std::function<void(int)> onClick;
	std::function<void(const std::string &)> onClickName;	// events.cpp: dialogs whose widgets all have ID 0
	int order = 0;	// show order (later on top)
	bool late = false;	// [N] drawn by rm8UiRenderLate (after the menu's 3D dolls), not by rm8UiRender
};

namespace {

std::map<std::string, Rm8Ui*> g_uis;
int g_showCounter = 0;
Widget *g_pressed = nullptr, *g_hover = nullptr;
Rm8Ui *g_pressedUi = nullptr;
Rm8Ui *g_hoverUi = nullptr;			// [N] the UI of g_hover (the netplay hover command)
Rm8Ui *g_netHotUi = nullptr; Widget *g_netHot = nullptr;	// [N] the replicated hover of a netplay game (rm8UiNetApplyHover)
// [N] netplay (net.cpp, docs/netplay.md): this UI's hover comes from the replicated hover command; unset hooks (single player) change nothing
bool netHoverOf(Rm8Ui *u){ return u && rm8UiNetHooks().netHover && rm8UiNetHooks().netHover(u); }
}
static int rm8UiNetKey(Rm8Ui *u, Widget *w);
namespace {

// ---- font (stb_truetype glyph atlas)
std::vector<unsigned char> g_fontData;
stbtt_fontinfo g_font;
bool g_fontOk = false;
const int ATLAS = 1024;
Raster *g_atlas = nullptr; Texture *g_atlasTex = nullptr;
std::vector<uint8> g_atlasPix;	// RGBA
int g_penX = 1, g_penY = 1, g_rowH = 0; bool g_atlasDirty = false;
struct Glyph { float u0, v0, u1, v1; int w, h, xoff, yoff; float adv; };
std::map<std::pair<int,int>, Glyph> g_glyphs;

const Glyph *glyph(int cp, int px)
{
	auto key = std::make_pair(cp, px);
	auto it = g_glyphs.find(key);
	if(it != g_glyphs.end()) return &it->second;
	if(!g_fontOk) return nullptr;
	float sc = stbtt_ScaleForPixelHeight(&g_font, (float)px);
	int gi = stbtt_FindGlyphIndex(&g_font, cp);
	int adv, lsb; stbtt_GetGlyphHMetrics(&g_font, gi, &adv, &lsb);
	int x0, y0, x1, y1; stbtt_GetGlyphBitmapBox(&g_font, gi, sc, sc, &x0, &y0, &x1, &y1);
	int w = x1 - x0, h = y1 - y0;
	if(g_penX + w + 1 >= ATLAS){ g_penX = 1; g_penY += g_rowH + 1; g_rowH = 0; }
	if(g_penY + h + 1 >= ATLAS){	// [A] atlas full: start over (rare: a few hundred glyphs per size)
		g_glyphs.clear(); std::fill(g_atlasPix.begin(), g_atlasPix.end(), 0); g_penX = g_penY = 1; g_rowH = 0;
	}
	std::vector<unsigned char> bm(std::max(1, w * h));
	if(w > 0 && h > 0) stbtt_MakeGlyphBitmap(&g_font, bm.data(), w, h, w, sc, sc, gi);
	for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){
		uint8 *d = &g_atlasPix[((g_penY + y) * ATLAS + g_penX + x) * 4];
		d[0] = d[1] = d[2] = 255; d[3] = bm[y * w + x];
	}
	Glyph g; g.w = w; g.h = h; g.xoff = x0; g.yoff = y0; g.adv = adv * sc;
	g.u0 = g_penX / (float)ATLAS; g.v0 = g_penY / (float)ATLAS; g.u1 = (g_penX + w) / (float)ATLAS; g.v1 = (g_penY + h) / (float)ATLAS;
	g_penX += w + 1; g_rowH = std::max(g_rowH, h);
	g_atlasDirty = true;
	return &(g_glyphs[key] = g);
}

void uploadAtlas()
{
	if(!g_atlasDirty) return;
	g_atlasDirty = false;
	if(!g_atlas){
		g_atlas = Raster::create(ATLAS, ATLAS, 32, Raster::TEXTURE | Raster::C8888);
		g_atlasTex = Texture::create(g_atlas);
		g_atlasTex->setFilter(Texture::LINEAR);
	}
	uint8 *p = g_atlas->lock(0, Raster::LOCKWRITE | Raster::LOCKNOFETCH);
	// [A] librw's GL3 rasters hold rows bottom-up (rasterFromImage writes the image's last row first; v = 0 samples
	// raster row ATLAS-1), so the top-down atlas is written flipped
	if(p){
		for(int y = 0; y < ATLAS; y++) memcpy(p + (size_t)(ATLAS - 1 - y) * ATLAS * 4, &g_atlasPix[(size_t)y * ATLAS * 4], ATLAS * 4);
		g_atlas->unlock(0);
	}
}

int utf8Next(const char *&s)
{
	unsigned c = (unsigned char)*s++;
	if(c < 0x80) return c;
	int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
	unsigned cp = c & (0x3F >> n);
	while(n-- && (*s & 0xC0) == 0x80) cp = (cp << 6) | (*s++ & 0x3F);
	return (int)cp;
}

// ---- data
std::map<std::string, Texture*> g_tex;
Texture *packTex(const std::string &key)
{
	auto it = g_tex.find(key);
	if(it != g_tex.end()) return it->second;
	Texture *t = rm8PackTexture(key);
	if(!t) rm8Log("ui: texture %s missing", key.c_str());
	return g_tex[key] = t;
}

uint32 parseColor(const std::string &v)	// [V] 0x5A6DE0: "0x.." hexadecimal (ARGB), else atof
{
	if(v.size() > 1 && v[0] == '0' && v[1] == 'x'){ uint32 c = 0; for(size_t i = 2; i < v.size(); i++){ int d = isxdigit((unsigned char)v[i]) ? (isdigit((unsigned char)v[i]) ? v[i] - '0' : (tolower(v[i]) - 'a' + 10)) : -1; if(d >= 0) c = c * 16 + d; } return c; }
	return (uint32)atof(v.c_str());
}
int keyword(const std::string &v)	// [V] 0x5A6EE0 keyword table
{
	std::string l = v; std::transform(l.begin(), l.end(), l.begin(), ::tolower);
	if(l == "top") return 3; if(l == "true") return 1; if(l == "false") return 0; if(l == "left") return 0; if(l == "mid") return 1;
	if(l == "right") return 2; if(l == "rect") return 0; if(l == "bottom") return 4; if(l == "pic") return 1;
	return atoi(v.c_str());
}

const UvRec *uv(int id){ auto it = g_uvl.find(id); return it == g_uvl.end() ? nullptr : &it->second; }

Rm8Ui *loadUi(const std::string &name, Rm8UiMode mode)
{
	std::ifstream f(g_dir + "/" + name + ".ui");
	if(!f){ rm8Log("ui: no %s.ui", name.c_str()); return nullptr; }
	// [V] 3.2 line grammar: [section], key=value (first '='), case-insensitive keys, later keys win, same sections merge
	std::vector<std::pair<std::string, std::map<std::string, std::string>>> secs;
	std::map<std::string, int> secIdx;
	std::string line; int cur = -1;
	auto low = [](std::string s){ std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
	while(std::getline(f, line)){
		line.erase(std::remove(line.begin(), line.end(), '\t'), line.end());
		if(line.empty() || line[0] == ';') continue;
		if(line[0] == '[' && line.back() == ']'){
			std::string n = line.substr(1, line.size() - 2);
			auto it = secIdx.find(low(n));
			if(it != secIdx.end()) cur = it->second; else { cur = (int)secs.size(); secIdx[low(n)] = cur; secs.push_back({n, {}}); }
			continue;
		}
		size_t e = line.find('=');
		if(e == std::string::npos || e == 0 || cur < 0) continue;
		secs[cur].second[low(line.substr(0, e))] = line.substr(e + 1);
	}
	Rm8Ui *u = new Rm8Ui; u->name = name; u->mode = mode;
	std::string head;
	for(auto &s : secs) if(low(s.first) == "head") head = s.second["head"];
	std::map<std::string, Widget*> byName;
	for(size_t i = 0; i < secs.size(); i++){
		auto &kv = secs[i].second;
		if(low(secs[i].first) == "head") continue;
		Widget *w = new Widget; w->name = secs[i].first; w->order = (int)i;
		auto g = [&](const char *k, const char *def = "") -> std::string { auto it = kv.find(k); return it == kv.end() ? def : it->second; };
		w->type = low(g("type", "plane")); w->parentName = g("parent", "null");
		w->id = atoi(g("id", "0").c_str());
		w->x = (float)atol(g("left", "0").c_str()); w->y = (float)atol(g("top", "0").c_str());
		w->w = (float)atol(g("width", "0").c_str()); w->h = (float)atol(g("height", "0").c_str());
		const char *pk[4] = {"normalpic", "mouseonpic", "mousedownpic", "disablepic"};
		for(int k = 0; k < 4; k++) w->pic[k] = atoi(g(pk[k], "-1").c_str());
		w->border = atoi(g("bordertype", "0").c_str()); w->picType = atoi(g("pictype", "0").c_str());
		w->index = std::max(0, atoi(g("index", "0").c_str()));
		w->bcolor = parseColor(g("bcolor", "0"));
		w->enabled = keyword(g("enabled", "true")) > 0; w->visible = keyword(g("visible", "true")) > 0;
		w->ignore = keyword(g("ignore", "false")) > 0; w->checkPic = keyword(g("checktype", "rect")) == 1;
		w->text = g("captext"); w->fontSize = atoi(g("capfontsize", "16").c_str());
		w->alignH = keyword(g("alignh", "left")); w->alignV = keyword(g("alignv", "top")); w->multiLine = keyword(g("multiline", "false")) > 0;
		std::string fc = g("capfontfcolor"); if(fc.empty()) fc = g("normalcapfontfcolor"); if(!fc.empty()) w->fcolor = parseColor(fc);
		std::string dc = g("disablecapfontfcolor"); if(!dc.empty()) w->dcolor = parseColor(dc);
		w->inSound = atoi(g("insound", "-1").c_str()); w->downSound = atoi(g("downsound", "-1").c_str());
		if(w->picType == 1){ const UvRec *r = uv(w->pic[0]); if(r){ w->w = r->w; w->h = r->h; } }	// [V] PicType 1: picture size
		u->all.push_back(w); byName[low(w->name)] = w;
	}
	for(Widget *w : u->all){
		auto it = byName.find(low(w->parentName));
		if(it != byName.end() && it->second != w){ w->parent = it->second; it->second->kids.push_back(w); }
	}
	auto rt = byName.find(low(head));
	u->root = rt == byName.end() ? (u->all.empty() ? nullptr : u->all[0]) : rt->second;
	// [V] draw order: Index ascending; for equal Index the section earlier in the file is drawn later (on top)
	for(Widget *w : u->all) std::sort(w->kids.begin(), w->kids.end(), [](Widget *a, Widget *b){ return a->index != b->index ? a->index < b->index : a->order > b->order; });
	return u;
}

// ---- layout: 1024x768 -> screen
void layoutTree(Widget *w, float px, float py, float s)
{
	w->ax = px + w->x * s; w->ay = py + w->y * s; w->aw = w->w * s; w->ah = w->h * s;
	for(Widget *k : w->kids) layoutTree(k, w->ax, w->ay, s);
}
void layout(Rm8Ui *u)
{
	if(!u->root) return;
	float s = g_H / 768.0f;	// [A] uniform scale by height (the game scales by width / 1024 on 4:3 screens)
	if(u->mode == RM8UI_DIALOG){	// centred like UI_ScaleDialogPosition, scaled to the screen height
		layoutTree(u->root, (g_W - 1024.0f * s) / 2, 0, s);
		return;
	}
	if(u->mode == RM8UI_FLOAT){	// [A] root position follows the screen width, sizes the height
		layoutTree(u->root, u->root->x * (g_W / 1024.0f - s), 0, s);
		return;
	}
	// HUD (UI_ScaleWidgetTree on the game's 4:3 screens). [A] wide screens, as the original with RichMan8Fix looks
	// (1920x1080 capture): positions of the root's children scale with width/1024 and height/768, sizes with the
	// height; the half-screen bottom bar pieces (width >= 500) stretch with the width
	Widget *r = u->root;
	float sx = g_W / 1024.0f;
	r->ax = 0; r->ay = 0; r->aw = (float)g_W; r->ah = (float)g_H;
	for(Widget *k : r->kids){
		layoutTree(k, k->x * sx - k->x * s, 0, s);
		if(k->w >= 500.0f){
			k->aw = k->w * sx; for(Widget *c : k->kids) layoutTree(c, k->ax, k->ay, s);
			// [A] no distortion: only the plain middle of the bar stretches, its outer end keeps the uniform scale
			k->slice = k->x <= 0.0f ? 1 : (k->x + k->w >= 1024.0f ? 2 : 0); k->sliceW = 48.0f * s;
		}
	}
}

// ---- drawing
float g_z, g_rz;
// [A] quads are batched per texture / filter (one Im2D draw per run) - each Im2D call costs ~50 us on the phone
std::vector<gl3::Im2DVertex> g_bv; std::vector<uint16> g_bi;
Raster *g_bRaster = nullptr; int g_bFilter = Texture::LINEAR; bool g_bOpen = false;
void flush()
{
	if(!g_bi.empty()){
		SetRenderStatePtr(TEXTURERASTER, g_bRaster);
		SetRenderState(TEXTUREFILTER, g_bFilter);
		im2d::RenderIndexedPrimitive(PRIMTYPETRILIST, g_bv.data(), (int32)g_bv.size(), g_bi.data(), (int32)g_bi.size());
	}
	g_bv.clear(); g_bi.clear();
}
void setTex(Raster *r, int filter = Texture::LINEAR)
{
	if(g_bOpen && r == g_bRaster && filter == g_bFilter) return;
	flush(); g_bRaster = r; g_bFilter = filter; g_bOpen = true;
}
void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32 argb)
{
	if(g_bv.size() + 4 > 65000) flush();
	uint16 b = (uint16)g_bv.size();
	float px[4] = {x0, x1, x0, x1}, py[4] = {y0, y0, y1, y1}, pu[4] = {u0, u1, u0, u1}, pv[4] = {v0, v0, v1, v1};
	for(int q = 0; q < 4; q++){
		gl3::Im2DVertex v;
		v.setScreenX(px[q]); v.setScreenY(py[q]); v.setScreenZ(g_z);
		v.setCameraZ(1.0f / g_rz); v.setRecipCameraZ(g_rz);
		v.setColor((argb >> 16) & 255, (argb >> 8) & 255, argb & 255, argb >> 24); v.setU(pu[q], g_rz); v.setV(pv[q], g_rz);
		g_bv.push_back(v);
	}
	uint16 idx[6] = {b, uint16(b + 1), uint16(b + 2), uint16(b + 2), uint16(b + 1), uint16(b + 3)};
	g_bi.insert(g_bi.end(), idx, idx + 6);
}

// [A] the same quad turned by deg degrees about its centre (menu_anim.cpp: the rotating rings, Sprite_SetZRotation @0x462490)
void quadRot(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32 argb, float deg)
{
	if(g_bv.size() + 4 > 65000) flush();
	uint16 b = (uint16)g_bv.size();
	float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, c = cosf(deg * 3.14159265f / 180.0f), s = sinf(deg * 3.14159265f / 180.0f);
	float dx[4] = {x0 - cx, x1 - cx, x0 - cx, x1 - cx}, dy[4] = {y0 - cy, y0 - cy, y1 - cy, y1 - cy}, pu[4] = {u0, u1, u0, u1}, pv[4] = {v0, v0, v1, v1};
	for(int q = 0; q < 4; q++){
		gl3::Im2DVertex v;
		v.setScreenX(cx + dx[q] * c - dy[q] * s); v.setScreenY(cy + dx[q] * s + dy[q] * c); v.setScreenZ(g_z);
		v.setCameraZ(1.0f / g_rz); v.setRecipCameraZ(g_rz);
		v.setColor((argb >> 16) & 255, (argb >> 8) & 255, argb & 255, argb >> 24); v.setU(pu[q], g_rz); v.setV(pv[q], g_rz);
		g_bv.push_back(v);
	}
	uint16 idx[6] = {b, uint16(b + 1), uint16(b + 2), uint16(b + 2), uint16(b + 1), uint16(b + 3)};
	g_bi.insert(g_bi.end(), idx, idx + 6);
}

void drawPic(int id, float x, float y, float w, float h, uint32 col = 0xffffffff, int slice = 0, float sliceW = 0, float rot = 0)
{
	const UvRec *r = uv(id);
	if(!r || r->fr.empty() || r->tex.empty()) return;
	// [V] uvlist frames carry a duration in ms; multi-frame pictures loop (e.g. 512, the rolling dice of go.ui)
	size_t fi = 0;
	if(r->fr.size() > 1){
		int total = 0; for(auto &q : r->fr) total += std::max(1, q.ms);
		long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		int t = (int)(now % total);
		while(fi + 1 < r->fr.size() && t >= std::max(1, r->fr[fi].ms)){ t -= std::max(1, r->fr[fi].ms); fi++; }
	}
	const UvRec::F &f = r->fr[fi];
	Texture *t = packTex("prt\\" + r->tex[std::min((int)r->tex.size() - 1, std::max(0, f.ti))]);
	if(!t) return;
	setTex(t->raster);
	if(slice && sliceW > 0 && sliceW < w && r->w > 0){
		float ue = (f.u1 - f.u0) * (sliceW / w) * (w / (r->w * (h / std::max(1.0f, r->h))));	// texture width of the kept end
		ue = std::min(ue, (f.u1 - f.u0) * 0.5f);
		if(slice == 1){ quad(x, y, x + sliceW, y + h, f.u0, f.v0, f.u0 + ue, f.v1, col); quad(x + sliceW, y, x + w, y + h, f.u0 + ue, f.v0, f.u1, f.v1, col); }
		else { quad(x, y, x + w - sliceW, y + h, f.u0, f.v0, f.u1 - ue, f.v1, col); quad(x + w - sliceW, y, x + w, y + h, f.u1 - ue, f.v0, f.u1, f.v1, col); }
		return;
	}
	if(rot != 0){ quadRot(x, y, x + w, y + h, f.u0, f.v0, f.u1, f.v1, col, rot); return; }
	quad(x, y, x + w, y + h, f.u0, f.v0, f.u1, f.v1, col);
}

// text: line break is a backslash, '^' is a space ([A]), "~[...]" markup skipped
void drawText(const std::string &txtIn, float x, float y, float w, float h, float px, int alignH, int alignV, uint32 col)
{
	const std::string &txt = rm8ZhConvert(txtIn);	// [A] optional Traditional -> Simplified (zh.cpp)
	if(txt.empty() || !g_fontOk) return;
	std::vector<std::vector<int>> lines(1);
	for(const char *s = txt.c_str(); *s;){
		if(s[0] == '~' && s[1] == '['){ const char *e = strchr(s, ']'); if(e){ s = e + 1; continue; } }
		int cp = utf8Next(s);
		if(cp == '\\' || cp == '\n'){ lines.emplace_back(); continue; }
		if(cp == '^') cp = 0x3000;
		lines.back().push_back(cp);
	}
	int ipx = std::max(6, (int)lroundf(px));
	float sc = stbtt_ScaleForPixelHeight(&g_font, (float)ipx);
	int asc, desc, gap; stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
	float lineH = (asc - desc + gap) * sc, total = lineH * lines.size();
	float ty = alignV == 1 ? y + (h - total) / 2 : alignV == 4 ? y + h - total : y;
	for(auto &L : lines){
		float lw = 0; for(int cp : L){ const Glyph *g = glyph(cp, ipx); if(g) lw += g->adv; }
		float tx = alignH == 1 ? x + (w - lw) / 2 : alignH == 2 ? x + w - lw : x;
		float base = ty + asc * sc;
		for(int cp : L){
			const Glyph *g = glyph(cp, ipx);
			if(!g) continue;
			if(g->w > 0){ if(g_atlasDirty){ flush(); g_bOpen = false; uploadAtlas(); } setTex(g_atlas); quad(tx + g->xoff, base + g->yoff, tx + g->xoff + g->w, base + g->yoff + g->h, g->u0, g->v0, g->u1, g->v1, col); }
			tx += g->adv;
		}
		ty += lineH;
	}
}

// [V] SpriteSheet_Load @0x417690 + Render_DrawSpriteSequence @0x417330: a texture split into cols x rows cells of
// (cw, ch) pixels (rects truncated), one character code per cell; each character drawn at its cell size x scale,
// advancing by cell width x scale + spacing; POINT sampling
struct Sheet { Texture *t = nullptr; int cols, rows; float cw, ch; std::string codes; };
std::map<std::string, Sheet> g_sheets;
void sheetText(const std::string &key, int cols, int rows, float cw, float ch, const char *codes, const char *txt, float x, float y, float scale, float spacing, uint32 col)
{
	Sheet &sh = g_sheets[key];
	if(!sh.t){ sh.t = packTex(key); sh.cols = cols; sh.rows = rows; sh.cw = cw; sh.ch = ch; sh.codes = codes; }
	if(!sh.t) return;
	float tw = (float)sh.t->raster->width, th = (float)sh.t->raster->height;
	setTex(sh.t->raster, Texture::NEAREST);
	for(const char *c = txt; *c; c++){
		size_t i = sh.codes.find(*c);
		if(i == std::string::npos || i >= (size_t)(cols * rows)){ x += (int)sh.cw * scale + spacing; continue; }
		int cx = (int)((i % cols) * sh.cw), cy = (int)((i / cols) * sh.ch), w = (int)sh.cw, h = (int)sh.ch;
		quad(x, y, x + w * scale, y + h * scale, cx / tw, cy / th, (cx + w) / tw, (cy + h) / th, col);
		x += w * scale + spacing;
	}
}

// [V] CText (SetContent @0x40de60, Update @0x40dd10, layout @0x40dfb0, draw @0x40dd90): the global mouse tooltip. The text shows
// 0.5 s after it was set, in a box at the mouse (x + 5, bottom edge at y - 2, moved left of the mouse near the right screen edge),
// fill 0xffd7b6ff with a black outline, black 16 px text; cleared by a mouse press. [A] widths from the port font, sizes x H / 768
std::string g_tip; bool g_tipShow = false; std::chrono::steady_clock::time_point g_tipT;
float g_mx = 0, g_my = 0;
bool g_touch = false;	// the current press is a finger (rm8UiPointerDown)
void drawTooltip()
{
	if(g_tip.empty() || !g_fontOk) return;
	if(!g_tipShow){
		static const float delay = getenv("RM8_TIP_DELAY") ? (float)atof(getenv("RM8_TIP_DELAY")) : 0.5f;	// [A] test switch (screenshots render too few frames)
		if(std::chrono::duration<float>(std::chrono::steady_clock::now() - g_tipT).count() < delay) return;
		g_tipShow = true;
	}
	float s = g_H / 768.0f, px = 16 * s;
	int ipx = std::max(6, (int)lroundf(px));
	const std::string &txt = rm8ZhConvert(g_tip);
	float tw = 0, lw = 0; int lines = 1;
	for(const char *p = txt.c_str(); *p;){
		int cp = utf8Next(p);
		if(cp == '\\' || cp == '\n'){ tw = std::max(tw, lw); lw = 0; lines++; continue; }
		const Glyph *g = glyph(cp == '^' ? 0x3000 : cp, ipx);
		if(g) lw += g->adv;
	}
	tw = std::max(tw, lw);
	float th = 16 * s + 21 * s * (lines - 1);
	float x = g_mx + 5 * s, y = g_my - th - (g_touch ? 60 * s : 2);	// [A] above the finger on touch
	if(x + tw + 2 > g_W) x = g_mx - tw - 5 * s;
	x = std::max(2.0f, x); y = std::max(2.0f, y);
	setTex(nil);
	quad(x - 2, y - 2, x + tw + 2, y + th + 2, 0, 0, 1, 1, 0xffd7b6ff);
	quad(x - 2, y - 2, x + tw + 2, y - 1, 0, 0, 1, 1, 0xff000000); quad(x - 2, y + th + 1, x + tw + 2, y + th + 2, 0, 0, 1, 1, 0xff000000);
	quad(x - 2, y - 2, x - 1, y + th + 2, 0, 0, 1, 1, 0xff000000); quad(x + tw + 1, y - 2, x + tw + 2, y + th + 2, 0, 0, 1, 1, 0xff000000);
	drawText(g_tip, x, y, tw + 4, th, px, 0, 0, 0xff000000);
}

void drawWidget(Rm8Ui *u, Widget *w)
{
	if(!w->visible) return;
	float s = g_H / 768.0f;
	if(w->picTex && w->texExact){ setTex(w->picTex->raster); quad(w->ax, w->ay, w->ax + w->aw, w->ay + w->ah, 0, 0, 1, 1, 0xffffffff); }
	else if(w->picTex){	// [V] Im2D_DrawTexturedRect_4eae10 @0x4eae10 + Rect_CalcAspectUVMargins @0x45f260: the square raster is shown through margins ((m - side) / 2 / m * 0.9) of the longer side m
		float m = std::max(w->aw, w->ah), mu = m > 0 ? (m - w->aw) / 2 / m * 0.9f : 0, mv = m > 0 ? (m - w->ah) / 2 / m * 0.9f : 0;
		setTex(w->picTex->raster); quad(w->ax, w->ay, w->ax + w->aw, w->ay + w->ah, mu, mv, 1 - mu, 1 - mv, 0xffffffff);
	}
	else if(w->border == 5 || !w->picFile.empty()){	// [V] 0x5AA190: BorderType 5 = picture
		int st = !w->enabled ? 3 : (w == g_pressed && w == g_hover) ? 2 : (w == g_hover ? 1 : 0);
		int id = w->pic[st] >= 0 ? w->pic[st] : (st == 3 ? -1 : w->pic[0]);
		if(!w->picFile.empty()){ Texture *t = packTex(w->picFile); if(t){ setTex(t->raster); quad(w->ax, w->ay, w->ax + w->aw, w->ay + w->ah, w->uv[0], w->uv[1], w->uv[2], w->uv[3], w->tint); } }
		else if(id >= 0) drawPic(id, w->ax, w->ay, w->aw, w->ah, w->tint, w->slice, w->sliceW, w->rot);
	}else if(w->bcolor >> 24){
		setTex(nil);
		quad(w->ax, w->ay, w->ax + w->aw, w->ay + w->ah, 0, 0, 1, 1, w->bcolor);
	}
	if(w->border >= 1 && w->border <= 4 && w->picFile.empty() && !w->picTex){	// [V] UiLib_Draw_Border @0x5a9ce0 (after the BColor fill, 0x5AA190): two 1 px rings, BorderType 1 / 4 black + 0xff848284, 2 white + 0xffd6d3ce, 3 0xff848284 + 0xffd6d3ce
		static const unsigned c0[5] = {0, 0xff000000, 0xffffffff, 0xff848284, 0xff000000}, c1[5] = {0, 0xff848284, 0xffd6d3ce, 0xffd6d3ce, 0xff848284};
		float t = std::max(1.0f, s);
		setTex(nil);
		for(int i = 0; i < 2; i++){
			unsigned c = i ? c1[w->border] : c0[w->border];
			float x0 = w->ax + i * t, y0 = w->ay + i * t, x1 = w->ax + w->aw - i * t, y1 = w->ay + w->ah - i * t;
			quad(x0, y0, x1, y0 + t, 0, 0, 1, 1, c); quad(x0, y1 - t, x1, y1, 0, 0, 1, 1, c);
			quad(x0, y0 + t, x0 + t, y1 - t, 0, 0, 1, 1, c); quad(x1 - t, y0 + t, x1, y1 - t, 0, 0, 1, 1, c);
		}
	}
	if((w->type == "label" || w->type == "button" || w->type == "title" || w->type == "edit") && !w->text.empty())
		drawText(w->text, w->ax, w->ay, w->aw, w->ah, w->fontSize * s, w->alignH, w->alignV, (!w->enabled && w->dcolor ? w->dcolor : w->fcolor) | 0xff000000);
	for(Widget *k : w->kids) drawWidget(u, k);
}

Widget *hitTree(Widget *w, float x, float y)
{
	if(!w->visible) return nullptr;
	for(auto it = w->kids.rbegin(); it != w->kids.rend(); ++it) if(Widget *h = hitTree(*it, x, y)) return h;
	if(w->ignore || !(x >= w->ax && y >= w->ay && x < w->ax + w->aw && y < w->ay + w->ah)) return nullptr;
	return w;
}

Widget *hit(float x, float y, Rm8Ui **ui)
{
	std::vector<Rm8Ui*> vis;
	for(auto &kv : g_uis) if(kv.second->shown && kv.second->root) vis.push_back(kv.second);
	std::sort(vis.begin(), vis.end(), [](Rm8Ui *a, Rm8Ui *b){ return a->order > b->order; });
	for(Rm8Ui *u : vis){
		Widget *w = hitTree(u->root, x, y);
		if(w && !(u->mode == RM8UI_HUD && w == u->root)){ if(ui) *ui = u; return w; }	// the HUD root is see-through
	}
	return nullptr;
}

Widget *findId(Rm8Ui *u, int id){ if(!u) return nullptr; for(Widget *w : u->all) if(w->id == id && w != u->root) return w; return nullptr; }

}	// namespace

// [A] opens a font file (face `index` of a .ttc) into g_font; needCjk: the face must have the glyph of U+5927 (a font without CJK would draw nothing)
static bool loadFontFile(const std::string &file, int index, bool needCjk)
{
	FILE *ff = fopen(file.c_str(), "rb");
	if(!ff) return false;
	fseek(ff, 0, SEEK_END); long n = ftell(ff); fseek(ff, 0, SEEK_SET);
	std::vector<unsigned char> data(n > 0 ? (size_t)n : 0);
	bool rd = n > 0 && fread(data.data(), 1, data.size(), ff) == data.size();
	fclose(ff);
	if(!rd) return false;
	int off = stbtt_GetFontOffsetForIndex(data.data(), index);
	stbtt_fontinfo fi;
	if(off < 0 || !stbtt_InitFont(&fi, data.data(), off)) return false;
	if(needCjk && stbtt_FindGlyphIndex(&fi, 0x5927) == 0) return false;
	g_fontData.swap(data);
	g_font = fi; g_font.data = g_fontData.data();	// the info points into the font data: re-point it at the stored copy
	return true;
}

bool rm8UiPickFont(const std::vector<Rm8FontCandidate> &cands, std::string *usedFile, int *usedIndex)
{
	g_fontOk = false;
	for(const Rm8FontCandidate &c : cands){
		if(loadFontFile(c.file, c.index, true)){
			g_fontOk = true;
			if(g_atlasPix.empty()) g_atlasPix.assign(ATLAS * ATLAS * 4, 0);	// the notice screen draws before rm8UiInit
			if(usedFile) *usedFile = c.file;
			if(usedIndex) *usedIndex = c.index;
			rm8Log("ui: font %s (%d)", c.file.c_str(), c.index);
			return true;
		}
		rm8Log("ui: font candidate %s (%d): not usable", c.file.c_str(), c.index);
	}
	return false;
}

// [A] a full-screen notice (first-run problems, rm8_app.cpp): light text on the cleared background, title larger than the body; needs only the font
void rm8UiDrawNotice(rw::Camera *cam, int W, int H, const std::string &title, const std::string &body)
{
	if(!g_fontOk) return;
	g_W = W; g_H = H;
	float s = H / 720.0f, margin = 48 * s, tw = W - 2 * margin;
	int tpx = std::max(10, (int)lroundf(34 * s)), bpx = std::max(8, (int)lroundf(22 * s));
	std::string wt = rm8UiWrapText(title, tw, tpx), wb = rm8UiWrapText(body, tw, bpx);
	auto lines = [](const std::string &t){ int n = 1; for(char c : t) if(c == '\\' || c == '\n') n++; return n; };
	int asc, desc, gap; stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
	float tlh = (asc - desc + gap) * stbtt_ScaleForPixelHeight(&g_font, (float)tpx), blh = (asc - desc + gap) * stbtt_ScaleForPixelHeight(&g_font, (float)bpx);
	float th = tlh * lines(wt), bh = blh * lines(wb), total = th + 24 * s + bh;
	float y = std::max(margin, (H - total) / 2);
	rm8UiOverlayBegin(cam);
	drawText(wt, margin, y, tw, th, (float)tpx, 0, 0, 0xffffd24a);
	drawText(wb, margin, y + th + 24 * s, tw, bh, (float)bpx, 0, 0, 0xfff0f0f0);
	rm8UiOverlayEnd();
}

bool rm8UiInit(const std::string &dir, const std::string &fontFile, int fontIndex)
{
	g_dir = dir;
	std::ifstream uf(dir + "/uvlist.txt");
	std::string line;
	while(std::getline(uf, line)){
		std::istringstream ss(line); std::string kw; int id, nt, nf; UvRec r; ss >> kw >> id >> r.w >> r.h >> nt;
		for(int i = 0; i < nt; i++){ std::string t; ss >> t; std::replace(t.begin(), t.end(), '?', ' '); r.tex.push_back(t); }
		ss >> nf; for(int i = 0; i < nf; i++){ UvRec::F f; ss >> f.u0 >> f.v0 >> f.u1 >> f.v1 >> f.ms >> f.ti; r.fr.push_back(f); }
		if(r.tex.size() == 1 && r.tex[0] == "-") r.tex.clear();
		g_uvl[id] = r;
	}
	std::ifstream sf(dir + "/strings.txt");
	while(std::getline(sf, line)){ size_t t = line.find('\t'); if(t != std::string::npos) g_str[atoi(line.c_str())] = line.substr(t + 1); }
	if(!g_fontOk) g_fontOk = loadFontFile(fontFile, fontIndex, false);	// [A] rm8UiPickFont (rm8_app.cpp) may have chosen the font already
	if(!g_fontOk) rm8Log("ui: cannot use font %s (%d)", fontFile.c_str(), fontIndex);
	g_atlasPix.assign(ATLAS * ATLAS * 4, 0);
	rm8Log("ui: %d pictures, %d strings, font %s", (int)g_uvl.size(), (int)g_str.size(), g_fontOk ? fontFile.c_str() : "none");
	return !g_uvl.empty();
}

void rm8UiSetScreen(int w, int h){ g_W = w; g_H = h; for(auto &kv : g_uis) layout(kv.second); }

Rm8Ui *rm8UiLoad(const std::string &name, Rm8UiMode mode)
{
	auto it = g_uis.find(name);
	if(it != g_uis.end()) return it->second;
	Rm8Ui *u = loadUi(name, mode);
	if(u) layout(u);
	return g_uis[name] = u;
}

void rm8UiShow(Rm8Ui *u, bool show){ if(!u) return; if(show && !u->shown) u->order = ++g_showCounter; u->shown = show; if(!show && g_pressedUi == u){ g_pressed = nullptr; g_pressedUi = nullptr; } }
bool rm8UiVisible(Rm8Ui *u){ return u && u->shown; }
bool rm8UiSetText(Rm8Ui *u, int id, const std::string &t){ Widget *w = findId(u, id); if(w) w->text = t; return w != nullptr; }
bool rm8UiSetTextByName(Rm8Ui *u, const char *name, const std::string &t)
{
	if(!u) return false;
	for(Widget *w : u->all) if(!strcasecmp(w->name.c_str(), name)){ w->text = t; return true; }
	return false;
}
bool rm8UiSetPic(Rm8Ui *u, int id, int pic){ Widget *w = findId(u, id); if(!w) return false; for(int &p : w->pic) p = pic; w->border = 5; w->picFile.clear(); return true; }
bool rm8UiSetPicFile(Rm8Ui *u, int id, const std::string &key){ Widget *w = findId(u, id); if(!w) return false; w->picFile = key; return true; }
bool rm8UiSetPicFileUV(Rm8Ui *u, int id, const std::string &key, float u0, float v0, float u1, float v1){ Widget *w = findId(u, id); if(!w) return false; w->picFile = key; w->uv[0] = u0; w->uv[1] = v0; w->uv[2] = u1; w->uv[3] = v1; return true; }
bool rm8UiSetVisible(Rm8Ui *u, int id, bool v){ Widget *w = findId(u, id); if(w) w->visible = v; return w != nullptr; }
bool rm8UiSetEnabled(Rm8Ui *u, int id, bool v){ Widget *w = findId(u, id); if(w) w->enabled = v; return w != nullptr; }
void rm8UiOnClick(Rm8Ui *u, std::function<void(int)> fn){ if(u) u->onClick = fn; }
bool rm8UiHot(Rm8Ui *u, int id){ Widget *w = findId(u, id); if(netHoverOf(u)) return w && u->shown && u == g_netHotUi && w == g_netHot; return w && u->shown && (w == g_hover || w == g_pressed); }
// by widget name (the game's FindByName): bank.ui, cardShop.ui ... give every widget ID 0
static Widget *findName(Rm8Ui *u, const char *name){ if(u) for(Widget *w : u->all) if(!strcasecmp(w->name.c_str(), name)) return w; return nullptr; }
bool rm8UiSetVisibleN(Rm8Ui *u, const char *name, bool v){ Widget *w = findName(u, name); if(w) w->visible = v; return w != nullptr; }
bool rm8UiSetEnabledN(Rm8Ui *u, const char *name, bool v){ Widget *w = findName(u, name); if(w) w->enabled = v; return w != nullptr; }
bool rm8UiSetFontSizeN(Rm8Ui *u, const char *name, int px){ Widget *w = findName(u, name); if(w) w->fontSize = px; return w != nullptr; }
bool rm8UiSetPicN(Rm8Ui *u, const char *name, int pic){ Widget *w = findName(u, name); if(!w) return false; for(int &p : w->pic) p = pic; w->border = 5; w->picFile.clear(); return true; }
bool rm8UiSetTextureN(Rm8Ui *u, const char *name, Texture *t){ Widget *w = findName(u, name); if(w) w->picTex = t; return w != nullptr; }
bool rm8UiSetTextureExactN(Rm8Ui *u, const char *name, Texture *t){ Widget *w = findName(u, name); if(w){ w->picTex = t; w->texExact = true; } return w != nullptr; }
bool rm8UiSetPicFileN(Rm8Ui *u, const char *name, const std::string &key){ Widget *w = findName(u, name); if(w) w->picFile = key; return w != nullptr; }
bool rm8UiSetColorN(Rm8Ui *u, const char *name, unsigned argb){ Widget *w = findName(u, name); if(w) w->fcolor = argb; return w != nullptr; }
bool rm8UiHotN(Rm8Ui *u, const char *name){ Widget *w = findName(u, name); if(netHoverOf(u)) return w && u->shown && u == g_netHotUi && w == g_netHot; return w && u->shown && (w == g_hover || w == g_pressed); }
void rm8UiOnClickName(Rm8Ui *u, std::function<void(const std::string &)> fn){ if(u) u->onClickName = fn; }
bool rm8UiSetIgnoreN(Rm8Ui *u, const char *name, bool v){ Widget *w = findName(u, name); if(w) w->ignore = v; return w != nullptr; }
void rm8UiSetIgnoreAll(Rm8Ui *u, bool v){ if(u) for(Widget *w : u->all) if(w != u->root) w->ignore = v; }
bool rm8UiGetScreenRectN(Rm8Ui *u, const char *name, float &x, float &y, float &w, float &h){ Widget *k = findName(u, name); if(!k) return false; x = k->ax; y = k->ay; w = k->aw; h = k->ah; return true; }
std::string rm8UiHitName(float x, float y){ Rm8Ui *u = nullptr; Widget *w = hit(x, y, &u); return w ? u->name + "/" + w->name : std::string(); }
bool rm8UiSetRectN(Rm8Ui *u, const char *name, float x, float y, float w, float h){ Widget *k = findName(u, name); if(!k) return false; if(k->x == x && k->y == y && k->w == w && k->h == h) return true; k->x = x; k->y = y; k->w = w; k->h = h; layout(u); return true; }
bool rm8UiGetRectN(Rm8Ui *u, const char *name, float &x, float &y, float &w, float &h){ Widget *k = findName(u, name); if(!k) return false; x = k->x; y = k->y; w = k->w; h = k->h; return true; }
bool rm8UiSetAlignN(Rm8Ui *u, const char *name, int h, int v){ Widget *w = findName(u, name); if(w){ w->alignH = h; w->alignV = v; } return w != nullptr; }

bool rm8UiPressIsTouch(){ return g_touch && g_pressed != nullptr; }	// a finger is down on a widget (cards.cpp: hold a card for its menu)
void rm8UiPointerPos(float &x, float &y){ x = g_mx; y = g_my; }	// [A] infoscreens.cpp: slider drags / the minimap read the pointer
bool rm8UiPressed(Rm8Ui *u, int id){ Widget *w = findId(u, id); return w && u->shown && w == g_pressed; }
// [A] places the root of a dialog-mode UI at a screen pixel (UiLib dialog SetPos, e.g. the card menu of u_UI_SetXYPositionIfActive @0x4aea80)
bool rm8UiSetRootPos(Rm8Ui *u, float px, float py)
{
	if(!u || !u->root) return false;
	float s = g_H / 768.0f;
	if(u->mode == RM8UI_FLOAT){ u->root->x = px / (g_W / 1024.0f); u->root->y = py / s; layout(u); return true; }	// [A] float panels (go.ui): x follows the screen width, y the height
	u->root->x = (px - (g_W - 1024.0f * s) / 2) / s; u->root->y = py / s;
	layout(u);
	return true;
}
std::chrono::steady_clock::time_point g_pressT;
const float LONG_PRESS = 0.5f;	// [A] seconds; equals the tooltip delay so the description appears exactly when the tap turns into an inspection
bool rm8UiPointerDown(float x, float y, bool touch)
{
	g_mx = x; g_my = y; g_tip.clear();	// [V] a mouse press clears the tooltip (CText_Update)
	g_touch = touch; g_pressT = std::chrono::steady_clock::now();
	Rm8Ui *u = nullptr;
	Widget *w = hit(x, y, &u);
	g_pressed = w; g_pressedUi = u; g_hover = w; g_hoverUi = u;
	// [V] UiLib_Title mouse-down handler @0x5c31f0 (button, left button, enabled): DownSound (+0x1f4) through the hook @0x4EC680
	// = CAudioMgr_PlaySample(id,0,0,0,1000,50,1.0,1)
	if(w && w->type == "button" && w->enabled && w->downSound >= 0) rm8SndPlayLocal(w->downSound);	// [N] the pointer of this device: never part of the deterministic sample clock (netplay)
	return w != nullptr;
}

bool rm8UiPointerUp(float x, float y)
{
	g_mx = x; g_my = y;
	Rm8Ui *u = nullptr;
	Widget *w = hit(x, y, &u);
	bool consumed = w != nullptr || g_pressed != nullptr;
	if(g_touch && std::chrono::duration<float>(std::chrono::steady_clock::now() - g_pressT).count() >= LONG_PRESS){	// [A] long press = inspect, no click
		g_pressed = nullptr; g_pressedUi = nullptr; g_hover = nullptr; g_hoverUi = nullptr; g_touch = false; g_tip.clear();
		return consumed;
	}
	if(w && w == g_pressed && w->enabled && u && (u->onClick || u->onClickName) && rm8UiNetHooks().click && rm8UiNetHooks().click(u, rm8UiNetKey(u, w), w->id, w->name)){ g_pressed = nullptr; }	// [N] netplay: sent as a command (or not the local player's turn)
	else if(w && w == g_pressed && w->enabled && u && u->onClick){ Widget *p = w; std::function<void(int)> fn = u->onClick; g_pressed = nullptr; fn(p->id); }	// [V] click = press and release on the same widget
	else if(w && w == g_pressed && w->enabled && u && u->onClickName){ std::string nm = w->name; std::function<void(const std::string &)> fn = u->onClickName; g_pressed = nullptr; fn(nm); }
	g_pressed = nullptr; g_pressedUi = nullptr; g_hover = nullptr; g_hoverUi = nullptr;
	return consumed;
}

void rm8UiPointerMove(float x, float y)
{
	g_mx = x; g_my = y;
	Rm8Ui *hu = nullptr;
	Widget *w = hit(x, y, &hu);
	g_hoverUi = w ? hu : nullptr;
	// [V] UiLib_Title mouse-enter handler @0x5c3460 (enabled, visible): InSound (+0x1f0)
	if(w && w != g_hover && w->type == "button" && w->enabled && w->inSound >= 0) rm8SndPlayLocal(w->inSound);	// [N] (as above)
	g_hover = w;
}

static void renderUis(Camera *cam, bool late);
void rm8UiRender(Camera *cam){ renderUis(cam, false); }
void rm8UiRenderLate(Camera *cam){ renderUis(cam, true); }
void rm8UiSetLate(Rm8Ui *u, bool late){ if(u) u->late = late; }
static void renderUis(Camera *cam, bool late)
{
	std::vector<Rm8Ui*> vis;
	for(auto &kv : g_uis) if(kv.second && kv.second->shown && kv.second->root && kv.second->late == late) vis.push_back(kv.second);
	if(vis.empty()) return;
	std::sort(vis.begin(), vis.end(), [](Rm8Ui *a, Rm8Ui *b){ return a->order < b->order; });
	if(getenv("RM8_DEBUG_UIVIS")){ std::string l = "ui: visible"; for(Rm8Ui *u : vis) l += " " + u->name; rm8Log("%s", l.c_str()); }
	g_z = im2d::GetNearZ(); g_rz = 1.0f / cam->nearPlane;
	uint32 zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE), atf = GetRenderState(ALPHATESTFUNC), cull = GetRenderState(CULLMODE);
	SetRenderState(ZTESTENABLE, 0); SetRenderState(ZWRITEENABLE, 0); SetRenderState(ALPHATESTFUNC, ALPHAALWAYS); SetRenderState(CULLMODE, CULLNONE);
	SetRenderState(VERTEXALPHA, 1); SetRenderState(SRCBLEND, BLENDSRCALPHA); SetRenderState(DESTBLEND, BLENDINVSRCALPHA);
	SetRenderState(TEXTUREFILTER, Texture::LINEAR); SetRenderState(TEXTUREADDRESS, Texture::CLAMP);
	g_bOpen = false;
	for(Rm8Ui *u : vis) drawWidget(u, u->root);
	drawTooltip();
	flush(); g_bOpen = false;
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(ALPHATESTFUNC, atf); SetRenderState(CULLMODE, cull);
}

const std::string &rm8Str(int id){ static std::string empty; auto it = g_str.find(id); return it == g_str.end() ? empty : it->second; }

std::string rm8StrFmt(int id, ...)
{
	const std::string &f = rm8Str(id);
	char buf[1024];
	va_list ap; va_start(ap, id); vsnprintf(buf, sizeof buf, f.c_str(), ap); va_end(ap);
	return buf;
}

void rm8UiSheetText(rw::Camera *cam, const std::string &key, int cols, int rows, float cw, float ch, const char *codes,
                    const char *txt, float x, float y, float scale, float spacing)
{
	g_z = im2d::GetNearZ(); g_rz = 1.0f / cam->nearPlane;
	uint32 zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE), atf = GetRenderState(ALPHATESTFUNC);
	SetRenderState(ZTESTENABLE, 0); SetRenderState(ZWRITEENABLE, 0); SetRenderState(ALPHATESTFUNC, ALPHAALWAYS);
	SetRenderState(VERTEXALPHA, 1); SetRenderState(SRCBLEND, BLENDSRCALPHA); SetRenderState(DESTBLEND, BLENDINVSRCALPHA);
	g_bOpen = false;
	sheetText(key, cols, rows, cw, ch, codes, txt, x, y, scale, spacing, 0xffffffff);
	flush(); g_bOpen = false;
	SetRenderState(TEXTUREFILTER, Texture::LINEAR);
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(ALPHATESTFUNC, atf);
}
void rm8UiHudScale(float &sx, float &sy){ sx = g_W / 1024.0f; sy = g_H / 768.0f; }

void rm8UiSetTooltip(const std::string &t){ if(t == g_tip) return; g_tip = t; g_tipShow = false; g_tipT = std::chrono::steady_clock::now(); }

bool rm8UiGetRect(Rm8Ui *u, int id, float &x, float &y, float &w, float &h)
{
	Widget *k = findId(u, id);
	if(!k) return false;
	x = k->x; y = k->y; w = k->w; h = k->h;
	return true;
}

bool rm8UiSetRect(Rm8Ui *u, int id, float x, float y, float w, float h)
{
	Widget *k = findId(u, id);
	if(!k) return false;
	if(k->x == x && k->y == y && k->w == w && k->h == h) return true;
	k->x = x; k->y = y; k->w = w; k->h = h;
	layout(u);
	return true;
}

// ---- [A] helpers for the front-end menus (menu.cpp): runtime widgets, absolute rectangles, hide all
void rm8UiHideAll(){ for(auto &kv : g_uis) if(kv.second) kv.second->shown = false; g_pressed = nullptr; g_pressedUi = nullptr; g_hover = nullptr; g_hoverUi = nullptr; }
static std::vector<Rm8Ui*> g_hiddenStack;
void rm8UiPushHidden(){ g_hiddenStack.clear(); for(auto &kv : g_uis) if(kv.second && kv.second->shown) g_hiddenStack.push_back(kv.second); rm8UiHideAll(); }
void rm8UiPopHidden(){ for(Rm8Ui *u : g_hiddenStack) rm8UiShow(u, true); g_hiddenStack.clear(); }

int rm8UiAddWidget(Rm8Ui *u, const char *type, int parentId, float x, float y, float w, float h, int pic, unsigned bcolor, int fontSize, int index)
{
	if(!u || !u->root) return 0;
	static int nextId = 5000;
	Widget *p = parentId ? findId(u, parentId) : u->root;
	if(!p) p = u->root;
	Widget *k = new Widget;
	k->id = nextId++; k->name = "dyn" + std::to_string(k->id); k->type = type; k->parent = p;
	k->x = x; k->y = y; k->w = w; k->h = h; k->index = index; k->order = 100000 + k->id;
	k->bcolor = bcolor; k->fontSize = fontSize; k->dyn = true;
	if(pic >= 0){ for(int &q : k->pic) q = pic; k->border = 5; }
	u->all.push_back(k); p->kids.push_back(k);
	std::sort(p->kids.begin(), p->kids.end(), [](Widget *a, Widget *b){ return a->index != b->index ? a->index < b->index : a->order > b->order; });
	layout(u);
	return k->id;
}

bool rm8UiSetIgnore(Rm8Ui *u, int id, bool v){ Widget *w = findId(u, id); if(w) w->ignore = v; return w != nullptr; }
bool rm8UiSetBColor(Rm8Ui *u, int id, unsigned argb){ Widget *w = findId(u, id); if(w) w->bcolor = argb; return w != nullptr; }

bool rm8UiGetScreenRect(Rm8Ui *u, int id, float &x, float &y, float &w, float &h)
{
	Widget *k = findId(u, id);
	if(!k) return false;
	x = k->ax; y = k->ay; w = k->aw; h = k->ah;
	return true;
}

// [A] an empty full-screen UI (a black canvas for the logo; widgets come from rm8UiAddWidget)
Rm8Ui *rm8UiCreateCanvas(const std::string &name, Rm8UiMode mode)
{
	auto it = g_uis.find(name);
	if(it != g_uis.end() && it->second) return it->second;
	Rm8Ui *u = new Rm8Ui; u->name = name; u->mode = mode;
	Widget *r = new Widget; r->name = "root"; r->type = "plane"; r->w = 1024; r->h = 768; r->picType = 0;
	u->all.push_back(r); u->root = r;
	layout(u);
	return g_uis[name] = u;
}

bool rm8UiSetTint(Rm8Ui *u, int id, unsigned argb){ Widget *w = findId(u, id); if(w) w->tint = argb; return w != nullptr; }

// [A] menu animation helpers (menu_anim.cpp): picture turn, picture size, UI name, and an immediate-mode overlay drawn above all UIs
bool rm8UiSetRot(Rm8Ui *u, int id, float deg){ Widget *w = findId(u, id); if(w) w->rot = deg; return w != nullptr; }
bool rm8UiPicSize(int uvlId, float &w, float &h){ const UvRec *r = uv(uvlId); if(!r) return false; w = r->w; h = r->h; return true; }
const std::string &rm8UiName(Rm8Ui *u){ static std::string none; return u ? u->name : none; }
static uint32 g_ovZt, g_ovZw, g_ovAtf, g_ovCull;
void rm8UiOverlayBegin(Camera *cam)
{
	g_z = im2d::GetNearZ(); g_rz = 1.0f / cam->nearPlane;
	g_ovZt = GetRenderState(ZTESTENABLE); g_ovZw = GetRenderState(ZWRITEENABLE); g_ovAtf = GetRenderState(ALPHATESTFUNC); g_ovCull = GetRenderState(CULLMODE);
	SetRenderState(ZTESTENABLE, 0); SetRenderState(ZWRITEENABLE, 0); SetRenderState(ALPHATESTFUNC, ALPHAALWAYS); SetRenderState(CULLMODE, CULLNONE);
	SetRenderState(VERTEXALPHA, 1); SetRenderState(SRCBLEND, BLENDSRCALPHA); SetRenderState(DESTBLEND, BLENDINVSRCALPHA);
	SetRenderState(TEXTUREFILTER, Texture::LINEAR); SetRenderState(TEXTUREADDRESS, Texture::CLAMP);
	g_bOpen = false;
}
// a picture (uvlist id) scaled to w x h pixels and turned about its centre (cx, cy), screen pixels; colour 0xAARRGGBB
void rm8UiOverlayPic(int uvlId, float cx, float cy, float w, float h, float rotDeg, unsigned argb){ drawPic(uvlId, cx - w / 2, cy - h / 2, w, h, argb, 0, 0, rotDeg); }
void rm8UiOverlayRect(float x, float y, float w, float h, unsigned argb){ setTex(nil); quad(x, y, x + w, y + h, 0, 0, 1, 1, argb); }
void rm8UiOverlayEnd()
{
	flush(); g_bOpen = false;
	SetRenderState(ZTESTENABLE, g_ovZt); SetRenderState(ZWRITEENABLE, g_ovZw); SetRenderState(ALPHATESTFUNC, g_ovAtf); SetRenderState(CULLMODE, g_ovCull);
}

bool rm8UiSetColor(Rm8Ui *u, int id, unsigned argb){ Widget *w = findId(u, id); if(w) w->fcolor = argb; return w != nullptr; }

// [A] word wrap for long texts: the lines are joined with the game's '\' line break; width in design units (1024 x 768), font size in design pixels
std::string rm8UiWrapText(const std::string &utf8, float width, int fontSize)
{
	const std::string &txt = rm8ZhConvert(utf8);
	if(!g_fontOk) return txt;
	std::string out; float lw = 0;
	for(const char *s = txt.c_str(); *s;){
		const char *start = s;
		int cp = utf8Next(s);
		if(cp == '\\' || cp == '\n'){ out += '\\'; lw = 0; continue; }
		const Glyph *g = glyph(cp == '^' ? 0x3000 : cp, std::max(6, fontSize));
		float adv = g ? g->adv : fontSize * 0.5f;
		if(lw + adv > width && lw > 0){ out += '\\'; lw = 0; }
		out.append(start, s - start); lw += adv;
	}
	return out;
}

// ======================================================================== [N] netplay (net.cpp, docs/netplay.md section 3.2)
// The key of a widget for the replicated click / hover: .ui widgets = their index in the UI's list (the file order, the same on every device);
// runtime widgets (rm8UiAddWidget) = -2 - their rank among the UI's runtime widgets (their IDs come from one global counter that differs between
// devices; the order in which a UI creates its runtime widgets does not). -1 = none.
static int rm8UiNetKey(Rm8Ui *u, Widget *w)
{
	if(!u || !w) return -1;
	int rank = 0;
	for(size_t i = 0; i < u->all.size(); i++){
		Widget *k = u->all[i];
		if(k == w) return w->dyn ? -2 - rank : (int)i;
		if(k->dyn) rank++;
	}
	return -1;
}
static Widget *netWidget(Rm8Ui *u, int key)
{
	if(!u || key == -1) return nullptr;
	if(key >= 0) return key < (int)u->all.size() && !u->all[key]->dyn ? u->all[key] : nullptr;
	int want = -2 - key, rank = 0;
	for(Widget *k : u->all){ if(!k->dyn) continue; if(rank == want) return k; rank++; }
	return nullptr;
}
Rm8UiNetHooks &rm8UiNetHooks(){ static Rm8UiNetHooks h; return h; }
Rm8Ui *rm8UiFind(const std::string &name){ auto it = g_uis.find(name); return it == g_uis.end() ? nullptr : it->second; }
// a replicated click: the widget's handler, exactly as rm8UiPointerUp calls it (the enabled / shown flags are not checked: some are refreshed only when a
// frame is drawn, which differs between devices; the handlers check the game state themselves)
bool rm8UiNetApplyClick(const std::string &ui, int key)
{
	Rm8Ui *u = rm8UiFind(ui);
	Widget *w = netWidget(u, key);
	if(!w){ rm8Log("ui: net click on a missing widget %s/%d", ui.c_str(), key); return false; }
	if(u->onClick){ std::function<void(int)> fn = u->onClick; fn(w->id); }
	else if(u->onClickName){ std::string nm = w->name; std::function<void(const std::string &)> fn = u->onClickName; fn(nm); }
	return true;
}
void rm8UiNetApplyHover(const std::string &ui, int key)
{
	Rm8Ui *u = ui.empty() ? nullptr : rm8UiFind(ui);
	Widget *w = netWidget(u, key);
	g_netHotUi = w ? u : nullptr; g_netHot = w;
}
bool rm8UiNetLocalHot(std::string &ui, int &key)
{
	Widget *w = g_pressed ? g_pressed : g_hover;
	Rm8Ui *u = g_pressed ? g_pressedUi : g_hoverUi;
	if(!w || !u || !u->shown){ ui.clear(); key = -1; return false; }
	ui = u->name; key = rm8UiNetKey(u, w);
	return key != -1;
}
// test driver (net.cpp RM8_NET_MONKEY): the widgets a tap could click now (shown UIs with a click handler; visible, enabled, not ignored), screen centres
static bool chainVisible(Widget *w){ for(; w; w = w->parent) if(!w->visible) return false; return true; }
void rm8UiListButtons(std::vector<Rm8UiButtonInfo> &out)
{
	out.clear();
	for(auto &kv : g_uis){
		Rm8Ui *u = kv.second;
		if(!u || !u->shown || !u->root || !(u->onClick || u->onClickName)) continue;
		for(Widget *w : u->all){
			if(w == u->root || w->ignore || !w->enabled || !chainVisible(w) || w->aw < 2 || w->ah < 2) continue;
			if(w->type != "button" && w->type != "title" && !(w->name.size() && u->onClickName)) continue;
			float cx = w->ax + w->aw / 2, cy = w->ay + w->ah / 2;
			Rm8Ui *hu = nullptr; Widget *top = hit(cx, cy, &hu);
			if(top != w) continue;	// covered by something else
			Rm8UiButtonInfo b; b.ui = u->name; b.key = rm8UiNetKey(u, w); b.id = w->id; b.name = w->name; b.type = w->type; b.x = cx; b.y = cy;
			out.push_back(b);
		}
	}
}
