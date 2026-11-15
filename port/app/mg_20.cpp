// Minigame 20 (ltlgame_20.dll): 打地鼠 / whack-a-mole. Transliteration of the DLL on the minigame host services (mg_core.h).
// Tags: [V] = verified in the decompile / disassembly (DLL address), [H] = hypothesis, [A] = port adaptation.
//
// Rules (all [V], see the functions named in the comments):
//   - 3x3 holes, 40 s (+ 2 s ready/go before, 1 s + 1 s result banners after); the pointer (mouse / touch) is the hammer, a click hits the mole
//     under the hammer head (3 probe points + sprite hit test); player 2 (digits 1-9 / numeric keypad) puts the "disruptor" (type 6, -10) into a hole.
//   - scores per hit by type: 0:-2 1:+2 2:+3 3:-3 4:-5 5:+5 6:-10, the total is clamped to 0..999 after every hit.
//   - rating banner: score <= 40 -> 0, 41..100 -> 1, >= 101 -> 2.
//
// Key codes the DLL compares (LG20_Field_OnNumpad @0x10002cf0): digits '1'..'9' (0x31..0x39) and the numeric keypad codes 0x99..0xa1 =
// KP1..KP9 (the code of KPn is 0x98 + n; 7 8 9 = top row, 4 5 6 = middle, 1 2 3 = bottom row, i.e. the keypad's own layout).
// The port's key translation (rm8MiniKeyFromSdl in mg_standalone.cpp) must therefore map SDLK_KP_1..SDLK_KP_9 to 0x99..0xa1.
//
// Test hooks ([A], environment variables, only for the --shot path; they change nothing in normal play):
//   RM8_MG20_PREDRAW=1    call draw() after every update. The DLL positions the mole sprites inside Draw (LG20_Field_DrawMoles @0x10002a50) and the click test
//                         uses those positions, but the --shot path only steps Update, so scripted clicks (RM8_MINI_SCRIPT) need this to hit anything.
//                         Draw outside a frame draws nothing (mg::spDraw) but runs the Draw side effects (sprite positions, count-down sounds, rating).
//   RM8_MG20_TESTSCORE=n  start with score n (to test the rating banners, the 0..999 clamp and the reward thresholds without playing 40 s).
//   A click at (cx, cy) puts the hammer head's middle probe on (cx - 11 sx, cy + 20 sy); a hole centre (hx, hy) is hit by click (hx + 14, hy - 26).
#include "mg_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

