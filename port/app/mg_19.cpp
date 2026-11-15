// Minigame 19 (ltlgame_19.dll): MemTreasure / <str#5315> - the penguin digs the treasures the player remembers.  Transliteration of the DLL on the mg:: host
// services.  Tags: [V] verified in the decompile / disassembly (addresses are DLL addresses), [H] hypothesis, [A] port adaptation.
//
// Map of the DLL (names from out/ghidra_lg_named/ltlgame_19.dll):
//   Game_Ctor @10003bd0, Init @10001d00, Finish @10002500, Update @10002710, Draw @10002960, OnKey @10003d20, OnMouseButton @10001290 (not in the decompile, disassembly),
//   StartGame @10001a70, ReadyGo @100011a0, EndMemoryPhase @10001650, ApplyPendingClick @10001670, Score_Add @10001090,
//   StateMachine: SetState @10006f90 (durations), Tick @10006f40, OnStateStart @100017f0, OnStateTimeout @10003060 (disassembly, jump tables @10001a38 / @100035d0),
//   Grid: Create @10004500, PlanShuffle @10004900, Update @10004a80, SwapCells @10004210, NeighborFree @100043e0, Cell_Index @10004070, RecordAt @100040c0,
//         ContainsPoint @10004460, ShowLayer @10003f80, DrawPlay @10004020,
//   Penguin: Ctor @100061c0, Init @100053b0, BeginWalkIn @10001030, SetTarget @100051c0, StepToTarget @10005db0, Update @100062d0, SetPose @10005090, Draw @10005c30,
//            GetScreenPos @10004eb0, GetDigPoint @10004f20, CellUnderDigPoint @10004fa0, EnqueueCmd @10006810, OnCommandDone @10005d30, DigItem @10006890,
//            UpdateCombo @10006950, UpdateComboTally @100056c0, ComputeFinalScore @10005a00, DrawComboBonus @10005ab0, DrawHistory @10005670, Score_Commit @10004f10,
//   HUD: Create @10007230, DrawTime @10007770, DrawTimeRed @10007810, DrawScore @100078b0, DrawValueBubble @100079e0, Update @10001760,
//   particles: Spawn @10007df0, Update @10007f20, Draw @10007bc0, sound object: Play @10006bb0 / StopAll @10006b10.
//
// Facts found while reading the code that differ from docs/minigames/lg_19.md:
//   * Grid+0xd48 (the number of shuffle rounds) IS initialised: Grid_Create stores 8 at @100048e3 (the decompile hid it as an "unreachable block"), so the memory phase
//     really shuffles: 8 rounds of 0.4 s, then EndMemoryPhase (state 4 -> 5) after about 3.3 s instead of 4.0 s.
//   * state 1 (intro) does NOT tick the countdown: Update returns right after Penguin_Update (@10002741 .. 1000274f).
//   * the project() sign / scale: sx = 512 - x / z * 1706.7, sy = 384 - y / z * 1706.7 (view window 0.3 x 0.225 as mg_3d.cpp has it; GUIDE.md quotes 341.3 / 512).
//
// Readings of the uninitialised / odd state of the DLL (each reproduced as the code behaves):
//   * Penguin+0x80 (the settled final score, game+0xa8) is set to 0 by Penguin_Ctor @10006238 and only written by ComputeFinalScore (state 14 start).  A game without any
//     combo goes 12/13 -> 15 (@10003551 / @1000355b test the combo list size), Score_Commit then copies 0 into the display score: the reported score is 0 (and no reward).
//   * The colour passed to Sprite_SetColor by UpdateComboTally has bytes {0xff, 0xff, <never written>, alpha}: the red byte is stack residue in the DLL; [A] 0xff here.
//   * ApplyPendingClick drops a buffered click on an already dug cell but does not clear the buffer, so the stale click is looked at again after the next dig (and walks
//     there once the cell was reset by the 3.8 s rule): reproduced.
//   * The 3.8 s rule (Grid_Update @10004ccf): a dug cell is reset to an empty covered cell (kind 0, flags 0, the sprite handle kept, cover back to frame 0 / state 0).
//   * Sound 0x15c (rating 2) is played with loops = 0 = forever (@100019d9) and stays in the StopAll list; it ends with the game (getResult stops it).
//   * Cheat "jema" (Enter, j, e, m, a, Enter): OnKey @10003d20 pops 'a', then 'm' if it is there, then 'e', and finally compares the last key with 'j': so "xjea"
//     (without the 'm') is accepted too.  Typing it while the penguin walks in (state 2) clears the walk command and the game never leaves state 2.
//   * state 7 has a 40 s timer whose timeout does nothing; states 6, 10, 11, 16 are never entered (16: the only entry would be a 0.3 s state that returns to the previous one).
//   * the DLL reseeds rand() with timeGetTime() in Grid_Create and in every PlanShuffle round; [A] the host's per game seed is kept (RM8_MINI_SEED reproduces a game).
//   * host +0x38 before the penguin draws is RwCameraClear(camera, 0, rwCAMERACLEARZ) (Z buffer only): not exposed by mg::, the one model per frame needs no clear.
//   * host +0x94 (Article_GetVector2 @0x462f60) is called on the explosion sprite in the trap path and on the HUD sprites but its result is never used.
//
// Test hooks of this file (environment, all optional):
//   RM8_MG19_LOG=1             log state changes, digs, combos to stderr
//   RM8_MG19_LAYOUT=<30 chars> forced board (applied when the covers go down, after the memory phase shuffle), row by row (6 columns x 5 rows): A = 15 point treasure (0x646), B = 7 (0x645), C = 3 (0x64a),
//                              T = trap (0x647), '.' = empty; example "AABBCC.....T..CCCCCCAAAAAAA..BBBB"
//   RM8_MG19_BOT=c,r;c,r;...   a bot clicks these cells (column,row) one after another whenever the penguin is idle in state 7 (it moves a private mouse position,
//                              which the highlight / click code also reads); "RM8_MG19_BOT=rand" clicks random cells
//   RM8_MG19_START=s           the game starts by itself (space key) after s game seconds
// [A] float arithmetic is done in single precision (the DLL's x87 code rounds intermediate results differently; ints coming out of ftol can differ by one in rare cases).
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
inline int ftol(float f){ return (int)f; }	// [V] FUN_10025328 = __ftol (truncates toward zero)
inline int ftold(double f){ return (int)f; }

// constants of the data section
const float K_SCALE = fbits(0x3aa3d70a);	// [V] 0x10033218 = 0.00125 (scale = width * 0.00125)
const float K_T30 = 30.0f;			// [V] 0x10033214
const float K_T25 = 25.0f;			// [V] 0x1003320c
const float K_T40 = 40.0f;			// [V] 0x1003a0cc
const float K_T550 = 550.0f;			// [V] 0x10033244
const float K_T90 = 90.0f;			// [V] 0x100331cc
const float K_HALF = 0.5f;			// [V] 0x100331ac

// ---- a cell record (0x38 bytes): A = the item, B = the cover (Grid_Ctor @10001150, Grid_Create @10004500)
struct Rec {
	int spr = 0;			// +0x00 sprite handle
	int kind = 0;			// +0x04 1 empty, 2 trap, 3 / 4 / 5 treasure (3 point, 15 point, 7 point)
	int value = 0;			// +0x08
	bool vis = false;		// +0x0c drawn
	bool trap = false;		// +0x0d
	bool dug = false;		// +0x0e (compared with 1)
	float timer = 0;		// +0x10 (cover records: time since the cell was dug)
	int a14 = 0xff;			// +0x14
	int dir = 0;			// +0x18 shuffle mark: direction of the slide planned for this cell (0 none)
	V3 pos;				// +0x1c..0x24 the centre of the cell this record sits in
	int pc = -1, pr = -1, pz = -1;	// +0x28..0x30 the partner cell of the planned swap
	int dir2 = 0;			// +0x34 the partner's direction mark
};
struct Hist { int kind = 0, value = 0, spr = 0; bool flag = false; float x = 0, y = 0, z = 0; int alpha = 0xff; int chain = 0; };	// 9 dwords (DigItem @10006890)
struct Part { bool on = false; int spr = 0; float cx = 0, cy = 0, tx = 0, ty = 0, hold = 0, speed = 0, sx = 0, sy = 0; int value = -1000; float total = 0; };	// 0x30 bytes at +0x4e4

