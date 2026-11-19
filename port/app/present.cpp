// Character presentation: expressions, speech (info box, speech box, bubbles, voices), role-to-role reactions and the
// effect player. Facts marked [V] are read from the decompile (docs/presentation.md has the addresses); [H] hypothesis, [A] port adaptation.
#include "present.h"
#include "story.h"
#include "ui.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <vector>

using namespace rw;

namespace {

// ---- data (roles.txt: rstr / sp / spl / fxart / fxhook)
struct FxArt { std::string dff, texdir, uva; int prt = -1; std::map<int, std::string> anims; std::vector<std::pair<int, int>> hooks;
	struct Ev { int action; float time; int effect, hook; };
	std::vector<Ev> events; };	// events: role.bin sub-list 4 (timed particle effects on a [@hook] frame, roles.txt fxev)
struct SpLine { int rl = 0; std::string text; };
struct SpEntry { int pId = 0; bool used = false; std::vector<SpLine> lines; };
std::map<int, FxArt> g_fxart;
std::map<int, int> g_rstr;			// role id -> roleinfo strId (+0x38c)
std::map<int, std::vector<SpEntry>> g_sp;	// role id -> its sp_%02d entries in file order (Config_LoadNpcSpeech @0x494e70)

Rm8Scene *g_s = nullptr;
bool g_hidden[8] = {};	// Role_ClearFlag1C8 per seat (the doll is not drawn)
Camera *g_cam = nullptr;
bool g_story = getenv("RM8_STORY") != nullptr;	// [V] &DAT_00632de8 +0: story mode flag (the pair dialogues only exist there)

float waitScale(){ return 1.0f - std::min((rm8GameSpeed - 1) / 10.0f, 1.0f); }	// [V] @0x47b3b0

int strIdOf(int seat)
{
	int id = rm8TurnCharId(seat);
	auto it = g_rstr.find(id);
	return it != g_rstr.end() ? it->second : 600 + 200 * (id - 1);	// [V] 16 roleinfo sections: 600 + 200 * (Id - 1)
}

bool validSeat(int s){ return s >= 0 && s < rm8TurnRoleCount(); }

// the game's yaw of the camera (the dolls and effects face it): the port follows its own camera, default 135
float camYaw()
{
	if(!g_cam) return 135.0f;
	const V3d &at = g_cam->getFrame()->getLTM()->at;
	if(fabsf(at.x) + fabsf(at.z) < 1e-4f) return 135.0f;
	float y = atan2f(-at.z, -at.x) * 180.0f / 3.14159265f;
	return y < 0 ? y + 360.0f : y;
}
void camForward(float f[3])	// [V] Camera_GetForwardXZ (the horizontal at vector)
{
	f[0] = 0.54f; f[1] = 0; f[2] = -0.54f;
	if(g_cam){ const Matrix *m = g_cam->getFrame()->getLTM(); f[0] = m->at.x; f[2] = m->at.z; }
	float l = sqrtf(f[0] * f[0] + f[2] * f[2]);
	if(l > 0){ f[0] /= l; f[2] /= l; }
}

// projection of world positions to screen pixels (same formula as cards.cpp: RW cameras have right pointing to screen left)
bool project(const float p[3], float &sx, float &sy)
{
	if(!g_cam || !g_cam->frameBuffer) return false;
	const Matrix *m = g_cam->getFrame()->getLTM();
	V3d d = {p[0] - m->pos.x, p[1] - m->pos.y, p[2] - m->pos.z};
	float xc = d.x * m->right.x + d.y * m->right.y + d.z * m->right.z;
	float yc = d.x * m->up.x + d.y * m->up.y + d.z * m->up.z;
	float zc = d.x * m->at.x + d.y * m->at.y + d.z * m->at.z;
	if(zc < 1.0f) return false;
	float W = (float)g_cam->frameBuffer->width, H = (float)g_cam->frameBuffer->height;
	sx = (0.5f - xc / (2.0f * g_cam->viewWindow.x * zc)) * W;
	sy = (0.5f - yc / (2.0f * g_cam->viewWindow.y * zc)) * H;
	return true;
}

// ---- per-seat state
struct SeatState {
	int voice = 0;			// [V] Anim_PlaySound @0x47aff0: the role's sample handle (+4), a new voice stops the old one
	int fx = 0;				// [V] Anim_CreateSprite @0x47b090: the role's effect article (+8 / sample +0xC) of the last 0x2C / 0x52, released by the wait skip (docs/skip.md)
	float exprT = 0;		// [A] > 0: back to standing when it runs out
	bool bubble = false; float bubbleT = 0;
	int plane = 0, text = 0, shadow = 0;	// bubble widgets (pbubble canvas)
};
std::vector<SeatState> g_seat;
SeatState &seatState(int s){ if((int)g_seat.size() <= s) g_seat.resize(s + 1); return g_seat[s]; }

void playVoice(int seat, int id)
{
	if(seat < 0) return;
	SeatState &st = seatState(seat);
	if(st.voice){ rm8SndStop(st.voice); st.voice = 0; }
	if(id > 0) st.voice = rm8SndPlay(id);
}

// ---- the dialogs
Rm8Ui *uiGt(){ static Rm8Ui *u = nullptr; static bool t = false; if(!t){ t = true; u = rm8UiLoad("gtinfo", RM8UI_DIALOG); } return u; }
Rm8Ui *uiSpeech(){ static Rm8Ui *u = nullptr; static bool t = false; if(!t){ t = true; u = rm8UiLoad("speech", RM8UI_DIALOG); } return u; }
Rm8Ui *uiBubble(){ static Rm8Ui *u = nullptr; static bool t = false; if(!t){ t = true; u = rm8UiCreateCanvas("pbubble", RM8UI_HUD); if(u) rm8UiShow(u, true); } return u; }
float g_infoT = 0; bool g_infoOn = false;

void portraitKey(char *key, size_t n, const char *pic, int idx){ snprintf(key, n, "prt\\npc\\%s_%02d.tga", pic, idx); }

// [V] Role_ShowInfoDialog @0x488ae0: gtinfo (text id 1, portrait picture id 2) when the entry has a picture, else ginfo
void showInfo(int seat, const std::string &text, int portrait)
{
	std::string t = rm8UiWrapText(text, 222.0f, 16);
	if(portrait >= 0 && validSeat(seat)){	// [V] the entry carries a picture name -> gtinfo; index 0 is "npc\<pic>_00" (not in the packs: blank)
		if(Rm8Ui *u = uiGt()){
			char key[96] = ""; if(portrait > 0) portraitKey(key, sizeof key, rm8TurnPic(seat), portrait);
			rm8UiSetText(u, 1, t); rm8UiSetPicFile(u, 2, key); rm8UiShow(u, true);
		}
	}else rm8HudInfo(t);
	g_infoOn = true;
}

void hideInfo()
{
	if(Rm8Ui *u = uiGt()) rm8UiShow(u, false);
	g_infoOn = false;
}

// ---- speech box (speech.ui): two portraits (ids 1, 2) and two texts (ids 3, 4)
void speechPortraits(int left, int right)	// [V] Ui_SetSpeechPortraits @0x48cb70: texts cleared, "npc\p_%02d_01.tga" per role Id
{
	Rm8Ui *u = uiSpeech(); if(!u) return;
	char key[96];
	rm8UiSetText(u, 3, ""); rm8UiSetText(u, 4, "");
	snprintf(key, sizeof key, "prt\\npc\\p_%02d_01.tga", left);  rm8UiSetPicFile(u, 1, left >= 0 ? key : "");
	snprintf(key, sizeof key, "prt\\npc\\p_%02d_01.tga", right); rm8UiSetPicFile(u, 2, right >= 0 ? key : "");
	rm8UiShow(u, true);
}
void speechSay(bool leftSide, const std::string &text)	// [V] Ui_ShowSpeechLeft @0x48ccb0 / Right @0x48cdb0: own text, the other cleared
{
	Rm8Ui *u = uiSpeech(); if(!u) return;
	std::string t = rm8UiWrapText(text, leftSide ? 190.0f : 196.0f, 16);
	rm8UiSetText(u, leftSide ? 3 : 4, t); rm8UiSetText(u, leftSide ? 4 : 3, "");
	rm8UiShow(u, true);
}

// ---- bubbles above the head
const float HEAD_Y = 105.0f;	// [H] head height above the node (paper doll scale 1.6; the game derives it from the mesh, @0x511c10)
void ensureBubble(int seat)
{
	Rm8Ui *u = uiBubble(); if(!u) return;
	SeatState &st = seatState(seat);
	if(st.plane) return;
	// [V] movieWord.ui: Plane 256x128 picture 1087 (j235.tga); label "word" (15,10,227,108) white over "word2" (16,11) as its shadow
	st.plane = rm8UiAddWidget(u, "plane", 0, 0, 0, 256, 128, 1087);
	st.shadow = rm8UiAddWidget(u, "label", st.plane, 16, 11, 227, 108, -1, 0, 16, 1);
	st.text = rm8UiAddWidget(u, "label", st.plane, 15, 10, 227, 108, -1, 0, 16, 2);
	char nm[32];
	snprintf(nm, sizeof nm, "dyn%d", st.shadow); rm8UiSetColorN(u, nm, 0xff000000); rm8UiSetAlignN(u, nm, 0, 1);
	snprintf(nm, sizeof nm, "dyn%d", st.text); rm8UiSetColorN(u, nm, 0xffffffff); rm8UiSetAlignN(u, nm, 0, 1);
	rm8UiSetVisible(u, st.plane, false);
}

void showBubble(int seat, const std::string &text, float seconds)
{
	if(!validSeat(seat)) return;
	ensureBubble(seat);
	Rm8Ui *u = uiBubble(); SeatState &st = seatState(seat);
	if(!u || !st.plane) return;
	std::string t = rm8UiWrapText(text, 227.0f, 16);
	rm8UiSetText(u, st.text, t); rm8UiSetText(u, st.shadow, t);
	rm8UiSetVisible(u, st.plane, true);
	st.bubble = true; st.bubbleT = seconds;
}
void hideBubble(int seat)
{
	SeatState &st = seatState(seat);
	if(st.plane) rm8UiSetVisible(uiBubble(), st.plane, false);
	st.bubble = false;
}

// ---- effect player
struct Fx {
	int id = 0, art = 0;
	Clump *clump = nullptr;
	std::vector<int> prt;		// particle emitters (a particle art, or [@hook] children)
	int snd = 0;
	float life = 0, age = 0;
	bool once = true, autoRemove = false, hasAnim = false, done = false, overlay = false;
	int anim = 0; float prevClip = -1;	// the playing action and the clip time the timed events were last checked at
	int seat = -1;			// the role this effect belongs to (the original keeps its handle in the role: +0x50 / +0x54)
};
std::vector<Fx> g_fx;
int g_fxNext = 1;

Fx *fxFind(int h){ for(Fx &f : g_fx) if(f.id == h) return &f; return nullptr; }

// the playing clip's time (the article's own age without an animation)
float fxClipTime(const Fx &f)
{
	if(g_s && f.clump && f.hasAnim) for(Rm8ArtAnim &aa : g_s->anims) if(aa.clump == f.clump){ AnimInterpolator *ip = aa.hier->interpolator; return ip->currentAnim ? ip->currentTime : f.age; }
	return f.age;
}
// [V] Article_Update @0x448540: the role.bin timed events of the playing action (sub-list 4) fire when the clip time passes them: a particle effect on the article's [@hook] frame
// (6001 builders: smoke 10 at 0.9 s, 6002 demolition: 10 at 0, 6004 monster: 10 at 0.192, 6008 missile: 12 at 1.3, 6027 tornado: 11 at 0 looping)
void fxTimedEvents(Fx &f)
{
	auto it = g_fxart.find(f.art);
	if(it == g_fxart.end() || it->second.events.empty() || !f.clump) return;
	float t = fxClipTime(f);
	for(const FxArt::Ev &e : it->second.events)
		if(e.action == f.anim && e.time > f.prevClip && e.time <= t){ int h = rm8PrtAttachH(e.effect, f.clump, e.hook); if(h >= 0) f.prt.push_back(h); if(getenv("RM8_DEBUG_PRESENT")) rm8Log("present: effect article %d: particle %d on [@%d] at %.2f s -> %s", f.art, e.effect, e.hook, t, h >= 0 ? "ok" : "failed"); }
	f.prevClip = t;
}

bool animEnded(const Fx &f)
{
	if(!g_s || !f.clump || !f.hasAnim) return false;
	for(Rm8ArtAnim &aa : g_s->anims) if(aa.clump == f.clump){
		AnimInterpolator *ip = aa.hier->interpolator;
		return ip->currentAnim && ip->currentTime >= ip->currentAnim->duration - 2e-3f;
	}
	return false;
}

void fxFree(Fx &f)
{
	for(int p : f.prt) rm8PrtStopH(p);
	f.prt.clear();
	if(f.clump && g_s) rm8SceneRemoveArticle(*g_s, f.clump);
	f.clump = nullptr;
	if(f.snd){ rm8SndStop(f.snd); f.snd = 0; }
}

}	// namespace

