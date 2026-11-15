// Minigame 23 (ltlgame_23.dll, debug build): 踩蛋 / egg stamping clown. A transliteration of the DLL on the mg:: host services.
//
// Tags: [V] verified in the decompile / disassembly (0x1004xxxx = DLL address), [H] hypothesis, [A] port adaptation.
// DLL object map (names are the docs' / ghidra_lg_named names):
//   Game      (0x70 bytes, DAT_100af684)  Init @10041480 Update @10043850 Draw @10043e30 OnKeyDown @10043e90 OnMouseButton @10044300
//   PipeEggMgr(0x138, DAT_100af688)       sprites @10037bf0 init @10037f00 Draw @10038250 Update @100389f0 Collide @10039270 Pipes @1003a420
//   Clown     (0x90,  DAT_100af67c)       sprites @1003e750 InitState @1003e880 OnHit @1003ea60 StartJump @1003eba0 Update @1003ec70 Draw @1003ef50
//   HudUi     (0xe0,  DAT_100af8fc)       sprites @100400b0 DrawFrame @1003f7b0     ShakeMeter (0x10, DAT_100af680) Tick @10043ca0
// Float arithmetic: the DLL runs the x87 at 53-bit precision (default CW 0x27f, cf. the fsin wrappers @10071004), so chains that the compiler kept on
// the FPU stack are evaluated here in double and rounded to float on the store (all "ground" compares are `ground <= y`, the DLL's fcomp/test ah,0x41).
// Compare directions were checked in the disassembly (Ghidra prints `a < b != (a == b)` for `a <= b`).
// Rule of the game (derived from the code, the docs call the trigger "unconfirmed"):
//   * eggs fired by the left pipe (+20) and the right pipe (+10) fly horizontally; a stomped egg falls straight down and lands on the ground or on
//     the egg already lying there ("pile"). The clown stands on top of the pile (CollideEggs end).
//   * pile top reaching the right pipe mouth stops the right pipe (flag +0xf0), reaching the left pipe mouth stops the left pipe (+0xf1) and ends
//     the game at once (Update @10043850: the clown turns into sprite 0x7e3 and the end flag +0x20 is set);
//   * if the two topmost pile eggs are more than 37*scale apart horizontally the pile collapses: the pile eggs scatter for 1 s (1.5 s with >= 5
//     eggs), then the game ends (Update @100389f0).
// Test hooks (inside this file only): RM8_MG23_TRACE=1 logs the gameplay events to stderr; RM8_MG23_BOT=1 (2 = right-pipe eggs only) plays the clown with a small
// look-ahead bot (it presses / releases the mouse button through mouseButton() so the whole input path is exercised). RM8_MG23_SCORE0=n starts with n points; RM8_MG23_PILE=n starts with n stacked eggs (RM8_MG23_PILEX=dx: shifted sideways); RM8_MINI_PLAYERS="hh" selects
// mode 2 (shaker), RM8_MINI_SCRIPT="t:down:down;..." drives the arrow keys.
#include "mg_core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using mg::V3;

// [V] Entity @10046fa0 (ctor), @10047070 (Draw), @100471d0 (UpdateAnim). 0x40 bytes: +0 sprite, +4 pos, +0x10 angle, +0x14 scale, +0x20 vx, +0x24 vy,
// +0x34/+0x38 half extents of the collision box (set by the owner), +0x3c valid flag (written by CollideEggs, read by nobody)
struct Entity {
	int spr = 0;
	float x = 0, y = 0, z = 0, ang = 0;
	float sx = 1, sy = 1, sz = 1;
	float vx = 0, vy = 0, vz = 0;
	float ex = 0, ey = 0;
	bool valid = true;
	void draw() const {	// [V] 0x10047070: +0x64 scale, +0x6c rotation about z, +0x68 position, +0x7c draw
		mg::spScale(spr, V3(sx, sy, sz)); mg::spRotZ(spr, ang); mg::spPos(spr, V3(x, y, z)); mg::spDraw(spr);
	}
	void updateAnim(float dt) const { mg::spUpdate(spr, dt); }	// [V] 0x100471d0: +0x78
};

// [V] FUN_10040ee0: inclusive point-in-rect; LG23_RectsOverlap @10040f60: a corner of either rectangle inside the other
bool pointIn(int px, int py, int l, int t, int r, int b){ return px >= l && px <= r && py >= t && py <= b; }
bool rectsOverlap(int a1, int a2, int a3, int a4, int b1, int b2, int b3, int b4)
{
	bool a = pointIn(a1, a2, b1, b2, b3, b4) || pointIn(a1, a4, b1, b2, b3, b4) || pointIn(a3, a2, b1, b2, b3, b4) || pointIn(a3, a4, b1, b2, b3, b4);
	bool b = pointIn(b1, b2, a1, a2, a3, a4) || pointIn(b1, b4, a1, a2, a3, a4) || pointIn(b3, b2, a1, a2, a3, a4) || pointIn(b3, b4, a1, a2, a3, a4);
	return a || b;
}
inline float fl(double v){ return (float)v; }
// [V] __ftol: truncation toward zero
inline int ftol(double v){ return (int)v; }
// the DLL dword colour of Sprite_SetColor @0x462370 is 0xAABBGGRR; the framework takes argb
inline uint32_t dllColor(uint32_t c){ return (c & 0xff00ff00u) | ((c & 0xff) << 16) | ((c >> 16) & 0xff); }

struct Popup { float life = 1, y = 0; int value = 0; float x = 0, z = 0, rise = 100; };	// [V] FUN_10044860 / FUN_10044940 (+0 life, +4 value, +8 pos, +0x18 rise speed)

struct G23 : MgGame {
	// ---- Game (0x70 bytes) -------------------------------------------------------------------------------------------------------
	float Wf = (float)mg::W, Hf = (float)mg::H;	// [V] host +0x120 / +0x124
	float sc = 0;			// +0x38 = W / 800
	float gravity = 0;		// +0x48 = 5000 * scale
	float tleft = 60.0f;		// +4  [V] 0x42700000
	int secs = 0, lastSecs = 0;	// +0xc, +0x10
	float readyT = 2.0f;		// +0x14 [V] 0x40000000
	float endT1 = 1.0f, endT2 = 1.0f;	// +0x18, +0x1c [V] 0x3f800000
	bool ended = false;		// +0x20
	int mode = 0;			// +0x24
	bool running = true;		// +0x30
	int score = 0;			// +0x4c
	bool introOpen = true;		// +0x50
	int lastReadySec = 5;		// +0x54 [V] Init writes 5
	bool ratingSound = false;	// +0x69
	int rating = 0;			// +0x6c
	bool ratingKnown = false;	// [A] (the DLL leaves +0x6c uninitialised until the rating banner is drawn)
	Rm8Ui *ui = nullptr;		// +0x2c
	std::vector<Popup> popups;	// +0x58 vector
	bool trace = false;
	bool bot = false, botDown = false; float botHold = 0; int botMode = 0;	// test bot (RM8_MG23_BOT)

