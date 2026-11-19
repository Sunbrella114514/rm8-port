// Internal interface of the minigame module: the DLL host services (CMiniGameHost_2, vtable 0x5E8928) and the game object interface.
// A game (mg_NN.cpp) is a transliteration of its DLL on top of these services; offsets in comments are the host vtable slots.
#pragma once
#include "minigame.h"
#include "ui.h"
#include <cstdint>
#include <functional>

namespace mg {

// [V] host +0x120 / +0x124 (client size). The port always plays at the game's 1024x768 mode (CApp_SetFullscreenMode @0x405DA0:
// 800x600 or 1024x768), so the DLL scale factors (width * 0.00125 = 1.28 ...) come out as they do on the original at 1024x768.
const int W = 1024, H = 768;
struct V3 { float x = 0, y = 0, z = 0; V3() {} V3(float a, float b, float c = 0) : x(a), y(b), z(c) {} };

// MSVC CRT rand() (the DLLs call it): 15 bits, seeded per game start
int rand15();
void srand15(unsigned seed);
inline float randf(){ return rand15() / 32768.0f; }	// the DLLs use rand() * 3.051851e-05

// ---- sprites (articles with record +0xc == 1, picture = uvlisttable record) -----------------------------------------------
// [V] CMiniGameHost_vf20 @0x50F750 -> CArticleMgr vf16 @0x43C470 -> CArticleDFF_Load @0x455BD0 (Sprite_Init @0x461E70). The position is the
// sprite CENTRE (CMiniGameHost vf40 SetPosition: offset = pos - centre), scaling happens about the centre. size = raster size x UV span.
int spNew(int art);				// +0x50: handle (> 0), 0 if the article is unknown
void spFree(int h);				// +0x54
void spPos(int h, V3 p);			// +0x68
void spScale(int h, V3 s);			// +0x64 (x, y used)
void spRotZ(int h, float deg);			// +0x6c (Sprite_SetZRotation)
void spColor(int h, uint32_t argb);		// +0x84 (Sprite_SetColor)
void spUpdate(int h, float dt);			// +0x78 (Sprite_Update: Article_AdvanceAnimFrame, then the vertex UVs)
void spDraw(int h);				// +0x7c (Sprite_Render: nothing is drawn once the animation state is 2 = finished)
void spSize(int h, float &w, float &hgt);	// +0x90 (Sprite_GetPixelSize, unscaled)
bool spHit(int h, float x, float y);		// +0xb4 (Sprite_HitTest: inverse transform, rect, alpha > 0x32 [H])
int spFrame(int h);				// +0x9c (field +0x1c = current frame index)
void spSetFrame(int h, int f);			// +0xa0
int spLoops(int h);				// +0xa4 (field +0x20, loop counter)
void spSetLoops(int h, int n);			// +0xa8
int spState(int h);				// +0xac (field +0x10: 0 idle, 1 frame changed, 2 finished)
void spSetState(int h, int s);			// +0xb0
void alphaTest(int thr);			// +0x3c: 1..255 -> GREATEREQUAL thr, else always (applies to the following quads)
// immediate 2D quad with a pack texture (host +0xf0, portraits): screen rectangle in 1024x768 space
void drawTexQuad(const std::string &texKey, float x, float y, float w, float h, uint32_t argb = 0xffffffff);
void drawRect(float x, float y, float w, float h, uint32_t argb);

// ---- host services -------------------------------------------------------------------------------------------------------
int snd(int id, float vol = 1.0f, int loops = 1);	// +0x114: CAudioMgr_PlaySample(id,0,0,0,1000,50,vol,loops) (all games play 2D)
void sndStop(int handle);				// +0x118
void music(int track);					// +0x108: CDigitalAudio_PlayBGMAsync (mus%05d)
const std::string &str(int id);				// +0xfc: string.bin
void mouse(float &x, float &y);				// +0x128: cursor position in 1024x768 space
void showCursor(bool show);				// +0x130 (the games draw their own cursor sprite)
Rm8Ui *introLoad(const char *name, int textId, std::function<void()> onOk);	// the lg_intro.ui panel (label 1 = string textId, button 2)
void introHide();
bool introVisible();
float now();						// game clock (s), advanced by Update
const std::string &scenesRoot();			// [A] the scenes directory given to rm8MiniInit (games that load extra definitions, e.g. 21: roles.txt)
const Rm8Mini21Net &net21();				// [N] N4: game 21 in a netplay game (on = false outside netplay)
const std::vector<Rm8Mini21Act> &net21Acts();		// [N] the referee's actions so far

// ---- module internals (mg_gfx.cpp / minigame.cpp)
bool loadSprites(const std::string &path);
void gfxBegin(rw::Camera *cam, int sw, int sh);
void gfxEnd();
void gfxFlush();
void gfxSetScreen(int sw, int sh);	// sets the virtual -> screen mapping without drawing (pointer input, headless steps)
void gfxMap(float &ox, float &oy, float &sc);	// virtual -> screen: x * sc + ox
void gfxTri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t argb);
void freeAllSprites();
void setScenesRoot(const std::string &s);
void gfxRestore2D();
// 3D models (mg_3d.cpp): article records without sprite flag (penguin 0x642 / 0x643, swords 0x711..0x717) and the projection the DLLs use
bool loadModels(const std::string &path);
int mdNew(int art);				// +0x50
void mdFree(int h);				// +0x54
void mdPos(int h, V3 p);			// +0x68 (translate by p - last p, post-concat)
void mdScale(int h, V3 s);			// +0x64 (about the frame position, cumulative)
void mdRot(int h, V3 axis, float deg);		// +0x6c (about the frame position, cumulative)
void mdRotFrame(int h, V3 axis, float deg);	// +0x70
void mdAction(int h, int action, int times);	// +0x58 (animation index, plays; 0 = loop)
void mdUpdate(int h, float dt);			// +0x78
void mdDraw(int h);				// +0x7c
bool mdAnimDone(int h);				// [A] a counted animation has played out
float mdAnimTime(int h);
float mdAnimDuration(int h);
void mdVisible(int h, bool v);
bool project(V3 p, float &sx, float &sy);	// +0x1c: world -> pixels in the 1024x768 space
bool models3d();				// any model alive (the camera is switched for those frames)
void camBegin(rw::Camera *cam, int sw, int sh);
void camEnd(rw::Camera *cam);
void freeAllModels();

}	// namespace mg

