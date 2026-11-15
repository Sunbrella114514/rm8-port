// Minigame 18 "Quick Chef" (ltlgame_18.dll): transliteration of the DLL on the port's host services.
// Tags: [V] verified in the decompile / disassembly (address), [H] hypothesis, [A] port adaptation.
// Object layout of the DLL (0x294 bytes) is kept as comments (+0xNNN) next to the members.
// Test hooks (env): RM8_MG18_LOG=1 logs score events to stderr; RM8_MG18_SIMDRAW=1 also runs the Draw logic (collision + scoring) after every Update so --shot runs
// (which only step Update) exercise the rules. Use RM8_MINI_PLAYERS=ch to get mode 1 (AI chef), hh for mode 2 (two humans), hc for mode 0.
// DLL entry points: Init @0x1002CC20, GetResult @0x1002FA60, Update @0x1002FB50, DrawAndCollide @0x10033100 (vtable slot 3 = Draw),
// DrawHud @0x10031B50, Chef_Update @0x10034660, Disruptor_Move @0x10034B00, OnKeyDown @0x10034C40, OnKeyUp @0x10035160,
// CloseIntro @0x10035030, ChefAI_PickTargetX @0x1002C440, popups FUN_10035AD0 / FUN_10035BB0 / DrawScorePopups @0x10035D30,
// DrawNumber @0x10035800, RectCornerInRect @0x1002C880 (+ FUN_1002C800).
#include "mg_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace {
using namespace mg;
#define DBG(...) do{ if(getenv("RM8_MG18_LOG")){ fprintf(stderr, "[mg18 t=%.2f] ", mg::now()); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } }while(0)

// element of the falling food vector (+0xd0) and of the thrown-object vector (+0xe0), 0x20 bytes [V] 0x10036840 callers in Update
struct Item { float x, y, z; int type; float vy; int score; int spr; };	// +0 +4 +8, +0x10, +0x14, +0x18, +0x1c
struct Roach { int spr; float x, y, z, dir, timer; };			// crawling cockroach (+0xf0), 0x18 bytes [V] 0x10033100
struct Flash { int spr; float x, y, z, life; };				// cup / plate shards (+0x100), 0x14 bytes
struct Popup { float life; int score; float x, y, z; float speed; };	// score popup, FUN_10035AD0 [V] (life 1.0, speed 100.0)

struct G18 : MgGame {
	// ---- DLL fields
	bool introOpen = true;		// +0x1b1 (1 while the lg_intro.ui panel is up)
	bool readyOver = false;		// +0x1b2
	bool running = true;		// +0x1b0
	bool done = false;		// +0x1d8 (game over: time out or cockroach)
	int mode = 0;			// +0x1ac: 0 human chef + AI disruptor, 1 AI chef + human disruptor, 2 two humans
	float sxW = 0, syH = 0;		// +0x260 = W / 800, +0x264 = H / 600 ([V] 0x1002CC20); +0x268..0x270 = +0x260
	float chefX = 0, chefY = 0, chefZ = 0;	// +0x14 +0x18 +0x1c
	float chefSpeed = 0;		// +0x20
	int chefCur = 0, chefL = 0, chefR = 0, chefHitL = 0, chefHitR = 0;	// +0x24 current, +0x28 0x2c 0x30 0x34
	float disX = 0, disY = 0;	// +0x38 +0x3c (the disruptor at the top)
	std::string portrait;		// +0x58 texture of the second player (host +0xf0 quad at +0x44)
	float quadX = 0, quadY = 0, quadW = 0, quadH = 0;	// +0x44 +0x48 +0x4c +0x50
	bool quadOn = false;
	int heldSpr = 0;		// +0x6c the next object in the disruptor's hand
	int heldType = 0;		// +0x70
	int foodSpr[6] = {0};		// +0x74
	int throwSpr[3] = {0};		// +0x8c
	int roachSprL = 0, roachSprR = 0, shardCup = 0, shardPlate = 0;	// +0x98 +0x9c +0xa0 +0xa4
	int bnReady = 0, bnGo = 0, bnGameA = 0, bnGameB = 0, bnTimeA = 0, bnTimeB = 0, bnR0 = 0, bnR1a = 0, bnR1b = 0, bnR2 = 0;	// +0xa8..+0xcc
	int bg[4] = {0};		// +0x118 +0x11c +0x110 +0x114
	int digScore[10] = {0}, digTime[10] = {0}, digTime10[10] = {0};	// +0x120 +0x148 +0x170
	int signMinus = 0, signPlus = 0, clockBg = 0, clockBg10 = 0;	// +0x198 +0x19c +0x1a0 +0x1a4 ([V] 0x1a0 = id 0x83f, 0x1a4 = id 0x84a)
	int score = 0;			// +0x1bc
	float timeLeft = 40.0f;		// +0x1c0
	int timeShown = -1, timeSaid = -1;	// +0x1c4 +0x1c8
	float t1cc = 2.0f;		// +0x1cc Ready / Go countdown
	float t1d0 = 1.0f, t1d4 = 1.0f;	// +0x1d0 +0x1d4 end delays
	float ground = 0;		// +0x1b8 = H * 7 / 9
	float stunTime = 0.2f, stun = 0;	// +0x1e4 +0x1e8
	float jumpV = 0, vy = 0, grav = 0;	// +0x1ec +0x1f0 +0x1f4
	int chefState = 7, disState = 7;	// +0x1f8 +0x1fc (0 left, 1 right, 2 jump, 3/4 jump + left/right, 6 throw, 7 idle)
	float cool[8] = {0};		// +0x200..+0x21c per column cooldown
	float throwTimer = 1.5f;	// +0x220
	float roachSoundT = 0.4f;	// +0x224
	int hw228 = 0, hh22c = 0, hw230 = 0, hh234 = 0, hw238 = 0, hh23c = 0;	// half sizes: chef (44 sx, 54 sy), item (16 sx, 16 sy), roach (16 sx, 12 sy)
	float flashLife = 0.3f;		// +0x240
	int lastReadySnd = 5;		// +0x248
	float aiMoves = 0, aiTimer = 0, aiRate = 0;	// +0x24c +0x250 +0x254 (AI chef move-rate limiter)
	int aiLastDir = 7;		// +0x258
	int disLastDir = 7;		// +0x25c [A] uninitialised in the DLL (heap fill), only compared with 0 / 1
	bool kLeft = false, kRight = false, kEnter = false, kA = false, kD = false, kSpace = false;	// +0x288 +0x289 +0x28a +0x28b +0x28c +0x28d
	bool endSnd = false;		// +0x28f
	int rating = 0;			// +0x290
	bool ratingSet = false;		// [A] see getResult
	int seat0 = 0;
	std::vector<Item> foods, throws;	// +0xd0, +0xe0
	std::vector<Roach> roaches;	// +0xf0
	std::vector<Flash> flashes;	// +0x100
	std::vector<Popup> popups;
	Rm8Ui *introUi = nullptr;

