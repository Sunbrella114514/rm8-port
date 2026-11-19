// Character presentation (present.cpp), after docs/presentation.md: expressions, speech (info box + bubbles, voices),
// role-to-role reactions and the generic effect ("eff" article) player.
//
// Everything the original does goes through the role message queue (docs/turn_flow.md 2.1), so the entry points are
// message helpers (the original's Role.Speak / expression callers) plus rm8PresentMessage, which game.cpp calls first
// for every message the turn flow does not handle. Seats are the turn-flow seats (turn.h).
#pragma once
#include "turn.h"

// ---- string.bin speech lines: "text@voice $anim" (StringTable_LoadFromIni @0x4015e0 splits '@' -> sample id, '$' -> animation)
struct Rm8SpeechLine { std::string text; int voice = -1; int anim = 0; };	// voice -1 = none, anim 0 = none
bool rm8SpeechParse(int strId, Rm8SpeechLine &out);

// ---- expressions: a role animation (logical id 3..14 = role.bin animations Act04.., each swaps the head texture by the
// role.bin face table) played looped (CPlayerTag vf5(id, 0, 0)), like the original it stays until the next pose change
// (walking, stopping, dice). seconds > 0 [A]: back to the standing pose after that long.
void rm8RoleExpression(int seat, int id, float seconds = 0);

// ---- speech
// INFO: the gtinfo / ginfo box at the bottom of the screen with the role's portrait npc\<pic>_NN (Role_ShowInfoDialog @0x488ae0,
//       what message 0x5A shows); only one at a time.
// BUBBLE: the movieWord bubble above the character's head (Movie.RoleSpeak @0x512b70, vf3 @0x511c10), one per seat.
enum Rm8SpeakStyle { RM8SPEAK_INFO = 0, RM8SPEAK_BUBBLE = 1 };
// show now. voice: Sound/snd0000.dat id (<= 0 none; a new voice stops the seat's previous one, Anim_PlaySound @0x47aff0).
// portrait: npc\<pic>_NN index 1..9 (INFO only, 0 = no picture). seconds 0 = until rm8RoleStopSpeak / rm8PresentHideSpeech.
void rm8RoleSpeak(int seat, const std::string &textUtf8, int voice = -1, Rm8SpeakStyle style = RM8SPEAK_INFO, float seconds = 0, int portrait = 0);
void rm8RoleSpeakStr(int seat, int strId, Rm8SpeakStyle style = RM8SPEAK_BUBBLE, float seconds = 0);	// absolute string.bin id (Movie.RoleSpeak)
void rm8RoleStopSpeak(int seat);		// hides the seat's bubble / info box and stops its voice (Movie.RoleStopSpeak)
void rm8PresentHideSpeech();			// message 0x1C: hide the info box and the two-role speech box

// ---- queue helpers (class = line offset from roleinfo strId: money gain 7..9, loss 0xA..0xC, rent 0xD..0xF, buy 1 / 0x11, build 1 / 2 / 0x10 / 0x12,
// bankrupt 0x1A, ... the card speakNo values are the same numbers)
void rm8RoleSay(int seat, int cls);			// Role.Speak(role, cls): append 0x5A(cls)
void rm8RoleSayAt(int seat, int pos, int cls);	// thunk_FUN_00484930(pos, cls): insert 0x5A(cls) at queue position pos
// the game's 70 % chance (FUN_005c5802() % 100 < 0x46) for the emotes of the money / buy functions
bool rm8PresentChance70();

