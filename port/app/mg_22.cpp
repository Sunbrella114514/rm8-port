// Minigame 22 (ltlgame_22.dll): Blastomere / 分裂球 - a cannon shoots balls that split.  Transliteration of the DLL on the mg:: host services.
// Tags: [V] verified in the decompile / disassembly (addresses are DLL addresses), [H] hypothesis, [A] port adaptation.
//
// Map of the DLL (names from out/ghidra_lg_named/ltlgame_22.dll):  Game_Init @100025a0, Update @10001dd0, Draw @10001f70 (slot 3 = jmp @10002a50),
//   KeyDown @10003b10, KeyUp @10001f40 (-> 100043b0), GetResult @100034b0, StartGame @100018b0, LoadHudSprites @100011c0, DrawNumber @100019d0,
//   Manager_Update @10009690, Manager_InitGame @100091e0, LoadManagerSprites @10003f40, FireBullets @10008390, SpawnBall @10008780,
//   SpawnItem @10008890, SplitBall @10008a60, ApplyItemEffect @10008c90, Collide @10009400, RectOverlap @10003c40 / @10003d90, Obj_Integrate @1000a360,
//   ball vtable slot0 @10003d10, base object slot0 @1000a4e0, powerup timers @10005900, popups @10006660 / @10005b70, list D @100069f0,
//   DrawAll @10006070, background @10009af0 / @10009e40 / @1000a070 / @1000a220 / @1000a270, cheat "peng" @10004510.
// The scale factors of this DLL are width * 0.00125 and height * (1/600) [V] 10002669 / 1000267f (constants 0x1003e300 / 0x1003e2fc); at the
// port's 1024x768 both are 1.28, the same as the width/1024 * 1.28 the guide mentions.  The arithmetic is kept as it is.
//
// Test hooks of this file (environment, all optional; they only touch the game through the normal input / spawn paths):
//   RM8_MINI_BOT=1            a bot plays: it moves the cannon below the lowest ball and presses space whenever the cannon may fire
//   RM8_MINI_FORCE_POWERUP=n  (0..7) every split makes its first (left) new object a powerup ball of type n (instead of the random roll)
//   RM8_MINI_FORCE_AFTER=s    the force above only applies from game second s on (default 0)
//   RM8_MINI_BALLS=n         n extra balls at the start (to test the 150 ball limit of the 'all balls split' powerup)
//   RM8_MINI_SCORE=n          the score starts at n (to test the 999 cap / the reward thresholds)
//   RM8_MINI_CHEAT=1          the cheat "peng" is switched on at the start (same effect as typing 0x8c p e n g Enter on the intro)
//   RM8_MINI_TIME=s           the game time (the DLL's 50 s) is s seconds (to reach the time-up path quickly)
//   RM8_MG22_LOG=1            log events (splits, powerups, floor hits, state changes) to stderr
// [A] float arithmetic is done in single precision (the DLL's x87 code rounds intermediate results differently, which can change a
//     truncated int by one in rare cases).
#include "mg_core.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

namespace {
using mg::V3;

inline float fbits(uint32_t u){ float f; memcpy(&f, &u, 4); return f; }
inline int ftol(float f){ return (int)f; }	// [V] FUN_10029d40 = __ftol (truncates toward zero)

// constants read from the DLL's data section
const float K_SX = fbits(0x3aa3d70a);	// [V] 0x1003e300 = 0.00125   (scale x = width * 0.00125)
const float K_SY = fbits(0x3ada740e);	// [V] 0x1003e2fc = 1/600     (scale y = height * (1/600))
const float K_08 = fbits(0x3f4ccccd);	// [V] 0x1003e250 = 0.8
const float K_2857 = fbits(0x3e924925);	// [V] 0x1003e240 = 0.2857143
const float K_RAND = fbits(0x38000100);	// [V] 0x1003e378 = 3.051851e-05  (rand() * this)
const float K_003 = fbits(0x3cf5c28f);	// [V] 0x1003e374 = 0.03
const float K_013 = fbits(0x3e051eb8);	// [V] 0x1003e370 = 0.13
const float K_THIRD = fbits(0x3eaaaaab);	// [V] 0x1003e37c = 0.33333334

// ---- the 0x48 / 0x4c byte objects (LG22_Obj_Ctor @1000a300: scale 1, state -1, rest 0)
struct Obj {
	int spr = 0;			// +4 sprite handle (shared between all objects of one kind)
	V3 pos;				// +8
	V3 scale{1, 1, 1};		// +0x14
	V3 vel;				// +0x24
	V3 acc;				// +0x30
	float hw = 0, hh = 0;		// +0x3c / +0x40 half extents (rectangle collision and the bounds clamp)
	int state = -1;			// +0x44 last bound hit: 0 top, 1 bottom, 2 left, 3 right, -1 none
	int type = 0;			// +0x48 (items: powerup type 0..7; list D: effect slot)
	bool bounce = false;		// vtable slot0 = 10003d10 (balls, items) instead of 1000a4e0 (base: bullets, cannon)
};
struct Popup { V3 pos; float life; };	// 16 bytes: position + life (10006660)
struct EffSlot { int spr = 0; int count = 0; bool active = false; };	// 12 bytes at mgr+0xf0 + 12 * i
struct Cloud { int spr = 0; float x = 0, y = 0, z = 0, vx = 0, vy = 0; };	// 6 words at bg+0x44 + 0x18 * i

struct G22 : MgGame {
	std::vector<Rm8MiniPlayer> players;
	// ---- game object (this): 0x3e0 bytes
	float fW = 1024, fH = 768;
	float s1 = 1, sx = 1, sy = 1, sz = 1;	// +0x2bc, +0x2c0, +0x2c4, +0x2c8
	bool running = true;			// +0x2b8
	int state = 0;				// +0x2cc: 0 intro, 1 countdown, 2 play, 3 end banner, 4 result banner
	Rm8Ui *ui = nullptr;			// +0x2d0
	int hudG[10] = {}, hudR[10] = {}, hudS[10] = {};	// +0x2d4 green / +0x2fc red timer digits, +0x324 score digits (index = digit)
	int clockG = 0, clockR = 0;		// +0x34c / +0x350
	int bReady = 0, bGo = 0;		// +0x354 / +0x358 (Ready / Go)
	int bA1 = 0, bA2 = 0;			// +0x35c / +0x360 (all balls gone)
	int bT1 = 0, bT2 = 0;			// +0x364 / +0x368 (time up)
	int bR0 = 0, bR1a = 0, bR1b = 0, bR2 = 0;	// +0x36c (<=50), +0x374 / +0x370 (<=150), +0x378 (>150) result banners
	int sprUnused = 0;			// +0x37c (0x6ba, loaded and scaled, never drawn)
	float sndX = 512, sndY = 384;		// +0x384 / +0x388 (sound position, ignored by mg::snd)
	float floorY = 0;			// +0x390
	int score = 0, tier = 0;		// +0x394, +0x398
	float tLeft = 50.0f, tCount = 2.0f, tEnd = 1.0f;	// +0x39c, +0x3a0, +0x3a4
	int last3a8 = 2, last3ac = 6;		// +0x3a8, +0x3ac sound de-dupe of the countdown numbers
	bool playTimeUp = true, playResult = true;	// +0x3b0, +0x3b1
	float bR = 0, bB = 0, bL = 0, bT = 0;	// +0x3b4 right, +0x3b8 bottom, +0x3c0 left, +0x3c4 top (bounds for Obj_Integrate)
	bool cheatTyping = false, cheatOn = false;	// +0x3cc, +0x3cd
	std::vector<char> cheatBuf;		// +0x3d0
	Rm8Ui *cheatUi = nullptr; int cheatId = 0;	// [A] the "Cheating......" text (host +0xf8) as a UI label