	// ---------------------------------------------------------------------------------------------------------------------
	int mk(int art){	// create + scale by (+0x26c, +0x270, +0x274) = (W/800, W/800, 1) [V] every spNew in Init is followed by +0x64 with &+0x26c
		int h = spNew(art);
		spScale(h, V3(sxW, sxW, 1.0f));
		return h;
	}

	bool init(const std::vector<Rm8MiniPlayer> &pl) override {
		showCursor(true);						// [V] +0x130(1)
		introUi = introLoad("lg__lg_intro", 6951, [this]{ closeIntro(); });	// [V] lg_intro.ui, label 1 = string 0x1b27, button 2 -> FUN_10035730 -> CloseIntro
		if(!introUi) introOpen = false;					// [A] no panel available: do not block the game
		// [V] mode: 0x1002cc20: type of player 0 == 1 (computer) -> 1; else 2 if any other player is human, else 0
		mode = 0;
		if(!pl.empty() && !pl[0].human) mode = 1;
		else for(size_t i = 1; i < pl.size(); i++) if(pl[i].human){ mode = 2; break; }
		seat0 = pl.empty() ? 0 : pl[0].seat;
		float w = (float)W, h = (float)H;				// +0x120 / +0x124
		sxW = w / 800.0f; syH = h / 600.0f;				// [V] +0x260 +0x264
		timeLeft = 40.0f;						// [V] +0x1c0 = 0x42200000
		t1cc = 2.0f;							// [V] +0x1cc = 0x40000000
		score = 0; running = true; stunTime = 0.2f; stun = 0;		// [V] +0x1bc +0x1b0 +0x1e4 = 0x3e4ccccd +0x1e8
		jumpV = syH * 1000.0f; vy = 0; grav = syH * 100.0f;		// [V] +0x1ec +0x1f0 +0x1f4
		chefState = 7; throwTimer = 1.5f; roachSoundT = 0.4f;		// [V] +0x1f8 +0x220 +0x224
		hw228 = (int)(44.0f * sxW); hh22c = (int)(54.0f * syH);		// [V] +0x228 +0x22c  (ftol)
		hw230 = (int)(16.0f * sxW); hh234 = (int)(16.0f * syH);		// [V] +0x230 +0x234
		hw238 = (int)(16.0f * sxW); hh23c = (int)(12.0f * syH);		// [V] +0x238 +0x23c
		flashLife = 0.3f; t1d0 = 1.0f; t1d4 = 1.0f;			// [V] +0x240 = 0x3e99999a, +0x1d0 +0x1d4 = 1.0
		static const float coolTab[3] = {1.0f, 3.0f, 5.0f};		// [V] 0x10088d74
		for(int i = 0; i < 8; i++) cool[i] = coolTab[rand15() % 3];	// [V] +0x200 + 4i
		chefSpeed = w / 4.0f;						// [V] +0x20
		ground = (h * 7.0f) / 9.0f;					// [V] +0x1b8
		chefX = (w / 8.0f) / 2.0f + w / 2.0f; chefY = ground; chefZ = 0;	// [V] +0x14 +0x18 +0x1c
		chefL = mk(0x5df); chefR = mk(0x5de); chefHitL = mk(0x5e1); chefHitR = mk(0x5e0);	// [V] +0x28 +0x2c +0x30 +0x34
		chefCur = chefL;
		spPos(chefCur, V3(chefX, chefY, chefZ));
		disX = (w / 8.0f) / 2.0f + w / 2.0f; disY = h / 8.0f;		// [V] +0x38 +0x3c
		quadW = sxW * 100.0f; quadH = syH * 100.0f;			// [V] +0x4c +0x50
		quadX = w / 2.0f - quadW / 2.0f; quadY = h / 8.0f - quadH / 2.0f;	// [V] +0x44 +0x48
		quadOn = false;
		if(pl.size() >= 2 && !pl[1].pic.empty()){			// [V] params[0x28] = texture of player 1; [A] key "prt\npc\<pic>_01.tga" (the guide), none if there is no second player
			portrait = "prt\\npc\\" + pl[1].pic + "_01.tga"; quadOn = true;
		}
		roachSprL = mk(0x5ec); roachSprR = mk(0x5ed); shardCup = mk(0x5e8); shardPlate = mk(0x5ea);	// [V] +0x98 +0x9c +0xa0 +0xa4
		static const int foodArt[6] = {0x5e2, 0x5e3, 0x5e5, 0x5e4, 0x5e6, 0x5ee};	// [V] +0x74..+0x88
		for(int i = 0; i < 6; i++) foodSpr[i] = mk(foodArt[i]);
		static const int throwArt[3] = {0x5e7, 0x5e9, 0x5eb};		// [V] +0x8c +0x90 +0x94: cup, plate, cockroach
		for(int i = 0; i < 3; i++) throwSpr[i] = mk(throwArt[i]);
		heldSpr = 0;
		bnReady = mk(0x84b); bnGo = mk(0x84c); bnGameA = mk(0x84d); bnGameB = mk(0x84e); bnTimeA = mk(0x85f); bnTimeB = mk(0x850);	// [V] +0xa8..+0xbc
		bnR0 = mk(0x85d); bnR1a = mk(0x84f); bnR1b = mk(0x851); bnR2 = mk(0x85e);				// [V] +0xc0 +0xc4 +0xc8 +0xcc
		bg[2] = spNew(0x5f0); bg[3] = spNew(0x5f1); bg[0] = spNew(0x5dd); bg[1] = spNew(0x5ef);		// [V] +0x110 +0x114 +0x118 +0x11c (scaled below)
		for(int i = 0; i < 4; i++) spScale(bg[i], V3(sxW, sxW, 1.0f));
		for(int d = 1; d < 10; d++) digScore[d] = mk(d + 0x852);	// [V] +0x120 + 4d
		digScore[0] = mk(0x85c);
		for(int d = 1; d < 10; d++) digTime[d] = mk(d + 0x834);		// [V] +0x148 + 4d
		digTime[0] = mk(0x83e);
		for(int d = 1; d < 10; d++) digTime10[d] = mk(d + 0x83f);	// [V] +0x170 + 4d
		digTime10[0] = mk(0x849);
		signMinus = mk(0x860); signPlus = mk(0x852);			// [V] +0x198 +0x19c
		clockBg = spNew(0x83f); clockBg10 = spNew(0x84a);		// [V] +0x1a0 +0x1a4
		spScale(clockBg, V3(sxW, sxW, 1.0f)); spScale(clockBg10, V3(sxW, sxW, 1.0f));
		// [V] 0x1002e39e..: tile positions (W/800 * 256 / 656 / 556)
		spPos(bg[0], V3(sxW * 256.0f, sxW * 256.0f, 0)); spPos(bg[1], V3(sxW * 656.0f, sxW * 256.0f, 0));
		spPos(bg[2], V3(sxW * 256.0f, sxW * 556.0f, 0)); spPos(bg[3], V3(sxW * 656.0f, sxW * 556.0f, 0));
		timeShown = -1; timeSaid = -1;					// [V] +0x1c4 +0x1c8 = -1
		lastReadySnd = 5;						// [V] +0x248 = 5
		aiMoves = aiTimer = aiRate = 0; aiLastDir = chefState;		// [V] +0x24c +0x250 +0x254 = 0, +0x258 = +0x1f8
		kLeft = kRight = kEnter = kA = kD = kSpace = false;		// [V] +0x288..+0x28d
		endSnd = false; rating = 0;
		music(0x15);							// [V] +0x108(0x15, 0); LoadBgm @0x1002ED60 asks for "L001.mp3" which the host never plays
		return true;
	}

