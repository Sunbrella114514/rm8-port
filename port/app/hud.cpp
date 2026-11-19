// Game HUD (gmaindlg) and the modal dialogs of the turn flow (okcancel, business, ginfo), after docs/ui_system.md
// 5.2 and the HUD code (CUi_gmaindlg__vf10_Init @0x4e9550, Role_UpdateUI @0x481120, CUi_gmaindlg_DrawSpriteSequences
// @0x4e9ef0, Role_InitDate @0x4e9da0).
#include "turn.h"
#include "ui.h"
#include "cards.h"
#include "menu.h"
#include "stock.h"
#include "infoscreens.h"
#include "balance.h"
#include "present.h"
#include "audio.h"
#include "scene.h"
#include "rules_plus.h"
void rm8HudSeatClick(int seat);
#include <cstdio>
#include <cstdlib>

using namespace rw;

namespace {
Rm8Ui *g_main = nullptr, *g_ok = nullptr, *g_biz = nullptr, *g_info = nullptr, *g_go = nullptr, *g_supply = nullptr;
bool g_goShown = false, g_goHot = false;
bool g_evGo = false; void (*g_evThrow)(void *) = nullptr; void *g_evArg = nullptr;	// the event dialogs' one-die go panel
int g_goFace[3] = {0, 0, 0};
int g_goSig = -1;	// vehicle / slot flags the go panel shows (Role_OnBeforeDice_004813c0 runs when they change)
// [V] the slot flags of the dialog (+0xC..+0x14, set 1 by Cls_5e23e8__vf10_OnLoad @0x4e8680, then u_Ui_SetSlotState from the role's flags): slot 1 is always on;
// the one-die event panel (g_evGo) has no toggles
bool goSlotOn(int k)
{
	if(k == 0) return true;
	int cur = rm8TurnCurrent();
	return !g_evGo && cur >= 0 && rm8TurnDiceSlotOn(cur, k + 1);
}
// [V] Dice_RollAndDisplay @0x4e8870: the slots whose flag is on (widgets 2..4) show random faces (uvlist 0x2C5 + face); a slot that is off keeps its face and picture. all: the load-time call (all flags 1)
void goRollFaces(bool all = false){ for(int k = 0; k < 3; k++){ if(!all && !goSlotOn(k)) continue; g_goFace[k] = rand() % 6; rm8UiSetPic(g_go, 2 + k, 0x2C5 + g_goFace[k]); } }
// [V] Role_OnBeforeDice_004813c0 @0x4813c0: Dice_HideSlotsByCount @0x4e8950 (widgets 3 / 4 by the vehicle's dice count, their toggle buttons 5 / 6 are their children),
// then u_Ui_SetSlotState @0x4e89c0 for the slots 2 / 3: picture 0x2C5 + face when on, 0x35E + face (the greyed die) when off
void goShowSlots(int maxDice)
{
	for(int k = 1; k < 3; k++){
		bool vis = maxDice > k;
		rm8UiSetVisible(g_go, 2 + k, vis);
		if(vis) rm8UiSetPic(g_go, 2 + k, (goSlotOn(k) ? 0x2C5 : 0x35E) + g_goFace[k]);
	}
}
int goSignature(){ int cur = rm8TurnCurrent(); int m = g_evGo ? 1 : rm8TurnDiceMax(cur); return m | (goSlotOn(1) ? 4 : 0) | (goSlotOn(2) ? 8 : 0); }
int g_modalSeat = -1;
std::string g_pic[8];
// [R] rules_plus B3: small reddish portraits of the roles a seat hates, under its portrait (gmaindlg label 27 + k at x 684 / 755 / 826 / 896, its portrait button 1 + k at (32, 20) 64 x 64 inside it: the free
// band is y 88 .. 128). up to 3 icons of 20 design pixels (64 px for 3 with 2 px gaps = the portrait's width), a dark red frame behind each, the picture tinted red (colour multiply)
const int HATE_MAX = 3;
int g_hateBg[4][HATE_MAX], g_hatePic[4][HATE_MAX], g_hateSig[4] = {-2, -2, -2, -2};
void hateIconsInit()
{
	for(int &g : g_hateSig) g = -2;
	for(int k = 0; k < 4; k++) for(int j = 0; j < HATE_MAX; j++){
		g_hateBg[k][j] = rm8UiAddWidget(g_main, "plane", 0, 0, 0, 22, 22, -1, 0xF0801818, 16, 9);
		g_hatePic[k][j] = rm8UiAddWidget(g_main, "plane", 0, 0, 0, 20, 20, -1, 0, 16, 10);
		rm8UiSetIgnore(g_main, g_hateBg[k][j], true); rm8UiSetIgnore(g_main, g_hatePic[k][j], true);
		rm8UiSetTint(g_main, g_hatePic[k][j], 0xFFFF8484);
		rm8UiSetVisible(g_main, g_hateBg[k][j], false); rm8UiSetVisible(g_main, g_hatePic[k][j], false);
	}
}
// [R] user 2026-11-18: every seat permanently shows the OTHER seats (seat order) under its portrait; the red of each mini portrait grows with the hate score
// (0 = untinted, >= the hate threshold = full red); the roles it actively hates (rm8HateList, the top hateMaxCount) also get a bright red frame
void hateIconsUpdate()
{
	const Rm8PlusTuning &t = rm8PlusTuning();
	int lv = std::max(1, std::min(3, rm8RulesLevel())) - 1;
	float thr = std::max(0.1f, t.hateThreshold[lv]);
	for(int k = 0; k < 4; k++){
		int oth[8], n = 0, hl[8], nh = 0;
		bool show = rm8RulesOn() && k < rm8TurnRoleCount() && !g_pic[k].empty() && rm8TurnStatus(k) != 7;
		if(show){
			for(int a = 0; a < rm8TurnRoleCount() && a < 8 && n < HATE_MAX; a++) if(a != k && !g_pic[a].empty() && rm8TurnStatus(a) != 7) oth[n++] = a;
			nh = rm8HateList(k, hl);
		}
		int lvl[HATE_MAX] = {0, 0, 0}; bool act[HATE_MAX] = {false, false, false};
		int sig = 7;
		for(int j = 0; j < n; j++){
			float f = std::max(0.0f, std::min(1.0f, rm8HateScore(k, oth[j]) / thr));
			lvl[j] = (int)(f * 8.0f + 0.5f);	// 9 shades
			for(int h = 0; h < nh; h++) act[j] |= hl[h] == oth[j];
			sig = sig * 31 + oth[j] * 20 + lvl[j] * 2 + (act[j] ? 1 : 0);
		}
		if(sig == g_hateSig[k]) continue;
		g_hateSig[k] = sig;
		float lx = 0, ly = 0, lw, lh, bx = 0, by = 0, bw = 64, bh = 64;
		rm8UiGetRect(g_main, 27 + k, lx, ly, lw, lh); rm8UiGetRect(g_main, 1 + k, bx, by, bw, bh);
		float x0 = lx + bx + (bw - (n * 22 + (n > 0 ? (n - 1) * 1 : 0))) / 2, y0 = ly + by + bh + 5;
		for(int j = 0; j < HATE_MAX; j++){
			bool on = j < n;
			if(on){
				float x = x0 + j * 23, f = lvl[j] / 8.0f;
				unsigned gb = (unsigned)(255.0f * (1.0f - 0.8f * f));	// white -> deep red
				rm8UiSetTint(g_main, g_hatePic[k][j], 0xFF000000u | 0x00FF0000u | (gb << 8) | gb);
				rm8UiSetBColor(g_main, g_hateBg[k][j], act[j] ? 0xF0E01010u : 0xC0302828u);	// active hate: a bright red frame; otherwise a neutral dark one
				rm8UiSetRect(g_main, g_hateBg[k][j], x, y0, 22, 22); rm8UiSetRect(g_main, g_hatePic[k][j], x + 1, y0 + 1, 20, 20);
				rm8UiSetPicFile(g_main, g_hatePic[k][j], "prt\\npc\\" + g_pic[oth[j]] + "_12.tga");
			}
			rm8UiSetVisible(g_main, g_hateBg[k][j], on); rm8UiSetVisible(g_main, g_hatePic[k][j], on);
		}
	}
}
const char *NUM2 = "font\\number2.tga";
const char *NUM2_CODES = "12345abcde67890fgh  ";	// [V] 0x5e6424, 5 x 4 cells of 25.6 x 32
}

