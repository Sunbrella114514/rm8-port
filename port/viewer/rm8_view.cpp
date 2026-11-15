// rm8_view: render RichMan8 RenderWare models with librw (GL3) and save PNG screenshots.
// Reads the original packs directly (no unpacking).
// usage: rm8_view <game_dir> <out.png> <model path inside clump pack> [more models...] [--yaw deg] [--pitch deg] [--size WxH]
//   textures are looked up by name inside Texture/tex0000.dat (+ tex0001.dat) and decoded with librw's TGA/BMP readers
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <rw.h>
#include <GLFW/glfw3.h>

using namespace rw;

// ---- pack reader (format A, see scripts/rm8pak.py) ----
struct Pack {
	FILE *f = nullptr;
	std::map<std::string, std::pair<uint32, uint32>> byName;   // lower-case path -> (off, size)
	std::map<std::string, std::string> byBase;                  // lower-case basename w/o ext -> full key (first wins)
	bool open(const std::string &path){
		f = fopen(path.c_str(), "rb");
		if(!f) return false;
		uint32 h[5]; fread(h, 4, 5, f);
		std::vector<uint32> idx(h[3] * 4); fread(idx.data(), 4, idx.size(), f);
		for(uint32 i = 0; i < h[3]; i++){
			uint32 nl = idx[i*4], off = idx[i*4+1], sz = idx[i*4+2];
			std::string n(nl, '\0'); fseek(f, off, SEEK_SET); fread(&n[0], 1, nl, f);
			std::transform(n.begin(), n.end(), n.begin(), ::tolower);
			byName[n] = {off + nl, sz};
			size_t s = n.find_last_of('\\'); std::string b = n.substr(s == std::string::npos ? 0 : s + 1);
			size_t d = b.rfind('.'); std::string stem = b.substr(0, d);
			if(!byBase.count(stem)) byBase[stem] = n;
			std::string key = stem + "." + (d == std::string::npos ? "" : b.substr(d + 1));
			if(!byBase.count(key)) byBase[key] = n;
		}
		return true;
	}
	bool read(const std::string &key, std::vector<uint8> &out){
		auto it = byName.find(key);
		if(it == byName.end()) return false;
		out.resize(it->second.second);
		fseek(f, it->second.first, SEEK_SET);
		fread(out.data(), 1, out.size(), f);
		return true;
	}
};
static std::vector<Pack> g_texPacks;
static std::string g_texPrefer;   // e.g. "article\\role_tex\\a01\\" : preferred directory for ambiguous names
static int g_texFound, g_texMissing;
static std::vector<std::string> g_missing;

static std::string lower(std::string s){ std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }


static Image *decodeImage(const std::string &key, std::vector<uint8> &buf)
{
	// librw's readTGA only reads from a file path (all TGAs in the pack are uncompressed type 2, as librw requires)
	if(key.size() < 4 || key.compare(key.size() - 4, 4, ".tga") != 0) return nullptr;
	static std::string tmp = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "/rm8_view_tex.tga";
	FILE *t = fopen(tmp.c_str(), "wb");
	if(!t) return nullptr;
	fwrite(buf.data(), 1, buf.size(), t); fclose(t);
	return readTGA(tmp.c_str());
}

static Texture *readTextureFromPacks(const char *name, const char *mask)
{
	std::string n = lower(name);
	std::vector<std::string> keys;
	if(!g_texPrefer.empty()) keys.push_back(g_texPrefer + n + ".tga");
	for(Pack &p : g_texPacks){
		auto it = p.byBase.find(n + ".tga");
		if(it != p.byBase.end()) keys.push_back(it->second);
		it = p.byBase.find(n);
		if(it != p.byBase.end()) keys.push_back(it->second);
	}
	for(const std::string &k : keys)
		for(Pack &p : g_texPacks){
			std::vector<uint8> buf;
			if(!p.read(k, buf)) continue;
			Image *img = decodeImage(k, buf);
			if(!img) continue;
			Raster *r = Raster::createFromImage(img);
			img->destroy();
			if(!r) continue;
			Texture *t = Texture::create(r);
			strncpy(t->name, name, 31);
			t->setFilter(Texture::LINEAR);
			g_texFound++;
			return t;
		}
	g_texMissing++; g_missing.push_back(name);
	return nullptr;
}

static Clump *loadClump(Pack &clumps, const std::string &path)
{
	std::vector<uint8> buf;
	if(!clumps.read(lower(path), buf)){ fprintf(stderr, "not in pack: %s\n", path.c_str()); return nullptr; }
	StreamMemory in; in.open(buf.data(), buf.size());
	Clump *c = nullptr;
	if(findChunk(&in, ID_CLUMP, nil, nil)) c = Clump::streamRead(&in);
	in.close();
	return c;
}