	void closeIntro(){	// [V] CloseIntroDialog @0x10035030: only if the panel exists
		if(introUi){ introOpen = false; introHide(); introUi = nullptr; }
	}

	// ---------------------------------------------------------------------------------------------------------------------
	static int rectInside(int px, int py, int bx1, int by1, int bx2, int by2){	// [V] FUN_1002c800
		return !(px < bx1 || bx2 < px || py < by1 || by2 < py);
	}
	static bool cornerIn(int x1, int y1, int x2, int y2, int bx1, int by1, int bx2, int by2){	// [V] RectCornerInRect @0x1002c880
		return rectInside(x1, y1, bx1, by1, bx2, by2) || rectInside(x1, y2, bx1, by1, bx2, by2)
		    || rectInside(x2, y1, bx1, by1, bx2, by2) || rectInside(x2, y2, bx1, by1, bx2, by2);
	}
	void chefRect(int &x1, int &y1, int &x2, int &y2){	// chef rect (+0x228 / +0x22c half sizes)
		x1 = (int)(chefX - (float)hw228); y1 = (int)(chefY - (float)hh22c);
		x2 = (int)(chefX + (float)hw228); y2 = (int)(chefY + (float)hh22c);
	}

	void addPopup(int sc, float x, float y, float z){	// [V] FUN_10035ad0: only if score != 0
		if(sc == 0) return;
		Popup p; p.life = 1.0f; p.score = sc; p.x = x; p.y = y; p.z = z; p.speed = 100.0f;
		popups.push_back(p);
	}

	// ---- Chef_Update @0x10034660 ---------------------------------------------------------------------------------------
	void chefUpdate(int state, float dt){
		float w = (float)W;
		if((state == 2 || state == 3 || state == 4) && chefY == ground){		// [V] start a jump on the ground
			vy = jumpV;
			snd(0x137, 1.0f, 1);
		}
		if(chefY != ground || vy != 0.0f){
			vy = vy - grav * 23.0f * dt;						// [V] 0x10034770: +0x1f4 * 23.0
			chefY = chefY - dt * vy;
			if(ground <= chefY){ vy = 0; chefY = ground; snd(0x138, 1.0f, 1); }	// [V] landing
		}
		if(stun <= 0.0f){								// [V] +0x1e8 <= 0 (asm test ah,0x41)
			spUpdate(chefCur, dt);
			if(state == 0 || state == 3){
				chefX = chefX - dt * chefSpeed;
				if(chefX < 0.0f) chefX = 0.0f;
				chefCur = chefL; snd(0x136, 0.6f, 1);				// [V] 0x3f19999a
			}else if(state == 1 || state == 4){
				chefX = dt * chefSpeed + chefX;
				if(w < chefX) chefX = w;
				chefCur = chefR; snd(0x136, 0.6f, 1);
			}
		}
		stun -= dt;
		if(stun <= 0.0f){
			if(chefCur == chefHitL) chefCur = chefL; else if(chefCur == chefHitR) chefCur = chefR;
		}else{
			if(chefCur == chefL) chefCur = chefHitL; else if(chefCur == chefR) chefCur = chefHitR;
		}
		spPos(chefCur, V3(chefX, chefY, chefZ));
	}

