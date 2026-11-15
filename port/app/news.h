// The news system (news.cpp), after docs/news.md: the weekly news (event task 0x11 = Cls_5e20a8 @0x5E20A8, constructor 0x493820, loader vf16 @0x4B4170,
// Update vf1 @0x4B4690), the festival news (message 0x3C with 0x2E..0x3A), the police / thief / lucky card / chest news of the board characters
// (News_OnUpdate @0x4B4DA0) and every news effect (News_EventSwitch @0x4B4920, News_EventDispatcher @0x4B7F80). Seats as in turn.h.
#pragma once
#include "turn.h"

namespace ev { struct Task; Task *makeNewsTask(int seat); }	// [V] event 0x11 (events.cpp createTask): the weekly news with its intro (animal / broadcast mode) and the random pick

// ---- data / lifecycle
// scenes/news.txt (scripts/export_news.py): the news pictures (art 900 + id), the anchor (art 1052) and the festival dates of Config\festival.bin
bool rm8NewsLoadData(const std::string &scenesDir);
void rm8NewsClear();	// new map / new game (rm8GameClear): drops the running presentation

// ---- turn flow hooks
// [V] Game_OnTurnStart @0x4a3b50 (Game_CheckFestival @0x4a5fb0 + Game_TriggerEventByDate @0x4a6780), called by rm8GameOnNewRound after the day was
// advanced (rm8GameDate / rm8GameDay are the new day). `otherEvent` = the monthly balance / the lottery draw started on this day (the original then skips the
// date news; a festival is checked anyway). A festival queues the message 0x3C(id + 0x2D, seat + 1) on the first role that is not bankrupt; otherwise on a
// Saturday when (days / 7) is odd the weekly news task (event 0x11) starts for that role. Returns true if the weekly news task started.
bool rm8NewsNewDay(bool otherEvent);
// the queued role messages of the news effects (called by rm8GameMessage, before the cards): 0x31 monster, 0x40 auction, 0x49 April fool, 0x4A, 0x4F, 0x5B, 0x5C, 0x5F
bool rm8NewsMessage(int seat, int cmd, int a, int b);
// [V] News_OnUpdate @0x4b4da0 (message 0x3C of the role `seat`, subject = the role the news is about): id 0..8 police (prison 3 days for `subject`), 9 thief,
// 10 lucky find, 11 chest; 0x2E..0x3A a festival; subject < 0 = News_EventSwitch(id) (ids 1..0x2D; 0x1E is the story mode's monster). The effects are queued
// at once, the news picture shows for 3.8 s (the role is in state 9 meanwhile). false = nothing started (invalid id / the conditions of the news failed).
bool rm8NewsStart(int seat, int id, int subject);
bool rm8NewsSkip();	// [V] RoleAnimation_SkipToEnd @0x4B1320 for the UFO task (skip.cpp): the role is carried off and the vanish follows at once; false = no UFO running
void rm8NewsUpdate(float dt);	// per frame (rm8RolesUpdate): the role-bound news task and the test hooks

// ---- 3D part of the news scene ([V] News_InitDialog @0x4b41b0 / News_CreateTextEffect @0x4b4f60 / Cls_5e20a8__vf2_Render @0x4b4770): the picture and the anchor
// in front of the camera, inside the main camera's beginUpdate, before the UI (rm8RolesRenderLate)
void rm8NewsRender(rw::Camera *cam);
bool rm8NewsActive();

// [A] the public land auction (event 0x50, not in the port yet): News_EventAuctionPublic only runs when an auction is available through this hook
// (seat = the role whose turn it is, terra = plot index); message 0x40 calls it
extern bool (*rm8NewsAuctionHook)(int seat, int terra);