// ======================================================================== data
bool rm8PresentLoadDefs(const std::string &path)
{
	std::ifstream f(path);
	if(!f) return false;
	g_fxart.clear(); g_rstr.clear(); g_sp.clear();
	std::string line; SpEntry *cur = nullptr; int curOwner = 0, remain = 0;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "rstr"){ int id, s; ss >> id >> s; g_rstr[id] = s; }
		else if(kw == "sp"){ int o, p, n; ss >> o >> p >> n; auto &v = g_sp[o]; v.emplace_back(); v.back().pId = p; cur = &v.back(); curOwner = o; remain = n; }
		else if(kw == "spl" && cur && remain > 0){
			SpLine l; ss >> l.rl; std::getline(ss, l.text);
			if(!l.text.empty() && l.text[0] == ' ') l.text.erase(0, 1);
			cur->lines.push_back(l); remain--;
			if(remain == 0) cur = nullptr;
		}
		else if(kw == "fxart"){
			int a; FxArt d; std::string tok; ss >> a >> d.dff >> d.texdir >> d.prt >> d.uva;
			while(ss >> tok){ size_t c = tok.find(':'); if(c != std::string::npos) d.anims[atoi(tok.c_str())] = tok.substr(c + 1); }
			g_fxart[a] = d;
		}
		else if(kw == "fxhook"){ int a, h, e; ss >> a >> h >> e; g_fxart[a].hooks.push_back({h, e}); }
		else if(kw == "fxev"){ int a, act, e, h, ty; float t; ss >> a >> act >> t >> e >> h >> ty; g_fxart[a].events.push_back({act, t, e, h}); }
	}
	(void)curOwner;
	rm8Log("present: %d speech entries, %d effect arts", (int)g_sp.size() * 15, (int)g_fxart.size());
	return true;
}