namespace {

using mg::V3;

// [V] LG20_Field_InitTables @0x10003380: score table (field +0x190..0x1a8) by mole type 0..6
const int kScore[7] = {-2, 2, 3, -3, -5, 5, -10};
// [V] DAT_10041040: (A, B) per phase, life = (rand % (A - B) + A) * 0.1, the remainder is taken with a negative divisor = rand % (B - A)
const int kLifeA[3] = {8, 6, 5}, kLifeB[3] = {19, 16, 11};

struct Slot {	// [V] field +0x1bc + hole * 0x18
	int id = -1;		// +0: sprite handle shown in the hole, -1 = empty
	float x = 0, y = 0, z = 0;	// +4/+8/+0xc: hole centre (InitTables)
	int type = 0;		// +0x10: mole type 0..5, 6 = disruptor, -1 = free (the heap block is not initialised: [H] 0 until the first SpawnAndExpire frees it)
	bool hittable = true;	// +0x14 (byte): can be hit. Not initialised by the DLL, SpawnAndExpire sets it to 1 for every free slot in its first frame
};

struct Popup { float x, y, z, ttl; int score; };	// [V] 0x14 bytes (field +0x1ac vector), pushed by OnClick @0x10005840

struct G20 : MgGame {
	// ---- game object (0x3c4 bytes) ----
	std::vector<Rm8MiniPlayer> players;
	bool run = true;			// +0x2b8
	float sx = 1, sy = 1, sz = 1;		// +0x2c0/+0x2c4/+0x2c8
	int state = 0;				// +0x2cc: 0 intro, 1 ready, 2 play, 3 time-up banner, 4 rating banner
	int score = 0, rating = 0;		// +0x2d0, +0x2d4
	int cursor = 0;				// +0x2d8 current cursor sprite
	int digW[10] = {}, digR[10] = {}, digS[10] = {};	// +0x2ec / +0x314 / +0x33c (digit 0 = index 0): white, red, third set (the last is unused by the DLL's Draw)
	int sprReady = 0, sprGo = 0;		// +0x36c / +0x370 (0x84b / 0x84c)
	int clockW = 0, clockR = 0;		// +0x364 / +0x368 (0x83f / 0x84a)
	int sprBanner[4] = {};			// +0x374, +0x378 (0x84d / 0x84e, created but never drawn), +0x37c, +0x380 (0x85f / 0x850)
	int sprR0 = 0, sprR1a = 0, sprR1b = 0, sprR2 = 0;	// +0x384 (0x85d), +0x388 (0x851), +0x38c (0x84f), +0x390 (0x85e)
	int curNormal = 0, curDown = 0;		// +0x394 / +0x398 (0x6ba / 0x6bb)
	float tGame = 40.0f, tReady = 2.0f, tResult = 1.0f;	// +0x39c / +0x3a0 / +0x3a4
	int cacheReady = 2, cacheCount = 6;	// +0x3a8 / +0x3ac: last whole second (sound de-duplication)
	bool resultPending = true;		// +0x3c0: result sound / rating not yet decided
	// background object (0x1c bytes, DAT_100430a4)
	int bg0 = 0, bg1 = 0;			// +0x14 / +0x18 (0x6a5 / 0x6a6)
	// ---- mole field object (0x3a8 bytes, DAT_100430a8) ----
	int mole[6][9] = {};			// +0x14 + (type * 9 + hole) * 4: 0x6b4 + type
	int disruptor = 0;			// +0x108 (0x6be)
	int hitSpr[7] = {};			// +0xec + type * 4: 0x6bf + type
	int tree0 = 0, tree1 = 0;		// +0x10c / +0x110 (0x6aa / 0x6ab)
	int rowL[3] = {}, rowR[3] = {};		// +0x114/+0x118, +0x11c/+0x120, +0x124/+0x128 (0x6ae / 0x6af per row)
	int strip0 = 0, strip1 = 0;		// +0x12c / +0x130 (0x6b1 / 0x6b3)
	int hitFx = 0;				// +0x134 (0x6b2)
	int popA[10] = {}, popB[10] = {};	// +0x138 (0x83e, 0x835..0x83d) for score <= 0, +0x160 (0x849, 0x840..0x848) for score > 0
	int popMinus = 0, popPlus = 0;		// +0x188 (0x860), +0x18c (0x852)
	std::vector<Popup> popups;		// +0x1ac
	Slot slot[9];
	float fTimer = 40.0f;			// +0x294
	float life[9] = {}, elapsed[9] = {}, delay[9] = {}, hold[9] = {}, top[9] = {};	// +0x298 / +0x2bc / +0x2e0 / +0x304 / +0x358
	int phase = 0;				// +0x328
	int fScore = 0;				// +0x32c
	float rx = 0, ry = 0, rw = 0, rh = 0;	// +0x330: playfield rectangle
	int hitSnd[8] = {0x143, 0x144, 0x145, 0x146, 0x147, 0x148, 0x149, 0x14a};	// +0x37c

	// [A] touch pad: the digit labels are drawn only while the overlay asked for the buttons in the previous frame
	bool predraw = getenv("RM8_MG20_PREDRAW") != nullptr;
	int padSeen = -100;
	int frameNo = 0;

	// ================================================================================================================
	// Init @0x100025a0
	bool init(const std::vector<Rm8MiniPlayer> &pl) override
	{
		players = pl;			// [V] the parameter block is copied to this+8 (count, players[i] 0x54 bytes)
		run = true;
		mg::music(0x17);		// [V] 0x10002610..: this+0x3b0 = 0x17, host +0x108(0x17, 0) (host +0x110 is an empty slot)
		float w = (float)mg::W, h = (float)mg::H;
		sz = 1.0f;
		sx = w * 0.00125f;		// [V] 0x10002678: * 0.00125 (width), * 0.0016666667 (height)
		sy = h * 0.0016666667f;
		// the lg_intro.ui panel with string 0x1b29 (6953), button 2 -> StartGame
		mg::introLoad("lg__lg_intro", 0x1b29, [this]{ startGame(); });	// [V] 0x100026f0 / button callback 0x10002530
		hudCreate();			// [V] LG20_Hud_CreateSprites @0x10001140
		bgCreate();			// [V] LG20_Bg_Create @0x10005e80
		fieldCreate();			// [V] LG20_Field_CreateSprites @0x10002e70
		fieldInitTables();		// [V] LG20_Field_InitTables @0x10003380
		state = 0;
		tGame = 40.0f; tReady = 2.0f; tResult = 1.0f;	// [V] 0x100027cd..
		cacheCount = 6; cacheReady = 2;
		score = 0; rating = 0;
		if(const char *e = getenv("RM8_MG20_TESTSCORE")) score = fScore = atoi(e);	// [A] test hook
		mg::showCursor(true);		// [V] 0x10002830: host +0x130(1)
		return true;
	}

