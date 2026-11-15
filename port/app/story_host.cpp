// The host of the story map menu, see story_host.h. [V] = verified in the decompile (address), [A] port adaptation.
#include "story_host.h"
#include "story.h"
#include "audio.h"
#include "present.h"
#include "ui.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <map>

using namespace rw;

namespace {

Rm8Scene *g_sc = nullptr;
struct HArt { std::string dff, texdir, uva; std::map<int, std::string> anims; } g_art;
bool g_artLoaded = false;
Clump *g_mesh = nullptr;
std::vector<Light*> g_lights;

int g_state = 1;
float g_timer = 0;		// this+8 [V]
int g_voice = 0;		// this+0xC [V]
std::string g_line;
Rm8Setup g_setup; int g_cand[4] = {0, 0, 0, 0}, g_level = 0;
float g_idleT = 0; int g_hostAnim = 3; bool g_once = false;

float waitScale(){ return 1.0f - std::min((rm8GameSpeed - 1) / 10.0f, 1.0f); }	// [V] Anim_ApplyDifficultyScale @0x47b3b0
void setWait(float s){ g_timer = s * waitScale(); }

bool loadArt(const std::string &root)
{
	if(g_artLoaded) return !g_art.dff.empty();
	g_artLoaded = true;
	std::ifstream f(root + "/movie_arts.txt"); std::string line;
	while(f && std::getline(f, line)){
		std::istringstream ss(line); std::string kw; int id; ss >> kw >> id;
		if(kw != "mart" || id != 1048) continue;
		std::string tok; ss >> g_art.dff >> g_art.texdir >> g_art.uva;
		while(ss >> tok){ size_t c = tok.find(':'); if(c != std::string::npos) g_art.anims[atoi(tok.c_str())] = tok.substr(c + 1); }
	}
	return !g_art.dff.empty();
}
std::string g_root;

// [V] Cls_5e81e0_LoadMesh @0x502d90: Mesh_LoadAndInit(0x418 = 1048), Mesh_Rotate(180), Mesh_SetTranslation((100, -100, 500))
void loadMesh()
{
	if(g_mesh || !g_sc || !g_sc->world) return;
	if(!loadArt(g_root)){ rm8Log("story host: art 1048 is not in movie_arts.txt"); return; }
	const float pos[3] = {100, -100, 500}, one[3] = {1, 1, 1};
	auto a = g_art.anims.find(3);
	g_mesh = rm8SceneAddArticle(*g_sc, g_art.dff, g_art.texdir, pos, one, 180.0f, a != g_art.anims.end() ? a->second : "-", g_art.uva, "loop");
	g_hostAnim = 3; g_once = false;
	if(g_mesh && g_lights.empty()){	// [A] the menu scene has no lights of its own
		Light *amb = Light::create(Light::AMBIENT); amb->setColor(0.75f, 0.75f, 0.75f); g_sc->world->addLight(amb); g_lights.push_back(amb);
		Light *dir = Light::create(Light::DIRECTIONAL); dir->setColor(0.5f, 0.5f, 0.5f);
		Frame *df = Frame::create(); V3d ax = {1, 0, 0}; df->rotate(&ax, 40.0f, COMBINEREPLACE); dir->setFrame(df);
		g_sc->world->addLight(dir); g_lights.push_back(dir);
	}
}

void playAction(int anim, bool once)	// [V] CUi_chsMapMenu_InitTexture(anim, 1, Ui_InitTexture): action `anim` once, then action 3
{
	if(!g_mesh || !g_sc) return;
	auto a = g_art.anims.find(anim);
	if(a == g_art.anims.end()) return;
	if(!rm8SceneArticleAnim(*g_sc, g_mesh, a->second, once)) return;
	g_hostAnim = anim; g_once = once;
}

// [V] Game_PlayMovie @0x504460: the previous sample is stopped, the line (string id) is shown and its voice played; the host plays `anim`
void say(int strId, int anim)
{
	Rm8SpeechLine l;
	if(!rm8SpeechParse(strId, l)){ rm8Log("story host: string %d is empty", strId); g_line.clear(); return; }
	g_line = l.text;
	if(g_voice){ rm8SndStop(g_voice); g_voice = 0; }
	if(l.voice > 0) g_voice = rm8SndPlay(l.voice, 1);	// [V] StringTable + 32000 + id * 4 (the '@' number)
	if(anim >= 0) playAction(anim, true);
}
// [V] u_Game_LoadMovieFrame @0x504540: seat `i`'s role name (string 3800 + role Id); wait 0.5 s after it
bool sayPlayer(int i, int anim)
{
	if(i < 0 || i >= (int)g_setup.seats.size() || g_setup.seats[i].charId <= 0) return false;
	say(3800 + g_setup.seats[i].charId, anim); setWait(0.5f); return true;
}
// [V] u_Game_ShowMovieEx @0x5045f0: the map's name (string 3816 + map id)
bool sayMap(int map, int anim){ if(map <= 0) return false; say(3816 + map, anim); setWait(0.5f); return true; }

}	// namespace

void rm8StoryHostSetScene(Rm8Scene *sc, const std::string &root){ g_sc = sc; g_root = root; }

void rm8StoryHostBegin(int initState, const Rm8Setup &st, const int cand[4], int level)
{
	rm8StoryHostEnd();
	g_setup = st; g_level = level;
	for(int k = 0; k < 4; k++) g_cand[k] = cand ? cand[k] : 0;
	g_state = initState; g_timer = 0; g_voice = 0; g_line.clear();
	if(initState != 1) rm8Log("story host: begin state 0x%x (layer %d)", initState, level);
}

