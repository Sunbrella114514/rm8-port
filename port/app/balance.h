// The monthly balance (月结算, event task 0x52), after docs/balance.md: Game_OnTurnStart @0x4a3b50 starts it on the 1st of a month (day > 5) through Game_TriggerNormalEvent
// @0x4a6620 -> Game_UseCard(0x52); the task is Cls_5e49c8 (vtable 0x5E49C8, loader vf16 @0x4af2e0, Update vf1 @0x4b0870, skip vf13 @0x4b0d90) showing balance.ui.
#pragma once
#include "turn.h"

namespace ev { struct Task; Task *makeBalanceTask(int seat); }	// event 0x52 (events.cpp createTask); nullptr when the data / UI is missing (the caller credits the interest silently)

// scenes/roles.txt + scenes/event_arts.txt (the character dolls, the anchor art 1050); once at start
bool rm8BalanceLoadData(const std::string &scenesDir);
// before the main camera's beginUpdate (rm8EventPreRender): renders the 3D part (the anchor and the role dolls) into a 1024 x 1024 raster shown over balance.ui
void rm8BalancePreRender(rw::Camera *main);
bool rm8BalanceActive();	// the balance screen covers the board (hud.cpp skips the number sprites)
// test hook RM8_BALANCE_EVERY=1: every new day is a 1st of the month (also before day 6)
bool rm8BalanceForced();
