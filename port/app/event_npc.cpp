// 3D presenters of the event dialogs (bank clerk, card-shop machine, question host): after the task loaders
// CUi_bank__LoadUI @0x4c1590 / Cls_5e4a10__vf16__Init @0x4e5aa0 / Question_LoadResources @0x4bd1f0, which
//   CArticleDFF_Load(art id)           -> a role.bin article (scenes/event_arts.txt, scripts/export_event_arts.py)
//   Article_CalcClumpBoundingSphere    -> sphere at task +0xC (centre) / +0x18 (radius)
//   RwRasterCreate(0x100, 0x100, ..), OrthoCam_Create(0x100, ..)
// and every frame (u_UI_DrawArticleSprite_6446f0 @0x4c11b0, UI callback 0x13 of the "NPC" widget) render the article into the 256x256
// raster (UI_RenderArticleToRaster @0x4c4460: render state 0x1D = 5, alpha test ref 0xC4) and draw it into the widget rectangle.
// The animation is Article_SelectByTypeAndSetProperties(act, 1, 0): role.bin animation `act`, played once (count 1).
// [A] the original's OrthoCam parameters are not decoded: the port fits the bounding box of the model into the raster with a
//     perspective camera looking along the model's z axis; the raster is drawn stretched into the widget (1 : 1.2 compensated).
#include "event_int.h"
#include "balance.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>

using namespace rw;

namespace ev {

namespace {

struct ArtDef { std::string dff, texdir; std::map<int, std::string> anims; };
std::map<int, ArtDef> g_defs;
struct FaceKey { float t; std::string part, tex; };
std::map<int, std::map<int, std::vector<FaceKey>>> g_faces;	// art -> anim -> texture swaps (roles.txt "face" lines, role.bin sub-list 3)
std::map<int, int> g_roleArt;					// roleinfo Id -> artId1 first model (+0x308, the court's accuser / accused doll)

}	// namespace

struct Npc {
	Clump *clump = nullptr;
	HAnimHierarchy *hier = nullptr;
	Matrix base;
	std::map<int, Animation*> anims;
	bool once = true;
	int art = 0, anim = -1;
	std::map<std::string, std::vector<Material*>> parts;	// material user data "name" -> materials (paper-doll parts)
	std::string face;
	Camera *cam = nullptr;
	Texture *tex = nullptr;
	float cx = 0, cy = 0, cz = 0, r = 1, rscale = 1;	// bounding sphere centre (world) / radius (task +0xC / +0x18), radius factor of the draw call
	bool placed = false, dumped = false;
	bool flip = true;				// paper-doll presenters are drawn from their back (see preRender); false for plain 3D models (the auction's house)
	float spin = 0, angle = 0, pitch = 0;		// degrees per second / current yaw / fixed pitch (the rotating house, Vendue_RotateImage @0x4e4fe0)
	Rm8Ui *ui = nullptr; std::string widget;	// the dialog widget that shows the raster
	World *world = nullptr;
};

namespace {
std::vector<Npc*> g_npcs;
}

bool loadEventArts(const std::string &file)
{
	std::ifstream f(file);
	if(!f){ rm8Log("events: no %s (run scripts/export_event_arts.py)", file.c_str()); return false; }
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw, a; int id;
		if(!(ss >> kw >> id)) continue;
		if(kw == "face"){ int anim; FaceKey k; if(ss >> anim >> k.t >> k.part >> k.tex) g_faces[id][anim].push_back(k); continue; }	// [V] the open / closed mouth frames
		if(kw != "bart") continue;
		ArtDef d; ss >> d.dff >> d.texdir;
		while(ss >> a){ size_t c = a.find(':'); if(c != std::string::npos) d.anims[atoi(a.c_str())] = a.substr(c + 1); }
		g_defs[id] = d;
	}
	rm8Log("events: %d presenter models", (int)g_defs.size());
	return !g_defs.empty();
}

static void applyFace(Npc *n, float t);
static void applyAnim(Npc *n, float dt)
{
	if(!n->hier || !n->hier->interpolator->currentAnim) return;
	AnimInterpolator *ip = n->hier->interpolator;
	float step = dt;
	if(n->once) step = std::min(step, std::max(0.0f, ip->currentAnim->duration - ip->currentTime - 1e-4f));
	ip->addTime(step);
	for(int i = 0; i < n->hier->numNodes; i++){
		Frame *f = n->hier->nodeInfo[i].frame;
		if(!f) continue;
		ip->applyCB(&f->matrix, ip->getInterpFrame(i));
		f->updateObjects();
	}
	n->hier->updateMatrices();
	applyFace(n, ip->currentTime);
}

