// The monthly balance, after docs/balance.md. [V] = read from the decompile (address given), [H] hypothesis, [A] port adaptation.
//
// Cls_5e49c8 @0x4af2e0 (loader), @0x4b0870 (Update, a step machine in +0x20 with the timer in +0x2c), @0x4b0b00 (render, AFTER the UI: the 3D part is drawn over balance.ui),
// @0x4b0d90 (vf13 = the click skip: voice + sample stopped, timer 0). The 3D part is the anchor (art 1050 = 24b) at (120, -100, 600) scale 1.5 and one doll per role that is not
// bankrupt at (60 - 60 i, -100, 600) (+ (0, 61, 0)) scale 0.5, all turned 180 degrees about Y, seen by a camera at the origin looking along +z with the 0.3 x 0.225 view window
// (the columns of balance.ui are 170 px apart = 60 units at that distance: the layout is consistent).
#include "balance.h"
#include "event_int.h"
#include "events.h"
#include "audio.h"
#include "present.h"
#include "stock.h"
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

bool rm8BalanceForced(){ static const bool f = getenv("RM8_BALANCE_EVERY") != nullptr; return f; }

namespace bal {

namespace {

struct FaceKey { float t; std::string part, tex; };
struct ArtDef { std::string dff, texdir; std::map<int, std::string> anims; std::map<int, std::vector<FaceKey>> faces; };
std::map<int, ArtDef> g_art;
std::map<int, int> g_roleArt;	// roleinfo Id -> the first model (role.bin art id 1, +0x308)
bool dbg(){ static const bool d = getenv("RM8_DEBUG_BALANCE") != nullptr; return d; }

// ======================================================================== models
struct Mdl {
	Clump *clump = nullptr;
	HAnimHierarchy *hier = nullptr;
	Matrix base;
	std::map<int, Animation*> anims;
	std::map<std::string, std::vector<Material*>> parts;	// material user data "name"
	bool once = false;
	int art = 0, anim = -1;
	std::string face;
};

void setPart(Mdl &m, const std::string &part, Texture *t)
{
	auto it = m.parts.find(part);
	if(!t || it == m.parts.end()) return;
	for(Material *k : it->second) k->setTexture(t);
}
void applyFace(Mdl &m, float t)
{
	auto a = g_art.find(m.art);
	if(a == g_art.end()) return;
	auto f = a->second.faces.find(m.anim);
	if(f == a->second.faces.end()) return;
	const FaceKey *k = nullptr;
	for(const FaceKey &x : f->second) if(x.t <= t + 1e-4f && (!k || x.t >= k->t)) k = &x;
	if(!k || k->tex == m.face) return;
	if(dbg() && m.art == 1050) rm8Log("balance: anchor act %d t %.2f face %s", m.anim, t, k->tex.c_str());
	setPart(m, k->part, rm8PackTexture(k->tex));
	m.face = k->tex;
}
void stepAnim(Mdl &m, float dt)
{
	if(!m.hier || !m.hier->interpolator->currentAnim) return;
	AnimInterpolator *ip = m.hier->interpolator;
	float step = dt;
	if(m.once) step = std::min(step, std::max(0.0f, ip->currentAnim->duration - ip->currentTime - 1e-4f));
	ip->addTime(step);
	for(int i = 0; i < m.hier->numNodes; i++){
		Frame *f = m.hier->nodeInfo[i].frame;
		if(!f) continue;
		ip->applyCB(&f->matrix, ip->getInterpFrame(i));
		f->updateObjects();
	}
	m.hier->updateMatrices();
	applyFace(m, ip->currentTime);
}
bool load(Mdl &m, int art)
{
	auto it = g_art.find(art);
	if(it == g_art.end()){ rm8Log("balance: no model for art %d", art); return false; }
	m.clump = rm8LoadClump(it->second.dff, it->second.texdir);
	if(!m.clump){ rm8Log("balance: cannot load %s", it->second.dff.c_str()); return false; }
	m.art = art;
	Frame *f = m.clump->getFrame();
	m.base = f->matrix; m.base.flags = 0;
	m.hier = HAnimHierarchy::find(f);
	if(m.hier) m.hier->attach();
	for(auto &a : it->second.anims){
		Animation *an = rm8LoadAnim(a.second);
		if(an && m.hier && an->getNumNodes() == m.hier->numNodes) m.anims[a.first] = an;
	}
	FORLIST(lnk, m.clump->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		for(int i = 0; i < g->matList.numMaterials; i++){
			Material *mat = g->matList.materials[i];
			int k = UserDataArray::materialFindIndex(mat, "name");
			if(k < 0) continue;
			UserDataArray *ua = UserDataArray::materialGet(mat, k);
			if(ua && ua->datatype == USERDATASTRING && ua->numElements > 0 && ua->getString(0)) m.parts[ua->getString(0)].push_back(mat);
		}
	}
	return true;
}
// [V] Mesh_SetTexture_0047a570(act, loops, 0) = Article_SelectByTypeAndSetProperties: loops 1 = once, otherwise for ever
void play(Mdl &m, int act, int loops)
{
	if(!m.hier) return;
	auto it = m.anims.find(act);
	if(it == m.anims.end() || !it->second){ if(dbg()) rm8Log("balance: art %d has no animation %d", m.art, act); return; }
	m.once = loops == 1; m.anim = act; m.face.clear();
	m.hier->interpolator->setCurrentAnim(it->second);
	stepAnim(m, 0);
}
void freeMdl(Mdl &m){ if(m.clump) m.clump->destroy(); m = Mdl(); }
// body / extra textures of a doll: [V] Asset_LoadCharacterTexture @0x4934a0 (front view "f", costume 0)
// [V] Balance_SetTextDisplay @0x4aedf0: Asset_LoadCharacterTexture(model, Id, outfit, 1) only for outfit 0..3 (the swimsuit model has its own textures)
void dress(Mdl &m, int charId, int outfit)
{
	if(outfit < 0 || outfit > 3) return;
	char key[160];
	snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_body_f_%02d.tga", charId, charId, outfit);
	setPart(m, "body", rm8PackTexture(key));
	const char *extra = (charId == 2 || charId == 15) ? "other" : (charId == 4 || charId == 9 || charId == 11 || charId == 13) ? "hair" : (charId == 7 || charId == 10) ? "toujin" : nullptr;
	if(extra){ snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_%s_f_%02d.tga", charId, charId, extra, outfit); setPart(m, extra, rm8PackTexture(key)); }
}
// model placed at p, scale s, turned 180 degrees about Y ([V] Mesh_Rotate_0047a790(0x43340000) + Mesh_SetTranslation + Mesh_SetScale)
void place(Mdl &m, float x, float y, float z, float s, bool turn = true)
{
	// [V] Mesh_SetScale @0x47a9e0 / Mesh_Rotate @0x47a790 act about the frame's own position (translate -pos, scale / rotate, translate +pos) and Mesh_SetTranslation moves the frame by
	// (target - tracked pos), the tracked pos starting at 0: frame position = the clump's own root position (base.pos: the pedestal's is (0, -197.7, 0), its geometry is centred on it) + target, unscaled
	Frame *f = m.clump->getFrame();
	Matrix b = m.base; V3d own = b.pos; b.pos = {0, 0, 0};
	f->transform(&b, COMBINEREPLACE);
	V3d sc = {s, s, s}, ay = {0, 1, 0}, t = {own.x + x, own.y + y, own.z + z};
	f->scale(&sc, COMBINEPOSTCONCAT);
	if(turn) f->rotate(&ay, 180.0f, COMBINEPOSTCONCAT);
	f->translate(&t, COMBINEPOSTCONCAT);
}

// debug: the model's bounding box in its local (base) space
void bbox(const char *tag, Mdl &m)
{
	if(!m.clump) return;
	Frame *f = m.clump->getFrame();
	f->transform(&m.base, COMBINEREPLACE);
	f->updateObjects();
	float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
	FORLIST(lnk, m.clump->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		Geometry *g = a->geometry;
		if(!g) continue;
		const Matrix *lt = a->getFrame() ? a->getFrame()->getLTM() : nullptr;
		float amn = 1e30f, amx = -1e30f;
		for(int i = 0; i < g->numVertices; i++){
			V3d p = g->morphTargets[0].vertices[i];
			if(lt) V3d::transformPoints(&p, &p, 1, lt);
			amn = std::min(amn, p.y); amx = std::max(amx, p.y);
			mn[0] = std::min(mn[0], p.x); mx[0] = std::max(mx[0], p.x); mn[1] = std::min(mn[1], p.y); mx[1] = std::max(mx[1], p.y); mn[2] = std::min(mn[2], p.z); mx[2] = std::max(mx[2], p.z);
		}
		if(dbg()) rm8Log("  atomic y %.1f..%.1f", amn, amx);
	}
	rm8Log("balance: bbox %s art %d: x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f; base pos %.1f %.1f %.1f right %.2f %.2f %.2f up %.2f %.2f %.2f", tag, m.art, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], m.base.pos.x, m.base.pos.y, m.base.pos.z, m.base.right.x, m.base.right.y, m.base.right.z, m.base.up.x, m.base.up.y, m.base.up.z);
}
// ======================================================================== the shared offscreen 3D view
Camera *g_cam = nullptr;
Texture *g_tex = nullptr;
int g_w = 0, g_h = 0;
struct BalanceTask; BalanceTask *g_cur = nullptr;	// the running balance task (rm8BalancePreRender / rm8BalanceActive)

// one role's doll (the original's Sprite3D, Cls_5e4384): the extra height moves toward the target at 300 units/s ([V] Sprite3D_MoveToward @0x4aefe0), then the doll bobs
// by +-(3..5) at 25 units/s ([V] Sprite3D_UpdateAnimation @0x4af1f0, Sprite3D_InitAnimation @0x4af140)
struct Doll {
	Mdl m, pillar; int seat = -1, charId = 0;	// m = the doll (this+8), pillar = the pedestal Mys121 (art 0x529, this+0x3c)
	float x = 0, cur = 0, target = 0, off = 0, amp = 3; int mode = 0;
	void initAnim(int md)
	{
		mode = md; amp = (float)(rm8GameRand() % 3 + 3);
		if(mode == 0) off = 0;
	}
	void setTarget(float v){ target = v; initAnim(0); }	// [V] u_Sprite3D_SetAnimationDuration @0x4af120
	void update(float dt)
	{
		float d = target - cur;
		if(d > 0.1f || d < -0.1f){
			float st = std::min(dt * 300.0f, std::fabs(d));
			cur += d < 0 ? -st : st;
		}else if(mode != 0){
			off += dt * 0.5f * 50.0f * (float)mode;
			if(mode < 1){ if(off < -amp) initAnim(1); }
			else if(amp < off) initAnim(-1);
		}
		stepAnim(pillar, dt);
		stepAnim(m, dt);
	}
	void placeAll()
	{
		place(m, x, -100.0f + cur + off, 600.0f, 1.0f);	// [V] the doll: Mesh_SetTranslation(pos), no scale call (1.0)
		place(pillar, x, -100.0f + 61.0f + cur + off, 600.0f, 0.5f, false);	// [V] the pedestal: pos + (0, 61, 0), scale 0.5 (Sprite3D_InitResourceAndTransform @0x4aecc0): its disc top ends at the doll's feet
	}
};

}	// namespace

}	// namespace bal