void rm8PresentSetScene(Rm8Scene *s)
{
	if(!s) rm8FxStopAll();
	g_s = s;
	for(auto &kv : g_sp) for(SpEntry &e : kv.second) e.used = false;	// [V] the entry marks are per game (CPlayerTag_FindAndMarkEntry @0x493770)
	for(SeatState &st : g_seat){ if(st.voice) rm8SndStop(st.voice); }
	g_seat.clear(); g_infoOn = false;
	for(bool &h : g_hidden) h = false;
}

// ======================================================================== speech lines
bool rm8SpeechParse(int strId, Rm8SpeechLine &out)
{
	const std::string &raw = rm8Str(strId);
	if(raw.empty()) return false;
	out = Rm8SpeechLine();
	// [V] StringTable_LoadFromIni @0x4015e0: text up to '@'; the integer after '@' is the sample id, the one after the next '$' the animation
	size_t at = raw.find('@');
	if(at == std::string::npos){ out.text = raw; return true; }
	out.text = raw.substr(0, at);
	out.voice = atoi(raw.c_str() + at + 1);
	size_t d = raw.find('$', at + 1);
	if(d != std::string::npos){ const char *p = raw.c_str() + d + 1; if(*p >= '0' && *p <= '9') out.anim = atoi(p); }
	return true;
}

// ======================================================================== expressions
void rm8RoleExpression(int seat, int id, float seconds)
{
	if(!validSeat(seat)) return;
	rm8TurnPlayAnim(seat, id, false);	// [V] CPlayerTag vf5(id, 0, 0): the animation loops, role.bin sub-list 3 swaps the head texture
	seatState(seat).exprT = seconds;
}

// ======================================================================== speech
void rm8RoleSpeak(int seat, const std::string &text, int voice, Rm8SpeakStyle style, float seconds, int portrait)
{
	if(voice > 0) playVoice(seat, voice);
	if(style == RM8SPEAK_BUBBLE) showBubble(seat, text, seconds);
	else { showInfo(seat, text, portrait > 0 ? portrait : -1); g_infoT = seconds; }
}

void rm8RoleSpeakStr(int seat, int strId, Rm8SpeakStyle style, float seconds)
{
	Rm8SpeechLine l;
	if(!rm8SpeechParse(strId, l)) return;
	rm8RoleSpeak(seat, l.text, l.voice, style, seconds, 0);
}

void rm8RoleStopSpeak(int seat)
{
	if(validSeat(seat)){ hideBubble(seat); playVoice(seat, 0); }
	hideInfo();
}

void rm8PresentHideSpeech()
{
	hideInfo();
	if(Rm8Ui *u = uiSpeech()) rm8UiShow(u, false);
}