int main(int argc, char *argv[])
{
	if(argc < 4){ fprintf(stderr, "usage: %s game_dir out.png model.dff [...] [--yaw d] [--pitch d] [--size WxH]\n", argv[0]); return 2; }
	std::string game = argv[1], outPng = argv[2];
	std::vector<std::string> models;
	float yaw = 0.0f, pitch = 15.0f; int W = 800, H = 600;
	for(int i = 3; i < argc; i++){
		if(!strcmp(argv[i], "--yaw") && i + 1 < argc) yaw = (float)atof(argv[++i]);
		else if(!strcmp(argv[i], "--pitch") && i + 1 < argc) pitch = (float)atof(argv[++i]);
		else if(!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
		else models.push_back(argv[i]);
	}

	Pack clumps;
	if(!clumps.open(game + "\\Clump\\clump0000.dat")){ fprintf(stderr, "cannot open clump pack\n"); return 1; }
	for(const char *t : {"\\Texture\\tex0001.dat", "\\Texture\\tex0000.dat"}){
		Pack p; if(p.open(game + t)) g_texPacks.push_back(p);
	}
	// prefer textures from the model's own role_tex directory (characters: article\richrole\a01 -> article\role_tex\a01)
	{
		std::string m = lower(models[0]);
		size_t a = m.find("richrole\\");
		if(a != std::string::npos){ std::string id = m.substr(a + 9, m.find('\\', a + 9) - (a + 9)); g_texPrefer = "article\\role_tex\\" + id + "\\"; }
	}

	// ---- engine (GL3 + GLFW) ----
	Engine::init();
	registerMeshPlugin(); registerNativeDataPlugin(); registerAtomicRightsPlugin(); registerMaterialRightsPlugin();
	xbox::registerVertexFormatPlugin(); registerSkinPlugin(); registerUserDataPlugin(); registerHAnimPlugin();
	registerMatFXPlugin(); registerUVAnimPlugin(); ps2::registerADCPlugin();
	GLFWwindow *win = nullptr;
	EngineOpenParams op; memset(&op, 0, sizeof op);
	op.width = W; op.height = H; op.windowtitle = "rm8_view"; op.window = &win;
	if(!Engine::open(&op) || !Engine::start()){ fprintf(stderr, "engine start failed\n"); return 1; }
	Texture::readCB = readTextureFromPacks;   // resolve dff material texture names inside the original packs

	World *world = World::create();
	Light *amb = Light::create(Light::AMBIENT); amb->setColor(0.75f, 0.75f, 0.75f); world->addLight(amb);
	Light *dir = Light::create(Light::DIRECTIONAL); dir->setColor(0.5f, 0.5f, 0.5f);
	dir->setFrame(Frame::create()); { V3d ax = {1, 0, 0}; dir->getFrame()->rotate(&ax, 135.0f, COMBINEREPLACE); }
	world->addLight(dir);

	std::vector<Clump*> loaded;
	for(const std::string &m : models){
		Clump *c = loadClump(clumps, m);
		if(!c){ fprintf(stderr, "load failed: %s\n", m.c_str()); continue; }
		world->addClump(c); loaded.push_back(c);
	}
	if(loaded.empty()) return 1;

	// bounding sphere over everything
	V3d center = {0, 0, 0}; int n = 0;
	for(Clump *c : loaded){ FORLIST(lnk, c->atomics){ center = add(center, Atomic::fromClump(lnk)->getWorldBoundingSphere()->center); n++; } }
	center = scale(center, 1.0f / n);
	float radius = 0.001f;
	for(Clump *c : loaded){ FORLIST(lnk, c->atomics){ Sphere *s = Atomic::fromClump(lnk)->getWorldBoundingSphere();
		float d = length(sub(s->center, center)) + s->radius; if(d > radius) radius = d; } }

	Camera *cam = Camera::create();
	cam->setFrame(Frame::create());
	cam->frameBuffer = Raster::create(W, H, 32, Raster::CAMERATEXTURE | Raster::C8888);   // explicit RGBA8 so toImage can read it back
	cam->zBuffer = Raster::create(W, H, 0, Raster::ZBUFFER);
	cam->setNearPlane(radius * 0.01f); cam->setFarPlane(radius * 20.0f);
	V2d vw = {0.5f, 0.5f * H / W}; cam->setViewWindow(&vw);
	world->addCamera(cam);

	// orbit camera: RenderWare models here are Y-up (game uses Y as height: terrain height step 12.5 in +Y)
	float yr = yaw * 3.14159265f / 180.0f, pr = pitch * 3.14159265f / 180.0f, dist = radius * 2.2f;
	V3d eye = {center.x + dist * cosf(pr) * sinf(yr), center.y + dist * sinf(pr), center.z - dist * cosf(pr) * cosf(yr)};
	V3d fwd = normalize(sub(center, eye));
	V3d up = {0, 1, 0};
	Matrix m; m.setIdentity(); m.lookAt(fwd, up); m.pos = eye;
	cam->getFrame()->transform(&m, COMBINEREPLACE);

	RGBA bg = {64, 96, 128, 255};
	for(int frame = 0; frame < 3; frame++){     // a couple of frames so the GL state settles
		cam->clear(&bg, Camera::CLEARIMAGE | Camera::CLEARZ);
		cam->beginUpdate();
		for(Clump *c : loaded) c->render();
		cam->endUpdate();
	}
	Image *img = cam->frameBuffer->toImage();
	if(!img){ fprintf(stderr, "readback failed\n"); return 1; }
	writePNG(img, outPng.c_str());
	img->destroy();

	int atoms = 0, tris = 0;
	for(Clump *c : loaded){ FORLIST(lnk, c->atomics){ Geometry *g = Atomic::fromClump(lnk)->geometry; atoms++; if(g) tris += g->numTriangles; } }
	printf("models=%zu atomics=%d triangles=%d radius=%.1f textures_found=%d missing=%d -> %s\n",
	       loaded.size(), atoms, tris, radius, g_texFound, g_texMissing, outPng.c_str());
	for(auto &s : g_missing) printf("  missing texture: %s\n", s.c_str());
	return 0;
}
