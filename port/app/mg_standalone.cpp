// Standalone test entry for the minigames (rm8_app --minigame N): own window loop, --shot / --time support, scripted input.
// Environment hooks (documented in docs/minigames/port.md):
//   RM8_MINI_PLAYERS  e.g. "hcc" (h = human, c = computer; the first is the current player), default depends on the game
//   RM8_MINI_SEED     random seed;  RM8_MINI_SCRIPT  scripted input "t:key:space;t:down:a;t:up:a;t:click:x,y" (t in game seconds)
//   RM8_TOUCH=1       always show the [A] touch overlay
#include "mg_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>

#if __has_include(<SDL.h>)	// not in the GLFW viewer target
#include <SDL.h>

using namespace rw;

int rm8MiniKeyFromSdl(int sym)
{
	if(sym >= SDLK_a && sym <= SDLK_z) return sym;	// lower case ASCII
	if(sym >= SDLK_0 && sym <= SDLK_9) return sym;
	if(sym >= SDLK_KP_1 && sym <= SDLK_KP_9) return 0x99 + (sym - SDLK_KP_1);	// [V] ltlgame_20 keypad codes 0x99..0xa1 (OnNumpad @0x10002cf0: code - 0x98 = keypad digit)
	switch(sym){
	case SDLK_SPACE: return ' ';
	case SDLK_RETURN: case SDLK_KP_ENTER: return RM8MK_ENTER;
	case SDLK_ESCAPE: return RM8MK_ESC;
	case SDLK_UP: return RM8MK_UP; case SDLK_DOWN: return RM8MK_DOWN; case SDLK_LEFT: return RM8MK_LEFT; case SDLK_RIGHT: return RM8MK_RIGHT;
	}
	return 0;
}

static void setupCam(Camera *cam, int w, int h)
{
	if(cam->frameBuffer) cam->frameBuffer->destroy();
	if(cam->zBuffer) cam->zBuffer->destroy();
	cam->frameBuffer = Raster::create(w, h, 0, Raster::CAMERA);
	cam->zBuffer = Raster::create(w, h, 0, Raster::ZBUFFER);
}

static void reportResult()
{
	Rm8MiniResult res;
	if(!rm8MiniFinish(res)) return;
	rm8Log("minigame %d result: %d player(s)", res.game, res.count);
	for(int i = 0; i < res.count; i++)
		rm8Log("  player %d: seat %d score %d flagA %d rating %d flagB %d", i, res.p[i].seat, res.p[i].score, res.p[i].flagA, res.p[i].rating, res.p[i].flagB);
	for(auto &r : rm8MiniRewards(res)){
		std::string cards; for(int c : r.cards){ char b[16]; snprintf(b, sizeof b, " %d(0x%x)", c + 1, c); cards += b; }
		rm8Log("  reward seat %d: tickets %+d, cards (Id)%s, hospital %d days, text %d(%d), reaction %d, best %d", r.seat, r.ticketsDelta,
		       cards.empty() ? " none" : cards.c_str(), r.hospitalDays, r.textId, r.textArg, r.reaction, r.bestScore);
	}
}

