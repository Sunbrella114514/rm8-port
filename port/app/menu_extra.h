// Extra front-end screens (menu_extra.cpp): the Bink opening / staff movie, the puzzle album (picmenu) and the storage box (boxmenu).
// They run inside the menu module (menu.cpp passes an Rm8MenuEnv for the shared screen plumbing). docs/media_collect.md.
#pragma once
#include "ui.h"
#include <functional>
#include <string>

struct Rm8MenuEnv {
	Rm8Ui *(*load)(const char *name);		// the .ui as a centred dialog
	// makes the UI the current screen: click handler, Esc / back, per-frame update (any may be empty)
	void (*show)(Rm8Ui *u, std::function<void(int)> click, std::function<void()> back, std::function<void(float)> update);
	void (*leave)(int to);				// 0 main menu (state 1), 1 single / mult menu (state 8), 2 map menu (state 3)
};

enum Rm8ExtraScreen { RM8X_PICMENU = 1, RM8X_BOXMENU, RM8X_MOVIE_START, RM8X_MOVIE_STAFF };

void rm8MenuExtraInit(const std::string &gameDir, const std::string &scenesRoot);	// init.txt binkplay, scenes/video
bool rm8MenuExtraHasMovie(int which);		// RM8X_MOVIE_START / STAFF: playable (binkplay on, file and decoder available)
void rm8MenuExtraEnter(const Rm8MenuEnv &env, int which);
void rm8MenuExtraRender(rw::Camera *cam);	// the movie picture; call before rm8UiRender
bool rm8MenuExtraAuto(const Rm8MenuEnv &env, const std::string &token);	// test tokens of RM8_MENU_AUTO: staff, opening, boxfill