bool rm8BalanceLoadData(const std::string &dir)
{
	using namespace bal;
	{
		std::ifstream f(dir + "/event_arts.txt");	// "bart <art id> <dff key> <texture dir key> <index>:<anm key> ..."
		std::string line;
		while(std::getline(f, line)){
			std::istringstream ss(line); std::string kw, a; int id;
			if(!(ss >> kw >> id)) continue;
			if(kw == "face"){ int anim; FaceKey k; if(ss >> anim >> k.t >> k.part >> k.tex) g_art[id].faces[anim].push_back(k); continue; }	// [V] role.bin sub-list 3 of the anchor (art 1050: 34 head textures keyed to the action and the time: blink, mouth)
			if(kw != "bart") continue;
			ArtDef d; ss >> d.dff >> d.texdir;
			while(ss >> a){ size_t c = a.find(':'); if(c != std::string::npos) d.anims[atoi(a.c_str())] = a.substr(c + 1); }
			d.faces = g_art[id].faces;
			g_art[id] = d;
		}
	}
	{ ArtDef p; p.dff = "article\\element\\mys121.dff"; p.texdir = "article\\element\\"; p.anims[0] = "article\\element\\mys121.anm"; g_art[1321] = p; }	// [V] art 0x529 = Mys121, the pedestal of a doll (Sprite3D_LoadResource(0x529))
	std::ifstream f(dir + "/roles.txt");
	if(!f) return false;
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "role"){ int id; float v; ss >> id; for(int k = 0; k < 9; k++) ss >> v; int art; if(ss >> art) g_roleArt[id] = art; }
		else if(kw == "model"){ int a; ss >> a; if(!g_art.count(a)) ss >> g_art[a].dff >> g_art[a].texdir; }
		else if(kw == "anim"){ int a, i; std::string p; ss >> a >> i >> p; if(!g_art[a].anims.count(i)) g_art[a].anims[i] = p; }
		else if(kw == "face"){ int a, i; FaceKey k; ss >> a >> i >> k.t >> k.part >> k.tex; g_art[a].faces[i].push_back(k); }
	}
	return !g_roleArt.empty();
}