// [V] Role_StopMoveAndIdle @0x484790 (the wait skip): Anim_StopSample @0x47b030 (the role's last voice) + Anim_ReleaseSprite @0x47b240 (its effect article and
// the effect's sample). The info / speech box itself is hidden by the 0x1C message that follows the wait.
void rm8PresentSkipSeat(int seat)
{
	if(!validSeat(seat)) return;
	SeatState &st = seatState(seat);
	playVoice(seat, 0);
	if(st.fx){ rm8FxStop(st.fx); st.fx = 0; }
	// [V] Anim_ReleaseSprite @0x47b240 frees the role's article handle (+0x54): the effect AND the game-start debut model (Role_SpawnCharacterModel
	// @0x48b410 keeps its handle there too); here both are Fx entries of the seat
	for(size_t i = 0; i < g_fx.size();){
		if(g_fx[i].seat == seat){ int id = g_fx[i].id; rm8FxStop(id); i = 0; } else i++;
	}
}

bool rm8PresentBusy(int seat){ return validSeat(seat) && (seatState(seat).bubble || (seatState(seat).voice && rm8SndPlaying(seatState(seat).voice))); }

void rm8RoleSay(int seat, int cls){ if(validSeat(seat)) rm8TurnEnqueue(seat, 0x5A, cls, 0); }
void rm8RoleSayAt(int seat, int pos, int cls){ if(validSeat(seat)) rm8TurnEnqueueAt(seat, pos, 0x5A, cls, 0); }
bool rm8PresentChance70(){ return rm8GameRand() % 100 < 0x46; }	// [V] FUN_005c5802() % 100 < 0x46

// ======================================================================== the message handlers
namespace {

// [V] CPlayerTag__OnNpcInteract @0x490f40 (message 0x5A, a = class, b = pass): pass 0 focuses the camera and re-queues itself with b = 1;
// pass 1 shows string strId + a with its portrait, plays the voice, waits 180 frames, hides, and starts the animation
void speakMsg(int seat, int cls, int pass)
{
	int st = rm8TurnStatus(seat);
	if(cls < 0 || st == 6 || st == 1 || st == 7) return;
	if(pass == 0){
		float p[3]; if(rm8RolesGetPos(seat, p)) rm8TurnFocus(p);
		rm8TurnEnqueueAt(seat, 0, 0x5A, cls, 1);
		return;
	}
	Rm8SpeechLine l;
	if(!rm8SpeechParse(strIdOf(seat) + cls, l)) return;
	// [V] 0x490f40: text, voice (+32000) and "@N" come from strId + class, but the "$N" animation / expression number is read from index
	// (strId + class - 600) % 200 + 600, i.e. ALWAYS from the first role's block (601..720): all roles share one expression per class.
	// Picture = npc\<picName>_%02d with ($N - 3), capped at 9; a class without "$N" gives index -4 = a missing texture (blank picture).
	Rm8SpeechLine ex;
	l.anim = rm8SpeechParse(600 + cls % 200, ex) ? ex.anim : 0;
	if(getenv("RM8_DEBUG_PRESENT")) rm8Log("present: seat %d class %d: %s (voice %d, anim %d)", seat, cls, l.text.c_str(), l.voice, l.anim);
	int portrait = l.anim > 0 ? std::max(0, std::min(l.anim - 3, 9)) : 0;
	showInfo(seat, l.text, portrait);
	if(l.voice > 0) playVoice(seat, l.voice);			// [V] Role_ShowInfoDialog: Anim_PlaySound(+0x204)
	rm8TurnEnqueueAt(seat, 0, 0x65, 180, 0);			// [V] Role_GetWaitFrames(1) = 180
	rm8TurnEnqueueAt(seat, 1, 0x1C, 0, 0);
	if(st == 0 && l.anim > 0) rm8RoleExpression(seat, l.anim);	// [V] status 0: vf5(anim, 0, 0)
}

// [V] Role_ShowEffectAnimAtTerra @0x48b320: sounds by effect art
int terraEffectSound(int art){ switch(art){ case 0x177a: return 0x68; case 0x178a: return 0x65; case 0x178b: return 0x66; case 0x178c: return 0x67; } return -1; }

}	// namespace

