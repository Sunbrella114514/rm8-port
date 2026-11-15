// Dice throw, after docs/turn_flow.md section 1 (Role_StartRollDice @0x484e50, Dice_SetupScene @0x47d4d0,
// Dice_UpdateRoll @0x47d090, Dice_BeginRoll @0x47d5c0, Dice_UpdateAnimation @0x47c730, Dice_FinishRoll @0x47d410).
// The dice are camera-facing 50x50 Im3D quads (dice\dice_1..6 faces, dice\animation\dice_a0..a5 rolling frames).
#include "scene.h"
#include "audio.h"
#include <cstring>
#include <cmath>
#include <vector>

using namespace rw;

namespace {

Texture *g_tex[12];	// 0..5 faces 1..6, 6..11 rolling frames
bool g_texLoaded = false;
// [V] 0x5e19f0: height of each roll frame (world y = table + base - 100)
const int HEIGHT[18] = {130, 160, 185, 205, 220, 230, 235, 220, 205, 180, 145, 100, 50, 60, 65, 60, 50, 50};

struct Die { int fc = 0; Texture *tex = nullptr; V3d pos = {0, 0, 0}, start, end; float base = 55; int value = 1; float scale = 1; };

struct DiceScene {
	bool active = false;
	bool hand = false;		// phase A: one die held for 0.3 s
	float handTimer = 0;
	V3d origin, dicePos;		// character position, dicePos with x flipped by facing
	int count = 1, forced = 0;
	int frame = 0;			// roll frame 0..17
	std::vector<Die> dice;
} g;

float g_rollAcc = 0, g_holdAcc = 0;	// [V] globals 0x63fbb0 / 0x63fbb4 (the roll accumulator is never reset)
uint32 g_seed = 7;
int rnd(){ g_seed = g_seed * 214013u + 2531011u; return (g_seed >> 16) & 0x7fff; }	// MSVC rand()

void loadTextures()
{
	if(g_texLoaded) return;
	g_texLoaded = true;
	char key[64];
	for(int i = 0; i < 6; i++){
		snprintf(key, sizeof key, "dice\\dice_%d.tga", i + 1); g_tex[i] = rm8PackTexture(key);
		snprintf(key, sizeof key, "dice\\animation\\dice_a%d.tga", i); g_tex[6 + i] = rm8PackTexture(key);
	}
	for(Texture *t : g_tex) if(!t) rm8Log("warning: dice texture missing");
}

}	// namespace

void rm8DiceSeed(unsigned s){ g_seed = s; }

// [V] Dice_SetupScene: one die in hand at (P.x + dp.x, P.y + dp.y, P.z), random rolling frame, 0.3 s
void rm8DiceStart(const float pos[3], const float dicePos[3], int count, int forced)
{
	loadTextures();
	g = DiceScene();
	g.active = true; g.hand = true; g.handTimer = 0.3f;
	g.origin = {pos[0], pos[1], pos[2]}; g.dicePos = {dicePos[0], dicePos[1], dicePos[2]};
	g.count = count; g.forced = forced;
	Die d; d.pos = {pos[0] + dicePos[0], pos[1] + dicePos[1], pos[2]}; d.tex = g_tex[6 + rnd() % 6]; d.scale = 0.8f;
	g.dice.push_back(d);
}

bool rm8DiceActive(){ return g.active; }
bool rm8DiceInHand(){ return g.active && g.hand; }

