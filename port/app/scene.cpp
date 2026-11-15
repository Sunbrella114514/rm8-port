// Shared RichMan8 scene code, see scene.h. Facts marked [V] are verified against the game code
// (docs/map_format.md, port/README.md); [A] marks approximations.
#include "scene.h"
#include "boardfx.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <map>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <filesystem>
#ifdef __ANDROID__
#include <android/log.h>
#endif

using namespace rw;

void rm8Log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
#ifdef __ANDROID__
	__android_log_vprint(ANDROID_LOG_INFO, "rm8", fmt, ap);
#else
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	fflush(stderr);
#endif
	va_end(ap);
}

static std::string g_dataEdition;
void rm8SetDataEdition(const std::string &edition){ g_dataEdition = edition; }
const std::string &rm8DataEdition(){ return g_dataEdition; }

// [A] the user's own files (rm8_settings.txt, rm8_gamedate.bin, rm8_box.bin, save\): desktop %APPDATA%\RM8, so deleting or re-extracting the data folder never
// takes the progress with it (docs/assets_legal.md section 8 item 9). RM8_DATA_DIR overrides (test runs); Android keeps the data dir. The first run copies whatever the old
// place (the folder above scenes\) has, once (a marker file in the new folder).
std::string rm8UserDataDir(const std::string &scenesRoot)
{
	static std::string dir; static bool done = false;
	if(done) return dir;
	done = true;
	namespace fs = std::filesystem;
	fs::path sp(scenesRoot);
	if(!sp.has_filename()) sp = sp.parent_path();
	fs::path oldDir = sp.parent_path();
	if(oldDir.empty()) oldDir = ".";
	{ std::error_code ec0; fs::path ab = fs::absolute(oldDir, ec0); if(!ec0) oldDir = ab; }
	const char *env = getenv("RM8_DATA_DIR");
	if(env && *env) return dir = env;
#if defined(__ANDROID__) || !defined(_WIN32)
	return dir = oldDir.string();
#else
	const char *ap = getenv("APPDATA");
	if(!ap || !*ap) return dir = oldDir.string();
	fs::path nd = fs::path(ap) / "RM8";
	std::error_code ec;
	fs::create_directories(nd, ec);
	if(ec){ rm8Log("user data: cannot create %s, using %s", nd.string().c_str(), oldDir.string().c_str()); return dir = oldDir.string(); }
	fs::path mark = nd / "migrated_from_old_location.txt";
	if(!fs::exists(mark, ec) && fs::weakly_canonical(oldDir, ec) != fs::weakly_canonical(nd, ec)){
		int n = 0;
		for(const char *f : {"rm8_settings.txt", "rm8_gamedate.bin", "rm8_box.bin"})
			if(fs::exists(oldDir / f, ec) && !fs::exists(nd / f, ec) && fs::copy_file(oldDir / f, nd / f, fs::copy_options::skip_existing, ec)) n++;
		if(fs::is_directory(oldDir / "save", ec)){
			fs::create_directories(nd / "save", ec);
			for(auto &e : fs::directory_iterator(oldDir / "save", ec))
				if(e.is_regular_file(ec) && !fs::exists(nd / "save" / e.path().filename(), ec) && fs::copy_file(e.path(), nd / "save" / e.path().filename(), fs::copy_options::skip_existing, ec)) n++;
		}
		if(FILE *m = fopen(mark.string().c_str(), "wb")){ fprintf(m, "copied %d file(s) from %s once\n", n, oldDir.string().c_str()); fclose(m); }
		rm8Log("user data: copied %d file(s) from the old place %s to %s", n, oldDir.string().c_str(), nd.string().c_str());
	}
	return dir = nd.string();
#endif
}