	// ---- ShakeMeter (DAT_100af680) -----------------------------------------------------------------------------------------------
	int shakeCount = 0; float shakeRate = 0; unsigned shakeLast = 0;	// [V] Reset @10041c50 (count, rate, last timeGetTime)
	static unsigned nowMs(){ return (unsigned)(mg::now() * 1000.0f + 0.5f); }	// [A] timeGetTime -> the game clock

	// ---- PipeEggMgr (0x138) ----------------------------------------------------------------------------------------------------------
	int sprEggL = 0, sprEggR = 0, sprFxL = 0, sprFxR = 0, sprPipeL = 0, sprPipeR = 0;	// +0x1c +0x20 +0x24 +0x28 +0x2c +0x30
	Entity fxL, fxR;		// +0x34, +0x74 (the muzzle flash entities)
	bool pipeLBusy = false, pipeRBusy = false;	// +0xb4, +0xb5
	std::vector<Entity> flying, falling, pile;	// +0xb8, +0xc8, +0xd8
	float tPipeL = 4.0f, tPipeR = 2.0f;	// +0xe8, +0xec [V] 0x10037f00 (4.0, 2.0)
	bool rightUsable = true, leftUsable = true;	// +0xf0, +0xf1
	float collapseT = 0;		// +0xf4
	bool wobble = false;		// +0xf8 (pile wobble warning)
	float wobX = 0, wobY = 0;	// +0xfc, +0x100
	float eggHalfW = 0, eggHalfH = 0;	// +0x120 = 27*scale, +0x124 = 16*scale
	float shakeK1 = 2.0f, shakeK2 = 10.0f;	// +0x110, +0x114 [V] 0x10037bf0
	float pipeOff = 0;		// +0x11c
	uint32_t warnColor = 0xffff0000u, whiteColor = 0xffffffffu;	// +0x128 / +0x12c: bytes ff 00 00 ff = DLL dword 0xff0000ff (R = low byte) = red; ff ff ff ff = white
	bool soundWob = false, soundTopple = false, soundToppleReq = false;	// +0, +1, +2
	float phase = 0;		// DAT_100af640 (pipe shake phase)
	bool eggVelInit = false;	// DAT_100af644

	// ---- Clown (0x90) ----------------------------------------------------------------------------------------------------------------
	int sprIdle = 0, sprJump = 0, sprHurt = 0, sprEnd = 0, sprFall = 0;	// +0x20 +0x24 +0x28 +0x2c +0x30
	Entity cl;			// +0x34 entity: spr = current sprite, ex/ey = 28*s, 46*s
	float holdT = 0;		// +0x74
	bool held = false, grounded = true;	// +0x78, +0x79
	float inj = 0;			// +0x7c
	int lives = 6;			// +0x80 (only decremented, never read)
	float footEx = 0, footEy = 0;	// +0x88, +0x8c = 17*s, 12*s

	// ---- HudUi (0xe0) ----------------------------------------------------------------------------------------------------------------
	int digA[10] = {}, digB[10] = {}, digC[10] = {};	// +0x30 / +0x58 / +0x80 digit sprites (score / clock > 10 s / clock <= 10 s)
	int hMinus = 0, hPlus = 0, hPanelA = 0, hPanelB = 0;	// +0xa8 +0xac +0xb0 +0xb4
	int hReady = 0, hGo = 0, hEndL = 0, hEndR = 0, hTimeL = 0, hTimeR = 0, hRate0 = 0, hRate1L = 0, hRate1R = 0, hRate2 = 0;	// +0xb8 .. +0xdc
	int hFront = 0, hBgBL = 0, hBgBR = 0, hBgTL = 0, hBgTR = 0;	// 0x7d7 (+0x2c), 0x7db (+0x1c), 0x7dc (+0x20), 0x7d1 (+0x24), 0x7d9 (+0x28)

	double groundD() const { return ((double)Hf - (double)sc * 64.0) - (double)sc * 48.0; }	// [V] H - 64*s - 48*s (53-bit)
	void log(const char *fmt, float a = 0, float b = 0, float c = 0){ if(trace){ fprintf(stderr, "[mg23 %.2f] ", mg::now()); fprintf(stderr, fmt, a, b, c); fputc('\n', stderr); } }

	// ================================================================================================================================
	bool init(const std::vector<Rm8MiniPlayer> &pl) override
	{
		trace = getenv("RM8_MG23_TRACE") != nullptr; botMode = getenv("RM8_MG23_BOT") ? atoi(getenv("RM8_MG23_BOT")) : 0; bot = botMode != 0;
		// [V] Game_Init @10041480: lg_intro.ui, text string 0x1b2c = 6956, scale of the ui = W / 1024 (the host handles it)
		ui = mg::introLoad("lg__lg_intro", 6956, [this]{ closeIntro(); });
		// [V] mode: params[0x12] == 1 (current player is a computer) -> 1; else 2 if another player is human (type 2), else 0
		mode = 0;
		if(!pl.empty() && !pl[0].human) mode = 1;
		else for(size_t i = 1; i < pl.size(); i++) if(pl[i].human){ mode = 2; break; }
		running = true; introOpen = true; ended = false; endT1 = 1.0f; endT2 = 1.0f; readyT = 2.0f; lastReadySec = 5;
		mg::showCursor(true);	// [V] +0x130(1)
		sc = fl((double)Wf / 800.0);	// [V] +0x38 = W / 800 (= 1.28)
		gravity = fl((double)sc * 5000.0);	// [V] +0x48
		tleft = 60.0f; score = getenv("RM8_MG23_SCORE0") ? atoi(getenv("RM8_MG23_SCORE0")) : 0;	// [V] +4 = 60.0, +0x4c = 0 (RM8_MG23_SCORE0: test hook)
		if(!ui) introOpen = false;	// [A] no intro panel available: start directly
		mgrLoad(); mgrInit();	// [V] FUN_10037b90 = sprites @10037bf0 + init @10037f00
		clownLoad(); clownInitState();	// [V] Clown_Init @1003e6f0
		hudLoad();	// [V] FUN_1003f720 = HudUi_LoadSprites @100400b0
		mg::music(0x1a);	// [V] +0x108(0x1a, 0): BGM track 0x1a
		if(getenv("RM8_MG23_PILE")){	// test hook: start with n stacked eggs (55 px apart) on the ground
			int n = atoi(getenv("RM8_MG23_PILE"));
			for(int i = 0; i < n; i++){ Entity e = makeEgg(sprEggR, Wf / 2.0f + (getenv("RM8_MG23_PILEX") ? (float)atoi(getenv("RM8_MG23_PILEX")) : 0.0f), fl((double)Hf - 91.0 * (double)sc - 55.0 * i), 0); pile.push_back(e); }
		}
		ratingSound = false;	// [V] +0x68 / +0x69 = 0
		shakeCount = 0; shakeRate = 0; shakeLast = nowMs();	// [V] ShakeMeter_Reset @10041c50
		return true;
	}

