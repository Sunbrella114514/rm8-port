// save / load hooks of dice.cpp (included at the end of dice.cpp): the dice RNG and the roll accumulators ([V] globals 0x63fbb0 / 0x63fbb4, never reset)
#include "save.h"
static void saveDiceExtWrite(Rm8SaveW &w){ w.u32(g_seed); w.f32(g_rollAcc); w.f32(g_holdAcc); }
static void saveDiceExtRead(Rm8SaveR &r){ g_seed = r.u32(); float a = r.f32(), b = r.f32(); if(!r.bad){ g_rollAcc = a; g_holdAcc = b; } }
RM8_SAVE_EXT(DICE, 30, saveDiceExtWrite, saveDiceExtRead);
