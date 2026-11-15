// The save / load dialog of the board (saveload.ui, 10 slots; cover.ui = "overwrite?"), after Cls_5e4428 (CUi_saveload, opened by gsys buttons 2 / 3),
// CSaveUI__vf12_HandleSaveSlot @0x4ac2a0 and Game_DisplaySaveSlotList @0x4ac420. [V] = decompile, [A] port adaptation.
// [V] saveload.ui: buttons 1..10 are the slots (caption = Game_DisplaySaveSlotList: "<map / role name>^^M月D日H時M分" = string 7101, or string 7102 "空"),
// plane 11 (picture 992) = the "save" title, plane 12 (picture 994) = the "load" title. Save mode: an empty slot is written at once
// (Game_SetRecordSlotName -> game state 6), a used slot first asks through cover.ui (OK 2 / CANCEL 3, CUi_cover__ShowUI); load mode: Game_SetRecordSlotName2
// -> game state 7. [A] load mode disables the empty slots; the original dialog has no close button (right click): the port adds one at the bottom and
// closes on Esc / the Android back key (rm8_app.cpp).
#include "save.h"
#include "ui.h"
#include <cstdio>
#include <cstdlib>

namespace {

Rm8Ui *g_ui = nullptr, *g_cover = nullptr;
bool g_saveMode = true;
int g_closeBtn = -1, g_coverSlot = 0, g_lbl[11] = {0};	// g_lbl[s]: the caption label of slot button s (a runtime label: the .ui button captions of the 64 px tall overlapping bars do not line up)

bool multi(){ return rm8SaveBoardOn() ? rm8SaveMulti() : false; }

void refresh()
{
	if(!g_ui) return;
	bool m = multi();
	for(int s = 1; s <= 10; s++){
		Rm8SaveSlotInfo i; bool has = rm8SaveSlotInfo(s, m, i);
		rm8UiSetText(g_ui, g_lbl[s], rm8SaveSlotLabel(s, m));
		rm8UiSetColor(g_ui, g_lbl[s], has ? 0xff301800 : 0xff8a6a3a);
		rm8UiSetEnabled(g_ui, s, g_saveMode || has);
	}
	rm8UiSetVisible(g_ui, 11, g_saveMode);
	rm8UiSetVisible(g_ui, 12, !g_saveMode);
}

void doSave(int slot)
{
	rm8SaveRequest(slot, multi());
	rm8SaveUiClose();
}

void init()
{
	if(g_ui) return;
	g_ui = rm8UiLoad("saveload", RM8UI_DIALOG);
	g_cover = rm8UiLoad("cover", RM8UI_DIALOG);
	if(!g_ui) return;
	// [A] the slot buttons of saveload.ui are 64 px tall but only 26 px apart (the bar art sits in the middle and is narrower than the rectangle): three buttons
	// overlap at every bar, so a tap at a bar could hit its neighbour. Each button gets the 26 px band of its bar (same centre, art unchanged) and a caption label
	for(int k = 1; k <= 10; k++){
		float top = 37 + 26 * (k - 1) + 19;
		rm8UiSetRect(g_ui, k, 0, top, 512, 26);
		g_lbl[k] = rm8UiAddWidget(g_ui, "label", 0, 112, top - 1, 290, 28, -1, 0, 14, 9);
		rm8UiSetIgnore(g_ui, g_lbl[k], true);
		char nm[24]; snprintf(nm, sizeof nm, "dyn%d", g_lbl[k]);
		rm8UiSetAlignN(g_ui, nm, 0, 1);
	}
	g_closeBtn = rm8UiAddWidget(g_ui, "button", 0, 192, 342, 128, 28, -1, 0xffffd24a, 16, 9);
	rm8UiSetText(g_ui, g_closeBtn, u8"返回");
	{ char nm[24]; snprintf(nm, sizeof nm, "dyn%d", g_closeBtn); rm8UiSetAlignN(g_ui, nm, 1, 1); rm8UiSetColorN(g_ui, nm, 0xff6a3a00); }
	rm8UiOnClick(g_ui, [](int id){
		if(id == g_closeBtn){ rm8SaveUiClose(); return; }
		if(id < 1 || id > 10) return;
		bool m = multi();
		if(!g_saveMode){
			Rm8SaveSlotInfo i; if(!rm8SaveSlotInfo(id, m, i)) return;
			rm8SaveRequestLoad(rm8SaveSlotPath(id, m));	// the app rebuilds the board outside of the UI callback
			rm8SaveUiClose();
			return;
		}
		Rm8SaveSlotInfo i;
		if(rm8SaveSlotInfo(id, m, i) && g_cover){ g_coverSlot = id; rm8UiShow(g_cover, true); }	// [V] a used slot: cover.ui
		else doSave(id);
	});
	if(g_cover) rm8UiOnClick(g_cover, [](int id){
		if(id == 2){ rm8UiShow(g_cover, false); doSave(g_coverSlot); }
		else if(id == 3) rm8UiShow(g_cover, false);
	});
}

}	// namespace

void rm8SaveUiOpen(bool saveMode)
{
	init();
	if(!g_ui) return;
	g_saveMode = saveMode;
	refresh();
	if(g_cover) rm8UiShow(g_cover, false);
	rm8UiShow(g_ui, true);
}

void rm8SaveUiClose()
{
	if(g_cover) rm8UiShow(g_cover, false);
	if(g_ui) rm8UiShow(g_ui, false);
}

bool rm8SaveUiVisible(){ return (g_ui && rm8UiVisible(g_ui)) || (g_cover && rm8UiVisible(g_cover)); }

void rm8SaveUiTick(){}

// test tokens of RM8_MENU_AUTO (menu.cpp forwards "savedlg" / "loaddlg" / "slot:N" / "cover:ok|cancel" / "dlgclose"): the real pointer path
bool rm8SaveUiAuto(const std::string &t)
{
	auto tap = [](Rm8Ui *u, int id){
		float x, y, w, h;
		if(!u || !rm8UiGetScreenRect(u, id, x, y, w, h)){ rm8Log("save ui auto: no widget %d", id); return; }
		float cx = x + w / 2, cy = y + h / 2;
		rm8UiPointerMove(cx, cy); rm8UiPointerDown(cx, cy, true); rm8UiPointerUp(cx, cy);
	};
	if(t == "savedlg"){ rm8SaveUiOpen(true); return true; }
	if(t == "loaddlg"){ rm8SaveUiOpen(false); return true; }
	if(t == "dlgclose"){ rm8SaveUiClose(); return true; }
	if(t.rfind("slot:", 0) == 0){ tap(g_ui, atoi(t.c_str() + 5)); return true; }
	if(t == "cover:ok"){ tap(g_cover, 2); return true; }
	if(t == "cover:cancel"){ tap(g_cover, 3); return true; }
	return false;
}