	// ---- Disruptor_Move @0x10034B00 -------------------------------------------------------------------------------------
	void disruptorMove(int state, float dt){
		float w = (float)W;
		if(state == 0){
			disX = disX - chefSpeed * 1.3f * dt;
			if(disX < 0.0f) disX = 0.0f;
		}else if(state == 1){
			disX = chefSpeed * 1.3f * dt + disX;
			if(w < disX) disX = w;
		}
	}

	// ---- ChefAI_PickTargetX @0x1002C440 (mode 1) -------------------------------------------------------------------------
	int pickTargetX(float x, float y, int w, int hArg){
		int sum[8] = {0};
		for(const Item &it : foods){
			int a = it.score * 100;
			int w8 = (w + ((w >> 31) & 7)) >> 3, w16 = (w + ((w >> 31) & 0xf)) >> 4;
			int b = (int)(1000.0f - std::fabs(it.x - x) * 100.0f / (float)w8);
			int c = (int)(1000.0f - ((float)hArg - it.y) * 100.0f / (float)w8);
			int k = (int)((it.x - (float)w16) / (float)w8);
			if(k < 0 || k > 7) continue;	// [A] the DLL does not bound the index (the food columns always give 0..7)
			sum[k] = a + b + c + sum[k];
		}
		int best = 0, bestV = 0;
		for(int i = 0; i < 8; i++) if(bestV < sum[i]){ best = i; bestV = sum[i]; }
		int bw = best * w;
		return ((bw + ((bw >> 31) & 7)) >> 3) + ((w + ((w >> 31) & 0xf)) >> 4);
	}

	// pick the next thrown object type (the same code at three places of Update) [V] 0x1002ff.. / 0x100305..
	void pickThrow(){
		int r = rand15() % 100;
		if(r < 0x25) heldType = 1;		// 37 %: plate (-40)
		else if(r < 0x46) heldType = 0;		// 33 %: cup (-30)
		else if(r < 0x65) heldType = 2;		// 30 %: cockroach
		heldSpr = throwSpr[heldType];		// +0x6c = +0x8c + 4 * type
	}
	void doThrow(int type, int loops){	// push a thrown object at the disruptor (+0x38, +0x3c + 16.0) [V] 0x100305.. / 0x1003078..
		static const int pts[3] = {-30, -40, 0};	// [V] 0x10088d80
		Item it; it.x = disX; it.y = disY + 16.0f; it.z = 0; it.type = type;
		it.vy = (ground * 18.0f) / 18.0f; it.score = pts[type]; it.spr = throwSpr[type];
		throws.push_back(it);
		if(type == 2) snd(0x139, 1.0f, loops);	// [V] cockroach throw sound (loops 1 in mode 1/2, 2 in mode 0)
		throwTimer = 1.5f; heldSpr = 0;
	}