struct G19 : MgGame {
	std::vector<Rm8MiniPlayer> players;
	// ======================================================================================================================== game object
	float s = 1;			// +0x24 scale = width * 0.00125
	float fW = 1024, fH = 768;	// +0x1ec / +0x1f0
	int state = 0, prevState = 0;	// +0x08 / +0x0c (state machine sub-object at +4)
	float stTimer = 0, stDur = 0;	// +0x18 remaining (sub-object +0x14) / +0x14 (sub-object +0x10)
	bool finished = false;		// +0x20
	float timer = 35.0f;		// +0xc0 countdown
	float coverKeep = 3.8f;		// +0xbc
	bool timeUp = false;		// +0x1cc
	float pendX = -1, pendY = -1;	// +0x1d0 / +0x1d4 buffered click
	bool cheat = false;		// +0x1f4
	int hitSpr = 0;			// +0x1f8 the sprite of the last dug trap (animated during state 8)
	float f21c = 0;			// +0x21c
	int secCnt = 34;		// +0x220
	bool readySnd = true, goSnd = true;	// +0x224 / +0x23a
	bool mouseHeld = false;		// +0x239
	bool cheatRec = false;		// +0x238
	std::vector<int> cheatBuf;	// +0x228..0x230
	int rating = 0;			// +0x23c
	bool startOk = true;		// +0x240
	int sprCursor = 0, sprBoom = 0, sprReady = 0, sprGo = 0, sprOver1 = 0, sprOver2 = 0, sprRes0 = 0, sprResA = 0, sprResB = 0, sprRes2 = 0;	// +0xc4 +0x1d8 +0x1fc +0x200 +0x204 +0x208 +0x20c +0x214 +0x210 +0x218
	bool introShown = false;
	std::vector<int> sndList;	// the sound object's list of looping handles (StopAll)
	float iconSpeed = 800.0f, digitSpeed = 1500.0f;	// DAT_1003a070 / DAT_1003a06c (800 and 1500, multiplied by the scale in Init)

	// ======================================================================================================================== grid (0xd4c bytes)
	Rec cell[6][5][2];		// [col][row][0 item A / 1 cover B]  (record index = z + (row + col * 5) * 2)
	int cw = 0, ch = 0;		// +0xd28 / +0xd2c cell size (ints)
	float gx0 = 0, gy0 = 0;		// +0xd30 / +0xd34 origin
	float gvx = 0, gvy = 0, gtime = -1.0f;	// +0xd38 / +0xd3c slide speed, +0xd40 round timer
	int gRounds = 8;		// +0xd48

	// ======================================================================================================================== HUD (0x190 bytes)
	int hBg[4] = {};		// +0xac..+0xb8 background tiles 0x641 0x64c 0x64d 0x64e
	int hBubble = 0;		// +0xbc 0x852
	int hFrame = 0, hFrameRed = 0;	// +0x14 / +0x18
	int hWhite[10] = {}, hRed[10] = {}, hScore[10] = {}, hPart[10] = {};	// +0x1c.. / +0x44.. / +0x84.. / +0xc0 + 0x14 * d
	V3 hT, hS;			// +0x6c time digits position / +0x78 score position
	float hOff188 = 0, hOff18c = 0;	// +0x188 / +0x18c

	// ======================================================================================================================== penguin (embedded at game + 0x28)
	int seat = 0;			// +0x00
	int m0 = 0, m1 = 0;		// +0x08 model 0x642, +0x0c model 0x643
	int sprPick = 0;		// +0x10 sprite 0x649
	V3 pos;				// +0x20 world position
	float speed = 70.0f;		// +0x2c
	int curCmd = 0;			// +0x30
	float score = 0;		// +0x34 (float)
	std::vector<int> queue;		// +0x38 command list (front = newest)
	int pose = 0;			// +0x44
	bool facing = true;		// +0x48
	std::vector<Hist> hist;		// +0x4c history of dug treasures (front = newest)
	std::vector<Hist> combos;	// +0x58 combo list
	float adjX = -1, adjY = -1, tgtX = 0, tgtY = 0;	// +0x64 / +0x68 / +0x6c / +0x70
	float toolX = 0, toolY = 0;	// +0x74 / +0x78
	int finalScore = 0;		// +0x80 (int)
	bool f84 = false, f85 = false, f86 = false, f87 = true;	// +0x84..+0x87
	float startX = 0, startY = 0, total = 0;	// +0x88 / +0x8c / +0x90

	Part parts[25];			// particle system (0x994 bytes; the 10 float-text slots are never used)

	// ---- test hooks
	bool logOn = false;
	bool botOn = false, botRand = false; std::vector<std::pair<int, int>> botCells; size_t botNext = 0; float botWait = 0;
	bool fakeMouse = false; float fmx = 0, fmy = 0;
	float startAt = -1;
	const char *layoutEnv = nullptr;	// applied when the covers go down (state 3), after the memory phase shuffle

#define LOG19(...) do{ if(logOn){ fprintf(stderr, "[mg19 t=%.2f] ", mg::now()); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } }while(0)

	// ======================================================================================================================== sound object (Play @10006bb0)
	// [V] local id -> host id; the host call is +0x114 (id, position (300,300,0), volume 100, loops); a sound played with loops == 0 is remembered for StopAll
	void sndPlay(int id, int loops)
	{
		int h;
		switch(id){
		case 0: h = 0x14b; break; case 1: h = 0x14c; break; case 2: h = 0x14d; break;
		case 3: h = 0x150; break; case 4: h = 0x14f; break; case 5: h = 0x15b; break; case 6: h = 0x151; break;
		case 0x14f: h = 0x12d; break; case 0x150: h = 0x12e; break; case 0x151: h = 0x12f; break; case 0x152: h = 0x130; break;
		case 0x153: h = 0x131; break; case 0x154: h = 0x132; break; case 0x155: h = 0x161; break; case 0x156: h = 0x134; break; case 0x157: h = 0x135; break;
		default: h = id; break;		// 0x14e, 0x15a (volume 50), 0x15c, 0x165 ... unchanged
		}
		int hd = mg::snd(h, id == 0x15a ? 50.0f : 100.0f, loops);	// [V] the host clamps the volume to 1.0 (CMiniGameHost_2_vf69 @0x510C30)
		if(loops == 0) sndList.push_back(hd);
	}
	void sndStopAll(){ for(int hd : sndList) if(hd) mg::sndStop(hd); sndList.clear(); }	// [V] @10006b10

	// ======================================================================================================================== grid helpers
	Rec *recordAt(int ix, int iy, int z)	// [V] RecordAt @100040c0 (inclusive bounds, float compares)
	{
		if((float)ix < gx0) return nullptr;
		if((float)ix > (float)(cw * 6) + gx0) return nullptr;
		if((float)iy < gy0) return nullptr;
		if((float)iy > (float)(ch * 5) + gy0) return nullptr;
		int col, row; cellIndex(ix, iy, col, row);
		if(col > 5 || row > 4) return nullptr;
		return &cell[col][row][z];
	}
	void cellIndex(int ix, int iy, int &col, int &row)	// [V] Cell_Index @10004070: ftol((x - x0) / cw), ftol((y - y0) / ch) (x87: extended precision)
	{
		col = ftold(((double)ix - (double)gx0) / (double)cw);
		row = ftold(((double)iy - (double)gy0) / (double)ch);
	}
	bool containsPoint(float x, float y) const	// [V] ContainsPoint @10004460
	{
		return !(x < gx0) && !((float)(cw * 6) + gx0 < x) && !(y < gy0) && !((float)(ch * 5) + gy0 < y);
	}
	float cellCX(int col) const { return (float)(cw * col + cw / 2) + gx0; }	// [V] fild(cw * col), fiadd(cw / 2), fadd(x0)
	float cellCY(int row) const { return (float)(ch * row + ch / 2) + gy0; }
	static void blank(Rec &r){ int sp = r.spr; r = Rec(); r.spr = sp; }	// [V] the blank record pattern (kind 0, +0x14 = 0xff, partner -1) with the sprite handle kept

	void gridCreate()	// [V] Grid_Create @10004500
	{
		cw = ch = ftol(s * 64.0f);			// [V] 10004548: ftol(64 * scale)
		gx0 = 90.0f * s; gy0 = 160.0f * s;		// [V] 0x1003a060 = 90, 0x1003a064 = 160
		// [A] the DLL reseeds with srand(timeGetTime() * 0x7fbf) here; the port keeps the per-game seed of the host (RM8_MINI_SEED) so that a game can be reproduced
		int n15 = 0, nTrap = 0, n3 = 0, n7 = 0;
		for(int col = 0; col < 6; col++) for(int row = 0; row < 5; row++){
			Rec &A = cell[col][row][0], &B = cell[col][row][1];
			B = Rec();
			B.spr = mg::spNew(0x644);			// [V] 100045e8: the cover
			mg::spScale(B.spr, {s, s, 1.0f});
			mg::spSetLoops(B.spr, 0); mg::spSetState(B.spr, 0); mg::spSetFrame(B.spr, 0);
			B.vis = true;
			V3 p(cellCX(col), cellCY(row), 0);
			mg::spPos(B.spr, p); B.pos = p;
			A = Rec();
			int art = 0;
			for(;;){					// [V] 100046b7: rand() % 110, redraw when the kind is used up
				int r = mg::rand15() % 110;
				if(r >= 0 && r < 20){ if(n15 + 1 > 3) continue; n15++; art = 0x646; A.kind = 4; A.value = 15; break; }
				if(r >= 20 && r < 40){ if(nTrap + 1 > 6) continue; nTrap++; art = 0x647; A.kind = 2; A.value = -1; A.trap = true; break; }
				if(r >= 40 && r < 75){ if(n3 + 1 > 11) continue; n3++; art = 0x64a; A.kind = 3; A.value = 3; break; }
				if(r >= 75 && r < 100){ if(n7 + 1 > 8) continue; n7++; art = 0x645; A.kind = 5; A.value = 7; break; }
				art = 0; A = Rec(); A.kind = 1; break;	// 100..109: an empty cell (blank record, no sprite)
			}
			if(art){
				A.spr = mg::spNew(art);
				mg::spScale(A.spr, {s, s, 1.0f});
				A.vis = true;
				mg::spPos(A.spr, p); A.pos = p;
			}
		}
		gtime = -1.0f; gvy = gvx = 0;			// [V] 100048cd: +0xd40 = -1, +0xd3c = +0xd38 = 0
		gRounds = 8;					// [V] 100048e3: +0xd48 = 8
	}