void rm8HudInit(const std::vector<std::string> &pics)
{
	rm8InfoReset();
	g_main = rm8UiLoad("gmaindlg", RM8UI_HUD);
	g_ok = rm8UiLoad("okcancel", RM8UI_DIALOG);
	g_biz = rm8UiLoad("business", RM8UI_DIALOG);
	g_info = rm8UiLoad("ginfo", RM8UI_DIALOG);
	g_go = rm8UiLoad("go", RM8UI_FLOAT);
	g_supply = rm8UiLoad("supply", RM8UI_DIALOG);	// [V] Role_OpenModalUi 0x87 @0x4883c0: the auto play question (the question is a picture of the .ui itself)
	for(size_t k = 0; k < 8; k++) g_pic[k] = k < pics.size() ? pics[k] : "";
	if(g_main){
		// [H] the four seat hexagons (labels 27..30 with buttons 1..4) show npc\p_NN_12; empty seats are hidden
		for(int k = 0; k < 4; k++){
			if(!g_pic[k].empty()) rm8UiSetPicFile(g_main, k + 1, "prt\\npc\\" + g_pic[k] + "_12.tga");
			else { rm8UiSetVisible(g_main, 27 + k, false); rm8UiSetVisible(g_main, k + 1, false); }
		}
		hateIconsInit();	// [R] rules_plus B3 (hidden until somebody hates somebody)
		rm8CardsHudInit(g_main);	// the card bar: widgets 11..18 pictures, 19..26 names, 31..38 buttons (cards.cpp)
		rm8UiOnClick(g_main, [](int id){ if(id >= 1 && id <= 4) rm8HudSeatClick(id - 1); else if(id >= 31 && id <= 38) rm8CardsOnSlotClick(id - 31); else if(id == 10) rm8MenuGameSystemOpen(true); else if(id == 7) rm8StockOpenFromHud(rm8TurnCurrent()); else if(id == 6 || id == 8 || id == 9) rm8InfoOpenFromHud(id, rm8TurnCurrent()); });	// [V] CGamePlayScene vf12 @0x4a2e10: 6 -> 0x67 wincondition, 8 -> 0x66 playerinfo, 9 -> 0x65 minimap (infoscreens.cpp)	// [V] Card_OnSelected @0x4e9340
		rm8UiShow(g_main, true);
	}
	// [V] dialog buttons -> the scene's vf12(id) -> the waiting role's result: OK 2 -> 1, CANCEL 3 -> 0, business 2..4
	if(g_ok) rm8UiOnClick(g_ok, [](int id){ if(id == 3 && rm8CardsCancelPick()) return; if(id == 2 || id == 3) rm8TurnModalResult(g_modalSeat, id == 2 ? 1 : 0); });
	if(g_biz) rm8UiOnClick(g_biz, [](int id){ if(id >= 2 && id <= 4) rm8TurnModalResult(g_modalSeat, id); });
	if(g_supply) rm8UiOnClick(g_supply, [](int id){ if(id == 2 || id == 3) rm8TurnModalResult(g_modalSeat, id == 2 ? 1 : 0); });	// [V] like okcancel: OK 2 -> 1, CANCEL 3 -> 0; message 0x79 @0x481df0 acts on a non-zero result
	// [V] Cls_5e23e8__vf10_OnLoad @0x4e8680: button 1 -> Game_TriggerRollDice (state 6 only)
	if(g_go){ rm8UiOnClick(g_go, [](int id){ if((id == 5 || id == 6) && !g_evGo){ rm8TurnToggleDiceSlot(rm8TurnCurrent(), id - 3); return; }	// [V] Cls_5e23e8__vf10_OnLoad @0x4e8680: widgets 5 / 6 -> Ui_ToggleDiceSlotOnSelect @0x4e8410 (slot 2 / 3; state 6 only)
		if(id != 1) return; if(g_evGo){ if(g_evThrow) g_evThrow(g_evArg); } else rm8RolesHumanRoll(); }); goRollFaces(true); }
}