static std::string lower(std::string s){ std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

// ---- pack reader (format A, see scripts/rm8pak.py) ----
struct Pack {
	FILE *f = nullptr;
	std::map<std::string, std::pair<uint32, uint32>> byName;
	bool open(const std::string &path){
		f = fopen(path.c_str(), "rb");
		if(!f) return false;
		uint32 h[5];
		if(fread(h, 4, 5, f) != 5) return false;
		std::vector<uint32> idx(h[3] * 4);
		if(fread(idx.data(), 4, idx.size(), f) != idx.size()) return false;
		for(uint32 i = 0; i < h[3]; i++){
			uint32 nl = idx[i*4], off = idx[i*4+1], sz = idx[i*4+2];
			std::string n(nl, '\0'); fseek(f, off, SEEK_SET);
			if(fread(&n[0], 1, nl, f) != nl) return false;
			byName[lower(n)] = {off + nl, sz};
		}
		return true;
	}
	bool read(const std::string &key, std::vector<uint8> &out){
		if(!f) return false;
		auto it = byName.find(lower(key));
		if(it == byName.end()) return false;
		out.resize(it->second.second);
		fseek(f, it->second.first, SEEK_SET);
		return fread(out.data(), 1, out.size(), f) == out.size();
	}
};
static Pack g_clumps, g_tex0, g_tex1;
static std::string g_texDir;	// texture dir of the model being loaded, e.g. "article\build\m01\"
static std::map<std::string, Texture*> g_texCache;
static int g_texMissing;

void rm8RegisterPlugins()
{
	registerMeshPlugin(); registerNativeDataPlugin(); registerAtomicRightsPlugin(); registerMaterialRightsPlugin();
	xbox::registerVertexFormatPlugin(); registerSkinPlugin(); registerUserDataPlugin(); registerHAnimPlugin();
	registerMatFXPlugin(); registerUVAnimPlugin(); ps2::registerADCPlugin();
	rm8LmRegister();
	rm8WaterRegister();
}

bool rm8OpenPacks(const std::string &game)
{
	if(!g_clumps.open(game + "/Clump/clump0000.dat")){ rm8Log("cannot open %s/Clump/clump0000.dat", game.c_str()); return false; }
	if(!g_tex0.open(game + "/Texture/tex0000.dat")){ rm8Log("cannot open %s/Texture/tex0000.dat", game.c_str()); return false; }
	g_tex1.open(game + "/Texture/tex0001.dat");
	return true;
}

int rm8MissingTextures(){ return g_texMissing; }

// returns a texture with one reference owned by the caller (or nullptr)
static Texture *texFromPackKey(const std::string &key)
{
	std::string k = lower(key);
	auto c = g_texCache.find(k);
	if(c != g_texCache.end()){ if(c->second) c->second->addRef(); return c->second; }
	std::vector<uint8> buf;
	Texture *t = nullptr;
	if(g_tex1.read(k, buf) || g_tex0.read(k, buf)){
		Image *img = readTGAMemory(buf.data(), (uint32)buf.size());
		if(img){
			t = Texture::create(Raster::createFromImage(img));
			img->destroy();
			t->setFilter(Texture::LINEAR);
		}
	}
	g_texCache[k] = t;	// the cache keeps the creation reference
	if(t) t->addRef();
	return t;
}

// [A] a particle sprite texture without an alpha channel (prt\prtNN.tga, 24 bit) drawn with an INVSRCALPHA destination factor: the quad would darken / blank the whole rectangle
// (alpha 1 everywhere) and show hard square edges; the sprite shape lives in the colour, so the alpha comes from the brightest colour channel (black = transparent)
Texture *rm8PackTextureLumaAlpha(const std::string &key)
{
	std::string k = lower(key) + "#luma";
	auto c = g_texCache.find(k);
	if(c != g_texCache.end()){ if(c->second) c->second->addRef(); return c->second; }
	std::vector<uint8> buf;
	Texture *t = nullptr;
	if(g_tex1.read(lower(key), buf) || g_tex0.read(lower(key), buf)){
		Image *img = readTGAMemory(buf.data(), (uint32)buf.size());
		if(img){
			bool had = img->hasAlpha();
			if(!had){
				img->convertTo32();
				for(int y = 0; y < img->height; y++){
					uint8 *row = img->pixels + y * img->stride;
					for(int x = 0; x < img->width; x++){ uint8 *q = row + x * 4; q[3] = std::max(q[0], std::max(q[1], q[2])); }
				}
			}
			t = Texture::create(Raster::createFromImage(img));
			img->destroy();
			t->setFilter(Texture::LINEAR);
		}
	}
	g_texCache[k] = t;
	if(t) t->addRef();
	return t;
}

static Texture *readTextureCB(const char *name, const char *mask)
{
	Texture *t = texFromPackKey(g_texDir + name + ".tga");
	if(!t){ g_texMissing++; rm8Log("warning: texture %s%s.tga not found", g_texDir.c_str(), name); return nullptr; }
	strncpy(t->name, name, 31);
	return t;
}

void rm8InstallTextureCallback(){ Texture::readCB = readTextureCB; rm8LmInit(); rm8WaterInit(); }

static Texture *texFromFile(const std::string &path)
{
	Image *img = readPNG(path.c_str());
	if(!img){ rm8Log("warning: cannot read %s", path.c_str()); return nullptr; }
	Texture *t = Texture::create(Raster::createFromImage(img));
	img->destroy();
	t->setFilter(Texture::LINEAR);
	t->setAddressU(Texture::CLAMP); t->setAddressV(Texture::CLAMP);
	return t;
}

// ---- custom geometry (prelit) ----
struct V { float x, y, z, u, v; };
// takes over the caller's reference on tex
// lit: the mesh gets per-vertex normals (area weighted from its triangles; the ground / road / cliff winding gives +y for a flat quad) and the LIGHT flag, for the multiplicative
// terrain lighting of lmpipe.cpp (D3D lights of the original's vertex buffers, CVertexBufObj_CreateBuffer @0x42af60 + vf2 @0x40bb70)
static Atomic *makeMesh(World *world, const std::vector<V> &verts, const std::vector<uint16> &tris, Texture *tex, RGBA color, bool lit = false)
{
	Geometry *geo = Geometry::create((int32)verts.size(), (int32)tris.size() / 3,
		Geometry::POSITIONS | Geometry::TEXTURED | Geometry::PRELIT | Geometry::MODULATE | (lit ? (Geometry::NORMALS | Geometry::LIGHT) : 0));
	MorphTarget *mt = &geo->morphTargets[0];
	if(lit && mt->normals){
		std::vector<V3d> nn(verts.size(), V3d{0, 0, 0});
		for(size_t i = 0; i + 2 < tris.size(); i += 3){
			const V &a = verts[tris[i]], &b = verts[tris[i+1]], &c = verts[tris[i+2]];
			V3d e1 = {b.x - a.x, b.y - a.y, b.z - a.z}, e2 = {c.x - a.x, c.y - a.y, c.z - a.z};
			V3d n = {e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
			for(int k = 0; k < 3; k++){ V3d &d = nn[tris[i + k]]; d.x += n.x; d.y += n.y; d.z += n.z; }
		}
		for(size_t i = 0; i < verts.size(); i++){
			float l = sqrtf(nn[i].x*nn[i].x + nn[i].y*nn[i].y + nn[i].z*nn[i].z);
			mt->normals[i] = l > 1e-6f ? V3d{nn[i].x / l, nn[i].y / l, nn[i].z / l} : V3d{0, 1, 0};
		}
	}
	for(size_t i = 0; i < verts.size(); i++){
		mt->vertices[i] = {verts[i].x, verts[i].y, verts[i].z};
		geo->texCoords[0][i].u = verts[i].u; geo->texCoords[0][i].v = verts[i].v;
		geo->colors[i] = {255, 255, 255, 255};
	}
	for(size_t i = 0; i < tris.size() / 3; i++){
		Triangle *t = &geo->triangles[i];
		t->v[0] = tris[i*3]; t->v[1] = tris[i*3+1]; t->v[2] = tris[i*3+2]; t->matId = 0;
	}
	Material *mat = Material::create();
	if(tex){ mat->setTexture(tex); tex->destroy(); }
	mat->color = color;
	geo->matList.appendMaterial(mat);
	mat->destroy();
	geo->calculateBoundingSphere();
	geo->unlock();
	Atomic *a = Atomic::create();
	a->setGeometry(geo, 0);
	geo->destroy();
	a->setFrame(Frame::create());
	world->addAtomic(a);
	return a;
}

// Skinned models with more bones than the GPU skin shader supports (Adreno GLES: 48) go through the
// default pipeline; when animated, their vertices are skinned on the CPU (rm8UpdateScene).
static std::vector<Atomic*> demoteBigSkins(Clump *c)
{
	std::vector<Atomic*> out;
	FORLIST(lnk, c->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		Skin *sk = a->geometry ? Skin::get(a->geometry) : nullptr;
		if(sk && sk->numBones > gl3::skinMaxBones){ a->pipeline = nil; out.push_back(a); }
	}
	return out;
}

static void updateUVAnims(Rm8Scene &s, float dt);
static std::map<std::string, Animation*> g_anims;	// shared by all instances, kept for the whole run
static Animation *loadAnim(const std::string &name)
{
	std::string k = lower(name);
	auto it = g_anims.find(k);
	if(it != g_anims.end()) return it->second;
	std::vector<uint8> buf;
	Animation *an = nullptr;
	if(g_clumps.read(k, buf)){
		StreamMemory in; in.open(buf.data(), (uint32)buf.size());
		if(findChunk(&in, ID_ANIMANIMATION, nil, nil)) an = Animation::streamRead(&in);
		in.close();
	}
	g_anims[k] = an;
	return an;
}

// Articles: frame#0 = clump root (our placement), frame#1 = HAnim hierarchy root, nodes below.
// The animation drives node frames only, so the placement on the clump frame is kept.
static bool setupAnim(Rm8Scene &s, Clump *c, const std::string &animName, const std::vector<Atomic*> &demoted, const std::string &dff, bool once, bool randomPhase)
{
	Animation *an = loadAnim(animName);
	HAnimHierarchy *h = HAnimHierarchy::find(c->getFrame());
	if(!an || !h){ rm8Log("warning: no animation for %s (%s%s)", dff.c_str(), an ? "" : "anim missing ", h ? "" : "no hierarchy"); return false; }
	if(an->getNumNodes() != h->numNodes){ rm8Log("warning: %s: anim nodes %d != hierarchy nodes %d", dff.c_str(), an->getNumNodes(), h->numNodes); return false; }
	h->attach();
	if(!h->interpolator->setCurrentAnim(an)) return false;
	// [V] RenderAnim_ApplyTransform @0x43cb90: map articles start at (rand()%10)/10 of the animation length
	if(randomPhase){ static uint32 seed = 12345; seed = seed * 1103515245u + 12345u; float ph = ((seed >> 16) % 10) / 10.0f * an->duration; if(ph > 0) h->interpolator->addTime(ph); }
	Rm8ArtAnim aa; aa.clump = c; aa.hier = h; aa.once = once; aa.name = dff;
	FORLIST(lnk, c->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		if(Skin::get(a->geometry)) Skin::setHierarchy(a, h);
	}
	for(Atomic *a : demoted){
		Geometry *g = a->geometry;
		if(Skin::get(g)->numBones != h->numNodes) continue;
		MorphTarget *mt = &g->morphTargets[0];
		aa.cpuSkin.push_back(a);
		aa.basePos.emplace_back(mt->vertices, mt->vertices + g->numVertices);
		aa.baseNrm.emplace_back();
		if(mt->normals) aa.baseNrm.back().assign(mt->normals, mt->normals + g->numVertices);
	}
	s.anims.push_back(std::move(aa));
	return true;
}

static void cpuSkinAtomic(HAnimHierarchy *h, Atomic *a, const std::vector<V3d> &bp, const std::vector<V3d> &bn)
{
	// same matrices as gl3 uploadSkinMatrices (non local-space): invBone * boneLTM * inverse(atomic LTM)
	Geometry *g = a->geometry;
	Skin *sk = Skin::get(g);
	Matrix invAtm, tmp;
	Matrix::invert(&invAtm, a->getFrame()->getLTM());
	std::vector<Matrix> M(sk->numBones);
	Matrix *inv = (Matrix*)sk->inverseMatrices;
	for(int i = 0; i < sk->numBones; i++){
		inv[i].flags = 0;
		Matrix::mult(&tmp, &h->matrices[i], &invAtm);
		Matrix::mult(&M[i], &inv[i], &tmp);
	}
	MorphTarget *mt = &g->morphTargets[0];
	for(int v = 0; v < g->numVertices; v++){
		V3d p = {0, 0, 0}, n = {0, 0, 0};
		for(int k = 0; k < 4; k++){
			float w = sk->weights[v*4 + k];
			if(w == 0.0f) continue;
			const Matrix *m = &M[sk->indices[v*4 + k]];
			V3d t;
			V3d::transformPoints(&t, &bp[v], 1, m);
			p.x += t.x * w; p.y += t.y * w; p.z += t.z * w;
			if(!bn.empty()){
				V3d::transformVectors(&t, &bn[v], 1, m);
				n.x += t.x * w; n.y += t.y * w; n.z += t.z * w;
			}
		}
		mt->vertices[v] = p;
		if(!bn.empty()) mt->normals[v] = normalize(n);
	}
	g->lock(Geometry::LOCKVERTICES | (bn.empty() ? 0 : Geometry::LOCKNORMALS));	// re-uploaded at the next render
}

void rm8UpdateScene(Rm8Scene &s, float dt)
{
	rm8WaterUpdate(dt);
	updateUVAnims(s, dt);
	for(Rm8ArtAnim &aa : s.anims){
		HAnimHierarchy *h = aa.hier;
		AnimInterpolator *ip = h->interpolator;
		// [A] speed 1, starts at t = 0; librw restarts at 0 after the end (loop); "once" holds the last frame
		float step = dt;	// per-animation: clamping the shared dt froze every animation after a finished one-shot
		if(aa.once) step = std::min(step, std::max(0.0f, ip->currentAnim->duration - ip->currentTime - 1e-4f));
		ip->addTime(step);
		for(int i = 0; i < h->numNodes; i++){
			Frame *f = h->nodeInfo[i].frame;
			if(!f) continue;
			ip->applyCB(&f->matrix, ip->getInterpFrame(i));
			f->updateObjects();
		}
		h->updateMatrices();
		static const char *dbg = getenv("RM8_DEBUG_ANIM");
		if(dbg && aa.name.find(dbg) != std::string::npos){
			rm8Log("[anim %s] t=%.3f", aa.name.c_str(), ip->currentTime);
			FORLIST(lnk, aa.clump->atomics){ Atomic *at = Atomic::fromClump(lnk); Matrix *l = at->getFrame()->getLTM();
				rm8Log("   atomic verts=%d ltm.right=(%.2f %.2f %.2f) up=(%.2f %.2f %.2f) pipe=%p", at->geometry->numVertices, l->right.x, l->right.y, l->right.z, l->up.x, l->up.y, l->up.z, (void*)at->pipeline); }
			for(int i = 0; i < h->numNodes; i++){ Frame *f = h->nodeInfo[i].frame; if(f) rm8Log("   node%d right=(%.2f %.2f %.2f) pos=(%.1f %.1f %.1f)", i, f->matrix.right.x, f->matrix.right.y, f->matrix.right.z, f->matrix.pos.x, f->matrix.pos.y, f->matrix.pos.z); }
		}
		for(size_t k = 0; k < aa.cpuSkin.size(); k++) cpuSkinAtomic(h, aa.cpuSkin[k], aa.basePos[k], aa.baseNrm[k]);
	}
	rm8PrtUpdate(dt);	// after the article animations: the emitters read their hook frames' LTMs
	rm8BoardFxUpdate(s, dt);	// event icons / clicked scenery (boardfx.cpp)
}

// CPU skinning of a clump outside the scene's article list (npc.cpp models: the ambulance's nurse has 91 bones, the phone's GPU limit is 48): same path as the map articles
Rm8CpuSkin rm8CpuSkinPrepare(Clump *c, HAnimHierarchy *h)
{
	Rm8CpuSkin r;
	for(Atomic *a : demoteBigSkins(c)){
		Geometry *g = a->geometry;
		if(Skin::get(g)->numBones != h->numNodes) continue;
		MorphTarget *mt = &g->morphTargets[0];
		r.atoms.push_back(a);
		r.basePos.emplace_back(mt->vertices, mt->vertices + g->numVertices);
		r.baseNrm.emplace_back();
		if(mt->normals) r.baseNrm.back().assign(mt->normals, mt->normals + g->numVertices);
	}
	return r;
}
void rm8CpuSkinApply(Rm8CpuSkin &r, HAnimHierarchy *h)
{
	for(size_t k = 0; k < r.atoms.size(); k++) cpuSkinAtomic(h, r.atoms[k], r.basePos[k], r.baseNrm[k]);
}

static std::map<std::string, UVAnimDictionary*> g_uvDicts;	// kept for the whole run
static UVAnimDictionary *loadUVDict(const std::string &name)
{
	std::string k = lower(name);
	auto it = g_uvDicts.find(k);
	if(it != g_uvDicts.end()) return it->second;
	std::vector<uint8> buf;
	UVAnimDictionary *d = nullptr;
	if(g_clumps.read(k, buf)){
		StreamMemory in; in.open(buf.data(), (uint32)buf.size());
		if(findChunk(&in, ID_UVANIMDICT, nil, nil)) d = UVAnimDictionary::streamRead(&in);
		in.close();
	}
	if(!d) rm8Log("warning: cannot load UV animation %s", name.c_str());
	g_uvDicts[k] = d;
	return d;
}

static void parseAtomicFlags(Clump *c);
static Clump *loadArticle(const std::string &dff, const std::string &texdir, const std::string &uva)
{
	std::vector<uint8> buf;
	if(!g_clumps.read(dff, buf)) return nullptr;
	g_texDir = texdir;
	// materials bind their UV animations by name from the current dictionary while being read
	currentUVAnimDictionary = uva != "-" ? loadUVDict(uva) : nullptr;
	StreamMemory in; in.open(buf.data(), (uint32)buf.size());
	Clump *c = nullptr;
	if(findChunk(&in, ID_CLUMP, nil, nil)) c = Clump::streamRead(&in);
	in.close();
	currentUVAnimDictionary = nullptr;
	if(c && !getenv("RM8_CLUMP_FWD")){	// RM8_CLUMP_FWD: experiment switch
		// [V] RpClumpStreamRead @0x53b920 links each atomic at the list head and RpClumpRender @0x53abb0 walks from the head, so they are drawn in
		// reverse stream order; librw appends. Restore RenderWare's order: it decides which blended edge wins
		// where coplanar paper-doll parts meet (visible seams at the characters' joints otherwise).
		std::vector<Atomic*> at;
		FORLIST(lnk, c->atomics) at.push_back(Atomic::fromClump(lnk));
		for(Atomic *a : at) c->removeAtomic(a);
		for(auto it = at.rbegin(); it != at.rend(); ++it) c->addAtomic(*it);
	}
	if(c) parseAtomicFlags(c);
	return c;
}

// [V] Atomic_ParseUserDataFlags @0x45c370: the frame user data string "[...]" of an atomic gives render flags:
// '@'+hex digit -> low 4 bits (particle hook), '#' 0x10000000, '$' 0x20000000, '%' 0x40000000, '^' 0x80000000,
// '&' 0x8000000, '*' 0x4000000, '(' 0x2000000, ')' 0x1000000, 'z' 0x400000, 'w'/'W' 0x4008000.
// A first string not starting with '[' stops the parse; each array overwrites the flag.
static std::unordered_map<Atomic*, uint32> g_atomFlags;
static void parseAtomicFlags(Clump *c)
{
	FORLIST(lnk, c->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		UserDataExtension *ud = a->getFrame() ? UserDataExtension::get(a->getFrame()) : nullptr;
		for(int i = 0; ud && i < ud->getCount(); i++){
			UserDataArray *arr = ud->get(i);
			const char *p = arr && arr->datatype == USERDATASTRING && arr->numElements > 0 ? arr->getString(0) : nullptr;
			if(!p || *p != '[') break;
			uint32 f = 0;
			for(p++; *p && *p != ']'; p++){
				switch(*p){
				case '@':
					p++;
					if(*p == ']' || !*p){ p--; break; }
					if(*p >= '1' && *p <= '9') f |= (*p - '0') & 0xF;
					else if(*p > '`' && *p < 'g') f |= (*p - 0x57) & 0xF;
					break;
				case '#': f |= 0x10000000; break;
				case '$': f |= 0x20000000; break;
				case '%': f |= 0x40000000; break;
				case '^': f |= 0x80000000; break;
				case '&': f |= 0x8000000; break;
				case '*': f |= 0x4000000; break;
				case '(': f |= 0x2000000; break;
				case ')': f |= 0x1000000; break;
				case 'z': f |= 0x400000; break;
				case 'w': case 'W': f |= 0x4008000; break;
				}
			}
			g_atomFlags[a] = f;
		}
	}
}

// [V] Atomic render callback 0x45c6a0 + CArticleMgr__vf5_SetRenderMode @0x43b210: articles are drawn in two passes
// (mode 0, then mode 1). Hidden: low 4 bits (hook frames), '^', '%'. Pass 0: atomics without '*': 'z' z-write off,
// '$' no culling, '(' alpha test >= 0x60. Pass 1: '*' atomics, z-write off with vertex alpha, '$', '(',
// ')' blend SRCALPHA/ONE, 'w' blend ZERO/SRCALPHA; states restored after each atomic.
static bool g_glassMode = false;	// the see-through pass: the cut-out alpha test ('(' flag, ref 0x60) would eat the 40 % alpha, so the blend does the cut-out
static void renderArticleAtomic(Atomic *a, int pass)
{
	auto it = g_atomFlags.find(a);
	uint32 f = it == g_atomFlags.end() ? 0 : it->second;
	if((f & 0xF) || (f & 0x80000000) || (f & 0x40000000)) return;
	bool second = (f & 0x4000000) != 0;
	if(second != (pass == 1)) return;
	if(!f){ a->render(); return; }
	uint32 zw = GetRenderState(ZWRITEENABLE), cull = GetRenderState(CULLMODE), atf = GetRenderState(ALPHATESTFUNC), atr = GetRenderState(ALPHATESTREF);
	uint32 sb = GetRenderState(SRCBLEND), db = GetRenderState(DESTBLEND);
	if(pass == 1 || (f & 0x400000)) SetRenderState(ZWRITEENABLE, 0);
	if(f & 0x20000000) SetRenderState(CULLMODE, CULLNONE);
	if((f & 0x2000000) && !g_glassMode){ SetRenderState(ALPHATESTREF, 0x60); SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); }
	if(pass == 1 && (f & 0x1000000)){ SetRenderState(SRCBLEND, BLENDSRCALPHA); SetRenderState(DESTBLEND, BLENDONE); }
	if(pass == 1 && (f & 0x8000)){ SetRenderState(SRCBLEND, BLENDZERO); SetRenderState(DESTBLEND, BLENDSRCALPHA); }
	a->render();
	SetRenderState(ZWRITEENABLE, zw); SetRenderState(CULLMODE, cull); SetRenderState(ALPHATESTFUNC, atf); SetRenderState(ALPHATESTREF, atr);
	SetRenderState(SRCBLEND, sb); SetRenderState(DESTBLEND, db);
}

// ---- per-article colour modulation / opacity (boardfx.cpp): [V] BuildObj_SetAlpha @0x4efb20 (CArticleMgr vf22 @0x440ea0 with (0x60, 0x60, 0x60, 0xFF): the building
// of a blinking plot is darkened) and Art_UpdateGlowAlpha @0x441740 + CArticleMgr__vf5_SetRenderMode @0x43b210 mode 3 (an article between the camera and a role is
// drawn with alpha 0.4 in the later pass instead of the opaque one). Materials are per clump, so the colours are edited in place and restored from `orig`.
namespace {
struct ArtMod { std::vector<std::pair<Material*, RGBA>> orig; int grey = 255; float alpha = 1.0f; };
std::unordered_map<Clump*, ArtMod> g_artMod;
ArtMod &artMod(Clump *c)
{
	auto it = g_artMod.find(c);
	if(it != g_artMod.end()) return it->second;
	ArtMod &m = g_artMod[c];
	FORLIST(lnk, c->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		if(!g) continue;
		for(int i = 0; i < g->matList.numMaterials; i++) if(Material *mat = g->matList.materials[i]){
			bool seen = false; for(auto &o : m.orig) seen |= o.first == mat;
			if(!seen) m.orig.push_back({mat, mat->color});
		}
	}
	return m;
}
void artModApply(Clump *c)
{
	ArtMod &m = artMod(c);
	for(auto &o : m.orig) o.first->color = {uint8(o.second.red * m.grey / 255), uint8(o.second.green * m.grey / 255), uint8(o.second.blue * m.grey / 255), uint8(o.second.alpha * m.alpha)};
}
}
void rm8SceneTintArticle(Clump *c, int grey)
{
	if(!c) return;
	if(grey >= 255 && g_artMod.find(c) == g_artMod.end()) return;
	artMod(c).grey = grey; artModApply(c);
}
void rm8SceneSetArticleOpacity(Clump *c, float a)
{
	if(!c) return;
	if(a >= 1.0f && g_artMod.find(c) == g_artMod.end()) return;
	ArtMod &m = artMod(c);
	if(m.alpha == a) return;
	m.alpha = a; artModApply(c);
}
float rm8SceneArticleOpacity(Clump *c){ auto it = g_artMod.find(c); return it == g_artMod.end() ? 1.0f : it->second.alpha; }

// The faded articles (see-through) are drawn AFTER the roles (rm8RenderArticleGlass): depth test on, depth write off, alpha blended. The roles were drawn first without them
// in the depth buffer, so a role behind a faded article shines through at 60 %, while a role in front of it still occludes it (its depth is in the buffer).
static std::vector<Clump*> g_glassList;
void rm8RenderArticles(Rm8Scene &s, bool deferGlass)
{
	g_glassList.clear();
	for(int pass = 0; pass < 2; pass++)
		for(Clump *c : s.clumps){
			if(!g_artMod.empty() && rm8SceneArticleOpacity(c) < 1.0f){ if(pass == 0 && deferGlass) g_glassList.push_back(c); if(deferGlass) continue; }
			FORLIST(lnk, c->atomics) renderArticleAtomic(Atomic::fromClump(lnk), pass);
		}
}

void rm8RenderArticleGlass()
{
	if(g_glassList.empty()) return;
	uint32 zw = GetRenderState(ZWRITEENABLE), zt = GetRenderState(ZTESTENABLE), sb = GetRenderState(SRCBLEND), db = GetRenderState(DESTBLEND), vb = GetRenderState(VERTEXALPHA);
	uint32 atf = GetRenderState(ALPHATESTFUNC), atr = GetRenderState(ALPHATESTREF), cm = GetRenderState(CULLMODE);
	// z write ON (nothing is drawn behind them any more): within the article the nearest layer wins, so a closed mesh does not show its far side through its near side
	SetRenderState(ZTESTENABLE, 1); SetRenderState(ZWRITEENABLE, 1); SetRenderState(VERTEXALPHA, 1); SetRenderState(SRCBLEND, BLENDSRCALPHA); SetRenderState(DESTBLEND, BLENDINVSRCALPHA);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL); SetRenderState(ALPHATESTREF, 0);
	g_glassMode = true;
	for(int pass = 0; pass < 2; pass++) for(Clump *c : g_glassList) if(rm8SceneArticleOpacity(c) < 1.0f){ FORLIST(lnk, c->atomics) renderArticleAtomic(Atomic::fromClump(lnk), pass); }
	g_glassMode = false;
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(SRCBLEND, sb); SetRenderState(DESTBLEND, db); SetRenderState(VERTEXALPHA, vb);
	SetRenderState(ALPHATESTFUNC, atf); SetRenderState(ALPHATESTREF, atr); SetRenderState(CULLMODE, cm);
	g_glassList.clear();
}