	void closeIntro()	// [V] LG23_Game_CloseIntroDialog @100440e0
	{
		if(ui){ introOpen = false; mg::introHide(); ui = nullptr; }
	}

	// ---- sprite sets ---------------------------------------------------------------------------------------------------------------
	void mgrLoad()	// [V] FUN_10037bf0
	{
		sprPipeL = mg::spNew(0x7d3); sprPipeR = mg::spNew(0x7d2);
		pipeLBusy = pipeRBusy = false;
		sprEggL = mg::spNew(0x7d4); sprEggR = mg::spNew(0x7d5);
		sprFxL = mg::spNew(0x7d8); sprFxR = mg::spNew(0x7d8);
		V3 s1(sc, fl((double)sc * 1.5), sc);	// local_1c = scale.y * 1.5
		mg::spScale(sprPipeL, s1); mg::spScale(sprPipeR, s1);
		mg::spSetState(sprPipeL, 0); mg::spSetState(sprPipeR, 0);
		mg::spSetState(sprFxR, 2); mg::spSetState(sprFxL, 2);
	}
	void mgrInit()	// [V] FUN_10037f00
	{
		eggHalfW = fl((double)sc * 27.0); eggHalfH = fl((double)sc * 16.0);	// +0x120 / +0x124
		tPipeL = 4.0f; tPipeR = 2.0f;
		wobble = false; rightUsable = true; leftUsable = true; collapseT = 0;
		fxL = Entity(); fxR = Entity();
		fxL.spr = sprFxL;
		fxL.sx = fl(sc * 1.4); fxL.sy = fl(sc * 3.0); fxL.sz = sc;
		fxL.x = fl((double)sc * 48.0 + (double)sc * 96.0);
		fxL.y = fl((double)Hf - (double)sc * 492.0);
		fxR.spr = sprFxR;
		fxR.sx = fl(sc * 1.4); fxR.sy = fl(sc * 3.0); fxR.sz = sc;
		fxR.x = fl(((double)Wf - (double)sc * 96.0) - (double)sc * 48.0);
		fxR.y = fl((double)Hf - (double)sc * 300.0);
		fxR.ang = 180.0f;	// [V] +0x84 = 0x43340000
		warnColor = dllColor(0xff0000ffu); whiteColor = dllColor(0xffffffffu);	// [V] +0x128 bytes ff 00 00 ff; +0x12c ff ff ff ff
		soundWob = soundTopple = soundToppleReq = false;	// [V] *+0, +1, +2 = 0
		phase = 0; eggVelInit = false; pipeOff = 0; wobX = wobY = 0;
	}
	void clownLoad()	// [V] Clown_LoadSprites @1003e750
	{
		sprIdle = mg::spNew(0x7df); sprJump = mg::spNew(0x7e0); sprHurt = mg::spNew(0x7e1); sprEnd = mg::spNew(0x7e3); sprFall = mg::spNew(0x7e2);
	}
	void clownInitState()	// [V] Clown_InitState @1003e880
	{
		cl = Entity();
		cl.spr = sprIdle;
		cl.sx = sc; cl.sy = sc; cl.sz = sc;	// +0x48.. = scale vector
		cl.x = Wf / 2.0f;
		cl.y = fl(groundD());	// +0x3c = (H - 64 s) - 48 s
		cl.ex = fl((double)sc * 28.0); cl.ey = fl((double)sc * 46.0);	// +0x68 / +0x6c
		inj = 0; lives = 6; holdT = 0; held = false; grounded = true;	// +0x7c +0x80 +0x74 +0x78 +0x79
		footEx = fl((double)sc * 17.0); footEy = fl((double)sc * 12.0);	// +0x88 / +0x8c
	}
	void hudLoad()	// [V] HudUi_LoadSprites @100400b0: every sprite is scaled by the game scale vector (s, s, s)
	{
		V3 sv(sc, sc, sc);
		auto mk = [&](int art){ int h = mg::spNew(art); mg::spScale(h, sv); return h; };
		for(int i = 1; i < 10; i++) digA[i] = mk(i + 0x852);
		digA[0] = mk(0x85c);
		for(int i = 1; i < 10; i++) digB[i] = mk(i + 0x834);
		digB[0] = mk(0x83e);
		for(int i = 1; i < 10; i++) digC[i] = mk(i + 0x83f);
		digC[0] = mk(0x849);
		hMinus = mk(0x860); hPlus = mk(0x852); hPanelA = mk(0x83f); hPanelB = mk(0x84a);
		hFront = mk(0x7d7); hBgBL = mk(0x7db); hBgBR = mk(0x7dc); hBgTL = mk(0x7d1); hBgTR = mk(0x7d9);
		mg::spPos(hBgTL, V3(fl((double)sc * 256.0), fl((double)sc * 256.0), 0));
		mg::spPos(hBgTR, V3(fl((double)sc * 656.0), fl((double)sc * 256.0), 0));
		mg::spPos(hBgBL, V3(fl((double)sc * 256.0), fl((double)sc * 556.0), 0));
		mg::spPos(hBgBR, V3(fl((double)sc * 656.0), fl((double)sc * 556.0), 0));
		hReady = mk(0x84b); hGo = mk(0x84c); hEndL = mk(0x84d); hEndR = mk(0x84e); hTimeL = mk(0x85f); hTimeR = mk(0x850);
		hRate0 = mk(0x85d); hRate1L = mk(0x84f); hRate1R = mk(0x851); hRate2 = mk(0x85e);
	}

