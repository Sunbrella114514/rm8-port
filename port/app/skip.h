// "Click the background to speed up / skip" of the original (docs/skip.md): CApp_OnMouseButton @0x4059F0 calls the board scene's vf13
// (CGamePlayScene__vf13_StopMoving @0x4A3060) on every left press; the scene forwards it to the running event task, else to the first waiting
// role (Role_StopMoveAndIdle @0x484790), else to the gods' arrival show (NPC vf+0x60). Everything is "skip to the end", nothing is a gradual speed-up
// (the only speed-up is the '.' key, InitConfig_SetSpeed(0x32) @0x406180).
#pragma once
#include "scene.h"

// The scene vf13 chain, in the original's order: the scene task (event task / build animation) -> the first waiting role (state 5 / the bankruptcy
// movie wait) -> the gods' arrival show. True = something was cut short. Cheap when nothing runs; safe to call at any time.
bool rm8SkipRequest();

// A press / tap that the board scene gets (rm8_app.cpp). onUi: the UI took the press (the original calls vf13 for every left press, also one a UI
// widget took, BEFORE the widget's click action runs): only the skip runs, returns false. Otherwise the order is: card target selection (keeps
// priority) -> skip -> scenery click (rm8BoardFxTap, when cam != nullptr). True = the tap was used.
bool rm8SkipTap(rw::Camera *cam, float x, float y, bool onUi);

// '.' (DIK_PERIOD -> key 0x2E, CGamePlayScene vf5 @0x4A1E80): InitConfig_SetSpeed(0x32) @0x406180 flips the speed setting by +-50:
// wait scale 1 - min((speed - 1) / 10, 1) becomes 0 (every Anim_ApplyDifficultyScale wait is 0) and the walking dt factor (speed - 1) / 4 + 1 about 14.
void rm8SkipToggleFast();

// per frame (rm8RolesUpdate): test hook only. RM8_SKIP_TEST="<t>,<t>,..." taps (rm8SkipRequest) at those game seconds, "auto[:period]" taps every
// `period` s (default 0.4), "fast@<t>" toggles the fast mode; RM8_SKIP_TRACE=1 logs the skips and every role wait (state 5) with its game time.
void rm8SkipUpdate(float dt);