	// ---- background object (DAT_10049064, ctor 1000a180)
	int bg[4] = {};				// +0x14..+0x20: 0x76d, 0x784, 0x785, 0x786
	int cloudSet1[3] = {}, cloudSet2[3] = {};	// +0x24.. (0x77e..0x780) / +0x30.. (0x781..0x783)
	Cloud cloud[3];
	float scroll = 0, scrollSpeed = 0;	// +0x3c, +0x40

	// ---- ball manager (DAT_10049068, ctor 10007fa0)
	std::vector<Obj> A, B, C, D;		// balls, powerup balls, bullets, vanishing balls (mgr+0x18 / +0x28 / +0x38 / +0x48)
	std::vector<Popup> popups;		// +0x58
	Obj cannon;				// +0x64
	int cannonSpr[3] = {}, bulletSpr[3] = {}, ballSpr = 0, itemSpr[8] = {};	// +0xdc.. / +0xd0.. / +0xac / +0xb0..
	EffSlot eff[10];			// +0xf0
	int plusSpr = 0, oneSpr = 0;		// +0xe8 / +0xec
	float baseInterval = 0.4f, interval = 0.4f, cooldown = 0;	// +0x168, +0x174, +0x178
	int mscore = 0;				// +0x16c
	bool keyL = false, keyR = false;	// +0x170, +0x171
	int bulletSpeed = 0, ballG = 0, splitVx = 0, splitVy = 0, playerSpeed = 0;	// +0x17c, +0x180, +0x184, +0x188, +0x18c
	int cannonType = 1;			// +0x190 (byte)
	float tFast = 0, tSlow = 0, tBallSlow = 0;	// +0x194, +0x198, +0x19c
	float autoFireEn = 0, autoFire = 0;	// +0x1ac (cheat), +0x1b0 (space held)
	// test hooks
	bool bot = false; int forcePowerup = -1; float forceAfter = 0; bool logOn = false;
#define LOG22(...) do{ if(logOn){ fprintf(stderr, "[mg22 t=%.2f] ", mg::now()); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } }while(0)

	// =========================================================================================================================
	// Obj_Integrate @1000a360 [V]: uniform acceleration, then clamp into the game's bounds and remember which bound was hit
	void integrate(Obj &o, float dt)
	{
		float dvx = dt * o.acc.x;
		float nx = dt * o.vel.x + dt * dvx * 0.5f + o.pos.x; o.pos.x = nx;
		float dvy = dt * o.acc.y;
		float ny = dt * o.vel.y + dt * dvy * 0.5f + o.pos.y; o.pos.y = ny;
		float dvz = dt * o.acc.z;
		o.pos.z = dt * o.vel.z + dvz * dt * 0.5f + o.pos.z;
		if(nx - o.hw < bL){ o.state = 2; o.pos.x = bL + o.hw; }	// [V] 1000a3f9: x - hw < left -> 2
		else if(nx + o.hw > bR){ o.state = 3; o.pos.x = bR - o.hw; }	// [V] 1000a428: x + hw > right -> 3
		if(ny - o.hh < bT){ o.state = 0; o.pos.y = bT + o.hh; }		// [V] 1000a449: y - hh < top -> 0
		else if(ny + o.hh > bB){ o.state = 1; o.pos.y = bB - o.hh; }	// [V] 1000a469: y + hh > bottom -> 1
		o.vel.x += dvx; o.vel.y += dvy; o.vel.z += dvz;			// [V] 1000a485
	}
	// slot0 of the base object @1000a4e0 (sprite clock + integrate); balls / items (@10003d10) then bounce
	void objUpdate(Obj &o, float dt)
	{
		mg::spUpdate(o.spr, dt);
		integrate(o, dt);
		if(o.bounce){
			if(o.state == 2 || o.state == 3){ o.state = -1; o.vel.x = -o.vel.x; }	// [V] 10003d41
			else if(o.state == 0){ o.state = -1; o.vel.y = -o.vel.y; }		// [V] 10003d2e (state 1 stays: the ball reached the floor)
		}
	}
	void objDraw(const Obj &o){ mg::spPos(o.spr, o.pos); mg::spDraw(o.spr); }	// [V] 1000a4b0

	// RectOverlap @10003c40 / @10003d90 [V]: both rectangles (x-hw, y-hh, x+hw, y+hh) are truncated to ints, true if a corner of one lies in the other
	static bool overlap(const Obj &a, const Obj &b)
	{
		int p1 = ftol(a.pos.x - a.hw), p2 = ftol(a.pos.y - a.hh), p3 = ftol(a.hw + a.pos.x), p4 = ftol(a.hh + a.pos.y);
		int p5 = ftol(b.pos.x - b.hw), p6 = ftol(b.pos.y - b.hh), p7 = ftol(b.hw + b.pos.x), p8 = ftol(b.hh + b.pos.y);
		auto in = [](int x, int y, int l, int t, int r, int bt){ return x >= l && x <= r && y >= t && y <= bt; };
		// [V] the nested conditions of 10003d90 were checked against this corner test with 200000 random rectangle pairs (identical results)
		return in(p1, p2, p5, p6, p7, p8) || in(p3, p2, p5, p6, p7, p8) || in(p1, p4, p5, p6, p7, p8) || in(p3, p4, p5, p6, p7, p8)
		    || in(p5, p6, p1, p2, p3, p4) || in(p7, p6, p1, p2, p3, p4) || in(p5, p8, p1, p2, p3, p4) || in(p7, p8, p1, p2, p3, p4);
	}