	void applyLayout(const char *lay)			// test hook: replace the board
	{
		int n = 0;
		for(const char *c = lay; *c && n < 30; c++){
			if(*c == '/' || *c == ' ') continue;
			int row = n / 6, col = n % 6; n++;
			Rec &A = cell[col][row][0];
			if(A.spr){ mg::spFree(A.spr); }
			int sp = 0, kind = 1, val = 0; bool trap = false;
			switch(*c){
			case 'A': sp = mg::spNew(0x646); kind = 4; val = 15; break;
			case 'B': sp = mg::spNew(0x645); kind = 5; val = 7; break;
			case 'C': sp = mg::spNew(0x64a); kind = 3; val = 3; break;
			case 'T': sp = mg::spNew(0x647); kind = 2; val = -1; trap = true; break;
			default: break;
			}
			V3 p = cell[col][row][1].pos;
			A = Rec(); A.spr = sp; A.kind = kind; A.value = val; A.trap = trap; A.vis = sp != 0; A.pos = p;
			if(sp){ mg::spScale(sp, {s, s, 1.0f}); mg::spPos(sp, p); }
		}
	}

	// ShowLayer @10003f80: 1 = all covers, 0 = the items with the visible flag
	void showLayer(int layer)
	{
		for(int col = 0; col < 6; col++) for(int row = 0; row < 5; row++){
			if(layer == 1) mg::spDraw(cell[col][row][1].spr);
			else if(layer == 0 && cell[col][row][0].vis) mg::spDraw(cell[col][row][0].spr);
		}
	}
	void drawPlay()	// [V] DrawPlay @10004020: per cell the item (visible flag) and then the cover (visible flag)
	{
		for(int col = 0; col < 6; col++) for(int row = 0; row < 5; row++){
			if(cell[col][row][0].vis) mg::spDraw(cell[col][row][0].spr);
			if(cell[col][row][1].vis) mg::spDraw(cell[col][row][1].spr);
		}
	}

	bool neighborFree(int col, int row, int dir)	// [V] NeighborFree @100043e0
	{
		switch(dir){
		case -2: row += 1; break; case -1: col -= 1; break; case 1: col += 1; break; case 2: row -= 1; break;
		default: return false;
		}
		if(col < 6 && col > -1 && row < 5 && row > -1) return cell[col][row][0].dir == 0;
		return false;
	}
	void planShuffle()	// [V] PlanShuffle @10004900
	{
		gtime = fbits(0x3ecccccd);					// 0.4 s per round
		gvx = (float)cw * 2.5f; gvy = (float)ch * 2.5f;			// 2.5 cells per second
		// [A] srand(timeGetTime()) is not repeated (see gridCreate)
		for(int col = 0; col < 6; col++) for(int row = 0; row < 5; row++){
			Rec &R = cell[col][row][0];
			if(R.dir != 0) continue;
			int d = mg::rand15() % 5;
			if(d == 3) d = -1; else if(d == 4) d = -2;
			if(d == -R.dir2) d = -d;
			if(d == 0) continue;
			if(!neighborFree(col, row, d)) continue;
			int nc = col, nr = row;
			switch(d){ case 1: nc = col + 1; break; case 2: nr = row - 1; break; case -2: nr = row + 1; break; case -1: nc = col - 1; break; }
			R.dir2 = d; R.dir = d;
			Rec &P = cell[nc][nr][0];
			P.dir2 = -d; P.dir = -d;
			R.pc = nc; R.pr = nr; R.pz = 0;
			P.pc = col; P.pr = row; P.pz = 0;
		}
	}
	void swapCells(int c1, int r1, int z1, int c2, int r2, int z2)	// [V] SwapCells @10004210
	{
		if(c1 > 5 || c1 < 0 || c2 > 5 || c2 < 0) return;
		if(r1 > 4 || r1 < 0 || r2 > 4 || r2 < 0) return;
		Rec t = cell[c1][r1][z1]; cell[c1][r1][z1] = cell[c2][r2][z2]; cell[c2][r2][z2] = t;
		V3 p1(cellCX(c1), cellCY(r1), 0), p2(cellCX(c2), cellCY(r2), 0);
		cell[c1][r1][z1].pos = p1; cell[c2][r2][z2].pos = p2;
		if(gRounds == 0){ mg::spPos(cell[c1][r1][z1].spr, p1); mg::spPos(cell[c2][r2][z2].spr, p2); }
	}
	void gridUpdate(float dt)	// [V] Grid_Update @10004a80
	{
		if(state == 4){
			if(gRounds < 0) endMemoryPhase();
			else {
				gtime = gtime - dt;
				if(!(gtime < 0.0f)){					// slide the marked cells
					for(int col = 0; col < 6; col++) for(int row = 0; row < 5; row++){
						Rec &R = cell[col][row][0];
						if(R.dir == 0) continue;
						switch(R.dir){
						case 1: R.pos.x = dt * gvx + R.pos.x; break;
						case 2: R.pos.y = R.pos.y - dt * gvy; break;
						case -2: R.pos.y = dt * gvy + R.pos.y; break;
						case -1: R.pos.x = R.pos.x - dt * gvx; break;
						}
						mg::spPos(R.spr, R.pos);
					}
				}else{							// the round is over: swap the planned pairs
					for(int col = 0; col < 6; col++) for(int row = 0; row < 5; row++){
						if(cell[col][row][0].dir == 0) continue;
						int pc = cell[col][row][0].pc, pr = cell[col][row][0].pr, pz = cell[col][row][0].pz;
						swapCells(col, row, 0, pc, pr, pz);
						cell[col][row][0].dir = 0;
						cell[pc][pr][pz].dir = 0;
					}
					gRounds--;
					planShuffle();
				}
			}
		}
		// the dug cells: the cover breaks, after 3.8 s the cell is reset to an empty covered one
		for(int col = 0; col < 6; col++) for(int row = 0; row < 5; row++){
			Rec &A = cell[col][row][0], &B = cell[col][row][1];
			if(!A.dug) continue;
			B.timer = dt + B.timer;
			float t = B.timer;
			if(cheat){
				int f = ftol(t * 10.0f);			// [V] 10004c67: frame = ftol(timer * 10)
				if(f < 0 || f > 5){				// [V] 10004da7: the cell is reset at once and Update returns
					blank(A); B.timer = 0; mg::spSetFrame(B.spr, 0); mg::spSetState(B.spr, 0);
					return;
				}
				if(f != 0){ mg::spSetFrame(B.spr, f); mg::spUpdate(B.spr, dt); }
			}else if(state != 0xc && state != 0xe && state != 0xf) mg::spUpdate(B.spr, dt);
			if(B.timer > coverKeep){				// [V] 10004ccf: timer > game+0xbc (3.8 s)
				blank(A); B.timer = 0; mg::spSetFrame(B.spr, 0); mg::spSetState(B.spr, 0);
			}
		}
	}