	// LG20_StartGame @0x100017b0
	void startGame()
	{
		mg::snd(0x131, 1.0f, 1);
		mg::showCursor(false);
		mg::introHide();
		state = 1;
		mg::snd(0x12d, 2.0f, 1);
	}

	int mk(int art, const V3 &scale)
	{
		int h = mg::spNew(art);
		mg::spScale(h, scale);
		return h;
	}

	// LG20_Hud_CreateSprites @0x10001140
	void hudCreate()
	{
		const V3 sc(sx, sy, sz);
		const float W = (float)mg::W, H = (float)mg::H;
		digW[0] = mk(0x83e, sc);
		for(int i = 1; i < 10; i++) digW[i] = mk(0x834 + i, sc);
		digR[0] = mk(0x849, sc);
		for(int i = 1; i < 10; i++) digR[i] = mk(0x83f + i, sc);
		digS[0] = mk(0x85c, sc);
		for(int i = 1; i < 10; i++) digS[i] = mk(0x852 + i, sc);
		sprReady = mg::spNew(0x84b);
		sprGo = mg::spNew(0x84c);
		mg::spScale(sprReady, sc); mg::spScale(sprGo, sc);
		V3 mid(sx * 400.0f, sy * 300.0f, 0);
		mg::spPos(sprReady, mid); mg::spPos(sprGo, mid);
		clockW = mk(0x83f, sc);
		clockR = mk(0x84a, sc);
		V3 pc(sx * 54.0f + W * 0.8f, H * 0.2857143f - sy * 6.0f, 0);
		mg::spPos(clockW, pc); mg::spPos(clockR, pc);
		float bw, bh;
		// 0x84d / 0x84e (+0x374 / +0x378): positioned, never drawn by Draw
		sprBanner[0] = mk(0x84d, sc); mg::spSize(sprBanner[0], bw, bh);
		mg::spPos(sprBanner[0], V3(sx * 387.5f - bw * 0.5f, sy * 300.0f, 0));
		sprBanner[1] = mk(0x84e, sc); mg::spSize(sprBanner[1], bw, bh);
		mg::spPos(sprBanner[1], V3(bw * 0.5f + sx * 412.5f, sy * 310.0f, 0));
		// 0x85f / 0x850 (+0x37c / +0x380): the two halves of the time-up banner (state 3)
		sprBanner[2] = mk(0x85f, sc); mg::spSize(sprBanner[2], bw, bh);
		mg::spPos(sprBanner[2], V3(sx * 387.5f - bw * 0.5f, sy * 300.0f, 0));
		sprBanner[3] = mk(0x850, sc); mg::spSize(sprBanner[3], bw, bh);
		mg::spPos(sprBanner[3], V3(bw * 0.5f + sx * 412.5f, sy * 310.0f, 0));
		// rating banners (state 4): 0x85d alone (score <= 40), 0x84f + 0x851 (41..100), 0x85e alone (>= 101)
		sprR0 = mk(0x85d, sc);
		mg::spPos(sprR0, V3(sx * 400.0f, sy * 300.0f, 0));
		sprR1b = mk(0x84f, sc); mg::spSize(sprR1b, bw, bh);
		mg::spPos(sprR1b, V3(sx * 387.5f - bw * 0.5f, sy * 310.0f, 0));
		sprR1a = mk(0x851, sc); mg::spSize(sprR1a, bw, bh);
		mg::spPos(sprR1a, V3(bw * 0.5f + sx * 412.5f, sy * 310.0f, 0));
		sprR2 = mk(0x85e, sc);
		mg::spPos(sprR2, V3(sx * 400.0f, sy * 300.0f, 0));
		curNormal = mg::spNew(0x6ba);
		curDown = mg::spNew(0x6bb);
		mg::spScale(curNormal, sc); mg::spScale(curDown, sc);
		cursor = curNormal;
	}