static void introModel(int seat);
bool rm8PresentMessage(int seat, int cmd, int a, int b)
{
	if(!validSeat(seat)) return false;
	switch(cmd){
	case 0x5A: speakMsg(seat, a, b); return true;
	case 0x2C: {	// [V] Role_ShowEffectAnimAtSelf @0x48b2b0 (Role.PlayPrt(role, art, sound)): focus, effect on the role, wait 60 frames
		float p[3]; if(rm8RolesGetPos(seat, p)) rm8TurnFocus(p);
		seatState(seat).fx = rm8FxPlayAtRole(seat, a, b);
		rm8TurnEnqueueAt(seat, 0, 0x65, 60, 0);
		return true; }
	case 0x52: {	// [V] Role_ShowEffectAnimAtTerra @0x48b320 (art, terra): [A] the terra is the port's plot index; b <= -2: the object is the event tile of road node -2 - b (the park of the story, story.cpp)
		if(g_s && b <= -2){
			float p[3];
			if(rm8TurnNodePos(-2 - b, p)){
				rm8TurnFocus(p);
				seatState(seat).fx = rm8FxPlayAtPoint(p, a, terraEffectSound(a));
				rm8TurnEnqueueAt(seat, 0, 0x65, 60, 0);
			}
		}else if(g_s && b >= 0 && b < (int)g_s->terras.size()){
			rm8TurnFocus(g_s->terras[b].pos);
			seatState(seat).fx = rm8FxPlayAtTerra(b, a, terraEffectSound(a));
			rm8TurnEnqueueAt(seat, 0, 0x65, 60, 0);
		}
		return true; }
	case 0x53:	// [V] Role_MsgHandler_PlayAnimOrWait @0x48e8c0: b < 0 only the sound, else the sound (replacing the role's last one) and a wait of b frames
		if(b < 0) rm8SndPlay(a);
		else { playVoice(seat, a); rm8TurnEnqueueAt(seat, 0, 0x65, b, 0); }
		return true;
	case 0x57: introModel(seat); return true;	// [V] vf36 Role_SpawnCharacterModel @0x48b410
	case 0x7B: g_hidden[seat & 7] = true; return false;	// [V] Role_ClearFlag1C8 (the role is not drawn); the story module consumes the message
	case 0x7C: g_hidden[seat & 7] = false; return false;	// [V] Role_SetFlag1C8
	case 0x54: speechPortraits(a, b); return true;	// [V] Ui_SetSpeechPortraits @0x48cb70 (role Ids)
	case 0x55: case 0x56:	// [V] Ui_ShowSpeechLeft @0x48ccb0 / Right @0x48cdb0 (a = text id); then wait 180 frames
		speechSay(cmd == 0x55, rm8GameText(a));
		rm8TurnEnqueueAt(seat, 0, 0x65, 180, 0);
		return true;
	case 0x2E: {	// [V] u_Role_StartInteractWithRole @0x48d040 (a = other seat + 1): the other role (idle, state 0) starts working its queue
			// (Role_TryEnterState8 @0x48d0e0) and this one pauses (state 0) until the other sends 0x2E back
		int other = a - 1;
		if(a != 0 && other >= 0 && other != seat && validSeat(other) && rm8TurnState(other) == 0){
			rm8TurnSetState(other, 8);
			rm8TurnSetState(seat, 0);
		}
		return true; }
	// observers: the money / ticket functions of the original roll the emote themselves; the port's handlers do the arithmetic
	case 0x16: rm8PresentMoney(seat, a, false); return false;	// [V] Role_SubMoney(a, acct, 1) @0x489ac0
	case 0x17: rm8PresentMoney(seat, a, true); return false;	// [V] Role_AddMoney(a, acct, 1) @0x489b80
	case 7:    if(a > 10 && b == 0 && rm8PresentChance70()){ rm8TurnEnqueueAt(seat, 0, 0x65, 30, 0); rm8RoleSayAt(seat, 1, 2); } return false;	// [V] Role_AddTicket @0x489ed0: class (30 < 50) + 1 = 2 after a 30 frame wait
	case 8:    if(a > 9 && rm8PresentChance70()) rm8RoleSayAt(seat, 0, a < 50 ? 5 : 4); return false;	// [V] Role_SubTicket @0x489f90
	case 0x4B: if(b != 0 && rm8PresentChance70()) rm8RoleSayAt(seat, 0, a < 50 ? 5 : 4); return false;	// [V] Role_SubTicketEmoteAny @0x48a030
	}
	return false;
}

// ======================================================================== triggers
// [V] Role_SubMoney(.., emote) @0x489ac0: 70 %: class by amount (PI * 5000 -> 0xB, PI * 1000 -> 0xA, else 0xC) after a 30 frame wait;
// Role_AddMoney(.., emote) @0x489b80: 70 %: >= 5000 -> 7, >= 1000 -> 8, else 9
void rm8PresentMoney(int seat, int32_t amount, bool gain)	// [N] int32_t
{
	if(!rm8PresentChance70()) return;
	if(gain) rm8RoleSayAt(seat, 0, amount >= 5000 ? 7 : amount >= 1000 ? 8 : 9);
	else{
		int32_t pi = rm8GamePI();	// [N] int32_t
		int cls = amount >= pi * 5000 ? 0xB : amount >= pi * 1000 ? 0xA : 0xC;
		rm8TurnEnqueueAt(seat, 0, 0x65, 30, 0); rm8RoleSayAt(seat, 1, cls);
	}
}

// [V] Role_TransferRentToOwner @0x487f20 (no frame-up card; sample 0x7B is boardfx.cpp's): the payer's Role_SubMoney emote with 60 %, the owner's Role_AddMoney
// emote with the other 40 %; when the owner emotes the payer hands control to the owner (0x2E) and the owner hands it back
void rm8PresentRent(int payer, int owner, int32_t rent)	// [N] int32_t
{
	bool pe = rm8GameRand() % 100 < 0x3c;
	if(pe) rm8PresentMoney(payer, rent, false);
	if(!validSeat(owner)) return;
	bool oe = !pe;
	if(oe){
		rm8PresentMoney(owner, rent, true);
		rm8TurnEnqueueAt(payer, 0, 0x2E, owner + 1, 0);
		rm8TurnEnqueue(owner, 0x2E, payer + 1, 0);
	}
}

// [V] Role_CheckRentExempt @0x486580: owner in a special status -> 70 % emote 0xD / 0xE / 0xF by the rent
void rm8PresentRentExempt(int seat, int32_t rent)	// [N] int32_t
{
	if(!rm8PresentChance70()) return;
	int32_t pi = rm8GamePI();	// [N] int32_t
	rm8RoleSay(seat, rent >= pi * 5000 ? 0xF : rent >= pi * 1000 ? 0xE : 0xD);
}

static int streetCount(int street, int owner)	// TerraMgr_CountTerraByStreetOwner
{
	int n = 0;
	for(int t = 0; t < rm8GameTerraCount(); t++) if(rm8GameTerraStreet(t) == street && rm8GameTerraOwner(t) == owner) n++;
	return n;
}

// [V] Role_BuyLandConfirmed @0x487030 (price already charged, owner already set; no god possession)
void rm8PresentBuy(int seat, int terra, int32_t price)	// [N] int32_t
{
	int cls = -1;
	if(price < 1){ if(rm8PresentChance70()) cls = 1; }
	else if(streetCount(rm8GameTerraStreet(terra), seat) < 3){ if(rm8GameTerraLevel(terra) != 0 && rm8PresentChance70()) cls = 1; }
	else cls = 0x11;
	if(cls >= 0) rm8RoleSayAt(seat, 0, cls);
}