	// =========================================================================================================================
	// ---- spawning
	Obj makeBase(int spr, float x, float y, float z)	// common part of SpawnBall @10008780 / SpawnItem @10008890
	{
		Obj o; o.spr = spr; o.pos = {x, y, z};
		o.scale = {sx, sy, sz};			// [V] +0x14.. = mgr+8.. (sx, sy, 1)
		o.hw = sx * 16.0f; o.hh = sy * 16.0f;	// [V] 100087a4 / 100087bd (x16)
		o.acc.y = (float)ballG;			// [V] +0x34 = (float)mgr+0x180
		o.bounce = true;
		return o;
	}
	void applyDir(Obj &o, int dir)
	{
		if(dir == 1){ o.vel.x = (float)(-splitVx); o.vel.y = (float)(-splitVy); }	// [V] 1000882d: left, upwards
		else if(dir == 2){ o.vel.x = (float)splitVx; o.vel.y = (float)(-splitVy); }	// [V] 10008811: right, upwards
	}
	void spawnBall(float x, float y, float z, int dir)	// [V] 10008780
	{
		Obj o = makeBase(ballSpr, x, y, z); applyDir(o, dir); A.push_back(o);
	}
	void spawnItem(float x, float y, float z, int dir, int type)	// [V] 10008890: sprite by type (mgr+0xb0 + 4 * type)
	{
		Obj o = makeBase(itemSpr[type & 7], x, y, z); applyDir(o, dir); o.type = type; B.push_back(o);
	}
	// SplitBall @10008a60 [V]: each of the two new objects is rolled independently
	//   r = rand() * 3.051851e-5;  r <= 0.03: type 6 / 7 (rand&1), a type 6 becomes a ball when the ball list already holds > 150 entries (0x96, unsigned compare)
	//   0.03 < r <= 0.13: type rand()%6;  otherwise a ball.  New object 1 goes left (dir 1), new object 2 right (dir 2).
	// returns the item type or -1 for a ball
	int rollNew()
	{
		float r = (float)mg::rand15() * K_RAND;
		if(r > K_003){					// [V] 10008a7a fcom 0.03 / 10008baa fcomp 0.13
			if(r <= K_013) return mg::rand15() % 6;
			return -1;
		}
		int t = 6 + (mg::rand15() & 1);			// [V] 10008a8d: (rand & 0x80000001) + 6
		if(t == 6 && !A.empty() && A.size() > 0x96) return -1;
		return t;
	}
	void splitBall(float x, float y, float z)
	{
		int t1 = rollNew();
		if(forcePowerup >= 0 && mg::now() >= forceAfter){ t1 = forcePowerup; if(t1 == 6 && !A.empty() && A.size() > 0x96) t1 = -1; }	// test hook (keeps the 150 ball limit of type 6)
		LOG22("split at %.0f,%.0f: first %d (balls %d)", x, y, t1, (int)A.size());
		if(t1 >= 0){					// [V] 10008be4: item first (dir 1), then a ball (dir 2) WITHOUT a second roll
			spawnItem(x, y, z, 1, t1);
			spawnBall(x, y, z, 2);
			return;
		}
		spawnBall(x, y, z, 1);
		int t2 = rollNew();
		if(t2 >= 0) spawnItem(x, y, z, 2, t2); else spawnBall(x, y, z, 2);
	}

	// FireBullets @10008390 [V]
	void fireBullets()
	{
		LOG22("fire: type %d at x %.0f, bullets %d, interval %.2f", cannonType, cannon.pos.x, (int)C.size(), interval);
		cooldown = interval;				// [V] mgr+0x178 = mgr+0x174
		float hw8 = sx * 8.0f, hh8 = sy * 8.0f;
		float by = (cannon.pos.y - cannon.hh) - hh8;	// [V] (cannon y - cannon half height 0xa4) - 8 * sy
		auto mk = [&](int spr, float x, float vx, float vy){
			Obj b; b.spr = spr; b.pos = {x, by, cannon.pos.z}; b.scale = {sx, sy, sz}; b.hw = hw8; b.hh = hh8; b.vel = {vx, vy, 0};
			return b;
		};
		float v = (float)(-bulletSpeed);		// [V] +0x28 = (float)(int)-mgr+0x17c
		if(cannonType == 0){
			C.push_back(mk(bulletSpr[0], cannon.pos.x, 0, v));
		}else if(cannonType == 1){
			C.push_back(mk(bulletSpr[1], (cannon.pos.x - cannon.hw) + hw8, 0, v));	// [V] left / right edge of the cannon
			C.push_back(mk(bulletSpr[1], (cannon.pos.x + cannon.hw) - hw8, 0, v));
		}else if(cannonType == 2){
			C.push_back(mk(bulletSpr[2], cannon.pos.x, 0, v));
			C.push_back(mk(bulletSpr[2], (cannon.pos.x - cannon.hw) + hw8, v * 0.6f, v * 0.8f));	// [V] left: vx = -0.6 speed, vy = -0.8 speed
			C.push_back(mk(bulletSpr[2], (cannon.pos.x + cannon.hw) - hw8, (float)bulletSpeed * 0.6f, v * 0.8f));
		}else return;					// [V] 10008705: any other type fires nothing and plays no sound
		mg::snd(0x13e, 2.0f, 1);			// [V] 10008705
	}

	// powerup speed helper shared by case 0 / 1 of ApplyItemEffect @10008c90 and the timers @10005900 [V]
	void setInterval(float newInt, float speedScale)
	{
		cooldown = (newInt - interval) + cooldown;	// [V] 10008ce3..10008cef
		interval = newInt;
		playerSpeed = ftol(sx * speedScale);		// [V] 10008cfb..: ftol(sx * 600 / 150 / 300)
		if(cannon.vel.x > 0) cannon.vel.x = (float)playerSpeed;
		else if(cannon.vel.x < 0) cannon.vel.x = (float)(-playerSpeed);
	}
	void scaleVel(std::vector<Obj> &l, float k){ for(Obj &o : l){ o.vel.x *= k; o.vel.y *= k; o.acc.y *= k; } }	// +0x24, +0x28, +0x34