// [V] u_Game_OnButton_RoleFindNext @0x4e8e90 -> Role_FindNext @0x4983f0 (seat hexagons 1..4): only while the current role waits for the dice (state 6),
// a seat that is not bankrupt: its previous sample stopped, the role's line strId + 0x1F or (rand odd) + 0x1D spoken, Camera_SetFocusPoint(role)
void rm8HudSeatClick(int seat)
{
	static int voice = 0;
	int cur = rm8TurnCurrent();
	// [A] touch hook for the F1..F4 keys: a tap on the hexagon of a seat that the computer plays gives it back to the human (the original has only the keys, rm8AutoKey)
	if(seat >= 0 && rm8TurnAuto(seat) && rm8AutoKey(seat)) return;
	if(getenv("RM8_DEBUG_HUD")) rm8Log("hud: seat %d clicked (current %d state %d)", seat, cur, cur >= 0 ? rm8TurnState(cur) : -1);
	if(cur < 0 || rm8TurnState(cur) != 6 || seat < 0 || seat >= rm8TurnRoleCount() || rm8TurnStatus(seat) == 7) return;
	int cls = (rm8GameRand() & 1) ? 0x1D : 0x1F;
	if(voice){ rm8SndStop(voice); voice = 0; }
	Rm8SpeechLine l;
	if(rm8SpeechParse(rm8PresentStrId(seat) + cls, l) && l.voice > 0) voice = rm8SndPlay(l.voice, 1);
	float p[3]; if(rm8RolesGetPos(seat, p)) rm8TurnFocus(p);
}