// touch buttons ([A]): the overlay gives games without a pointer interface a way to be played on a phone
struct MgTouchBtn { float x, y, w, h; int key; int shape; };	// shape: 0 left 1 right 2 up 3 down 4 circle
enum { MG_SHAPE_LEFT, MG_SHAPE_RIGHT, MG_SHAPE_UP, MG_SHAPE_DOWN, MG_SHAPE_CIRCLE };

// mouse button flags the DLLs receive from CMiniGameHost_vf9 @0x50EE30
enum { MG_MOUSE_LDOWN = 0x40, MG_MOUSE_LUP = 0x400 };

// the DLL game object (vtable: Init, GetResult, Update, Draw, KeyDown, KeyUp, MouseButton, MouseMove)
struct MgGame {
	virtual ~MgGame() {}
	virtual bool init(const std::vector<Rm8MiniPlayer> &players) = 0;	// [0] Init(host, params) - false = the host gives up
	virtual void getResult(Rm8MiniResult &out) = 0;			// [1] GetResult(out) (the game frees its resources in its destructor)
	virtual bool update(float dt) = 0;				// [2] false = the game is over
	virtual void draw() = 0;					// [3] Draw (sprites in the DLL's order)
	virtual void keyDown(int code) {}				// [4]
	virtual void keyUp(int code) {}					// [5]
	virtual void mouseButton(int flags) {}				// [6]
	virtual void touchButtons(std::vector<MgTouchBtn> &out) {}	// [A] on-screen buttons for the current state
	int eventId = 0;						// 18..23
};
MgGame *mgNew18();
MgGame *mgNew19();
MgGame *mgNew20();
MgGame *mgNew21();
MgGame *mgNew22();
MgGame *mgNew23();
