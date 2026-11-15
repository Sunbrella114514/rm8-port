// rm8_scene: render a RichMan8 map exported by scripts/export_scene.py to a PNG (desktop, librw GL3).
// usage: rm8_scene <game_dir> <scene_dir> <out.png> [--cam yawDeg pitchDeg distFactor] [--target x z] [--size WxH] [--time animSeconds]
//        [--camfile f]                camera read from the running game (scripts/game_camera.py)
//        [--roles 1,2,3,4] [--seed n] [--speed n] [--follow]   characters (scenes/roles.txt), computer turns;
//                                     --follow: the camera snaps to the turn focus like the game
//        [--role-at id x y z model anim]            place one character (model 0 fl 1 fr 2 br 3 bl), no movement
//        [--seq count interval]       write out_000.png, out_001.png ... every interval seconds
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
// rw.h comes through scene.h (rw.h has no include guard)
#include "../app/turn.h"
#include "../app/events.h"
#include "../app/ui.h"
#ifdef LIBRW_SDL2
#include <SDL.h>
#else
#include <GLFW/glfw3.h>
#endif

using namespace rw;

int main(int argc, char *argv[])
{
	if(argc < 4){ fprintf(stderr, "usage: %s game_dir scene_dir out.png [--cam yaw pitch distFactor] [--target x z] [--size WxH]\n", argv[0]); return 2; }
	std::string game = argv[1], sdir = argv[2], outPng = argv[3];
	float camYaw = 0, camPitch = 50, camDist = 0.9f, tx = 0, tz = 0, animTime = 0; int W = 1600, H = 1200; const char *camFile = nullptr;
	std::vector<int> roleIds; unsigned seed = 1; int seqN = 1; float seqDt = 0.25f;
	bool followCam = false;
	std::vector<std::string> uiNames; std::string font = "C:/Windows/Fonts/simsun.ttc"; int fontIdx = 0;
	struct At { int id, model, anim; float p[3]; };
	std::vector<At> roleAt;
	for(int i = 4; i < argc; i++){
		if(!strcmp(argv[i], "--cam") && i + 3 < argc){ camYaw = (float)atof(argv[i+1]); camPitch = (float)atof(argv[i+2]); camDist = (float)atof(argv[i+3]); i += 3; }
		else if(!strcmp(argv[i], "--target") && i + 2 < argc){ tx = (float)atof(argv[i+1]); tz = (float)atof(argv[i+2]); i += 2; }
		else if(!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
		else if(!strcmp(argv[i], "--time") && i + 1 < argc) animTime = (float)atof(argv[++i]);
		else if(!strcmp(argv[i], "--camfile") && i + 1 < argc) camFile = argv[++i];
		else if(!strcmp(argv[i], "--roles") && i + 1 < argc){ for(char *t = strtok(argv[++i], ","); t; t = strtok(nullptr, ",")) roleIds.push_back(atoi(t)); }
		else if(!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (unsigned)atoi(argv[++i]);
		else if(!strcmp(argv[i], "--speed") && i + 1 < argc) rm8GameSpeed = atoi(argv[++i]);
		else if(!strcmp(argv[i], "--follow")) followCam = true;
		else if(!strcmp(argv[i], "--ui") && i + 1 < argc){ for(char *t = strtok(argv[++i], ","); t; t = strtok(nullptr, ",")) uiNames.push_back(t); }
		else if(!strcmp(argv[i], "--font") && i + 2 < argc){ font = argv[i+1]; fontIdx = atoi(argv[i+2]); i += 2; }
		else if(!strcmp(argv[i], "--seq") && i + 2 < argc){ seqN = atoi(argv[i+1]); seqDt = (float)atof(argv[i+2]); i += 2; }
		else if(!strcmp(argv[i], "--role-at") && i + 6 < argc){
			At a; a.id = atoi(argv[i+1]); for(int k = 0; k < 3; k++) a.p[k] = (float)atof(argv[i+2+k]);
			a.model = atoi(argv[i+5]); a.anim = atoi(argv[i+6]); roleAt.push_back(a); i += 6;
		}
	}
	if(!rm8OpenPacks(game)) return 1;

	Engine::init();
	rm8RegisterPlugins();
	EngineOpenParams op; memset(&op, 0, sizeof op);
#ifdef LIBRW_SDL2
	SDL_Window *win = nullptr;
#else
	GLFWwindow *win = nullptr;
#endif
	op.width = W; op.height = H; op.windowtitle = "rm8_scene"; op.window = &win;
	if(!Engine::open(&op) || !Engine::start()){ fprintf(stderr, "engine start failed\n"); return 1; }
	rm8InstallTextureCallback();

	Rm8Scene sc;
	if(!rm8LoadScene(sdir, sc)) return 1;
	if(!roleIds.empty() || !roleAt.empty()){
		if(!rm8RolesLoadDefs(sdir + "/../roles.txt")) return 1;
		rm8PyInit(game + "/Pysrc");	// original Python scripts (computer decisions)
		if(!roleIds.empty()) rm8RolesSpawn(sc, roleIds, seed, -1);
		for(At &a : roleAt) rm8RolesPlace(sc, a.id, a.p, a.model, a.anim);
	}

	Camera *cam = Camera::create();
	cam->setFrame(Frame::create());
	cam->frameBuffer = Raster::create(W, H, 32, Raster::CAMERATEXTURE | Raster::C8888);
	cam->zBuffer = Raster::create(W, H, 0, Raster::ZBUFFER);
	V2d vw = {0.5f, 0.5f * H / W}; cam->setViewWindow(&vw);
	sc.world->addCamera(cam);
	rm8PlaceCamera(cam, sc, camYaw, camPitch, camDist, tx, tz);
	if(camFile){	// camera read from the running game (scripts/game_camera.py): 16 floats LTM, view window x y, near, far
		FILE *cf = fopen(camFile, "r"); float v[20];
		for(int k = 0; k < 20; k++) if(!cf || fscanf(cf, "%f", &v[k]) != 1){ fprintf(stderr, "bad camfile\n"); return 1; }
		fclose(cf);
		Matrix m; m.setIdentity();
		m.right = {v[0], v[1], v[2]}; m.up = {v[4], v[5], v[6]}; m.at = {v[8], v[9], v[10]}; m.pos = {v[12], v[13], v[14]}; m.flags = 0;	// setIdentity() left the IDENTITY flag set
		cam->getFrame()->transform(&m, COMBINEREPLACE);
		V2d gw = {v[16], v[17]}; cam->setViewWindow(&gw); cam->setNearPlane(v[18]); cam->setFarPlane(v[19]);
	}
	auto follow = [&](){	// [V] Camera_SetFocusPoint: camera = focus - 800 at
		if(!followCam) return;
		float f[3]; rm8RolesFocus(f);
		Matrix m = *cam->getFrame()->getLTM(); m.flags = 0;
		m.pos = {f[0] - 800.0f * m.at.x, f[1] - 800.0f * m.at.y, f[2] - 800.0f * m.at.z};
		cam->getFrame()->transform(&m, COMBINEREPLACE);
	};
	auto advance = [&](float T){
		for(float t = 0; t < T; t += 1.0f / 30){ float d = T - t < 1.0f / 30 ? T - t : 1.0f / 30; rm8UpdateScene(sc, d); rm8RolesSetCamera(cam); rm8RolesUpdate(sc, d); rm8EventUpdate(d); follow(); }
	};
	if(!uiNames.empty()){	// --ui gmaindlg,okcancel: show .ui screens over the scene (first = HUD layout, others dialogs)
		rm8UiInit(sdir + "/../ui", font, fontIdx); rm8UiSetScreen(W, H);
		for(size_t k = 0; k < uiNames.size(); k++){
			if(uiNames[k] == "hud"){ std::vector<std::string> p; for(int j = 0; j < rm8TurnRoleCount(); j++) p.push_back(rm8TurnPic(j)); rm8HudInit(p); continue; }
			rm8UiShow(rm8UiLoad(uiNames[k], k == 0 ? RM8UI_HUD : RM8UI_DIALOG), true);
		}
	}
	rm8RolesSetCamera(cam);
	advance(animTime);

	RGBA bg = {96, 150, 200, 255};
	for(int k = 0; k < seqN; k++){
		if(k > 0) advance(seqDt);
		for(int frame = 0; frame < 3; frame++){
			rm8RenderReflection(sc, cam);
			cam->clear(&bg, Camera::CLEARIMAGE | Camera::CLEARZ);
			cam->beginUpdate();
			rm8RenderScene(sc); rm8PrtRender(cam);
			rm8RolesRender();
			rm8RolesRenderLate(cam);
			if(!uiNames.empty()) rm8HudRender(cam);
			cam->endUpdate();
		}
		Image *img = cam->frameBuffer->toImage();
		if(!img){ fprintf(stderr, "readback failed\n"); return 1; }
		std::string fn = outPng;
		if(seqN > 1){ char suf[16]; snprintf(suf, sizeof suf, "_%03d.png", k); fn = outPng.substr(0, outPng.size() - 4) + suf; }
		writePNG(img, fn.c_str());
		img->destroy();
		if(FILE *chk = fopen(fn.c_str(), "rb")) fclose(chk);
		else { fprintf(stderr, "error: could not write %s (does the directory exist?)\n", fn.c_str()); return 1; }
	}
	printf("particles=%d emitters=%d ", rm8PrtCount(), sc.nPrt);
	printf("leaves=%d road_quads=%d articles=%d failed=%d skin_to_cpu=%d animated=%d anim_failed=%d uv_animated=%d pad_quads=%d max_gpu_bones=%d missing_textures=%d -> %s\n",
	       sc.nLeaf, sc.nRoadQuads, sc.nArt, sc.nArtFail, sc.nSkinToCpu, sc.nAnimated, sc.nAnimFail, sc.nUVAnimated, sc.nPadQuads, (int)gl3::skinMaxBones, rm8MissingTextures(), outPng.c_str());
	return 0;
}
