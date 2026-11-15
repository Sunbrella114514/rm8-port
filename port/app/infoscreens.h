// In-game information screens (infoscreens.cpp), opened from the bottom-bar buttons of gmaindlg and by taps on the board; docs/infoscreens.md.
// CGamePlayScene vf12 @0x4a2e10 (state 6 only): button 6 -> task 0x67 wincondition, 7 -> 0x61 stock board (stock.cpp), 8 -> 0x66 playerinfo,
// 9 -> 0x65 minimap, 10 -> 0x68 gsys (menu.cpp); gsys button 4 -> task 0x69 setting (NPC_SendMiniGameSignal @0x4aa890).
#pragma once
#include "turn.h"

// [A] the board scene (the minimap renders it from above, the board picks use its road nodes); rm8_app.cpp, once per run
void rm8InfoSetScene(Rm8Scene *s);
bool rm8InfoLoadData(const std::string &scenesDir);	// scenes/introduce.txt (scripts/export_introduce.py): names / introductions of the map articles; once at start
void rm8InfoReset();			// rm8HudInit (a new board / loaded game): hides every screen and the info box

// HUD buttons 6 / 8 / 9 (hud.cpp onClick): [V] Game_DispatchCardAction(0x67 / 0x66 / 0x65) @0x4a4420: only for the current role in state 6; the role goes to
// state 9 (CPlayerTag_SetStatusCode9), the queues are cleared and message 100 queued; closing the screen sets state 9 -> 8 (Game_EndTurn @0x4a48d0).
// Returns false when it did not open.
enum { RM8INFO_WIN = 6, RM8INFO_PLAYER = 8, RM8INFO_MAP = 9 };
bool rm8InfoOpenFromHud(int button, int seat);
// gsys button 4 -> Game_DispatchCardAction(0x69, 1) (menu.cpp); the system menu is hidden by the caller
bool rm8InfoSettingOpen();
bool rm8InfoVisible();			// one of the screens is open
bool rm8InfoFullScreen();		// the player info (a whole-screen dialog: hud.cpp skips its number sprites, drawn after the UIs)
bool rm8InfoClose();			// right click / Esc / Android back: closes the open screen (the scene vf11 -> the task's vf11); false if none was open

// every frame (hud.cpp, before the UI renders): scroll bar / slider drags, the minimap hover, test hooks
void rm8InfoTick(rw::Camera *cam);
void rm8InfoBoxYield();		// hud.cpp rm8HudInfo took the shared ginfo box: the tap info no longer owns (hides) it
// before the main camera's beginUpdate (next to rm8EventPreRender): the minimap's top-down render into its 512 x 512 camera raster
void rm8InfoPreRender(rw::Camera *main);

// [V] CGamePlayScene vf9 -> Game_HandleCardClickInGame @0x4a2940 / Game_OnTileClickWithCard @0x4a4a10: a left press on the board in state 6 shows the info box (ginfo) of
// the thing under the cursor: a role's name, an NPC's name, a plot (street, level, owner, rent), a wonder site, an event tile (right click hides it, Game_HidePlayerInfoPanel
// @0x4a2ad0). (sx, sy) in pixels of the camera's frame buffer. True = something was shown. Also hides a shown box (the touch "right click": the next tap).
bool rm8InfoBoardTap(rw::Camera *cam, float sx, float sy);