namespace bal {

namespace {

// ======================================================================== the task
struct BalanceTask : ev::Task {
	int step = 1;			// +0x20: the step run when the timer ends
	float timer = 0;		// +0x2c
	int voice = 0, sample = 0;	// +8 (Anim_PlaySound) / +0x48
	int tragic = 0, champion = 0;	// +0x28 / +0x24: the doll indices
	bool done = false, ok = false;
	Mdl anchor;
	std::vector<Doll*> dolls;
	Raster *fb = nullptr, *zb = nullptr;
	int texWidget = 0;
	std::string texName;
	bool pushed = false;
	int alignedW = 0, alignedH = 0;	// the light columns are re-centred on the table columns for this screen size

	static float scale(){ return ev::waitScale(); }
	void setTimer(float s){ timer = s * scale(); }

	BalanceTask(int seat_)
	{
		seat = seat_;
		ui = rm8UiLoad("balance", RM8UI_HUD);	// [V] LoadScaled (UI_ScaleWidgetTree): the whole-screen layout
		if(!ui) return;
		// [V] Cls_5e49c8__vf16_ShowBalanceUI @0x4af2e0: the main dialog hidden, the board music paused and track 102 ("Balance") started, a doll per role that is not bankrupt
		if(!load(anchor, 1050)){ ui = nullptr; return; }
		place(anchor, 120.0f, -100.0f, 600.0f, 1.5f);
		play(anchor, 0, 0);
		if(dbg()) bbox("anchor", anchor);
		{ Mdl pl; if(load(pl, 1321)){ bbox("pillar", pl); freeMdl(pl); } }
		int n = 0;
		for(int s = 0; s < rm8TurnRoleCount() && n < 4; s++){
			if(rm8TurnStatus(s) == 7) continue;
			auto r = g_roleArt.find(rm8TurnCharId(s));
			int outfit = rm8TurnOutfit(s), art = r != g_roleArt.end() ? r->second : 0;
			if(outfit == 4 && g_art.count(art + 192)) art += 192;	// [V] @0x4af2e0: outfit 4 -> artId + 0xC0
			else if(outfit == 4) outfit = 0;
			Doll *d = new Doll;
			if(r == g_roleArt.end() || !load(d->m, art)){ rm8Log("balance: create role failed (seat %d)", s); delete d; freeMdl(anchor); for(Doll *x : dolls){ freeMdl(x->m); freeMdl(x->pillar); delete x; } dolls.clear(); ui = nullptr; return; }
			d->seat = s; d->charId = rm8TurnCharId(s);
			d->x = 60.0f - 60.0f * n;
			dress(d->m, d->charId, outfit);
			if(dbg()) bbox("doll", d->m);
			play(d->m, 2, 0);	// [V] Sprite3D_InitResourceAndTransform @0x4aecc0: the doll (this+8) plays animation 2 (stand); the pedestal (this+0x3c) animation 0
			if(!load(d->pillar, 1321)){ rm8Log("balance: pedestal failed"); }
			play(d->pillar, 0, 0);
			place(d->m, d->x, -100.0f, 600.0f, 1.0f);
			dolls.push_back(d); n++;
		}
		rm8UiPushHidden();
		pushed = true;
		texWidget = rm8UiAddWidget(ui, "plane", 0, 0, 0, 1024, 768, -1, 0, 16, 9);	// the 3D layer: a screen-sized raster (the HUD layout stretches this widget over the whole screen), above everything [V] drawn after the UI
		texName = "dyn" + std::to_string(texWidget);
		rm8UiSetIgnore(ui, texWidget, true);
		for(int i = 0; i < 4; i++) rm8UiSetVisible(ui, 20 + i, false);
		rm8UiShow(ui, true);
		rm8MusicPlay(102, true);
		g_cur = this;
		ok = true;
		step = 1;
		addSimpleRow(4701, 0, 1);	// [V] the loader ends with Balance_AddSimpleRow(0x125d, 0, 1)
		if(getenv("RM8_BALANCE_HOLD") && atoi(getenv("RM8_BALANCE_HOLD")) == 0) timer = 1e9f;
		rm8Log("balance: the monthly balance starts for seat %d (%d dolls)", seat, (int)dolls.size());
	}
	~BalanceTask() override
	{
		if(g_cam && texWidget && ui) rm8UiSetTextureN(ui, texName.c_str(), nullptr);
		stopSounds();
		if(ui){ rm8UiShow(ui, false); }
		if(pushed) rm8UiPopHidden();
		for(Doll *d : dolls){ freeMdl(d->m); freeMdl(d->pillar); delete d; }
		freeMdl(anchor);
		if(g_cur == this) g_cur = nullptr;
		if(ok){ rm8MusicStop(); rm8MusicResumeBoard(); }	// [V] Balance_Cleanup @0x4af650: StopBGMWithFade + ManageBGMTransition(-5): the board music again
		if(texWidget && ui) rm8UiSetVisible(ui, texWidget, false);
	}
	void stopSounds(){ if(voice){ rm8SndStop(voice); voice = 0; } if(sample){ rm8SndStop(sample); sample = 0; } }

