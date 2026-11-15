// Menu scene animations (docs/menus.md section 7): the rotating rings, the pulsing glow pictures and circles, the floating bubbles, the characters
// flying across the main / single / mult menus and the 3D characters of the role menu seats. [V] = verified in the decompile, [A] port adaptation.
#pragma once
#include "scene.h"
#include <string>
#include <utility>
#include <vector>

struct Rm8Ui;

void rm8MenuAnimInit(const std::string &scenesRoot);	// roles.txt / npcs.txt model tables (lazy)
void rm8MenuAnimReset();					// the menu scene is (re)created: rings and glow restart, flyers / bubbles go
void rm8MenuAnimEnter(Rm8Ui *u);				// a screen's UI was shown: its decorations (by the UI name; nothing for the other UIs)
void rm8MenuAnimLeave();					// the screen goes away [V] mainmenu vf18 @0x508a20 / Cls_5e8000_vf18 @0x50da60: Particle_ResetAll @0x50c930
void rm8MenuAnimUpdate(float dt);				// per frame
void rm8MenuAnimRender(rw::Camera *cam);			// per frame, between beginUpdate / endUpdate, after rm8UiRender (the original draws these after the UI)
// chsRoleMenu: the 3D character of every seat (role id, outfit). True when the dolls are drawn (the caller then hides its portrait stand-ins).
bool rm8MenuAnimSeats(const std::vector<std::pair<int, int>> &seats);