	// ApplyItemEffect @10008c90 [V]
	void applyItem(int type)
	{
		LOG22("powerup %d (balls %d, items %d, vanishing %d, interval %.2f, speed %d, timers fast %.2f slow %.2f ballslow %.2f)", type, (int)A.size(), (int)B.size(), (int)D.size(), interval, playerSpeed, tFast, tSlow, tBallSlow);
		switch(type){
		case 0:	// faster cannon: interval = 0.5 * base for 5 s, player speed x2
			if(tFast <= 0) setInterval(baseInterval * 0.5f, 600.0f);
			tFast = 5.0f; tSlow = 0;
			break;
		case 1:	// slower cannon: interval = 2 * base for 5 s, player speed x0.5
			if(tSlow <= 0) setInterval(baseInterval + baseInterval, 150.0f);
			tSlow = 5.0f; tFast = 0;
			break;
		case 2:	// all balls 3 times slower for 8 s (only when not already active)
			if(tBallSlow <= 0){
				for(size_t i = 0; i < A.size(); i++){	// [V] the three ints are recomputed inside the loop, i.e. only when there is a ball
					ballG = ftol(sy * 100.0f); splitVx = ftol(sx * 33.3333f); splitVy = ballG;	// [V] 0x1003e380 = 33.333332
				}
				scaleVel(A, K_THIRD); scaleVel(B, K_THIRD);
			}
			tBallSlow = 8.0f;
			break;
		case 3: cannon.spr = cannonSpr[0]; cannonType = 0; break;	// [V] single
		case 4: cannon.spr = cannonSpr[1]; cannonType = 1; break;	// [V] double
		case 5: cannon.spr = cannonSpr[2]; cannonType = 2; break;	// [V] triple
		case 6: {	// all balls split at once
			int n = (int)A.size();
			if(n > 0){
				mg::snd(0x15e, 3.0f, 1);	// [V] 10009003 (volume argument 3.0)
				do { V3 p = A[0].pos; splitBall(p.x, p.y, p.z); A.erase(A.begin()); } while(--n);
			}
			break;
		}
		case 7: {	// half of the balls vanish (they turn into list D effects: no score)
			int n = (int)A.size() / 2;
			if(n > 0){
				mg::snd(0x141, 2.0f, 1);	// [V] 1000908e
				int slot = 0;
				for(; slot < 10; slot++) if(!eff[slot].active) break;
				if(slot < 10){			// a free effect slot: count = n, restart its animation
					eff[slot].count = n; eff[slot].active = true;
					mg::spSetFrame(eff[slot].spr, 0); mg::spSetState(eff[slot].spr, 0);
				}else slot = 0;			// [V] 100090ec: all busy -> slot 0 is reused as it is
				for(int k = 0; k < n; k++){
					size_t i = (size_t)mg::rand15() % A.size();	// [V] 1000912a div: unsigned
					Obj d = A[i]; d.spr = eff[slot].spr; d.type = slot; d.bounce = false;
					D.push_back(d);
					A.erase(A.begin() + i);
				}
			}
			break;
		}
		}
	}

	// Powerup_TimersUpdate @10005900 [V]: the 3 effects end when their timers have run below 0
	void timersUpdate()
	{
		if(tFast < 0){ setInterval(baseInterval, 300.0f); tFast = 0; }	// [V] 10005915: only if < 0 (strict)
		if(tSlow < 0){ setInterval(baseInterval, 300.0f); tSlow = 0; }
		if(tBallSlow < 0){
			for(size_t i = 0; i < A.size(); i++){
				ballG = ftol(sy * 300.0f); splitVx = ftol(sx * 100.0f); splitVy = ballG;	// [V] 10005a85
			}
			scaleVel(A, 3.0f); scaleVel(B, 3.0f);
			tBallSlow = 0;
		}
	}

	// =========================================================================================================================
	// Manager_Collide @10009400 [V]
	void collide()
	{
		for(size_t i = 0; i < C.size();){
			bool hit = false;
			for(size_t j = 0; j < A.size(); j++){
				if(!overlap(A[j], C[i])) continue;
				mg::snd(0x13f, 2.0f, 1);
				V3 p = A[j].pos; splitBall(p.x, p.y, p.z);	// before the hit ball is erased (it still counts for the 150 limit)
				A.erase(A.begin() + j); C.erase(C.begin() + i);
				hit = true; break;
			}
			if(hit) continue;				// the same index again (the next bullet moved into it)
			for(size_t j = 0; j < B.size(); j++){
				if(!overlap(B[j], C[i])) continue;
				mg::snd(0x140, 2.0f, 1);
				applyItem(B[j].type);
				B.erase(B.begin() + j); C.erase(C.begin() + i);
				hit = true; break;
			}
			if(!hit) i++;
		}
	}

	// Manager_Update @10009690 [V]; returns true when A, B and D are all empty
	bool managerUpdate(float dt)
	{
		if(tLeft <= 0){ keyL = false; keyR = false; cannon.vel.x = 0; }	// [V] 100096a9: time over -> no more movement
		collide();
		if(autoFireEn != 0 && tLeft > 0 && autoFire != 0 && cooldown <= 0) fireBullets();	// [V] cheat auto fire
		for(size_t i = 0; i < A.size();){
			objUpdate(A[i], dt);
			if(A[i].state == -1){ i++; continue; }
			popups.push_back({A[i].pos, 1.0f});		// [V] a ball that reached the floor: popup "+1" ...
			A.erase(A.begin() + i);
			mscore++; if(mscore > 999) mscore = 999;	// [V] ... and +1 point, capped at 999
			score = mscore;					// [V] 1000986a: synced to the game object (+0x394)
			LOG22("floor: score %d (balls left %d)", score, (int)A.size());
		}
		for(size_t i = 0; i < B.size();){
			objUpdate(B[i], dt);
			if(B[i].state == -1) i++; else B.erase(B.begin() + i);	// [V] powerup balls just disappear at the floor
		}
		for(size_t i = 0; i < C.size();){
			objUpdate(C[i], dt);
			if(C[i].state == -1) i++; else C.erase(C.begin() + i);	// [V] bullets vanish at any bound
		}
		// ListD_Update @100069f0 [V]
		for(size_t i = 0; i < D.size();){
			Obj &d = D[i];
			if(d.state == -1 && mg::spState(d.spr) != 2){ integrate(d, dt); i++; continue; }
			int s = d.type;
			D.erase(D.begin() + i);
			if(--eff[s].count == 0) eff[s].active = false;
		}
		for(EffSlot &e : eff) if(e.active) mg::spUpdate(e.spr, dt);
		// PopupsUpdate @10006660 [V]: the element after a removed one is skipped for this frame
		for(size_t i = 0; i < popups.size(); i++){
			popups[i].life -= dt;
			if(popups[i].life > 0) popups[i].pos.y -= dt * 50.0f;
			else popups.erase(popups.begin() + i);
		}
		objUpdate(cannon, dt);					// [V] 100099a8: cannon (base slot0)
		cooldown -= dt;
		if(tFast > 0) tFast -= dt;
		if(tSlow > 0) tSlow -= dt;
		if(tBallSlow > 0) tBallSlow -= dt;
		timersUpdate();
		return A.empty() && B.empty() && D.empty();
	}