// [V] Role_OpenModalUi @0x4883c0: 0x80 okcancel (text in label 1), 0x81 business
void rm8HudOpenModal(int seat, int kind, const std::string &text)
{
	g_modalSeat = seat;
	if(kind == 0x87 && g_supply) rm8UiShow(g_supply, true);
	else if(kind == 0x81 && g_biz) rm8UiShow(g_biz, true);
	else if(g_ok){
		// both buttons back: a card target selection shows this dialog with the OK button hidden (cards.cpp openPick), which used to
		// leave the next buy / build question without its OK button
		rm8UiSetVisible(g_ok, 2, true); rm8UiSetVisible(g_ok, 3, true); rm8UiSetEnabled(g_ok, 2, true); rm8UiSetEnabled(g_ok, 3, true);
		rm8UiSetText(g_ok, 1, text); rm8UiShow(g_ok, true);
	}
}

static bool g_gtMine = false;	// gtinfo.ui is shared with present.cpp: only hidden here when this file showed it
void rm8HudCloseModal(){ if(getenv("RM8_DEBUG_HUD")) rm8Log("hud: close modal"); rm8UiShow(g_ok, false); rm8UiShow(g_biz, false); rm8UiShow(g_supply, false); rm8UiShow(g_info, false); if(g_gtMine){ g_gtMine = false; rm8UiShow(rm8UiLoad("gtinfo", RM8UI_DIALOG), false); } }

static bool g_shotTrig = false;
bool rm8HudShotTriggered(){ return g_shotTrig; }
void rm8HudInfo(const std::string &text){ if(const char *k = getenv("RM8_SHOT_ON_INFO")) if(text.find(k) != std::string::npos) g_shotTrig = true; { static int n = 0; n++; if(const char *k = getenv("RM8_SHOT_ON_INFO_N")) if(n == atoi(k)) g_shotTrig = true; }	/* the n-th info box (Windows passes a Chinese RM8_SHOT_ON_INFO in the ANSI code page, not UTF-8) */ if(getenv("RM8_DEBUG_HUD")) rm8Log("hud: info [%s]%s", text.c_str(), g_info ? "" : " (no ginfo)"); if(g_info){ rm8InfoBoxYield(); rm8UiSetText(g_info, 1, text); rm8UiShow(g_info, true); } }

void rm8HudInfoPic(const std::string &text, int uvlId)
{
	rm8HudInfo(text);	// the counters / triggers / the shared-box hand-over of the plain box, then swapped for the picture box
	Rm8Ui *gt = rm8UiLoad("gtinfo", RM8UI_DIALOG);
	if(!gt || !g_info) return;
	rm8UiShow(g_info, false);
	rm8UiSetText(gt, 1, rm8UiWrapText(text, 222.0f, 16)); rm8UiSetPic(gt, 2, uvlId);
	rm8UiShow(gt, true); g_gtMine = true;
}

void rm8HudInfoPicFile(const std::string &text, const std::string &pack)
{
	rm8HudInfo(text);
	Rm8Ui *gt = rm8UiLoad("gtinfo", RM8UI_DIALOG);
	if(!gt || !g_info) return;
	rm8UiShow(g_info, false);
	rm8UiSetText(gt, 1, rm8UiWrapText(text, 222.0f, 16)); rm8UiSetPicFile(gt, 2, pack);
	rm8UiShow(gt, true); g_gtMine = true;
}

