// Movie playback, see video.h. [V] = verified in the decompile (address), [A] = port adaptation.
// [V] The original (Movie_LoadBinkVideo @0x516330 / Movie_UpdateBinkPlayback @0x5165d0 / Movie_RenderBinkFrame @0x5161a0) opens the .bik with
// BinkOpen, plays its sound through Bink's DirectSound, decodes a frame whenever BinkWait says one is due, blits it scaled to the client
// area (BinkBufferSetScale) and ends when the last frame was shown (Movie_FinishBinkPlayback @0x516890).
// [A] Here: MPEG-1 / MP2 (converted by scripts/convert_videos.py), pl_mpeg decoding, picture in a texture, letterboxed 4:3.
#include "video.h"
#include "audio.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

#define PL_MPEG_IMPLEMENTATION
#include "../extern/pl_mpeg/pl_mpeg.h"

using namespace rw;

namespace {

struct Movie {
	plm_t *plm = nullptr;
	int w = 0, h = 0;
	double fps = 30;
	Raster *ras = nullptr; Texture *tex = nullptr;
	std::vector<uint8_t> rgba;		// top-down picture (flipped into the raster on upload)
	bool dirty = false, audio = false, videoEnded = false, audioEnded = false, havePending = false, finished = false;
	plm_frame_t *pending = nullptr; double pendingTime = 0;
	double clock = 0, shownTime = 0;	// movie clock: the audio stream when there is a device, else wall time
	Rm8VideoStats st;
} g_m;

const double AUDIO_AHEAD = 0.4;		// seconds of sound kept queued (the device buffer is ~0.05 s)

void closeAll()
{
	if(g_m.audio) rm8AudioStreamClose();
	if(g_m.plm) plm_destroy(g_m.plm);
	if(g_m.tex){ g_m.tex->destroy(); g_m.ras = nullptr; }	// the texture owns its raster
	Rm8VideoStats st = g_m.st;
	g_m = Movie();
	g_m.st = st;
}

void upload()
{
	if(!g_m.dirty || !g_m.ras) return;
	g_m.dirty = false;
	uint8 *p = g_m.ras->lock(0, Raster::LOCKWRITE | Raster::LOCKNOFETCH);
	if(!p) return;
	// librw's GL3 rasters hold rows bottom-up (docs: ui.cpp uploadAtlas): the picture goes in flipped
	const size_t row = (size_t)g_m.w * 4;
	for(int y = 0; y < g_m.h; y++) memcpy(p + (size_t)(g_m.h - 1 - y) * row, &g_m.rgba[(size_t)y * row], row);
	g_m.ras->unlock(0);
}

void pullAudio()
{
	if(!g_m.audio) return;
	// keep the stream queued AUDIO_AHEAD seconds ahead of the device
	int guard = 0;
	while(!g_m.audioEnded && rm8AudioStreamQueued() < AUDIO_AHEAD && guard++ < 64){
		plm_samples_t *s = plm_decode_audio(g_m.plm);
		if(!s){ g_m.audioEnded = true; break; }
		rm8AudioStreamPush(s->interleaved, (int)s->count);
		g_m.st.audioSeconds += (double)s->count / plm_get_samplerate(g_m.plm);
	}
}

}	// namespace

bool rm8VideoOpen(const std::string &path)
{
	if(g_m.plm || g_m.tex) closeAll();
	g_m = Movie(); g_m.st = Rm8VideoStats();
	FILE *f = fopen(path.c_str(), "rb");
	if(!f){ rm8Log("video: cannot open %s", path.c_str()); return false; }
	g_m.plm = plm_create_with_file(f, 1);
	if(!g_m.plm){ fclose(f); return false; }
	g_m.w = plm_get_width(g_m.plm); g_m.h = plm_get_height(g_m.plm); g_m.fps = plm_get_framerate(g_m.plm);
	if(g_m.w < 16 || g_m.h < 16 || g_m.w > 4096 || g_m.h > 4096){ rm8Log("video: %s has no usable video stream", path.c_str()); closeAll(); return false; }
	g_m.rgba.assign((size_t)g_m.w * g_m.h * 4, 0);
	for(size_t i = 3; i < g_m.rgba.size(); i += 4) g_m.rgba[i] = 255;
	g_m.ras = Raster::create(g_m.w, g_m.h, 32, Raster::TEXTURE | Raster::C8888);
	g_m.tex = g_m.ras ? Texture::create(g_m.ras) : nullptr;
	if(!g_m.tex){ rm8Log("video: cannot create the %dx%d texture", g_m.w, g_m.h); if(g_m.ras) g_m.ras->destroy(); g_m.ras = nullptr; closeAll(); return false; }
	g_m.tex->setFilter(Texture::LINEAR); g_m.tex->setAddressU(Texture::CLAMP); g_m.tex->setAddressV(Texture::CLAMP);
	g_m.dirty = true; upload();
	int rate = plm_get_samplerate(g_m.plm);
	g_m.audio = rate > 0 && plm_get_audio_enabled(g_m.plm) && rm8AudioStreamOpen(rate, 2);
	if(!g_m.audio) plm_set_audio_enabled(g_m.plm, 0);	// no soundtrack / no device: wall-clock timing, the audio packets are skipped
	rm8Log("video: %s %dx%d %.2f fps, %.1f s, audio %s (%d Hz)", path.c_str(), g_m.w, g_m.h, g_m.fps, plm_get_duration(g_m.plm), g_m.audio ? "on" : "off", rate);
	pullAudio();	// the clock starts once the device takes the first samples
	return true;
}