	// ---- Update @0x1002FB50 --------------------------------------------------------------------------------------------
	bool update(float dt) override {	// [A] test hook RM8_MG18_SIMDRAW=1: run the Draw logic (collision + scoring live in Draw in the DLL) after every Update, for --shot runs that only step Update
		bool r = updateImpl(dt);
		static const bool sim = getenv("RM8_MG18_SIMDRAW") != nullptr;
		if(sim) draw();
		return r;
	}
	bool updateImpl(float dt){
		if(introOpen) return true;						// [V] +0x1b1 != 0: nothing runs behind the panel
		if(t1cc <= 0.0f) readyOver = true;					// [V] +0x1cc <= 0 -> +0x1b2
		t1cc = t1cc - dt;
		if(!readyOver) return true;						// [V] Ready / Go phase: only the timer
		if(done){								// [V] +0x1d8: after game over only the two 1 s delays run
			t1d0 = t1d0 - dt;
			if(t1d0 < 0.0f){
				t1d4 = t1d4 - dt;
				if(t1d4 <= 0.0f) running = !running;
			}
			if(running) return true;
		}
		if(!running) return false;
		// ---- key states -> disruptor / chef state (every frame)
		if(!kLeft && !kRight && !kEnter) disState = 7;
		if(kLeft && !kEnter) disState = 0;
		if(kRight && !kEnter) disState = 1;
		if(kEnter && !kLeft && !kRight) disState = 6;
		if(mode == 0 || mode == 2){
			if(!kA && !kD && !kSpace) chefState = 7;
			if(kA && !kSpace && !kD) chefState = 0;
			if(kD && !kSpace && !kA) chefState = 1;
			if(kA && kSpace) chefState = 3;
			if(kD && kSpace) chefState = 4;
			if(chefState == 0 && kSpace && !kD) chefState = 3;
			if(chefState == 1 && kSpace && !kA) chefState = 4;
			if(kA && kD) chefState = 7;
		}
		timeLeft = timeLeft - dt;						// [V] +0x1c0
		if(timeLeft <= 0.0f){ done = true; DBG("time out, score %d", score); snd(0x130, 1.0f, 1); }		// [V] time out: only the first frame (the body is skipped afterwards)
		disruptorPosQuad();							// [V] +0x44 = +0x38
		chefUpdate(chefState, dt);						// [V] vtable +0x20
		disruptorMove(disState, dt);						// [V] vtable +0x24
		// ---- food spawning: 8 columns with cooldown
		int tries = rand15() % 5 + 3;
		int w = W;
		for(int i = 0; i < 8; i++) cool[i] = cool[i] - dt;
		std::vector<int> cols;
		for(int i = 0; i < 8; i++) cols.push_back(((w * i + ((w * i >> 31) & 7)) >> 3) + ((w + ((w >> 31) & 0xf)) >> 4));	// W*i/8 + W/16
		for(int b = 0; b < tries; b++){
			int k = rand15() % (8 - b);					// [V] index into the shrinking column list (also the cooldown index)
			if(cool[k] <= 0.0f){						// [V] asm: test ah,0x41 -> <= 0
				Item it; it.x = (float)cols[k]; it.y = 0; it.z = 0;
				cols.erase(cols.begin() + k);
				int r = rand15();
				float f = (float)((double)r / 32767.0);			// [V] 0x1007f0cc = 32767.0
				int type = -1;
				if(f < 0.1f) type = 0;
				else if(f < 0.3f) type = 1;
				else if(f < 0.5f) type = 2;
				else if(f < 0.7f) type = 3;
				else if(f < 0.9f) type = 4;
				else if(f <= 1.0f) type = 5;
				static const float spd[3] = {2.0f, 4.0f, 6.0f};		// [V] 0x10088d68
				static const int pts[6] = {8, 6, 3, 5, 3, 5};		// [V] 0x10088d50
				static const float coolTab[3] = {1.0f, 3.0f, 5.0f};	// [V] 0x10088d74
				float sp = (spd[rand15() % 3] * ground) / 7.0f;
				if(type < 0) type = 0;	// [A] unreachable (f <= 1.0 always)
				it.type = type; it.vy = sp; it.score = pts[type]; it.spr = foodSpr[type];
				foods.push_back(it);
				snd(0x13b, 0.5f, 1);					// [V] food spawn sound
				cool[k] = coolTab[rand15() % 3];
			}
		}
		// ---- the disruptor's next object
		if(heldSpr == 0){
			if(mode != 0 || 1.0f <= throwTimer){
				if((mode == 1 || mode == 2) && throwTimer < 0.5f) pickThrow();
			}else pickThrow();
		}
		if(mode == 1 || mode == 2){
			throwTimer = throwTimer - dt;					// [V] +0x220
			if(heldSpr != 0 && (disState == 6 || throwTimer < -1.0f)) doThrow(heldType, 1);	// [V] Enter or after 1 s of waiting
		}
		if(mode == 0){								// AI disruptor
			throwTimer = throwTimer - dt;
			float fw = (float)W;
			int r = rand15() % 10;
			if(r < 4){							// [V] 40 % per frame: approach the chef at 2.1 x speed
				if(disX < chefX && fw / 27.0f < chefX - disX) disX = chefSpeed * 2.1f * dt + disX;
				if(chefX < disX && fw / 27.0f < disX - chefX) disX = disX - chefSpeed * 2.1f * dt;
			}
			if(throwTimer <= 0.0f) doThrow(heldType, 2);			// [V] sprite comes from the type (+0x1c), not from +0x6c
		}
		if(mode == 1) aiChef(dt);
		// ---- move everything
		for(Item &it : foods){							// [V] 0x10030f3f: vy += dt * g; y += dt * vy
			it.vy = dt * grav + it.vy;
			it.y = dt * it.vy + it.y;
			spUpdate(it.spr, dt);
		}
		for(Item &it : throws){
			if(it.type == 2) it.vy = grav * 0.0f * dt + it.vy;		// cockroach: constant speed
			else it.vy = dt * grav + it.vy;
			it.y = dt * it.vy + it.y;
			spUpdate(it.spr, dt);
		}
		for(Roach &r : roaches){
			float fw = (float)W;
			r.x = ((fw * r.dir) / 4.0f) * dt + r.x;				// [V] crawls W/4 px/s
			if(r.timer <= 0.0f){ r.timer = roachSoundT; snd(0x13a, 0.5f, 1); }
			spUpdate(throwSpr[2], dt);
			spUpdate(r.spr, dt);
			r.timer = r.timer - dt;
		}
		for(Flash &f : flashes){ f.life = f.life - dt; spUpdate(f.spr, dt); }
		for(size_t i = 0; i < popups.size();){					// [V] FUN_10035bb0
			popups[i].life -= dt;
			popups[i].y -= dt * popups[i].speed;
			if(popups[i].life <= 0.0f) popups.erase(popups.begin() + i); else i++;
		}
		return running;
	}
	void disruptorPosQuad(){ quadX = disX; }	// [V] 0x1002ffa0: *(this+0x44) = *(this+0x38)