// [V] BuildObj_SetAlpha @0x4efb20 -> Terrain_SetAlpha(pad, 0x60 / 0xFF): the alpha of every vertex of the plot pad
void rm8SceneSetTerraAlpha(Rm8Scene &s, int terra, unsigned char alpha)
{
	auto it = s.terraPads.find(terra);
	if(it == s.terraPads.end()) return;
	for(auto &pr : it->second){
		Geometry *g = pr.a->geometry;
		g->lock(Geometry::LOCKPRELIGHT);
		for(int k = 0; k < pr.count; k++) g->colors[pr.first + k].alpha = alpha;
		g->unlock();
	}
}

void rm8RenderArticleClump(Clump *c)
{
	if(!c) return;
	for(int pass = 0; pass < 2; pass++) FORLIST(lnk, c->atomics) renderArticleAtomic(Atomic::fromClump(lnk), pass);
}

Clump *rm8LoadClump(const std::string &dff, const std::string &texdir){ return loadArticle(dff, texdir, "-"); }
Animation *rm8LoadAnim(const std::string &key){ return loadAnim(key); }
Texture *rm8PackTexture(const std::string &key){ return texFromPackKey(key); }
bool rm8PackRead(const std::string &key, std::vector<uint8> &out){ std::string k = lower(key); return g_tex1.read(k, out) || g_tex0.read(k, out); }