	// ================================================================================================================================
	// Update @10043850
	bool update(float dt) override
	{
		if(introOpen) return true;	// [V] 0x10043876: +0x50 -> nothing happens while the intro panel is up
		readyT -= dt;		// [V] +0x14 -= dt (unclamped), return true while it is > 0
		if(readyT > 0.0f) return true;
		if(dt > 0.1f) dt = 0.1f;	// [V] 0x100438b1
		if(ended){
			cl.updateAnim(dt);	// [V] 0x100438d4: Entity_UpdateAnim on the clown entity
			endT1 -= dt;
			if(endT1 < 0.0f){
				endT2 -= dt;
				if(endT2 <= 0.0f) running = !running;	// [V] 0x10043930: +0x30 = (+0x30 == 0)
			}
		}
		if(running){
			tleft -= dt;	// [V] 0x10043948
			if(!leftUsable){	// [V] 0x10043957: GetFlagF1 == 0 -> the pile reached the left pipe: the clown turns into sprite +0x2c and the game ends
				if(cl.spr != sprEnd){ cl.spr = sprEnd; ended = true; log("pile reached the left pipe: game over"); }
			}
			if(soundToppleReq){	// [V] 0x10043997: PipeEggMgr +2
				if(cl.spr != sprEnd && !soundTopple){
					mg::snd(0x163, 1.0f, 1); mg::snd(0x15d, 1.0f, 1);	// [V] 0x100439f3 / 0x10043a20
					soundTopple = true; soundToppleReq = false;
				}
			}
			if(tleft < 0.0f && !ended){	// [V] 0x10043a52
				ended = true; mg::snd(0x130, 1.0f, 1);
				log("time up, score %.0f", (float)score);
			}
			if(!ended){
				shakeTick();		// [V] 0x10043ca0
				if(bot) botStep(dt);
				clownUpdate(dt);	// [V] 0x1003ec70
				mgrUpdate(dt);		// [V] 0x100389f0
			}
			popupsUpdate(dt);	// [V] FUN_10044940
		}
		return running;
	}

	// ---- ShakeMeter ----------------------------------------------------------------------------------------------------------------
	void shakeTick()	// [V] LG23_ShakeMeter_Tick @10043ca0
	{
		unsigned cur = nowMs();
		if((int)(cur - shakeLast) >= 250){	// cmp 0xfa, jl
			shakeRate = (float)((double)shakeCount / 0.25);	// +4 = count / 0.25
			shakeCount = 0; shakeLast = cur;
		}
	}
	bool shakeKey(int code)	// [V] LG23_ShakeMeter_CountArrowKey @10044060: 0x94 +1, 0x93 -1
	{
		if(code == RM8MK_DOWN){ shakeCount++; return true; }
		if(code == RM8MK_UP){ shakeCount--; return true; }
		return false;
	}

	// ---- Clown ---------------------------------------------------------------------------------------------------------------------
	void startJump()	// [V] Clown_StartJump @1003eba0
	{
		cl.vy = fl((double)sc * -1000.0);
		cl.spr = sprJump;
		mg::spSetFrame(sprJump, 0); mg::spSetState(sprJump, 0);
	}
	void onHit()	// [V] Clown_OnHit @1003ea60
	{
		if(inj <= 0.0f){
			mg::snd(0x158, 1.0f, 3);	// loops = 3 as the DLL passes
			inj = 2.0f; grounded = false; lives--;
			cl.spr = sprHurt;
			mg::spSetFrame(sprHurt, 0); mg::spSetState(sprHurt, 0);
			log("clown hit (injured 2 s) y %.1f vy %.1f", cl.y, cl.vy);
		}
	}
	void clownUpdate(float dt)	// [V] Clown_Update @1003ec70
	{
		cl.updateAnim(dt);
		if(inj > 0.0f){	// hurt animation finished: restart it
			if(mg::spState(sprHurt) == 2){ mg::spSetFrame(sprHurt, 0); mg::spSetState(sprHurt, 0); }
		}
		if(inj <= 0.0f && cl.spr == sprHurt) cl.spr = sprIdle;
		if(cl.spr == sprJump){ if(mg::spState(sprJump) == 2) cl.spr = sprIdle; }
		inj -= dt;
		if(inj <= 0.0f && grounded && held){	// [V] 0x1003edb2: holding the button on the ground: keep rising for at most 0.25 s
			holdT += dt;
			startJump();
			if(0.25f <= holdT){ grounded = false; held = false; }
		}
		if(!grounded && groundD() > (double)cl.y){	// [V] 0x1003ee0d: in the air above the ground -> gravity, terminal velocity 1000 * s
			if((double)sc * 1000.0 < (double)cl.vy) cl.vy = fl((double)sc * 1000.0);
			else cl.vy = fl((double)dt * gravity + cl.vy);
		}
		if(cl.spr != sprFall) cl.y = fl((double)dt * cl.vy + cl.y);	// [V] 0x1003ee7c
	}
	void clownUpdateFalling(float dt)	// [V] Clown_UpdateFalling @1003f260 (only while the current sprite is 0x7e2)
	{
		if(cl.spr == sprFall){
			cl.vy = fl((double)gravity * 0.5 * dt + cl.vy);
			cl.y = fl((double)cl.vy * 0.5 * dt + cl.y);
			cl.ang = fl((double)dt * 600.0 + cl.ang);
		}
	}
	void clownDraw()	// [V] Clown_Draw @1003ef50
	{
		bool shaken = false; float oldX = 0, oldAng = 0;
		if(wobble && pile.size() > 1){	// +0xf8 && vec(+0xd8).size() > 1: add the pile wobble offset (x component twice, as the DLL does)
			shaken = true; oldX = cl.x; oldAng = cl.ang;
			cl.x = cl.x + wobX; cl.ang = cl.ang + wobX;
		}
		cl.draw();
		if(wobble && pile.size() > 1 && shaken){ cl.x = oldX; cl.ang = oldAng; }	// (the DLL restores the position from locals; y/z are unchanged)
	}