int rm8MiniStandalone(SDL_Window *win, Camera *cam, int game, const std::string &shot, float shotTime, int W, int H, const std::string &scenesRoot)
{
	if(!rm8MiniInit(scenesRoot)) return 1;
	std::vector<Rm8MiniPlayer> pl;
	std::string spec = getenv("RM8_MINI_PLAYERS") ? getenv("RM8_MINI_PLAYERS") : (game == 21 ? "hccc" : game == 22 ? "hc" : "h");
	static const char *pics[] = {"p_01", "p_02", "p_03", "p_04"};
	for(size_t i = 0; i < spec.size() && i < 4; i++){
		Rm8MiniPlayer p; p.seat = (int)i; p.human = spec[i] == 'h'; p.charId = (int)i + 1; if(const char *o = getenv("RM8_MINI_OUTFIT")) p.outfit = atoi(o);	// test hook: the outfit of every player (4 = swimsuit)
		p.pic = pics[i]; p.name = (p.human ? "Player" : "Computer") + std::to_string(i + 1);
		pl.push_back(p);
	}
	if(!rm8MiniStart(game, pl)) return 1;
	RGBA bg = {0, 0, 0, 255};
	if(!shot.empty()){
		cam->frameBuffer = Raster::create(W, H, 32, Raster::CAMERATEXTURE | Raster::C8888);
		cam->zBuffer = Raster::create(W, H, 0, Raster::ZBUFFER);
		rm8UiSetScreen(W, H); rm8MiniSetScreen(W, H);
		for(float t = 0; t < shotTime && !rm8MiniDone(); t += 1.0f / 30) { rm8MiniUpdate(std::min(1.0f / 30, shotTime - t)); rm8MiniDrawHeadless(); }
		for(int f = 0; f < 2; f++){ cam->clear(&bg, Camera::CLEARIMAGE | Camera::CLEARZ); cam->beginUpdate(); rm8MiniRender(cam); cam->endUpdate(); }
		Image *img = cam->frameBuffer->toImage();
		if(!img){ rm8Log("readback failed"); return 1; }
		writePNG(img, shot.c_str()); img->destroy();
		if(FILE *chk = fopen(shot.c_str(), "rb")) fclose(chk); else { rm8Log("error: could not write %s", shot.c_str()); return 1; }
		rm8Log("wrote %s", shot.c_str());
		if(rm8MiniDone()) reportResult(); else rm8MiniAbort();
		return 0;
	}
	int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
	setupCam(cam, dw, dh); rm8UiSetScreen(dw, dh); rm8MiniSetScreen(dw, dh);
	const uint32 flipFlags = getenv("RM8_NOVSYNC") ? 0 : Raster::FLIPWAITVSYNCH;
	Uint32 last = SDL_GetTicks(), doneAt = 0;
	bool run = true, cursorState = true;
	while(run){
		SDL_Event e;
		while(SDL_PollEvent(&e)){
			switch(e.type){
			case SDL_QUIT: run = false; break;
			case SDL_APP_WILLENTERBACKGROUND: rm8MiniPause(true); break;
			case SDL_APP_DIDENTERFOREGROUND: rm8MiniPause(false); last = SDL_GetTicks(); break;
			case SDL_WINDOWEVENT:
				if(e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED){ SDL_GL_GetDrawableSize(win, &dw, &dh); if(dw > 0 && dh > 0){ setupCam(cam, dw, dh); rm8UiSetScreen(dw, dh); } }
				break;
			case SDL_KEYDOWN:
				if(e.key.repeat) break;
				if(e.key.keysym.scancode == SDL_SCANCODE_AC_BACK || (e.key.keysym.sym == SDLK_ESCAPE && game != 23)){ run = false; break; }
				if(int k = rm8MiniKeyFromSdl(e.key.keysym.sym)) rm8MiniKey(k, true);
				break;
			case SDL_KEYUP: if(int k = rm8MiniKeyFromSdl(e.key.keysym.sym)) rm8MiniKey(k, false); break;
			case SDL_MOUSEMOTION: if(e.motion.which != SDL_TOUCH_MOUSEID) rm8MiniPointer(2, (float)e.motion.x, (float)e.motion.y); break;
			case SDL_MOUSEBUTTONDOWN: if(e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_LEFT) rm8MiniPointer(0, (float)e.button.x, (float)e.button.y); break;
			case SDL_MOUSEBUTTONUP: if(e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_LEFT) rm8MiniPointer(1, (float)e.button.x, (float)e.button.y); break;
			case SDL_FINGERDOWN: rm8MiniPointer(0, e.tfinger.x * dw, e.tfinger.y * dh, (int)e.tfinger.fingerId, true); break;
			case SDL_FINGERUP: rm8MiniPointer(1, e.tfinger.x * dw, e.tfinger.y * dh, (int)e.tfinger.fingerId, true); break;
			case SDL_FINGERMOTION: rm8MiniPointer(2, e.tfinger.x * dw, e.tfinger.y * dh, (int)e.tfinger.fingerId, true); break;
			}
		}
		Uint32 now = SDL_GetTicks();
		float dt = std::min(0.1f, (now - last) / 1000.0f); last = now;
		rm8MiniUpdate(dt);
		bool hide = rm8MiniCursorHidden();
		if(hide == cursorState){ SDL_ShowCursor(hide ? SDL_DISABLE : SDL_ENABLE); cursorState = !hide; }
		cam->clear(&bg, Camera::CLEARIMAGE | Camera::CLEARZ);
		cam->beginUpdate(); rm8MiniRender(cam); cam->endUpdate();
		cam->showRaster(flipFlags);
		if(rm8MiniDone()){ if(!doneAt) doneAt = now; if(now - doneAt > 500){ reportResult(); run = false; } }
	}
	SDL_ShowCursor(SDL_ENABLE);
	rm8MiniAbort();
	return 0;
}

#endif
