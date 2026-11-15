// Extra front-end screens, see menu_extra.h. [V] = verified in the decompile (address), [H] hypothesis, [A] port adaptation.
#include "menu_extra.h"
#include "menu.h"
#include "collect.h"
#include "cards.h"
#include "audio.h"
#include "video.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <filesystem>

namespace {

std::string g_game, g_videoDir;
bool g_binkplay = true;
const Rm8MenuEnv *g_env = nullptr;

// ---- movies --------------------------------------------------------------------------------------------------------------------------
// [V] CApp_SwitchToStartMovie @0x4052c0: nothing happens when init.txt binkplay (InitTxt_GetParam(10), default 1 @0x401080) is 0; the scene opens
//     "bink\<table[param]>" (Movie_LoadStartAnimation @0x516750; table 0x5e92e0: 0 start.bik, 1 Staff.bik) and a failure falls back to the menus
//     (0x507b70: state 1; Movie_Finish @0x515d40: state 8).
// [V] after the last frame Movie_FinishBinkPlayback @0x516890 -> CApp_SwitchToGameScene(index != 1 ? 1 : 8): the start movie leads to the main menu,
//     the staff credits to the single / mult menu.
// [V] input: CStartMovieScene's vf11 @0x407a20 = Movie_FinishBinkPlayback is the "skip": CApp_HandleInputEvent @0x405cf0 calls it for a right
//     mouse press (CApp_OnMouseButton 0x120) and for the key code 0x80 (DirectInput scancode 1 = Escape through the table @0x5df5f0); the left button
//     and the other keys do nothing. [A] Esc / back, a click or a tap skip (a phone has no right button).
// [V] who plays the staff credits: Movie_Finish @0x515d40 after an ending movie played with flag +0x2c (CApp_PlayMovieRange(role - 1, 1) from game
//     state 5, the story ending, @0x4a17b0); the finalMenu (state 7, ending movies, flag 0) never shows them.
int g_movie = 0;	// RM8X_MOVIE_START / STAFF while a movie screen runs
int g_movieBtn = 0;
const char *MOVIE_FILE[2] = {"start.mpg", "staff.mpg"};

std::string moviePath(int which){ return g_videoDir + "/" + MOVIE_FILE[which == RM8X_MOVIE_STAFF ? 1 : 0]; }

void movieFinish()
{
	int which = g_movie;
	g_movie = 0;
	rm8Log("movie: %s ended at %.2f s (%s)", which == RM8X_MOVIE_STAFF ? "staff" : "start", rm8VideoTime(), rm8VideoPlaying() ? "skipped" : "finished");
	rm8VideoClose();
	if(which == RM8X_MOVIE_STAFF){ rm8MusicPlay(101, true); g_env->leave(1); }	// [V] 0x516890: state 8 (the menu music is the scene's own start)
	else g_env->leave(0);								// [V] state 1
}

void enterMovie(int which)
{
	rm8MusicStop();	// [A] the game's own sound is only Bink's
	if(!rm8MenuExtraHasMovie(which) || !rm8VideoOpen(moviePath(which))){
		rm8Log("movie: %s not played, going on to the menu", which == RM8X_MOVIE_STAFF ? "staff" : "start");
		if(which == RM8X_MOVIE_STAFF){ rm8MusicPlay(101, true); g_env->leave(1); } else g_env->leave(0);	// [V] 0x507b70 / 0x515d40 fallbacks
		return;
	}
	g_movie = which;
	Rm8Ui *u = rm8UiCreateCanvas("movie", RM8UI_HUD);
	if(!g_movieBtn) g_movieBtn = rm8UiAddWidget(u, "button", 0, 0, 0, 1024, 768, -1, 0, 16, 9);	// covers the screen: any click / tap skips
	g_env->show(u, [](int){ movieFinish(); }, [](){ movieFinish(); }, [](float dt){
		rm8VideoUpdate(dt);
		if(!rm8VideoPlaying()) movieFinish();
	});
}

// ---- picmenu (state 10) ---------------------------------------------------------------------------------------------------------------
// [V] Cls_5e7f60__vf16_ShowPicMenu @0x50ad50: label 30 = string 171, slots cleared (PicMenu_FillItemSlots(0) @0x50af60: widgets 0x15..0x1d <- picture -1),
//     buttons 1..10 enabled, 11..16 enabled when Event_GetTerrainData(role) != 0 (the role is unlocked)
// [V] Cls_5e7f60__vf12 @0x50aed0: button 1..16 = PicMenu_FillItemSlots(role): widget 0x14 + k (k = 1..9) <- uvlist (role - 1) * 9 + 0x12d + k when
//     Event_GetItemData(role, k) != 0, else -1; 0x12 -> vf11 @0x50af10: Ui_SetActiveMenuObject(8) (the single / mult menu)
int g_picRole = 0;
void picFill(Rm8Ui *u, int role)
{
	g_picRole = role;
	for(int k = 1; k <= 9; k++) rm8UiSetPic(u, 0x14 + k, role >= 1 && role <= 16 && rm8PuzzleHas(role, k) ? rm8PuzzlePic(role, k) : -1);
}
void enterPic()
{
	Rm8Ui *u = g_env->load("picmenu");
	if(!u){ g_env->leave(1); return; }
	g_env->show(u, [u](int id){
		if(id >= 1 && id <= 16) picFill(u, id);
		else if(id == 18) g_env->leave(1);
	}, [](){ g_env->leave(1); }, nullptr);
	rm8UiSetText(u, 30, rm8UiWrapText(rm8Str(171), 246, 16));
	picFill(u, 0);
	for(int r = 1; r <= 16; r++) rm8UiSetEnabled(u, r, r < 11 || rm8ProfileRoleUnlocked(r));	// [V] Cls_5e7f60 vf16 @0x50ad50: roles 1..10 always, 11..16 only with Event_GetTerrainData != 0 (the port used to enable a role that has a piece too)
}

// ---- boxmenu (state 0xB) --------------------------------------------------------------------------------------------------------------
// [V] CUi_boxmenu__vf10_InitUI @0x501a70: widget 21 <- string 7201 (現金), 24 <- string 7202 (點券); buttons 1..3, 23, 26 and 4..19 are active
// [V] CUi_boxmenu_UpdateDisplay @0x502050 / UpdateLand @0x5021b0: labels 22 / 27 = cash stored / carried, 25 / 28 = points stored / carried (%d)
// [V] CUi_boxmenu_UpdateCards @0x502310: widget 4 + i = stored card i (picture card\<name>, enabled when the slot holds a card), 12 + i = carried card i
// [V] Cls_5e7f10__vf12_OnEvent @0x501f50: 1 close (vf11 @0x502000: Ui_SetActiveMenuObject(3) = the map menu), 2 all carried cards -> stored, 3 all stored -> carried,
//     23 cash (carried = stored, stored = 0), 26 points, 4..11 one stored card -> carried, 12..19 one carried card -> stored
// [V] only story mode: chsMapMenu button 0x13 (hidden by Game_InitOptions @0x5031c0 unless setup+0 != 0) opens it
void boxRefresh(Rm8Ui *u)
{
	Rm8Box &b = rm8Box();
	char t[32];
	snprintf(t, sizeof t, "%d", b.cash[0]); rm8UiSetText(u, 22, t);
	snprintf(t, sizeof t, "%d", b.cash[1]); rm8UiSetText(u, 27, t);
	snprintf(t, sizeof t, "%d", b.points[0]); rm8UiSetText(u, 25, t);
	snprintf(t, sizeof t, "%d", b.points[1]); rm8UiSetText(u, 28, t);
	for(int side = 0; side < 2; side++) for(int i = 0; i < 8; i++){
		int id = side ? 12 + i : 4 + i, card = b.cards[side][i];
		bool has = card > 0 && rm8CardName(card);
		if(has){ char key[64]; snprintf(key, sizeof key, "prt\\card\\D%02d.tga", card); rm8UiSetPicFile(u, id, key); }
		rm8UiSetVisible(u, id, has); rm8UiSetEnabled(u, id, has);
	}
}
void enterBox()
{
	Rm8Ui *u = g_env->load("boxmenu");
	if(!u){ g_env->leave(2); return; }
	g_env->show(u, [u](int id){
		if(id == 1){ g_env->leave(2); return; }
		else if(id == 2) rm8BoxMoveAll(1);
		else if(id == 3) rm8BoxMoveAll(0);
		else if(id == 23) rm8BoxMoveCash();
		else if(id == 26) rm8BoxMovePoints();
		else if(id >= 4 && id <= 11) rm8BoxMoveCard(0, id - 4);
		else if(id >= 12 && id <= 19) rm8BoxMoveCard(1, id - 12);
		boxRefresh(u);
	}, [](){ g_env->leave(2); }, [u](float){
		// [A] hovering a card shows its name and text (the original's hover callback 5 / 6 of widget 100, @0x501a70)
		std::string tip;
		for(int side = 0; side < 2 && tip.empty(); side++) for(int i = 0; i < 8; i++){
			int card = rm8Box().cards[side][i];
			if(card > 0 && rm8CardName(card) && rm8UiHot(u, side ? 12 + i : 4 + i)){ tip = std::string(rm8CardName(card)) + "\n" + rm8CardDesc(card); break; }
		}
		rm8UiSetTooltip(tip);
	});
	rm8UiSetText(u, 21, rm8Str(7201)); rm8UiSetText(u, 24, rm8Str(7202));
	boxRefresh(u);
}

}	// namespace

