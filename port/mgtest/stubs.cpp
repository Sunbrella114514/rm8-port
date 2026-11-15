// Board symbols that scene.cpp / boardfx.cpp reference but the minigame test host never uses.
#include "scene.h"
int rm8TurnRoleCount(){ return 0; }
int rm8TurnCurrent(){ return -1; }
bool rm8TurnHuman(int){ return false; }
int rm8TurnState(int){ return 0; }
void rm8TurnSetState(int, int){}
bool rm8RolesGetPos(int, float*){ return false; }
void rm8TurnFocus(const float*){}
int rm8GameSpeed = 4;