// librw's GL3 pipeline does not apply MatFX UV transforms, so animated UVs are written into the vertices
// (only a few billboard/water quads use them).
static int collectUVAnims(Rm8Scene &s, Clump *c)
{
	int n = 0;
	FORLIST(lnk, c->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		if(!g || !g->meshHeader || g->numTexCoordSets < 1) continue;
		Mesh *m = g->meshHeader->getMeshes();
		for(int i = 0; i < g->meshHeader->numMeshes; i++){
			Material *mat = m[i].material;
			if(!mat || !UVAnim::exists(mat)) continue;
			Rm8UVTarget t; t.geo = g; t.mat = mat;
			std::vector<bool> seen(g->numVertices, false);
			for(uint32 k = 0; k < m[i].numIndices; k++){
				uint16 v = m[i].indices[k];
				if(v < g->numVertices && !seen[v]){ seen[v] = true; t.verts.push_back(v); t.base.push_back(g->texCoords[0][v]); }
			}
			if(std::find(s.uvMats.begin(), s.uvMats.end(), mat) == s.uvMats.end()) s.uvMats.push_back(mat);
			s.uvTargets.push_back(std::move(t));
			n++;
		}
	}
	return n;
}

static void updateUVAnims(Rm8Scene &s, float dt)
{
	static const bool off = getenv("RM8_NO_UVANIM") != nullptr;	// testing switch
	if(off) return;
	for(Material *mat : s.uvMats){ UVAnim::addTime(mat, dt); UVAnim::applyUpdate(mat); }
	for(Rm8UVTarget &t : s.uvTargets){
		Matrix *M = PLUGINOFFSET(UVAnim, t.mat, uvAnimOffset)->uv[0];
		if(!M) continue;
		for(size_t k = 0; k < t.verts.size(); k++){
			TexCoords b = t.base[k];
			// [A] RenderWare 2D texture transform: (u v 1) x [right; up; pos]
			t.geo->texCoords[0][t.verts[k]] = {b.u * M->right.x + b.v * M->up.x + M->pos.x,
			                                   b.u * M->right.y + b.v * M->up.y + M->pos.y};
		}
		t.geo->lock(Geometry::LOCKTEXCOORDS);
	}
}

