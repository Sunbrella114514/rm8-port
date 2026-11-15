// save / load hooks of present.cpp (included at the end of present.cpp): the expression timers of the roles. An expression pose (role animation >= 3) freezes the idle timer
// of the role, whose end draws from the RNG, so the pose's remaining seconds belong to the logical state (extension chunk "PRES").
#include "save.h"
static void savePresentExtWrite(Rm8SaveW &w){ w.i32((int)g_seat.size()); for(const SeatState &s : g_seat) w.f32(s.exprT); }
static void savePresentExtRead(Rm8SaveR &r){ int n = r.i32(); for(int i = 0; i < n && !r.bad; i++){ float t = r.f32(); if(i < 8) seatState(i).exprT = t; } }
RM8_SAVE_EXT(PRES, 22, savePresentExtWrite, savePresentExtRead);