	// Manager_OnKeyDown @10009380 / OnKeyUp @100043b0 [V]
	void mgrKeyDown(int k)
	{
		LOG22("key down %d (cannon x %.0f vx %.0f cooldown %.2f)", k, cannon.pos.x, cannon.vel.x, cooldown);
		if(k == ' '){ if(cooldown <= 0){ fireBullets(); autoFire = 1.0f; } }	// [V] the auto fire flag is only set when a shot was fired
		else if(k == 'a'){ keyL = true; cannon.vel.x = (float)(-playerSpeed); }
		else if(k == 'd'){ keyR = true; cannon.vel.x = (float)playerSpeed; }
	}
	void mgrKeyUp(int k)
	{
		LOG22("key up %d (cannon x %.0f vx %.0f)", k, cannon.pos.x, cannon.vel.x);
		if(k == ' ') autoFire = 0;
		else if(k == 'a'){ keyL = false; cannon.vel.x = keyR ? (float)playerSpeed : 0.0f; }
		else if(k == 'd'){ keyR = false; cannon.vel.x = keyL ? (float)(-playerSpeed) : 0.0f; }
	}

	// =========================================================================================================================
	// background: Bg_RespawnCloud @10009e40, Bg_Update @1000a070, Bg_Draw @10009af0 [V]
	void respawnCloud(int idx)
	{
		static const float spdY[5] = {350, 400, 250, 270, 300};			// [V] 0x1003e390
		static const float spdX[12] = {-400, -200, -50, -100, -150, -70, 30, 200, 100, 60, 150, 400};	// [V] 0x1003e3a4
		static const float spdY0[3] = {50, 40, 45};				// [V] 0x1003e3d4
		int w = mg::W; float cw, ch;
		int r = mg::rand15();
		Cloud &c = cloud[idx];
		if(idx != 0 || (r & 1)){	// [V] idx != 0: small clouds (set 2); idx 0 with an odd roll: the same
			int pick = (idx != 0) ? r % 3 : mg::rand15() % 3;
			c.spr = cloudSet2[pick]; mg::spSize(c.spr, cw, ch);
			int xr = mg::rand15() % (w + 2);
			c.z = 0; c.x = (float)xr - cw * 0.5f; c.y = -ch * 0.5f;
			c.vy = spdY[mg::rand15() % 5] * sy;
			c.vx = spdX[mg::rand15() % 12] * sx;
		}else{				// idx 0, even roll: a big slow one (set 1)
			c.spr = cloudSet1[mg::rand15() % 3]; mg::spSize(c.spr, cw, ch);
			c.vx = 0;
			c.vy = spdY0[mg::rand15() % 3] * sy;
			int xr = mg::rand15() % (w + 2);
			c.z = 0; c.x = (float)xr - cw * 0.5f; c.y = -ch * 0.5f;
		}
	}
	void bgUpdate(float dt)	// Bg_UpdateScroll @1000a200 + Bg_Update @1000a070
	{
		scroll = dt * scrollSpeed + scroll;
		for(int i = 0; i < 3; i++){
			Cloud &c = cloud[i];
			c.x = dt * c.vx + c.x; c.y = dt * c.vy + c.y;
			float cw, ch; mg::spSize(c.spr, cw, ch);
			// [V] 1000a0d0: the third test compares the sprite width * 0.5 + W with y (not with x): kept
			if(ch * 0.5f + (float)mg::H < c.y || c.x < -cw * 0.5f || cw * 0.5f + (float)mg::W < c.y) respawnCloud(i);
		}
	}
	void bgDraw()
	{
		if(!((float)mg::H > scroll)) scroll = 0;	// [V] 10009b14
		auto at = [&](int s, float x, float y){ mg::spPos(s, {x, y, 0}); mg::spDraw(s); };
		float y0 = scroll - sy * 256.0f;
		at(bg[0], sx * 256.0f, y0); at(bg[1], sx * 656.0f, y0);			// [V] two 512 px tiles (left 512 wide, right 288 wide)
		float y1 = sy * 344.0f + scroll;
		at(bg[0], sx * 256.0f, y1); at(bg[1], sx * 656.0f, y1);
		float y2 = scroll - sy * 556.0f;						// [V] and the two 88 px strips between them
		at(bg[2], sx * 256.0f, y2); at(bg[3], sx * 656.0f, y2);
		float y3 = sy * 44.0f + scroll;
		at(bg[2], sx * 256.0f, y3); at(bg[3], sx * 656.0f, y3);
		for(int i = 0; i < 3; i++){ mg::spPos(cloud[i].spr, {cloud[i].x, cloud[i].y, cloud[i].z}); mg::spDraw(cloud[i].spr); }
	}

	// DrawNumber @100019d0 [V]: "%d", digit sprite = arr[digit]; x of the digit i: 1 digit x, 2 digits x - w/2 + i*w, 3 digits (x - w) + i*w, more: all at x
	void drawNumber(const int *arr, float x, float y, float z, int value, float w)
	{
		char buf[32]; snprintf(buf, sizeof buf, "%d", value);
		int len = (int)strlen(buf);
		for(int i = 0; i < len; i++){
			char c = buf[i];
			if(c < '0' || c > '9') continue;
			int d = c - '0';
			if(!arr[d]) continue;
			float px = x;
			if(len == 2) px = (x - w * 0.5f) + (float)i * w;
			else if(len == 3) px = (float)i * w + (x - w);
			mg::spPos(arr[d], {px, y, z}); mg::spDraw(arr[d]);
		}
	}

	// =========================================================================================================================
	// StartGame @100018b0 [V]
	void startGame()
	{
		LOG22("start game (cheat %d)", (int)cheatOn);
		mg::snd(0x131, 2.0f, 1);
		mg::showCursor(false);
		if(ui){ mg::introHide(); ui = nullptr; }
		state = 1;
		mg::snd(0x12d, 2.0f, 1);
	}

