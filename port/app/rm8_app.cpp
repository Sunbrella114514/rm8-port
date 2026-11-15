// rm8_app: interactive RichMan8 map viewer (SDL2 + librw GL3/GLES). Same code on desktop and Android.
//   touch: 1 finger = orbit, 2 fingers = pinch zoom + pan, double tap = next map, back = quit
// The log reports fps and CPU time per frame (animation update, render submission).
//   mouse: left drag = orbit, right drag = pan, wheel = zoom, N/space = next map, Esc = quit
// Android data layout (app-specific external storage, e.g. /sdcard/Android/data/com.rm8.port/files):
//   game/Clump/clump0000.dat, game/Texture/tex0000.dat, game/Texture/tex0001.dat, scenes/<id>/scene.txt ...
// Desktop: rm8_app <output folder of rm8_extract> [options]   (= <out>/game <out>/scenes, read through <out>/manifest.json; no folder: the folder of the exe)
//          rm8_app <game_dir> <scenes_root> [--map id] [--cam yaw pitch dist] [--size WxH]     (two paths: no manifest needed, dev setup)
//                  [--shot out.png]   render one offscreen frame to PNG and quit
//                  [--frames n]       run the interactive loop for n frames and quit
//                  [--time t]         with --shot: advance the article animations to t seconds first
//                  [--cycle k]        switch to the next map every k frames (load/unload test)
// If the data or manifest.json is missing / of another data_format a Chinese notice is drawn (own strings, system font) and logged (adb logcat -s rm8); exit code 3.
#include <SDL.h>
// rw.h comes through scene.h (rw.h has no include guard)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include "turn.h"
#include "events.h"
#include "news.h"
#include "balance.h"
#include "cards.h"
#include "boardfx.h"
#include "zh.h"
#include "ui.h"
#include "audio.h"
#include "menu.h"
#include "stock.h"
#include "minigame.h"
#include "collect.h"
#include "skip.h"
#include "story.h"
#include "movie.h"
#include "story_host.h"
#include "save.h"
#include "infoscreens.h"

// [V] the board scene's key handler (CGamePlayScene__OnEvent @0x4a1e80): Game_SelectNextPlayer @0x4a6d60 first (any key while the current role walks gives the next
// auto-played seat back and is consumed), then F1..F4 (keys 0x81..0x84) = seats 0..3: toggle that seat's auto play (rm8AutoKey, roles.cpp). True = the key is used.
static bool autoPlayKey(SDL_Keycode k)
{
	if(rm8AutoRelease()) return true;
	if(k < SDLK_F1 || k > SDLK_F4) return false;
	int seat = k - SDLK_F1;
	if(rm8EventRunning()) return true;	// [V] the F keys do nothing while the auction / wonder UI is open (DAT_00642780 != 0); [A] any event task stands in for it
	// [A] the port keeps some dialogs open in state 6 (the original runs them as scene tasks that take the key first): do not hand the role over under them
	if(!rm8TurnAuto(seat) && (rm8StockUiOpen() || rm8InfoVisible() || rm8CardsPicking() || rm8MenuGameSystemVisible() || rm8SaveUiVisible())) return true;
	rm8AutoKey(seat);
	return true;
}

using namespace rw;

static std::vector<std::string> listMaps(const std::string &root)
{
	std::vector<std::string> ids;
	std::error_code ec;
	for(auto &e : std::filesystem::directory_iterator(root, ec))
		if(e.is_directory() && std::filesystem::exists(e.path() / "scene.txt"))
			ids.push_back(e.path().filename().string());
	std::sort(ids.begin(), ids.end());
	return ids;
}

#ifdef __ANDROID__
// [A] self-contained test APK (android/build_apk.py --with-data): assets/rm8data/manifest.txt lists the bundled data files; on the first
// start (or when the bundle version changed) they are copied into the app data dir. A plain progress bar is drawn with raw GL calls.
static void unpackBundledData(const std::string &base, SDL_Window *win)
{
	SDL_RWops *mf = SDL_RWFromFile("rm8data/manifest.txt", "rb");
	if(!mf) return;	// a normal APK without bundled data
	Sint64 msz = SDL_RWsize(mf);
	std::string man((size_t)std::max<Sint64>(0, msz), '\0');
	if(msz > 0) SDL_RWread(mf, &man[0], 1, (size_t)msz);
	SDL_RWclose(mf);
	std::vector<std::pair<long long, std::string>> files; std::string ver;
	size_t p = 0;
	while(p < man.size()){
		size_t e = man.find('\n', p); if(e == std::string::npos) e = man.size();
		std::string l = man.substr(p, e - p); p = e + 1;
		if(l.compare(0, 8, "version ") == 0){ ver = l.substr(8); continue; }
		size_t t = l.find('\t'); if(t == std::string::npos) continue;
		files.push_back({atoll(l.c_str()), l.substr(t + 1)});
	}
	std::string mark = base + "/rm8data.version";
	if(FILE *f = fopen(mark.c_str(), "rb")){ char b[64] = {0}; fread(b, 1, 63, f); fclose(f); if(ver == b){ rm8Log("bundled data %s already unpacked", ver.c_str()); return; } }
	typedef void (*Fn4i)(int, int, int, int); typedef void (*Fn4f)(float, float, float, float); typedef void (*Fn1u)(unsigned);
	Fn4i glScissorP = (Fn4i)SDL_GL_GetProcAddress("glScissor"), glViewportP = (Fn4i)SDL_GL_GetProcAddress("glViewport");
	Fn4f glClearColorP = (Fn4f)SDL_GL_GetProcAddress("glClearColor");
	Fn1u glClearP = (Fn1u)SDL_GL_GetProcAddress("glClear"), glEnableP = (Fn1u)SDL_GL_GetProcAddress("glEnable"), glDisableP = (Fn1u)SDL_GL_GetProcAddress("glDisable");
	long long total = 0, done = 0; for(auto &f : files) total += f.first;
	auto draw = [&](){
		int w, h; SDL_GL_GetDrawableSize(win, &w, &h);
		if(!glClearP) return;
		glViewportP(0, 0, w, h); glDisableP(0x0C11 /*GL_SCISSOR_TEST*/);
		glClearColorP(0.08f, 0.08f, 0.12f, 1); glClearP(0x4000 /*GL_COLOR_BUFFER_BIT*/);
		int bw = w * 6 / 10, bh = h / 30, bx = (w - bw) / 2, by = h / 2 - bh / 2;
		glEnableP(0x0C11);
		glScissorP(bx - 3, by - 3, bw + 6, bh + 6); glClearColorP(0.9f, 0.75f, 0.2f, 1); glClearP(0x4000);
		glScissorP(bx, by, bw, bh); glClearColorP(0.15f, 0.15f, 0.2f, 1); glClearP(0x4000);
		glScissorP(bx, by, total > 0 ? (int)(bw * (double)done / total) : 0, bh); glClearColorP(1.0f, 0.82f, 0.25f, 1); glClearP(0x4000);
		glDisableP(0x0C11);
		SDL_GL_SwapWindow(win);
		SDL_PumpEvents();
	};
	rm8Log("unpacking bundled data %s: %d files, %.1f MB", ver.c_str(), (int)files.size(), total / 1e6);
	std::vector<char> buf(4 << 20);
	Uint32 lastDraw = 0;
	for(auto &f : files){
		// [A] saves / profile of the original game (rm8_extract --saves): manifest entries "import_saves/<rel>" go to <data dir>/<rel> (save/record_NN.sav, rm8_gamedate.bin)
		// and are only copied when that file does not exist yet, so later progress is never overwritten by a newer APK
		const bool imp = f.second.compare(0, 13, "import_saves/") == 0;
		std::string dst = base + "/" + (imp ? f.second.substr(13) : f.second);
		std::error_code ec; std::filesystem::create_directories(std::filesystem::path(dst).parent_path(), ec);
		if(imp && std::filesystem::exists(dst, ec)){ rm8Log("unpack: keeping the existing %s (not replaced by the imported one)", dst.c_str()); done += f.first; continue; }
		if(std::filesystem::exists(dst, ec) && (long long)std::filesystem::file_size(dst, ec) == f.first){ done += f.first; continue; }	// already there
		SDL_RWops *in = SDL_RWFromFile(("rm8data/" + f.second).c_str(), "rb");
		FILE *out = fopen((dst + ".part").c_str(), "wb");
		if(!in || !out){ rm8Log("unpack: cannot copy %s", f.second.c_str()); if(in) SDL_RWclose(in); if(out) fclose(out); continue; }
		size_t n;
		while((n = SDL_RWread(in, buf.data(), 1, buf.size())) > 0){
			fwrite(buf.data(), 1, n, out); done += (long long)n;
			if(SDL_GetTicks() - lastDraw > 100){ draw(); lastDraw = SDL_GetTicks(); }
		}
		SDL_RWclose(in); fclose(out);
		std::filesystem::rename(dst + ".part", dst, ec);
	}
	if(FILE *f = fopen(mark.c_str(), "wb")){ fputs(ver.c_str(), f); fclose(f); }
	rm8Log("unpacked bundled data %s", ver.c_str());
}
#endif

static void logGlInfo()
{
	typedef const unsigned char *(*GetStringFn)(unsigned);
	typedef void (*GetIntegervFn)(unsigned, int*);
	GetStringFn gs = (GetStringFn)SDL_GL_GetProcAddress("glGetString");
	GetIntegervFn gi = (GetIntegervFn)SDL_GL_GetProcAddress("glGetIntegerv");
	int vecs = -1;
	if(gi) gi(0x8DFB /*GL_MAX_VERTEX_UNIFORM_VECTORS*/, &vecs);
	if(gs) rm8Log("GL: %s | %s | %s | max vertex uniform vec4 = %d | skin shader bones = %d",
	              (const char*)gs(0x1F00), (const char*)gs(0x1F01), (const char*)gs(0x1F02), vecs, (int)gl3::skinMaxBones);
}