// returns the sum once the dice have rested for 1 s, else 0. cam: for the throw direction
int rm8DiceUpdate(float dt, Camera *cam)
{
	if(!g.active) return 0;
	if(g.hand){	// [V] Dice_UpdateRollPhase1 @0x47d210
		g.handTimer -= dt;
		if(g.handTimer > 0){ g.dice[0].pos.y -= 10.0f * dt; return 0; }
		// [V] Dice_BeginRoll: start = P - 2i F, end = P + off[i] R - (100 + 2i) F
		g.hand = false; g.frame = 0; g.dice.clear();
		int n = (g.forced >= 1 && g.forced <= 6) ? 1 : g.count;
		const Matrix *cm = cam->getFrame()->getLTM();
		V3d F = {cm->at.x, 0, cm->at.z}; float fl = sqrtf(F.x*F.x + F.z*F.z); if(fl > 0){ F.x /= fl; F.z /= fl; }
		V3d R = cm->right;
		const float off3[3] = {0, 50, -50}, off2[2] = {25, -25};
		for(int i = 0; i < n; i++){
			Die d;
			float o = n == 2 ? off2[i] : off3[i];
			d.start = {g.origin.x - 2*i*F.x, g.origin.y, g.origin.z - 2*i*F.z};
			d.end = {g.origin.x + o*R.x - (100 + 2*i)*F.x, g.origin.y + o*R.y, g.origin.z + o*R.z - (100 + 2*i)*F.z};
			d.value = rnd() % 6 + 1;	// [V] Dice_SetupRoll @0x47c630
			d.fc = rnd() % 6;
			d.base = g.dicePos.z;
			if(n == 1 && g.forced >= 1 && g.forced <= 6) d.value = g.forced;
			d.pos = d.start; d.tex = g_tex[6 + d.fc];
			g.dice.push_back(d);
		}
		return 0;
	}
	if(g.frame < 17){	// [V] 0.04 s per roll frame
		g_rollAcc += dt;
		if(g_rollAcc >= 0.04f){
			int n = (int)(g_rollAcc / 0.04f);
			int old = g.frame;
			g.frame = std::min(17, g.frame + std::max(n, 1));
			g_rollAcc -= n * 0.04f;
			(void)old;
			for(Die &d : g.dice){
				d.fc = (d.fc + 1) % 6;
				d.tex = g.frame < 16 ? g_tex[6 + d.fc] : g_tex[d.value - 1];
				float t = g.frame / 16.0f;
				d.pos.x = d.start.x + (d.end.x - d.start.x) * t;
				d.pos.z = d.start.z + (d.end.z - d.start.z) * t;
				d.pos.y = HEIGHT[g.frame] + d.base - 100.0f;	// absolute world height
			}
			if(g.frame == 12) rm8SndPlay(9);	// [V] Dice_UpdateRoll @0x47d090: CAudioMgr_PlaySample(9,0,0,0,1000,0,1.0,1) when the frame is exactly 12
		}
		return 0;
	}
	g_holdAcc += dt;	// [V] Dice_FinishRoll: 1.0 s on the result
	if(g_holdAcc < 1.0f) return 0;
	g_holdAcc = 0;
	int sum = 0; for(Die &d : g.dice) sum += d.value;
	g.active = false; g.dice.clear();
	return sum;
}

// [V] Dice_DrawEffect @0x47c8e0: z test/write off, no culling; quad rotated about X by pitch, then Y by yaw
// (camera-facing), scaled 0.8 while in hand
void rm8DiceRender(Camera *cam)
{
	if(!g.active) return;
	const Matrix *cm = cam->getFrame()->getLTM();
	float yaw = atan2f(-cm->at.z, -cm->at.x) * 180.0f / 3.14159265f;
	float pitch = asinf(std::max(-1.0f, std::min(1.0f, -cm->at.y))) * 180.0f / 3.14159265f;
	uint32 zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE), cull = GetRenderState(CULLMODE), atf = GetRenderState(ALPHATESTFUNC);
	SetRenderState(ZTESTENABLE, 0); SetRenderState(ZWRITEENABLE, 0); SetRenderState(CULLMODE, CULLNONE);
	SetRenderState(ALPHATESTFUNC, ALPHAALWAYS);
	for(Die &d : g.dice){
		Matrix m; m.setIdentity(); m.flags = 0;
		V3d ax = {1, 0, 0}, ay = {0, 1, 0}, s = {d.scale, d.scale, d.scale};
		m.scale(&s, COMBINEREPLACE);
		m.rotate(&ax, pitch, COMBINEPOSTCONCAT);
		m.rotate(&ay, yaw, COMBINEPOSTCONCAT);
		m.translate(&d.pos, COMBINEPOSTCONCAT);
		gl3::Im3DVertex v[4];
		for(int k = 0; k < 4; k++){
			v[k].setX(25.0f - 50.0f * (k & 1)); v[k].setY(25.0f - 50.0f * (k / 2)); v[k].setZ(0);
			v[k].setNormalX(0); v[k].setNormalY(0); v[k].setNormalZ(1);
			v[k].setColor(255, 255, 255, 255);
			v[k].setU((float)(k & 1)); v[k].setV((float)(k / 2));
		}
		SetRenderStatePtr(TEXTURERASTER, d.tex ? d.tex->raster : nil);
		SetRenderState(VERTEXALPHA, 1);
		im3d::Transform(v, 4, &m, im3d::EVERYTHING);
		im3d::RenderPrimitive(PRIMTYPETRISTRIP);
		im3d::End();
	}
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(CULLMODE, cull); SetRenderState(ALPHATESTFUNC, atf);
}

void rm8DiceClear(){ g = DiceScene(); }

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_dice.inc"
