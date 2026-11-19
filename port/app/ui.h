// UI library for the port, after docs/ui_system.md: .ui files (decrypted by scripts/export_ui.py), widget tree
// (Parent / Index), pictures from uvlisttable (texture + frame UV), text with a TrueType CJK font (the game draws
// GDI <str#21> glyphs; [A] the port uses a system serif CJK font through stb_truetype), touch/mouse input.
#pragma once
#include "scene.h"
#include <functional>

struct Rm8Ui;	// a loaded .ui (one root widget)

// [A] font candidates (a face index for .ttc files): the first one that opens and has CJK glyphs is used (rm8UiInit then keeps it)
struct Rm8FontCandidate { std::string file; int index; };
bool rm8UiPickFont(const std::vector<Rm8FontCandidate> &cands, std::string *usedFile = nullptr, int *usedIndex = nullptr);
// [A] a full-screen text notice that needs no game data (first-run problems); draws between cam->beginUpdate / endUpdate; rm8UiPickFont first
void rm8UiDrawNotice(rw::Camera *cam, int W, int H, const std::string &title, const std::string &body);
// one-time setup: data dir (scenes/ui) and the font file (+ face index of a .ttc)
bool rm8UiInit(const std::string &uiDir, const std::string &fontFile, int fontIndex);
// screen size in pixels (call when the window changes)
void rm8UiSetScreen(int w, int h);
// [V] layout modes: Hud = whole-screen tree (UI_ScaleWidgetTree), Dialog = centred box (UI_ScaleDialogPosition);
// [A] Float = a small panel whose root position follows the screen (x * W/1024, y * H/768), size scaled by H/768
enum Rm8UiMode { RM8UI_HUD, RM8UI_DIALOG, RM8UI_FLOAT };
Rm8Ui *rm8UiLoad(const std::string &name, Rm8UiMode mode);	// cached per name
void rm8UiShow(Rm8Ui *u, bool show);
bool rm8UiVisible(Rm8Ui *u);
// widget access by ID (the game's FindById) or name (FindByName); returns false if missing
bool rm8UiSetText(Rm8Ui *u, int id, const std::string &utf8);
bool rm8UiSetTextByName(Rm8Ui *u, const char *name, const std::string &utf8);
bool rm8UiSetPic(Rm8Ui *u, int id, int uvlId);			// NormalPic (and the other states) by uvlist id
bool rm8UiSetPicFile(Rm8Ui *u, int id, const std::string &texKey);	// SetPicName: a texture pack key
bool rm8UiSetPicFileUV(Rm8Ui *u, int id, const std::string &texKey, float u0, float v0, float u1, float v1);	// [A] the same, drawing only that part of the texture (sprite sheet cell)
bool rm8UiSetVisible(Rm8Ui *u, int id, bool v);
bool rm8UiSetEnabled(Rm8Ui *u, int id, bool v);
// click handler: called with the widget ID on a click (press and release on the same enabled widget)
void rm8UiOnClick(Rm8Ui *u, std::function<void(int id)> fn);
// true while the pointer is over (mouse) or pressing (touch) the widget: the game's MouseOn callbacks 5 / 6
bool rm8UiHot(Rm8Ui *u, int id);
// by widget name (case-insensitive FindByName), for .ui files whose widgets all have ID 0 (bank, cardShop, ...)
bool rm8UiSetVisibleN(Rm8Ui *u, const char *name, bool v);
bool rm8UiSetEnabledN(Rm8Ui *u, const char *name, bool v);
bool rm8UiSetFontSizeN(Rm8Ui *u, const char *name, int px);	// [A] a widget's text size in design pixels (GDI cells are about 6% narrower than the port's glyph advance)
bool rm8UiSetPicN(Rm8Ui *u, const char *name, int uvlId);
bool rm8UiSetTextureN(Rm8Ui *u, const char *name, rw::Texture *t);	// live texture (camera raster) drawn in the widget rect; nullptr clears
bool rm8UiSetTextureExactN(Rm8Ui *u, const char *name, rw::Texture *t);	// same, the texture fills the whole widget rect 1:1 (a screen-sized raster)
bool rm8UiSetPicFileN(Rm8Ui *u, const char *name, const std::string &texKey);
bool rm8UiSetColorN(Rm8Ui *u, const char *name, unsigned argb);	// text colour (0xAARRGGBB)
bool rm8UiHotN(Rm8Ui *u, const char *name);
void rm8UiOnClickName(Rm8Ui *u, std::function<void(const std::string &name)> fn);	// used when no OnClick(id) handler is set
bool rm8UiSetIgnoreN(Rm8Ui *u, const char *name, bool ignore);	// ignored widgets are transparent for taps (their children still get them)
void rm8UiSetIgnoreAll(Rm8Ui *u, bool ignore);
bool rm8UiGetScreenRectN(Rm8Ui *u, const char *name, float &x, float &y, float &w, float &h);	// pixels (after layout)
std::string rm8UiHitName(float x, float y);	// debug: "ui/widget" under a screen point (what a tap would hit)
bool rm8UiSetRectN(Rm8Ui *u, const char *name, float x, float y, float w, float h);	// design units relative to the parent (the game's SetPosY / SetPosX of a named widget)
bool rm8UiGetRectN(Rm8Ui *u, const char *name, float &x, float &y, float &w, float &h);	// design units relative to the parent
bool rm8UiSetAlignN(Rm8Ui *u, const char *name, int h, int v);	// UiLib_Label_SetAlignment / SetVAlign: h 0 left 1 mid 2 right, v 0 top 1 mid 4 bottom
// input in screen pixels; returns true if the UI consumed it
// [A] touch = true for a finger: holding it >= 0.5 s is an inspection (the mouse hover of the original: tooltips, hover
// pictures) and its release does not click; a quick tap clicks as before
bool rm8UiPointerDown(float x, float y, bool touch = false);
bool rm8UiPointerUp(float x, float y);
bool rm8UiPressIsTouch();	// a finger is down on a widget
void rm8UiPointerPos(float &x, float &y);	// [A] the last pointer position (screen pixels: press, release, mouse move, finger move of a UI press)
bool rm8UiPressed(Rm8Ui *u, int id);		// [A] widget `id` is under a pointer press (slider drags poll it)
bool rm8UiSetRootPos(Rm8Ui *u, float px, float py);	// screen pixels; dialog-mode UIs
void rm8UiPointerMove(float x, float y);
void rm8UiRender(rw::Camera *cam);	// all visible UIs, after the 3D scene
void rm8UiRenderLate(rw::Camera *cam);	// [N] the UIs marked late (rm8UiSetLate): on top of the menu's 3D dolls
void rm8UiSetLate(Rm8Ui *u, bool late);
// string.bin text (UTF-8); "" if missing
const std::string &rm8Str(int id);
// the game's text conventions: '\' line break, '^' space ([A]), printf-style %s/%d already applied by the caller
std::string rm8StrFmt(int id, ...);
// sprite-sheet text (number fonts): texture key, cells, character codes per cell; position in pixels
void rm8UiSheetText(rw::Camera *cam, const std::string &key, int cols, int rows, float cw, float ch, const char *codes,
                    const char *txt, float x, float y, float scale, float spacing);