	// ======================================================================================================================== HUD
	void hudCreate()	// [V] Hud_Create @10007230
	{
		V3 sc3(s, s, 1.0f);
		float w1, h1, w2, h2, w3, h3, w4, h4;
		hBg[0] = mg::spNew(0x641); mg::spScale(hBg[0], sc3); mg::spSize(hBg[0], w1, h1);
		mg::spPos(hBg[0], {w1 * s * K_HALF, h1 * s * K_HALF, 0});
		hBg[1] = mg::spNew(0x64c); mg::spScale(hBg[1], sc3); mg::spSize(hBg[1], w2, h2);
		mg::spPos(hBg[1], {(w2 * K_HALF + w1) * s, h2 * s * K_HALF, 0});
		hBg[2] = mg::spNew(0x64d); mg::spScale(hBg[2], sc3); mg::spSize(hBg[2], w3, h3);
		mg::spPos(hBg[2], {w3 * s * K_HALF, (h3 * K_HALF + h1) * s, 0});
		hBg[3] = mg::spNew(0x64e); mg::spScale(hBg[3], sc3); mg::spSize(hBg[3], w4, h4);
		mg::spPos(hBg[3], {(w4 * K_HALF + w3) * s, (h4 * K_HALF + h1) * s, 0});
		hBubble = mg::spNew(0x852); mg::spScale(hBubble, sc3);
		hFrame = mg::spNew(0x83f); mg::spScale(hFrame, sc3);
		V3 P(fW * fbits(0x3f4ccccd) - s * 38.0f + s * 92.0f, fH * fbits(0x3e924925) + s * 4.0f - s * 10.0f, 0);	// [V] 100074d5..: 0.8 W - 38 s + 92 s, 0.2857143 H + 4 s - 10 s
		mg::spPos(hFrame, P);
		hWhite[0] = mg::spNew(0x83e); mg::spScale(hWhite[0], sc3);
		hT = P; hT.x = s * K_T25 + hT.x; hT.y = hT.y - s * 5.0f;	// [V] 1000754c..: time digits position
		for(int d = 1; d <= 9; d++){ hWhite[d] = mg::spNew(0x835 + d - 1); mg::spScale(hWhite[d], sc3); mg::spPos(hWhite[d], hT); }
		mg::spPos(hWhite[0], hT);	// (the DLL does not position digit 0 here; DrawTime sets the position before every draw)
		hFrameRed = mg::spNew(0x84a); mg::spScale(hFrameRed, sc3); mg::spPos(hFrameRed, P);
		hRed[0] = mg::spNew(0x849); mg::spScale(hRed[0], sc3);
		for(int d = 1; d <= 9; d++){ hRed[d] = mg::spNew(0x840 + d - 1); mg::spScale(hRed[d], sc3); mg::spPos(hRed[d], hT); }
		hS = hT; hS.x = hS.x - s * K_T40; hS.y = s * 5.0f + hS.y;	// [V] 10007636..: score position
		hScore[0] = mg::spNew(0x85c); mg::spScale(hScore[0], sc3);
		for(int d = 1; d <= 9; d++){ hScore[d] = mg::spNew(0x853 + d - 1); mg::spScale(hScore[d], sc3); }
		for(int d = 0; d < 10; d++) mg::spPos(hScore[d], P);
		hPart[0] = mg::spNew(0x85c); mg::spScale(hPart[0], sc3);
		for(int d = 1; d <= 9; d++){ hPart[d] = mg::spNew(0x853 + d - 1); mg::spScale(hPart[d], sc3); }
		hOff188 = s * 27.0f; hOff18c = s * 22.0f;	// [V] Hud_Ctor @10007af0 (+0x188 = 27 scale, +0x18c = 22 scale)
	}
	void drawTwo(const int *sprs, int n)		// DrawTime @10007770 / DrawTimeRed @10007810: tens digit at T, units digit 27 * scale to the right
	{
		int tens = n / 10, units = n % 10;
		V3 p = hT;
		mg::spPos(sprs[tens], p); mg::spDraw(sprs[tens]);
		p.x = p.x + hOff188;
		mg::spPos(sprs[units], p); mg::spDraw(sprs[units]);
	}
	void hudDrawScore(int n)	// [V] DrawScore @100078b0: right aligned at S, digits 22 * scale apart; leading zeros are not drawn
	{
		int h = n / 100, t = n / 10; if(t > 9) t = t % 10;
		int u = n % 10;
		V3 p = hS;
		mg::spPos(hScore[u], p); mg::spDraw(hScore[u]);
		if(t > 0 || h > 0){
			p.x = p.x - hOff18c;
			mg::spPos(hScore[t], p); mg::spDraw(hScore[t]);
			if(h > 0){
				p.x = p.x - hOff18c;
				mg::spPos(hScore[h], p); mg::spDraw(hScore[h]);
			}
		}
	}
	void hudDrawBubble(int value, V3 p)	// [V] DrawValueBubble @100079e0: the bubble sprite at p, then the digits left to right from p.x + 22 s * s
	{
		mg::spPos(hBubble, p); mg::spDraw(hBubble);
		int h = value / 100, t = value / 10; if(t > 9) t = t % 10;
		int u = value % 10;
		if(h > 0){ p.x = hOff18c * s + p.x; mg::spPos(hScore[h], p); mg::spDraw(hScore[h]); }
		if(t > 0 || h > 0){ p.x = hOff18c * s + p.x; mg::spPos(hScore[t], p); mg::spDraw(hScore[t]); }
		p.x = hOff18c * s + p.x; mg::spPos(hScore[u], p); mg::spDraw(hScore[u]);
	}
	void hudUpdate()	// [V] Hud_Update @10001760
	{
		if(state == 4 || state == 5 || state == 3 || state == 2) drawTwo(hWhite, 35);
		else if(!(timer <= -1.0f)){
			if(timer <= 6.0f) drawTwo(hRed, ftol(timer));
			else drawTwo(hWhite, ftol(timer));
		}
		hudDrawScore(ftol(score));
	}

	// ======================================================================================================================== particles
	void spawn(int spr, float x0, float y0, float tx, float ty, float spd, int value)	// [V] Particle_Spawn @10007df0
	{
		for(int i = 0; i < 25; i++){
			if(parts[i].on) continue;
			Part &p = parts[i];
			p.cx = p.sx = x0; p.cy = p.sy = y0; p.tx = tx; p.ty = ty;
			p.spr = spr; p.speed = spd; p.hold = 0; p.on = true; p.value = value;
			float dx = tx - x0, dy = ty - y0;
			p.total = sqrtf(dy * dy + dx * dx);		// the DLL adds y^2 + x^2 (pow loops) then fsqrt
			if(value != -1000) sndPlay(0x15a, 1);
			return;
		}
	}
	void particleUpdate(float dt)	// [V] Particle_Update @10007f20 (the ten float text slots at +0x78.. are never started)
	{
		for(int i = 0; i < 25; i++){
			Part &p = parts[i];
			if(!(p.on && p.hold == 0.0f)) continue;
			float dx = p.tx - p.cx, dy = p.ty - p.cy;		// RwV3dNormalize(dir) then pos += dir * speed * dt
			float len = sqrtf(dx * dx + dy * dy), nx = 0, ny = 0;
			if(len > 0){ nx = dx / len; ny = dy / len; }
			p.cx = nx * p.speed * dt + p.cx; p.cy = ny * p.speed * dt + p.cy;
			float mx = p.cx - p.sx, my = p.cy - p.sy;
			float d = sqrtf(my * my + mx * mx);
			if(!(d > p.total) && d != p.total) continue;		// [V] arrived when the distance from the start >= the total distance
			mg::spPos(p.spr, {p.tx, p.ty, 0});
			if(cheat || (state != 0xc && state != 0xd)){ if(p.value == -1000) updateCombo(); }	// [V] 10008659..1000867a
			if(hS.x == p.tx && hS.y == p.ty){			// reached the score board
				mg::spPos(p.spr, {-100.0f, 0, 0});
				score = (float)p.value + score;			// Score_Add @10001090
				sndPlay(0x14e, 1);
			}
			p.on = false;
		}
	}
	void particleDraw()	// [V] Particle_Draw @10007bc0
	{
		for(int i = 0; i < 25; i++){
			Part &p = parts[i];
			if(!p.on) continue;
			mg::spPos(p.spr, {p.cx, p.cy, 0}); mg::spDraw(p.spr);
		}
	}