// [V] Role_BuildOnLand @0x487380 (cost already charged, level already raised to `level`): own land: 70 % class 1 / 2; level 5 -> class 0x10 and the
// firework effect 0x177A (message 0x52) first; else >= 3 plots in the street -> 0x12
void rm8PresentBuild(int seat, int terra, int level)
{
	if(rm8GameTerraOwner(terra) != seat) return;
	int cls = rm8PresentChance70() ? rm8GameRand() % 2 + 1 : -1, pos = 0;
	if(level == 5){ cls = 0x10; rm8TurnEnqueueAt(seat, 0, 0x52, 0x177A, terra); pos = 1; }
	else if(streetCount(rm8GameTerraStreet(terra), seat) > 2) cls = 0x12;
	if(cls >= 0) rm8RoleSayAt(seat, pos, cls);
}

// [V] Role_TakeLandOwnership(t, b != 0) @0x487250: 70 %: the previous owner is annoyed (class 4) and the two roles hand over (0x2E)
void rm8PresentLandTaken(int seat, int prevOwner)
{
	if(!rm8PresentChance70() || !validSeat(prevOwner) || prevOwner == seat) return;
	rm8RoleSayAt(prevOwner, 0, 4);
	rm8TurnEnqueueAt(prevOwner, 1, 0x2E, seat + 1, 0);
	rm8TurnEnqueueAt(seat, 0, 0x2E, prevOwner + 1, 0);
}

// [V] CPlayerTag::OnStopAtTerra @0x485c00 -> CPlayerTag__GetRandomBankruptRole(0, 0) (misnamed: a random normal-status role on the same node)
// -> Role_StartNpcDialogForRole @0x48c950 -> u_Role_QueueTagEntryMsgs @0x48ca30: the first sp entry of this role whose pId is the other's Id, once per
// game: 0x54(left Id, right Id), 0x55 / 0x56 per line (the speaker is the left one when rlId = this role's Id), 0x1C. Only in the story mode.
void rm8PresentOnStop(int seat)
{
	if(!g_story || !validSeat(seat)) return;
	std::vector<int> c;
	for(int k = 0; k < rm8TurnRoleCount(); k++)
		if(k != seat && rm8TurnStatus(k) == 0 && rm8TurnNode(k) == rm8TurnNode(seat)) c.push_back(k);
	if(c.empty()) return;
	int other = c[rm8GameRand() % c.size()];
	int me = rm8TurnCharId(seat), you = rm8TurnCharId(other);
	if(rm8StoryMeetSpecial(seat, other)) return;	// [V] Role_StartNpcDialogForRole @0x48c950: 錢夫人 meets <q#1> on map 5 (story.cpp)
	auto it = g_sp.find(me);
	if(it == g_sp.end()) return;
	SpEntry *e = nullptr;
	for(SpEntry &x : it->second) if(x.pId == you){ e = &x; break; }	// [V] only the first match is considered
	if(!e || e->used) return;
	e->used = true;
	rm8TurnEnqueue(seat, 0x54, me, you);
	for(const SpLine &l : e->lines) rm8TurnEnqueue(seat, l.rl == me ? 0x55 : 0x56, rm8GamePushText(l.text), -1);
	rm8TurnEnqueue(seat, 0x1C, 0, 0);
}

void rm8PresentSetStory(bool on){ g_story = on; }

// ======================================================================== per frame
// test hook: RM8_PRESENT_TEST="<seconds>:<kind>:<args>,..." e.g. "1:say:0:7,1:bubble:1:601,2:fx:6002,1:expr:2:5" (say seat class, bubble seat strId, expr seat id, fx art)
static void testHooks(float dt)
{
	static const char *env = getenv("RM8_PRESENT_TEST");
	if(!env) return;
	static float t = 0; static std::vector<std::string> done;
	t += dt;
	std::istringstream ss(env); std::string item;
	while(std::getline(ss, item, ',')){
		float at = (float)atof(item.c_str());
		if(t < at || std::find(done.begin(), done.end(), item) != done.end()) continue;
		done.push_back(item);
		char kind[16] = {0}; int x = 0, y = 0;
		const char *p = strchr(item.c_str(), ':'); if(!p) continue;
		if(sscanf(p + 1, "%15[^:]:%d:%d", kind, &x, &y) < 2) continue;
		float pos[3] = {0, 0, 0};
		if(!strcmp(kind, "say")) rm8RoleSay(x, y);
		else if(!strcmp(kind, "bubble")) rm8RoleSpeakStr(0, x, RM8SPEAK_BUBBLE, 6);
		else if(!strcmp(kind, "bubbleat")) rm8RoleSpeakStr(x, y, RM8SPEAK_BUBBLE, 6);
		else if(!strcmp(kind, "expr")) rm8RoleExpression(x, y);
		else if(!strcmp(kind, "fxrole")){ rm8FxPlayAtRole(0, x, y); }
		else if(!strcmp(kind, "fx")){ rm8RolesGetPos(y, pos); pos[1] += 30; rm8FxPlay(x, pos, camYaw() - 180.0f, 0, true, 0, 1.0f, 0, false); }
		else if(!strcmp(kind, "talk")) rm8PresentMessage(x, 0x5A, y, 1);	// the dialog box now (second pass), whatever the seat state
		else if(!strcmp(kind, "cur")) rm8TurnSetState(x, 8);
		rm8Log("present test: %s", item.c_str());
	}
}

void rm8PresentUpdate(float dt)
{
	testHooks(dt);
	for(int s = 0; s < (int)g_seat.size(); s++){
		SeatState &st = g_seat[s];
		if(st.exprT > 0){ st.exprT -= dt; if(st.exprT <= 0){ st.exprT = 0; rm8TurnPlayAnim(s, 2, false); } }
		if(st.bubble && st.bubbleT > 0){ st.bubbleT -= dt; if(st.bubbleT <= 0) hideBubble(s); }
		if(st.voice && !rm8SndPlaying(st.voice)) st.voice = 0;
	}
	if(g_infoOn && g_infoT > 0){ g_infoT -= dt; if(g_infoT <= 0){ g_infoT = 0; hideInfo(); rm8HudCloseModal(); } }
	for(size_t i = 0; i < g_fx.size();){
		Fx &f = g_fx[i];
		f.age += dt;
		fxTimedEvents(f);
		if(!f.done && ((f.life > 0 && f.age >= f.life) || (f.once && animEnded(f)))) f.done = true;
		if(f.done && f.autoRemove){ fxFree(f); g_fx.erase(g_fx.begin() + i); continue; }
		i++;
	}
}