// ---- ground (flat-tile texture composition, see scripts/export_scene.py and Material_InitRender @0x433440) ----
struct GroundLeaf { int id; float x0, z0, x1, z1; std::string lm; float m[16]; };
struct GroundData {
	std::map<int, std::string> landKeys;			// landtex index -> pack key
	std::vector<std::vector<std::pair<int,int>>> tiles;	// per tile (y*W+x): (texture index, cell) layers in draw order
	std::vector<std::vector<uint32>> vcol;			// vertex colours 0xRRGGBB
	std::vector<GroundLeaf> leaves;
	std::map<std::pair<int, std::string>, std::pair<std::vector<V>, std::vector<uint16>>> cliffs;	// (leaf, texture) -> mesh
	std::vector<std::pair<std::string, std::vector<V>>> roadQuads;	// route texture, 4 corners
};

static Image *toRGBA32(Image *img)
{
	if(!img || img->depth == 32) return img;
	Image *o = Image::create(img->width, img->height, 32);
	o->allocate();
	for(int y = 0; y < img->height; y++)
		for(int x = 0; x < img->width; x++){
			uint8 *d = o->pixels + y*o->stride + x*4;
			if(img->depth == 24){ uint8 *s = img->pixels + y*img->stride + x*3; d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; }
			else if(img->depth <= 8 && img->palette){ uint8 *c = img->palette + img->pixels[y*img->stride + x]*4; memcpy(d, c, 4); }
			else { d[0] = d[1] = d[2] = 128; d[3] = 255; }
		}
	img->destroy();
	return o;
}