void rm8HudSeatBankrupt(int seat){ if(g_main && seat >= 0 && seat < 4 && !g_pic[seat].empty()) rm8UiSetPicFile(g_main, seat + 1, "prt\\npc\\" + g_pic[seat] + "_14.tga"); }

void rm8HudRender(Camera *cam)
{
	if(!g_main) return;
	int cur = rm8TurnCurrent();
	if(cur >= 0 && cur < 8 && !g_pic[cur].empty()) rm8UiSetPicFile(g_main, 5, "prt\\npc\\" + g_pic[cur] + ".tga");	// [V] Role_UpdateUI: npc\<picName>
	// [V] Game_ToggleCharacterVisibility @0x4a6f90 (every frame): state 6 shows the go panel and gmaindlg buttons 6..10
	bool waiting = rm8TurnState(cur) == 6;
	for(int id = 6; id <= 10; id++) rm8UiSetVisible(g_main, id, waiting);
	waiting = waiting || g_evGo;
	if(g_go){
		if(waiting && !g_goShown){	// [V] Role_OnBeforeDice_004813c0 + vf7: slots 3 / 4 by the dice count, new faces
			goRollFaces();
			g_goSig = goSignature(); goShowSlots(g_goSig & 3);
		}else if(waiting && goSignature() != g_goSig){	// [V] the vehicle changed / a slot was toggled: Role_OnBeforeDice_004813c0(0) redraws the slots (the faces stay)
			g_goSig = goSignature(); goShowSlots(g_goSig & 3);
		}
		if(!waiting) g_goSig = -1;
		g_goShown = waiting; rm8UiShow(g_go, waiting);
		// [V] MouseOn callbacks 5 / 6 of button 1: rolling picture 0x200 on the slots that are on (Dice_ShowRollingImagePlaySound @0x4e8780), new random faces when left
		bool hot = waiting && rm8UiHot(g_go, 1);
		if(hot && !g_goHot) for(int k = 0; k < 3; k++) if(goSlotOn(k)) rm8UiSetPic(g_go, 2 + k, 0x200);
		if(!hot && g_goHot) goRollFaces();
		g_goHot = hot;
	}
	rm8StockTick();	// stock.cpp: test hooks of the stock board
	rm8InfoTick(cam);	// infoscreens.cpp: scroll bar / slider drags, the minimap hover, test hooks
	rm8CardsHudUpdate(g_main, cam);
	hateIconsUpdate();
	rm8UiRender(cam);
	rm8CardsHudRender(cam);
	// [V] number2 sprite strings at fixed 1024x768 positions, scale s, spacing -6 s
	float sx, sy; rm8UiHudScale(sx, sy);
	int y, mo, d, wd; rm8GameDate(y, mo, d, wd);
	char wch[2] = {wd >= 1 && wd <= 7 ? (char)('a' + wd - 1) : 'g', 0};
	char mtxt[8], dtxt[8], c1[16], c2[16], c3[16], c4[16];
	snprintf(mtxt, sizeof mtxt, "%2d", mo); snprintf(dtxt, sizeof dtxt, "%2d", d);
	int32_t cash, dep; rm8GameHud(cur, cash, dep);	// [N] int32_t
	snprintf(c1, sizeof c1, "%d", cash); snprintf(c2, sizeof c2, "%d", dep); snprintf(c3, sizeof c3, "%d", rm8GamePoints(cur)); snprintf(c4, sizeof c4, "%d", rm8GamePI());
	struct { const char *t; float x, y; } items[] = {{wch, 105, 15}, {mtxt, 34, 38}, {dtxt, 96, 38}, {c1, 182, 38}, {c2, 365, 38}, {c3, 550, 38}, {c4, 640, 38}};
	if(!rm8StockUiOpen() && !rm8BalanceActive() && !rm8InfoFullScreen())	// [A] the stock dialogs, the balance screen and the info screens cover the whole screen: the number sprites would be drawn over them
		for(auto &it : items) rm8UiSheetText(cam, NUM2, 5, 4, 25.6f, 32.0f, NUM2_CODES, it.t, it.x * sx, it.y * sy, sy, -6.0f * sy);
}
Rm8Ui *rm8HudMain(){ return g_main; }