// roles.txt (roles.cpp): the character models for the dolls shown in the court / magic house ("role", "model", "anim", "face" lines)
bool loadEventRoles(const std::string &file)
{
	std::ifstream f(file);
	if(!f) return false;
	std::string line;
	while(std::getline(f, line)){
		std::istringstream ss(line); std::string kw;
		ss >> kw;
		if(kw == "role"){ int id; float v; ss >> id; for(int k = 0; k < 9; k++) ss >> v; int art; if(ss >> art) g_roleArt[id] = art; }
		else if(kw == "model"){ int a; ss >> a; if(!g_defs.count(a)) ss >> g_defs[a].dff >> g_defs[a].texdir; }
		else if(kw == "anim"){ int a, i; std::string p; ss >> a >> i >> p; if(!g_defs[a].anims.count(i)) g_defs[a].anims[i] = p; }
		else if(kw == "face"){ int a, i; FaceKey k; ss >> a >> i >> k.t >> k.part >> k.tex; g_faces[a][i].push_back(k); }
	}
	return !g_roleArt.empty();
}

static void setPart(Npc *n, const std::string &part, Texture *t)
{
	auto it = n->parts.find(part);
	if(!t || it == n->parts.end()) return;
	for(Material *m : it->second) m->setTexture(t);
}

static void applyFace(Npc *n, float t)
{
	auto fa = g_faces.find(n->art);
	if(fa == g_faces.end()) return;
	auto it = fa->second.find(n->anim);
	if(it == fa->second.end()) return;
	const FaceKey *k = nullptr;
	for(const FaceKey &f : it->second) if(f.t <= t + 1e-4f && (!k || f.t >= k->t)) k = &f;
	if(!k || k->tex == n->face) return;
	setPart(n, k->part, rm8PackTexture(k->tex));
	n->face = k->tex;
}

Npc *npcCreate(int art, Rm8Ui *ui, const char *widget, int charId, int outfit)
{
	if(charId > 0){
		auto r = g_roleArt.find(charId); if(r == g_roleArt.end()){ rm8Log("events: no doll model for character %d", charId); return nullptr; }
		art = r->second;
		if(outfit == 4 && g_defs.count(art + 192)) art += 192;	// [V] Court_LoadCharacterList @0x4d7e20: outfit 4 -> roleinfo +0x308 + 0xC0 (the swimsuit model, no colour texture)
		else if(outfit == 4) outfit = 0;
	}
	auto it = g_defs.find(art);
	if(it == g_defs.end()){ rm8Log("events: no presenter model %d", art); return nullptr; }
	Npc *n = new Npc; n->ui = ui; n->widget = widget;
	n->clump = rm8LoadClump(it->second.dff, it->second.texdir);
	if(!n->clump){ rm8Log("events: cannot load presenter %s", it->second.dff.c_str()); delete n; return nullptr; }
	Frame *f = n->clump->getFrame();
	n->base = f->matrix; n->base.flags = 0;
	n->hier = HAnimHierarchy::find(f);
	if(n->hier) n->hier->attach();
	for(auto &a : it->second.anims){
		Animation *an = rm8LoadAnim(a.second);
		if(an && n->hier && an->getNumNodes() == n->hier->numNodes) n->anims[a.first] = an;
	}
	n->art = art;
	FORLIST(lnk, n->clump->atomics){	// paper-doll parts by material user data "name" (roles.cpp loadModel)
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		for(int i = 0; i < g->matList.numMaterials; i++){
			Material *mat = g->matList.materials[i];
			int k = UserDataArray::materialFindIndex(mat, "name");
			if(k < 0) continue;
			UserDataArray *ua = UserDataArray::materialGet(mat, k);
			if(ua && ua->datatype == USERDATASTRING && ua->numElements > 0 && ua->getString(0)) n->parts[ua->getString(0)].push_back(mat);
		}
	}
	if(charId > 0 && outfit >= 0 && outfit <= 3){	// [V] Court_LoadCharacterTexture(model, Id, outfit, 1) = Asset_LoadCharacterTexture @0x4934a0: body colour = the outfit, front view (the original shows the front-left model)
		char key[160];
		snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_body_f_%02d.tga", charId, charId, outfit);
		setPart(n, "body", rm8PackTexture(key));
		const char *extra = (charId == 2 || charId == 15) ? "other" : (charId == 4 || charId == 9 || charId == 11 || charId == 13) ? "hair" : (charId == 7 || charId == 10) ? "toujin" : nullptr;
		if(extra){ snprintf(key, sizeof key, "article\\role_tex\\A%02d\\A%02d_%s_f_%02d.tga", charId, charId, extra, outfit); setPart(n, extra, rm8PackTexture(key)); }
	}
	g_npcs.push_back(n);
	return n;
}