	// mode 1: the AI chef [V] 0x100308af..0x10030ba3 and the loops after it; unreachable in the shipped game (an AI chef never runs the DLL)
	void aiChef(float dt){
		int w = W, h = H; (void)h;
		aiTimer = dt + aiTimer;
		if(1.0f < aiTimer){ aiRate = aiMoves / aiTimer; aiMoves = 0; aiTimer = 0; }
		float target = (float)pickTargetX(chefX, chefY, w, (int)((float)hw228 + ground));	// [V] last argument = ftol(+0x228 + +0x1b8)
		if(target <= chefX || target - chefX <= (float)w / 36.0f){
			if(chefX <= target) chefState = 7;
			else{
				int r = rand15() % 200;
				if(r < 0x97){
					if(aiLastDir == 1){
						if(aiRate <= 5.0f){ chefState = 0; aiLastDir = chefState; aiMoves = aiMoves + 1.0f; }
					}else{ chefState = 0; aiLastDir = chefState; }
				}else if(r < 0x98) chefState = 3;
			}
		}else{
			int r = rand15() % 200;
			if(r < 0x97){
				if(aiLastDir == 0){
					if(aiRate <= 5.0f){ chefState = 1; aiLastDir = chefState; aiMoves = aiMoves + 1.0f; }
				}else{ chefState = 1; aiLastDir = chefState; }
			}else if(r < 0x98) chefState = 4;
		}
		for(Roach &r : roaches){						// run from crawling cockroaches
			float d = std::fabs(r.x - chefX);
			if(d <= ((float)w * 1.0f) / 5.0f){
				int q = rand15() % 10;
				if(q < 5) chefState = 2;
				else if(q < 8){
					if(r.x <= chefX){ if(r.x < chefX) chefState = 4; }
					else chefState = 3;
				}
			}
		}
		for(Item &it : throws){							// ... and from cockroaches in flight
			if(it.type != 2) continue;
			if(it.x == chefX){
				int q = rand15() % 100;
				if(q < 0x2d) chefState = 0; else if(q < 0x5a) chefState = 1; else chefState = 7;
			}else{
				float d = std::fabs(it.x - chefX);
				if(d <= (float)(w >> 3)){
					if(it.x <= chefX){
						int q = rand15() % 100;
						if(q < 0x50) chefState = 1; else if(q < 0x5a) chefState = 0; else chefState = 7;
					}else{
						int q = rand15() % 100;
						if(q < 0x50) chefState = 0; else if(q < 0x5a) chefState = 1; else chefState = 7;
					}
				}
			}
		}
	}

	// ---- DrawNumber @0x10035800: sprintf("%d"), every digit char is a sprite, 2 / 3 digits are centred by the spacing --------------------
	void drawNumber(const int *digs, float x, float y, float z, int value, int spacing){
		char buf[64]; snprintf(buf, sizeof buf, "%d", value);
		size_t n = strlen(buf);
		for(size_t i = 0; i < n; i++){
			int c = buf[i];
			if(c < '0' || c > '9') continue;				// the '-' of a negative number is skipped but counts as a position
			int d = c - '0';
			if(!digs[d]) continue;
			float px = x;
			if(n == 1) px = x;
			else if(n == 2) px = (x - (float)spacing * 0.5f) + (float)((int)i * spacing);
			else if(n == 3) px = (x - (float)spacing * 1.0f) + (float)((int)i * spacing);
			spPos(digs[d], V3(px, y, z));
			spDraw(digs[d]);
		}
	}

	// ---- DrawAndCollide @0x10033100 (Draw) ---------------------------------------------------------------------------------
	void draw() override {
		float fw = (float)W;
		// [V] DrawCornerFrames @0x10032fe0 (host +0x20 is a no-op)
		spDraw(bg[0]); spDraw(bg[1]); spDraw(bg[2]); spDraw(bg[3]);
		// ---- falling food
		for(size_t i = 0; i < foods.size();){
			Item &it = foods[i];
			spPos(it.spr, V3(it.x, it.y, it.z)); spDraw(it.spr);
			int ax1 = (int)(it.x - (float)hw230), ay1 = (int)(it.y - (float)hh234), ax2 = (int)(it.x + (float)hw230), ay2 = (int)(it.y + (float)hh234);
			int bx1, by1, bx2, by2; chefRect(bx1, by1, bx2, by2);
			if((float)hh22c + ground > it.y){				// still above the floor (chef feet line)
				if(cornerIn(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2) && !done){
					snd(0x13d, 0.5f, 1);
					score += it.score;
					DBG("food type %d pts %d -> score %d", it.type, it.score, score);
					addPopup(it.score, it.x, it.y, it.z);
					foods.erase(foods.begin() + i);
				}else i++;
			}else foods.erase(foods.begin() + i);
		}
		// ---- thrown cups / plates / cockroaches
		for(size_t i = 0; i < throws.size();){
			Item &it = throws[i];
			spPos(it.spr, V3(it.x, it.y, it.z)); spDraw(it.spr);
			int ax1 = (int)(it.x - (float)hw230), ay1 = (int)(it.y - (float)hh234), ax2 = (int)(it.x + (float)hw230), ay2 = (int)(it.y + (float)hh234);
			int bx1, by1, bx2, by2; chefRect(bx1, by1, bx2, by2);
			if((float)hh22c + ground > it.y){
				if(cornerIn(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2) && !done){
					if(it.type == 0 || it.type == 1){		// cup / plate hits the chef
						Flash f; f.x = it.x; f.y = it.y; f.z = 0; f.life = flashLife; f.spr = it.type == 0 ? shardCup : shardPlate;
						flashes.push_back(f);
						stun = stunTime;				// [V] +0x1e8 = +0x1e4
						snd(0x13c, 1.0f, 1);
					}else{						// cockroach: game over
						snd(0x15d, 1.0f, 1);
						stun = stunTime * 10.0f;
						if(chefCur == chefL) chefCur = chefHitL; else if(chefCur == chefR) chefCur = chefHitR;
						spPos(chefCur, V3(chefX, chefY, chefZ));
						done = true; DBG("cockroach (flying) hit: game over");
					}
					score += it.score;
					DBG("throw type %d pts %d -> score %d (before clamp)", it.type, it.score, score);
					addPopup(it.score, it.x, it.y, it.z);
					throws.erase(throws.begin() + i);
					if(score < 0) score = 0;			// [V] clamp
				}else i++;
			}else{								// reached the floor
				if(it.type == 2){					// cockroach: starts crawling
					Roach r; r.x = it.x; r.y = (float)hh22c + ground; r.z = 0;
					r.dir = (rand15() & 1) ? -1.0f : 1.0f;
					r.spr = r.dir <= 0.0f ? roachSprL : roachSprR;
					r.timer = -1.0f;				// [A] uninitialised in the DLL (stack fill 0xcccccccc, a negative float)
					roaches.push_back(r);
				}else{
					Flash f; f.x = it.x; f.y = (float)hh22c + ground; f.z = 0; f.life = flashLife; f.spr = it.type == 0 ? shardCup : shardPlate;
					flashes.push_back(f);
					snd(0x13c, 1.0f, 1);
				}
				throws.erase(throws.begin() + i);
			}
		}
		// ---- crawling cockroaches
		for(size_t i = 0; i < roaches.size();){
			Roach &r = roaches[i];
			if(r.x < 0.0f || fw < r.x){ roaches.erase(roaches.begin() + i); continue; }	// left the screen
			spPos(r.spr, V3(r.x, r.y, r.z)); spDraw(r.spr);
			i++;
			int ax1 = (int)(r.x - (float)hw238), ay1 = (int)(r.y - (float)hh23c), ax2 = (int)(r.x + (float)hw238), ay2 = (int)(r.y + (float)hh23c);
			int bx1, by1, bx2, by2; chefRect(bx1, by1, bx2, by2);
			if(cornerIn(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2) && !done){
				snd(0x15d, 1.0f, 1);
				stun = stunTime * 10.0f;
				if(chefCur == chefL) chefCur = chefHitL; else if(chefCur == chefR) chefCur = chefHitR;
				spPos(chefCur, V3(chefX, chefY, chefZ));
				done = true; DBG("cockroach (crawling) hit: game over");
			}
		}
		// ---- shards
		for(size_t i = 0; i < flashes.size();){
			Flash &f = flashes[i];
			if(0.0f <= f.life){ spPos(f.spr, V3(f.x, f.y, f.z)); spDraw(f.spr); i++; }
			else flashes.erase(flashes.begin() + i);
		}
		// ---- 100x100 portrait of the second player (host +0xf0), the disruptor's hand, the chef, popups, HUD
		if(quadOn) drawTexQuad(portrait, quadX, quadY, quadW, quadH);
		if(heldSpr != 0){
			spPos(heldSpr, V3(disX, disY + 16.0f, 0)); spDraw(heldSpr);
		}
		spDraw(chefCur);
		drawPopups();
		drawHud();
	}