	// HUD: LoadHudSprites @100011c0 [V]
	void loadHud()
	{
		V3 sc{sx, sy, sz};
		auto mk = [&](int art){ int h = mg::spNew(art); mg::spScale(h, sc); return h; };
		for(int d = 0; d < 10; d++) hudG[d] = mk(d == 0 ? 0x83e : 0x834 + d);	// green digits (0x83e = "0")
		for(int d = 0; d < 10; d++) hudR[d] = mk(d == 0 ? 0x849 : 0x83f + d);	// red digits (0x849 = "0")
		for(int d = 0; d < 10; d++) hudS[d] = mk(d == 0 ? 0x85c : 0x852 + d);	// score digits (0x85c = "0")
		bReady = mg::spNew(0x84b); bGo = mg::spNew(0x84c);
		mg::spScale(bReady, sc); mg::spScale(bGo, sc);
		mg::spPos(bReady, {sx * 400.0f, sy * 300.0f, 0}); mg::spPos(bGo, {sx * 400.0f, sy * 300.0f, 0});
		clockG = mk(0x83f); clockR = mk(0x84a);
		float cx = (fW * K_08 - s1 * 38.0f) + s1 * 92.0f, cy = (s1 * 4.0f + fH * K_2857) - s1 * 10.0f;	// [V] 100013c9..
		mg::spPos(clockG, {cx, cy, 0}); mg::spPos(clockR, {cx, cy, 0});
		float w, h;
		auto left = [&](int s, float y){ mg::spSize(s, w, h); mg::spPos(s, {sx * 387.5f - w * 0.5f, y, 0}); };	// [V] right edge of the 1st half at 387.5
		auto right = [&](int s, float y){ mg::spSize(s, w, h); mg::spPos(s, {sx * 412.5f + w * 0.5f, y, 0}); };	// [V] left edge of the 2nd half at 412.5
		bA1 = mk(0x84d); left(bA1, sy * 300.0f);
		bA2 = mk(0x84e); right(bA2, sy * 310.0f);
		bT1 = mk(0x85f); left(bT1, sy * 300.0f);
		bT2 = mk(0x850); right(bT2, sy * 310.0f);
		bR0 = mk(0x85d); mg::spPos(bR0, {sx * 400.0f, sy * 300.0f, 0});
		bR1b = mk(0x851); right(bR1b, sy * 310.0f);	// +0x370
		bR1a = mk(0x84f); left(bR1a, sy * 310.0f);	// +0x374
		bR2 = mk(0x85e); mg::spPos(bR2, {sx * 400.0f, sy * 300.0f, 0});
		sprUnused = mk(0x6ba);
	}

	// LoadManagerSprites @10003f40 + Manager_InitGame @100091e0 [V]
	void loadManager()
	{
		V3 sc{sx, sy, sz};
		auto mk = [&](int art){ int h = mg::spNew(art); mg::spScale(h, sc); return h; };
		for(int i = 0; i < 3; i++) cannonSpr[i] = mk(0x76e + i);
		for(int i = 0; i < 3; i++) bulletSpr[i] = mk(0x771 + i);
		ballSpr = mk(0x774);
		for(int i = 0; i < 8; i++) itemSpr[i] = mk(0x775 + i);
		for(int i = 0; i < 10; i++){ eff[i].spr = mk(0x77d); eff[i].count = 0; eff[i].active = false; }
		plusSpr = mk(0x852);
		oneSpr = mg::spNew(0x840); mg::spScale(oneSpr, {sx * 0.5f, sy * 0.5f, sz * 0.5f});	// [V] half size "1"
		// ---- InitGame
		baseInterval = fbits(0x3ecccccd); interval = baseInterval; cooldown = 0;	// [V] 0.4 s
		autoFireEn = 0; autoFire = 0; keyL = keyR = false;
		bulletSpeed = ftol(sy * 1000.0f);	// [V] +0x17c
		ballG = ftol(sy * 300.0f);		// [V] +0x180
		splitVx = ftol(sx * 100.0f);		// [V] +0x184
		splitVy = ballG;			// [V] +0x188
		mscore = 0; tFast = tSlow = tBallSlow = 0;
		cannon = Obj();
		cannon.hw = sx * 32.0f; cannon.hh = sy * 32.0f;			// [V] +0xa0 / +0xa4
		cannon.pos.x = fW * 0.5f + sx * 32.0f;				// [V] 100092bb: W * 0.5 + 32 * sx
		cannon.pos.y = floorY - (sy * 32.0f) * s1;			// [V] 100092c8: floorY - 32 * sy * mgr[1]
		cannon.scale = {sx, sy, sz};
		cannon.spr = cannonSpr[1]; cannonType = 1;			// [V] starts with the double cannon
		playerSpeed = ftol(sx * 300.0f);				// [V] +0x18c
		spawnBall((float)((int)fW / 2), sy * 16.0f, 0, 0);		// [V] 10009346: the first ball, top centre, no velocity
		sndX = fW * 0.5f; sndY = fH * 0.5f;
	}