	// ======================================================================================================================== penguin
	void screenPos(float &sx, float &sy)	// [V] GetScreenPos @10004eb0: the projected position, y minus 30 * scale
	{
		mg::project(pos, sx, sy);
		sy = sy - s * K_T30;
	}
	void digPoint(float &x, float &y)	// [V] GetDigPoint @10004f20
	{
		float px, py; mg::project(pos, px, py);
		py = py - s * K_T30;
		x = facing ? px - toolX : px + toolX;
		y = py - toolY;
	}
	Rec *cellUnderDigPoint()	// [V] CellUnderDigPoint @10004fa0 (the item record A of the cell, or null)
	{
		float dx, dy; digPoint(dx, dy);
		if(!(dx < gx0) && !((float)(cw * 6) + gx0 < dx) && !(dy < gy0) && !((float)(ch * 5) + gy0 < dy))
			return recordAt(ftol(dx), ftol(dy), 0);
		return nullptr;
	}
	void setTarget(float x, float y)	// [V] SetTarget @100051c0
	{
		int col, row; cellIndex(ftol(x), ftol(y), col, row);
		if(containsPoint(x, y)){ x = cellCX(col); y = cellCY(row); }	// snap to the cell centre
		float sx, sy; mg::project(pos, sx, sy);
		sy = sy - s * K_T30;
		adjY = y; adjX = x; tgtY = y; tgtX = x;
		// [V] 100052d8: sx >= x (not less): the pickaxe side is on the target's right (x + offset), else x - offset
		if(!(sx < x)) adjX = x + toolX; else adjX = x - toolX;
		adjY = toolY + adjY;
		mg::project(pos, sx, sy);
		startX = sx; startY = sy - s * K_T30;
		float ddx = adjX - startX, ddy = adjY - startY;
		total = sqrtf(ddy * ddy + ddx * ddx);
	}
	void enqueue(int cmd)	// [V] EnqueueCmd @10006810
	{
		if(cmd == 2) facing = true; else if(cmd == 3) facing = false;
		if(queue.empty()) sndPlay(0, 0);	// the walking loop
		queue.insert(queue.begin(), cmd);
		curCmd = queue.front();
	}
	void clearQueue(){ sndStopAll(); queue.clear(); }	// [V] ClearQueue @10005b90
	void onCommandDone(int cmd)	// [V] OnCommandDone @10005d30
	{
		if(queue.empty()){
			if(state == 7){
				pose = 0;
				mg::mdAction(facing ? m0 : m1, 3, 0);
			}
		}else{
			queue.erase(std::remove(queue.begin(), queue.end(), cmd), queue.end());
			if(!queue.empty()){ curCmd = queue.front(); return; }
			sndStopAll();
		}
	}
	void digSounds()	// the four dig sounds (id 2, loops 4; cheat: 3)
	{
		int n = cheat ? 3 : 4;
		for(int i = 0; i < 4; i++) sndPlay(2, n);
	}
	void setPose(int p)	// [V] SetPose @10005090 (jump table @100051a0)
	{
		pose = p;
		switch(p){
		case 0: case 1: mg::mdAction(facing ? m0 : m1, 3, 0); break;
		case 2: mg::mdAction(m0, 0, 0); mg::mdAction(m1, 0, 0); break;
		case 3: mg::mdAction(m1, 0, 0); break;
		case 6: case 7: {
			Rec *r = cellUnderDigPoint();
			if(!r) return;
			r->dug = true;
			if(state == 0xf) return;
			digSounds();
			break; }
		default: break;
		}
	}
	void stepToTarget(float dt)	// [V] StepToTarget @10005db0
	{
		float sx, sy; mg::project(pos, sx, sy);
		float cx = sx, cy = sy - s * K_T30;
		float dx = adjX - cx, dy = adjY - cy;
		float len = sqrtf(dx * dx + dy * dy), nx = 0, ny = 0;
		if(len > 0){ nx = dx / len; ny = dy / len; }		// RwV3dNormalize (host +0xc8)
		// [V] 10005e1e..10005e40: world x -= nx * speed * dt, world y -= ny * speed * dt.  The direction is a SCREEN direction; world +x is the screen's left and
		//     world +y is up, so subtracting moves the penguin towards the target on the screen (sx = 512 - x / z * k, sy = 384 - y / z * k).
		pos.x = pos.x - nx * speed * dt;
		pos.y = pos.y - ny * speed * dt;
		float mx = cx - startX, my = cy - startY;
		float d = sqrtf(my * my + mx * mx);			// the distance walked is measured from the position BEFORE this frame's step
		bool arrived = (d > total) || d == total;		// [V] 10005eb1..10005ecd
		if(arrived){
			onCommandDone(4);
			if(state == 2){					// [V] 10005eea: the walk-in is over
				pose = 0;
				mg::mdAction(facing ? m0 : m1, 3, 0);
				setState(4);
				mg::mdUpdate(facing ? m0 : m1, dt);
				return;					// (no mdPos on this frame)
			}
			sndStopAll();
			queue.clear();					// 10005f43..10005f51: StopAll, List_Clear
			bool left = !(adjX < tgtX);			// [V] 10005f56: adjusted x >= target x -> the penguin stands right of the cell and digs to the left
			facing = left; mg::mdAction(left ? m0 : m1, 1, 1); pose = left ? 6 : 7;
			Rec *r = cellUnderDigPoint();
			if(logOn){ float dx2, dy2; digPoint(dx2, dy2); LOG19("arrived: dig point %.1f,%.1f target %.1f,%.1f (walked %.1f of %.1f)", dx2, dy2, tgtX, tgtY, d, total); }
			if(r){ r->dug = true; if(state != 0xf) digSounds(); }
			if(!cheat) setState(8);
			else if(containsPoint(tgtX, tgtY)) setState(8);
		}
		mg::mdPos(m0, pos); mg::mdPos(m1, pos);			// [V] 1000612f: both models follow the position
		mg::mdUpdate(facing ? m0 : m1, dt);
	}
	void penguinUpdate(float dt)	// [V] Penguin_Update @100062d0 (commands 0..3 are never queued: dead code left out)
	{
		if(queue.empty()){
			switch(pose){
			case 0: if(facing) mg::mdUpdate(m0, dt); else mg::mdUpdate(m1, dt); break;
			case 4: case 6: mg::mdUpdate(m0, dt); break;
			case 5: case 7: mg::mdUpdate(m1, dt); break;
			default: break;
			}
		}else if(curCmdFront() == 4) stepToTarget(dt);
		if(!queue.empty()){ mg::mdUpdate(m1, dt); mg::mdUpdate(m0, dt); }	// 100064b..
		if(state == 8){ mg::mdUpdate(m1, dt * 1.85f); mg::mdUpdate(m0, dt * 1.85f); }	// [V] 0x10033298 = 1.85
		if(state == 0xf && score < 40.0f){ mg::mdUpdate(m1, dt * 4.0f); mg::mdUpdate(m0, dt * 4.0f); }
		if(cheat){ mg::mdUpdate(m1, dt * 3.0f); mg::mdUpdate(m0, dt * 3.0f); }
		if(!queue.empty()){ mg::mdUpdate(m1, dt * 0.5f); mg::mdUpdate(m0, dt * 0.5f); }
	}
	int curCmdFront() const { return queue.empty() ? -1 : queue.front(); }
	bool hasNonMoveCmd() const { return !queue.empty() && queue.front() != 4; }	// [V] HasNonMoveCmd @10006180
	void penguinDraw()	// [V] Penguin_Draw @10005c30
	{
		mg::alphaTest(state == 0xe ? 0x14 : 0xc0);
		if(queue.empty()){
			switch(pose){
			case 4: case 6: mg::mdDraw(m0); break;
			case 5: case 7: mg::mdDraw(m1); break;
			default: mg::mdDraw(facing ? m0 : m1); break;
			}
		}else{
			switch(queue.front()){
			case 0: case 1: case 4: mg::mdDraw(facing ? m0 : m1); break;
			case 2: mg::mdDraw(m0); break;
			case 3: mg::mdDraw(m1); break;
			default: break;
			}
		}
		for(const Hist &h : hist) mg::spDraw(h.spr);		// DrawHistory @10005670
	}
	int digItem()	// [V] DigItem @10006890: value > 0 treasure, -1 trap, -2 nothing
	{
		Rec *r = cellUnderDigPoint();
		if(!r) return -2;
		int v = r->value;
		if(r->trap) return -1;
		if(r->kind == 1 || r->kind == 0) return -2;
		Hist h;
		h.kind = r->kind; h.value = v; h.spr = r->spr; h.flag = false; h.alpha = 0xff; h.chain = 0;
		h.x = (float)((int)hist.size() + 1) * K_T40 * s; h.y = s * K_T550; h.z = 0;
		hist.insert(hist.begin(), h);
		return v;
	}
	void updateCombo()	// [V] UpdateCombo @10006950
	{
		if(hist.empty()) return;
		if(hist.size() > 1){
			Hist &cur = hist[0]; Hist &prev = hist[1];
			if(prev.kind == cur.kind){
				V3 sc(1.2f, 1.2f, 1.0f);
				if(s > 1.0f) sc = V3(1.5f, 1.5f, 0.0f);
				cur.flag = true;
				if(prev.flag){ cur.chain = prev.chain + 1; mg::spScale(cur.spr, sc); }
				else{
					cur.chain = 2; prev.flag = true; prev.chain = 1;
					mg::spScale(cur.spr, sc); mg::spScale(prev.spr, sc);
				}
			}
		}
		Hist &cur = hist[0];
		if(cur.flag){
			if(cur.chain == 2) combos.insert(combos.begin(), cur);
			else if(cur.chain > 2){
				Hist c = cur;
				if(!combos.empty()) combos.erase(combos.begin());
				combos.insert(combos.begin(), c);
			}
			LOG19("combo: kind %d chain %d (combo entries %d)", cur.kind, cur.chain, (int)combos.size());
		}
	}
	static int comboBonus(int chain){ int e = chain - 2, v = 1; for(int i = 0; i < e; i++) v *= 2; return e < 0 ? 1 / v : v; }	// 2^(n-2) [V] the pow loop @10005a40
	void computeFinalScore()	// [V] ComputeFinalScore @10005a00
	{
		f84 = true;
		for(const Hist &c : combos) finalScore = ftol((float)finalScore + (float)comboBonus(c.chain));
		finalScore = ftol((float)finalScore + score);
		LOG19("final score computed: %d (score %.0f, %d combo entries)", finalScore, score, (int)combos.size());
	}
	void updateComboTally(float dt)	// [V] UpdateComboTally @100056c0
	{
		bool bl = false;
		uint8_t colB = 0xff, colG = 0xff, colR = 0xff, colA = 0x32;	// [A] byte 2 (red) of the colour is never set in the DLL (stack residue): white
		for(Hist &h : hist){
			if(!h.flag) continue;
			h.y = h.y - dt * K_T30;
			mg::spPos(h.spr, {h.x, h.y, h.z});
			if(h.alpha > 0){
				h.alpha = ftol((float)h.alpha - dt * 130.0f);	// [V] 0x1003328c = 130
				colA = (uint8_t)h.alpha;
				if(h.alpha < 0) h.alpha = 0;
				else mg::spColor(h.spr, ((uint32_t)colA << 24) | ((uint32_t)colR << 16) | ((uint32_t)colG << 8) | colB);
				if(h.alpha < 0x3c){ bl = true; f84 = false; f85 = true; f86 = true; }
			}else h.alpha = 0;
		}
		if(hist.empty() || !bl){							// 100059c5: the combo bubbles drift up
			for(Hist &c : combos) c.y = c.y - dt * K_T30;
			return;
		}
		if(!f86) return;
		if(f87){
			for(const Hist &c : combos){						// the bonus digits fly to the score board
				int n = c.chain;
				int val = comboBonus(n);
				float x = c.x - (float)n * s * K_T40 * K_HALF;
				float y = c.y - K_T40 * s;
				int h = val / 100, t = val / 10; if(t > 9) t = t % 10;
				int u = val % 10;
				float spd = s * 1000.0f;
				spawn(hPart[u], x, y, hS.x, hS.y, spd, u);
				if(t > 0 || h > 0){
					x = x - s * 20.0f;
					spawn(hPart[t], x, y, hS.x, hS.y, spd, t * 10);
					if(h > 0) spawn(hPart[h], x - s * 20.0f, y, hS.x, hS.y, spd, h * 100);
				}
			}
		}
		f87 = false; f86 = false;
	}
	void drawComboBonus()	// [V] DrawComboBonus @10005ab0
	{
		if(!f84) return;
		for(const Hist &c : combos){
			int n = c.chain;
			hudDrawBubble(comboBonus(n), {c.x - (float)n * K_T40 * s * K_HALF, c.y - K_T40 * s, c.z});
		}
	}