	// LG20_Bg_Create @0x10005e80: two tiles side by side along the top (0x6a5 left, 0x6a6 right)
	void bgCreate()
	{
		const V3 sc(sx, sy, sz);
		float w1, h1, w2, h2;
		bg0 = mg::spNew(0x6a5);
		mg::spScale(bg0, sc);
		mg::spSize(bg0, w1, h1);
		float x1 = w1 * sx * 0.5f, y1 = h1 * sy * 0.5f;
		mg::spPos(bg0, V3(x1, y1, 0));
		bg1 = mg::spNew(0x6a6);
		mg::spSize(bg1, w2, h2);
		mg::spScale(bg1, sc);
		mg::spPos(bg1, V3(w2 * sx * 0.5f + x1 + x1, y1, 0));	// [V] 0x10005f5d: w2 * sx * 0.5 + 2 * x1, same y as the first tile
	}

	// LG20_Field_CreateSprites @0x10002e70
	void fieldCreate()
	{
		const V3 sc(sx, sy, sz);
		for(int t = 0; t < 6; t++) for(int h = 0; h < 9; h++) mole[t][h] = mk(0x6b4 + t, sc);
		disruptor = mk(0x6be, sc);
		for(int t = 0; t < 7; t++) hitSpr[t] = mk(0x6bf + t, sc);
		tree0 = mk(0x6aa, sc);
		tree1 = mk(0x6ab, sc);
		float w, h;
		mg::spSize(tree0, w, h);
		mg::spPos(tree0, V3(sx * 740.0f, sy * 286.0f, 0));
		mg::spPos(tree1, V3(sx * 61.0f, sy * 401.0f, 0));
		// three rows of foreground (the dirt rims in front of the moles): 0x6ae left half, 0x6af right half
		const double rowY[3] = {249.0, 365.0, 481.0};	// [V] 0x10039350 / 0x10039348 (float 365) / 0x10039344 (float 481)
		float xl = 0;
		for(int r = 0; r < 3; r++){
			rowL[r] = mk(0x6ae, sc);
			rowR[r] = mk(0x6af, sc);
			float wl, hl, wr, hr;
			mg::spSize(rowL[r], wl, hl);
			mg::spSize(rowR[r], wr, hr);
			if(r == 0) xl = wl * sx * 0.5f;
			float y = (float)(sy * rowY[r]);
			mg::spPos(rowL[r], V3(xl, y, 0));
			mg::spPos(rowR[r], V3(wr * sx * 0.5f + xl + xl, y, 0));
		}
		strip0 = mk(0x6b1, sc);
		strip1 = mk(0x6b3, sc);
		hitFx = mk(0x6b2, sc);
		mg::spSetFrame(hitFx, 0);
		mg::spSetState(hitFx, 2);	// [V] 0x10003236: finished = not drawn until the first hit
		const V3 small(sx * 0.6f, sy * 0.6f, sz * 0.7f);	// [V] 0x10003251..
		popA[0] = mk(0x83e, small);
		for(int i = 1; i < 10; i++) popA[i] = mk(0x834 + i, small);
		popB[0] = mk(0x849, small);
		for(int i = 1; i < 10; i++) popB[i] = mk(0x83f + i, small);
		popPlus = mk(0x852, sc);	// [V] 0x10003332: the plus sign keeps the full scale
		popMinus = mk(0x860, small);
	}

	// LG20_Field_InitTables @0x10003380
	void fieldInitTables()
	{
		rw = sx * 432.0f; rh = sy * 348.0f;
		rx = sx * 185.0f; ry = sy * 118.0f;
		const float colX[3] = {rw * 0.16666667f + rx, rw * 0.5f + rx, rw * 0.8333333f + rx};
		const float rowYs[3] = {rh * 0.16666667f + ry, rh * 0.5f + ry, rh * 0.8333333f + ry};
		for(int i = 0; i < 9; i++){
			slot[i].id = -1; slot[i].x = colX[i % 3]; slot[i].y = rowYs[i / 3]; slot[i].z = 0;
			life[i] = elapsed[i] = delay[i] = hold[i] = top[i] = 0;
		}
		fTimer = 40.0f; phase = 0; fScore = 0;
	}

	// ================================================================================================================
	bool update(float dt) override
	{
		bool r = updateDll(dt);
		if(predraw) draw();	// [A] test hook, see the header
		return r;
	}