	// ---- test bot: simulates the jump physics for a few hold times and starts the one that ends in a stomp without an earlier body hit
	bool botSim(float hold, float step) const
	{
		float y = cl.y, vy = cl.vy, acc = 0; bool boost = true, ground = true;
		std::vector<Entity> eg = flying;
		for(int k = 1; k < 200; k++){
			if(k >= 2 && hold - (k - 1) * step <= 0){ boost = false; ground = false; }	// the bot releases the button (before this frame's clown update)
			if(boost){ acc += step; vy = fl((double)sc * -1000.0); if(0.25f <= acc){ boost = false; ground = false; } }	// held and grounded: StartJump, at most 0.25 s
			if(!ground && groundD() > (double)y){ if((double)sc * 1000.0 < (double)vy) vy = fl((double)sc * 1000.0); else vy = fl((double)step * gravity + vy); }
			y = fl((double)step * vy + y);
			for(Entity &e : eg) e.x = fl((double)step * e.vx + e.x);
			if(!pile.empty()){ if(((double)pile.back().y - (double)sc * 32.0) <= (double)sc * 48.0 + (double)y) return false; }
			else if(!ground && groundD() <= (double)y) return false;
			float footCy = fl((double)sc * 36.0 + y);
			for(const Entity &e : eg){
				int bl = ftol((double)cl.x - cl.ex), bt = ftol((double)y - cl.ey), br = ftol((double)cl.x + cl.ex), bb = ftol((double)y + cl.ey);
				int el = ftol((double)e.x - e.ex), et = ftol((double)e.y - e.ey), er = ftol((double)e.x + e.ex), eb = ftol((double)e.y + e.ey);
				if(!rectsOverlap(bl, bt, br, bb, el, et, er, eb)) continue;
				int fl2 = ftol((double)cl.x - footEx), ft = ftol((double)footCy - footEy), fr = ftol((double)cl.x + footEx), fb = ftol((double)footCy + footEy);
				int gl = ftol((double)e.x - eggHalfW), gt = ftol(((double)e.y - 16.0) - eggHalfH), gr = ftol((double)e.x + eggHalfW), gb = ftol(((double)e.y - 16.0) + eggHalfH);
				return rectsOverlap(fl2, ft, fr, fb, gl, gt, gr, gb) && vy >= 0.0f && (botMode != 2 || e.spr == sprEggR);
			}
		}
		return false;
	}
	void botStep(float dt)
	{
		if(botDown){ botHold -= dt; if(botHold <= 0){ mouseButton(MG_MOUSE_LUP); botDown = false; } return; }
		if(!grounded || inj > 0 || collapseT > 0 || flying.empty()) return;
		for(float h = 0.01f; h <= 0.2501f; h += 0.01f) if(botSim(h, dt)){ log("bot jumps, hold %.2f (y %.1f)", h, cl.y); botHold = h; botDown = true; mouseButton(MG_MOUSE_LDOWN); return; }
	}

	// ---- mouse -----------------------------------------------------------------------------------------------------------------------
	void mouseButton(int flags) override	// [V] LG23_Game_OnMouseButton @10044300
	{
		if(flags & MG_MOUSE_LDOWN){
			if(cl.vy <= 0.0f) held = true;
			if(inj <= 0.0f && grounded && held) mg::snd(0x159, 1.0f, 1);	// jump sound
		}else if(flags & MG_MOUSE_LUP){
			held = false; grounded = false;	// [V] +0x78 = 0, +0x79 = 0 (CollideEggs re-arms +0x79 while the clown stands on the ground)
		}
	}

	// ---- keyboard --------------------------------------------------------------------------------------------------------------------
	void keyDown(int code) override	// [V] LG23_Game_OnKeyDown @10043e90
	{
		if(shakeKey(code)) return;	// the arrow keys only count for the ShakeMeter
		switch(code){
		case ' ': case RM8MK_ENTER: if(introOpen) closeIntro(); break;	// 0x20 / 0xab: CloseIntroDialog when +0x50 == 1
		case RM8MK_ESC: running = false; break;	// 0x80: +0x30 = 0 (the game is over at once)
		default: break;		// a / d / s / w and the arrow keys: empty branches
		}
	}
	void keyUp(int) override {}	// [V] OnKeyUp @10044210: all branches empty
	void touchButtons(std::vector<MgTouchBtn> &out) override
	{
		// [A] the shaker (second human player, mode 2) uses the up/down arrow keys (0x93 / 0x94); the clown player clicks / taps the playfield
		if(mode != 2) return;
		out.push_back({862, 380, 140, 150, RM8MK_UP, MG_SHAPE_UP});
		out.push_back({862, 560, 140, 150, RM8MK_DOWN, MG_SHAPE_DOWN});
	}

	// ---- popups (+score) -------------------------------------------------------------------------------------------------------------
	void addPopup(int value, float x, float y, float z)	// [V] FUN_10044860
	{
		if(value == 0) return;
		Popup p; p.life = 1.0f; p.value = value; p.x = x; p.y = y; p.z = z; p.rise = 100.0f;
		popups.push_back(p);
	}
	void popupsUpdate(float dt)	// [V] FUN_10044940
	{
		for(size_t i = 0; i < popups.size();){
			Popup &p = popups[i];
			p.life = p.life - dt;
			p.y = fl(p.y - (double)dt * p.rise);
			if(p.life <= 0.0f) popups.erase(popups.begin() + i); else i++;
		}
	}
	void popupsDraw()	// [V] LG23_Game_DrawScorePopups @10044ab0
	{
		for(const Popup &p : popups){
			V3 pos(fl((double)p.x - 40.0 * sc), p.y, p.z);	// the +/- sign sits 40 * s left of the digits
			if(p.value > 0){ mg::spPos(hPlus, pos); mg::spDraw(hPlus); }
			else if(p.value < 0){ mg::spPos(hMinus, pos); mg::spDraw(hMinus); }
			drawNumber(digA, V3(p.x, p.y, p.z), p.value, ftol((double)sc * 22.0));
		}
	}
	void drawNumber(const int *set, V3 p, int value, int w)	// [V] LG23_DrawNumber @10044470: sprintf("%d"), one digit sprite per char
	{
		char buf[64]; snprintf(buf, sizeof buf, "%d", value);
		size_t len = strlen(buf);
		for(size_t i = 0; i < len; i++){
			int d = buf[i] - '0';
			if(d < 0 || d > 9 || !set[d]) continue;
			V3 q = p;
			if(len == 1) q.x = p.x;
			else if(len == 2) q.x = fl(((double)p.x - (double)w * 0.5) + (double)((int)i * w));
			else if(len == 3) q.x = fl(((double)p.x - (double)w * 1.0) + (double)((int)i * w));
			mg::spPos(set[d], q); mg::spDraw(set[d]);
		}
	}

