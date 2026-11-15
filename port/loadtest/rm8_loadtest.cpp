// rm8_loadtest: stream every RenderWare asset of RichMan8 straight out of the game's packs with librw
// (NULL platform, no GPU) and report which ones librw can parse.
// usage: rm8_loadtest <Clump/clump0000.dat> [filter]
// Pack format A (verified, see scripts/rm8pak.py):
//   u32 tag, u32 0, u32 fileSize, u32 count, u32 unk;  count * {u32 nameLen, u32 off, u32 size, u32 flag}
//   at off: name[nameLen] then data[size]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <rw.h>

using namespace rw;

struct Entry { std::string name; uint32 off, size; };

static std::vector<Entry> readIndex(FILE *f)
{
	std::vector<Entry> v;
	uint32 h[5];
	if(fread(h, 4, 5, f) != 5) return v;
	std::vector<uint32> idx(h[3] * 4);
	fread(idx.data(), 4, idx.size(), f);
	for(uint32 i = 0; i < h[3]; i++){
		uint32 nl = idx[i*4], off = idx[i*4+1], sz = idx[i*4+2];
		std::string name(nl, '\0');
		fseek(f, off, SEEK_SET);
		fread(&name[0], 1, nl, f);
		v.push_back({name, off + nl, sz});
	}
	return v;
}

static std::string ext(const std::string &n)
{
	size_t p = n.rfind('.');
	std::string e = p == std::string::npos ? "" : n.substr(p + 1);
	for(auto &c : e) c = (char)tolower(c);
	return e;
}

static Atomic *countAtomic(Atomic *a, void *data)
{
	int *s = (int*)data;
	Geometry *g = a->geometry;
	s[0]++;
	if(g){ s[1] += g->numVertices; s[2] += g->numTriangles; s[3] += g->matList.numMaterials;
	       if(Skin::get(g)) s[4]++; }
	return a;
}

int main(int argc, char *argv[])
{
	if(argc < 2){ fprintf(stderr, "usage: %s clump0000.dat [filter]\n", argv[0]); return 2; }
	const char *filter = argc > 2 ? argv[2] : nullptr;

	Engine::init();
	registerMeshPlugin();
	registerNativeDataPlugin();
	registerAtomicRightsPlugin();
	registerMaterialRightsPlugin();
	xbox::registerVertexFormatPlugin();
	registerSkinPlugin();
	registerUserDataPlugin();
	registerHAnimPlugin();
	registerMatFXPlugin();
	registerUVAnimPlugin();
	ps2::registerADCPlugin();
	// NULL platform: open params are never dereferenced
	if(!Engine::open(nil) || !Engine::start()){ fprintf(stderr, "engine start failed\n"); return 1; }
	Texture::setLoadTextures(false);   // models only; textures live in tex0000.dat

	FILE *f = fopen(argv[1], "rb");
	if(!f){ perror(argv[1]); return 1; }
	std::vector<Entry> ents = readIndex(f);

	std::map<std::string, int> ok, fail;
	long totAtom = 0, totVert = 0, totTri = 0, totSkin = 0;
	FILE *log = fopen("loadtest_failures.txt", "w");
	for(const Entry &e : ents){
		if(filter && !strstr(e.name.c_str(), filter)) continue;
		std::string x = ext(e.name);
		if(x != "dff" && x != "anm" && x != "uva") continue;
		std::vector<uint8> buf(e.size);
		fseek(f, e.off, SEEK_SET);
		fread(buf.data(), 1, e.size, f);

		StreamMemory in;
		in.open(buf.data(), e.size);
		bool good = false;
		char why[160] = "";
		if(x == "dff"){
			ChunkHeaderInfo hi;
			if(readChunkHeaderInfo(&in, &hi) && hi.type == ID_CLUMP){
				// Clump::streamRead expects the stream positioned just after the CLUMP header
				Clump *c = Clump::streamRead(&in);
				if(c){
					int s[5] = {0,0,0,0,0};
					FORLIST(lnk, c->atomics) countAtomic(Atomic::fromClump(lnk), s);
					good = s[0] > 0;
					totAtom += s[0]; totVert += s[1]; totTri += s[2]; totSkin += s[4];
					if(!good) snprintf(why, sizeof why, "clump has no atomics");
					c->destroy();
				}else snprintf(why, sizeof why, "Clump::streamRead failed");
			}else snprintf(why, sizeof why, "first chunk 0x%x is not a clump", hi.type);
		}else if(x == "anm"){
			ChunkHeaderInfo hi;
			if(readChunkHeaderInfo(&in, &hi) && hi.type == ID_ANIMANIMATION){
				Animation *a = Animation::streamRead(&in);
				if(a){ good = a->numFrames > 0; if(!good) snprintf(why, sizeof why, "0 frames"); a->destroy(); }
				else snprintf(why, sizeof why, "Animation::streamRead failed");
			}else snprintf(why, sizeof why, "first chunk 0x%x is not an animation", hi.type);
		}else{ // uva
			ChunkHeaderInfo hi;
			if(readChunkHeaderInfo(&in, &hi) && hi.type == ID_UVANIMDICT){
				UVAnimDictionary *d = UVAnimDictionary::streamRead(&in);
				if(d){ good = true; d->destroy(); }
				else snprintf(why, sizeof why, "UVAnimDictionary::streamRead failed");
			}else snprintf(why, sizeof why, "first chunk 0x%x is not a UV anim dict", hi.type);
		}
		in.close();
		(good ? ok : fail)[x]++;
		if(!good && log) fprintf(log, "%s\t%s\n", e.name.c_str(), why);
	}
	if(log) fclose(log);
	fclose(f);
	for(const char *x : {"dff", "anm", "uva"})
		printf("%-4s ok=%-5d fail=%d\n", x, ok[x], fail[x]);
	printf("atomics=%ld vertices=%ld triangles=%ld skinned_geometries=%ld\n", totAtom, totVert, totTri, totSkin);
	Engine::stop(); Engine::close(); Engine::term();
	int nfail = 0; for(auto &p : fail) nfail += p.second;
	return nfail ? 1 : 0;
}