	// Update @0x10001c40
	bool updateDll(float dt)
	{
		if(!run) return run;
		switch(state){
		case 1:
			tReady -= dt;
			if(tReady <= 0) state = 2;
			break;
		case 2:
			tGame -= dt;
			if(tGame > 0){ fieldUpdate(dt); return run; }
			tGame = 0;
			mg::snd(0x130, 2.0f, 1);
			state = 3;
			return run;
		case 3:
			tResult -= dt;
			if(tResult <= 0){ state = 4; tResult = 1.0f; return run; }
			break;
		case 4:
			tResult -= dt;
			if(tResult <= 0){ run = false; return false; }
			break;
		}
		return run;
	}

	// LG20_Field_Update @0x10004d20
	void fieldUpdate(float dt)
	{
		fTimer -= dt;
		for(int i = 0; i < 9; i++){
			if(slot[i].id == -1) continue;
			delay[i] -= dt;
			if(delay[i] <= 0){
				if(hold[i] <= 0){ life[i] -= dt; elapsed[i] += dt; }	// alive: lifetime runs, time shown grows
				else hold[i] -= dt;					// just hit: frozen for 0.3 s
			}
		}
		int n = (int)fTimer;		// [V] _ftol truncation, 0x100050fc
		if(n <= 13) phase = 2;		// [V] 0x10005109..: > 27 s phase 0 (initial), 13 < n <= 27 phase 1, n <= 13 phase 2
		else if(n <= 27) phase = 1;
		for(int t = 0; t < 6; t++) for(int h = 0; h < 9; h++) mg::spUpdate(mole[t][h], dt);
		mg::spUpdate(disruptor, dt);
		mg::spUpdate(hitFx, dt);
		mg::spUpdate(tree0, dt);
		mg::spUpdate(tree1, dt);
		spawnAndExpire();
		// LG20_Field_UpdatePopups @0x10004c20
		for(size_t i = 0; i < popups.size();){
			popups[i].ttl -= dt;
			if(popups[i].ttl <= 0){ popups.erase(popups.begin() + i); continue; }
			popups[i].y = popups[i].y - dt * sy * 30.0f;	// [V] floats up at 30 * sy px/s
			i++;
		}
	}

	// LG20_Field_SpawnAndExpire @0x10003720
	void spawnAndExpire()
	{
		int shown = 0, count = 0, last = 0;
		for(int i = 0; i < 9; i++){
			if(slot[i].type == -1) continue;
			if(life[i] <= 0){	// expired (or the never initialised slots of the first frame)
				slot[i].id = -1; slot[i].type = -1; slot[i].hittable = true;
				elapsed[i] = 0; life[i] = 0;
			}else{
				if(delay[i] <= 0) shown = 1;
				count++; last = i;
			}
		}
		if(shown == 0){
			if(count == 0) shown = 2;
			else delay[last] = 0;	// nobody is out of a hole yet: the last spawned one comes out now
		}
		int want = 0;
		if(phase == 0) want = mg::rand15() % 3 + 1;
		else if(phase == 1) want = mg::rand15() % 4 + 2;
		else want = mg::rand15() % 5 + 3;
		int need = want - count;
		while(need > 0){
			int type = mg::rand15() % 6;
			int hole;
			do { hole = mg::rand15() % 9; } while(slot[hole].id != -1);
			slot[hole].type = type;
			slot[hole].id = mole[type][hole];
			int a = kLifeA[phase], b = kLifeB[phase];
			int r = mg::rand15();
			life[hole] = (float)((double)(r % (b - a) + a) * 0.1);	// [V] 0x10003886: idiv by (A - B), a negative divisor, remainder >= 0
			if(shown == 2){ delay[hole] = 0; shown = -1; }
			else{
				int d = mg::rand15();
				delay[hole] = (float)((double)(d % 230) * (double)0.01f);
			}
			need--;
		}
	}