	// ======================================================================================================================== game flow
	void endMemoryPhase(){ sndStopAll(); setState(5); }	// [V] EndMemoryPhase @10001650

	void setState(int st)	// [V] SetState @10006f90 (durations from the table @1000707c) -> OnStateStart @100017f0
	{
		prevState = state; state = st;
		float dur = -1;
		switch(st){
		case 2: dur = 20.0f; break;
		case 3: dur = 2.0f; break;
		case 4: dur = 4.0f; break;
		case 5: dur = 1.0f; break;
		case 7: dur = 40.0f; break;
		case 8: dur = cheat ? fbits(0x3e99999a) : 1.0f; break;
		case 9: dur = 0.5f; break;
		case 12: dur = 2.0f; break;
		case 13: dur = cheat ? fbits(0x3e99999a) : 2.0f; break;
		case 14: dur = 3.0f; break;
		case 15: dur = 2.0f; break;
		case 16: dur = fbits(0x3e99999a); break;
		default: return;			// states 1, 6, 10, 11: no timer, no start handler
		}
		onStateStart(dur);
		LOG19("state %d -> %d (%.1f s)", prevState, st, dur);
	}
	void onStateStart(float dur)	// [V] @100017f0 (jump table @10001a38, index = state - 4)
	{
		if(dur > 0.0f) stDur = stTimer = dur;
		switch(state){
		case 3: if(layoutEnv){ applyLayout(layoutEnv); layoutEnv = nullptr; } break;	// test hook only
		case 4: sndPlay(1, 0); break;				// the memory phase loop
		case 8: {
			Rec *r = cellUnderDigPoint();
			if(r && r->kind == 2){				// a trap: its sprite starts its animation
				r = cellUnderDigPoint();
				hitSpr = r->spr;
				mg::spSetLoops(hitSpr, 1); mg::spSetFrame(hitSpr, 1);
			}
			break; }
		case 9: {
			V3 sc(fbits(0x3f4ccccd), fbits(0x3f4ccccd), 1.0f);	// 0.8 (scale == 1) or 1.0
			if(s != 1.0f) sc = V3(1.0f, 1.0f, 1.0f);
			Rec *r = cellUnderDigPoint();
			if(r) mg::spScale(r->spr, sc);
			break; }
		case 12: case 13: setPose(0); break;
		case 14: computeFinalScore(); setPose(0); break;
		case 15: {
			score = (float)finalScore;			// Score_Commit @10004f10: the display score becomes the final score (0 when no combo was settled!)
			if(score < 40.0f){
				rating = 0; sndPlay(0x157, 1);
				if(facing){ mg::mdAction(m0, 1, 0); setPose(6); } else { mg::mdAction(m1, 1, 0); setPose(7); }
			}else if(score < 90.0f){
				rating = 1; sndPlay(0x156, 1); setPose(0);
			}else{
				rating = 2; sndPlay(0x15c, 0);			// [V] 100019d9: loops = 0 (the DLL pushes 0): this one is a forever loop, remembered for StopAll
				if(facing){ mg::mdAction(m0, 2, 0); setPose(4); } else { mg::mdAction(m1, 2, 0); setPose(5); }
			}
			LOG19("result: score %.0f rating %d", score, rating);
			break; }
		default: break;
		}
	}
	void tick(float dt)	// [V] Tick @10006f40
	{
		if(state == 0) return;
		if(stTimer <= 0.0f){
			if(stTimer < 0.0f){ stTimer = 0; onStateTimeout(state); }
			return;
		}
		stTimer = stTimer - dt;
	}
	void onStateTimeout(int st)	// [V] @10003060 (jump table @100035d0, index = state - 2)
	{
		switch(st){
		case 2: speed = fbits(0x428c0000); break;		// 70
		case 3: setState(7); timer = 35.0f; break;		// the countdown starts now
		case 4: sndStopAll(); setState(5); break;
		case 5: setState(3); break;
		case 6: case 7: case 10: case 11: break;
		case 8: {
			mg::mdAction(facing ? m0 : m1, 2, 1);
			int v = digItem();
			if(v > 0){
				setState(9);
				setPose(facing ? 4 : 5);
				Rec *r = cellUnderDigPoint();
				switch(r ? r->kind : 0){
				case 3: sndPlay(5, 1); break;
				case 4: sndPlay(4, 1); break;
				case 5: sndPlay(3, 1); break;
				default: break;
				}
				LOG19("dig: treasure %d (history %d)", v, (int)hist.size());
			}else if(v == -1){				// the trap
				V3 sc((float)(0.6 * s), (float)(0.6 * s), 1.0f);	// [V] 0.6 * scale (double constant 0.6 @10033258)
				if(cheat) sc = V3(s, s, 1.0f);
				mg::spScale(sprPick, sc);
				float sx, sy; screenPos(sx, sy);
				mg::spPos(sprPick, {sx, sy, 0});
				float dx, dy; digPoint(dx, dy);
				Rec *r = recordAt(ftol(dx), ftol(dy), 0);
				if(r){
					r->vis = false;
					int col, row; cellIndex(ftol(dx), ftol(dy), col, row);
					mg::spPos(sprBoom, {cellCX(col), cellCY(row), 0});
					sndPlay(6, 1);
					setState(0xd);
					if(!cheat){ timer = 0; timeUp = true; }
					LOG19("dig: trap");
				}
			}else{
				setPose(0);
				setState(7);
				applyPendingClick();
				sndPlay(0x155, 1);
				LOG19("dig: nothing");
			}
			break; }
		case 9: {
			setPose(0);
			setState(7);
			float sx, sy; mg::project(pos, sx, sy);		// where the penguin is on the screen (no 30 * scale here)
			if(!hist.empty()){
				const Hist &h = hist[0];
				spawn(h.spr, sx, sy - s * K_T90, (float)hist.size() * s * K_T40, s * K_T550, iconSpeed, -1000);	// the icon flies to its slot of the history bar
				int v = h.value;
				int ones = v % 10, tens = v / 10;
				float x0 = s * K_T25 + sx, y0 = sy - s * K_T90;
				spawn(hPart[ones], x0, y0, hS.x, hS.y, digitSpeed, ones);
				if(tens > 0) spawn(hPart[tens], x0 - s * 20.0f, y0, hS.x, hS.y, digitSpeed, tens * 10);
			}
			applyPendingClick();
			break; }
		case 12: setState(combos.empty() ? 0xf : 0xe); break;
		case 13: if(!cheat) setState(combos.empty() ? 0xf : 0xe); else setState(7); break;	// [V] 10003551: cheat -> back to play
		case 14: setState(0xf); break;
		case 15: sndStopAll(); finished = true; break;	// [V] 100035ab (the DLL also resets the alpha test threshold to 0xc0 through host +0x3c here and at the end of Finish: [A] left out, it is outside a frame and the port restores the state in gfxEnd)
		case 16: setState(prevState); break;
		default: break;
		}
	}
	// [V] ApplyPendingClick @10001670
	void applyPendingClick()
	{
		if(pendX == -1.0f || pendY == -1.0f) return;
		Rec *r = recordAt(ftol(pendX), ftol(pendY), 0);
		if(r && r->dug) return;					// the buffered click is dropped but NOT cleared (it stays in the buffer)
		setTarget(pendX, pendY);
		float sx, sy; screenPos(sx, sy);
		if(sx <= adjX){ facing = false; mg::mdAction(m1, 0, 0); }
		else { facing = true; mg::mdAction(m0, 0, 0); }
		enqueue(4);
		pendX = pendY = -1.0f;
	}
	void startGame()	// [V] StartGame @10001a70 + BeginWalkIn @10001030
	{
		mg::introHide(); introShown = false;
		setState(2);
		setTarget(fW * 0.5f, fH * 0.5f);
		facing = true;
		mg::mdAction(m0, 0, 0);
		enqueue(4);
	}

