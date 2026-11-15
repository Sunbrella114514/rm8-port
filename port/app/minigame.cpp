// Minigame module: lifecycle of the running game, input routing, the [A] touch overlay, the host's reward rules and the computer
// result generator. Games are in mg_NN.cpp.
#include "mg_core.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <map>
#include <algorithm>

using namespace rw;

namespace {
MgGame *g_game = nullptr;
bool g_done = false, g_inited = false, g_paused = false;
std::vector<Rm8MiniPlayer> g_players;
float g_mx = 0, g_my = 0;	// cursor in 1024x768 space
bool g_touch =
#ifdef __ANDROID__
	true;
#else
	false;
#endif
bool g_forceTouch = false;
std::map<int, int> g_fingerBtn;	// pointer id -> key held through an overlay button
Rm8Ui *g_intro = nullptr;
bool g_cursorHidden = false;

// ---- [A] scripted input for tests: RM8_MINI_SCRIPT="t:click:x,y;t:down:a;t:up:a;t:key:space;t:wait:..." (t = seconds since the start)
struct ScriptEv { double t; std::string kind, arg; };
std::vector<ScriptEv> g_script; size_t g_scriptPos = 0; double g_time = 0;
int keyByName(const std::string &n)
{
	if(n == "space") return ' '; if(n == "enter") return RM8MK_ENTER; if(n == "esc") return RM8MK_ESC;
	if(n == "left") return RM8MK_LEFT; if(n == "right") return RM8MK_RIGHT; if(n == "up") return RM8MK_UP; if(n == "down") return RM8MK_DOWN;
	if(n.size() > 2 && n.substr(0, 2) == "0x") return (int)strtol(n.c_str(), nullptr, 16);
	return n.empty() ? 0 : (unsigned char)n[0];
}
void loadScript()
{
	g_script.clear(); g_scriptPos = 0;
	const char *s = getenv("RM8_MINI_SCRIPT");
	if(!s) return;
	std::string all = s, item;
	size_t p = 0;
	while(p <= all.size()){
		size_t e = all.find(';', p); if(e == std::string::npos) e = all.size();
		item = all.substr(p, e - p); p = e + 1;
		size_t a = item.find(':'); if(a == std::string::npos) continue;
		size_t b = item.find(':', a + 1);
		ScriptEv ev; ev.t = atof(item.substr(0, a).c_str());
		ev.kind = item.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
		ev.arg = b == std::string::npos ? "" : item.substr(b + 1);
		g_script.push_back(ev);
	}
	std::stable_sort(g_script.begin(), g_script.end(), [](const ScriptEv &x, const ScriptEv &y){ return x.t < y.t; });
}
void toScreen(float vx, float vy, float &sx, float &sy){ float ox, oy, sc; mg::gfxMap(ox, oy, sc); sx = ox + vx * sc; sy = oy + vy * sc; }
void runScript()
{
	while(g_scriptPos < g_script.size() && g_script[g_scriptPos].t <= g_time){
		const ScriptEv &ev = g_script[g_scriptPos++];
		if(ev.kind == "key"){ int k = keyByName(ev.arg); rm8MiniKey(k, true); rm8MiniKey(k, false); }
		else if(ev.kind == "down") rm8MiniKey(keyByName(ev.arg), true);
		else if(ev.kind == "up") rm8MiniKey(keyByName(ev.arg), false);
		else if(ev.kind == "click" || ev.kind == "pdown" || ev.kind == "pup" || ev.kind == "pmove"){	// virtual coordinates
			float vx = 0, vy = 0; sscanf(ev.arg.c_str(), "%f,%f", &vx, &vy);
			float sx, sy; toScreen(vx, vy, sx, sy);
			if(ev.kind == "click"){ rm8MiniPointer(0, sx, sy); rm8MiniPointer(1, sx, sy); }
			else rm8MiniPointer(ev.kind == "pdown" ? 0 : ev.kind == "pup" ? 1 : 2, sx, sy);
		}
	}
}
}	// namespace

namespace mg {
void mouse(float &x, float &y){ x = g_mx; y = g_my; }
void showCursor(bool show){ g_cursorHidden = !show; }
float now(){ return (float)g_time; }
static std::string g_scenesRoot;
const std::string &scenesRoot(){ return g_scenesRoot; }
void setScenesRoot(const std::string &s){ g_scenesRoot = s; }
Rm8Ui *introLoad(const char *name, int textId, std::function<void()> onOk)
{
	Rm8Ui *u = rm8UiLoad(name, RM8UI_DIALOG);
	if(!u) return nullptr;
	rm8UiSetText(u, 1, rm8Str(textId));
	rm8UiOnClick(u, [onOk](int id){ if(id == 2 && onOk) onOk(); });
	rm8UiShow(u, true);
	g_intro = u;
	return u;
}
void introHide(){ if(g_intro){ rm8UiShow(g_intro, false); g_intro = nullptr; } }
bool introVisible(){ return g_intro && rm8UiVisible(g_intro); }
}