	void drawPopups(){	// DrawScorePopups @0x10035d30
		int sp = (int)(22.0f * sxW);
		for(const Popup &p : popups){
			V3 pos(p.x - sxW * 40.0f, p.y, p.z);
			if(p.score >= 1){ spPos(signPlus, pos); spDraw(signPlus); }
			else if(p.score < 0){ spPos(signMinus, pos); spDraw(signMinus); }
			drawNumber(digScore, p.x, p.y, p.z, p.score, sp);
		}
	}

	int bannerW(int h){ float w, hh; spSize(h, w, hh); return (int)w; }	// [A] host +0x8c rect width (sizes are whole pixels)

	void computeRating(){	// [V] 0x10032614: < 40 -> 0, < 110 -> 1, else 2
		rating = score < 0x28 ? 0 : score < 0x6e ? 1 : 2;
		ratingSet = true;
	}

	void drawHud(){	// DrawHud @0x10031B50
		float fw = (float)W, fh = (float)H;
		int iw = W, ih = H;
		(void)ih;
		if(0.0f < timeLeft) timeShown = (int)(std::floor(timeLeft) + 1.0f);	// [V] FUN_1006a430 = floor
		if(timeLeft <= 0.0f) timeShown = 0;
		if(timeLeft == 40.0f) timeShown = 0x28;
		if(timeLeft < 3.0f && 0.0f < timeLeft && timeShown != timeSaid){		// [V] last 3 seconds: one tick per second
			snd(0x12f, 1.0f, 1);
			timeSaid = timeShown;
		}
		int sc = score;
		float tx = sxW * 92.0f + (fw * 8.0f) / 10.0f;
		float ty = (fh + fh) / 7.0f - sxW * 10.0f;
		V3 bgPos(sxW * 92.0f + ((fw * 8.0f) / 10.0f - sxW * 38.0f), (sxW * 4.0f + (fh + fh) / 7.0f) - sxW * 10.0f, 0);
		if(timeLeft <= 10.0f){
			spPos(clockBg10, bgPos); spDraw(clockBg10);
			drawNumber(digTime10, tx, ty, 0, timeShown, (int)(27.0f * sxW));
		}else{
			spPos(clockBg, bgPos); spDraw(clockBg);
			drawNumber(digTime, tx, ty, 0, timeShown, (int)(27.0f * sxW));
		}
		float sx2 = sxW * 92.0f + ((fw * 8.0f) / 10.0f - sxW * 77.0f);
		float sy2 = (fh + fh) / 7.0f - sxW * 10.0f;
		drawNumber(digScore, sx2, sy2, 0, sc, (int)(22.0f * sxW));
		// Ready / Go
		if(t1cc < 2.0f && 0.0f < t1cc){
			int b = 0;
			if(t1cc > 2.0f || t1cc <= 1.0f){
				if(t1cc <= 1.0f && 0.0f < t1cc) b = bnGo;
			}else b = bnReady;
			V3 p((float)(iw / 2), (float)(H / 2), 0);
			spPos(b, p); spDraw(b);
			int tt = (int)t1cc;
			if(lastReadySnd != tt){
				int id = b == bnReady ? 0x12d : b == bnGo ? 0x165 : -1;
				snd(id, 1.0f, 1);
				lastReadySnd = tt;
			}
		}
		// banner after the game is over (first second): "time up" / "game over"
		if(done && 0.0f <= t1d0){
			int a = timeLeft <= 0.0f ? bnTimeA : bnGameA, c = timeLeft <= 0.0f ? bnTimeB : bnGameB;
			float wa = (float)bannerW(a), wc = (float)bannerW(c);
			spPos(a, V3(((float)(iw / 2) - sxW * 12.5f) - wa / 2.0f, (float)(H / 2), 0)); spDraw(a);
			spPos(c, V3(wc / 2.0f + sxW * 12.5f + (float)(iw / 2), (float)(H / 2), 0)); spDraw(c);
		}
		// rating banner in the second second
		if(done && t1d4 >= 0.0f && t1d0 < 0.0f){
			computeRating();
			float cxp = (float)(iw / 2), cyp = (float)(H / 2);
			if(rating == 0){
				spPos(bnR0, V3(cxp, cyp, 0)); spDraw(bnR0);
				if(!endSnd){ snd(0x135, 1.0f, 1); endSnd = true; }
			}
			if(rating == 1){
				float wa = (float)bannerW(bnR1a), wc = (float)bannerW(bnR1b);
				spPos(bnR1a, V3((cxp - sxW * 12.5f) - wa / 2.0f, cyp, 0)); spDraw(bnR1a);
				spPos(bnR1b, V3(wc / 2.0f + sxW * 12.5f + cxp, cyp, 0)); spDraw(bnR1b);
				if(!endSnd){ snd(0x134, 1.0f, 1); endSnd = true; }
			}
			if(rating == 2){
				spPos(bnR2, V3(cxp, cyp, 0)); spDraw(bnR2);
				if(!endSnd){ snd(0x15c, 1.0f, 1); endSnd = true; }
			}
		}
	}

