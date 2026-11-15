// save / load hooks of event_build.cpp (included at the end of event_build.cpp): the research institute's state goes into the plot record of the original
// ([V] FUN_004f6b40: a building of type 2 (research institute) of level 1..5 adds Building +0x14, 2 ints, to its terra record; here: the card index being researched
// (-1 none) and the days left [H: the meaning of the two ints is inferred from the port's own fields]).
#include "save.h"

namespace {
bool instGet(int terra, int out[2]){ int c = -1, d = 0; if(!ev::instituteGet(terra, c, d)){ c = -1; d = 0; } out[0] = c; out[1] = d; return true; }
void instSet(int terra, const int in[2]){ if(in[0] >= 0 && in[1] > 0) ev::instituteSet(terra, in[0], in[1]); }
struct BuildSaveInit { BuildSaveInit(){ Rm8SaveHooks &h = rm8SaveHooks(); h.instituteGet = instGet; h.instituteSet = instSet; } } g_buildSaveInit;
}