// [V] a plain model shown in a dialog (the auction's "Item" article: Terra_GetBuildingModelId art, CArticleDFF_Load(art) + Article_ApplyPropertiesIfActive(art, anim, 0, 0)
// in CUi_vendue__vf16__Init @0x4e2fc0) from the scene's own art definition (dff / texture dir / animation 0 key)
Npc *npcCreateModel(const std::string &dff, const std::string &texdir, const std::string &anim, Rm8Ui *ui, const char *widget)
{
	Npc *n = new Npc; n->ui = ui; n->widget = widget; n->flip = false;
	n->clump = rm8LoadClump(dff, texdir);
	if(!n->clump){ rm8Log("events: cannot load model %s", dff.c_str()); delete n; return nullptr; }
	Frame *f = n->clump->getFrame();
	n->base = f->matrix; n->base.flags = 0;
	n->hier = HAnimHierarchy::find(f);
	if(n->hier) n->hier->attach();
	if(!anim.empty() && anim != "-"){
		Animation *an = rm8LoadAnim(anim);
		if(an && n->hier && an->getNumNodes() == n->hier->numNodes) n->anims[0] = an;
	}
	n->art = -1;
	g_npcs.push_back(n);
	return n;
}

void npcSetSpin(Npc *n, float degPerSec, float rscale){ if(n){ n->spin = degPerSec; n->rscale = rscale; } }

void npcPlay(Npc *n, int act, bool loop)
{
	if(!n || !n->hier) return;
	n->once = !loop; n->anim = act; n->face.clear();
	auto it = n->anims.find(act);
	if(it == n->anims.end() || !it->second) return;
	n->hier->interpolator->setCurrentAnim(it->second);	// [V] Article_SelectByTypeAndSetProperties(act, 1, 0): once, from the start
	applyAnim(n, 0);
	applyFace(n, 0);
}

void npcDestroy(Npc *n)
{
	if(!n) return;
	g_npcs.erase(std::remove(g_npcs.begin(), g_npcs.end(), n), g_npcs.end());
	if(n->tex){ rm8UiSetTextureN(n->ui, n->widget.c_str(), nullptr); n->tex->raster = nullptr; n->tex->destroy(); }
	if(n->cam){
		if(n->cam->world) n->cam->world->removeCamera(n->cam);
		Raster *fb = n->cam->frameBuffer, *zb = n->cam->zBuffer;
		n->cam->destroy();
		if(fb) fb->destroy();
		if(zb) zb->destroy();
	}
	if(n->clump) n->clump->destroy();
	delete n;
}

Texture *npcTexture(Npc *n){ return n ? n->tex : nullptr; }

void npcUpdate(float dt){ for(Npc *n : g_npcs){ applyAnim(n, dt); n->angle = std::fmod(n->angle + n->spin * dt, 360.0f); } }

// [V] Article_CalcClumpBoundingSphere @0x443d60 / Article_UpdateBoundingSphere @0x443b00: the atomics' bounding spheres (in clump space: atomic LTM * inverse clump LTM) merged one
// after the other (the first one starts the sphere; every further one: if r <= d + r2 the centre moves to the mean of the old centre and the new one shifted by (r2 - r) / d,
// r = (d + r2 + r) / 2); the loaders then move the centre into the world with the article's frame (RwV3dTransformPoint with frame + 0x10) and keep it with the radius (task +0xC / +0x18)
static void measure(Npc *n)
{
	Frame *f = n->clump->getFrame();
	f->transform(&n->base, COMBINEREPLACE);
	f->updateObjects();
	Matrix inv; Matrix::invert(&inv, f->getLTM());
	float c[3] = {0, 0, 0}, r = 0; bool first = true;
	FORLIST(lnk, n->clump->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		Geometry *g = a->geometry;
		if(!g || !a->getFrame()) continue;
		V3d p = g->morphTargets[0].boundingSphere.center;
		float r2 = g->morphTargets[0].boundingSphere.radius;
		V3d::transformPoints(&p, &p, 1, a->getFrame()->getLTM());
		V3d::transformPoints(&p, &p, 1, &inv);
		if(first){ c[0] = p.x; c[1] = p.y; c[2] = p.z; r = r2; first = false; continue; }
		float d3[3] = {p.x - c[0], p.y - c[1], p.z - c[2]};
		float d = std::sqrt(d3[0] * d3[0] + d3[1] * d3[1] + d3[2] * d3[2]);
		if(r <= d + r2 && d > 1e-6f){
			float k = (r2 - r) * (1.0f / d), np[3] = {p.x, p.y, p.z};
			for(int q = 0; q < 3; q++) c[q] = (np[q] + c[q] + k * d3[q]) * 0.5f;
			r = (d + r2 + r) * 0.5f;
		}
	}
	if(first){ r = 1; }
	V3d cw = {c[0], c[1], c[2]};
	V3d::transformPoints(&cw, &cw, 1, f->getLTM());
	n->cx = cw.x; n->cy = cw.y; n->cz = cw.z; n->r = std::max(r, 1.0f);
	n->placed = true;
	rm8Log("events: presenter sphere centre (%.0f %.0f %.0f) radius %.0f", n->cx, n->cy, n->cz, n->r);
}