	// ---- GetResult @0x1002FA60: param[0] = 1, param[2] = score, param[4] = rating (param[1] seat and [3] are not written by the DLL) ------------
	void getResult(Rm8MiniResult &o) override {
		if(!ratingSet) computeRating();		// [A] the DLL sets the rating while drawing the second end second; a frame-less caller (--shot) never draws it
		o.count = 1;
		o.p[0].score = score;
		o.p[0].rating = rating;
		o.p[0].seat = seat0;			// [A] the DLL leaves it to the host buffer
		showCursor(true);			// [V] +0x130(1)
	}

	// ---- keys (OnKeyDown @0x10034C40 / OnKeyUp @0x10035160) -----------------------------------------------------------------
	void keyDown(int code) override {
		switch(code){
		case ' ':
			if(mode == 0 || mode == 2){
				if(chefState == 0) chefState = 3; else if(chefState == 1) chefState = 4; else chefState = 2;
				kSpace = true;
			}
			if(introOpen) closeIntro();
			break;
		case 'a':
			if(mode == 0 || mode == 2){ chefState = chefState == 2 ? 3 : 0; kA = true; }
			break;
		case 'd':
			if(mode == 0 || mode == 2){ chefState = chefState == 2 ? 4 : 1; kD = true; }
			break;
		case RM8MK_LEFT:
			if(mode == 1 || mode == 2){ disState = 0; disLastDir = 0; kLeft = true; }
			break;
		case RM8MK_RIGHT:
			if(mode == 1 || mode == 2){ disState = 1; disLastDir = 1; kRight = true; }
			break;
		case RM8MK_ENTER:
			if(mode == 1 || mode == 2){ disState = 6; kEnter = true; }
			if(introOpen) closeIntro();
			break;
		default: break;		// 's' 'w' 0x93 0x94: empty in the DLL
		}
	}
	void keyUp(int code) override {
		switch(code){
		case ' ':
			if(mode == 0 || mode == 2){
				if(chefState == 2) chefState = 7; else if(chefState == 3) chefState = 0; else if(chefState == 4) chefState = 1;
				kSpace = false;
			}
			break;
		case 'a':
			if(mode == 0 || mode == 2){ if(chefState == 0) chefState = 7; kA = false; }
			break;
		case 'd':
			if(mode == 0 || mode == 2){ if(chefState == 1) chefState = 7; kD = false; }
			break;
		case RM8MK_LEFT:
			if(mode == 1 || mode == 2){ if(disState == 0){ disState = 7; disLastDir = 7; } kLeft = false; }
			break;
		case RM8MK_RIGHT:
			if(mode == 1 || mode == 2){ if(disState == 1){ disState = 7; disLastDir = 7; } kRight = false; }
			break;
		case RM8MK_ENTER:
			if(mode == 1 || mode == 2){
				if(disState == 6){
					disState = 7;
					if(disLastDir == 0) disState = 0;
					if(disLastDir == 1) disState = 1;
				}
				kEnter = false;
			}
			break;
		default: break;
		}
	}

	// [A] touch overlay: chef A / D / Space; the second human (mode 2) or the human disruptor (mode 1): Left / Right / Enter on separate buttons
	void touchButtons(std::vector<MgTouchBtn> &o) override {
		if(mode == 0 || mode == 2){
			o.push_back({20, 610, 130, 130, 'a', MG_SHAPE_LEFT});
			o.push_back({160, 610, 130, 130, 'd', MG_SHAPE_RIGHT});
			o.push_back({mode == 2 ? 300.0f : 860.0f, 610, 130, 130, ' ', MG_SHAPE_UP});
		}
		if(mode == 1 || mode == 2){
			o.push_back({580, 610, 130, 130, RM8MK_LEFT, MG_SHAPE_LEFT});
			o.push_back({720, 610, 130, 130, RM8MK_RIGHT, MG_SHAPE_RIGHT});
			o.push_back({870, 610, 130, 130, RM8MK_ENTER, MG_SHAPE_CIRCLE});
		}
	}
};
}	// namespace

MgGame *mgNew18(){ return new G18; }