// ---- hooks called by the rules / turn flow (each cites its original in present.cpp)
bool rm8PresentMessage(int seat, int cmd, int a, int b);	// 0x5A, 0x2C, 0x2E, 0x52, 0x53, 0x54..0x56 (+ observes 0x16 / 0x17); true = consumed
void rm8PresentMoney(int seat, int32_t amount, bool gain);	// [N] Role_AddMoney / Role_SubMoney(emote) @0x489b80 / 0x489ac0 (the 70 % emote)
void rm8PresentRent(int payer, int owner, int32_t rent);		// Role_TransferRentToOwner @0x487f20 (60 % / 40 % emotes + the 0x2E hand-off)
void rm8PresentOnStop(int seat);				// CPlayerTag::OnStopAtTerra @0x485c00: meeting dialogue with a role on the same node
void rm8PresentLandTaken(int seat, int prevOwner);	// Role_TakeLandOwnership(t, b != 0) @0x487250 (not wired: no sender in the port yet)
void rm8PresentRentExempt(int seat, int32_t rent);		// [N] Role_CheckRentExempt @0x486580: the 70 % emote 0xD / 0xE / 0xF when the owner's status exempts the rent
void rm8PresentBuy(int seat, int terra, int32_t price);	// Role_BuyLandConfirmed @0x487030 (after the purchase): emote 1 / 0x11 (sample 1 is boardfx.cpp's)
void rm8PresentBuild(int seat, int terra, int level);	// Role_BuildOnLand @0x487380 (after the new level): emote 1 / 2 / 0x10 / 0x12, firework at level 5
void rm8PresentSetStory(bool on);				// [V] the role-meeting dialogues (OnStopAtTerra) only exist in the story mode (&DAT_00632de8 +0); default off, env RM8_STORY=1
void rm8PresentUpdate(float dt);				// per frame (called from rm8RolesUpdate)
void rm8PresentRenderLate(rw::Camera *cam);		// per frame before the UI is drawn: places the bubbles (called from rm8RolesRenderLate)
void rm8PresentSetScene(Rm8Scene *s);			// the effect player's scene (rm8RolesSpawn); nullptr on rm8RolesClear
bool rm8PresentLoadDefs(const std::string &rolesTxt);	// extra line types of roles.txt: rstr, sp / spl, fxart / fxhook
bool rm8PresentBusy(int seat);				// a speech of this seat is showing (the turn flow waits through 0x65 / 0x2E itself)
void rm8PresentSkipSeat(int seat);		// [V] Role_StopMoveAndIdle @0x484790 (wait skip, docs/skip.md): stops the seat's voice and releases its effect article

// ---- effect player ([V] Anim_CreateSprite @0x47b090, Role_ShowEffectAnimAtSelf @0x48b2b0 / AtTerra @0x48b320)
// Plays an effect article (role.bin art id, normally 6000..6399: eff\, destiny\, mend\, gods\) at pos: a model with its
// animation `anim` (role.bin animation index; -1 = none) played once or looped, or - for particle arts (6005 ...) - the
// particle effect. Returns a handle (> 0), 0 on failure. yawDeg: rotation about Y. sound: Sound id played once (<= 0 none)
// and stopped with the effect. life > 0: removed after that many seconds (Anim_ReduceDuration @0x47b340); autoRemove: also
// removed when a "once" animation has finished (the original's callback). Without both the caller removes it with rm8FxStop.
int rm8FxPlay(int art, const float pos[3], float yawDeg = 0, int anim = 0, bool once = true, int sound = 0, float scale = 1.0f, float life = 0, bool autoRemove = false);
bool rm8FxFinished(int handle);	// a once animation has ended / life elapsed / the handle is gone (stopped, removed or invalid)
bool rm8FxActive(int handle);
void rm8FxStop(int handle);		// removes the article / emitter and stops its sound
void rm8FxMove(int handle, const float pos[3]);
void rm8FxStopAll();
void rm8FxSetOverlay(int handle);	// [V] drawn over everything after a z clear (the UFO mesh, Cls_5e3af0__vf2 @0x4B1420) instead of with the scenery
// Role_ShowEffectAnimAtSelf: effect at the role (position + (0,30,0) pulled 20 units toward the camera), removed when it ends (4 s / 10 s * wait scale)
int rm8FxPlayAtRole(int seat, int art, int sound);
// message 0x52 / 0x2C style: [A] terra index -> its pad position
int rm8FxPlayAtTerra(int terra, int art, int sound);
int rm8FxPlayAtPoint(const float p[3], int art, int sound);	// the same at a world position (Anim_CreateSprite @0x47b090 placement)

// ---- game-start introduction (docs/presentation.md section 9)
void rm8PresentIntroBegin(int seat);		// Role_ClearFlag1C8: the doll is hidden until message 0x7C
bool rm8PresentRoleHidden(int seat);			// the doll of this seat is not drawn (introduction)
bool rm8PresentSeatFxBusy(int seat);			// an effect / debut model of this role is still playing (Role_TickWaitTimer's +0x50 / +0x54 test)
int rm8PresentStrId(int seat);			// roleinfo +0x38c: the role's string.bin block (speech lines strId + class)