	// =========================================================================================================================
	bool init(const std::vector<Rm8MiniPlayer> &pl) override
	{
		players = pl;
		running = true;
		fW = (float)mg::W; fH = (float)mg::H;				// [V] 10002635..: host +0x120 / +0x124
		s1 = sx = fW * K_SX;						// [V] 1000266f: +0x2bc, +0x2c0 = W * 0.00125
		sy = fH * K_SY;							// [V] 10002685: +0x2c4 = H * (1/600)
		sz = 1.0f;							// [V] +0x2c8
		mg::music(0x19);						// [V] 10002624: host +0x108 (0x19, 0) (+0x110 is a no-op)
		// intro panel lg_blastomere.ui [V] 100026a8..10002872: label 1 = 6955, legend labels by id
		ui = mg::introLoad("lg__lg_blastomere", 6955, [this]{ startGame(); });	// [V] OK button (id 2) -> StartGame (callback 10002460)
		if(ui){
			static const int ids[9] = {4, 6, 8, 10, 12, 14, 16, 18, 20};
			static const int strs[9] = {6957, 6961, 6962, 6958, 6959, 6960, 6963, 6964, 6965};	// [V] 0x1b2d, 0x1b31, 0x1b32, 0x1b2e, 0x1b2f, 0x1b30, 0x1b33, 0x1b34, 0x1b35
			for(int i = 0; i < 9; i++) rm8UiSetText(ui, ids[i], mg::str(strs[i]));
		}
		bT = 0; bL = 0;							// [V] 100028d2..: left 0x3c0, top 0x3c4
		bR = fW;							// [V] 100028e8: right 0x3b4 = W
		bB = fH - sy * 32.0f;						// [V] 100028d8: bottom 0x3b8 = H - 32 * sy
		floorY = fH - s1 * 32.0f;					// [V] 10002900: +0x390 = H - 32 * s1 (the floor line / cannon base)
		loadHud();
		// background object (ctor 1000a180, Init2 1000a270 -> LoadSprites 10009d40) [V]
		static const int bgArt[4] = {0x76d, 0x784, 0x785, 0x786};
		for(int i = 0; i < 4; i++){ bg[i] = mg::spNew(bgArt[i]); mg::spScale(bg[i], {sx, sy, sz}); }
		for(int i = 0; i < 3; i++){ cloudSet1[i] = mg::spNew(0x77e + i); mg::spScale(cloudSet1[i], {sx, sy, sz}); }
		for(int i = 0; i < 3; i++){ cloudSet2[i] = mg::spNew(0x781 + i); mg::spScale(cloudSet2[i], {sx, sy, sz}); }
		scroll = 0; scrollSpeed = sy * 20.0f;				// [V] 1000a279: +0x3c = 0, +0x40 = sy * 20
		for(int i = 0; i < 3; i++) respawnCloud(i);
		cloud[1].y = sy * 400.0f;					// [V] 1000a29a: +0x64 = cloud 1 y, +0x7c = cloud 2 y
		cloud[2].y = sy * 130.0f;
		loadManager();
		state = 0; tLeft = 50.0f; tCount = 2.0f; tEnd = 1.0f;		// [V] 100029a9..: 50 s, 2 s countdown, 1 s
		sndX = fW * 0.5f; sndY = fH * 0.5f;
		cheatTyping = false; cheatOn = false;
		last3ac = 6; last3a8 = 2; score = 0; tier = 0;
		playTimeUp = true; playResult = true;
		mg::showCursor(true);						// [V] 10002a2a: host +0x130 (1)
		// test hooks
		bot = getenv("RM8_MINI_BOT") && atoi(getenv("RM8_MINI_BOT"));
		if(const char *e = getenv("RM8_MINI_FORCE_POWERUP")) forcePowerup = atoi(e);
		if(const char *e = getenv("RM8_MINI_FORCE_AFTER")) forceAfter = (float)atof(e);
		if(const char *e = getenv("RM8_MINI_SCORE")){ mscore = score = std::max(0, std::min(999, atoi(e))); }
		if(const char *e = getenv("RM8_MINI_TIME")) tLeft = (float)atof(e);
		logOn = getenv("RM8_MG22_LOG") != nullptr;
		if(const char *e = getenv("RM8_MINI_BALLS")){ int n = atoi(e); for(int i = 0; i < n; i++) spawnBall((float)(mg::rand15() % mg::W), (float)(mg::rand15() % 300), 0, 1 + (i & 1)); }
		if(getenv("RM8_MINI_CHEAT") && atoi(getenv("RM8_MINI_CHEAT"))) cheat();
		return true;
	}

	void cheat()	// LG22_Cheat_Peng @10004510 [V]: the BASE interval 0.2 s (the current one changes the next time the timers restore it), auto fire enabled
	{
		baseInterval = fbits(0x3e4ccccd); autoFireEn = 1.0f; cheatOn = true;
		if(!cheatUi){
			cheatUi = rm8UiCreateCanvas("mg22cheat", RM8UI_HUD);	// [A] host +0xf8 text "Cheating......" at (0,0), 12 px, colour 0xff0000ff [H]
			if(cheatUi){
				cheatId = rm8UiAddWidget(cheatUi, "label", 0, 0, 0, 240, 20, -1, 0, 12);
				rm8UiSetText(cheatUi, cheatId, "Cheating......");
				rm8UiSetColorN(cheatUi, ("dyn" + std::to_string(cheatId)).c_str(), 0xff0000ff);
			}
		}
		if(cheatUi) rm8UiShow(cheatUi, true);
	}

	~G22() override { if(cheatUi) rm8UiShow(cheatUi, false); }

	// GetResult @100034b0 [V]: count = player count, entry i: {id = player id, score (entry 0 only), 0, tier, score copy}
	void getResult(Rm8MiniResult &o) override
	{
		if(playResult && state >= 3){	// [A] the tier is normally set by the first Draw of state 3 (10001f70); the test shot path may not have drawn
			tier = score <= 0x32 ? 0 : score <= 0x96 ? 1 : 2; playResult = false;
		}
		int n = std::max((int)players.size(), 2);	// [H] the host's table holds max(humans, 2) entries (lg_22.md 7.2); only entry 0 is used by the rewards
		n = std::min(n, 8);
		o.count = n;
		for(int i = 0; i < n; i++){
			Rm8MiniPlayerResult &p = o.p[i];
			p.seat = i < (int)players.size() ? players[i].seat : -1;	// [A] a missing second entry (single human) gets seat -1
			p.score = i == 0 ? score : 0;
			p.flagA = 0;
			p.rating = tier;
			p.flagB = p.score;
		}
		mg::showCursor(true);				// [V] 10003582: host +0x130 (1)
	}

	// Update @10001dd0 [V]
	bool update(float dt) override
	{
		if(!running) return false;
		if(bot) botStep();
		switch(state){
		case 1:
			if(tCount <= 0){ state = 2; LOG22("state 2 (play)"); return running; }	// [V] 10001df0: check first, then subtract
			tCount = tCount - dt;
			break;
		case 2: {
			tLeft = tLeft - dt;
			bgUpdate(dt);
			if(tLeft < 0){
				tLeft = 0;
				if(playTimeUp){ mg::snd(0x130, 2.0f, 1); playTimeUp = false; LOG22("time up (balls %d)", (int)A.size()); }	// [V] 10001e75
			}
			if(managerUpdate(dt)){ state = 3; LOG22("state 3: no balls left, time left %.2f, score %d", tLeft, score); return running; }
			break;
		}
		case 3:
			tEnd = tEnd - dt;
			if(tEnd <= 0){ state = 4; tEnd = 1.0f; LOG22("state 4"); return running; }
			break;
		case 4:
			tEnd = tEnd - dt;
			if(tEnd <= 0){ running = false; LOG22("end, score %d tier %d", score, tier); return false; }
			break;
		}
		return running;
	}