	// ---- PipeEggMgr ------------------------------------------------------------------------------------------------------------------
	Entity makeEgg(int spr, float x, float y, float vx) const	// [V] FireLeftPipe @10039fe0 / FireRightPipe @1003a200
	{
		Entity e; e.spr = spr; e.x = x; e.y = y; e.z = 0; e.ang = 0;
		e.sx = sc; e.sy = sc; e.sz = sc;	// mgr +0xc..+0x14
		e.vx = vx; e.vy = 0; e.vz = 0;
		e.ex = fl((double)sc * 30.0); e.ey = fl((double)sc * 30.0);	// +0x34 / +0x38
		return e;
	}
	void fireLeft()	// [V] 0x10039fe0
	{
		mg::spColor(sprPipeL, whiteColor); tPipeL = 4.0f;
		Entity e = makeEgg(sprEggL, fl((double)sc * 64.0), fl((double)Hf - (492.0 - (double)pipeOff) * (double)sc), fl((double)sc * 450.0));
		flying.push_back(e);
		mg::snd(0x156, 1.0f, 1); mg::snd(0x15a, 0.5f, 1);
		log("left pipe fires (egg y %.1f)", e.y);
	}
	void fireRight()	// [V] 0x1003a200
	{
		tPipeR = 4.0f; mg::spColor(sprPipeR, whiteColor);
		Entity e = makeEgg(sprEggR, fl((double)Wf - (double)sc * 64.0), fl((double)Hf - (300.0 - (double)pipeOff) * (double)sc), fl((double)sc * -350.0));
		flying.push_back(e);
		mg::snd(0x156, 1.0f, 1); mg::snd(0x15a, 0.5f, 1);
		log("right pipe fires (egg y %.1f)", e.y);
	}
	void updatePipes(float dt)	// [V] LG23_PipeEggMgr_UpdatePipes @1003a420
	{
		tPipeL -= dt; tPipeR -= dt;
		float topY = 0;
		if(pipeLBusy){
			mg::spUpdate(sprPipeL, dt);
			if(mg::spState(sprPipeL) == 2){ mg::spSetState(sprPipeL, 0); mg::spSetFrame(sprPipeL, 0); pipeLBusy = false; }
		}
		if(pipeRBusy){
			mg::spUpdate(sprPipeR, dt);
			if(mg::spState(sprPipeR) == 2){ mg::spSetState(sprPipeR, 0); mg::spSetFrame(sprPipeR, 0); pipeRBusy = false; }
		}
		fxL.updateAnim(dt); fxR.updateAnim(dt);
		if(!pile.empty()){
			topY = fl((double)pile.back().y - (double)sc * 32.0);	// top of the pile - 32 s
			if((double)topY <= (double)sc * 32.0 + ((double)Hf - (double)sc * 300.0)) rightUsable = false;	// the pile reached the right pipe mouth
			if((double)topY <= (double)Hf - (double)sc * 492.0) leftUsable = false;	// ... the left pipe mouth
		}
		if(leftUsable){
			if(tPipeL <= 0.5f) mg::spColor(sprPipeL, warnColor);	// warning colour for the last 0.5 s
			if(tPipeL <= 0.0f){
				if(!pipeLBusy){	// start the muzzle animation and the pipe animation
					mg::spSetFrame(sprFxL, 0); mg::spSetState(sprFxL, 0);
					fxL.y = fl((double)Hf - (492.0 - (double)pipeOff) * (double)sc);
					mg::spSetState(sprPipeL, 0);
					pipeLBusy = true;
				}
				if(mg::spFrame(sprPipeL) == 2) fireLeft();	// the egg leaves when the pipe animation reaches frame 2
			}
		}
		if(rightUsable){
			if(tPipeR <= 0.5f) mg::spColor(sprPipeR, warnColor);
			if(tPipeR <= 0.0f){
				if(!pipeRBusy){
					mg::spSetFrame(sprFxR, 0); mg::spSetState(sprFxR, 0);
					fxR.y = fl((double)Hf - (300.0 - (double)pipeOff) * (double)sc);
					mg::spSetState(sprPipeR, 0);
					pipeRBusy = true;
				}
				if(mg::spFrame(sprPipeR) == 2) fireRight();
			}
		}
	}
	void updateEggPhysics(float dt)	// [V] LG23_PipeEggMgr_UpdateEggPhysics @1003ab00: the pile scatters during the collapse
	{
		if(!eggVelInit){	// DAT_100af644: first call hands every pile egg a random velocity
			for(Entity &e : pile){
				e.vx = fl(((double)(mg::rand15() % 400) - 400.0) * (double)sc);
				e.vy = fl(((double)(mg::rand15() % 200) - 200.0) * (double)sc);
				eggVelInit = true;
			}
		}
		for(Entity &e : pile){
			e.x = fl((double)e.vx * 0.5 * dt + e.x);
			e.vy = fl((double)gravity * 0.5 * dt + e.vy);
			e.y = fl((double)e.vy * 1.0 * dt + e.y);
			if(e.vx <= 0.0f) e.ang = fl(e.ang - (double)dt * 600.0); else e.ang = fl((double)dt * 600.0 + e.ang);
			if(groundD() <= (double)e.y){ e.vy = fl(-((double)e.vy * 0.95)); }	// bounce (vx = vx)
		}
	}
	void mgrUpdate(float dt)	// [V] LG23_PipeEggMgr_Update @100389f0
	{
		phase = fl((double)fl((double)dt * 50.0) * shakeRate + phase);	// DAT_100af640 += dt * 50 * rate
		if(360.0f < phase) phase = 0.0f;
		if(mode == 2) pipeOff = fl(sin((double)phase) * 3.0 + (double)fl(((double)shakeK2 * shakeRate) / shakeK1));	// +0x11c (only with a human shaker)
		else pipeOff = 0;
		if(collapseT > 0.0f){	// the pile collapse runs for 1 / 1.5 s, then the game ends
			collapseT -= dt;
			updateEggPhysics(dt);
			clownUpdateFalling(dt);
			if(collapseT <= 0.0f){ ended = true; log("pile collapse finished: game over"); }
		}
		if(collapseT <= 0.0f && pile.size() >= 2){
			const Entity &top = pile[pile.size() - 1], &prev = pile[pile.size() - 2];
			double dx = fabs((double)top.x - (double)prev.x);
			if(dx <= 37.0 * (double)sc){	// the two topmost eggs are stacked: check for an egg far from the centre -> wobble warning
				for(const Entity &e : pile){
					if(!wobble){
						double dc = fabs((double)e.x - (double)Wf / 2.0);
						if(36.0 * (double)sc <= dc) wobble = true;
					}
				}
			}else{	// the pile is unstable: collapse
				collapseT = pile.size() < 5 ? 1.0f : 1.5f;	// cmp eax, 4; ja
				if(cl.spr != sprEnd && !soundTopple) soundToppleReq = true;	// [V] 0x10038cf3: clown sprite != +0x2c && +1 == 0 -> +2 = 1
				cl.vy = fl((double)sc * -250.0);	// +0x58
				cl.spr = sprFall;			// clown +0x34 = +0x30
				log("pile collapses (%.0f eggs)", (float)pile.size());
			}
		}
		for(size_t i = 0; i < flying.size();){	// [V] 0x10038de6: flying eggs
			flying[i].updateAnim(dt);
			flying[i].x = fl((double)dt * flying[i].vx + flying[i].x);
			if((double)flying[i].x < (double)sc * -32.0 || (double)sc * 32.0 + (double)Wf < (double)flying[i].x) flying.erase(flying.begin() + i);
			else i++;
		}
		updatePipes(dt);
		if(collapseT <= 0.0f){	// [V] 0x10038f24
			collideEggs();
			for(Entity &e : falling) e.y = fl((double)dt * e.vy + e.y);	// stomped eggs fall
		}
	}
	// 0x10039270: collisions of the flying eggs with the clown, landing of the falling eggs, the clown on the ground / on the pile
	void collideEggs()
	{
		Entity &c = cl;
		float footCx = c.x;
		float footCy = fl((double)sc * 36.0 + c.y);
		int fl_ = ftol((double)footCx - footEx), ft = ftol((double)footCy - footEy), fr = ftol((double)footCx + footEx), fb = ftol((double)footCy + footEy);
		for(size_t i = 0; i < flying.size(); i++){
			Entity &e = flying[i];
			// Entity_OverlapsClown @10047260: the entity boxes (ext at +0x34/+0x38) of the clown and the egg
			int bl = ftol((double)c.x - c.ex), bt = ftol((double)c.y - c.ey), br = ftol((double)c.x + c.ex), bb = ftol((double)c.y + c.ey);
			int el = ftol((double)e.x - e.ex), et = ftol((double)e.y - e.ey), er = ftol((double)e.x + e.ex), eb = ftol((double)e.y + e.ey);
			if(!rectsOverlap(bl, bt, br, bb, el, et, er, eb)) continue;
			e.valid = false;
			int gl = ftol((double)e.x - eggHalfW), gt = ftol(((double)e.y - 16.0) - eggHalfH), gr = ftol((double)e.x + eggHalfW), gb = ftol(((double)e.y - 16.0) + eggHalfH);
			if(rectsOverlap(fl_, ft, fr, fb, gl, gt, gr, gb) && 0.0f <= c.vy && inj <= 0.0f){	// stomp: falling, not injured, feet on the egg
				c.vy = fl((double)c.vy - 1000.0 * (double)sc);	// bounce
				e.vx = 0; e.vy = fl(1000.0 * (double)sc);	// the egg falls straight down
				falling.push_back(e);
				Entity stomped = e;
				flying.erase(flying.begin() + i);
				mg::snd(0x157, 1.0f, 1);
				// [V] 0x1003952e: the DLL tests sprite id and position through the vector iterator AFTER the erase: it now designates the element that
				// slid into the slot (the next flying egg) or, if the stomped egg was the last one, the stale copy of the stomped egg itself
				const Entity &r = i < flying.size() ? flying[i] : stomped;
				if(r.spr == sprEggL){ addPopup(20, r.x, fl((double)r.y - (double)sc * 0.0), r.z); score += 20; log("stomp left egg +20 (score %.0f)", (float)score); }
				if(r.spr == sprEggR){ addPopup(10, r.x, fl((double)r.y - (double)sc * 0.0), r.z); score += 10; log("stomp right egg +10 (score %.0f)", (float)score); }
				break;
			}
			onHit();
		}
		for(size_t i = 0; i < falling.size(); i++){
			Entity &e = falling[i];
			if(e.vy == 0.0f) continue;
			bool landed = false;
			if(!pile.empty()){
				const Entity &top = pile.back();
				float dx = fl((double)e.x - top.x), dy = fl((double)e.y - top.y);
				dx = fl(pow((double)dx, 2.0)); dy = fl(pow((double)dy, 2.0));	// LG23_PowF @10039ec0 (float store)
				float d = fl(sqrt((double)(dx + dy)));	// FUN_10039f30 = sqrt
				if((double)d <= 62.0 * (double)sc){	// within 62 s of the top egg: lands on the pile
					e.vy = 0;
					pile.push_back(e);
					falling.erase(falling.begin() + i);
					mg::snd(0x15b, 1.0f, 1);	// one of the two (left / right) tests always matches
					landed = true;
					log("egg lands on the pile (%.0f)", (float)pile.size());
				}
			}
			if(!landed && (double)Hf - 91.0 * (double)sc <= (double)e.y){	// ground: y = H - 91 s
				e.vy = 0; e.y = fl((double)Hf - 91.0 * (double)sc);
				pile.push_back(e);
				mg::snd(0x15b, 1.0f, 1);
				falling.erase(falling.begin() + i);
				landed = true;
				log("egg lands on the ground (%.0f)", (float)pile.size());
			}
			if(landed) break;
		}
		if(pile.empty()){	// clown on the ground
			if(groundD() <= (double)c.y){
				c.y = fl(groundD()); c.vy = 0; grounded = true; holdT = 0;
			}
		}else{	// clown on top of the pile
			const Entity &top = pile.back();
			if(((double)top.y - (double)sc * 32.0) <= (double)sc * 48.0 + (double)c.y){
				c.y = fl((double)top.y - (double)sc * 80.0); c.vy = 0; grounded = true; holdT = 0;
			}
		}
	}
	void mgrDraw()	// [V] LG23_PipeEggMgr_Draw @10038250
	{
		fxL.draw(); fxR.draw();	// muzzle flashes (state 2 = not drawn until a pipe fires)
		for(const Entity &e : flying) e.draw();
		for(const Entity &e : falling) e.draw();
		int idx = 0;
		for(Entity &e : pile){
			bool tmp = false; float oX = 0, oY = 0, oZ = 0, oAng = 0;
			if(wobble && !ended){	// pile wobble warning
				if(!soundWob){ mg::snd(0x164, 1.0f, 1); soundWob = true; }
				unsigned t = nowMs();	// [A] timeGetTime
				unsigned phaseDeg = (t << 1) % 0x168;
				float a = fl(((double)idx * 3.14159) / 4.0 + ((double)phaseDeg / 180.0) * 3.14159);
				tmp = true; oX = e.x; oY = e.y; oZ = e.z; oAng = e.ang;
				float k = fl((double)idx * 0.4 + 0.2);
				wobX = fl(cos((double)a) * (double)k * (double)sc);
				e.x = fl((double)e.x + wobX);
				float k2 = fl((double)idx * 0.2 + 0.0);
				wobY = fl(sin((double)a) * (double)k2 * (double)sc);
				e.y = fl((double)e.y + wobY);
				e.ang = fl((double)wobX / (double)sc + e.ang);
				idx++;
			}
			e.draw();
			if(tmp){ e.x = oX; e.y = oY; e.z = oZ; e.ang = oAng; }
		}
		// the pipes (bodies) on top of the eggs
		mg::spPos(sprPipeL, V3(fl((double)sc * 48.0), fl((double)Hf - (492.0 - (double)pipeOff) * (double)sc), 0)); mg::spDraw(sprPipeL);
		mg::spPos(sprPipeR, V3(fl((double)Wf - (double)sc * 48.0), fl((double)Hf - (300.0 - (double)pipeOff) * (double)sc), 0)); mg::spDraw(sprPipeR);
	}