// every frame, before the main camera: render each live presenter into its 256x256 camera raster
// [V] OrthoCam_Create @0x4617f0 (a 256 x 256 parallel-projection camera, identity frame: looks along +z) + OrthoCam_RenderToRaster @0x461a30 / OrthoCam_SetPosAndSize @0x4618e0:
// far = 5 r, near = 0.1 r, view window (1.1 r, 1.1 r) (half extents in world units), the camera sits 1.5 r behind the sphere centre; cleared to transparent black
// (image + Z), the model rendered with alpha test >= 0xC4. The raster is drawn into the widget with Rect_CalcAspectUVMargins @0x45f260 (ui.cpp).
void preRender(Camera *main)
{
	for(Npc *n : g_npcs){
		if(!n->clump) continue;
		if(!n->cam){
			n->cam = Camera::create();
			n->cam->setFrame(Frame::create());
			n->cam->frameBuffer = Raster::create(256, 256, 32, Raster::CAMERATEXTURE | Raster::C8888);
			n->cam->zBuffer = Raster::create(256, 256, 0, Raster::ZBUFFER);
			n->cam->setProjection(Camera::PARALLEL);
			n->tex = Texture::create(n->cam->frameBuffer);
			n->tex->setFilter(Texture::LINEAR);
			n->tex->setAddressU(Texture::CLAMP); n->tex->setAddressV(Texture::CLAMP);
			rm8UiSetTextureN(n->ui, n->widget.c_str(), n->tex);
		}
		if(!n->placed) measure(n);
		if(n->cam->world != main->world){ if(n->cam->world) n->cam->world->removeCamera(n->cam); main->world->addCamera(n->cam); }
		float rs = n->r * n->rscale;	// [V] the auction's lot is drawn with r * 1.6 (LittleGame_Rotate3D @0x4e1f40)
		float D = 1.5f * rs;
		// [A] the paper-doll NPC models are drawn right (facing the panel, correct layer order) when seen from their +z side, i.e. rotated half a turn
		// about Y in front of the identity camera (verified against the original on the phone; the front side shows the mirrored, layer-reversed art)
		static const bool back = getenv("RM8_NPC_FRONT") == nullptr;	// test switch: the other side
		Frame *f = n->clump->getFrame();
		f->transform(&n->base, COMBINEREPLACE);
		V3d c = {-n->cx, -n->cy, -n->cz}, ay = {0, 1, 0}, d = {0, 0, D};
		f->translate(&c, COMBINEPOSTCONCAT);
		if(back && n->flip) f->rotate(&ay, 180.0f, COMBINEPOSTCONCAT);
		if(n->angle != 0) f->rotate(&ay, n->angle, COMBINEPOSTCONCAT);
		f->translate(&d, COMBINEPOSTCONCAT);
		Matrix id; id.setIdentity();
		n->cam->getFrame()->transform(&id, COMBINEREPLACE);
		V2d vw = {rs * 1.1f, rs * 1.1f};
		n->cam->setViewWindow(&vw);
		n->cam->setNearPlane(rs * 0.1f); n->cam->setFarPlane(rs * 5.0f);
		RGBA none = {0, 0, 0, 0};
		n->cam->clear(&none, Camera::CLEARIMAGE | Camera::CLEARZ);
		n->cam->beginUpdate();
		uint32 fn = GetRenderState(ALPHATESTFUNC), ref = GetRenderState(ALPHATESTREF);
		SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 196);	// [V] 0xC4
		n->clump->render();
		SetRenderState(ALPHATESTFUNC, fn); SetRenderState(ALPHATESTREF, ref);
		n->cam->endUpdate();
		static const char *seq = getenv("RM8_NPC_DUMPSEQ");	// test switch: the raster every 6th frame (prefix_NNN.png, 60 frames) to see the mouth frames change
		static int sf = 0;
		if(seq && (sf++ % 6) == 0 && sf / 6 < 60){ if(Image *img = n->cam->frameBuffer->toImage()){ char fn[260]; snprintf(fn, sizeof fn, "%s_%03d.png", seq, sf / 6); writePNG(img, fn); img->destroy(); } }
		static const char *dump = getenv("RM8_NPC_DUMP");	// test switch: write the offscreen raster once per presenter to this png
		if(dump && !n->dumped){ n->dumped = true; if(Image *img = n->cam->frameBuffer->toImage()){ writePNG(img, dump); img->destroy(); } }
	}
}

}	// namespace ev

void rm8EventPreRender(Camera *main){ ev::preRender(main); rm8BalancePreRender(main); }	// the balance screen's anchor + dolls (balance.cpp)
