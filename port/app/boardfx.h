// Board feedback (boardfx.cpp): event icons that press down when a role walks onto / stops on / leaves them, clicks on map
// scenery (animation + sound), and the sounds / effects of the land rules. Details and addresses: docs/boardfx.md.
#pragma once
#include "turn.h"
#include <functional>

// ---- scene hooks (scene.cpp): per frame after the article animations, and when the scene is destroyed
void rm8BoardFxUpdate(Rm8Scene &s, float dt);
void rm8BoardFxClear();

// ---- event icons (roles.cpp). `node` is a road node index (Rm8Scene::rnodes), pos the role's world position.
// [V] Article_PlayApproachEffect @0x4EE3C0: pressed (action 0 / 2) when the role is < 40 units from the icon and `final` (the node is
// the walk's last one) also plays sample 0x7D; a second role on the same icon only sets its bit.
void rm8BoardFxIconApproach(int seat, int node, const float pos[3], bool final);
// [V] Article_PlayDepartEffect @0x4EE4F0: the seat's bit is cleared when the role is > 10 units away; the last one out plays action 1.
void rm8BoardFxIconDepart(int seat, int node, const float pos[3]);

// ---- clicks on the scenery. [V] CArticleMgr vf28 PickByRay @0x43E0E0 + vf29 RandomizeVariant @0x43E9C0 (docs/boardfx.md section 3).
// (sx, sy) in pixels of the camera's frame buffer. Returns true when an article reacted (a random action 1.. started, with its sound
// events). With characters on the board the current role must be a human waiting for the dice (state 6), like the original.
bool rm8BoardFxTap(rw::Camera *cam, float sx, float sy);

// ---- land rules (game.cpp)
void rm8BoardFxLandTaken();	// [V] Role_TakeLandOwnership @0x487250: sample 1
void rm8BoardFxRentPaid();	// [V] Role_TransferRentToOwner @0x487f20: sample 0x7B

// ---- build / upgrade animation. [V] Role_BuildOnLand @0x487380 -> NPC_CreateAndDisplay @0x4F4B50 (task vtable 0x5E6618): the role goes to state 9,
// effect art 6001 + sample 2; after 1.5 s the old model (level 1..4) plays action 1 (residential) / 3; then Land_OnEnter writes the level (`apply`:
// the caller changes its data and swaps the model, returning the new model and its art id), the new model plays action 0 / 2 + sample 0x7A, then the
// idle action; the role returns to state 8 when the effect (3.8 s) has also ended. Returns false (nothing started) if the effect art is missing.
struct Rm8BuildModel { rw::Clump *clump = nullptr; int art = 0; };
bool rm8BoardFxBuildStart(int seat, const float pos[3], bool residential, int oldLevel, Rm8BuildModel oldModel, std::function<Rm8BuildModel()> apply);
void rm8BoardFxCancel();	// new game: drop running tasks and cutscenes (no more callbacks)

// ---- bankruptcy cutscene: message 0x7F, [V] CPlayerTag__OnMovieDrama @0x492F30 (ghost art 6006, music 104, 14 s * wait scale)
void rm8BoardFxBankruptcy(int seat);
bool rm8BoardFxBuildSkip();		// [V] Cls_5e6618__vf13_Cleanup @0x4F4EC0 (scene vf13): cut the build task short (skip.cpp); false = none running
bool rm8BoardFxGhostSkip(int seat);	// [V] Role_StopMoveAndIdle @0x484790, state 0xE: end the bankruptcy cutscene of this seat now (roles.cpp)

// the main camera's view direction / yaw for the effect placement (roles.cpp passes the camera it already gets)
void rm8BoardFxSetCamera(rw::Camera *cam);
float rm8BoardFxCamYaw();	// the camera yaw the effects use (camera yaw - 180 turns them toward the screen)

// ---- property effect tasks (docs/effects.md section 3). Each starts a scene task: the role is in state 9 until it has finished (back to 8), the data of the plots
// changes at the original's moment. false = nothing to animate (the caller then applies the change at once, like the original's fallback).
bool rm8BoardFxDemolish(int seat, int terra);	// [V] message 0xD, NPC_InitDisplay @0x4F4F20: art 6002 + sample 3, the building's action 1 / 3, level - 1 when the effect ends
// blinking plots (pad alpha 0x60 / building grey, every 0.4 s, 1.6 s): the wizard 0x4F, owner changes 0x29 / 0x2A / 0xE / 0x44. endSample played with onEnd (1 / 4 / 0)
bool rm8BoardFxBlink(int seat, const std::vector<int> &plots, int endSample, std::function<void()> onEnd);
bool rm8BoardFxAngelBuild(int seat, int street);	// [V] message 0x10, Building_AnimateMultiple @0x4F3B20: art 6121 + sample 0x1E, then every plot of the street gets +1 level
bool rm8BoardFxCrash(int seat, const std::vector<int> &plots);	// [V] message 0x35, Cls_5e6a28 @0x4F4380: per plot art 6028 + sample 0x67, the building goes after 1.5 s
bool rm8BoardFxMonster(int seat, int terra);	// [V] message 0x31, NPC_TreasureChest_Init @0x4F51C0: art 6004 + sample 6, the building goes after 1.5 s