void rm8VideoUpdate(float dt)
{
	if(!g_m.plm || g_m.finished) return;
	if(g_m.audio) g_m.clock = rm8AudioStreamPlayed();
	else g_m.clock += dt;
	pullAudio();
	// present the newest frame whose time has come (frames that are already late are decoded and dropped)
	int guard = 0;
	while(!g_m.videoEnded && guard++ < 16){
		if(!g_m.havePending){
			plm_frame_t *fr = plm_decode_video(g_m.plm);
			if(!fr){ g_m.videoEnded = true; break; }
			g_m.pending = fr; g_m.pendingTime = fr->time; g_m.havePending = true; g_m.st.decoded++;
		}
		if(g_m.pendingTime > g_m.clock) break;
		plm_frame_to_rgba(g_m.pending, g_m.rgba.data(), g_m.w * 4);
		g_m.dirty = true; g_m.havePending = false; g_m.shownTime = g_m.pendingTime; g_m.st.shown++;
		g_m.st.maxLag = std::max(g_m.st.maxLag, g_m.clock - g_m.pendingTime);
	}
	upload();
	// [V] 0x5165d0 ends when BinkNextFrame has no frame left; here: video decoded to the end and its last frame time passed, sound drained
	if(g_m.videoEnded && !g_m.havePending && (!g_m.audio || g_m.audioEnded) && (!g_m.audio || rm8AudioStreamQueued() < 0.02)){
		g_m.finished = true; g_m.st.seconds = g_m.clock;
		rm8Log("video: finished, %d frames decoded, %d shown, %.2f s (audio %.2f s), worst lag %.3f s", g_m.st.decoded, g_m.st.shown, g_m.clock, g_m.st.audioSeconds, g_m.st.maxLag);
	}
}

void rm8VideoRender(Camera *cam)
{
	if(!g_m.tex || !cam || !cam->frameBuffer) return;
	float W = (float)cam->frameBuffer->width, H = (float)cam->frameBuffer->height;
	float va = (float)g_m.w / (float)g_m.h, sa = W / H;
	float qw = sa > va ? H * va : W, qh = sa > va ? H : W / va;	// [A] aspect kept (the game stretches to its 4:3 client area)
	float x0 = (W - qw) * 0.5f, y0 = (H - qh) * 0.5f, x1 = x0 + qw, y1 = y0 + qh;
	float z = im2d::GetNearZ(), rz = 1.0f / cam->nearPlane;
	uint32 zt = GetRenderState(ZTESTENABLE), zw = GetRenderState(ZWRITEENABLE), atf = GetRenderState(ALPHATESTFUNC), cull = GetRenderState(CULLMODE);
	SetRenderState(ZTESTENABLE, 0); SetRenderState(ZWRITEENABLE, 0); SetRenderState(ALPHATESTFUNC, ALPHAALWAYS); SetRenderState(CULLMODE, CULLNONE);
	SetRenderState(VERTEXALPHA, 0); SetRenderState(SRCBLEND, BLENDONE); SetRenderState(DESTBLEND, BLENDZERO);
	SetRenderState(TEXTUREADDRESS, Texture::CLAMP); SetRenderState(TEXTUREFILTER, Texture::LINEAR);
	SetRenderStatePtr(TEXTURERASTER, g_m.tex->raster);
	gl3::Im2DVertex v[4]; uint16 idx[6] = {0, 1, 2, 2, 1, 3};
	float px[4] = {x0, x1, x0, x1}, py[4] = {y0, y0, y1, y1}, pu[4] = {0, 1, 0, 1}, pv[4] = {0, 0, 1, 1};
	for(int q = 0; q < 4; q++){
		v[q].setScreenX(px[q]); v[q].setScreenY(py[q]); v[q].setScreenZ(z);
		v[q].setCameraZ(1.0f / rz); v[q].setRecipCameraZ(rz);
		v[q].setColor(255, 255, 255, 255); v[q].setU(pu[q], rz); v[q].setV(pv[q], rz);
	}
	im2d::RenderIndexedPrimitive(PRIMTYPETRILIST, v, 4, idx, 6);
	SetRenderState(VERTEXALPHA, 1); SetRenderState(SRCBLEND, BLENDSRCALPHA); SetRenderState(DESTBLEND, BLENDINVSRCALPHA);
	SetRenderState(ZTESTENABLE, zt); SetRenderState(ZWRITEENABLE, zw); SetRenderState(ALPHATESTFUNC, atf); SetRenderState(CULLMODE, cull);
}

bool rm8VideoPlaying(){ return g_m.plm && !g_m.finished; }
void rm8VideoClose(){ if(g_m.plm || g_m.tex) closeAll(); }
double rm8VideoTime(){ return g_m.shownTime; }
Rm8VideoStats rm8VideoStats(){ return g_m.st; }