	// ======================================================================================================================== host interface
	bool init(const std::vector<Rm8MiniPlayer> &pl) override
	{
		players = pl;
		fW = (float)mg::W; fH = (float)mg::H;			// [V] 10001d44: host +0x120 / +0x124
		s = fW * K_SCALE;					// [V] 10001d71: scale = W * 0.00125 = 1.28
		finished = false; timeUp = false;
		timer = fbits(0x420c0000);				// 35.0 [V] 10001dbf: +0xc0
		coverKeep = fbits(0x40733333);				// 3.8 [V] 10001dc9: +0xbc
		iconSpeed = s * 800.0f; digitSpeed = s * 1500.0f;	// [V] 10001da4 / 10001db6: DAT_1003a06c = 1500 * scale, DAT_1003a070 = 800 * scale
		hudCreate();						// [V] HUD (Hud_Init @10007b40)
		gridCreate();						// [V] Grid_Create @10001e9a
		// penguin: Penguin_Init @100053b0
		toolX = s * K_T30; toolY = s * fbits(0xc1200000);	// [V] +0x74 = 30 s, +0x78 = -10 s
		speed = fbits(0x428c0000);				// 70
		sprPick = 0;
		m0 = mg::mdNew(0x642); m1 = mg::mdNew(0x643); sprPick = mg::spNew(0x649);
		mg::spScale(sprPick, {s, s, 1.0f});
		mg::mdRot(m0, {0, 1, 0}, 180.0f); mg::mdRot(m1, {0, 1, 0}, 180.0f);	// [V] 100054ed: +0x6c (axis 0,1,0, 180 degrees)
		mg::mdPos(m0, {-100.0f, 0, 500.0f}); mg::mdPos(m1, {-100.0f, 0, 500.0f});	// [V] 1000552c
		pos = V3(-100.0f, 0, 500.0f);
		pose = 0; facing = true;
		mg::mdAction(m0, 3, 0);					// [V] 10005572: stand (facing flag is 1 from Penguin_Ctor @10006258)
		seat = pl.empty() ? 0 : pl[0].seat;			// [V] 10001ed7: *(params + 4)
		sprCursor = mg::spNew(0x648); mg::spScale(sprCursor, {s, s, 0});			// [V] 10001eeb
		sprBoom = mg::spNew(0x64b); mg::spScale(sprBoom, {s * 2.0f, s * 2.0f, 0});		// [V] 10001f0c: scale * 2
		mg::showCursor(true);					// [V] 10001f3a: host +0x130 (1)
		V3 c((float)((cw * 6) / 2) + gx0, (float)((ch * 5) / 2) + gy0, 0);	// [V] grid centre: x0 + (cw * 6) / 2, y0 + (ch * 5) / 2 (int divisions)
		V3 sc3(s, s, 1.0f);
		sprReady = mg::spNew(0x84b); mg::spScale(sprReady, sc3); mg::spPos(sprReady, c);	// [V] 10001fd3..10001ff7
		sprGo = mg::spNew(0x84c); mg::spScale(sprGo, sc3); mg::spPos(sprGo, c);
		c.x = c.x - s * 30.0f;					// [V] 1000202b: x - 30 s
		sprOver1 = mg::spNew(0x84d); mg::spScale(sprOver1, sc3); mg::spPos(sprOver1, c);
		c.x = s * 180.0f + c.x; c.y = s * 25.0f + c.y;		// [V] 1000206d: x + 180 s, y + 25 s
		sprOver2 = mg::spNew(0x84e); mg::spScale(sprOver2, sc3); mg::spPos(sprOver2, c);
		c.x = (float)((cw * 6) / 2) + gx0; c.y = (float)((ch * 5) / 2) + gy0;
		sprRes0 = mg::spNew(0x85d); mg::spScale(sprRes0, sc3); mg::spPos(sprRes0, c);		// [V] 10002134: score < 40
		sprRes2 = mg::spNew(0x85e); mg::spScale(sprRes2, sc3); mg::spPos(sprRes2, c);		// score >= 90
		c.x = c.x - s * 50.0f;					// [V] 10002196: x - 50 s
		sprResA = mg::spNew(0x84f); mg::spScale(sprResA, sc3); mg::spPos(sprResA, c);
		c.x = s * 180.0f + c.x;					// [V] 100021d8: x + 180 s
		sprResB = mg::spNew(0x851); mg::spScale(sprResB, sc3); mg::spPos(sprResB, c);
		// the intro panel (lg_intro.ui, label 1 = string 0x1b28 = 6952, OK button -> StartGame) [V] 10002225..100022c2
		mg::introLoad("lg__lg_intro", 6952, [this]{ if(state == 1) startGame(); });
		introShown = true;
		setState(1);						// [V] 100022cb
		// the penguin starts right of the screen centre [V] 100022d0..1000232e: (-70 scale, -27 scale, 500), or (-90, -30, 500) at scale 1
		V3 p0(s * -90.0f, s * -30.0f, 500.0f);
		if(s != 1.0f) p0 = V3(s * -70.0f, s * -27.0f, 500.0f);
		mg::mdPos(m0, p0);					// only the first model is moved here
		pos = p0;
		mg::music(0x16);					// [V] 10002353: host +0x108 (0x16, 0)
		// ---- test hooks
		logOn = getenv("RM8_MG19_LOG") != nullptr;
		layoutEnv = getenv("RM8_MG19_LAYOUT");
		if(const char *e = getenv("RM8_MG19_BOT")){
			botOn = true;
			if(!strcmp(e, "rand")) botRand = true;
			else{
				const char *p = e;
				while(*p){ int c, r, n = 0; if(sscanf(p, "%d,%d%n", &c, &r, &n) >= 2){ botCells.push_back({c, r}); p += n; } else break; while(*p == ';') p++; }
			}
		}
		if(const char *e = getenv("RM8_MG19_START")){ startAt = (float)atof(e); botOn = true; }
		return true;
	}

	~G19() override { mg::showCursor(true); sndStopAll(); }

	// Finish @10002500
	void getResult(Rm8MiniResult &o) override
	{
		o.count = 1;							// [V] out[0] = 1
		Rm8MiniPlayerResult &p = o.p[0];
		p.seat = seat;							// out[1] = the seat from the parameter block
		p.score = ftol(score);						// out[2] = ftol(score)
		p.flagA = 0;							// out[3] = 0
		p.rating = rating; p.flagB = rating;				// out[4] = out[5] = the rating tier
		mg::showCursor(true);						// [V] 10002508: host +0x130 (1)
		mg::introHide();
		sndStopAll();
	}