	// [V] Balance_SetTitle @0x4b0cc0: string `id` into the title label (widget ID 9) and its voice (the @ number) through Anim_PlaySound
	void setTitle(int id)
	{
		Rm8SpeechLine l;
		std::string t = ev::plain(rm8Str(id));
		rm8UiSetText(ui, 9, ev::wrap(t, 10.0f));
		if(voice){ rm8SndStop(voice); voice = 0; }
		if(rm8SpeechParse(id, l) && l.voice > 0) voice = rm8SndPlay(l.voice, 1);
		if(dbg()) rm8Log("balance: title %d: %s", id, t.c_str());
	}
	void setCol(int id, const std::string &t){ rm8UiSetText(ui, id, t); }
	void clearRows(){ for(int i = 1; i <= 8; i++) setCol(i, ""); }
	// [V] Balance_AddSimpleRow @0x4afe50 (title, anchor action, next step): the table cleared, the anchor plays `act` once; the timer 3.0
	void addSimpleRow(int title, int act, int next)
	{
		setTitle(title);
		setCol(10, ""); setCol(11, ""); clearRows();
		play(anchor, act, 1);
		step = next; setTimer(3.0f);
	}
	// [V] Balance_InitDisplay @0x4af870 (title, anchor action, next step, sample): sample (loops 1) replaces the old one, the title, the table cleared, the dolls that do not bob start to
	void initDisplay(int title, int act, int next, int snd)
	{
		if(snd >= 0){ if(sample){ rm8SndStop(sample); sample = 0; } sample = rm8SndPlay(snd, 1); }
		setTitle(title);
		play(anchor, act, 1);
		for(size_t i = 0; i < dolls.size(); i++){ setCol((int)i * 2 + 1, ""); setCol((int)i * 2 + 2, ""); if(dolls[i]->mode == 0) dolls[i]->initAnim(1); }
		setCol(10, ""); setCol(11, "");
		step = next; setTimer(3.0f);
	}
	void raise(const std::vector<long> &v, long mx)	// [V] u_Sprite3D_SetAnimationDuration(value * 60 / max)
	{
		for(size_t i = 0; i < dolls.size(); i++) dolls[i]->setTarget(mx > 0 ? (float)((double)v[i] * 60.0 / (double)mx) : 0.0f);
	}
	static std::string n9(long v){ char b[32]; snprintf(b, sizeof b, "%9d", (int)v); return b; }

