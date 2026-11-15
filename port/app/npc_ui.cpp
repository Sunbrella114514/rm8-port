// NPC presentation helpers: the god's greeting line, the reel dialog of 財神 / 窮神 and the time bomb counter. [V] = decompile, [A] = approximation.
#include "npc_int.h"
#include "story.h"
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>

using namespace rw;

namespace npc {

// ---------------------------------------------------------------- greeting line
// [V] Cls_5e33a8__OnUpdate @0x49b910 (state 1, stage 3): the line of string 400 + id drawn white over three black copies shifted by
// (0, 2) (2, 0) (2, 2), at the horizontal centre, h / 2 + h / 10 from the top (GDI font 32)
namespace {
Rm8Ui *g_greet = nullptr; int g_gw[4] = {0, 0, 0, 0}; bool g_gShown = false;
Rm8Ui *greetUi()
{
	if(g_greet) return g_greet;
	g_greet = rm8UiCreateCanvas("npcgreet", RM8UI_HUD);
	if(!g_greet) return nullptr;
	// [V] @0x49b910: the screen is 800 x 600 in the game; font 32 px, the line starts at y = H / 2 + H / 10 = 0.6 H, the shadows are 2 px away. In the 1024 x 768 design
	// units of the HUD canvas: y = 460.8, font 41, offsets 2.56
	static const float off[4][2] = {{0, 2.56f}, {2.56f, 0}, {2.56f, 2.56f}, {0, 0}};
	for(int k = 0; k < 4; k++){
		g_gw[k] = rm8UiAddWidget(g_greet, "label", 0, 112 + off[k][0], 460.8f + off[k][1], 800, 42, -1, 0, 41, k < 3 ? 1 : 2);
		char nm[32]; snprintf(nm, sizeof nm, "dyn%d", g_gw[k]);
		rm8UiSetColorN(g_greet, nm, k < 3 ? 0xff000000 : 0xffffffff);
		rm8UiSetAlignN(g_greet, nm, 1, 1);
	}
	return g_greet;
}
}

void greetingShow(const std::string &t)
{
	Rm8Ui *u = greetUi(); if(!u) return;
	for(int k = 0; k < 4; k++) rm8UiSetText(u, g_gw[k], t);
	rm8UiShow(u, true); g_gShown = true;
}
void greetingHide(){ if(g_greet && g_gShown){ rm8UiShow(g_greet, false); g_gShown = false; } }

// ---------------------------------------------------------------- reel dialog
// [V] Lottery_InitScrollBar @0x49f810 / Cls_5e2a80__OnUpdate @0x49f570 / Lottery_OnStop @0x49fab0 / Lottery_CalculateResult @0x49fb50: the dialog is scrbar (4 digit
// reels, x = 74 143 211 279) in the normal game and sscrbar (6 reels) when the story mode flag (*0x632de8) is set. Every reel is a CUi (@0x4108d0, stride 0x24c) scrolling
// the cells of J218.tga (a sprite sheet of 52 x 40 cells, drawn 40 x 32 at the label's position: digits 1 2 3 .. 9 0 on the top row, x and divide at (51, 214) / (102, 214))
// downwards at `speed` px / s, 32 px per char, sample 0x74 (half volume) at every char. Digit reels (10 chars, first shown "1", speed 200 + (rand() % 10) * 10),
// story: reel 4 = the sign (2 chars: x, divide), reel 5 = the factor (3 chars: 1 2 3, factor = index + 1), both first shown "x" "1".
// A computer presses after 1.0 s (+0xde0), a human with a key / click (Cls_5e2a80__OnAbort -> Lottery_OnStop): the title is cleared and reel 3 (and in the story mode reels
// 4 and 5) get deceleration 200 px / s^2; reel n < 3 starts to slow down when reel n + 1 has stopped (a reel stops at the next char once its speed is <= 50). The story
// mode rolls cheat in 1 of 5 games (+0xde4 = rand() % 5 == 0): a slowing reel stops on the char of its right neighbour, so all four digits come out equal. Only then
// (all four equal and not 0) the extra reels are updated at all - they sit on "x 1" otherwise - first the sign, then the factor, and the amount is
// digits x factor (sign x) or digits / factor (sign divide). The result stays 1.2 s. [A] the speed of the scroll direction / the exact stop positions follow CUi_CalcTrajectory
namespace {
const int CELL_DIGIT[10][2] = {{461, 0}, {0, 0}, {51, 0}, {102, 0}, {154, 0}, {205, 0}, {256, 0}, {307, 0}, {358, 0}, {410, 0}};	// DAT_005e36c8
const int CELL_OP[2][2] = {{51, 214}, {102, 214}};								// DAT_005e3768
const int CELL_MUL[3][2] = {{0, 0}, {51, 0}, {102, 0}};								// DAT_005e36d8
struct Wheel {
	int count = 10; const int (*cell)[2] = nullptr;
	int cur = 0, target = -1, snd = 0;
	float d = 0, speed = 0, decel = 0;	// d: how far the current char has slid down (0 .. 32)
	float x = 0, y = 0;			// the window (design units, relative to the dialog)
	int wa = 0, wb = 0;			// the two sprite widgets: the current char (lower part) and the next char entering at the top
};
struct Reel {
	Rm8Ui *ui = nullptr; bool active = false, human = false, story = false, cheat = false;
	int n = 4, state = 1;			// state +0xddc: 1 rolling, 2 stopping, 0 the result is shown
	float autoT = 0, resT = 0;		// +0xde0, _DAT_00642260
	Wheel w[6]; int amount = 0;
	void (*done)(void *, int) = nullptr; void *ctx = nullptr;
} g_reel;
std::map<Rm8Ui*, std::vector<int>> g_sprites;	// per dialog: the sprite widget ids (2 per reel)
const char *SHEET = "prt\\ui\\inside\\j218.tga";

void wheelShow(Wheel &w)
{
	Rm8Ui *u = g_reel.ui;
	const float TW = 512.0f, TH = 256.0f, CW = 52.0f, CH = 40.0f;
	float d = std::max(0.0f, std::min(31.99f, w.d)), f = (32.0f - d) / 32.0f;
	int cx = w.cell[w.cur][0], cy = w.cell[w.cur][1];
	rm8UiSetRect(u, w.wa, w.x, w.y + d, 40, 32.0f - d);
	rm8UiSetPicFileUV(u, w.wa, SHEET, cx / TW, cy / TH, std::min(1.0f, (cx + CW) / TW), (cy + CH * f) / TH);
	rm8UiSetVisible(u, w.wa, true);
	int nx = (w.cur + 1) % w.count, nxx = w.cell[nx][0], nxy = w.cell[nx][1];
	if(d > 0.5f){
		rm8UiSetRect(u, w.wb, w.x, w.y, 40, d);
		rm8UiSetPicFileUV(u, w.wb, SHEET, nxx / TW, (nxy + CH * (1.0f - d / 32.0f)) / TH, std::min(1.0f, (nxx + CW) / TW), (nxy + CH) / TH);
		rm8UiSetVisible(u, w.wb, true);
	}else rm8UiSetVisible(u, w.wb, false);
}
void showAll(){ for(int k = 0; k < g_reel.n; k++) wheelShow(g_reel.w[k]); }

// [V] CUi_UpdateCharAnimation @0x410b30 (false = the reel is stopped), CUi_CalcTrajectory @0x410f70, CUi_CalcDistance @0x410f40
bool wheelUpdate(Wheel &w, float dt)
{
	float ldt = std::min(dt, 0.1f);
	if(w.speed <= 0) return false;
	if(w.speed > 50.0f){
		if(w.target < 0 || w.speed >= 150.0f) w.speed -= ldt * w.decel;
		else{
			int dist = w.target < w.cur ? w.target + w.count - w.cur : w.target - w.cur;
			if(dist == 1) w.speed = 50.0f;
			else if(dist > 1 && dist < 4) w.speed -= ldt * w.decel;
		}
	}
	float acc = w.d + ldt * w.speed;
	if(acc < 32.0f){ w.d = acc; return true; }
	float over = acc - 32.0f;
	if(w.snd) rm8SndStop(w.snd);
	w.snd = rm8SndPlay(0x74, 1, 0.5f);	// every char
	w.cur = (w.cur + 1) % w.count; w.d = 0;
	if(w.speed > 50.0f || (w.target >= 0 && w.target != w.cur)){ if(over > 0) w.d = std::min(over, 31.0f); }
	else w.speed = 0;
	return true;
}

void reelStop()	// [V] Lottery_OnStop @0x49fab0
{
	Reel &r = g_reel;
	if(r.state != 1) return;
	r.w[3].decel = 200.0f;
	if(r.n == 6){ r.w[4].decel = 200.0f; r.w[5].decel = 200.0f; }
	rm8UiSetText(r.ui, 2, "");	// the dialog's vf+8 with an empty text: the title goes
	r.state = 2; r.autoT = 0;
}
}

void reelOpen(const std::string &title, bool human, void (*done)(void *, int), void *ctx)
{
	bool story = rm8StoryActive() || getenv("RM8_NPC_REEL_STORY") != nullptr;	// test hook: the story mode dialog in a free game
	Rm8Ui *u = rm8UiLoad(story ? "sscrbar" : "scrbar", RM8UI_DIALOG);
	for(int k = 0; k < 6; k++) if(g_reel.w[k].snd) rm8SndStop(g_reel.w[k].snd);
	g_reel = Reel();
	if(getenv("RM8_NPC_REEL_HUMAN")) human = true;	// test hook: the dialog waits for a press (RM8_NPC_REEL_PRESS = seconds until it is pressed)
	Reel &r = g_reel;
	r.ui = u; r.human = human; r.story = story; r.n = story ? 6 : 4; r.done = done; r.ctx = ctx; r.active = true;
	if(!u){ if(done) done(ctx, 1000 + (rnd() % 9000)); r.active = false; return; }
	rm8UiSetText(u, 2, title);
	rm8UiSetColor(u, 2, 0xff301800);
	std::vector<int> &sp = g_sprites[u];
	if(sp.empty()){	// the sprite widgets over the reel boxes, created once per dialog
		rm8UiOnClick(u, [](int){ reelPress(); });
		for(int k = 0; k < 12; k++) sp.push_back(rm8UiAddWidget(u, "plane", 0, 0, 0, 40, 32, -1, 0, 16, 9));
	}
	for(int k = 0; k < r.n; k++){
		Wheel &w = r.w[k];
		float lx = 0, ly = 0, lw = 0, lh = 0;
		rm8UiGetRect(u, 3 + k, lx, ly, lw, lh);
		rm8UiSetText(u, 3 + k, "");
		w.x = lx; w.y = ly; w.wa = sp[k * 2]; w.wb = sp[k * 2 + 1];
		if(k < 4){ w.count = 10; w.cell = CELL_DIGIT; w.cur = 1; }	// CUi_Initialize(.., 10, 1, ..): first char index 1
		else if(k == 4){ w.count = 2; w.cell = CELL_OP; w.cur = 0; }
		else{ w.count = 3; w.cell = CELL_MUL; w.cur = 0; }
		w.speed = 200.0f + (float)((rnd() % 10) * 10);	// [V] @0x49f90f: fild (rand() % 10 * 10) + 200.0
	}
	for(int k = r.n; k < 6; k++){ rm8UiSetVisible(u, sp[k * 2], false); rm8UiSetVisible(u, sp[k * 2 + 1], false); }
	r.cheat = story && ((rnd() % 5) == 0);	// [V] +0xde4
	if(getenv("RM8_NPC_REEL_CHEAT")) r.cheat = true;
	r.autoT = human ? 0.0f : 1.0f;		// [V] +0xde0 = 1.0 when the current role is a computer
	showAll();
	rm8UiShow(u, true);
}

bool reelActive(){ return g_reel.active; }
bool reelPress()
{
	if(!g_reel.active || g_reel.state != 1 || !g_reel.human) return false;
	reelStop();
	return true;
}

void reelUpdate(float dt)
{
	Reel &r = g_reel;
	if(!r.active) return;
	static float tHuman = 0;
	if(r.state == 1 && r.human){
		static const char *autoPress = getenv("RM8_NPC_REEL_PRESS");
		tHuman += dt;
		if(autoPress && tHuman >= (float)atof(autoPress)){ reelPress(); tHuman = 0; }
	}else if(r.state == 0) tHuman = 0;
	if(r.state == 0){
		r.resT += dt;
		if(r.resT > 1.2f){	// [V] the result is shown 1.2 s (_DAT_00642260 > 1.2)
			r.active = false;
			rm8UiShow(r.ui, false);
			if(r.done) r.done(r.ctx, r.amount);
		}
		return;
	}
	if(r.autoT > 0){ r.autoT -= dt; if(r.autoT < 0) reelStop(); }
	int k = 0;
	while(k < 4 && wheelUpdate(r.w[k], dt)){
		if(k < 3 && r.w[k].speed >= 200.0f && r.w[k + 1].speed <= 0){	// the right neighbour has stopped: this reel slows down
			r.w[k].decel = 200.0f;
			if(r.cheat) r.w[k].target = r.w[k + 1].cur;
		}
		k++;
	}
	if(k == 0){	// reel 0 has stopped: all digits are in
		int c = 1;
		if(r.w[0].cur > 0) while(c < 4 && r.w[0].cur == r.w[c].cur) c++;
		if(r.n == 6 && c > 3 && (wheelUpdate(r.w[4], dt) || wheelUpdate(r.w[5], dt))){ showAll(); return; }	// four equal digits: the sign and the factor roll
		r.amount = 0;
		for(int i = 0; i < 4; i++) r.amount = r.amount * 10 + r.w[i].cur;	// [V] Lottery_CalculateResult @0x49fb50
		if(r.n == 6){ int f = r.w[5].cur + 1; r.amount = r.w[4].cur == 0 ? r.amount * f : r.amount / f; }
		r.state = 0; r.resT = 0;
		if(getenv("RM8_DEBUG_NPC")) rm8Log("npc: reel result %d (digits %d%d%d%d, sign %d, factor %d)", r.amount, r.w[0].cur, r.w[1].cur, r.w[2].cur, r.w[3].cur, r.w[4].cur, r.w[5].cur + 1);
	}
	showAll();
}

void reelClear()
{
	if(g_reel.ui && g_reel.active) rm8UiShow(g_reel.ui, false);
	for(int k = 0; k < 6; k++) if(g_reel.w[k].snd) rm8SndStop(g_reel.w[k].snd);
	g_reel = Reel();
}

// ---------------------------------------------------------------- time bomb counter
// [V] Billboard_RenderWithNumber @0x499ef0 (CPlayerTag__Render): two camera facing 32 x 32 quads (TimeBomb.tga, TimeBomb_01.tga from texture\prt\npc),
// the counter "%2d" in black (font 16) at the projected corner. [A] drawn as 2D quads above the role's head.
namespace {
bool projectP(Camera *cam, const float p[3], float &sx, float &sy)
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
void quadTex(Camera *cam, Texture *t, float x0, float y0, float x1, float y1)
{
	if(!t) return;
	gl3::Im2DVertex v[4]; uint16 idx[6] = {0, 1, 2, 2, 1, 3};
	float rz = 1.0f / cam->nearPlane, z = im2d::GetNearZ();
	float px[4] = {x0, x1, x0, x1}, py[4] = {y0, y0, y1, y1}, pu[4] = {0, 1, 0, 1}, pv[4] = {0, 0, 1, 1};
	for(int q = 0; q < 4; q++){
		v[q].setScreenX(px[q]); v[q].setScreenY(py[q]); v[q].setScreenZ(z);
		v[q].setCameraZ(cam->nearPlane); v[q].setRecipCameraZ(rz);
		v[q].setColor(255, 255, 255, 255); v[q].setU(pu[q], rz); v[q].setV(pv[q], rz);
	}
	SetRenderStatePtr(TEXTURERASTER, t->raster);
	im2d::RenderIndexedPrimitive(PRIMTYPETRILIST, v, 4, idx, 6);
}
}

void bombRender(Camera *cam)
{
	if(!cam || !cam->frameBuffer) return;
	static Texture *tb = nullptr, *tb1 = nullptr; static bool tried = false;
	if(!tried){ tried = true; tb = rm8PackTexture("prt\\npc\\TimeBomb.tga"); tb1 = rm8PackTexture("prt\\npc\\TimeBomb_01.tga"); if(!tb) rm8Log("warning: TimeBomb.tga missing"); }
	if(!tb) return;
	bool any = false;
	for(int s = 0; s < rm8TurnRoleCount(); s++) if(rm8NpcTimeBomb(s) > 0 && rm8TurnStatus(s) != 7) any = true;
	if(!any) return;
	float k = (float)cam->frameBuffer->height / 600.0f;
	uint32 zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE), atf = GetRenderState(ALPHATESTFUNC);
	SetRenderState(ZTESTENABLE, 0); SetRenderState(ZWRITEENABLE, 0); SetRenderState(ALPHATESTFUNC, ALPHAALWAYS);
	SetRenderState(VERTEXALPHA, 1); SetRenderState(SRCBLEND, BLENDSRCALPHA); SetRenderState(DESTBLEND, BLENDINVSRCALPHA);
	for(int s = 0; s < rm8TurnRoleCount(); s++){
		int n = rm8NpcTimeBomb(s);
		float p[3];
		if(n <= 0 || rm8TurnStatus(s) == 7 || !rm8RolesGetPos(s, p)) continue;
		p[1] += 60.0f;
		float sx, sy;
		if(!projectP(cam, p, sx, sy)) continue;
		float w = 40.0f * k;
		quadTex(cam, tb, sx - w / 2, sy - w / 2, sx + w / 2, sy + w / 2);
		char txt[8]; snprintf(txt, sizeof txt, "%d", n);
		rm8UiSheetText(cam, "font\\number.tga", 4, 3, 32, 42, "0123456789* ", txt, sx - 8.0f * k * (int)strlen(txt), sy - 10.0f * k, 0.5f * k, 0);
	}
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(ALPHATESTFUNC, atf);
}

}	// namespace npc