	// ---- input
	void mouseXY(int &x, int &y)
	{
		float fx, fy;
		if(fakeMouse){ fx = fmx; fy = fmy; } else mg::mouse(fx, fy);
		x = (int)fx; y = (int)fy;
	}
	void mouseButton(int flags) override	// [V] OnMouseButton @10001290
	{
		if(flags & MG_MOUSE_LDOWN){					// 0x40: the action button went down
			mouseHeld = true;					// +0x239
			if(state == 1) return;
			if(state != 7){
				int mx, my; mouseXY(mx, my);
				float x = (float)mx, y = (float)my;		// buffered click, only inside the grid, only while digging (8) or collecting (9)
				if(x < gx0) return;
				if((float)(cw * 6) + gx0 < x) return;
				if(y < gy0) return;
				if((float)(ch * 5) + gy0 < y) return;
				if(state == 9 || state == 8){ pendX = x; pendY = y; }
				return;
			}
			int mx, my; mouseXY(mx, my);
			float x = (float)mx, y = (float)my;
			if(!cheat){						// the click must be inside the grid (inclusive)
				if(x < gx0) return;
				if((float)(cw * 6) + gx0 < x) return;
				if(y < gy0) return;
				if((float)(ch * 5) + gy0 < y) return;
			}else{
				if(mg::spHit(hFrame, x, y)){ score = score + 1.0f; sndPlay(3, 1); LOG19("cheat: timer frame clicked, score %.0f", score); }	// [V] 100014d3: cheat: clicking the timer frame adds a point
			}
			Rec *r = recordAt(mx, my, 0);
			if(r && r->dug) return;					// an already dug cell: nothing
			setTarget(x, y);
			float sx, sy; screenPos(sx, sy);
			if(sx > adjX){ facing = true; setPose(2); }		// [V] 1000157f: sx > adjusted x: faces the other way (flag 1), pose 2
			else if(sx < adjX){ facing = false; setPose(3); }
			else{ if(facing) setPose(2); else setPose(3); }		// equal: keep the facing flag
			enqueue(4);
			LOG19("click %d,%d -> target %.1f,%.1f (adj %.1f,%.1f) dist %.1f facing %d", mx, my, tgtX, tgtY, adjX, adjY, total, (int)facing);
		}else{
			if(flags & MG_MOUSE_LUP){ mouseHeld = false; return; }	// 0x400: released: the highlight stops animating
			// flags & 4 without 0x10 / 0x100 (a middle button state) would restart the highlight animation: no such event in the port
		}
	}
	void keyDown(int code) override	// [V] OnKey @10003d20
	{
		if(code == 0x20){
			if(state == 1 && startOk){ startGame(); startOk = false; }
		}else if(code == 0xab){
			if(state == 1 && startOk){ startGame(); startOk = false; }
			if(cheatRec){
				std::vector<int> b = cheatBuf;
				bool ok = false;
				if(b.size() == 4 && b.back() == 'a'){
					b.pop_back();
					int e = b.back();
					if(e == 'm'){ b.pop_back(); e = b.back(); }
					if(e == 'e'){ b.pop_back(); ok = b.back() == 'j'; }
				}
				if(ok && !cheat && state != 0xd && state != 0xc && state != 0xf && state != 0xe) enableCheat();
				cheatBuf.clear();
			}
			cheatRec = !cheatRec;
			return;
		}
		if(cheatRec && code != 0xab) cheatBuf.push_back(code);
	}
	void enableCheat()	// [V] 10003e01..10003f10
	{
		cheat = true;
		clearQueue();
		speed = fbits(0x43160000);					// 150
		V3 sc(fbits(0x3fa66666), fbits(0x3fa66666), 1.0f);		// 1.3
		mg::mdScale(m0, sc); mg::mdScale(m1, sc);
		mg::mdPos(m0, pos); mg::mdPos(m1, pos);
		toolX = toolX * 1.3f; toolY = toolY * 1.3f;			// the tool offset grows with the model
		iconSpeed = s * 1200.0f;					// [V] 0x1003a070 = scale * 1200
		LOG19("cheat on");
	}

	// ======================================================================================================================== Update / Draw
	void botStep(float dt)
	{
		if(!botOn) return;
		if(startAt >= 0 && mg::now() >= startAt && state == 1){ startAt = -1; keyDown(' '); }
		if(state != 7 || !queue.empty() || finished) return;
		botWait -= dt;
		if(botWait > 0) return;
		int c, r;
		if(botRand){ c = mg::rand15() % 6; r = mg::rand15() % 5; }
		else{ if(botNext >= botCells.size()) return; c = botCells[botNext].first; r = botCells[botNext].second; botNext++; }
		fakeMouse = true; fmx = cellCX(c); fmy = cellCY(r);
		botWait = 0.2f;
		mouseButton(MG_MOUSE_LDOWN); mouseButton(MG_MOUSE_LUP);
	}
	bool update(float dt) override	// [V] Update @10002710
	{
		botStep(dt);
		if(finished) return false;
		switch(state){
		case 1: penguinUpdate(dt); return true;			// [V] 10002741: the intro only animates the penguin (no countdown, no state timer)
		case 8: if(hitSpr) mg::spUpdate(hitSpr, dt + dt); break;
		case 0xd:
			mg::spUpdate(sprPick, dt); mg::spUpdate(sprBoom, dt);
			f21c = dt * 3.0f + f21c; if(f21c > 1.5f) f21c = 1.5f;	// [V] 10002775..100027a2 (never read elsewhere)
			break;
		case 0xe: updateComboTally(dt); break;
		default: break;
		}
		if(state != 4 && state != 3 && state != 2 && state != 5 && !timeUp){
			timer = timer - dt;
			if(timer < (float)secCnt){				// [V] 10002811: one sound per second
				secCnt--;
				if(secCnt > 5) sndPlay(0x150, 1);
				else if(secCnt > 0) sndPlay(0x151, 1);
				else if(secCnt == 0) sndPlay(0x152, 1);
			}
		}
		if(timer < 0.0f){						// [V] 1000285e: time is up
			timer = 0; timeUp = true;
			clearQueue();
			setState(12);
			if(!hist.empty()) mg::spPos(hist[0].spr, {(float)hist.size() * K_T40 * s, s * K_T550, 0});	// the newest icon jumps to its slot
		}
		if(mouseHeld) mg::spUpdate(sprCursor, dt + dt);
		tick(dt);
		particleUpdate(dt);
		penguinUpdate(dt);
		gridUpdate(dt);
		return !finished;
	}

	void readyGo()	// [V] ReadyGo_Overlay @100011a0
	{
		if(0.5485103f < stTimer){
			if(readySnd){ sndPlay(0x14f, 1); readySnd = false; }
			mg::spScale(sprReady, {1, 1, 1}); mg::spDraw(sprReady);
			return;
		}
		if(goSnd){ sndPlay(0x165, 1); goSnd = false; }
		float c = (float)(1.0 - stTimer * 1.8231198f) * 2.0f;	// grows from 0 to 2 during the last 0.55 s (0x100331b8 = 1.8231198)
		mg::spScale(sprGo, {c, c, 1.0f}); mg::spDraw(sprGo);
	}
	void highlight(bool dig)	// the cell under the mouse (or under the dig point) in states 7, 8 and 9: sprite 0x648
	{
		int mx, my;
		if(dig){ float dx, dy; digPoint(dx, dy); mx = ftol(dx); my = ftol(dy); }
		else{
			mouseXY(mx, my);
			float x = (float)mx, y = (float)my;
			if(x < gx0 || (float)(cw * 6) + gx0 < x || y < gy0 || (float)(ch * 5) + gy0 < y) return;
		}
		int col, row; cellIndex(mx, my, col, row);
		mg::spPos(sprCursor, {cellCX(col), cellCY(row), 0});
		mg::spDraw(sprCursor);
	}
	void draw() override	// [V] Draw @10002960
	{
		for(int i = 0; i < 4; i++) mg::spDraw(hBg[i]);		// HUD background tiles
		mg::alphaTest(state == 0xe ? 0x14 : 0xc0);
		if(state == 4 || state == 5 || state == 3) mg::spDraw(hFrame);
		else if(-1.0f < timer){ if(6.0f < timer) mg::spDraw(hFrame); else mg::spDraw(hFrameRed); }
		switch(state){
		case 1: showLayer(1); penguinDraw(); break;
		case 2: case 4: case 5: case 0xc: showLayer(1); showLayer(0); penguinDraw(); break;
		case 3: showLayer(1); penguinDraw(); readyGo(); break;
		case 7: case 8: case 0x10:
			drawPlay(); penguinDraw();
			highlight(hasNonMoveCmd());
			break;
		case 9: {
			drawPlay(); penguinDraw();
			if(!hist.empty()){
				float sx, sy; mg::project(pos, sx, sy);
				V3 v(sx, sy - s * K_T90, 0);
				mg::spPos(hist[0].spr, v); mg::spDraw(hist[0].spr);
				v.x = s * K_T25 + v.x;
				hudDrawBubble(hist[0].value, v);
			}
			highlight(false);
			break; }
		case 0xd:
			if(!cheat){ showLayer(1); showLayer(0); } else { showLayer(0); showLayer(1); }	// [V] 10002e95
			mg::spDraw(sprPick); mg::spDraw(sprBoom);
			mg::spScale(sprOver1, {1, 1, 1}); mg::spScale(sprOver2, {1, 1, 1});
			if(!cheat){ mg::spDraw(sprOver1); mg::spDraw(sprOver2); }
			for(const Hist &h : hist) mg::spDraw(h.spr);		// DrawHistory (the penguin itself is not drawn)
			break;
		case 0xe: showLayer(1); penguinDraw(); drawComboBonus(); break;
		case 0xf:
			showLayer(1); penguinDraw();
			if(score < 40.0f) mg::spDraw(sprRes0);
			else if(score < 90.0f){ mg::spDraw(sprResA); mg::spDraw(sprResB); }
			else mg::spDraw(sprRes2);
			break;
		default: break;
		}
		hudUpdate();
		particleDraw();
	}
};
}	// namespace

MgGame *mgNew19(){ return new G19; }