	// ================================================================================================================
	// OnClick @0x10005840 (the click position is the cursor position)
	void onClick(float mx, float my)
	{
		// [V] three probe points relative to the cursor (hammer head)
		float x1 = mx - sx * 22.0f, y1 = my + sy * 21.0f;
		float x2 = mx - sx * 11.0f, y2 = my + sy * 20.0f;
		float x3 = mx + sx * 3.0f, y3 = my + sy * 17.0f;
		// [V] all probes inside the playfield rectangle (LG20_PointInRect @0x10003900, inclusive)
		auto inRect = [&](float x, float y){ return !(x < rx || rx + rw < x || y < ry || ry + rh < y); };
		if(!inRect(x1, y1) || !inRect(x2, y2) || !inRect(x3, y3)) return;
		auto hit3 = [&](int spr){ return mg::spHit(spr, x1, y1) || mg::spHit(spr, x2, y2) || mg::spHit(spr, x3, y3); };
		for(int i = 0; i < 9; i++){
			// [V] the front rim sprites of the hole's row cover the mole: a probe on an opaque pixel of them skips this hole
			int row = i / 3;
			if(hit3(rowL[row]) || hit3(rowR[row])) continue;
			if(y1 < top[i]) continue;	// [V] the probe must be below the mole's top edge
			if(slot[i].id == -1 || !slot[i].hittable) continue;
			if(!hit3(slot[i].id)) continue;
			// ---- hit ----
			int type = slot[i].type;
			int r = mg::rand15();
			if(type == 6) mg::snd(hitSnd[5 + r % 3], 2.0f, 1);	// [V] 0x148..0x14a
			else mg::snd(hitSnd[r % 5], 2.0f, 1);			// [V] 0x143..0x147
			slot[i].id = hitSpr[type];
			hold[i] = 0.3f;						// [V] 0x3e99999a
			if(elapsed[i] <= 0.2){ life[i] = elapsed[i]; elapsed[i] = 1.2f; }	// [V] 0x10005cdf: still rising: it sinks from where it is
			else if(life[i] > 0.2) life[i] = 0.2f;		// [V] fully up: goes down in 0.2 s
			mg::spPos(hitFx, V3(x2, y2, 0));
			mg::spSetFrame(hitFx, 0);
			mg::spSetState(hitFx, 0);
			slot[i].hittable = false;
			popups.push_back({x2, y2, 0, 2.0f, kScore[type]});
			fScore += kScore[type];
			if(fScore < 0) fScore = 0;
			else if(fScore > 999) fScore = 999;			// [V] 0x3e7
			score = fScore;
			return;
		}
	}

	// OnNumpad @0x10002cf0: player 2 puts a disruptor into a hole (only while none is on the field)
	bool onNumpad(int code)
	{
		for(int i = 0; i < 9; i++)
			if(slot[i].id == disruptor || slot[i].id == hitSpr[6]) return false;
		int hole;
		if(code >= 0x31 && code <= 0x39) hole = kHoleOfDigit[code - 0x31];	// [V] byte table @0x10002df4 / jump table @0x10002dcc
		else if(code >= 0x99 && code <= 0xa1) hole = kHoleOfDigit[code - 0x99];
		else return false;
		slot[hole].id = disruptor;
		slot[hole].type = 6;
		slot[hole].hittable = true;
		if(delay[hole] > 0) delay[hole] = 0;
		life[hole] = 2.0f;
		return true;
	}
	// digit n (1..9) -> hole: 7 8 9 top row (holes 0..2), 4 5 6 middle, 1 2 3 bottom (numeric keypad layout)
	static constexpr int kHoleOfDigit[9] = {6, 7, 8, 3, 4, 5, 0, 1, 2};

	void keyDown(int code) override
	{
		if(state == 2) onNumpad(code);						// [V] 0x10001da0
		else if(state == 0 && (code == 0xab || code == ' ')) startGame();
	}

	void mouseButton(int flags) override
	{
		float fx, fy; mg::mouse(fx, fy);
		float x = (float)(int)fx, y = (float)(int)fy;				// [V] host +0x128 returns ints
		if(flags & MG_MOUSE_LDOWN){							// [V] 0x10001de0 (0x40)
			if(state != 0) mg::snd(0x142, 2.0f, 1);
			cursor = curDown;
			if(state == 2) onClick(x, y);
		}else if(flags & MG_MOUSE_LUP){						// 0x400
			cursor = curNormal;
		}
	}