// test hook (docs/autoplay.md): RM8_AUTO_PLAY=<s>: from <s> seconds of play, the first time a human seat waits for the dice, press the real buttons - HUD button 10
// (gsys), gsys button 1 (auto play), supply OK - through the pointer path (hit test, press and release). RM8_AUTO_PLAY_OFF=<s>: <s> seconds after that, ask the app
// to push the F-key of the seat (keyboard path of Game_GetPlayerByIndex @0x4a6cf0); RM8_AUTO_PLAY_KEY=1: the on switch is that key too (Game_GetPlayerAtSeat @0x4a6c30).
// Returns seat + 1 when the key is due.
int rm8HudAutoPlayTest(float dt)
{
	static const char *on = getenv("RM8_AUTO_PLAY"), *off = getenv("RM8_AUTO_PLAY_OFF");
	static float t = 0, wait = 0, tOn = 0; static int step = 0, seat = -1;
	if(!on && !off) return 0;
	t += dt;
	if(wait > 0){ wait -= dt; return 0; }
	auto press = [&](Rm8Ui *u, int id){
		float x, y, w, h;
		if(!u || !rm8UiGetScreenRect(u, id, x, y, w, h)){ rm8Log("autoplay test: no button %d", id); return false; }
		float cx = x + w / 2, cy = y + h / 2;
		rm8UiPointerMove(cx, cy); rm8UiPointerDown(cx, cy); rm8UiPointerUp(cx, cy);
		return true;
	};
	int cur = rm8TurnCurrent();
	{ static int lc = -1, ls = -1; int st = cur >= 0 ? rm8TurnState(cur) : -1; if(cur != lc || st != ls){ lc = cur; ls = st; rm8Log("autoplay test: t %.1f step %d cur %d state %d human %d auto %d", t, step, cur, st, (int)rm8TurnHuman(cur), (int)rm8TurnAuto(cur)); } }
	if(step == 0 && on && t >= atof(on) && cur >= 0 && rm8TurnHuman(cur) && rm8TurnState(cur) == 6 && !rm8StockUiOpen() && !rm8InfoVisible()){
		seat = cur;
		if(getenv("RM8_AUTO_PLAY_KEY")){ rm8Log("autoplay test: F%d (on)", seat + 1); step = 3; tOn = t; return seat + 1; }	// the keyboard path of Game_GetPlayerAtSeat instead of the buttons
		rm8Log("autoplay test: seat %d presses HUD button 10 (system menu)", seat);
		if(press(g_main, 10)){ step = 1; wait = 0.4f; }
	}else if(step == 1){
		rm8Log("autoplay test: gsys button 1 (auto play)");
		if(press(rm8UiLoad("gsys", RM8UI_FLOAT), 1)){ step = 2; wait = 0.4f; }
	}else if(step == 2){
		rm8Log("autoplay test: supply OK");
		if(press(g_supply, 2)){ step = 3; tOn = t; }
	}else if(step == 3 && off && t - tOn >= atof(off)){
		rm8Log("autoplay test: F%d", seat + 1); step = 4; return seat + 1;
	}
	else if(step == 4 && getenv("RM8_AUTO_PLAY_ROLL") && cur == seat && rm8TurnHuman(cur) && rm8TurnState(cur) == 6){	// control is back: the human throws with the real dice button
		rm8Log("autoplay test: the human presses the dice button"); press(g_go, 1); step = 5;
	}
	return 0;
}
void rm8HudDieGo(bool on, float px, float py, void (*onThrow)(void *), void *arg)
{
	if(!g_go) return;
	g_evGo = on; g_evThrow = onThrow; g_evArg = arg; g_goShown = false;	// re-roll the faces / slots next frame
	if(on) rm8UiSetRootPos(g_go, px, py); else { float sx, sy; rm8UiHudScale(sx, sy); rm8UiSetRootPos(g_go, 804.0f * sx, 86.0f * sy); }	// go.ui root (804, 86)
	if(!on) rm8UiShow(g_go, false);
	else { rm8UiSetVisible(g_go, 3, false); rm8UiSetVisible(g_go, 4, false); goRollFaces(); rm8UiShow(g_go, true); g_goShown = true; }	// [V] Cls_5e2538__vf10_Init @0x4e8b80: slots 3 / 4 hidden (one die)
}
bool rm8HudDieGoRect(float &x, float &y, float &w, float &h){ return g_go && g_evGo && rm8UiVisible(g_go) && rm8UiGetScreenRect(g_go, 1, x, y, w, h); }