int rm8StoryHostState(){ return g_state; }
bool rm8StoryHostBusy(){ return g_state != 1 && g_state != 0x65; }
const std::string &rm8StoryHostLine(){ return g_line; }

void rm8StoryHostSkip()
{
	if(g_state == 1) return;
	if(g_state >= 3 && g_state <= 8) g_state = 0x65;	// [V] @0x5041f0
	else if(g_state > 0xb && g_state < 0x10) g_state = 1;
	if(g_voice){ rm8SndStop(g_voice); g_voice = 0; }
	g_timer = 0;
}

void rm8StoryHostUpdate(float dt)
{
	if(g_state == 1 || g_state == 0x65 || !g_sc) { if(g_mesh && g_sc) rm8UpdateScene(*g_sc, dt); return; }
	if(g_voice && !rm8SndPlaying(g_voice)) g_voice = 0;	// [V] the sample's callback @0x5043d0 clears +0xC
	if(g_mesh && g_sc){
		rm8UpdateScene(*g_sc, dt);	// the host's action
		// [V] Ui_InitTexture @0x5030e0: when the once-played action ends the idle action 3 loops again
		if(g_once){
			for(Rm8ArtAnim &aa : g_sc->anims) if(aa.clump == g_mesh){
				AnimInterpolator *ip = aa.hier->interpolator;
				if(ip->currentAnim && ip->currentTime >= ip->currentAnim->duration - 2e-3f){ playAction(3, false); }
			}
		}
	}
	if(g_voice) return;	// [V] in_ECX[3] != 0: the timer does not run
	g_timer -= dt;
	if(g_timer > 0) return;
	g_timer = 0;
	switch(g_state){	// [V] Cls_5e81e0__vf1_OnUpdate @0x503830
	case 2: loadMesh(); sayPlayer(0, 0); g_state = 3; break;
	case 3: say(3828, 1); setWait(0.2f); g_state = 4; break;	// 0xef4 "<str#3828>，<str#3828>"
	case 4: sayMap(g_setup.map, 1); g_state = 5; break;	// u_Game_ShowMovieEx(setup+4, 1)
	case 5: loadMesh(); say(3829, 1); setWait(1.0f); g_state = 6; break;	// 0xef5 "<str#3829>"
	case 6: sayPlayer(1, 1); g_state = 7; break;
	case 7: sayPlayer(2, 1); g_state = 8; break;
	case 8: sayPlayer(3, 1); g_state = 0x65; break;
	case 0xb:
		loadMesh();
		if(g_level < 3){ say(3830, 1); g_state = 0xc; }	// 0xef6 "<str#3830>，接下來，請在"
		else{ say(g_setup.seats.empty() || g_setup.seats[0].charId != 16 ? 3834 : 3835, 2); g_state = 100; }	// 0xefa / 0xefb, action 2
		break;
	case 0xc: case 0xd: case 0xe: case 0xf: {
		int k = g_state - 0xc;
		sayMap(g_cand[k], 1);	// u_Game_ShowMovieEx(candidate k, 1): a missing candidate says nothing and goes on at once
		g_state++;
		break; }
	case 0x10: say(3831, 1); g_state = 1; break;	// 0xef7 "<str#3831>"; the cursor is shown again
	case 100: g_state = 1; break;
	}
}

// [V] 3D pass like menu_anim.cpp render3D: the camera frame is identity (Camera_SaveViewState), RwCameraClear(z) before the mesh, alpha test >= 196
void rm8StoryHostRender(Camera *cam)
{
	if(!g_mesh || !g_sc) return;
	float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height, Hb = H * 4.0f / 3.0f;
	cam->endUpdate();
	Matrix savedFrame = cam->getFrame()->matrix; V2d savedVw = cam->viewWindow; float savedNear = cam->nearPlane, savedFar = cam->farPlane;
	V2d vw = {0.3f * W / Hb, 0.225f};
	cam->setViewWindow(&vw); cam->setNearPlane(10.0f); cam->setFarPlane(5000.0f);
	Matrix id; id.setIdentity(); cam->getFrame()->transform(&id, COMBINEREPLACE);
	RGBA none = {0, 0, 0, 0};
	cam->clear(&none, Camera::CLEARZ);
	cam->beginUpdate();
	uint32 fn = GetRenderState(ALPHATESTFUNC), ref = GetRenderState(ALPHATESTREF), zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE);
	SetRenderState(ZTESTENABLE, 1); SetRenderState(ZWRITEENABLE, 1);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196);
	rm8RenderArticles(*g_sc);
	SetRenderState(ALPHATESTFUNC, fn); SetRenderState(ALPHATESTREF, ref); SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw);
	cam->endUpdate();
	cam->getFrame()->transform(&savedFrame, COMBINEREPLACE);
	cam->setViewWindow(&savedVw); cam->setNearPlane(savedNear); cam->setFarPlane(savedFar);
	cam->beginUpdate();
}

void rm8StoryHostEnd()
{
	if(g_voice){ rm8SndStop(g_voice); g_voice = 0; }
	if(g_mesh && g_sc && g_sc->world) rm8SceneRemoveArticle(*g_sc, g_mesh);
	g_mesh = nullptr;
	if(g_sc && g_sc->world) for(Light *l : g_lights){ Frame *f = l->getFrame(); g_sc->world->removeLight(l); l->destroy(); if(f) f->destroy(); }
	g_lights.clear();
	g_line.clear(); g_state = 1;
}