static void buildGround(Rm8Scene &s, const std::string &sdir, const std::vector<std::vector<float>> &hgt, GroundData &g)
{
	const int CELL = 64, PAGE = 2048, PER_ROW = PAGE / CELL, PER_PAGE = PER_ROW * PER_ROW;
	std::map<int, Image*> land;
	for(auto &k : g.landKeys){
		std::vector<uint8> buf;
		if(g_tex1.read(k.second, buf) || g_tex0.read(k.second, buf)) land[k.first] = toRGBA32(readTGAMemory(buf.data(), (uint32)buf.size()));
		if(!land[k.first]) rm8Log("warning: land texture %s missing", k.second.c_str());
	}
	// compose unique layer combinations into atlas pages
	std::map<std::vector<std::pair<int,int>>, int> slotOf;
	std::vector<Image*> pages;
	std::vector<int> tileSlot(g.tiles.size(), -1);
	for(size_t i = 0; i < g.tiles.size(); i++){
		auto it = slotOf.find(g.tiles[i]);
		if(it != slotOf.end()){ tileSlot[i] = it->second; continue; }
		int slot = (int)slotOf.size();
		slotOf[g.tiles[i]] = slot; tileSlot[i] = slot;
		if(slot / PER_PAGE >= (int)pages.size()){
			Image *p = Image::create(PAGE, PAGE, 32); p->allocate(); memset(p->pixels, 0, p->stride * PAGE); pages.push_back(p);
		}
		Image *page = pages[slot / PER_PAGE];
		int ox = (slot % PER_PAGE) % PER_ROW * CELL, oy = (slot % PER_PAGE) / PER_ROW * CELL;
		for(auto &l : g.tiles[i]){
			Image *src = land.count(l.first) ? land[l.first] : nullptr;
			if(!src) continue;
			int c = l.second, sx = (c % 4) * CELL + (c >= 16 ? 256 : 0), sy = ((c % 16) / 4) * CELL;	// DAT_00604138
			for(int y = 0; y < CELL; y++){
				uint8 *d = page->pixels + (oy + y) * page->stride + ox * 4;
				const uint8 *sp = src->pixels + (sy + y) * src->stride + sx * 4;
				for(int x = 0; x < CELL; x++, d += 4, sp += 4){
					int a = sp[3];
					d[0] = (uint8)((d[0] * (255 - a) + sp[0] * a) / 255);
					d[1] = (uint8)((d[1] * (255 - a) + sp[1] * a) / 255);
					d[2] = (uint8)((d[2] * (255 - a) + sp[2] * a) / 255);
					d[3] = 255;
				}
			}
		}
	}
	for(auto &l : land) if(l.second) l.second->destroy();
	std::vector<Texture*> pageTex;
	for(Image *p : pages){
		Texture *t = Texture::create(Raster::createFromImage(p));
		p->destroy();
		t->setFilter(Texture::LINEAR); t->setAddressU(Texture::CLAMP); t->setAddressV(Texture::CLAMP);
		pageTex.push_back(t);
	}
	// one atomic per quadtree leaf, one material per atlas page; lightmap as second texture (lmpipe.cpp)
	for(GroundLeaf &lf : g.leaves){
		int gx0 = (int)lroundf((lf.x0 + s.MW * 25) / 50), gz0 = (int)lroundf((lf.z0 + s.MH * 25) / 50);
		int nx = (int)lroundf((lf.x1 - lf.x0) / 50), nz = (int)lroundf((lf.z1 - lf.z0) / 50);
		int nt = 0;
		for(int j = 0; j < nz; j++) for(int i = 0; i < nx; i++) if(!g.tiles[(gz0 + j) * s.MW + gx0 + i].empty()) nt++;
		std::map<int,int> matOf;
		for(int j = 0; j < nz; j++) for(int i = 0; i < nx; i++){
			if(g.tiles[(gz0 + j) * s.MW + gx0 + i].empty()) continue;
			int sl = tileSlot[(gz0 + j) * s.MW + gx0 + i];
			if(!matOf.count(sl / PER_PAGE)) { int k = (int)matOf.size(); matOf[sl / PER_PAGE] = k; }
		}
		Geometry *geo = Geometry::create(nt * 4, nt * 2, Geometry::POSITIONS | Geometry::TEXTURED | Geometry::PRELIT | Geometry::MODULATE | Geometry::NORMALS | Geometry::LIGHT);
		MorphTarget *mt = &geo->morphTargets[0];
		int v = 0, t = 0;
		for(int j = 0; j < nz; j++) for(int i = 0; i < nx; i++){
			int tx = gx0 + i, tz = gz0 + j;
			if(g.tiles[tz * s.MW + tx].empty()) continue;	// cliff tile: template mesh instead
			int sl = tileSlot[tz * s.MW + tx], loc = sl % PER_PAGE;
			float u0 = ((loc % PER_ROW) * CELL + 0.5f) / PAGE, u1 = ((loc % PER_ROW) * CELL + CELL - 0.5f) / PAGE;
			float v0 = ((loc / PER_ROW) * CELL + 0.5f) / PAGE, v1 = ((loc / PER_ROW) * CELL + CELL - 0.5f) / PAGE;
			for(int c = 0; c < 4; c++){	// corners (x,y) (x+1,y) (x,y+1) (x+1,y+1); UV (i%2, i/2) as Render_TerraObject
				int cx = tx + c % 2, cz = tz + c / 2;
				mt->vertices[v + c] = { cx * 50.0f - s.MW * 25, hgt[cz][cx], cz * 50.0f - s.MH * 25 };
				{	// vertex normal of the height field (Map_CalcVertexNormal @0x423020: the average of the surrounding tile normals; central differences here)
					int xl = std::max(0, cx - 1), xr = std::min(s.MW, cx + 1), zl = std::max(0, cz - 1), zr = std::min(s.MH, cz + 1);
					float dx = (hgt[cz][xr] - hgt[cz][xl]) / ((xr - xl) * 50.0f), dz = (hgt[zr][cx] - hgt[zl][cx]) / ((zr - zl) * 50.0f);
					float l = sqrtf(dx * dx + 1.0f + dz * dz);
					mt->normals[v + c] = { -dx / l, 1.0f / l, -dz / l };
				}
				geo->texCoords[0][v + c] = { c % 2 ? u1 : u0, c / 2 ? v1 : v0 };
				uint32 col = g.vcol.empty() ? 0xFFFFFF : g.vcol[cz][cx];
				geo->colors[v + c] = { uint8(col >> 16), uint8(col >> 8), uint8(col), 255 };
			}
			uint16 m = (uint16)matOf[sl / PER_PAGE];
			geo->triangles[t].v[0] = v; geo->triangles[t].v[1] = v + 2; geo->triangles[t].v[2] = v + 1; geo->triangles[t].matId = m; t++;
			geo->triangles[t].v[0] = v + 1; geo->triangles[t].v[1] = v + 2; geo->triangles[t].v[2] = v + 3; geo->triangles[t].matId = m; t++;
			v += 4;
		}
		std::vector<std::pair<int,int>> mats(matOf.begin(), matOf.end());
		std::sort(mats.begin(), mats.end(), [](const std::pair<int,int> &a, const std::pair<int,int> &b){ return a.second < b.second; });
		for(auto &pm : mats){
			Material *mat = Material::create();
			mat->setTexture(pageTex[pm.first]);
			geo->matList.appendMaterial(mat);
			mat->destroy();
		}
		geo->calculateBoundingSphere();
		geo->unlock();
		Atomic *a = Atomic::create();
		a->setGeometry(geo, 0);
		geo->destroy();
		a->setFrame(Frame::create());
		s.world->addAtomic(a);
		Texture *lmTex = texFromFile(sdir + "/" + lf.lm);
		rm8LmAttach(a, lmTex, lf.m, true);
		{	// [V] Route_RenderSegment @0x436810: road quads (vertex colour 0xFFFFFFFF, normals) go into the quadtree
			// leaf's vertex buffer (QuadTree_AddUniqueItemToLeaf), so they are drawn like the terrain: lightmap MODULATE2X
			std::map<std::string, std::pair<std::vector<V>, std::vector<uint16>>> rm;
			for(auto &q : g.roadQuads){
				float cx = (q.second[0].x + q.second[3].x) / 2, cz = (q.second[0].z + q.second[3].z) / 2;
				if(cx < lf.x0 || cx >= lf.x1 || cz < lf.z0 || cz >= lf.z1) continue;
				auto &m = rm[q.first]; uint16 base = (uint16)m.first.size();
				m.first.insert(m.first.end(), q.second.begin(), q.second.end());
				// [V] index list {0,2,1, 1,2,3}
				m.second.insert(m.second.end(), {uint16(base), uint16(base + 2), uint16(base + 1), uint16(base + 1), uint16(base + 2), uint16(base + 3)});
			}
			for(auto &r : rm){
				Texture *rt = texFromPackKey(r.first);
				if(!rt){ rm8Log("warning: road texture %s not found", r.first.c_str()); g_texMissing++; }
				Atomic *ra = makeMesh(s.world, r.second.first, r.second.second, rt, {255, 255, 255, 255}, true);
				rm8LmAttach(ra, lmTex, lf.m, true);
				s.atomics.push_back(ra);
			}
		}
		for(auto &c : g.cliffs){	// cliff template meshes of this leaf
			if(c.first.first != lf.id) continue;
			Texture *ct = texFromPackKey(c.first.second);
			Atomic *ca = makeMesh(s.world, c.second.first, c.second.second, ct, {255, 255, 255, 255}, true);
			rm8LmAttach(ca, lmTex, lf.m, true);
			s.atomics.push_back(ca); s.nCliffs++;
		}
		if(lmTex) lmTex->destroy();	// rm8LmAttach keeps its own reference
		s.atomics.push_back(a); s.nLeaf++;
	}
	for(Texture *t : pageTex) t->destroy();	// materials hold the references
}

// [V] FUN_0043cb90: all post-concat on the clump frame: T(-P0), S, R_y(ry), T(P0 + pos)
Clump *rm8SceneAddArticle(Rm8Scene &s, const std::string &dff, const std::string &td, const float pos[3], const float scl[3], float ry,
                          const std::string &anim, const std::string &uva, const std::string &mode)
{
	Clump *c = loadArticle(dff, td, uva);
	if(!c){ s.nArtFail++; rm8Log("warning: cannot load %s", dff.c_str()); return nullptr; }
	std::vector<Atomic*> demoted = demoteBigSkins(c);
	s.nSkinToCpu += (int)demoted.size();
	Frame *f = c->getFrame();
	V3d p0 = f->matrix.pos, np0 = {-p0.x, -p0.y, -p0.z}, sc = {scl[0], scl[1], scl[2]}, ay = {0, 1, 0};
	V3d p1 = {p0.x + pos[0], p0.y + pos[1], p0.z + pos[2]};
	f->translate(&np0, COMBINEPOSTCONCAT); f->scale(&sc, COMBINEPOSTCONCAT);
	f->rotate(&ay, ry, COMBINEPOSTCONCAT); f->translate(&p1, COMBINEPOSTCONCAT);
	s.world->addClump(c); s.clumps.push_back(c); s.clumpNames.push_back(dff); s.nArt++;
	if(anim != "-"){ if(setupAnim(s, c, anim, demoted, dff, mode == "once", mode == "loopr")) s.nAnimated++; else s.nAnimFail++; }
	if(uva != "-") s.nUVAnimated += collectUVAnims(s, c) > 0;
	return c;
}

// the clump leaves the scene's article list (it is no longer drawn with the scenery) but keeps its animation / UV animation and the world membership; the owner draws it
// (present.cpp overlay: [V] Cls_5e3af0__vf2_RenderMesh @0x4B1420 clears the z buffer and draws the UFO mesh over everything); rm8SceneRemoveArticle still destroys it
void rm8SceneDetachArticle(Rm8Scene &s, Clump *c)
{
	for(size_t i = 0; i < s.clumps.size(); i++) if(s.clumps[i] == c){ s.clumps.erase(s.clumps.begin() + i); s.clumpNames.erase(s.clumpNames.begin() + i); return; }
}

void rm8SceneSetArticleActive(Rm8Scene &s, Clump *c, bool on)
{
	if(!c) return;
	if(on){
		for(size_t i = 0; i < s.hiddenArts.size(); i++) if(s.hiddenArts[i].first == c){
			s.clumps.push_back(c); s.clumpNames.push_back(s.hiddenArts[i].second);
			s.mapArticles.push_back(c);
			s.hiddenArts.erase(s.hiddenArts.begin() + i);
			return;
		}
		return;
	}
	for(size_t i = 0; i < s.clumps.size(); i++) if(s.clumps[i] == c){
		s.hiddenArts.push_back({c, s.clumpNames[i]});
		s.clumps.erase(s.clumps.begin() + i); s.clumpNames.erase(s.clumpNames.begin() + i);
		s.mapArticles.erase(std::remove(s.mapArticles.begin(), s.mapArticles.end(), c), s.mapArticles.end());
		return;
	}
}