	// ================================================================================================================
	// Draw @0x10001ea0
	void draw() override
	{
		frameNo++;
		mg::spDraw(bg0); mg::spDraw(bg1);		// LG20_Bg_Draw @0x10005e20
		drawMoles();
		drawPopups();
		const float W = (float)mg::W, H = (float)mg::H;
		const float A = W * 0.8f, B = H * 0.2857143f;
		float tx = sx * 92.0f + A, ty = B - sy * 10.0f;
		bool red = tGame <= 6.0f;
		mg::spDraw(red ? clockR : clockW);
		hudNumber(red ? digR : digW, tx, ty, (int)tGame, sx * 27.0f);
		hudNumber(digS, sx * 15.0f + A, B - sy * 10.0f, score, sx * 22.0f);
		switch(state){
		case 1: {
			mg::spDraw(tReady > 1.0f ? sprReady : sprGo);
			int w = (int)tReady;
			if(cacheReady != w){
				if(cacheReady == 1) mg::snd(0x165, 2.0f, 1);
				cacheReady = (int)tReady;
			}
			break; }
		case 2: {
			int n = (int)tGame;
			if(n <= 5){
				if(n >= 0){
					mg::spPos(digR[n], V3(sx * 400.0f, sy * 40.0f, 0));
					mg::spDraw(digR[n]);
				}
				if(cacheCount != n){
					mg::snd(0x12f, 2.0f, 1);
					cacheCount = n;
				}
			}
			break; }
		case 3:
			mg::spDraw(sprBanner[2]);
			mg::spDraw(sprBanner[3]);
			decideRating(true);
			break;
		case 4:
			if(score <= 40) mg::spDraw(sprR0);
			else if(score <= 100){ mg::spDraw(sprR1b); mg::spDraw(sprR1a); }
			else mg::spDraw(sprR2);
			break;
		}
		if(state != 0){
			float fx, fy; mg::mouse(fx, fy);
			mg::spPos(cursor, V3((float)(int)fx, (float)(int)fy, 0));
			mg::spDraw(cursor);
			mg::spDraw(hitFx);	// LG20_Field_DrawHitEffect @0x10002cd0
		}
		drawPadLabels();
	}

	// the rating and its sound are decided by the first Draw in state 3 [V]; GetResult also calls it with no sound [A] (the shot path may skip Draw)
	void decideRating(bool sound)
	{
		if(!resultPending) return;
		if(score <= 40){ if(sound) mg::snd(0x135, 2.0f, 1); rating = 0; }
		else if(score <= 100){ if(sound) mg::snd(0x134, 2.0f, 1); rating = 1; }
		else { if(sound) mg::snd(0x15c, 2.0f, 1); rating = 2; }
		resultPending = false;
	}

	// LG20_Hud_DrawNumber @0x100018d0: digits centred on x (1 digit: x, 2: x - s/2 + i*s, 3: x - s + i*s)
	void hudNumber(const int *set, float x, float y, int n, float sp)
	{
		char buf[16]; snprintf(buf, sizeof buf, "%d", n);
		int len = (int)strlen(buf);
		for(int i = 0; i < len; i++){
			char c = buf[i];
			if(c < '0' || c > '9') continue;
			int d = c - '0';
			if(!set[d]) continue;
			float px = x;
			if(len == 2) px = (x - sp * 0.5f) + (float)i * sp;
			else if(len == 3) px = (float)i * sp + (x - sp);
			mg::spPos(set[d], V3(px, y, 0));
			mg::spDraw(set[d]);
		}
	}

	// LG20_Field_DrawMoles @0x10002a50
	void drawMoles()
	{
		const double kRise = 1.6666666666666665, kHit = 2.5;
		float stripY = (float)(sy * 219.0);
		mg::spPos(strip0, V3(sx * 255.0f, stripY, 0)); mg::spDraw(strip0);
		mg::spPos(strip1, V3(sx * 575.0f, stripY, 0)); mg::spDraw(strip1);
		for(int i = 0; i < 9; i++){
			Slot &s = slot[i];
			if(s.id != -1 && delay[i] <= 0){
				float py = s.y;
				if(elapsed[i] > 0.2){	// up: only the way down at the end of life is animated
					if(life[i] <= 0.2) py = (float)((0.2 - (double)life[i]) * (double)rh * (s.hittable ? kRise : kHit) + (double)s.y);
				}else py = (float)((0.2 - (double)elapsed[i]) * (double)rh * (s.hittable ? kRise : kHit) + (double)s.y);	// rising for 0.2 s
				top[i] = py - rh * 0.16666667f;		// +0x358: the top of the mole, used by OnClick
				mg::spPos(s.id, V3(s.x, py, s.z));
				mg::spDraw(s.id);
			}
			if(i == 2){
				mg::spDraw(rowL[0]); mg::spDraw(rowR[0]);
				stripY = (float)(sy * 335.0);
			}else if(i == 5){
				mg::spDraw(tree0);
				mg::spDraw(rowL[1]); mg::spDraw(rowR[1]);
				stripY = (float)(sy * 451.0);
			}else if(i == 8){
				mg::spDraw(tree1);
				mg::spDraw(rowL[2]); mg::spDraw(rowR[2]);
				continue;
			}else continue;
			mg::spPos(strip0, V3(sx * 255.0f, stripY, 0)); mg::spDraw(strip0);
			mg::spPos(strip1, V3(sx * 575.0f, stripY, 0)); mg::spDraw(strip1);
		}
	}

