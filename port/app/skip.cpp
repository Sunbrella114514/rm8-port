// Click / key skip of waits, after the original's scene vf13 (docs/skip.md). The logic of each layer sits next to its data (roles.cpp, events, boardfx.cpp,
// npc_act.cpp, present.cpp); this file only keeps the original's order and the input glue.
#include "skip.h"
#include "turn.h"
#include "events.h"
#include "cards.h"
#include "boardfx.h"
#include "npc.h"
#include "news.h"
#include "infoscreens.h"
#if __has_include(<SDL.h>)
#include <SDL.h>
#define RM8_SKIP_HAVE_SDL 1
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {

float g_time = 0;			// game seconds (the sum of the rm8SkipUpdate dts), for the trace
bool trace(){ static const bool t = getenv("RM8_SKIP_TRACE") != nullptr || getenv("RM8_SKIP_TEST") != nullptr; return t; }

// RM8_SKIP_TEST_SDL=1 (windowed runs): the test tap is a real touch press / release pushed into the SDL queue, so it takes rm8_app.cpp's whole tap path
// (UI first finger, card targets, skip, scenery); in --shot mode nothing polls the queue, so the hook calls rm8SkipRequest directly.
bool testTap()
{
	static const bool viaSdl = getenv("RM8_SKIP_TEST_SDL") != nullptr;
	if(!viaSdl) return rm8SkipRequest();
#ifndef RM8_SKIP_HAVE_SDL
	return rm8SkipRequest();	// the GLFW viewer build has no SDL
#else
	SDL_Event e; memset(&e, 0, sizeof e);
	e.type = SDL_FINGERDOWN; e.tfinger.fingerId = 77; e.tfinger.x = 0.5f; e.tfinger.y = 0.3f; SDL_PushEvent(&e);
	e.type = SDL_FINGERUP; SDL_PushEvent(&e);
	return true;
#endif
}

}	// namespace

// [N] netplay (net.cpp): a skip / the '.' key of the local player becomes a command (true = taken); the command runs rm8SkipRequestNow / rm8SkipToggleFastNow
namespace { bool (*g_skipNet)() = nullptr; bool (*g_fastNet)() = nullptr; }
void rm8SkipSetNetHooks(bool (*skip)(), bool (*fast)()){ g_skipNet = skip; g_fastNet = fast; }
bool rm8SkipRequest(){ if(g_skipNet && g_skipNet()) return false; return rm8SkipRequestNow(); }
void rm8SkipToggleFast(){ if(g_fastNet && g_fastNet()) return; rm8SkipToggleFastNow(); }

// [V] CGamePlayScene__vf13_StopMoving @0x4A3060: this+0x34 (the scene task) non-null -> only ITS vf13 (+0x34) answers; else
// Role_TryStopMoveUntilDone @0x4986B0 (first waiting role); else Collection_FindFirstByVf60 @0x49DEA0 over the NPCs.
bool rm8SkipRequestNow()
{
	if(rm8EventRunning()){	// the event task (cardShop, bank, question ... vf13 of each class, see docs/skip.md section 3)
		bool r = rm8EventSkip();
		if(trace() && r) rm8Log("skip: t=%.2f event task cut short", g_time);
		return r;
	}
	if(rm8BoardFxBuildSkip()){	// the build animation is a scene task too (vtable 0x5E6618, vf13 @0x4F4EC0)
		if(trace()) rm8Log("skip: t=%.2f build animation cut short", g_time);
		return true;
	}
	if(rm8NpcTransferSkip() || rm8NewsSkip()){	// the police car / ambulance / UFO tasks (vtable 0x5E3AF0 family, RoleAnimation_SkipToEnd @0x4B1320)
		if(trace()) rm8Log("skip: t=%.2f vehicle / UFO show cut short", g_time);
		return true;
	}
	float left = 0;
	int seat = rm8TurnSkipRequest(&left);
	if(seat >= 0){
		bool dlg = rm8EventSkipDialogs();
		if(trace()) rm8Log("skip: t=%.2f seat %d wait cut short (%.2f s left)%s", g_time, seat, left, dlg ? ", card box hidden" : "");
		return true;
	}
	if(rm8NpcSkip()){
		if(trace()) rm8Log("skip: t=%.2f god arrival show cut short", g_time);
		return true;
	}
	return false;
}

bool rm8SkipTap(rw::Camera *cam, float x, float y, bool onUi)
{
	if(onUi){ rm8SkipRequest(); return false; }
	if(rm8CardsOnTap(x, y)) return true;
	if(rm8SkipRequest()) return true;
	bool info = cam && rm8InfoBoardTap(cam, x, y);	// [V] the press shows the info box of the thing under the cursor (infoscreens.cpp); the release animates the scenery
	bool fx = cam && rm8BoardFxTap(cam, x, y);
	return info || fx;
}

void rm8SkipToggleFastNow()
{

	rm8GameSpeed += rm8GameSpeed < 50 ? 50 : -50;	// [V] InitConfig_SetSpeed(0x32) @0x406180: < 0x32 -> +0x32, else -0x32
	rm8Log("speed: %d (%s)", rm8GameSpeed, rm8GameSpeed >= 50 ? "fast" : "normal");
}

void rm8SkipUpdate(float dt)
{
	g_time += dt;
	static const char *env = getenv("RM8_SKIP_TEST");
	static const bool tr = getenv("RM8_SKIP_TRACE") != nullptr;
	if(tr){	// every role wait (state 5) and every event task: when it starts and when it ends
		static bool evPrev = false;
		if(rm8EventRunning() != evPrev){ evPrev = rm8EventRunning(); rm8Log("skip: t=%.2f event task %s", g_time, evPrev ? "starts" : "ends"); }
		static std::vector<int> prev;
		static const bool all = getenv("RM8_SKIP_TRACE") && getenv("RM8_SKIP_TRACE")[0] == '2';	// =2: every state change
		int n = rm8TurnRoleCount();
		if((int)prev.size() != n) prev.assign(n, -1);
		for(int s = 0; s < n; s++){
			int st = rm8TurnState(s);
			if(st != prev[s] && (st == 5 || prev[s] == 5 || all)) rm8Log("skip: t=%.2f seat %d state %d -> %d", g_time, s, prev[s], st);
			prev[s] = st;
		}
	}
	if(!env) return;
	struct Item { float at; bool fast; bool done; };
	static std::vector<Item> items; static bool init = false; static float autoPeriod = 0, autoNext = 0;
	if(!init){
		init = true;
		std::istringstream ss(env); std::string it;
		while(std::getline(ss, it, ',')){
			if(it.compare(0, 4, "auto") == 0){ autoPeriod = it.size() > 5 ? (float)atof(it.c_str() + 5) : 0.4f; autoNext = 0; }
			else if(it.compare(0, 5, "fast@") == 0) items.push_back({(float)atof(it.c_str() + 5), true, false});
			else items.push_back({(float)atof(it.c_str()), false, false});
		}
	}
	for(Item &i : items){
		if(i.done || g_time < i.at) continue;
		i.done = true;
		if(i.fast) rm8SkipToggleFast();
		else { bool r = testTap(); rm8Log("skip test: tap at t=%.2f -> %s", g_time, r ? "sent" : "nothing to skip"); }
	}
	if(autoPeriod > 0 && g_time >= autoNext){ autoNext = g_time + autoPeriod; testTap(); }
}