	// [V] Balance_ShowDepositInfo @0x4afa40: "deposit:" / "interest:" (5501 / 5502); per doll the deposit and the interest = deposit / 10 (the money wonder: its owner / 5, the
	// others / 20: Wonder_CheckConstruction(0)), credited at once to the deposit (Role_AddMoney(interest, 1, 0)); the dolls rise by deposit * 60 / max
	void showDeposit()
	{
		setCol(10, ev::plain(rm8Str(5501))); setCol(11, ev::plain(rm8Str(5502)));
		std::vector<long> dep(dolls.size(), 0); long mx = 0;
		for(size_t i = 0; i < dolls.size(); i++){
			int s = dolls[i]->seat;
			dep[i] = rm8GameDeposit(s);
			long interest = dep[i] / rm8EventInterestDivisor(s);
			rm8GameAddMoney(s, interest, true);
			setCol((int)i * 2 + 1, n9(dep[i])); setCol((int)i * 2 + 2, n9(interest));
			rm8Log("balance: seat %d deposit %ld interest %ld", s, dep[i], interest);
			mx = std::max(mx, dep[i]);
		}
		raise(dep, mx);
		step = 3; setTimer(3.0f);
	}
	// [V] Balance_ShowLossInfo @0x4aff30: "loss:" (5503) = the rent paid (+0x33C), "unlucky days:" (5504) = +0x340 (sleep / hotel / hospital / prison / sleepwalk turn starts);
	// score = days * 2500 * price index + loss (>= 0); the first maximum is the tragic character
	void showLoss()
	{
		setCol(10, ev::plain(rm8Str(5503))); setCol(11, ev::plain(rm8Str(5504)));
		std::vector<long> sc(dolls.size(), 0); long mx = 0;
		for(size_t i = 0; i < dolls.size(); i++){
			int s = dolls[i]->seat;
			long loss = rm8GameRentPaid(s); int days = rm8GameBadDays(s);
			setCol((int)i * 2 + 1, n9(loss)); setCol((int)i * 2 + 2, n9(days));
			sc[i] = std::max(0L, (long)(int)((long)days * 2500 * rm8GamePI() + loss));
			if(mx < sc[i]){ mx = sc[i]; tragic = (int)i; }
		}
		raise(sc, mx);
		step = 6; setTimer(2.0f);
	}
	// [V] Balance_ShowTotalAssets @0x4b0330: "total assets:" (5505) and Role_CalculateNetCash(1) (cash + deposit - loan + stocks + the plots' prices with their buildings); the first maximum is the champion
	void showAssets()
	{
		setCol(10, ev::plain(rm8Str(5505))); setCol(11, "");
		std::vector<long> as(dolls.size(), 0); long mx = 0;
		for(size_t i = 0; i < dolls.size(); i++){
			int s = dolls[i]->seat;
			long v = rm8GameCash(s) + rm8GameDeposit(s) - rm8EventLoan(s) + rm8StockValue(s);
			for(int t = 0; t < rm8GameTerraCount(); t++) if(rm8GameTerraOwner(t) == s) v += rm8GameTerraPrice(t);
			as[i] = v;
			setCol((int)i * 2 + 1, n9(v)); setCol((int)i * 2 + 2, "");
			if(mx < v){ mx = v; champion = (int)i; }
		}
		raise(as, mx);
		step = 10; setTimer(2.0f);
	}
	// [V] Balance_ShowPlayerBalance @0x4b0670 (doll, kind, next): the doll plays 9 (kind 0, tragic) / 5 (kind 1, champion), the others 4 / 8; the title is the character's name line
	// (4704 + Id / 4722 + Id: name + voice); sample 0x82; the highlight frame (widget 20 + i) of that doll only
	void showPlayer(int idx, int kind, int next)
	{
		if(idx >= 0 && idx < (int)dolls.size()){
			for(size_t i = 0; i < dolls.size(); i++) play(dolls[i]->m, (int)i == idx ? (kind == 0 ? 9 : 5) : (kind == 0 ? 4 : 8), 0);
			setTitle((kind == 0 ? 4704 : 4722) + dolls[idx]->charId);
			if(sample){ rm8SndStop(sample); sample = 0; }
			sample = rm8SndPlay(0x82, 1);
			for(int i = 0; i < 4; i++) rm8UiSetVisible(ui, 20 + i, i == idx);
			rm8Log("balance: the %s is seat %d", kind == 0 ? "tragic character" : "champion", dolls[idx]->seat);
		}
		step = next; setTimer(3.0f);
	}
	void resetDisplay()	// [V] u_BalanceUI_InitializeDisplay @0x4b0df0: every doll animation 2, the frames hidden
	{
		for(Doll *d : dolls) play(d->m, 2, 0);
		for(int i = 0; i < 4; i++) rm8UiSetVisible(ui, 20 + i, false);
	}

