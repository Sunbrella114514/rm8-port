// The host of the story map menu (chsMapMenu, Cls_5e81e0): the 3D presenter art 1048 (NPC\N23_act01.dff) who announces the first stage, the opponents and
// the next stage choice, with the voice sample of every string. docs/story_mode.md 1.3. [V] = verified in the decompile (address), [A] port adaptation.
//
// [V] Cls_5e81e0__vf1_OnUpdate @0x503830 is the state machine below; Game_PlayMovie @0x504460 shows one line: the string text goes to widget 0x34 of chsMapMenu.ui
//     (the shipped file has no widget 0x34, so the original shows NO text: UI_DialogShowMessage @0x4e78b0 finds nothing; [A] the port shows the line in the
//     description label), widget 0x33 is shown, the previous sample is stopped and the new one (string id, the number after '@' of the line) played
//     (CAudioMgr_PlaySample(id, 0, 0, 0, 1000, 0, 1.0, 1)); the host plays action `anim` once (CUi_chsMapMenu_InitTexture(anim, 1, Ui_InitTexture): when it ends Ui_InitTexture
//     starts action 3, the idle loop). The wait timer does not run while the voice plays (this+0xC != 0 until the sample's callback @0x5043d0 clears it).
#pragma once
#include "scene.h"
#include "menu.h"
#include <string>
#include <vector>

// the scene whose world the menu is drawn in (lights, article list): rm8_app.cpp once after the empty menu scene exists; the host adds its article there
void rm8StoryHostSetScene(Rm8Scene *sc, const std::string &scenesRoot);
// initState: 2 = first stage of a run (layer 0): player name, "<str#3828>…<q#35>", map; 5 = later stage: "<str#3829>", opponents; 0xB = after a cleared stage:
// "<str#3830>…請在" the candidate maps "<str#3831>", or the finalist announcement when the run reached layer 3.
// cand: the candidate map ids of the new layer (0xB); level: Rm8Box::stage (the run level)
void rm8StoryHostBegin(int initState, const Rm8Setup &setup, const int cand[4], int level);
int rm8StoryHostState();		// 1 = idle (the player may act), 0x65 = done: start the game
bool rm8StoryHostBusy();		// state != 1 and not 0x65
void rm8StoryHostUpdate(float dt);
// [V] Cls_5e81e0__vf13_OnSkipMovie @0x5041f0: a click in states 3..8 jumps to 0x65, in 0xc..0xf to state 1; the voice stops and the wait ends
void rm8StoryHostSkip();
const std::string &rm8StoryHostLine();	// the text of the current line ("" none)
void rm8StoryHostRender(rw::Camera *cam);	// the 3D host, after the UI (identity camera, like menu_anim.cpp)
void rm8StoryHostEnd();			// leaving the screen (Cls_5e81e0__vf18_OnHide: the mesh is freed)