	// LG20_Field_DrawPopups @0x100042b0 + LG20_Hud_DrawSignedNumber @0x10003950
	void drawPopups()
	{
		for(size_t i = 0; i < popups.size(); i++){
			const Popup &p = popups[i];
			mg::spPos(popPlus, V3(p.x, p.y, p.z));	// [V] 0x10004303 (positioned, then repositioned by the number drawing)
			float spacing = sx * 10.0f;
			uint32_t a = (uint32_t)(int)(p.ttl * 127.5f) & 0xff;	// [V] alpha = ftol(ttl * 127.5) (byte)
			uint32_t argb = (a << 24) | 0x00ffffff;
			signedNumber(p.score > 0 ? popB : popA, V3(spacing + p.x, p.y, 0), p.score, spacing, argb);
		}
	}
	void signedNumber(const int *set, V3 pos, int n, float spacing, uint32_t argb)
	{
		char buf[16]; snprintf(buf, sizeof buf, n > 0 ? "+%d" : "%d", n);	// [V] formats at 0x10039388 ("+%d") / 0x10039234 ("%d")
		for(int i = 0; buf[i]; i++){
			char c = buf[i];
			int spr = 0;
			if(c >= '0' && c <= '9'){
				spr = set[c - '0'];
				if(!spr) continue;
				pos.x += spacing;	// [V] the x advances before every digit, not before the signs
			}else if(c == '+') spr = popPlus;
			else if(c == '-') spr = popMinus;
			else continue;
			mg::spPos(spr, pos);
			mg::spColor(spr, argb);
			mg::spDraw(spr);
		}
	}

	// ================================================================================================================
	// GetResult @0x10002860: out[0] = count, per player 5 ints {seat, score (first player only), 0, rating, copy of score}
	void getResult(Rm8MiniResult &o) override
	{
		decideRating(false);
		o.count = (int)players.size();
		if(o.count > 8) o.count = 8;
		for(int i = 0; i < o.count; i++){
			o.p[i].seat = players[i].seat;
			o.p[i].score = i == 0 ? score : 0;
			o.p[i].flagA = 0;
			o.p[i].rating = rating;
			o.p[i].flagB = o.p[i].score;
		}
	}

	// ================================================================================================================
	// [A] touch: a 3x3 pad for the second player = the numeric keypad layout (7 8 9 / 4 5 6 / 1 2 3, key codes '7'.. '1' = 0x37.. 0x31 as OnNumpad
	// compares), at the lower right edge (x 796..1020, y 536..760) outside the hole field (x 237..790) and below the score/time box. Only while
	// the game is in the playing state (the DLL ignores keys in the other states); the digit labels are drawn small in the corner of each button.
	static constexpr float kPadX = 796, kPadY = 536, kPadCell = 72, kPadGap = 4;
	int padLabel[9] = {};
	void touchButtons(std::vector<MgTouchBtn> &out) override
	{
		if(state != 2) return;
		padSeen = frameNo;
		static const int keys[9] = {'7', '8', '9', '4', '5', '6', '1', '2', '3'};
		for(int i = 0; i < 9; i++){
			float x = kPadX + (i % 3) * (kPadCell + kPadGap), y = kPadY + (i / 3) * (kPadCell + kPadGap);
			out.push_back({x, y, kPadCell, kPadCell, keys[i], MG_SHAPE_CIRCLE});
		}
	}
	void drawPadLabels()
	{
		if(state != 2 || frameNo - padSeen > 1) return;
		static const int digit[9] = {7, 8, 9, 4, 5, 6, 1, 2, 3};
		for(int i = 0; i < 9; i++){
			if(!padLabel[i]) padLabel[i] = mk(0x834 + digit[i], V3(sx * 0.35f, sy * 0.35f, sz));
			float x = kPadX + (i % 3) * (kPadCell + kPadGap), y = kPadY + (i / 3) * (kPadCell + kPadGap);
			mg::spPos(padLabel[i], V3(x + 10, y + 15, 0));
			mg::spDraw(padLabel[i]);
		}
	}
};

}	// namespace

MgGame *mgNew20(){ return new G20; }