bool rm8MiniInit(const std::string &scenesRoot)
{
	mg::setScenesRoot(scenesRoot);
	g_inited = mg::loadSprites(scenesRoot + "/minigame/sprites.txt");
	mg::loadModels(scenesRoot + "/minigame/sprites.txt");
	if(!g_inited) rm8Log("minigame: %s/minigame/sprites.txt missing (scripts/export_minigame.py)", scenesRoot.c_str());
	g_forceTouch = getenv("RM8_TOUCH") && atoi(getenv("RM8_TOUCH"));
	if(g_forceTouch) g_touch = true;
	return g_inited;
}

bool rm8MiniStart(int game, const std::vector<Rm8MiniPlayer> &players, unsigned seed)
{
	if(!g_inited || g_game || game < 18 || game > 23 || players.empty()) return false;
	if(!seed){ const char *e = getenv("RM8_MINI_SEED"); seed = e ? (unsigned)atoi(e) : (unsigned)time(nullptr); }
	mg::srand15(seed);
	rm8Log("minigame %d: start, %d player(s), seed %u", game, (int)players.size(), seed);
	switch(game){
	case 18: g_game = mgNew18(); break; case 19: g_game = mgNew19(); break; case 20: g_game = mgNew20(); break;
	case 21: g_game = mgNew21(); break; case 22: g_game = mgNew22(); break; case 23: g_game = mgNew23(); break;
	}
	if(!g_game){ rm8Log("minigame %d: not implemented", game); return false; }
	g_game->eventId = game;
	g_players = players; g_done = false; g_time = 0; g_paused = false; g_fingerBtn.clear(); g_intro = nullptr;
	g_cursorHidden = false;
	loadScript();
	if(!g_game->init(players)){ rm8Log("minigame %d: init failed", game); delete g_game; g_game = nullptr; mg::freeAllSprites(); return false; }
	return true;
}

bool rm8MiniActive(){ return g_game != nullptr; }
bool rm8MiniCursorHidden(){ return g_game && g_cursorHidden; }
bool rm8MiniDone(){ return g_game && g_done; }
void rm8MiniPause(bool p){ g_paused = p; }

void rm8MiniUpdate(float dt)
{
	if(!g_game || g_done || g_paused) return;
	dt = std::min(dt, 0.1f);	// [V] the game clamps dt to 0.1 s
	g_time += dt;
	runScript();
	if(!g_game->update(dt)){ g_done = true; rm8Log("minigame %d: finished after %.1f s", g_game->eventId, g_time); }
}

static bool showOverlay(){ return g_forceTouch || g_touch; }

void rm8MiniRender(Camera *cam)
{
	if(!g_game) return;
	int sw = cam->frameBuffer ? cam->frameBuffer->width : 1280, sh = cam->frameBuffer ? cam->frameBuffer->height : 720;
	bool d3 = mg::models3d();
	if(d3){ mg::gfxSetScreen(sw, sh); cam->endUpdate(); mg::camBegin(cam, sw, sh); cam->beginUpdate(); }	// [A] the DLLs see the game camera at the identity frame
	mg::gfxBegin(cam, sw, sh);
	{	// black background (also the letterbox bars)
		float ox, oy, sc; mg::gfxMap(ox, oy, sc);
		mg::drawRect(-ox / sc - 1, -oy / sc - 1, sw / sc + 2, sh / sc + 2, 0xff000000);
	}
	g_game->draw();
	if(showOverlay() && !mg::introVisible() && !g_done){
		std::vector<MgTouchBtn> btns; g_game->touchButtons(btns);
		for(auto &b : btns){
			bool held = false;
			for(auto &kv : g_fingerBtn) if(kv.second == b.key) held = true;
			uint32_t fill = held ? 0x90ffffff : 0x50ffffff, line = 0xa0000000;
			mg::drawRect(b.x, b.y, b.w, b.h, fill);
			mg::drawRect(b.x, b.y, b.w, 3, line); mg::drawRect(b.x, b.y + b.h - 3, b.w, 3, line);
			mg::drawRect(b.x, b.y, 3, b.h, line); mg::drawRect(b.x + b.w - 3, b.y, 3, b.h, line);
			float cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = std::min(b.w, b.h) * 0.3f;
			switch(b.shape){
			case MG_SHAPE_LEFT: mg::gfxTri(cx - r, cy, cx + r, cy - r, cx + r, cy + r, 0xe0000000); break;
			case MG_SHAPE_RIGHT: mg::gfxTri(cx + r, cy, cx - r, cy - r, cx - r, cy + r, 0xe0000000); break;
			case MG_SHAPE_UP: mg::gfxTri(cx, cy - r, cx - r, cy + r, cx + r, cy + r, 0xe0000000); break;
			case MG_SHAPE_DOWN: mg::gfxTri(cx, cy + r, cx - r, cy - r, cx + r, cy - r, 0xe0000000); break;
			default: { const int N = 14; for(int i = 0; i < N; i++){ float a0 = i * 6.2831853f / N, a1 = (i + 1) * 6.2831853f / N;
				mg::gfxTri(cx, cy, cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, 0xe0000000); } }
			}
		}
	}
	mg::gfxEnd();
	rm8UiRender(cam);
	if(d3){ cam->endUpdate(); mg::camEnd(cam); cam->beginUpdate(); }
}

