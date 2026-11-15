// Test host for the minigames only (same startup as rm8_app, without the board). See CMakeLists.txt.
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include "scene.h"
#include "ui.h"
#include "zh.h"
#include "audio.h"
#include "minigame.h"

using namespace rw;

int main(int argc, char *argv[])
{
	if(argc < 3){ fprintf(stderr, "usage: %s game_dir scenes_root --minigame N [--shot out.png] [--time t] [--size WxH]\n", argv[0]); return 2; }
	std::string game = argv[1], scenes = argv[2], shot; int W = 1280, H = 720, mini = 0; float shotTime = 0;
	for(int i = 3; i < argc; i++){
		if(!strcmp(argv[i], "--minigame") && i + 1 < argc) mini = atoi(argv[++i]);
		else if(!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
		else if(!strcmp(argv[i], "--time") && i + 1 < argc) shotTime = (float)atof(argv[++i]);
		else if(!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
	}
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	Engine::init(); rm8RegisterPlugins();
	SDL_Window *win = nullptr;
	EngineOpenParams op; memset(&op, 0, sizeof op);
	op.width = W; op.height = H; op.windowtitle = "RichMan8 minigame test"; op.window = &win;
	if(!Engine::open(&op) || !Engine::start()){ rm8Log("engine start failed: %s", SDL_GetError()); return 1; }
	rm8InstallTextureCallback();
	if(!rm8OpenPacks(game)) return 1;
	if(shot.empty()) rm8AudioInit(game);
	Camera *cam = Camera::create(); cam->setFrame(Frame::create());
	const char *lang = getenv("RM8_LANG");
	bool simplified = !(lang && !strcmp(lang, "zh-Hant")) && rm8ZhLoad(scenes + "/zh_t2s.txt");
#ifdef __ANDROID__
	const char *fontFile = "/system/fonts/NotoSerifCJK-Regular.ttc"; int fontIdx = simplified ? 2 : 3;
#else
	const char *fontFile = "C:/Windows/Fonts/simsun.ttc"; int fontIdx = 0;
#endif
	if(!rm8UiInit(scenes + "/ui", fontFile, fontIdx)){ rm8Log("UI data missing"); return 1; }
	int rc = rm8MiniStandalone(win, cam, mini, shot, shotTime, W, H, scenes);
	rm8AudioShutdown();
	return rc;
}