static void fxRenderOverlay(Camera *cam);
void rm8PresentRenderLate(Camera *cam)
{
	g_cam = cam;
	if(!cam || !cam->frameBuffer) return;
	fxRenderOverlay(cam);
	Rm8Ui *u = nullptr;
	float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height;
	for(int s = 0; s < (int)g_seat.size(); s++){
		SeatState &st = g_seat[s];
		if(!st.bubble || !st.plane) continue;
		if(!u) u = uiBubble();
		float p[3], sx, sy;
		bool ok = rm8RolesGetPos(s, p);
		p[1] += HEAD_Y;
		if(ok && project(p, sx, sy)){
			// HUD layout: x design units scale with the width, y and sizes with the height (ui.cpp); the bubble sits above the head, centred
			float k = H / 768.0f, bx = sx - 128.0f * k, by = sy - 128.0f * k;
			bx = std::max(0.0f, std::min(bx, W - 256.0f * k)); by = std::max(0.0f, by);
			rm8UiSetRect(u, st.plane, bx * 1024.0f / W, by / k, 256.0f, 128.0f);
			rm8UiSetVisible(u, st.plane, true);
		}else rm8UiSetVisible(u, st.plane, false);
	}
}

// ======================================================================== effect player
namespace {
struct FxRole { int seat = -1, charId = 0, outfit = 0; };
void skinDebut(Clump *c, int charId, int outfit);
int fxPlayImpl(int art, const float pos[3], float yawDeg, int anim, bool once, int sound, const float sc[3], float life, bool autoRemove, const FxRole &role);
}
int rm8FxPlay(int art, const float pos[3], float yawDeg, int anim, bool once, int sound, float scale, float life, bool autoRemove)
{
	float sc[3] = {scale, scale, scale};
	return fxPlayImpl(art, pos, yawDeg, anim, once, sound, sc, life, autoRemove, FxRole());
}
namespace {
int fxPlayImpl(int art, const float pos[3], float yawDeg, int anim, bool once, int sound, const float sc[3], float life, bool autoRemove, const FxRole &role)
{
	auto it = g_fxart.find(art);
	if(it == g_fxart.end() || !g_s){ rm8Log("fx: art %d unknown", art); return 0; }
	const FxArt &d = it->second;
	Fx f; f.id = g_fxNext; f.art = art; f.once = once; f.autoRemove = autoRemove; f.anim = anim;
	if(d.dff == "-" || d.dff.empty()){	// a particle art: the emitter at the position
		if(d.prt >= 0){ int e = rm8PrtPlaceH(d.prt, pos); if(e >= 0) f.prt.push_back(e); }
		if(f.prt.empty()) return 0;
	}else{
		auto an = d.anims.find(anim);
		std::string anm = anim >= 0 && an != d.anims.end() ? an->second : "-";
		f.clump = rm8SceneAddArticle(*g_s, d.dff, d.texdir, pos, sc, yawDeg, anm, d.uva.empty() ? "-" : d.uva, once ? "once" : "loop");
		if(!f.clump) return 0;
		f.hasAnim = anm != "-";
		for(auto &h : d.hooks){ int e = rm8PrtAttachH(h.second, f.clump, h.first); if(e >= 0) f.prt.push_back(e); }
		if(role.charId > 0) skinDebut(f.clump, role.charId, role.outfit);
	}
	f.seat = role.seat;
	f.life = life;
	if(f.life <= 0 && autoRemove) f.life = std::max(1e-3f, (f.hasAnim ? 10.0f : 4.0f) * waitScale());	// fast mode (wait scale 0): ends at once; a life of 0 meant "forever" here and the god's cloud never ended (state 10 for good)	// [V] Anim_CreateSprite @0x47b090: 10 s with the animation callback, else 4 s, x wait scale
	if(sound > 0) f.snd = rm8SndPlay(sound);
	g_fxNext++;
	g_fx.push_back(f);
	return f.id;
}
}	// namespace

// [V] the UFO of the news (Cls_5e3af0 family, Mesh_LoadAndInit @0x47a2c0): not an article of the scenery, drawn by the scene task after RwCameraClear(z) (vf2 @0x4B1420), i.e. over the
// buildings and roles. The article keeps its animation; present.cpp draws it in rm8PresentRenderLate.
void rm8FxSetOverlay(int h)
{
	Fx *f = fxFind(h);
	if(!f || !f->clump || !g_s || f->overlay) return;
	f->overlay = true;
	rm8SceneDetachArticle(*g_s, f->clump);
}
static void fxRenderOverlay(Camera *cam)
{
	bool any = false;
	for(const Fx &f : g_fx) any |= f.overlay && f.clump;
	if(!any) return;
	RGBA c = {0, 0, 0, 0};
	cam->clear(&c, Camera::CLEARZ);
	for(const Fx &f : g_fx) if(f.overlay && f.clump) rm8RenderArticleClump(f.clump);
}

bool rm8FxFinished(int h){ Fx *f = fxFind(h); return !f || f->done; }
bool rm8FxActive(int h){ return fxFind(h) != nullptr; }

void rm8FxStop(int h)
{
	for(size_t i = 0; i < g_fx.size(); i++) if(g_fx[i].id == h){ fxFree(g_fx[i]); g_fx.erase(g_fx.begin() + i); return; }
}

void rm8FxMove(int h, const float pos[3])
{
	Fx *f = fxFind(h);
	if(!f || !f->clump) return;
	Frame *fr = f->clump->getFrame();
	V3d d = {pos[0] - fr->matrix.pos.x, pos[1] - fr->matrix.pos.y, pos[2] - fr->matrix.pos.z};
	fr->translate(&d, COMBINEPOSTCONCAT);
}