void rm8MiniKey(int code, bool down)
{
	if(!g_game || g_done) return;
	if(down && !g_forceTouch) g_touch = false;	// a keyboard exists: hide the touch overlay
	if(down) g_game->keyDown(code); else g_game->keyUp(code);
}

void rm8MiniPointer(int kind, float x, float y, int id, bool touch)
{
	if(!g_game || g_done) return;
	if(touch) g_touch = true;
	float ox, oy, sc; mg::gfxMap(ox, oy, sc);
	float vx = (x - ox) / sc, vy = (y - oy) / sc;
	g_mx = vx; g_my = vy;
	if(mg::introVisible()){	// the lg_intro.ui panel (screen pixels)
		if(kind == 0) rm8UiPointerDown(x, y); else if(kind == 1) rm8UiPointerUp(x, y); else rm8UiPointerMove(x, y);
		return;
	}
	if(showOverlay()){	// [A] on-screen buttons
		std::vector<MgTouchBtn> btns; g_game->touchButtons(btns);
		auto hitBtn = [&](float px, float py) -> int { for(size_t i = 0; i < btns.size(); i++) if(px >= btns[i].x && py >= btns[i].y && px < btns[i].x + btns[i].w && py < btns[i].y + btns[i].h) return (int)i; return -1; };
		auto it = g_fingerBtn.find(id);
		int hb = hitBtn(vx, vy);
		if(kind == 0){ if(hb >= 0){ g_fingerBtn[id] = btns[hb].key; g_game->keyDown(btns[hb].key); return; } }
		else if(it != g_fingerBtn.end()){
			if(kind == 1){ g_game->keyUp(it->second); g_fingerBtn.erase(it); return; }
			if(hb < 0 || btns[hb].key != it->second){	// slid off / onto another button
				g_game->keyUp(it->second); g_fingerBtn.erase(it);
				if(hb >= 0){ g_fingerBtn[id] = btns[hb].key; g_game->keyDown(btns[hb].key); }
			}
			return;
		}
	}
	if(kind == 0) g_game->mouseButton(MG_MOUSE_LDOWN);	// [V] CMiniGameHost_vf9 @0x50EE30: left button down -> 0x40, up -> 0x400
	else if(kind == 1) g_game->mouseButton(MG_MOUSE_LUP);
}

bool rm8MiniFinish(Rm8MiniResult &out)
{
	if(!g_game) return false;
	out = Rm8MiniResult(); out.game = g_game->eventId;
	g_game->getResult(out);
	delete g_game; g_game = nullptr;
	mg::introHide(); mg::freeAllSprites();
	g_done = false;
	return true;
}

void rm8MiniAbort()
{
	if(!g_game) return;
	delete g_game; g_game = nullptr;
	mg::introHide(); mg::freeAllSprites();
	g_done = false;
}