	// Draw @10001f70 [V]
	void draw() override
	{
		bgDraw();							// [V] 10009af0 (the first call, host +0x20, is an empty placeholder)
		mg::drawRect(0, floorY, (float)mg::W, 1.0f, 0xffffffff);	// [V] host +0xec (vf59, a line) from (0, floorY) to (W, floorY), white
		float ny = fH * K_2857 - sy * 10.0f;				// [V] 10002052..: y of both numbers
		float tx = sx * 92.0f + fW * K_08;				// [V] 1000203e: x of the time number
		if(tLeft <= 6.0f){						// [V] 10002074..: red from 6 s on
			mg::spDraw(clockR);
			drawNumber(hudR, tx, ny, 0, ftol(tLeft), sx * 27.0f);
		}else{
			mg::spDraw(clockG);
			drawNumber(hudG, tx, ny, 0, ftol(tLeft), sx * 27.0f);
		}
		drawNumber(hudS, sx * 15.0f + fW * K_08, ny, 0, score, sx * 22.0f);	// [V] 10002114: score number
		switch(state){
		case 1:
			mg::spDraw(tCount > 1.0f ? bReady : bGo);		// [V] 10002199 (fcomp 1.0 / jp): > 1 s "Ready", else "Go"
			if(last3a8 != ftol(tCount)){
				if(last3a8 == 1) mg::snd(0x165, 2.0f, 1);	// [V] 100021e0
				last3a8 = ftol(tCount);
			}
			break;
		case 2:
			if(ftol(tLeft) < 6){					// [V] 10002234: the last 5 s: the red digit at the top centre
				int d = ftol(tLeft);
				mg::spPos(hudR[d], {sx * 400.0f, sy * 40.0f, 0}); mg::spDraw(hudR[d]);
				if(last3ac != d){ mg::snd(0x12f, 2.0f, 1); last3ac = d; }	// [V] 100022b3
			}
			break;
		case 3:
			if(tLeft <= 0){ mg::spDraw(bT1); mg::spDraw(bT2); }	// [V] 100022fa: time up banner
			else { mg::spDraw(bA1); mg::spDraw(bA2); }		// all balls gone before the end
			if(playResult){						// [V] 1000233d: once: the sound of the score tier and the tier itself
				if(score <= 0x32){ mg::snd(0x135, 2.0f, 1); tier = 0; }
				else if(score <= 0x96){ mg::snd(0x134, 2.0f, 1); tier = 1; }
				else { mg::snd(0x15c, 2.0f, 1); tier = 2; }
				playResult = false;
			}
			break;
		case 4:
			if(score <= 0x32) mg::spDraw(bR0);			// [V] 100023f8
			else if(score <= 0x96){ mg::spDraw(bR1a); mg::spDraw(bR1b); }
			else mg::spDraw(bR2);
			break;
		}
		// DrawAll @10006070: bullets, balls, powerup balls, vanishing balls, cannon, popups
		for(const Obj &o : C) objDraw(o);
		for(const Obj &o : A) objDraw(o);
		for(const Obj &o : B) objDraw(o);
		for(const Obj &o : D) objDraw(o);
		objDraw(cannon);
		for(const Popup &p : popups){					// [V] 10005b70
			uint32_t col = ((uint32_t)(uint8_t)ftol(p.life * 255.0f) << 24) | 0xffffffu;
			mg::spPos(plusSpr, p.pos);
			mg::spPos(oneSpr, {sx * 10.0f + p.pos.x, p.pos.y, 0});
			mg::spColor(oneSpr, col); mg::spColor(plusSpr, col);
			mg::spDraw(plusSpr); mg::spDraw(oneSpr);
		}
		// [A] the original window is exactly 1024x768, so the clouds that drift past the left edge are clipped by it; the port letterboxes
		//     (other aspect ratios), so the bars are painted over whatever the game drew outside the 1024x768 area
		float ox, oy, sc; mg::gfxMap(ox, oy, sc);
		float bx = ox / sc, by = oy / sc, W_ = (float)mg::W, H_ = (float)mg::H;
		if(bx > 0.01f){ mg::drawRect(-bx - 1, -by - 1, bx + 1, H_ + 2 * by + 2, 0xff000000); mg::drawRect(W_, -by - 1, bx + 1, H_ + 2 * by + 2, 0xff000000); }
		if(by > 0.01f){ mg::drawRect(-bx - 1, -by - 1, W_ + 2 * bx + 2, by + 1, 0xff000000); mg::drawRect(-bx - 1, H_, W_ + 2 * bx + 2, by + 1, 0xff000000); }
	}

	// KeyDown @10003b10 [V]
	void keyDown(int code) override
	{
		if(state == 2 && tLeft > 0){ mgrKeyDown(code); return; }
		if(state != 0) return;
		if(code != 0x20){
			if(code == 0x8c){ cheatTyping = true; cheatBuf.clear(); return; }	// [V] start typing the cheat code
			if(code != 0xab){ cheatBuf.push_back((char)code); return; }
			cheatTyping = false;						// [V] Enter: check the buffer
			if(cheatBuf.size() == 4 && cheatBuf[0] == 'p' && cheatBuf[1] == 'e' && cheatBuf[2] == 'n' && cheatBuf[3] == 'g') cheat();
		}
		startGame();								// [V] space and Enter both start the game
	}
	// KeyUp @10001f40 [V]
	void keyUp(int code) override
	{
		if(state == 2 && tLeft > 0) mgrKeyUp(code);
	}

	// [A] the cannon is keyboard driven: left / right / fire buttons for phones (the cheat code has no touch button)
	void touchButtons(std::vector<MgTouchBtn> &out) override
	{
		out.push_back({30, 590, 170, 150, 'a', MG_SHAPE_LEFT});
		out.push_back({220, 590, 170, 150, 'd', MG_SHAPE_RIGHT});
		out.push_back({640, 560, 350, 180, ' ', MG_SHAPE_CIRCLE});
	}

	// test hook: a bot that plays through keyDown / keyUp
	bool botL = false, botR = false;
	void botStep()
	{
		if(state != 2 || tLeft <= 0) return;
		float tx = cannon.pos.x, best = -1e9f;
		for(const Obj &o : A) if(o.pos.y > best){ best = o.pos.y; tx = o.pos.x; }
		bool wantL = cannon.pos.x > tx + 10, wantR = cannon.pos.x < tx - 10;
		if(wantL != botL){ botL = wantL; if(botL) keyDown('a'); else keyUp('a'); }
		if(wantR != botR){ botR = wantR; if(botR) keyDown('d'); else keyUp('d'); }
		if(cooldown <= 0){ keyDown(' '); keyUp(' '); }
	}
};
}	// namespace

MgGame *mgNew22(){ return new G22; }