	// ---- drawing -----------------------------------------------------------------------------------------------------------------------
	void draw() override	// [V] LG23_Game_Draw @10043e30
	{
		// DrawBackground @100436e0 -> HudUi_DrawFrame @1003f7b0
		mg::spDraw(hBgTL); mg::spDraw(hBgTR); mg::spDraw(hBgBL); mg::spDraw(hBgBR);
		mg::spPos(hFront, V3(Wf / 2.0f, fl((double)Hf - (double)sc * 37.0), 0)); mg::spDraw(hFront);
		// DrawScene @100437d0
		mgrDraw(); clownDraw(); popupsDraw(); drawHud();
	}
	// width of a banner as the DLL reads it (+0x8c rect width)
	float bannerW(int h){ float w = 0, hh = 0; mg::spSize(h, w, hh); return w; }
	void drawHud()	// [V] LG23_Game_DrawHud @100423a0
	{
		int fsecs = ftol(floor((double)tleft));
		if(tleft > 0.0f) secs = ftol(floor((double)tleft) + 1.0);
		if(tleft <= 0.0f) secs = 0;
		if(tleft == 60.0f) secs = 0x3c;
		if(tleft < 3.0f && tleft > 0.0f && secs != lastSecs){	// last 3 seconds: tick
			mg::snd(0x12f, 1.0f, 1); lastSecs = secs;
		}
		float tnx = fl(((double)Wf * 8.0) / 10.0 + (double)sc * 92.0), tny = fl(((double)Hf + (double)Hf) / 7.0 - (double)sc * 10.0);
		V3 panel(fl(((double)Wf * 8.0) / 10.0 - (double)sc * 38.0 + (double)sc * 92.0), fl((((double)Hf + (double)Hf) / 7.0 + (double)sc * 4.0) - (double)sc * 10.0), 0);
		if(fsecs <= 10){	// (signed compare: negative values too)
			mg::spPos(hPanelB, panel); mg::spDraw(hPanelB);
			drawNumber(digC, V3(tnx, tny, 0), secs, ftol((double)sc * 27.0));
		}else{
			mg::spPos(hPanelA, panel); mg::spDraw(hPanelA);
			drawNumber(digB, V3(tnx, tny, 0), secs, ftol((double)sc * 27.0));
		}
		float snx = fl(((double)Wf * 8.0) / 10.0 - (double)sc * 77.0 + (double)sc * 92.0);
		drawNumber(digA, V3(snx, tny, 0), score, ftol((double)sc * 22.0));	// score
		// Ready / Go
		if(readyT < 2.0f && readyT > 0.0f){
			int cur = 0;
			if(!(readyT > 2.0f) && readyT > 1.0f) cur = hReady;
			else if(readyT < 1.0f && readyT > 0.0f) cur = hGo;
			V3 mid(Wf / 2.0f, Hf / 2.0f, 0);
			if(cur){ mg::spPos(cur, mid); mg::spDraw(cur); }
			if(lastReadySec != ftol(readyT)){
				int sid = cur == hReady ? 0x12d : cur == hGo ? 0x165 : -1;
				if(sid >= 0) mg::snd(sid, 1.0f, 1);
				lastReadySec = ftol(readyT);
			}
		}
		float cc = sc;
		auto half = [&](int left, int right){	// two half banners left / right of the centre
			float wl = bannerW(left);
			mg::spPos(left, V3(fl(((double)Wf / 2.0 - (double)cc * 12.5) - (double)wl / 2.0), Hf / 2.0f, 0)); mg::spDraw(left);
			float wr = bannerW(right);
			mg::spPos(right, V3(fl((double)wr / 2.0 + (double)cc * 12.5 + (double)Wf / 2.0), Hf / 2.0f, 0)); mg::spDraw(right);
		};
		if(ended && 0.0f <= endT1){	// first second after the end: "time up" (time <= 0) / the other banner (pile ended the game)
			if(tleft <= 0.0f) half(hTimeL, hTimeR); else half(hEndL, hEndR);
		}
		if(ended && 0.0f <= endT2 && endT1 < 0.0f){	// second second: the rating banner
			if(score < 0x28) rating = 0; else if(score < 0x6e) rating = 1; else rating = 2;	// [V] <40 -> 0, <110 -> 1, else 2
			ratingKnown = true;
			if(rating == 0){
				mg::spPos(hRate0, V3(Wf / 2.0f, Hf / 2.0f, 0)); mg::spDraw(hRate0);
				if(!ratingSound){ mg::snd(0x135, 1.0f, 1); ratingSound = true; }
			}
			if(rating == 1){
				half(hRate1L, hRate1R);
				if(!ratingSound){ mg::snd(0x134, 1.0f, 1); ratingSound = true; }
			}
			if(rating == 2){
				mg::spPos(hRate2, V3(Wf / 2.0f, Hf / 2.0f, 0)); mg::spDraw(hRate2);
				if(!ratingSound){ mg::snd(0x15c, 1.0f, 1); ratingSound = true; }
			}
		}
	}

	// ---- result -------------------------------------------------------------------------------------------------------------------
	void getResult(Rm8MiniResult &o) override	// [V] LG23_Game_GetResult @10043d80
	{
		if(!ratingKnown){	// [A] the DLL would leave +0x6c uninitialised when the game is left with ESC before the rating banner
			if(score < 0x28) rating = 0; else if(score < 0x6e) rating = 1; else rating = 2;
		}
		o.count = 1;
		o.p[0].score = score;	// [2]
		o.p[0].rating = rating;	// [4]
		o.p[0].flagB = score;	// [5]
		mg::showCursor(true);	// +0x130(1)
	}
};
}	// namespace

MgGame *mgNew23(){ return new G23; }