// Default view = the board camera: [V] camera wrapper yaw 135 / pitch 40 / distance 800 (rm8PlaceCamera yaw 225, pitch 40,
// 800 / (2 * 64 * 25 * 1.42) of the map extent), following the player whose turn it is; panning offsets from that player.
struct Orbit { float yaw = 225, pitch = 40, dist = 0.176f, tx = 0, tz = 0; };
static const float kBoardVwY = 0.225f;	// [V] the board camera's vertical view window (0.3 x 0.225 at 4:3)
// [A] the card range (cards.cpp rm8CardsPosInView) is the DEFAULT board view on the acting role for human and AI alike; while a card target is chosen the
// camera is locked to it (the original cannot pan then), so what the human can tap is exactly what the AI could target
static void cardRangeView(const Rm8Scene &sc){ Orbit d; rm8CardsSetDefaultView(d.yaw, d.pitch, sc.MW * 25.0f * 1.42f * 2.0f * d.dist, kBoardVwY); }
static bool cardPickCamera(const Rm8Scene &sc, Camera *cam)
{
	float rp[3];
	if(!rm8CardsPicking() || !rm8RolesGetPos(rm8TurnCurrent(), rp)) return false;
	Orbit d; rm8PlaceCamera(cam, sc, d.yaw, d.pitch, d.dist, rp[0], rp[2]);
	return true;
}

static void setupWindowCamera(Camera *cam, int w, int h)
{
	if(cam->frameBuffer) cam->frameBuffer->destroy();
	if(cam->zBuffer) cam->zBuffer->destroy();
	cam->frameBuffer = Raster::create(w, h, 0, Raster::CAMERA);
	cam->zBuffer = Raster::create(w, h, 0, Raster::ZBUFFER);
	// [V] board camera view window 0.3 x 0.225 (4:3, read from the game); keep its vertical field of view on wider screens
	V2d vw = {kBoardVwY * w / h, kBoardVwY};
	if(h > w){ vw.x = 0.3f; vw.y = 0.3f * h / w; }	// portrait: keep the horizontal one
	cam->setViewWindow(&vw);
}

// game random seed: [A] the original seeds the C runtime rand() from the clock, so every game differs; tests (--shot / --frames) keep the
// old fixed seed 1 + map index so their logs stay comparable, RM8_SEED=n forces one
static std::string g_shotArg; static int g_framesArg = -1;
static unsigned gameSeed(size_t mapIdx)
{
	if(const char *e = getenv("RM8_SEED")) return (unsigned)strtoul(e, nullptr, 10);
	if(!g_shotArg.empty() || g_framesArg > 0) return 1u + (unsigned)mapIdx;
	return (unsigned)time(nullptr) ^ (SDL_GetTicks() * 2654435761u);
}

// ======================================================================== first-run checks (docs/assets_legal.md section 8 items 1-3)
// [A] rm8_extract 1.1.0 writes data_format 2 (2 = + scenes/save_tables.txt); the app refuses other formats with a notice
static const int RM8_DATA_FORMAT = 2;

// a minimal JSON reader: only the top level object and the members of its object members are kept (data_format, source.edition, game_dir_for_runtime)
struct Jv { char t = 0; double d = 0; std::string s; std::vector<std::pair<std::string, Jv>> kv;
	const Jv *get(const char *k) const { for(auto &p : kv) if(p.first == k) return &p.second; return nullptr; } };