// HUD scale: positions x * sx (width / 1024), y and sizes * sy (height / 768)
void rm8UiHudScale(float &sx, float &sy);
// widget rectangle in 1024x768 design units relative to its parent (the game's UiLib_Widget_SetPosX/Y/Width/Height); Set re-lays the UI out
bool rm8UiGetRect(Rm8Ui *u, int id, float &x, float &y, float &w, float &h);
bool rm8UiSetRect(Rm8Ui *u, int id, float x, float y, float w, float h);
// [V] the CText mouse tooltip (@0x40de60): shown 0.5 s after it was set, at the mouse; "" clears it
void rm8UiSetTooltip(const std::string &utf8);
// [A] menu helpers: hide every UI; add a runtime widget ("label" / "button" / "plane") under parent parentId (0 = root), design units,
// optional picture (uvlist id, -1 none) and background colour; returns its ID. Index = draw order among siblings (ascending).
void rm8UiHideAll();
// hide every shown UI and remember them / show them again (a full-screen mode on top of the board, e.g. a minigame)
void rm8UiPushHidden();
void rm8UiPopHidden();
// a widget that never takes the pointer (decoration drawn over buttons: hits and hover go to what lies below)
bool rm8UiSetIgnore(Rm8Ui *u, int id, bool ignore);
int rm8UiAddWidget(Rm8Ui *u, const char *type, int parentId, float x, float y, float w, float h, int pic = -1, unsigned bcolor = 0, int fontSize = 16, int index = 9);
bool rm8UiSetBColor(Rm8Ui *u, int id, unsigned argb);
bool rm8UiGetScreenRect(Rm8Ui *u, int id, float &x, float &y, float &w, float &h);	// pixels (after layout)
Rm8Ui *rm8UiCreateCanvas(const std::string &name, Rm8UiMode mode);	// empty 1024x768 root, cached per name
bool rm8UiSetTint(Rm8Ui *u, int id, unsigned argb);	// picture colour (0xAARRGGBB, default 0xffffffff): fades
bool rm8UiSetColor(Rm8Ui *u, int id, unsigned argb);		// text colour by widget ID (0xAARRGGBB)
std::string rm8UiWrapText(const std::string &utf8, float width, int fontSize);	// [A] wraps a long text with the '\' line break
// [A] menu animation helpers (menu_anim.cpp): turn a picture about its centre (degrees, Sprite_SetZRotation @0x462490); size of a uvlist picture;
// name of a loaded .ui; an immediate-mode overlay drawn above all UIs (screen pixels): Begin, any Pic / Rect calls, End
bool rm8UiSetRot(Rm8Ui *u, int id, float deg);
bool rm8UiPicSize(int uvlId, float &w, float &h);
const std::string &rm8UiName(Rm8Ui *u);
void rm8UiOverlayBegin(rw::Camera *cam);
void rm8UiOverlayPic(int uvlId, float cx, float cy, float w, float h, float rotDeg, unsigned argb);
void rm8UiOverlayRect(float x, float y, float w, float h, unsigned argb);
void rm8UiOverlayEnd();

