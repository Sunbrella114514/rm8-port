// rm8_probe: print frame tree / HAnim hierarchy / skin info of models and the matching animation (NULL platform).
// usage: rm8_probe <game_dir> <dff> [anm] ...  (pairs: dff anm, anm may be "-")
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <rw.h>
using namespace rw;

static std::string lower(std::string s){ std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
struct Pack {
	FILE *f = nullptr; std::map<std::string, std::pair<uint32, uint32>> byName;
	bool open(const std::string &p){ f = fopen(p.c_str(), "rb"); if(!f) return false; uint32 h[5]; fread(h, 4, 5, f);
		std::vector<uint32> idx(h[3]*4); fread(idx.data(), 4, idx.size(), f);
		for(uint32 i = 0; i < h[3]; i++){ uint32 nl = idx[i*4], off = idx[i*4+1], sz = idx[i*4+2]; std::string n(nl, 0);
			fseek(f, off, SEEK_SET); fread(&n[0], 1, nl, f); byName[lower(n)] = {off + nl, sz}; } return true; }
	bool read(const std::string &k, std::vector<uint8> &o){ auto it = byName.find(lower(k)); if(it == byName.end()) return false;
		o.resize(it->second.second); fseek(f, it->second.first, SEEK_SET); fread(o.data(), 1, o.size(), f); return true; }
};

static std::map<Frame*, int> g_idx;
static void dumpFrame(Frame *f, int depth, Clump *c){
	HAnimData *hd = HAnimData::get(f);
	int natm = 0, nskin = 0;
	int nv = 0; FORLIST(lnk, c->atomics){ Atomic *a = Atomic::fromClump(lnk); if(a->getFrame() == f){ natm++; nv += a->geometry->numVertices; if(Skin::get(a->geometry)) nskin++; } }
	V3d p = f->matrix.pos;
	printf("%*sframe#%d hanimId=%d%s atomics=%d(skinned %d, verts %d) pos=(%.1f %.1f %.1f) right=(%.2f %.2f %.2f)\n", depth*2, "", g_idx[f], hd->id,
	       hd->hierarchy ? " [HIERARCHY]" : "", natm, nskin, nv, p.x, p.y, p.z, f->matrix.right.x, f->matrix.right.y, f->matrix.right.z);
	for(Frame *ch = f->child; ch; ch = ch->next) dumpFrame(ch, depth + 1, c);
}

int main(int argc, char **argv){
	if(argc < 3) return 2;
	Pack clumps; if(!clumps.open(std::string(argv[1]) + "/Clump/clump0000.dat")) return 1;
	Engine::init();
	registerMeshPlugin(); registerNativeDataPlugin(); registerAtomicRightsPlugin(); registerMaterialRightsPlugin();
	xbox::registerVertexFormatPlugin(); registerSkinPlugin(); registerUserDataPlugin(); registerHAnimPlugin();
	registerMatFXPlugin(); registerUVAnimPlugin(); ps2::registerADCPlugin();
	Engine::open(nil); Engine::start();
	for(int i = 2; i + 1 < argc; i += 2){
		std::vector<uint8> buf;
		if(!clumps.read(argv[i], buf)){ printf("%s: not found\n", argv[i]); continue; }
		StreamMemory in; in.open(buf.data(), buf.size());
		Clump *c = findChunk(&in, ID_CLUMP, nil, nil) ? Clump::streamRead(&in) : nullptr;
		if(!c){ printf("%s: load failed\n", argv[i]); continue; }
		g_idx.clear(); int n = 0;
		std::vector<Frame*> st = {c->getFrame()};
		while(!st.empty()){ Frame *f = st.back(); st.pop_back(); g_idx[f] = n++; for(Frame *ch = f->child; ch; ch = ch->next) st.push_back(ch); }
		printf("== %s: frames=%d atomics=%d\n", argv[i], n, c->countAtomics());
		dumpFrame(c->getFrame(), 1, c);
		HAnimHierarchy *h = HAnimHierarchy::find(c->getFrame());
		if(h){
			printf("  hierarchy: numNodes=%d flags=0x%x parentFrame=frame#%d\n", h->numNodes, h->flags, g_idx.count(h->parentFrame) ? g_idx[h->parentFrame] : -1);
			h->attach();
			for(int k = 0; k < h->numNodes; k++) printf("    node%d id=%d flags=%d frame#%d\n", k, h->nodeInfo[k].id, h->nodeInfo[k].flags,
			                                            h->nodeInfo[k].frame && g_idx.count(h->nodeInfo[k].frame) ? g_idx[h->nodeInfo[k].frame] : -1);
		}
		FORLIST(lnk, c->atomics){ Atomic *a = Atomic::fromClump(lnk); Skin *s = Skin::get(a->geometry);
			if(s) printf("  skinned atomic on frame#%d: bones=%d used=%d maxWeights=%d\n", g_idx[a->getFrame()], s->numBones, s->numUsedBones, s->numWeights); }
		if(strcmp(argv[i+1], "-")){
			std::vector<uint8> ab;
			if(!clumps.read(argv[i+1], ab)){ printf("  anim %s not found\n", argv[i+1]); continue; }
			StreamMemory ai; ai.open(ab.data(), ab.size());
			Animation *anim = findChunk(&ai, ID_ANIMANIMATION, nil, nil) ? Animation::streamRead(&ai) : nullptr;
			if(!anim){ printf("  anim %s: load failed\n", argv[i+1]); continue; }
			printf("  anim %s: nodes=%d frames=%d duration=%.3f type=%d\n", argv[i+1], anim->getNumNodes(), anim->numFrames, anim->duration, anim->interpInfo->id);
		}
	}
	return 0;
}