static void jWs(const char *&p, const char *e){ while(p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
static void jUtf8(std::string &o, unsigned cp)
{
	if(cp < 0x80) o += (char)cp;
	else if(cp < 0x800){ o += (char)(0xC0 | cp >> 6); o += (char)(0x80 | (cp & 0x3F)); }
	else if(cp < 0x10000){ o += (char)(0xE0 | cp >> 12); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
	else { o += (char)(0xF0 | cp >> 18); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}
static bool jHex4(const char *p, const char *e, unsigned &v)
{
	if(e - p < 4) return false;
	v = 0;
	for(int i = 0; i < 4; i++){ char c = p[i]; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if(d < 0) return false; v = v * 16 + d; }
	return true;
}
static bool jStr(const char *&p, const char *e, std::string &out)
{
	if(p >= e || *p != '"') return false;
	p++; out.clear();
	while(p < e && *p != '"'){
		if(*p != '\\'){ out += *p++; continue; }
		if(++p >= e) return false;
		switch(*p){
		case 'n': out += '\n'; p++; break; case 't': out += '\t'; p++; break; case 'r': out += '\r'; p++; break;
		case 'b': out += '\b'; p++; break; case 'f': out += '\f'; p++; break;
		case 'u': {
			unsigned cp; if(!jHex4(p + 1, e, cp)) return false;
			p += 5;
			if(cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u'){ unsigned lo; if(jHex4(p + 2, e, lo) && lo >= 0xDC00 && lo < 0xE000){ cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; } }
			jUtf8(out, cp); break; }
		default: out += *p++; break;
		}
	}
	if(p >= e) return false;
	p++;
	return true;
}
static bool jVal(const char *&p, const char *e, Jv &out, int depth)
{
	jWs(p, e);
	if(p >= e || depth > 40) return false;
	if(*p == '{'){
		out.t = 'o'; p++; jWs(p, e);
		if(p < e && *p == '}'){ p++; return true; }
		for(;;){
			jWs(p, e);
			std::string k; if(!jStr(p, e, k)) return false;
			jWs(p, e); if(p >= e || *p != ':') return false; p++;
			Jv v; if(!jVal(p, e, v, depth + 1)) return false;
			if(depth <= 1) out.kv.emplace_back(std::move(k), std::move(v));
			jWs(p, e);
			if(p < e && *p == ','){ p++; continue; }
			if(p < e && *p == '}'){ p++; return true; }
			return false;
		}
	}
	if(*p == '['){
		out.t = 'a'; p++; jWs(p, e);
		if(p < e && *p == ']'){ p++; return true; }
		for(;;){
			Jv v; if(!jVal(p, e, v, depth + 1)) return false;
			jWs(p, e);
			if(p < e && *p == ','){ p++; continue; }
			if(p < e && *p == ']'){ p++; return true; }
			return false;
		}
	}
	if(*p == '"'){ out.t = 's'; return jStr(p, e, out.s); }
	if(e - p >= 4 && !strncmp(p, "true", 4)){ out.t = 'b'; out.d = 1; p += 4; return true; }
	if(e - p >= 5 && !strncmp(p, "false", 5)){ out.t = 'b'; out.d = 0; p += 5; return true; }
	if(e - p >= 4 && !strncmp(p, "null", 4)){ out.t = 'n'; p += 4; return true; }
	char *end = nullptr; double d = strtod(p, &end);	// the text is a NUL-terminated std::string
	if(end == p) return false;
	out.t = 'd'; out.d = d; p = end;
	return true;
}

struct Rm8Manifest { bool present = false, ok = false; int dataFormat = -1; std::string edition, gameDir, scenesDir; };
static Rm8Manifest readManifest(const std::string &path)
{
	Rm8Manifest m;
	FILE *f = fopen(path.c_str(), "rb");
	if(!f) return m;
	m.present = true;
	std::string txt; char buf[65536]; size_t n;
	while((n = fread(buf, 1, sizeof buf, f)) > 0) txt.append(buf, n);
	fclose(f);
	if(txt.compare(0, 3, "\xEF\xBB\xBF") == 0) txt.erase(0, 3);
	const char *p = txt.c_str(), *e = p + txt.size();
	Jv root;
	if(!jVal(p, e, root, 0) || root.t != 'o') return m;
	m.ok = true;
	if(const Jv *v = root.get("data_format")) if(v->t == 'd') m.dataFormat = (int)v->d;
	if(const Jv *src = root.get("source")) if(const Jv *ed = src->get("edition")) if(ed->t == 's') m.edition = ed->s;
	if(const Jv *v = root.get("game_dir_for_runtime")) if(v->t == 's') m.gameDir = v->s;
	if(const Jv *v = root.get("scenes_dir_for_runtime")) if(v->t == 's') m.scenesDir = v->s;
	return m;
}

// the notice of a first-run problem (own strings: scenes/ui/strings.txt may be missing), drawn by runNotice and logged
static std::string g_noticeTitle, g_noticeBody;
static void setNotice(const std::string &title, const std::string &body)
{
	g_noticeTitle = title; g_noticeBody = body;
	for(char &c : g_noticeBody) if(c == '\\') c = '/';	// the text convention of ui.cpp: a backslash is a line break, so paths are shown with slashes
	rm8Log("%s: %s", title.c_str(), body.c_str());
}
static const char *kHowTo = "请先用 rm8_extract 从你自己的《大富翁8》提取数据（见 README）";

// [A] CJK font candidates (the first that opens and has the glyph of U+5927 is used; RM8_FONT=<file>[,<face index>] goes first). Windows: the stock fonts
// SimSun (the game's own look), YaHei, SimHei, then the Traditional ones; Android: Noto CJK (.ttc order JP KR SC TC HK: SC = 2, TC = 3, [A] from the old code) and the older fallbacks
static std::vector<Rm8FontCandidate> fontCandidates(bool simplified)
{
	std::vector<Rm8FontCandidate> c;
	if(const char *ev = getenv("RM8_FONT")){ std::string f = ev; int idx = 0; size_t k = f.rfind(','); if(k != std::string::npos && k + 1 < f.size() && isdigit((unsigned char)f[k + 1])){ idx = atoi(f.c_str() + k + 1); f.erase(k); } c.push_back({f, idx}); }
#ifdef __ANDROID__
	int ttc = simplified ? 2 : 3;
	c.push_back({"/system/fonts/NotoSerifCJK-Regular.ttc", ttc});
	c.push_back({"/system/fonts/NotoSansCJK-Regular.ttc", ttc});
	c.push_back({"/system/fonts/NotoSerifSC-Regular.otf", 0});	// [A] file names of some other Android builds
	c.push_back({"/system/fonts/NotoSansSC-Regular.otf", 0});
	c.push_back({"/system/fonts/NotoSansCJKsc-Regular.otf", 0});
	c.push_back({"/system/fonts/DroidSansFallbackFull.ttf", 0});
	c.push_back({"/system/fonts/DroidSansFallback.ttf", 0});
#else
	(void)simplified;
	const char *wd = getenv("WINDIR"); if(!wd) wd = getenv("SystemRoot");
	std::string dir = std::string(wd && *wd ? wd : "C:/Windows") + "/Fonts/";
	for(const char *f : {"simsun.ttc", "msyh.ttc", "msyh.ttf", "simhei.ttf", "Deng.ttf", "simkai.ttf", "msjh.ttc", "mingliu.ttc"}) c.push_back({dir + f, 0});
#endif
	return c;
}

// the notice screen: the first-run problem on the cleared screen (shot mode: one frame to the PNG), until the window is closed / Esc / Back. Exit code 3.
static int runNotice(SDL_Window *win, const std::string &shot, int W, int H)
{
	RGBA bg = {24, 28, 44, 255};
	Camera *cam = Camera::create();
	cam->setFrame(Frame::create());
	if(!shot.empty()){
		cam->frameBuffer = Raster::create(W, H, 32, Raster::CAMERATEXTURE | Raster::C8888);
		cam->zBuffer = Raster::create(W, H, 0, Raster::ZBUFFER);
		V2d vw = {0.5f, 0.5f * H / W}; cam->setViewWindow(&vw);
		for(int f = 0; f < 2; f++){ cam->clear(&bg, Camera::CLEARIMAGE | Camera::CLEARZ); cam->beginUpdate(); rm8UiDrawNotice(cam, W, H, g_noticeTitle, g_noticeBody); cam->endUpdate(); }
		Image *img = cam->frameBuffer->toImage();
		if(!img){ rm8Log("readback failed"); return 3; }
		writePNG(img, shot.c_str()); img->destroy();
		rm8Log("wrote %s", shot.c_str());
		return 3;
	}
	int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
	setupWindowCamera(cam, dw, dh);
	for(bool run = true; run;){
		SDL_Event e;
		while(SDL_PollEvent(&e)){
			if(e.type == SDL_QUIT) run = false;
			else if(e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.scancode == SDL_SCANCODE_AC_BACK)) run = false;
			else if(e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED){ SDL_GL_GetDrawableSize(win, &dw, &dh); if(dw > 0 && dh > 0) setupWindowCamera(cam, dw, dh); }
		}
		cam->clear(&bg, Camera::CLEARIMAGE | Camera::CLEARZ);
		cam->beginUpdate(); rm8UiDrawNotice(cam, dw, dh, g_noticeTitle, g_noticeBody); cam->endUpdate();
		cam->showRaster(Raster::FLIPWAITVSYNCH);
		SDL_Delay(10);
	}
	return 3;
}

int main(int argc, char *argv[])
{
	std::string game, scenesRoot, shot, startMap, loadPath;
	std::string manifestPath, dataHome; bool manifestRequired = false; int optStart = 3;	// the manifest of the data folder; required when only the output folder was given
	Orbit cam0; int W = 1280, H = 720, maxFrames = -1, cycle = 0, miniGame = 0; float shotTime = 0;
#ifdef __ANDROID__
	int humanSeat = 0;	// tap = dice button
#else
	int humanSeat = -1;
#endif
#ifdef __ANDROID__
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	const char *ext = SDL_AndroidGetExternalStoragePath();
	std::string base = ext ? ext : "/sdcard/Android/data/com.rm8.port/files";
	game = base + "/game"; scenesRoot = base + "/scenes";
	dataHome = base;
	manifestPath = base + "/manifest.json";	// [A] optional here: the adb-pushed dev data has none (the personal APK of rm8_extract carries it)
	rm8Log("data dir: %s", base.c_str());
	if(FILE *ef = fopen((base + "/rm8_env.txt").c_str(), "r")){	// [A] debug only: KEY=VALUE lines become environment variables (the phone has no shell env; push the file with adb)
		char ln[512]; while(fgets(ln, sizeof ln, ef)){ char *eq = strchr(ln, '='); if(!eq || ln[0] == '#') continue; *eq = 0; char *v = eq + 1; v[strcspn(v, "\r\n")] = 0; setenv(ln, v, 1); rm8Log("env: %s=%s", ln, v); }
		fclose(ef);
	}
#else
	if(argc >= 3 && argv[2][0] != '-'){	// two paths: game folder + scenes folder (dev setup); manifest.json next to scenes\ is read when it exists
		game = argv[1]; scenesRoot = argv[2];
		std::filesystem::path sp(scenesRoot); if(!sp.has_filename()) sp = sp.parent_path();
		manifestPath = (sp.parent_path() / "manifest.json").string();
	} else {	// one path: the output folder of rm8_extract (game\, scenes\, manifest.json); no path at all: the folder of the exe (a double click)
		std::string out;
		if(argc >= 2 && argv[1][0] != '-'){ out = argv[1]; optStart = 2; }
		else { char *bp = SDL_GetBasePath(); out = bp ? bp : "."; if(bp) SDL_free(bp); optStart = 1; }
		while(out.size() > 1 && (out.back() == '/' || out.back() == '\\') && !(out.size() == 3 && out[1] == ':')) out.pop_back();
		manifestPath = out + "/manifest.json"; manifestRequired = true; dataHome = out;
		game = out + "/game"; scenesRoot = out + "/scenes";	// [V] layout of rm8_extract (manifest: game_dir_for_runtime / scenes_dir_for_runtime)
	}
	for(int i = optStart; i < argc; i++){
		if(!strcmp(argv[i], "--map") && i + 1 < argc) startMap = argv[++i];
		else if(!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
		else if(!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
		else if(!strcmp(argv[i], "--cycle") && i + 1 < argc) cycle = atoi(argv[++i]);
		else if(!strcmp(argv[i], "--time") && i + 1 < argc) shotTime = (float)atof(argv[++i]);
		else if(!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
		else if(!strcmp(argv[i], "--human") && i + 1 < argc) humanSeat = atoi(argv[++i]);	// seat played by the user (-1 none)
		else if(!strcmp(argv[i], "--load") && i + 1 < argc) loadPath = argv[++i];	// [A] a record (docs/save_port.md): the saved board instead of a new game
		else if(!strcmp(argv[i], "--minigame") && i + 1 < argc) miniGame = atoi(argv[++i]);	// start minigame 18..23 only (mg_standalone.cpp)
		else if(!strcmp(argv[i], "--cam") && i + 3 < argc){ cam0.yaw = (float)atof(argv[i+1]); cam0.pitch = (float)atof(argv[i+2]); cam0.dist = (float)atof(argv[i+3]); i += 3; }
	}
#endif
	// [A] the front-end menus run by default (docs/menus.md); the test / batch options skip them unless --menu is given
	g_shotArg = shot; g_framesArg = maxFrames;	// for gameSeed
	bool skipMenu = getenv("RM8_NOMENU") != nullptr;
	for(int i = 1; i < argc; i++){
		if(!strcmp(argv[i], "--map") || !strcmp(argv[i], "--shot") || !strcmp(argv[i], "--human") || !strcmp(argv[i], "--cycle") || !strcmp(argv[i], "--frames") || !strcmp(argv[i], "--minigame")) skipMenu = true;
	}
	for(int i = 1; i < argc; i++) if(!strcmp(argv[i], "--menu")) skipMenu = false;
	if(getenv("RM8_STORY") || getenv("RM8_MOVIE")) skipMenu = true;	// story / cutscene test hooks (story.h, movie.h) run without the menus
	if(!loadPath.empty()) skipMenu = true;	// --load: straight to the saved board
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	Engine::init();
	rm8RegisterPlugins();
	SDL_Window *win = nullptr;
	EngineOpenParams op; memset(&op, 0, sizeof op);
	op.width = W; op.height = H; op.windowtitle = "RichMan8 map viewer"; op.window = &win;
	if(!Engine::open(&op) || !Engine::start()){ rm8Log("engine start failed: %s", SDL_GetError()); return 1; }
#ifdef __ANDROID__
	SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN);	// immersive mode: hide status and navigation bars
#endif
	logGlInfo();
#ifdef __ANDROID__
	unpackBundledData(base, win);	// test APK with bundled data: copy it into the data dir first
#endif
	{ int db = 0; SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &db); rm8Log("depth buffer bits: %d", db); }
	rm8InstallTextureCallback();

	// manifest.json (docs/assets_legal.md section 8 item 2; read after unpackBundledData, which may bring a new one): data_format, source.edition (sc = Steam GBK, tc = old Big5 edition), game_dir_for_runtime (an absolute path after --no-copy-game)
	{
		Rm8Manifest mf = readManifest(manifestPath);
		char hdr[160];
		if(!mf.present && manifestRequired)
			setNotice("找不到 manifest.json", std::string("文件夹里没有 manifest.json：") + manifestPath + "\n\n" + kHowTo + "，再把输出文件夹里的 game、scenes、manifest.json 放在同一个文件夹里（或在命令行里把它作为唯一的参数）。");
		else if(mf.present && !mf.ok)
			setNotice("manifest.json 无法读取", std::string("文件已损坏或不是 rm8_extract 生成的：") + manifestPath + "\n\n请用最新的 rm8_extract 重新提取数据。");
		else if(mf.ok && mf.dataFormat != RM8_DATA_FORMAT){
			snprintf(hdr, sizeof hdr, "数据版本不符（data_format %d，本程序需要 %d）", mf.dataFormat, RM8_DATA_FORMAT);
			setNotice(hdr, mf.dataFormat < RM8_DATA_FORMAT ? std::string("这份数据是用旧版 rm8_extract 提取的。请用最新的 rm8_extract 重新提取（只替换 game、scenes、manifest.json，存档和设置不受影响）。\n\n") + manifestPath
			                                               : std::string("这份数据比本程序新。请更新移植版程序，或用与本程序配套的 rm8_extract 重新提取。\n\n") + manifestPath);
		}
		if(mf.ok){
			rm8SetDataEdition(mf.edition);
			rm8Log("manifest: data_format %d, edition %s%s", mf.dataFormat, mf.edition.empty() ? "?" : mf.edition.c_str(), mf.edition == "sc" ? " (Steam, GBK)" : mf.edition == "tc" ? " (old edition, Big5)" : "");
			if(manifestRequired && !mf.gameDir.empty()){	// "game" next to scenes\, or the original game folder (--no-copy-game)
				game = std::filesystem::path(mf.gameDir).is_absolute() ? mf.gameDir : dataHome + "/" + mf.gameDir;
			}
			if(manifestRequired && !mf.scenesDir.empty() && !std::filesystem::path(mf.scenesDir).is_absolute()) scenesRoot = dataHome + "/" + mf.scenesDir;
		} else rm8SetDataEdition("");
	}
	// [A] text language: Simplified by default (RM8_LANG=zh-Hant keeps the game's Traditional texts); the font comes from a candidate list, a missing one is a message
	const char *lang = getenv("RM8_LANG");
	bool simplified = !(lang && !strcmp(lang, "zh-Hant")) && rm8ZhLoad(scenesRoot + "/zh_t2s.txt");
	std::string fontFile; int fontIdx = 0;
	if(!rm8UiPickFont(fontCandidates(simplified), &fontFile, &fontIdx)){
		const char *t = "找不到中文字体";
		const char *b = "没有找到可以显示中文的系统字体（电脑：宋体 simsun.ttc、微软雅黑 msyh.ttc 等；安卓：Noto Serif / Sans CJK）。可以用环境变量 RM8_FONT=<字体文件>[,<序号>] 指定一个。";
		rm8Log("%s: %s", t, b);
		if(shot.empty()) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, t, b, win);
		return 5;
	}
	if(!g_noticeTitle.empty()) return runNotice(win, shot, W, H);	// a problem with manifest.json (checked before the window opened)

	RGBA bg = {96, 150, 200, 255};
	bool dataOk = rm8OpenPacks(game);
	std::vector<std::string> maps = dataOk ? listMaps(scenesRoot) : std::vector<std::string>();
	if(!dataOk || maps.empty()){
		std::string where = !dataOk ? "游戏数据（Clump、Texture 等）：" + game : "场景数据（scenes）：" + scenesRoot;
		setNotice("找不到游戏数据", std::string("未发现《大富翁8》的游戏数据。本程序不含游戏文件。\n\n") + kHowTo + "，再把输出文件夹里的 game、scenes、manifest.json 放到：\n" + (dataHome.empty() ? "（游戏文件夹 + scenes 文件夹，见启动参数）" : dataHome) + "\n\n缺少的位置：\n" + where);
		return runNotice(win, shot, W, H);
	}
	if(shot.empty()) rm8AudioInit(game);	// [A] no audio device for the offscreen shot
	size_t mapIdx = 0;
	for(size_t i = 0; i < maps.size(); i++) if(maps[i] == startMap) mapIdx = i;

	Rm8Scene sc;
	rm8InfoSetScene(&sc);	// the minimap and the board info box (infoscreens.cpp)
	Camera *cam = Camera::create();
	cam->setFrame(Frame::create());
	Orbit o = cam0;
	bool haveRoles = dataOk && rm8RolesLoadDefs(scenesRoot + "/roles.txt");
	if(haveRoles) rm8PyInit(game + "/Pysrc");	// original Python scripts (computer decisions)
	if(haveRoles) rm8CardLoadDefs(scenesRoot + "/cards.txt");	// card.bin (scripts/export_cards.py); before the first game starts
	bool haveUi = haveRoles && rm8UiInit(scenesRoot + "/ui", fontFile, fontIdx);
	if(haveUi) rm8EventLoadData(scenesRoot);	// scenes/questions.txt (AskPoint event)
	if(haveUi) rm8InfoLoadData(scenesRoot);	// scenes/introduce.txt (names / introductions of the map articles, infoscreens.cpp)
	if(haveUi) rm8NewsLoadData(scenesRoot);	// scenes/news.txt (scripts/export_news.py): the news pictures, the anchor, the festival dates
	if(haveUi) rm8BalanceLoadData(scenesRoot);	// the balance screen's dolls and anchor (roles.txt, event_arts.txt)
	bool haveMini = haveUi && miniGame <= 0 && rm8MiniInit(scenesRoot);	// board minigames (events 0x12..0x17)
	if(haveUi) rm8Log("text: %s (%s)", simplified ? "simplified" : "traditional", rm8ZhConvert(rm8Str(1)).c_str());
	if(miniGame > 0){	// [A] test entry: one minigame without a map (docs/minigames/port.md)
		if(!haveUi){ rm8Log("minigame: data / UI missing"); return 1; }
		int rc = rm8MiniStandalone(win, cam, miniGame, shot, shotTime, W, H, scenesRoot);
		rm8AudioShutdown();
		return rc;
	}
	std::vector<std::string> seatPics;
	std::vector<int> roleIds = {6, 1, 5, 3}; unsigned humanMask = 0; Rm8GameOptions gameOpts;	// [A] demo players; the menus replace them (startFromMenu)
	if(const char *re = getenv("RM8_ROLES")){ roleIds.clear(); for(const char *c = re; *c;){ roleIds.push_back(atoi(c)); while(*c && *c != ',') c++; if(*c) c++; } }	// [A] test hook: the character ids of the demo players
	auto loadMap = [&](size_t idx){
		rm8SaveBoardEnded();	// [V] Game_Cleanup @0x4a1340: record_temp.sav is deleted when a board ends / restarts
		if(sc.world){ sc.world->removeCamera(cam); rm8RolesClear(sc); rm8DestroyScene(sc); }
		Uint32 t0 = SDL_GetTicks();
		if(!rm8LoadScene(scenesRoot + "/" + maps[idx], sc)){ bg = {230, 140, 20, 255}; sc.world = World::create(); }
		sc.world->addCamera(cam);
		rm8MusicPlayBoard(maps[idx]);	// [V] Game_LoadMapData @0x4a31f0 starts the board music playlist once the map is loaded
		if(haveRoles){ rm8StockSetMap(scenesRoot, maps[idx]); rm8RolesSetHumans(humanMask); rm8GameSetOptions(gameOpts); rm8RolesSpawn(sc, roleIds, gameSeed(idx), humanSeat); }	// [A] demo players; seat 0 human on a phone
		if(haveUi){ seatPics.clear(); for(int k = 0; k < rm8TurnRoleCount(); k++) seatPics.push_back(rm8TurnPic(k)); rm8HudInit(seatPics); }
		o = cam0;
		rm8Log("map %s: leaves=%d road_quads=%d articles=%d failed=%d skin_to_cpu=%d animated=%d anim_failed=%d uv_animated=%d pad_quads=%d missing_textures=%d load=%ums",
		       maps[idx].c_str(), sc.nLeaf, sc.nRoadQuads, sc.nArt, sc.nArtFail, sc.nSkinToCpu, sc.nAnimated, sc.nAnimFail, sc.nUVAnimated, sc.nPadQuads, rm8MissingTextures(), SDL_GetTicks() - t0);
	};
	bool inMenu = false, fromMenu = false;	// [A] front-end menus (menu.cpp): shown instead of the board until a game starts
	// [A] save / load (save.cpp, docs/save_port.md): the records live in <user data dir>/save (next to rm8_settings.txt; %APPDATA%/RM8 on desktop, RM8_DATA_DIR overrides); the load menu and the
	// system menu only queue a request (rm8SaveRequestLoad), the board is rebuilt here, outside of the UI callbacks
	{ rm8SaveInit(rm8UserDataDir(scenesRoot), scenesRoot, game); }
	rm8MenuHooks().saveSlotInfo = [](int slot, bool multi, Rm8SaveSlotInfo &o){ return rm8SaveSlotInfo(slot, multi, o); };
	rm8MenuHooks().loadGame = [](int slot, bool multi){ Rm8SaveSlotInfo i; if(!rm8SaveSlotInfo(slot, multi, i)) return false; rm8SaveRequestLoad(rm8SaveSlotPath(slot, multi)); return true; };
	Rm8Setup curSetup;	// the setup of the board that is on (the story restart rebuilds it)
	auto loadSaved = [&](const std::string &path) -> bool {	// [V] CApp_LoadSavedGame @0x406380 -> Save_LoadRecord @0x4a78f0: the map and the seats come from the record, then the state
		Rm8SaveInfo info; std::string err;
		if(!haveRoles || !rm8SaveParse(path, info, err)){ rm8Log("load: %s: %s", path.c_str(), err.c_str()); return false; }
		size_t idx = 0; bool found = false;
		for(size_t i = 0; i < maps.size(); i++) if(maps[i] == info.mapDir){ idx = i; found = true; }
		if(!found){ rm8Log("load: no scene %s", info.mapDir.c_str()); return false; }
		mapIdx = idx;
		const Rm8Setup &st = info.setup;
		curSetup = st;	// a story restart after a loaded game rebuilds this board (it was empty: the restart spawned 0 roles and the game hung)
		roleIds.clear(); humanMask = 0; std::vector<int> outfits;
		for(size_t k = 0; k < st.seats.size(); k++){ outfits.push_back(st.seats[k].outfit); rm8RolesSetOutfits(outfits); roleIds.push_back(st.seats[k].charId); if(!st.seats[k].computer) humanMask |= 1u << k; }
		gameOpts.cash = st.cash; gameOpts.tickets = st.tickets; gameOpts.vehicle = st.vehicle; gameOpts.timeLimit = st.timeLimit; gameOpts.winCond = st.winCond;
		humanSeat = -1;
		rm8SaveStoryLoadSetup(st.story, info.storyLevel);
		rm8StoryBegin(st);
		loadMap(idx);
		rm8PlaceCamera(cam, sc, o.yaw, o.pitch, o.dist, o.tx, o.tz); rm8RolesSetCamera(cam);	// the same-node placement of the loaded roles (Game_MoveRoleToPosition) needs the camera axes
		if(!rm8SaveApply(err)){ rm8Log("load: %s", err.c_str()); return false; }
		// [V] the original keeps the C runtime rand() state out of the record (Game_WriteRecordToFile @0x4a7540), so a reloaded game rolls other dice ("SL"):
		// interactive loads draw fresh seeds for the dice, the rules rand() and Python's random; the save round-trip tests (shot / frames runs, RM8_SAVE_EXACT)
		// keep the stored ones for byte-identical replays
		if(g_shotArg.empty() && g_framesArg <= 0 && !getenv("RM8_SAVE_EXACT")){
			unsigned s = (unsigned)time(nullptr) ^ (SDL_GetTicks() * 2654435761u);
			rm8DiceSeed(s * 7 + 3); rm8GameReseed(s * 13 + 5); rm8SavePySeed(s);
			rm8Log("load: fresh random seeds (dice / rules / python)");
		}
		return true;
	};
	{ rm8CollectInit(rm8UserDataDir(scenesRoot), game); }	// [A] profile (puzzle pieces) + story box next to rm8_settings.txt in the user data folder (scene.cpp rm8UserDataDir: desktop %APPDATA%/RM8, RM8_DATA_DIR overrides)
	if(!skipMenu && haveUi && !maps.empty() && shot.empty() && rm8MenuInit(game, scenesRoot, maps)) inMenu = true;
	const bool menuOk = inMenu;	// the front-end menus exist (the story host falls back to quitting without them)
	if(dataOk){ rm8StoryLoad(scenesRoot); rm8MovieInit(scenesRoot); rm8StoryHostSetScene(&sc, scenesRoot); }	// scenes/story.txt, scenes/movie_arts.txt (scripts/export_story.py)
	bool loadedBoard = !loadPath.empty() && haveRoles && !maps.empty() && loadSaved(loadPath);	// --load <record>
	if(loadedBoard) fromMenu = true;
	if(loadedBoard){}
	else if(!maps.empty() && !inMenu && !getenv("RM8_STORY") && !getenv("RM8_MOVIE")) loadMap(mapIdx);
	else { sc.world = World::create(); sc.world->addCamera(cam); }
	if(inMenu) rm8MenuOpen(true, false);

	if(!shot.empty()){	// offscreen screenshot (desktop verification of the same shaders/pipelines)
		cam->frameBuffer = Raster::create(W, H, 32, Raster::CAMERATEXTURE | Raster::C8888);
		cam->zBuffer = Raster::create(W, H, 0, Raster::ZBUFFER);
		V2d vw = {0.5f, 0.5f * H / W}; if(getenv("RM8_SHOT_GAMEVW")) vw = {kBoardVwY * W / H, kBoardVwY}; cam->setViewWindow(&vw);	// [A] RM8_SHOT_GAMEVW: the live game's view window (bug 44 repro)
		rm8PlaceCamera(cam, sc, o.yaw, o.pitch, o.dist, o.tx, o.tz);
		rm8RolesSetCamera(cam);
		if(haveUi) rm8UiSetScreen(W, H);	// the UI layout of the shot size from the start (test drivers click through the real UI during the simulation)
		static const bool autoRoll = getenv("RM8_AUTO_ROLL") != nullptr;	// test hook: the human seat presses GO whenever it waits (shot mode)
		static const float stepDt = getenv("RM8_SHOT_DT") ? (float)atof(getenv("RM8_SHOT_DT")) : 1.0f / 30;	// test hook: simulate a slow phone (frame time in seconds, the game clamps it to 0.1)
		float trigT = -1;	// test hook RM8_SHOT_ON_INFO=<text>: stop the simulation 0.5 s after an info box containing <text> appeared (hud.cpp)
		for(float t = 0; t < shotTime; t += stepDt){ if(rm8HudShotTriggered() && trigT < 0) trigT = t; if(trigT >= 0 && t - trigT > 0.5f) break; float d = std::min(stepDt, shotTime - t); cardRangeView(sc); rm8UpdateScene(sc, d); rm8RolesUpdate(sc, d); rm8EventUpdate(d); if(autoRoll) rm8RolesHumanRoll(); if(!cardPickCamera(sc, cam)){ float fp[3] = {o.tx, 0, o.tz}; if(getenv("RM8_SHOT_FOLLOW")) rm8RolesFocus(fp); rm8PlaceCamera(cam, sc, o.yaw, o.pitch, o.dist, fp[0], fp[2]); } }
		if(getenv("RM8_SHOT_FOLLOW")){ float fp[3]; rm8RolesFocus(fp); rm8PlaceCamera(cam, sc, o.yaw, o.pitch, o.dist, fp[0], fp[2]); }	// test hook: the shot looks at the turn flow's focus point
		for(int f = 0; f < 3; f++){ rm8RenderReflection(sc, cam); rm8EventPreRender(cam); rm8InfoPreRender(cam); cam->clear(&bg, Camera::CLEARIMAGE | Camera::CLEARZ); cam->beginUpdate(); rm8RenderScene(sc, true); rm8PrtRender(cam); rm8RolesRender(); rm8RenderArticleGlass(); rm8RolesRenderLate(cam); if(haveUi){ rm8UiSetScreen(W, H); rm8HudRender(cam); } cam->endUpdate(); }
		Image *img = cam->frameBuffer->toImage();
		if(!img){ rm8Log("readback failed"); return 1; }
		writePNG(img, shot.c_str()); img->destroy();
		if(FILE *chk = fopen(shot.c_str(), "rb")) fclose(chk); else { rm8Log("error: could not write %s", shot.c_str()); return 1; }
		rm8Log("wrote %s", shot.c_str());
		return 0;
	}

	int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
	setupWindowCamera(cam, dw, dh);
	rm8UiSetScreen(dw, dh);
	rm8Log("drawable %dx%d", dw, dh);

	struct Finger { SDL_FingerID id; float x, y; Uint32 t0; float sx, sy; bool ui; bool held = false; };
	std::vector<Finger> fingers;
	Uint32 lastTap = 0, fpsT0 = SDL_GetTicks(), lastTick = SDL_GetTicks(); int fpsFrames = 0, frames = 0;
	Uint64 updTicks = 0, rndTicks = 0, hudTicks = 0;
	// vsync: no point rendering faster than the display (battery/heat on phones); RM8_NOVSYNC=1 to measure raw speed
	const uint32 flipFlags = getenv("RM8_NOVSYNC") ? 0 : Raster::FLIPWAITVSYNCH;	// librw sets the swap interval on every showRaster
	bool run = true, paused = false, mouseUi = false, mouseMoved = false; float mouseX0 = 0, mouseY0 = 0, rightX0 = 0, rightY0 = 0;
	int followIdx = -1;
	auto pan = [&](float dx, float dy){	// screen deltas in [0,1] units
		if(rm8CardsPicking()) return;	// [V] no panning / turning / zooming while a card target is chosen
		float yr = o.yaw * 3.14159265f / 180.0f, k = sc.MW * 25.0f * 1.42f * 2.0f * o.dist;
		// screen right = (-cos, 0, -sin), screen up on the ground = forward = (-sin, 0, cos)
		o.tx += (dx * cosf(yr) - dy * sinf(yr)) * k;
		o.tz += (dx * sinf(yr) + dy * cosf(yr)) * k;
		float lim = sc.MW * 25.0f;
		o.tx = std::max(-lim, std::min(lim, o.tx)); o.tz = std::max(-lim, std::min(lim, o.tz));
		// [V] Camera_MoveClamped @0x41d500: the look-at point stays inside the bounding box of the board's road tiles grown by 50
		// (TerraMgr_CreateAll @0x4f07d0: g_TerraMgr +0x48..+0x5c)
		if(!sc.rnodes.empty()){
			float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
			for(const Rm8RoadNode &n : sc.rnodes){ x0 = std::min(x0, n.x); x1 = std::max(x1, n.x); z0 = std::min(z0, n.z); z1 = std::max(z1, n.z); }
			float fp[3] = {0, 0, 0}; rm8RolesFocus(fp);
			float cx = std::max(x0 - 50.0f, std::min(x1 + 50.0f, fp[0] + o.tx)), cz = std::max(z0 - 50.0f, std::min(z1 + 50.0f, fp[2] + o.tz));
			o.tx = cx - fp[0]; o.tz = cz - fp[2];
		}
	};
	auto orbit = [&](float dx, float dy){ if(rm8CardsPicking()) return; o.yaw += dx * 180.0f; o.pitch = std::max(8.0f, std::min(89.5f, o.pitch + dy * 90.0f)); };
	// [V] Camera_ZoomClamped @0x41b030: the camera distance along the view axis stays in 600..1000 (start 800, Camera_CreateGlobal @0x41dda0),
	// and zooming tilts the camera with it (Camera_PitchAroundPoint @0x41ae50 via Camera_UpdateSmoothMotion @0x41d000: pitch += -delta / 20,
	// clamped 30..50): closer = flatter, farther = steeper. A wider screen still shows more to the sides ([A] a feature: the original is 4:3)
	auto zoom = [&](float f){
		if(rm8CardsPicking()) return;
		float ext2 = sc.MW * 25.0f * 1.42f * 2.0f;	// rm8PlaceCamera: distance = ext2 * dist factor
		float d0 = o.dist * ext2, d1 = std::max(600.0f, std::min(1000.0f, d0 * f));
		o.dist = d1 / ext2;
		o.pitch = std::max(30.0f, std::min(50.0f, o.pitch + (d1 - d0) / 20.0f));
	};
	auto nextMap = [&](){ if(maps.size() > 1){ mapIdx = (mapIdx + 1) % maps.size(); loadMap(mapIdx); } };
	// [A] menus -> board: the chosen map, seats and start options (docs/menus.md)
	auto unloadBoard = [&](){	// the board is deleted; the empty scene stays for the menus / cutscenes
		if(sc.world){ sc.world->removeCamera(cam); rm8RolesClear(sc); rm8DestroyScene(sc); }
		sc.world = World::create(); sc.world->addCamera(cam);
	};
	// [V] CApp_StartNewGame: the board of a setup (story.cpp records the story, hands the puzzle pieces to the NPC spawner and applies the start values)
	auto beginBoard = [&](const Rm8Setup &st){
		curSetup = st;
		std::string dir = rm8MenuMapDir(st.map);
		if(dir.empty()) dir = rm8StoryMapDir(st.map);
		for(size_t i = 0; i < maps.size(); i++) if(maps[i] == dir) mapIdx = i;
		roleIds.clear(); humanMask = 0; std::vector<int> outfits;
		for(size_t k = 0; k < st.seats.size(); k++){ outfits.push_back(st.seats[k].outfit); rm8RolesSetOutfits(outfits); roleIds.push_back(st.seats[k].charId); if(!st.seats[k].computer) humanMask |= 1u << k; }
		gameOpts.cash = st.cash; gameOpts.tickets = st.tickets; gameOpts.vehicle = st.vehicle; gameOpts.timeLimit = st.timeLimit; gameOpts.winCond = st.winCond;
		humanSeat = -1;
		rm8StoryBegin(st);
		loadMap(mapIdx);
		rm8StoryApplyStart();	// [V] Game_StartNewRound @0x4962a0 (story: carried items + the pass entry's start values)
		inMenu = false; fromMenu = true;
	};
	// [A] menus -> board (docs/menus.md). A story game of layer 0 first plays the hero's start movie ([V] chsMapMenu 0x65 -> CApp_PlayMovieChapter(role - 1) @0x4050a0;
	// Movie_Finish @0x515d40 -> CApp_StartNewGame)
	auto startFromSetup = [&](const Rm8Setup &st){
		rm8MenuClose();
		if(st.story && rm8StoryConfirmed() && rm8StorySetupLevel() == 0 && rm8MovieAvailable() && !getenv("RM8_NOMOVIE")){
			std::vector<std::string> scripts = rm8StoryMovieScripts(st.seats[0].charId, 0);
			if(!scripts.empty()){
				inMenu = false; fromMenu = false;
				Rm8Setup s2 = st;
				rm8MovieStart(sc, cam, scripts, [&, s2](){ beginBoard(s2); });
				return;
			}
		}
		beginBoard(st);
	};
	auto startFromMenu = [&](){ startFromSetup(rm8MenuSetup()); };
	// [V] game state 3 (exit confirmed in gsys) -> CApp_SwitchToGameScene(3): back to the map menu (0x4a17b0)
	auto backToMenu = [&](){
		// [V] CApp_SwitchToGameScene(3) @0x405650 from 0x4a17b0: a story game goes back to the story role menu (single menu flow), a free game to the map menu
		bool story = rm8StoryActive() && rm8StoryConfirmed();
		unloadBoard();
		rm8MenuGameSystemOpen(false);
		inMenu = true; fromMenu = false;
		if(story) rm8MenuOpenPage(RM8PAGE_ROLES); else rm8MenuOpen(false, true);
	};
	// the application side of the story game states (CGamePlayScene__Game_OnUpdate @0x4a17b0, story.h)
	{
		Rm8StoryHost &sh = rm8StoryHost();
		sh.start = [&](const Rm8Setup &st){ startFromSetup(st); };
		sh.restart = [&](){ Rm8Setup s = curSetup; beginBoard(s); };	// state 2: Game_Cleanup, Game_LoadAllConfigTables, Game_InitGame (no start movie)
		auto toMenu = [&](){
			if(!menuOk){ rm8Log("story: the game ends here (no menus in this run)"); run = false; return; }
			backToMenu();
		};
		// [V] game state 3 of a story game (the player declined "<str#93>"): the role menu (state 2); state 4 (a stage was cleared): the map menu in its state 0xB
		auto toPage = [&](Rm8MenuPage page){
			if(!menuOk){ rm8Log("story: the game ends here (no menus in this run)"); run = false; return; }
			unloadBoard();
			rm8MenuGameSystemOpen(false);
			inMenu = true; fromMenu = false; rm8MenuOpenPage(page);
		};
		sh.roleMenu = [&, toPage](){ toPage(RM8PAGE_ROLES); };	// [A] the local lambdas are captured by value: they outlive this block
		sh.mapMenu = [&, toPage, toMenu](bool story){ if(story) toPage(RM8PAGE_MAP_NEXT); else toMenu(); };	// the free game: state 3 / 4 = the map menu (CApp_SwitchToGameScene(3))
		sh.ending = [&](){	// state 5: CApp_PlayMovieRange(role - 1, 1), then the staff movie (Movie_Finish @0x515d40) and the single menu
			unloadBoard();
			if(menuOk) rm8MenuGameSystemOpen(false);
			fromMenu = false; inMenu = false;
			std::vector<std::string> scripts = rm8StoryMovieScripts(rm8StoryHero(), 1);
			if(getenv("RM8_NOMOVIE")) scripts.clear();
			rm8MovieStart(sc, cam, scripts, [&](){
				if(menuOk){ inMenu = true; rm8MenuOpen(false, false); rm8MenuPlayStaffCredits(); }
				else { rm8Log("story: the ending is over (no menus in this run)"); run = false; }
			});
		};
	}

	// test hooks without the menus: RM8_STORY=<role>[,<map>] (story.h), RM8_MOVIE=<script>[,<script>...] plays cutscenes and quits (movie.h)
	if(getenv("RM8_STORY")){ Rm8Setup st; if(rm8StoryDebugStart(st)) startFromSetup(st); else rm8Log("story: RM8_STORY could not start (story.txt / hero?)"); }
	else if(const char *mv = getenv("RM8_MOVIE")){
		std::vector<std::string> scripts; std::string cur;
		for(const char *c = mv;; c++){ if(*c == ',' || !*c){ if(!cur.empty()) scripts.push_back(cur); cur.clear(); if(!*c) break; } else cur += *c; }
		rm8MovieStart(sc, cam, scripts, [&](){ rm8Log("movie test: done after %.1f s", rm8MovieElapsed()); run = false; });
	}

	while(run){
		SDL_Event e;
		while(SDL_PollEvent(&e)){
			if(inMenu && rm8MenuEvent(e, dw, dh)) continue;	// [A] the menus take pointer / key input
			if(rm8MovieActive()){	// a cutscene: a click / tap / key skips the running script ([V] Cls_5dff48_vf13 @0x515b50); Esc / back skips all of them
				bool used = true;
				if(e.type == SDL_KEYDOWN){ if(e.key.keysym.scancode == SDL_SCANCODE_AC_BACK || e.key.keysym.sym == SDLK_ESCAPE) rm8MovieSkipAll(); else rm8MovieSkip(); }
				else if((e.type == SDL_MOUSEBUTTONDOWN && e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_LEFT) || e.type == SDL_FINGERDOWN) rm8MovieSkip();
				else if(e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERUP || e.type == SDL_FINGERMOTION || e.type == SDL_KEYUP) {}
				else used = false;
				if(used) continue;
			}
			if(haveMini && rm8MiniActive()){	// a board minigame takes every input (the same mapping as rm8MiniStandalone)
				bool used = true;
				switch(e.type){
				case SDL_KEYDOWN: if(int k = rm8MiniKeyFromSdl(e.key.keysym.sym)) rm8MiniKey(k, true); break;
				case SDL_KEYUP: if(int k = rm8MiniKeyFromSdl(e.key.keysym.sym)) rm8MiniKey(k, false); break;
				case SDL_MOUSEMOTION: if(e.motion.which != SDL_TOUCH_MOUSEID) rm8MiniPointer(2, (float)e.motion.x, (float)e.motion.y); break;
				case SDL_MOUSEBUTTONDOWN: if(e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_LEFT) rm8MiniPointer(0, (float)e.button.x, (float)e.button.y); break;
				case SDL_MOUSEBUTTONUP: if(e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_LEFT) rm8MiniPointer(1, (float)e.button.x, (float)e.button.y); break;
				case SDL_FINGERDOWN: rm8MiniPointer(0, e.tfinger.x * dw, e.tfinger.y * dh, (int)e.tfinger.fingerId, true); break;
				case SDL_FINGERUP: rm8MiniPointer(1, e.tfinger.x * dw, e.tfinger.y * dh, (int)e.tfinger.fingerId, true); break;
				case SDL_FINGERMOTION: rm8MiniPointer(2, e.tfinger.x * dw, e.tfinger.y * dh, (int)e.tfinger.fingerId, true); break;
				default: used = false; break;
				}
				if(used) continue;
			}
			switch(e.type){
			case SDL_QUIT: run = false; break;
			case SDL_APP_WILLENTERBACKGROUND: case SDL_APP_DIDENTERBACKGROUND: paused = true; rm8AudioPause(true); break;
			case SDL_APP_DIDENTERFOREGROUND: paused = false; rm8AudioPause(false); break;
			case SDL_WINDOWEVENT:
				if(e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED){
					SDL_GL_GetDrawableSize(win, &dw, &dh);
					if(dw > 0 && dh > 0){ setupWindowCamera(cam, dw, dh); rm8UiSetScreen(dw, dh); }
					rm8Log("drawable %dx%d", dw, dh);
				}
				break;
			case SDL_KEYDOWN:
				if(e.key.keysym.scancode == SDL_SCANCODE_AC_BACK || e.key.keysym.sym == SDLK_ESCAPE){ if(rm8InfoClose()){} else if(fromMenu && rm8SaveUiVisible()) rm8SaveUiClose(); else if(fromMenu) rm8MenuGameSystemOpen(!rm8MenuGameSystemVisible()); else run = false; }	// [A] back / Esc: system menu (gsys) in a menu game; closes the save / load dialog first
				else if(e.key.keysym.sym != SDLK_KP_ENTER && autoPlayKey(e.key.keysym.sym)){}	// [V] auto play keys (docs/autoplay.md)
				else if(e.key.keysym.sym == SDLK_n) nextMap();
				else if(e.key.keysym.sym == SDLK_SPACE || e.key.keysym.sym == SDLK_RETURN) rm8RolesHumanRoll();
				else if(e.key.keysym.sym == SDLK_PERIOD) rm8SkipToggleFast();	// [V] key 0x2E: InitConfig_SetSpeed(0x32), wait scale 0 / walk x14 (skip.h)
				else if(e.key.keysym.sym == SDLK_KP_ENTER) rm8SkipRequest();	// [V] key 0xA7 = the numpad Enter acts as a left click (CApp_OnKeyDown @0x405A90): scene vf13
				break;
			case SDL_FINGERDOWN:
				fingers.push_back({e.tfinger.fingerId, e.tfinger.x, e.tfinger.y, SDL_GetTicks(), e.tfinger.x, e.tfinger.y,
				                   fingers.empty() && rm8UiPointerDown(e.tfinger.x * dw, e.tfinger.y * dh, true)});	// the UI gets the first finger first
				break;
			case SDL_FINGERUP: {
				auto it = std::find_if(fingers.begin(), fingers.end(), [&](const Finger &f){ return f.id == e.tfinger.fingerId; });
				if(it != fingers.end()){
					if(it->held){ fingers.erase(it); break; }	// a long press was the inspection (below): no tap
					bool tap = fingers.size() == 1 && SDL_GetTicks() - it->t0 < 250 && fabsf(e.tfinger.x - it->sx) < 0.02f && fabsf(e.tfinger.y - it->sy) < 0.02f;
					bool ui = it->ui;
					fingers.erase(it);
					if(ui){ if(tap) rm8SkipTap(cam, 0, 0, true); rm8UiPointerUp(e.tfinger.x * dw, e.tfinger.y * dh); break; }	// [V] vf13 runs for a press the UI took too, before the click action
					// a tap on the board: card target selection, then the scenery ([V] PickByRay, boardfx); without the UI (viewer mode) a
					// double tap switches the map and a tap rolls
					// (card target selection -> click skip of the running waits -> scenery: rm8SkipTap)
					if(tap && !rm8SkipTap(cam, e.tfinger.x * dw, e.tfinger.y * dh, false) && !haveUi){
						if(SDL_GetTicks() - lastTap < 400){ nextMap(); lastTap = 0; } else { lastTap = SDL_GetTicks(); rm8RolesHumanRoll(); }
					}
				}
				break; }
			case SDL_FINGERMOTION: {
				auto it = std::find_if(fingers.begin(), fingers.end(), [&](const Finger &f){ return f.id == e.tfinger.fingerId; });
				if(it == fingers.end()) break;
				if(fingers.size() == 1){ if(!it->ui) pan(e.tfinger.dx, e.tfinger.dy); else rm8UiPointerMove(e.tfinger.x * dw, e.tfinger.y * dh); }	// one finger drags the board: the world follows the finger (same sign as the two-finger pan)
				else if(fingers.size() >= 2){
					Finger &a = fingers[0], &b = fingers[1];
					float ax = a.x, ay = a.y, bx = b.x, by = b.y;
					float od = hypotf(ax - bx, ay - by), omx = (ax + bx) / 2, omy = (ay + by) / 2;
					it->x = e.tfinger.x; it->y = e.tfinger.y;
					float nd = hypotf(a.x - b.x, a.y - b.y), nmx = (a.x + b.x) / 2, nmy = (a.y + b.y) / 2;
					if(nd > 0.01f && od > 0.01f) zoom(od / nd);
					pan(nmx - omx, nmy - omy);
				}
				it->x = e.tfinger.x; it->y = e.tfinger.y;
				break; }
			case SDL_MOUSEMOTION:
				if(e.motion.which == SDL_TOUCH_MOUSEID) break;
				rm8UiPointerMove((float)e.motion.x, (float)e.motion.y);
				if(e.motion.state & SDL_BUTTON_LMASK && hypotf(e.motion.x - mouseX0, e.motion.y - mouseY0) > 6) mouseMoved = true;
				if(e.motion.state & SDL_BUTTON_LMASK){ if(!mouseUi) pan(-e.motion.xrel / (float)dw, -e.motion.yrel / (float)dh); }
				else if(e.motion.state & SDL_BUTTON_RMASK) orbit(e.motion.xrel / (float)dw, e.motion.yrel / (float)dh);
				break;
			case SDL_MOUSEBUTTONDOWN:
				if(e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_RIGHT){ rightX0 = (float)e.button.x; rightY0 = (float)e.button.y; break; }
				if(e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) break;
				mouseUi = rm8UiPointerDown((float)e.button.x, (float)e.button.y);
				mouseMoved = false; mouseX0 = (float)e.button.x; mouseY0 = (float)e.button.y;
				break;
			case SDL_MOUSEBUTTONUP:
				if(e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_RIGHT){	// [V] the right press (0x120) runs vf13 first (skip), then vf11; a drag is the orbit
					if(hypotf(e.button.x - rightX0, e.button.y - rightY0) <= 6 && !rm8InfoClose() && !rm8CardsOnRightClick((float)e.button.x, (float)e.button.y)) rm8SkipRequest();	// the card menu / cancel of a selection first (cards.cpp)
					break;
				}
				if(e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) break;
				if(mouseUi){ if(!mouseMoved) rm8SkipTap(cam, 0, 0, true); rm8UiPointerUp((float)e.button.x, (float)e.button.y); mouseUi = false; }
				else if(!mouseMoved) rm8SkipTap(cam, (float)e.button.x, (float)e.button.y, false);	// card target selection, then the click skip, then the scenery
				break;
			case SDL_MOUSEWHEEL:
				if(e.wheel.which == SDL_TOUCH_MOUSEID) break;
				zoom(e.wheel.y > 0 ? 0.9f : 1.1f);
				break;
			}
		}
		static const int holdMs = getenv("RM8_HOLD_MS") ? atoi(getenv("RM8_HOLD_MS")) : 500;	// the long press time (test hook: shorter)
		if(fingers.size() == 1 && !fingers[0].ui && !fingers[0].held && !inMenu && SDL_GetTicks() - fingers[0].t0 >= (Uint32)holdMs
		   && fabsf(fingers[0].x - fingers[0].sx) < 0.02f && fabsf(fingers[0].y - fingers[0].sy) < 0.02f){
			fingers[0].held = true;	// [A] a long press on the board = the original's mouse press / hover on a role, a god, a plot or a landmark: its info box (infoscreens.cpp)
			bool shown = rm8InfoBoardTap(cam, fingers[0].sx * dw, fingers[0].sy * dh); if(getenv("RM8_INFO_AUTO")) rm8Log("long press at (%.0f, %.0f) -> %d", fingers[0].sx * dw, fingers[0].sy * dh, shown);
		}
		if(paused){ SDL_Delay(50); continue; }
		if(fromMenu && !inMenu && rm8MenuGameSystemExitRequested()) backToMenu();
		{	// [A] a load chosen in the system menu: the board is rebuilt from the record ([V] game state 7 -> Save_LoadRecord @0x4a78f0)
			std::string lp;
			if(fromMenu && !inMenu && rm8SaveTakeLoad(lp)){ rm8MenuGameSystemOpen(false); if(!loadSaved(lp)){ rm8Log("load failed: back to the menu"); backToMenu(); } lastTick = SDL_GetTicks(); continue; }
		}
		if(rm8MovieActive()){	// [A] a story cutscene replaces the board and the menus (movie.cpp)
			Uint32 mnow = SDL_GetTicks(); float mdt = std::min(0.1f, (mnow - lastTick) / 1000.0f); lastTick = mnow;
			rm8UiSetScreen(dw, dh);
			rm8MovieUpdate(mdt);
			if(rm8MovieActive()){
				RGBA black = {0, 0, 0, 255};
				cam->clear(&black, Camera::CLEARIMAGE | Camera::CLEARZ);
				cam->beginUpdate(); rm8MovieRender(cam); cam->endUpdate();
				rm8MovieTestHooks(dw, dh);
				cam->showRaster(flipFlags);
				frames++;
				if(maxFrames > 0 && frames >= maxFrames) run = false;
			}
			continue;
		}
		if(inMenu){	// [A] front-end menus (menu.cpp)
			Uint32 mnow = SDL_GetTicks(); float mdt = std::min(0.1f, (mnow - lastTick) / 1000.0f); lastTick = mnow;
			Rm8MenuResult mr = rm8MenuUpdate(mdt);
			RGBA black = {0, 0, 0, 255};
			cam->clear(&black, Camera::CLEARIMAGE | Camera::CLEARZ);
			cam->beginUpdate(); rm8MenuRender(cam); cam->endUpdate();
			rm8MenuPostRender(dw, dh);
			cam->showRaster(flipFlags);
			frames++;
			if(maxFrames > 0 && frames >= maxFrames) run = false;
			if(mr == RM8MENU_QUIT) run = false;
			else if(mr == RM8MENU_START_GAME) startFromMenu();
			else if(mr == RM8MENU_PLAY_ENDING){	// finalMenu: CApp_PlayMovieRange(role - 1, 0); Movie_Finish @0x515d40 returns to the finalMenu (state 7)
				std::vector<std::string> scripts = rm8StoryMovieScripts(rm8MenuMovieRole(), 1);
				rm8MenuClose(); inMenu = false;
				rm8MovieStart(sc, cam, scripts, [&](){ inMenu = true; rm8MenuOpenPage(RM8PAGE_FINAL); });
			}
			else if(mr == RM8MENU_LOAD_GAME){	// [A] the load menu queued a record (rm8SaveRequestLoad): the board is rebuilt here
				std::string lp; rm8MenuClose(); inMenu = false;
				if(rm8SaveTakeLoad(lp) && loadSaved(lp)) fromMenu = true;
				else { rm8Log("load failed: back to the main menu"); unloadBoard(); inMenu = true; fromMenu = false; rm8MenuOpen(false, false); }
				lastTick = SDL_GetTicks();
			}
			continue;
		}
		if(haveMini && rm8MiniActive()){	// [V] the minigame host replaces the board (CMiniGameHost task); the event task collects the result
			Uint32 mnow = SDL_GetTicks(); float mdt = std::min(0.1f, (mnow - lastTick) / 1000.0f); lastTick = mnow;
			rm8MiniSetScreen(dw, dh); rm8UiSetScreen(dw, dh);
			rm8MiniUpdate(mdt);
			rm8EventUpdate(mdt);
			if(rm8MiniActive()){
				RGBA black = {0, 0, 0, 255};
				cam->clear(&black, Camera::CLEARIMAGE | Camera::CLEARZ);
				cam->beginUpdate(); rm8MiniRender(cam); cam->endUpdate();
				cam->showRaster(flipFlags);
				frames++;
				if(maxFrames > 0 && frames >= maxFrames) run = false;
				continue;
			}
		}
		Uint32 now = SDL_GetTicks();
		Uint64 c0 = SDL_GetPerformanceCounter();
		float dt = std::min(0.1f, (now - lastTick) / 1000.0f);	// [V] the game clamps dt to 0.1 s
		rm8UpdateScene(sc, dt);
		rm8RolesSetCamera(cam);	// last frame's camera
		{ static const bool autoRoll = getenv("RM8_AUTO_ROLL") != nullptr; if(autoRoll) rm8RolesHumanRoll(); }	// test hook (as in shot mode): a human seat presses GO whenever it waits
		{ static bool fastDone = getenv("RM8_FAST") == nullptr; if(!fastDone){ fastDone = true; SDL_Event ev; memset(&ev, 0, sizeof ev); ev.type = SDL_KEYDOWN; ev.key.state = SDL_PRESSED; ev.key.keysym.sym = SDLK_PERIOD; SDL_PushEvent(&ev); } }	// test hook: the '.' key once (fast mode, skip.h)
		if(int fk = rm8HudAutoPlayTest(dt)){ SDL_Event ev; memset(&ev, 0, sizeof ev); ev.type = SDL_KEYDOWN; ev.key.state = SDL_PRESSED; ev.key.keysym.sym = SDLK_F1 + fk - 1; SDL_PushEvent(&ev); }	// test hook RM8_AUTO_PLAY / RM8_AUTO_PLAY_OFF (hud.cpp): the off key goes through the event loop
		rm8RolesUpdate(sc, dt);
		rm8EventUpdate(dt);
		rm8StoryUpdate(dt);	// the 14 s win scene, test hooks (story.cpp)
		if(rm8StoryDispatch()) continue;	// a game state change of the story (restart / menus / ending): the board may be gone
		Uint64 c1 = SDL_GetPerformanceCounter();
		lastTick = now;
		{
			float fp[3] = {0, 0, 0};
			int serial = rm8RolesFocus(fp);	// [V] the turn flow snaps the camera to its focus; panning offsets from it
			if(serial != followIdx){ followIdx = serial; o.tx = o.tz = 0; }
			cardRangeView(sc);
			if(!cardPickCamera(sc, cam)) rm8PlaceCamera(cam, sc, o.yaw, o.pitch, o.dist, fp[0] + o.tx, fp[2] + o.tz);
			// [A] comparison hook: scenes/camfile.txt (scripts/game_camera.py values: LTM 16, view window 2, near, far) pins the
			// camera to the original game's view (stretched like RichMan8Fix Stretch=1)
			static int camFix = -1; static float cv[20];
			if(camFix < 0){ FILE *cf = fopen((scenesRoot + "/camfile.txt").c_str(), "r"); camFix = 0; if(cf){ camFix = 1; for(int k = 0; k < 20; k++) if(fscanf(cf, "%f", &cv[k]) != 1) camFix = 0; fclose(cf); } }
			if(camFix == 1){
				Matrix m; m.setIdentity();
				m.right = {cv[0], cv[1], cv[2]}; m.up = {cv[4], cv[5], cv[6]}; m.at = {cv[8], cv[9], cv[10]}; m.pos = {cv[12], cv[13], cv[14]}; m.flags = 0;
				cam->getFrame()->transform(&m, COMBINEREPLACE);
				V2d gw = {cv[16], cv[17]}; cam->setViewWindow(&gw); cam->setNearPlane(cv[18]); cam->setFarPlane(cv[19]);
			}
		}
		rm8RenderReflection(sc, cam);
		rm8EventPreRender(cam);
		rm8InfoPreRender(cam);	// the minimap raster (infoscreens.cpp)
		cam->clear(&bg, Camera::CLEARIMAGE | Camera::CLEARZ);
		cam->beginUpdate();
		if(!maps.empty()){ rm8RenderScene(sc, true); rm8PrtRender(cam); rm8RolesRender(); rm8RenderArticleGlass(); rm8RolesRenderLate(cam); if(haveUi){ Uint64 h0 = SDL_GetPerformanceCounter(); rm8HudRender(cam); hudTicks += SDL_GetPerformanceCounter() - h0; } }
		cam->endUpdate();
		if(fromMenu){ rm8MenuGameTick(); rm8MenuPostRender(dw, dh); }	// [A] test hook for menu games (system menu, shots)
		{	// [A] test hook RM8_GAME_SHOT=<png>: in the interactive loop (--frames, story tests) write the frame 30 frames after the RM8_SHOT_ON_INFO / RM8_SHOT_ON_INFO_N trigger (hud.cpp), then quit
			static const char *gs = getenv("RM8_GAME_SHOT"); static int trigF = -1;
			if(gs){
				static const int shotFrame = getenv("RM8_GAME_SHOT_FRAME") ? atoi(getenv("RM8_GAME_SHOT_FRAME")) : -1;	// RM8_GAME_SHOT_FRAME=<n>: or at frame n
				if(((shotFrame >= 0 && frames >= shotFrame) || rm8HudShotTriggered()) && trigF < 0) trigF = frames;
				if(trigF >= 0 && frames - trigF >= 30){
					typedef void (*ReadPixelsFn)(int, int, int, int, unsigned, unsigned, void*);
					ReadPixelsFn rp = (ReadPixelsFn)SDL_GL_GetProcAddress("glReadPixels");
					if(rp){
						std::vector<unsigned char> px((size_t)dw * dh * 4);
						rp(0, 0, dw, dh, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, px.data());
						Image *gi = Image::create(dw, dh, 32); gi->allocate();
						for(int y = 0; y < dh; y++) memcpy(gi->pixels + (size_t)y * gi->stride, &px[(size_t)(dh - 1 - y) * dw * 4], (size_t)dw * 4);
						writePNG(gi, gs); gi->destroy();
						rm8Log("game shot: %s", gs);
					}else rm8Log("game shot: no glReadPixels");
					run = false;
				}
			}
		}
		Uint64 c2 = SDL_GetPerformanceCounter();
		cam->showRaster(flipFlags);
		updTicks += c1 - c0; rndTicks += c2 - c1;
		fpsFrames++; frames++;
		if(SDL_GetTicks() - fpsT0 >= 5000){
			double f = (double)SDL_GetPerformanceFrequency() * fpsFrames / 1000.0;
			rm8Log("fps %.1f | cpu per frame: update %.2f ms, render submit %.2f ms (hud %.2f)", fpsFrames * 1000.0f / (SDL_GetTicks() - fpsT0), updTicks / f, rndTicks / f, hudTicks / f);
			fpsT0 = SDL_GetTicks(); fpsFrames = 0; updTicks = rndTicks = hudTicks = 0;
		}
		if(maxFrames > 0 && frames >= maxFrames) run = false;
		if(cycle > 0 && frames % cycle == 0){ nextMap(); o.yaw = frames * 0.5f; }
	}
	rm8Log("exit after %d frames", frames);
	rm8AudioShutdown();
	if(sc.world){ sc.world->removeCamera(cam); rm8RolesClear(sc); rm8DestroyScene(sc); }
	return 0;
}