void rm8MenuExtraInit(const std::string &gameDir, const std::string &scenesRoot)
{
	g_game = gameDir; g_videoDir = scenesRoot + "/video";
	// [V] InitConfig_LoadInitTxt @0x401080: binkplay default 1, init.txt [param] binkplay overrides
	std::ifstream f(gameDir + "/Config/init.txt"); std::string line;
	while(std::getline(f, line)){
		size_t e = line.find('=');
		if(e != std::string::npos && line.substr(0, e) == "binkplay") g_binkplay = atoi(line.c_str() + e + 1) != 0;
	}
	if(getenv("RM8_NOVIDEO")) g_binkplay = false;	// [A] test switch
	rm8Log("menu: movies %s (%s)", g_binkplay ? "on" : "off", g_videoDir.c_str());
}

bool rm8MenuExtraHasMovie(int which)
{
	std::error_code ec;
	return g_binkplay && std::filesystem::exists(moviePath(which), ec);
}

void rm8MenuExtraEnter(const Rm8MenuEnv &env, int which)
{
	g_env = &env;
	switch(which){
	case RM8X_PICMENU: enterPic(); break;
	case RM8X_BOXMENU: enterBox(); break;
	case RM8X_MOVIE_START: case RM8X_MOVIE_STAFF: enterMovie(which); break;
	}
}

void rm8MenuExtraRender(rw::Camera *cam){ if(g_movie) rm8VideoRender(cam); }

bool rm8MenuExtraAuto(const Rm8MenuEnv &env, const std::string &t)
{
	g_env = &env;
	if(t == "staff"){ rm8MenuExtraEnter(env, RM8X_MOVIE_STAFF); return true; }
	if(t == "opening"){ rm8MenuExtraEnter(env, RM8X_MOVIE_START); return true; }
	if(t.rfind("piece:", 0) == 0){	// piece:<role>:<n>: the profile part of a puzzle pickup (rm8PuzzleGrant)
		int r = 0, k = 0; sscanf(t.c_str(), "piece:%d:%d", &r, &k);
		rm8Log("menu auto: piece %d of role %d: %s", k, r, rm8PuzzleGrant(r, k) ? "new" : "refused / owned");
		return true;
	}
	if(t == "boxfill"){	// demo content: rewards of a treasure chest (OnTreasurechest @0x48f9d0)
		int cards[4] = {1, 2, 5, 12};
		rm8BoxAddReward(30000, 120, cards, 4);
		rm8BoxMoveCard(0, 3);	// one card already on the carried side
		return true;
	}
	return false;
}