// ---- host reward rules: MiniGameHost_Cleanup @0x50E300
std::vector<Rm8MiniReward> rm8MiniRewards(const Rm8MiniResult &out)
{
	std::vector<Rm8MiniReward> r;
	if(out.count < 1) return r;
	const int game = out.game;
	if(game == 21){
		// [V] 0x50E300 (event 0x15): per entry tickets +score / -(-score); score > 50 = big winner (3+ players, the pirate won),
		// flagA == 1 = the defeated pirate, flagB == 1 = the one who stabbed the trap with the first sword
		int bigSeat = -1, bigScore = 0, aSeat = -1, bSeat = -1;
		for(int i = 0; i < out.count; i++){
			const Rm8MiniPlayerResult &p = out.p[i];
			Rm8MiniReward e; e.seat = p.seat; e.ticketsDelta = p.score;
			if(p.score > 50){ bigSeat = p.seat; bigScore = p.score; }
			if(p.flagA == 1) aSeat = p.seat; else if(p.flagB == 1) bSeat = p.seat;	// [V] "else if": a pirate that lost cannot also be B
			r.push_back(e);
		}
		auto find = [&](int seat) -> Rm8MiniReward * { for(auto &e : r) if(e.seat == seat) return &e; return nullptr; };
		if(bigSeat < 0){
			if(aSeat >= 0){
				if(Rm8MiniReward *a = find(aSeat)){ a->textId = 5317; a->textArg = 3; a->hospitalDays = 3; a->reaction = 4; }	// [V] string 5317 (name, 3 days), (0x18, 3)
				if(bSeat >= 0) if(Rm8MiniReward *b = find(bSeat)){ b->textId = 5303; b->cards.push_back(0x2e); }	// [V] string 5303 + card 0x2e (Role_EnqueueMsg(4, 0x2e, 1))
			}
		}else if(Rm8MiniReward *w = find(bigSeat)){ w->textId = 5318; w->textArg = bigScore; w->reaction = 1; w->bestScore = bigScore; }	// [V] string 5318, FUN_004aa5d0(4, score)
		return r;
	}
	const Rm8MiniPlayerResult &p = out.p[0];
	Rm8MiniReward e; e.seat = p.seat;
	if(p.score > 0){	// [V] only for a positive score
		int sid = game + 0x14b0; if(sid > 0x14c5) sid++;	// 5314 .. 5320 (game 21 has its own)
		e.textId = sid; e.textArg = p.score; e.ticketsDelta = p.score; e.bestScore = p.score;
		switch(game){
		case 18: if(p.score < 180){ if(p.score >= 100) e.cards.push_back(0x14); } else e.cards.push_back(0x12); break;
		case 19: if(p.score >= 500){ e.cards.push_back(0x12); e.cards.push_back(0x2c); e.cards.push_back(0x14); } e.cards.push_back(0x2f); break;
		case 20: if(p.score < 150){ if(p.score >= 100) e.cards.push_back(0x11); } else e.cards.push_back(0x12); break;
		case 22: if(p.score < 999){ if(p.score > 0x95) e.cards.push_back(0x2c); } else { e.cards.push_back(0x2c); e.cards.push_back(0x2f); e.cards.push_back(0x12); } break;
		case 23: if(p.score < 160){ if(p.score > 0x95) e.cards.push_back(0x2e); } else e.cards.push_back(0x30); break;
		}
		e.reaction = p.rating == 0 ? 4 : p.rating == 1 ? 1 : 2;
	}
	r.push_back(e);
	return r;
}

// MiniGame_ExecuteGame @0x4FCB10 (the part that rolls the result)
Rm8MiniReward rm8MiniComputer(int game, int diff, int (*rnd)())
{
	Rm8MiniReward e; e.seat = -1;
	int sc = 0, sid = 0;
	bool low = diff < 3;
	switch(game){
	case 18: sid = 0x14c2; sc = low ? rnd() % 60 + 20 : rnd() % 100 + 40; break;
	case 19: sid = 0x14c3; sc = low ? rnd() % 60 + 30 : rnd() % 100 + 40; break;
	case 20: sid = 0x14c4; sc = low ? rnd() % 50 + 10 : rnd() % 80 + 30; break;
	case 21:
		if(rnd() % 10 > 4){ e.textId = 5317; e.textArg = 3; e.hospitalDays = 3; e.reaction = 4; return e; }	// [V] no human: 50% hospital for 3 days
		sid = 0x14c6; sc = 0x5a; break;	// else string 5318, +90 tickets
	case 22: sid = 0x14c7; sc = low ? rnd() % 120 + 1 : rnd() % 180 + 20; break;
	case 23: sid = 0x14c8; sc = low ? (rnd() % 10 + 1) * 10 : (rnd() % 8 + 3) * 10; break;
	default: return e;
	}
	if(sc > 0){ e.textId = sid; e.textArg = sc; e.ticketsDelta = sc; }
	return e;
}

void rm8MiniDrawHeadless(){ if(g_game && !g_done) g_game->draw(); }
void rm8MiniSetScreen(int w, int h){ mg::gfxSetScreen(w, h); }