void rm8SceneRemoveArticle(Rm8Scene &s, Clump *c)
{
	if(!c) return;
	for(size_t i = 0; i < s.clumps.size(); i++) if(s.clumps[i] == c){ s.clumps.erase(s.clumps.begin() + i); s.clumpNames.erase(s.clumpNames.begin() + i); break; }
	for(size_t i = 0; i < s.anims.size(); i++) if(s.anims[i].clump == c){ s.anims.erase(s.anims.begin() + i); break; }
	FORLIST(lnk, c->atomics) g_atomFlags.erase(Atomic::fromClump(lnk));
	g_artMod.erase(c);
	// UV animations of the removed clump (collectUVAnims): its geometries and materials die with it
	FORLIST(lnk, c->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		s.uvTargets.erase(std::remove_if(s.uvTargets.begin(), s.uvTargets.end(), [g](const Rm8UVTarget &t){ return t.geo == g; }), s.uvTargets.end());
		if(g && g->meshHeader) for(int i = 0; i < g->meshHeader->numMeshes; i++){
			Material *mat = g->meshHeader->getMeshes()[i].material;
			bool used = false; for(const Rm8UVTarget &t : s.uvTargets) used |= t.mat == mat;
			if(!used) s.uvMats.erase(std::remove(s.uvMats.begin(), s.uvMats.end(), mat), s.uvMats.end());
		}
	}
	s.world->removeClump(c);
	c->destroy();
}

// [V] vf23 Article_SelectByTypeAndSetProperties: switch the clump's animation (once = play once and hold)
bool rm8SceneArticleAnim(Rm8Scene &s, Clump *c, const std::string &anim, bool once)
{
	for(Rm8ArtAnim &aa : s.anims) if(aa.clump == c){
		Animation *an = loadAnim(anim);
		if(!an || an->getNumNodes() != aa.hier->numNodes) return false;
		aa.hier->interpolator->setCurrentAnim(an); aa.once = once;
		return true;
	}
	return false;
}

bool rm8LoadScene(const std::string &sdir, Rm8Scene &s)
{
	std::ifstream sf(sdir + "/scene.txt");
	if(!sf){ rm8Log("no %s/scene.txt", sdir.c_str()); return false; }
	{ size_t q = sdir.find_last_of("/\\"); s.mapId = q == std::string::npos ? sdir : sdir.substr(q + 1); }
	s.world = World::create();
	std::vector<std::vector<float>> hgt;
	GroundData gd;
	std::map<std::string, std::pair<std::vector<V>, std::vector<uint16>>> roads;
	std::map<std::string, std::vector<std::pair<int, int>>> padTerra;	// pad mesh -> (terra, first vertex of a quad)
	std::string line;
	Clump *lastArt = nullptr;	// the article the next "prt" lines attach to
	std::string lastAnim = "-";	// its idle animation (action 0), for the "aact" lines
	while(std::getline(sf, line)){
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "map"){ ss >> s.MW >> s.MH; hgt.assign(s.MH + 1, std::vector<float>(s.MW + 1, 0)); }
		else if(kw == "ambient"){ float r, g, b, a; ss >> r >> g >> b >> a;
			Light *l = Light::create(Light::AMBIENT); l->setColor(r, g, b); s.world->addLight(l); s.lights.push_back(l); }
		else if(kw == "directional"){ float r, g, b, a, pitch, yaw; ss >> r >> g >> b >> a >> pitch >> yaw;
			// [V] FUN_0041c240: rotate the light frame about (1,0,0) by pitch, then about (0,1,0) by yaw
			Light *l = Light::create(Light::DIRECTIONAL); l->setColor(r, g, b);
			Frame *f = Frame::create(); V3d ax = {1, 0, 0}, ay = {0, 1, 0};
			f->rotate(&ax, pitch, COMBINEREPLACE); f->rotate(&ay, yaw, COMBINEPOSTCONCAT);
			l->setFrame(f); s.world->addLight(l); s.lights.push_back(l); }
		else if(kw == "vrow"){ int y; ss >> y; for(int x = 0; x <= s.MW; x++) ss >> hgt[y][x]; }
		else if(kw == "vcrow"){ int y; ss >> y; if(gd.vcol.empty()) gd.vcol.assign(s.MH + 1, std::vector<uint32>(s.MW + 1, 0xFFFFFF));
			for(int x = 0; x <= s.MW; x++){ std::string h; ss >> h; gd.vcol[y][x] = (uint32)strtoul(h.c_str(), nullptr, 16); } }
		else if(kw == "cliff"){ int leaf, nv, nt; std::string tex; ss >> leaf >> tex >> nv >> nt;
			auto &m = gd.cliffs[{leaf, tex}]; uint16 base = (uint16)m.first.size();
			for(int k = 0; k < nv; k++){ V v; ss >> v.x >> v.y >> v.z >> v.u >> v.v; m.first.push_back(v); }
			for(int k = 0; k < nt * 3; k++){ int i; ss >> i; m.second.push_back(uint16(base + i)); } }
		else if(kw == "landtex"){ int i; std::string key; ss >> i >> key; gd.landKeys[i] = key; }
		else if(kw == "rnode"){ Rm8RoadNode n; ss >> n.tx >> n.ty >> n.x >> n.y >> n.z >> n.road >> n.event; if(!(ss >> n.terra)) n.terra = -1; s.rnodes.push_back(n); }
		else if(kw == "tile"){ int x, y, t, c; ss >> x >> y; if(gd.tiles.empty()) gd.tiles.resize(s.MW * s.MH);
			auto &L = gd.tiles[y * s.MW + x]; while(ss >> t >> c) L.push_back({t, c}); }
		else if(kw == "leaf"){ GroundLeaf lf; ss >> lf.id >> lf.x0 >> lf.z0 >> lf.x1 >> lf.z1 >> lf.lm; for(int k = 0; k < 16; k++) ss >> lf.m[k]; gd.leaves.push_back(lf); }
		else if(kw == "road" || kw == "pad"){	// pad: plot (terra) pads from scripts/plots.py, same quad layout
			std::string tex; ss >> tex;
			if(kw == "road"){ std::vector<V> q(4); for(V &v : q) ss >> v.x >> v.y >> v.z >> v.u >> v.v; gd.roadQuads.push_back({tex, q}); s.nRoadQuads++; continue; }
			auto &r = roads["pad:" + tex]; uint16 base = (uint16)r.first.size();
			for(int i = 0; i < 4; i++){ V v; ss >> v.x >> v.y >> v.z >> v.u >> v.v; r.first.push_back(v); }
			int ti; if(ss >> ti && ti >= 0) padTerra["pad:" + tex].push_back({ti, (int)base});
			// [V] Route_RenderSegment index list {0,2,1, 1,2,3}
			r.second.insert(r.second.end(), {uint16(base), uint16(base + 2), uint16(base + 1), uint16(base + 1), uint16(base + 2), uint16(base + 3)});
			if(kw == "road") s.nRoadQuads++; else s.nPadQuads++;
		}
		else if(kw == "article"){
			std::string dff, td, anim = "-", uva = "-", mode = "loop"; float pos[3], sc[3], ry; ss >> dff >> td >> pos[0] >> pos[1] >> pos[2] >> sc[0] >> sc[1] >> sc[2] >> ry >> anim >> uva >> mode;
			Clump *c = rm8SceneAddArticle(s, dff, td, pos, sc, ry, anim, uva, mode);
			lastArt = c;
			lastAnim = anim;
			if(!c) continue;
			std::string opt;
			static const bool noArtLm = getenv("RM8_NO_ARTLM") != nullptr;	// debugging switch
			if(!noArtLm && ss >> opt && opt == "lm"){	// baked article lightmap (.ltm segment B), MODULATE2X like the terrain
				std::string fn; float lm[16]; ss >> fn; for(int k = 0; k < 16; k++) ss >> lm[k];
				Texture *lt = texFromFile(sdir + "/" + fn);
				if(lt){ FORLIST(lnk, c->atomics) rm8LmAttach(Atomic::fromClump(lnk), lt, lm); lt->destroy(); s.nArtLm++; }
			}
		}
		else if(kw == "icon"){	// event icon of the article above: <tile x> <tile y> <action>:<anm> ... (scripts/export_scene.py)
			Rm8Icon ic; std::string a; ss >> ic.tx >> ic.ty;
			while(ss >> a){ size_t q = a.find(':'); if(q != std::string::npos) ic.anims[atoi(a.c_str())] = a.substr(q + 1); }
			ic.clump = lastArt; if(lastArt) s.icons.push_back(ic);
		}
		else if(kw == "itex" || kw == "ifx"){	// timed events of the icon above (role.bin lists 3 / 4, see boardfx.cpp): itex <action> <time> <material> <texture key> / ifx <action> <time> <effect> <hook> <type>
			if(s.icons.empty() || s.icons.back().clump != lastArt) continue;
			Rm8Icon &ic = s.icons.back();
			if(kw == "itex"){ Rm8IconTex e; ss >> e.action >> e.time >> e.material >> e.texture; if(ss) ic.tex.push_back(e); }
			else { Rm8IconFx e; ss >> e.action >> e.time >> e.effect >> e.hook >> e.type; if(ss) ic.fx.push_back(e); }
		}
		else if(kw == "aown"){ int role; if(lastArt && (ss >> role)) s.artOwner[lastArt] = role; }	// owner role of the article above (scripts/export_scene.py, article record [17])
		else if(kw == "aact" || kw == "asnd"){	// map article actions / sound events (role.bin sub-lists 1 / 4), see boardfx.cpp
			if(!lastArt) continue;
			Rm8ArtActs &aa = s.artActs[lastArt];
			if(aa.anims.empty()) aa.anims[0] = lastAnim;
			if(kw == "aact"){ std::string a; while(ss >> a){ size_t q = a.find(':'); if(q != std::string::npos) aa.anims[atoi(a.c_str())] = a.substr(q + 1); } }
			else { Rm8ArtSound e; if(ss >> e.action >> e.sound >> e.flag >> e.time) aa.sounds.push_back(e); }
		}
		else if(kw == "terra"){ Rm8TerraDef t; int idx; ss >> idx >> t.type >> t.street >> t.pos[0] >> t.pos[1] >> t.pos[2] >> t.yaw >> t.level0; s.terras.push_back(t); }
		else if(kw == "building"){ int id, lv, art; ss >> id >> lv >> art; s.buildingArt[{id, lv}] = art; }
		else if(kw == "street"){ int id, price; std::string nm; ss >> id >> price >> nm; s.streetPrice[id] = price; s.streetName[id] = nm == "-" ? "" : nm; }
		else if(kw == "bname"){ int id; std::string nm; ss >> id >> nm; s.buildingName[id] = nm; }
		else if(kw == "bart"){
			int art; Rm8ArtDef d; std::string a; ss >> art >> d.dff >> d.texdir;
			while(ss >> a){ size_t c = a.find(':'); if(c != std::string::npos) d.anims[atoi(a.c_str())] = a.substr(c + 1); }
			s.arts[art] = d;
		}
		else if(kw == "prt" || kw == "prtat"){	// particle effect on the article above ([@hook] frame) or at a map position
			if(!rm8PrtLoadDefs(sdir + "/../particles.txt")) continue;
			int eff; ss >> eff;
			if(kw == "prt"){ int hook; ss >> hook; if(lastArt) s.nPrt += rm8PrtAttach(eff, lastArt, hook); }
			else { float p[3]; ss >> p[0] >> p[1] >> p[2]; s.nPrt += rm8PrtPlace(eff, p); }
		}
		else if(kw == "water"){
			float y, r, g, b, a, bump = 0.04f, dir[3] = {0, 1, 0}, speed = 0.2f, lscale = 1.0f; std::string tex;
			ss >> y >> r >> g >> b >> a >> tex >> bump >> dir[0] >> dir[1] >> dir[2] >> speed >> lscale;
			float ex = s.MW * 25.0f, ez = s.MH * 25.0f;
			std::vector<V> vs = {{-ex, y, -ez, 0, 0}, {ex, y, -ez, 1, 0}, {-ex, y, ez, 0, 1}, {ex, y, ez, 1, 1}};
			std::vector<uint16> ts = {0, 2, 1, 1, 2, 3};
			Texture *sky = texFromPackKey(tex);
			if(!sky){ rm8Log("warning: water sky texture %s not found", tex.c_str()); g_texMissing++; }
			Atomic *wa = makeMesh(s.world, vs, ts, nullptr, {uint8(r * 255), uint8(g * 255), uint8(b * 255), uint8(a * 255)});
			rm8WaterSetup(wa, sky, y, bump, dir, speed, lscale, texFromPackKey);
			s.atomics.push_back(wa);
		}
	}
	for(Clump *c : s.clumps){ bool icon = false; for(const Rm8Icon &ic : s.icons) icon |= ic.clump == c; if(!icon) s.mapArticles.push_back(c); }	// the scenery articles (rm8BoardFxUpdate: see-through)
	buildGround(s, sdir, hgt, gd);
	for(auto &r : roads){
		bool pad = r.first.compare(0, 4, "pad:") == 0;
		std::string key = pad ? r.first.substr(4) : r.first;
		Texture *t = texFromPackKey(key);
		if(!t){ rm8Log("warning: road/pad texture %s not found", key.c_str()); g_texMissing++; }
		RGBA col = pad ? RGBA{255, 255, 255, 255} : RGBA{230, 230, 230, 255};
		s.atomics.push_back(makeMesh(s.world, r.second.first, r.second.second, t, col));
		for(auto &pt : padTerra[r.first]) s.terraPads[pt.first].push_back({s.atomics.back(), pt.second, 4});
	}
	// pose every animated article at t = 0: skinned atomics read hier->matrices, which are undefined until computed
	rm8UpdateScene(s, 0.0f);
	rm8PrtUpdate(1.0f);	// [V] 0x4A33D0: one Article_UpdateEffect(1.0) at the end of the map load
	return true;
}