	bool update(float dt) override
	{
		if(!ok) return true;
		for(Doll *d : dolls) d->update(dt);
		stepAnim(anchor, dt);
		timer -= dt;
		if(timer < 0) timer = 0;
		bool busy = timer > 0 || (voice && rm8SndPlaying(voice)) || (sample && rm8SndPlaying(sample));	// [V] Balance_UpdateTimer @0x4b0a70: waits for the timer, the voice and the sample
		if(!busy){
			if(voice && !rm8SndPlaying(voice)) voice = 0;
			if(sample && !rm8SndPlaying(sample)) sample = 0;
			int ex = step;
			if(dbg()) rm8Log("balance: step %d", step);
			switch(step){	// [V] Cls_5e49c8__vf1_UpdateBalanceDisplay @0x4b0870
			case 1: addSimpleRow(4702, 1, 2); break;
			case 2: showDeposit(); break;
			case 3: initDisplay(4703, 2, 4, -1); break;
			case 4: initDisplay(4704, 3, 5, 0x83); break;
			case 5: showLoss(); break;
			case 6: showPlayer(tragic, 0, 7); break;
			case 7: resetDisplay(); addSimpleRow(4721, 4, 8); break;
			case 8: initDisplay(4722, 5, 9, 0x83); break;
			case 9: showAssets(); break;
			case 10: showPlayer(champion, 1, 11); break;
			case 11: resetDisplay(); addSimpleRow(4739, 5, 12); break;
			case 12: rm8Log("balance: done"); return true;	// Balance_OnClose @0x4aff10
			}
			static const int hold = getenv("RM8_BALANCE_HOLD") ? atoi(getenv("RM8_BALANCE_HOLD")) : -1;	// test hook: stay on this step's display (0 = the opening)
			if(hold == ex) timer = 1e9f;
		}
		return false;
	}
	// [V] vf13 @0x4b0d90 (a click / tap): the voice and the sample stopped, the timer 0: the next step runs at once
	bool skip() override
	{
		stopSounds(); timer = 0;
		return true;
	}
};

}	// namespace

}	// namespace bal