// ---- [N] netplay (net.cpp, docs/netplay.md section 3.2). Unset hooks = single player: nothing changes.
struct Rm8UiNetHooks {
	bool (*click)(Rm8Ui *u, int key, int id, const std::string &name) = nullptr;	// a local click on a widget of `u`: true = netplay took it (sent as a command / not the player's turn)
	bool (*netHover)(Rm8Ui *u) = nullptr;	// true = rm8UiHot / rm8UiHotN of this UI report the replicated hover (rm8UiNetApplyHover), not the local pointer
};
Rm8UiNetHooks &rm8UiNetHooks();
Rm8Ui *rm8UiFind(const std::string &name);			// a loaded UI by name (nullptr: never loaded)
bool rm8UiNetApplyClick(const std::string &ui, int key);	// a replicated click: the widget's click handler (key: rm8UiNetLocalHot / the click hook)
void rm8UiNetApplyHover(const std::string &ui, int key);	// the replicated hover (key -1 / ui "" = none)
bool rm8UiNetLocalHot(std::string &ui, int &key);		// the local pointer's pressed / hovered widget on a shown UI
struct Rm8UiButtonInfo { std::string ui, name, type; int key = -1, id = 0; float x = 0, y = 0; };
void rm8UiListButtons(std::vector<Rm8UiButtonInfo> &out);	// test driver: the widgets a tap could click now (screen centres)