void rm8DestroyScene(Rm8Scene &s)
{
	if(!s.world) return;
	rm8LmClear();
	rm8WaterClear();
	rm8PrtClear();
	rm8BoardFxClear();
	g_atomFlags.clear();
	for(Atomic *a : s.atomics){ Frame *f = a->getFrame(); s.world->removeAtomic(a); a->destroy(); if(f) f->destroy(); }
	for(Clump *c : s.clumps){ s.world->removeClump(c); c->destroy(); }
	for(auto &h : s.hiddenArts){ s.world->removeClump(h.first); h.first->destroy(); }
	for(Light *l : s.lights){ Frame *f = l->getFrame(); s.world->removeLight(l); l->destroy(); if(f) f->destroy(); }
	s.world->destroy();
	s = Rm8Scene();
}

void rm8RenderScene(Rm8Scene &s, bool deferGlass)
{
	for(Atomic *a : s.atomics) if(a->geometry->matList.materials[0]->color.alpha == 255) a->render();	// opaque first
	static const char *hideV = getenv("RM8_HIDE_VERTS");	// debugging: hide atomics with this vertex count
	if(!hideV) rm8RenderArticles(s, deferGlass);
	else for(Clump *c : s.clumps){ FORLIST(lnk, c->atomics){ Atomic *a = Atomic::fromClump(lnk); if(a->geometry->numVertices != atoi(hideV)) a->render(); } }
	static const bool noWater = getenv("RM8_NO_WATER") != nullptr;	// debugging switch
	if(!noWater) for(Atomic *a : s.atomics) if(a->geometry->matList.materials[0]->color.alpha != 255) a->render();	// water last
}

void rm8PlaceCamera(Camera *cam, const Rm8Scene &s, float yawDeg, float pitchDeg, float distFactor, float tx, float tz)
{
	float ext = s.MW * 25.0f * 1.42f;
	cam->setNearPlane(10.0f); cam->setFarPlane(ext * 6.0f);
	float yr = yawDeg * 3.14159265f / 180.0f, pr = pitchDeg * 3.14159265f / 180.0f, dist = ext * 2.0f * distFactor;
	V3d center = {tx, 0, tz};
	V3d eye = {center.x + dist * cosf(pr) * sinf(yr), center.y + dist * sinf(pr), center.z - dist * cosf(pr) * cosf(yr)};
	V3d fwd = normalize(sub(center, eye)), up = {0, 1, 0};
	Matrix m; m.setIdentity(); m.lookAt(fwd, up); m.pos = eye;
	cam->getFrame()->transform(&m, COMBINEREPLACE);
}

void rm8SceneSetTerraColor(Rm8Scene &s, int terra, RGBA c)
{
	auto it = s.terraPads.find(terra);
	if(it == s.terraPads.end()) return;
	for(auto &pr : it->second){
		Geometry *g = pr.a->geometry;
		g->lock(Geometry::LOCKPRELIGHT);	// re-uploaded at the next render
		for(int k = 0; k < pr.count; k++) g->colors[pr.first + k] = c;
		g->unlock();
	}
}