ev::Task *ev::makeBalanceTask(int seat)
{
	bal::BalanceTask *t = new bal::BalanceTask(seat);
	if(!t->ok){ delete t; return nullptr; }
	return t;
}

// the 3D view: a 1024 x 1024 camera raster (the view window 0.3 x 0.3 at distance 600: 2.84 px per unit like the 4:3 original), cleared transparent, the anchor and the dolls drawn
void rm8BalancePreRender(Camera *main)
{
	using namespace bal;
	if(!g_cur) return;
	BalanceTask *t = g_cur;
	if(!t || !t->ok) return;
	float rx, ry, rw, rh;
	if(!rm8UiGetScreenRect(t->ui, t->texWidget, rx, ry, rw, rh) || rw < 16 || rh < 16) return;
	int W = (int)(rw + 0.5f), H = (int)(rh + 0.5f);
	if(!g_cam || g_w != W || g_h != H){	// the raster follows the screen (the layer covers it 1:1)
		if(g_cam){
			if(g_cam->world) g_cam->world->removeCamera(g_cam);
			rm8UiSetTextureN(t->ui, t->texName.c_str(), nullptr);
			if(g_tex){ g_tex->raster = nullptr; g_tex->destroy(); g_tex = nullptr; }
			Raster *fb = g_cam->frameBuffer, *zb = g_cam->zBuffer;
			g_cam->destroy(); g_cam = nullptr;
			if(fb) fb->destroy();
			if(zb) zb->destroy();
		}
		g_cam = Camera::create();
		g_cam->setFrame(Frame::create());
		g_cam->frameBuffer = Raster::create(W, H, 32, Raster::CAMERATEXTURE | Raster::C8888);
		g_cam->zBuffer = Raster::create(W, H, 0, Raster::ZBUFFER);
		g_tex = Texture::create(g_cam->frameBuffer);
		g_tex->setFilter(Texture::LINEAR);
		g_tex->setAddressU(Texture::CLAMP); g_tex->setAddressV(Texture::CLAMP);
		g_w = W; g_h = H;
		g_cam->setNearPlane(10.0f); g_cam->setFarPlane(3000.0f);
	}
	if(t->alignedW != W || t->alignedH != H){	// the light columns (widgets 20..23): centred over their table column (the HUD layout scales their x by W / 1024 but their width by H / 768, the labels' too: a wide screen shifts them apart)
		t->alignedW = W; t->alignedH = H;
		for(size_t i = 0; i < t->dolls.size() && i < 4; i++){
			float lx, ly, lw, lh, px, py, pw, ph, ux, uy, uw, uh;
			if(rm8UiGetScreenRect(t->ui, (int)i * 2 + 1, lx, ly, lw, lh) && rm8UiGetScreenRect(t->ui, 20 + (int)i, px, py, pw, ph) && rm8UiGetRect(t->ui, 20 + (int)i, ux, uy, uw, uh))
				rm8UiSetRect(t->ui, 20 + (int)i, (lx + lw * 0.5f - pw * 0.5f) / ((float)W / 1024.0f), uy, uw, uh);
		}
	}
	// [V] the game camera after Camera_SaveViewState @0x41b2e0 (frame = identity) keeps its perspective: view window 0.225 high (x 0.225 W / H on a wide screen, the board camera of the port)
	V2d vw = {0.225f * (float)W / (float)H, 0.225f};
	g_cam->setViewWindow(&vw);
	// the columns follow the table: each doll stands under its value labels (IDs 2i + 1), the anchor under the title label (ID 9); camera space x is mirrored (RenderWare: +x = screen left)
	float ppu = (float)H / 270.0f;	// pixels per unit at z = 600
	auto wx = [&](int id, float fallback){ float x, y, w, h; if(rm8UiGetScreenRect(t->ui, id, x, y, w, h)) return ((float)W * 0.5f - (x - rx + w * 0.5f)) / ppu; return fallback; };
	for(size_t i = 0; i < t->dolls.size(); i++){ t->dolls[i]->x = wx((int)i * 2 + 1, 60.0f - 60.0f * (float)i); t->dolls[i]->placeAll(); }
	place(t->anchor, wx(9, 120.0f), -100.0f, 600.0f, 1.5f);
	rm8UiSetTextureExactN(t->ui, t->texName.c_str(), g_tex);
	if(g_cam->world != main->world){ if(g_cam->world) g_cam->world->removeCamera(g_cam); main->world->addCamera(g_cam); }
	Matrix id; id.setIdentity();
	g_cam->getFrame()->transform(&id, COMBINEREPLACE);
	RGBA none = {0, 0, 0, 0};
	g_cam->clear(&none, Camera::CLEARIMAGE | Camera::CLEARZ);
	g_cam->beginUpdate();
	uint32 fn = GetRenderState(ALPHATESTFUNC), ref = GetRenderState(ALPHATESTREF);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196);	// [V] 0xC4
	for(Doll *d : t->dolls){ if(d->pillar.clump) d->pillar.clump->render(); if(d->m.clump) d->m.clump->render(); }	// [V] Cls_5e49c8__vf2 @0x4b0b00 draws the sprites first (each: the pedestal, then the doll: vf4 @0x4aefb0)...
	if(t->anchor.clump) t->anchor.clump->render();	// ... then the anchor
	SetRenderState(ALPHATESTFUNC, fn); SetRenderState(ALPHATESTREF, ref);
	g_cam->endUpdate();
}

bool rm8BalanceActive(){ return bal::g_cur != nullptr; }