void rm8FxStopAll(){ for(Fx &f : g_fx) fxFree(f); g_fx.clear(); }

static int fxAtPoint(const float p[3], int art, int sound, int seat = -1)
{
	// [V] Anim_CreateSprite @0x47b090: position + (0, 30, 0), pulled 20 units toward the camera; yaw = camera yaw - 180; once, removed when it ends
	float fw[3]; camForward(fw);
	float q[3] = {p[0] - fw[0] * 20.0f, p[1] + 30.0f, p[2] - fw[2] * 20.0f};
	FxRole role; role.seat = seat;
	const float one[3] = {1, 1, 1};
	return fxPlayImpl(art, q, camYaw() - 180.0f, 0, true, sound, one, 0, true, role);
}

int rm8FxPlayAtRole(int seat, int art, int sound)
{
	float p[3];
	if(!rm8RolesGetPos(seat, p)) return 0;
	return fxAtPoint(p, art, sound, seat);
}

int rm8FxPlayAtPoint(const float p[3], int art, int sound){ return fxAtPoint(p, art, sound); }
int rm8FxPlayAtTerra(int terra, int art, int sound)
{
	if(!g_s || terra < 0 || terra >= (int)g_s->terras.size()) return 0;
	return fxAtPoint(g_s->terras[terra].pos, art, sound);
}

// ======================================================================== game-start introduction
// [V] Role_LeaveStatus8 @0x483540 (status 8 = placed by Game_SelectRandomTerritory @0x496180): Role_ClearFlag1C8 hides the doll, 0x57 =
// vf36 Role_SpawnCharacterModel @0x48b410: the debut model (art 0x180 + Id, "A06dc": role.bin animation 0 played once) at the role's node +
// roleinfo offset, roleinfo scale, yaw = camera yaw - 180, body texture of the outfit; 0x65(60) at the queue front; the wait ends only when
// the model is gone (its callback Role_FindWithCondition releases it at the end of the animation: Role_TickWaitTimer @0x4850b0 needs +0x54 < 0)
namespace {
std::map<std::string, Texture*> g_skinTex;
Texture *skinTexture(const std::string &key)
{
	auto it = g_skinTex.find(key);
	if(it != g_skinTex.end()) return it->second;
	Texture *t = rm8PackTexture(key);
	if(!t) rm8Log("warning: debut texture %s not found", key.c_str());
	return g_skinTex[key] = t;
}
// [V] Asset_LoadCharacterTexture @0x4934a0 as in roles.cpp loadModel (front = 1)
void skinDebut(Clump *c, int charId, int outfit)
{
	const char *extra = (charId == 2 || charId == 15) ? "other" : (charId == 4 || charId == 9 || charId == 11 || charId == 13) ? "hair" :
	                    (charId == 7 || charId == 10) ? "toujin" : nullptr;
	FORLIST(lnk, c->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		for(int i = 0; i < g->matList.numMaterials; i++){
			Material *mat = g->matList.materials[i];
			int k = UserDataArray::materialFindIndex(mat, "name");
			if(k < 0) continue;
			UserDataArray *ua = UserDataArray::materialGet(mat, k);
			if(!ua || ua->datatype != USERDATASTRING || ua->numElements < 1 || !ua->getString(0)) continue;
			std::string part = ua->getString(0), what = part == "body" ? "body" : (extra && part == extra ? part : "");
			if(what.empty()) continue;
			char key[160];
			snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_%s_f_%02d.tga", charId, charId, what.c_str(), outfit);
			if(Texture *t = skinTexture(key)) mat->setTexture(t);
		}
	}
}
}	// namespace

void rm8PresentIntroBegin(int seat){ if(seat >= 0 && seat < 8) g_hidden[seat] = true; }
bool rm8PresentRoleHidden(int seat){ return seat >= 0 && seat < 8 && g_hidden[seat]; }
bool rm8PresentSeatFxBusy(int seat)
{
	for(const Fx &f : g_fx) if(f.seat == seat) return true;
	// [V] Role_TickWaitTimer @0x4850b0 also needs +0x50 == 0: the role's voice sample (Anim_PlaySound @0x47aff0) has ended (docs/skip.md section 2.1)
	return validSeat(seat) && seat < (int)g_seat.size() && g_seat[seat].voice && rm8SndPlaying(g_seat[seat].voice);
}

static void introModel(int seat)
{
	float pos[3], off[3], sc[3]; int outfit = 0;
	if(!rm8TurnRoleLook(seat, pos, off, sc, outfit)) return;
	int id = rm8TurnCharId(seat);
	float p[3] = {pos[0] + off[0], pos[1] + off[1], pos[2] + off[2]};
	FxRole role; role.seat = seat; role.charId = id; role.outfit = outfit;
	int art = (outfit == 4 ? 400 : 384) + id;	// [V] Role_SpawnCharacterModel @0x48b410: outfit 4 -> art 400 + Id (A%02dxdc); textures are loaded only for outfit < 4
	if(outfit == 4) role.charId = 0;
	rm8TurnFollowerDebut(seat);	// [V] Cls_5e27d8__vf36 @0x47e3b0: Role_SpawnCharacterModel, then the companion's Path_SetWaypoint (伊格魯)
	int h = fxPlayImpl(art, p, camYaw() - 180.0f, 0, true, 0, sc, 0, true, role);
	if(getenv("RM8_DEBUG_PRESENT")) rm8Log("present: seat %d debut model art %d -> handle %d", seat, art, h);
	rm8TurnEnqueueAt(seat, 0, 0x65, 60, 0);	// [V] Role_EnqueueMsgAt(0, 0x65, Role_GetWaitFrames(2))
}

// save / load hooks of this module (save.h, docs/save_port.md)
#include "save_present.inc"

int rm8PresentStrId(int seat){ return strIdOf(seat); }	// the role's string.bin block (roleinfo +0x38c)
