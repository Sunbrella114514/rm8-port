// Audio: a small SDL2 software mixer in place of the game's Miles Sound System (docs/audio.md).
//   Sound/snd0000.dat  'snd%' pack of WAV files (all PCM: 44.1 kHz / 22.05 kHz mono 16 bit, one 22 kHz 8 bit), entries read on demand,
//                      decoded once and cached as 44.1 kHz stereo 16 bit.
//   Music/mus0000.dat  'mus%' pack of MP3 files (MPEG-1 layer 3, 128 kbps, 44.1 kHz stereo), decoded while mixing with dr_mp3.
// [A] Miles' 3D positioning is dropped: [V] every CAudioMgr_PlaySample call of the game uses position (0,0,0) and the
//     listener (Audio_Set3DPosition @0x470060 has no caller) never moves, so the result is plain 2D playback.
// Without SDL.h (the GLFW viewer build) everything below is a no-op.
#include "audio.h"

#if __has_include(<SDL.h>)
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <algorithm>

void rm8Log(const char *fmt, ...);

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../extern/dr_libs/dr_mp3.h"

namespace {

const int RATE = 44100;		// device rate; samples are converted once on load
const int MAX_VOICES = 32;
const size_t CACHE_BYTES = 48u << 20;

// ---- pack files: char[4] tag, u32, u32 count, count x {u32 id, u32 offset, u32 size, u32 flags} (docs/README.md format B)
struct Pack {
	FILE *f = nullptr;
	struct Ent { uint32_t off, size; };
	std::unordered_map<int, Ent> idx;
	bool open(const std::string &path, const char *tag)
	{
		f = fopen(path.c_str(), "rb");
		if(!f) return false;
		unsigned char h[12];
		if(fread(h, 1, 12, f) != 12 || memcmp(h, tag, 4)){ close(); return false; }
		uint32_t n; memcpy(&n, h + 8, 4);
		std::vector<uint32_t> e((size_t)n * 4);
		if(n > 100000 || fread(e.data(), 4, e.size(), f) != e.size()){ close(); return false; }
		for(uint32_t i = 0; i < n; i++) idx[(int)e[i * 4]] = {e[i * 4 + 1], e[i * 4 + 2]};	// [V] the game ignores the 4th field when it is 0 (all entries here)
		return true;
	}
	void close(){ if(f) fclose(f); f = nullptr; idx.clear(); }
	bool read(int id, std::vector<uint8_t> &out)
	{
		auto it = idx.find(id);
		if(!f || it == idx.end()) return false;
		out.resize(it->second.size);
		if(fseek(f, (long)it->second.off, SEEK_SET) != 0 || fread(out.data(), 1, out.size(), f) != out.size()){ out.clear(); return false; }
		return true;
	}
};

struct Sample { std::vector<int16_t> pcm; };	// stereo interleaved, RATE

struct Voice { std::shared_ptr<Sample> s; size_t pos = 0; int loops = 1; bool forever = false; int vol = 65536; int handle = 0; bool active = false; };

// ---- state: the game thread touches it under SDL_LockAudioDevice, the callback owns it while running
SDL_AudioDeviceID g_dev = 0;
bool g_trace = false;
Pack g_snd, g_mus;
Voice g_voice[MAX_VOICES];
int g_nextHandle = 1;
bool g_levelsSet = false;
float g_sndMaster = 0.8f, g_musMaster = 0.2f;	// [V] level / 5.0 (Sound_SetVolumeLevel @0x406230, CDigitalAudio_SetMasterVolume @0x476250)

struct CacheEnt { std::shared_ptr<Sample> s; uint64_t tick; };
std::unordered_map<int, CacheEnt> g_cache;
size_t g_cacheBytes = 0; uint64_t g_tick = 0;	// game thread only

struct Track { int id; std::shared_ptr<std::vector<uint8_t>> data; };
std::vector<Track> g_pl;
int g_cur = -1;
bool g_plLoop = true, g_decOpen = false;
drmp3 g_dec;

std::shared_ptr<Sample> decodeWav(const std::vector<uint8_t> &d)
{
	SDL_AudioSpec spec; Uint8 *buf = nullptr; Uint32 len = 0;
	if(!SDL_LoadWAV_RW(SDL_RWFromConstMem(d.data(), (int)d.size()), 1, &spec, &buf, &len)) return nullptr;
	auto s = std::make_shared<Sample>();
	SDL_AudioCVT cvt;
	int r = SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, RATE);
	if(r < 0){ SDL_FreeWAV(buf); return nullptr; }
	if(r == 0){ s->pcm.assign((int16_t*)buf, (int16_t*)buf + len / 2); }
	else {
		std::vector<uint8_t> tmp((size_t)len * cvt.len_mult);
		memcpy(tmp.data(), buf, len);
		cvt.len = (int)len; cvt.buf = tmp.data();
		if(SDL_ConvertAudio(&cvt) != 0){ SDL_FreeWAV(buf); return nullptr; }
		s->pcm.assign((int16_t*)tmp.data(), (int16_t*)tmp.data() + cvt.len_cvt / 2);
	}
	SDL_FreeWAV(buf);
	return s;
}

std::shared_ptr<Sample> getSample(int id)
{
	auto it = g_cache.find(id);
	if(it != g_cache.end()){ it->second.tick = ++g_tick; return it->second.s; }
	std::vector<uint8_t> raw;
	if(!g_snd.read(id, raw)) return nullptr;
	std::shared_ptr<Sample> s = decodeWav(raw);
	if(!s || s->pcm.empty()){ rm8Log("audio: cannot decode sample %d (%s)", id, SDL_GetError()); return nullptr; }
	g_cacheBytes += s->pcm.size() * 2;
	while(g_cacheBytes > CACHE_BYTES){	// evict the least recently used entry that no voice is playing
		auto oldest = g_cache.end();
		for(auto i = g_cache.begin(); i != g_cache.end(); ++i)
			if(i->second.s.use_count() == 1 && (oldest == g_cache.end() || i->second.tick < oldest->second.tick)) oldest = i;
		if(oldest == g_cache.end()) break;
		g_cacheBytes -= oldest->second.s->pcm.size() * 2; g_cache.erase(oldest);
	}
	g_cache[id] = {s, ++g_tick};
	return s;
}

// ---- music (audio thread, or the game thread under the device lock)
void closeDecoder(){ if(g_decOpen){ drmp3_uninit(&g_dec); g_decOpen = false; } }
void startTrack(int i)
{
	closeDecoder();
	g_cur = i;
	if(i < 0 || i >= (int)g_pl.size()) return;
	const std::vector<uint8_t> &d = *g_pl[i].data;
	g_decOpen = drmp3_init_memory(&g_dec, d.data(), d.size(), nullptr) != 0;
	if(g_decOpen && (g_dec.channels < 1 || g_dec.channels > 2 || g_dec.sampleRate != RATE)){ closeDecoder(); }	// [A] no resampling: all game tracks are 44.1 kHz
}
// [V] CDigitalAudio_PlayNextBGM @0x4742f0: the order list is 0..n-1; the next track is the first one that is not the finished one
// (n == 1: the same one again)
int nextTrack(int finished)
{
	int n = (int)g_pl.size(), c = 0;
	while(n > 1 && c == finished){ c++; if(c >= n) c = 0; }
	return c;
}
void mixMusic(int32_t *acc, int frames)
{
	if(!g_decOpen) return;
	const float gain = g_musMaster * 32767.0f;	// [V] track volume 1.0 * master (CDigitalAudio_PlayBGM @0x473710)
	int done = 0, retries = 0;
	float buf[512 * 2];
	while(done < frames && g_decOpen){
		int want = std::min(frames - done, 512), ch = (int)g_dec.channels;
		int got = (int)drmp3_read_pcm_frames_f32(&g_dec, want, buf);
		for(int i = 0; i < got; i++){
			acc[(done + i) * 2] += (int32_t)(buf[i * ch] * gain);
			acc[(done + i) * 2 + 1] += (int32_t)(buf[i * ch + (ch > 1 ? 1 : 0)] * gain);
		}
		done += got;
		if(got < want){	// end of the track (CDigitalAudio_UpdateMusic @0x473f40 status 2)
			if(!g_plLoop){ closeDecoder(); g_cur = -1; break; }
			startTrack(nextTrack(g_cur));
			if(got == 0 && ++retries > 2) break;
		}
	}
}

// ---- push stream (movie soundtrack): linear resampling to RATE; the device lock protects it
struct Stream { bool on = false; int rate = RATE, ch = 2; std::vector<float> buf; size_t head = 0; double frac = 0, consumed = 0; } g_stream;

void mixStream(int32_t *acc, int frames)
{
	Stream &s = g_stream;
	if(!s.on) return;
	const double step = (double)s.rate / RATE;
	const float gain = 32767.0f * 0.9f;
	const size_t ch = (size_t)s.ch;
	for(int i = 0; i < frames; i++){
		if(s.head + 2 * ch > s.buf.size()) break;	// underrun: silence, the clock waits
		const float *a = &s.buf[s.head], *b = a + ch;
		float f = (float)s.frac;
		float l = a[0] + (b[0] - a[0]) * f, r = ch > 1 ? a[1] + (b[1] - a[1]) * f : l;
		acc[i * 2] += (int32_t)(l * gain); acc[i * 2 + 1] += (int32_t)(r * gain);
		s.frac += step;
		while(s.frac >= 1.0){ s.frac -= 1.0; s.head += ch; s.consumed += 1.0; }
	}
}

void SDLCALL callback(void *, Uint8 *stream, int len)
{
	int frames = len / 4;
	static std::vector<int32_t> acc;
	acc.assign((size_t)frames * 2, 0);
	for(Voice &v : g_voice){
		if(!v.active) continue;
		const int16_t *p = v.s->pcm.data();
		size_t total = v.s->pcm.size() / 2;
		for(int i = 0; i < frames; i++){
			if(v.pos >= total){
				if(v.forever || --v.loops > 0) v.pos = 0;
				else { v.active = false; v.s.reset(); break; }
			}
			acc[i * 2] += (p[v.pos * 2] * v.vol) >> 16;
			acc[i * 2 + 1] += (p[v.pos * 2 + 1] * v.vol) >> 16;
			v.pos++;
		}
	}
	mixMusic(acc.data(), frames);
	mixStream(acc.data(), frames);
	int16_t *o = (int16_t*)stream;
	for(int i = 0; i < frames * 2; i++) o[i] = (int16_t)std::max(-32768, std::min(32767, acc[i]));
}

int readLevel(const std::string &ini, const char *key, int def)
{
	FILE *f = fopen(ini.c_str(), "rb");
	if(!f) return def;
	char line[256]; int v = def; size_t kl = strlen(key);
	while(fgets(line, sizeof line, f)) if(!strncmp(line, key, kl) && line[kl] == '=') v = atoi(line + kl + 1);
	fclose(f);
	return v;
}

}	// namespace

bool rm8AudioInit(const std::string &gameDir)
{
	if(g_dev) return true;
	g_trace = getenv("RM8_AUDIO_TRACE") != nullptr;
	// [V] InitConfig_LoadInitTxt @0x401080: defaults sound 4 / music 3, init.txt [param] overrides; the master volume is level / 5 and
	// only 0..1 is accepted (@0x406230)
	// [A] when the option module has already set the levels (rm8AudioSetLevels, menu.cpp: rm8_settings.txt over init.txt) they stay; this read is the fallback of the test tools
	std::string ini = gameDir + "/Config/init.txt";
	int sl = readLevel(ini, "sound", 4), ml = readLevel(ini, "music", 3);
	if(!g_levelsSet){
		if(sl >= 0 && sl <= 5) g_sndMaster = sl / 5.0f;
		if(ml >= 0 && ml <= 5) g_musMaster = ml / 5.0f;
	}
	bool s = g_snd.open(gameDir + "/Sound/snd0000.dat", "snd%"), m = g_mus.open(gameDir + "/Music/mus0000.dat", "mus%");
	if(!s && !m){ rm8Log("audio: no Sound/snd0000.dat or Music/mus0000.dat under %s", gameDir.c_str()); return false; }
	if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0){ rm8Log("audio: SDL audio init failed: %s", SDL_GetError()); return false; }
	SDL_AudioSpec want, got; SDL_zero(want);
	want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.callback = callback;
#ifdef __ANDROID__
	want.samples = 2048;
#else
	want.samples = 1024;
#endif
	g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);	// no allowed changes: SDL converts to the hardware format itself
	if(!g_dev){ rm8Log("audio: cannot open the audio device: %s", SDL_GetError()); SDL_QuitSubSystem(SDL_INIT_AUDIO); return false; }
	SDL_PauseAudioDevice(g_dev, 0);
	rm8Log("audio: %s, %d Hz x%d, %d samples/buffer; sound level %d (%.1f), music level %d (%.1f); %d sounds, %d tracks",
	       SDL_GetCurrentAudioDriver(), got.freq, got.channels, got.samples, sl, g_sndMaster, ml, g_musMaster, (int)g_snd.idx.size(), (int)g_mus.idx.size());
	return true;
}

// [V] Config_ApplySetting @0x50a710 -> Music_SetVolumeLevel @0x4061e0 (CDigitalAudio_SetMasterVolume(level / 5.0)) / Sound_SetVolumeLevel @0x406230
// (the sound master = level / 5.0, accepted when 0..1); a level outside 0..5 is not applied. The music master is read by the mixer every block, so a playing track follows at once;
// the sound master is applied when a sample starts (@0x4703a0) [H: the original may also touch playing samples].
void rm8AudioSetLevels(int music, int sound)
{
	if(g_dev) SDL_LockAudioDevice(g_dev);
	if(music >= 0 && music <= 5){ g_musMaster = music / 5.0f; g_levelsSet = true; }
	if(sound >= 0 && sound <= 5){ g_sndMaster = sound / 5.0f; g_levelsSet = true; }
	if(g_dev) SDL_UnlockAudioDevice(g_dev);
	if(g_trace) rm8Log("audio: levels music %d (%.1f) sound %d (%.1f)", music, g_musMaster, sound, g_sndMaster);
}

void rm8AudioShutdown()
{
	if(!g_dev) return;
	SDL_CloseAudioDevice(g_dev); g_dev = 0;	// the callback is gone after this returns
	closeDecoder(); g_pl.clear(); g_cur = -1;
	for(Voice &v : g_voice){ v.active = false; v.s.reset(); }
	g_cache.clear(); g_cacheBytes = 0;
	g_snd.close(); g_mus.close();
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

namespace {
// [N] netplay (docs/netplay.md section 2.6): "is the sample still playing" gates game waits (the event dialogs' voices, the role waits, the balance screen). The real
// playback depends on the device (buffering, a missing device, the app in the background), so a netplay game asks a deterministic clock instead: every sample
// lasts its PCM length from the WAV header (frames / rate x loops; forever = until stopped), counted down by rm8AudioDetTick with the fixed simulation step.
// The sound itself still plays on the device as before. Off (single player): nothing changes.
bool g_det = false;
struct DetVoice { float left = 0; bool forever = false; };
std::unordered_map<int, DetVoice> g_detV;
std::unordered_map<int, float> g_detLen;	// sample id -> seconds (-1 = missing)
float detLength(int id)
{
	auto it = g_detLen.find(id);
	if(it != g_detLen.end()) return it->second;
	float len = -1;
	std::vector<uint8_t> d;
	if(g_snd.read(id, d) && d.size() >= 12 && !memcmp(d.data(), "RIFF", 4) && !memcmp(d.data() + 8, "WAVE", 4)){
		uint32_t rate = 0; uint16_t align = 0; uint32_t data = 0; bool haveFmt = false, haveData = false;
		for(size_t p = 12; p + 8 <= d.size();){
			uint32_t sz; memcpy(&sz, d.data() + p + 4, 4);
			if(!memcmp(d.data() + p, "fmt ", 4) && p + 8 + 16 <= d.size()){ memcpy(&rate, d.data() + p + 12, 4); memcpy(&align, d.data() + p + 20, 2); haveFmt = true; }
			else if(!memcmp(d.data() + p, "data", 4)){ data = (uint32_t)std::min<size_t>(sz, d.size() - (p + 8)); haveData = true; }
			p += 8 + (size_t)sz + (sz & 1);
		}
		if(haveFmt && haveData && rate > 0 && align > 0) len = (float)(data / align) / (float)rate;
	}
	g_detLen[id] = len;
	return len;
}

int playReal(int id, int loops, float volume, int handle)
{
	if(!g_dev) return 0;
	std::shared_ptr<Sample> s = getSample(id);	// decoded outside the device lock
	if(!s) return 0;
	int got = 0;
	SDL_LockAudioDevice(g_dev);
	for(Voice &v : g_voice){
		if(v.active) continue;
		v.s = s; v.pos = 0; v.forever = loops <= 0; v.loops = loops;	// [V] Miles: 0 = forever, 1 = once (a negative count is taken as forever [H])
		v.vol = (int)(std::max(0.0f, std::min(1.0f, volume * g_sndMaster)) * 65536.0f);	// [V] volume * master (@0x4703a0)
		if(handle) got = v.handle = handle;
		else { got = v.handle = g_nextHandle++; if(g_nextHandle <= 0) g_nextHandle = 1; }
		v.active = true;
		break;
	}
	SDL_UnlockAudioDevice(g_dev);
	return got;
}
}	// namespace

int rm8SndPlay(int id, int loops, float volume)
{
	if(g_det){	// [N] the handle exists (and "plays" for the sample's length) whether or not the device could play it
		float len = detLength(id);
		if(len < 0) return 0;
		int handle = g_nextHandle++; if(g_nextHandle <= 0) g_nextHandle = 1;
		DetVoice v; v.forever = loops <= 0; v.left = len * (float)std::max(1, loops);
		g_detV[handle] = v;
		playReal(id, loops, volume, handle);
		if(g_trace) rm8Log("audio: play %d loops %d vol %.2f -> %d (deterministic %.3f s)", id, loops, volume, handle, v.left);
		return handle;
	}
	int handle = playReal(id, loops, volume, 0);
	if(g_trace) rm8Log("audio: play %d loops %d vol %.2f -> %d", id, loops, volume, handle);
	return handle;
}

int rm8SndPlayLocal(int id, int loops, float volume)
{
	if(!g_det) return rm8SndPlay(id, loops, volume);
	static int next = 0x40000000;	// [N] N4: the minigame handles (far from g_nextHandle, which the simulation shares)
	int handle = next++; if(next <= 0x40000000) next = 0x40000000;
	playReal(id, loops, volume, handle);
	if(g_trace) rm8Log("audio: local play %d loops %d vol %.2f -> %d", id, loops, volume, handle);
	return handle;
}

void rm8AudioSetDeterministic(bool on){ if(g_det != on) rm8Log("audio: deterministic sample clock %s", on ? "on (netplay)" : "off"); g_det = on; g_detV.clear(); }
void rm8AudioDetSync(){ if(g_det && !g_detV.empty()){ if(g_trace) rm8Log("audio: deterministic clock: %d sample(s) counted as ended at the day boundary", (int)g_detV.size()); g_detV.clear(); } }
void rm8AudioDetTick(float dt)
{
	if(!g_det) return;
	for(auto it = g_detV.begin(); it != g_detV.end();){
		if(!it->second.forever){ it->second.left -= dt; if(it->second.left <= 0){ it = g_detV.erase(it); continue; } }
		++it;
	}
}

void rm8SndStop(int handle)
{
	if(g_det && handle > 0) g_detV.erase(handle);
	if(!g_dev || handle <= 0) return;
	SDL_LockAudioDevice(g_dev);
	for(Voice &v : g_voice) if(v.active && v.handle == handle){ v.active = false; v.s.reset(); if(g_trace) rm8Log("audio: stop %d", handle); }
	SDL_UnlockAudioDevice(g_dev);
}

void rm8SndStopAll()
{
	if(g_det) g_detV.clear();
	if(!g_dev) return;
	SDL_LockAudioDevice(g_dev);
	for(Voice &v : g_voice){ v.active = false; v.s.reset(); }
	SDL_UnlockAudioDevice(g_dev);
}

bool rm8SndPlaying(int handle)
{
	if(g_det) return handle > 0 && g_detV.count(handle) != 0;
	if(!g_dev || handle <= 0) return false;
	SDL_LockAudioDevice(g_dev);
	bool r = false;
	for(Voice &v : g_voice) if(v.active && v.handle == handle) r = true;
	SDL_UnlockAudioDevice(g_dev);
	return r;
}

static void startPlaylist(const std::vector<int> &ids, bool loop)
{
	std::vector<Track> pl;
	for(int id : ids){
		std::vector<uint8_t> raw;
		if(!g_mus.read(id, raw)){ rm8Log("audio: music track %d missing", id); continue; }
		pl.push_back({id, std::make_shared<std::vector<uint8_t>>(std::move(raw))});
	}
	SDL_LockAudioDevice(g_dev);
	closeDecoder(); g_pl = std::move(pl); g_plLoop = loop; g_cur = -1;
	if(!g_pl.empty()) startTrack(0);	// [V] CDigitalAudio_PlayBGM(0,0): playlist item 0
	SDL_UnlockAudioDevice(g_dev);
	if(g_trace){ std::string s; for(auto &t : g_pl) s += " " + std::to_string(t.id); rm8Log("audio: music playlist%s loop %d", s.c_str(), (int)loop); }
}

void rm8MusicPlay(int id, bool loop){ if(g_dev) startPlaylist({id}, loop); }

void rm8MusicStop()
{
	if(!g_dev) return;
	SDL_LockAudioDevice(g_dev);
	closeDecoder(); g_pl.clear(); g_cur = -1;
	SDL_UnlockAudioDevice(g_dev);
}

static std::string g_boardMap; static bool g_boardStory = false;
void rm8MusicResumeBoard(){ if(!g_boardMap.empty()) rm8MusicPlayBoard(g_boardMap, g_boardStory); }
void rm8MusicPlayBoard(const std::string &mapName, bool storyMode)
{
	g_boardMap = mapName; g_boardStory = storyMode;
	if(!g_dev) return;
	std::vector<int> ids; int mapId = 0, clime = 1;
	if(mapName.compare(0, 8, "download") == 0 && mapName.size() > 8){
		// [V] Map_LoadDownloadMap @0x4a9020: id 21+, clime = first digit after "download" clamped to 1..3
		clime = std::max(1, std::min(3, mapName[8] - '0')); mapId = 21;
	} else {
		static const int CLIME[12] = {0, 1, 1, 3, 1, 3, 1, 2, 2, 3, 3, 2};	// [V] Config\stage.bin clime of map ids 1..11
		mapId = atoi(mapName.c_str());
		if(mapId >= 1 && mapId <= 11) clime = CLIME[mapId];
	}
	// [V] Game_LoadMapData @0x4a31f0: map id < 12 -> that track (15 instead of 3 in the story mode, flag +0 of &DAT_00632de8),
	// then track clime + 0xB; alternate through CDigitalAudio_PlayNextBGM
	if(mapId >= 1 && mapId < 12) ids.push_back(storyMode && mapId == 3 ? 15 : mapId);
	ids.push_back(clime + 0xB);
	startPlaylist(ids, true);
}

void rm8AudioPause(bool paused)
{
	if(g_dev) SDL_PauseAudioDevice(g_dev, paused ? 1 : 0);	// [V] the game pauses the samples and the music while its window is inactive (CApp__vf4_WndProc @0x403aa0, WM_ACTIVATE: Audio_Stop3DSample(0) / CDigitalAudio_StopMusic(0), resumed on activation)
}

bool rm8AudioStreamOpen(int rate, int channels)
{
	if(!g_dev || rate < 8000 || rate > 192000 || channels < 1 || channels > 2) return false;
	SDL_LockAudioDevice(g_dev);
	g_stream = Stream(); g_stream.on = true; g_stream.rate = rate; g_stream.ch = channels;
	SDL_UnlockAudioDevice(g_dev);
	return true;
}

void rm8AudioStreamPush(const float *p, int frames)
{
	if(!g_dev || !g_stream.on || frames <= 0) return;
	SDL_LockAudioDevice(g_dev);
	Stream &s = g_stream;
	if(s.head > 32768){ s.buf.erase(s.buf.begin(), s.buf.begin() + (long)s.head); s.head = 0; }	// drop what was played
	s.buf.insert(s.buf.end(), p, p + (size_t)frames * s.ch);
	SDL_UnlockAudioDevice(g_dev);
}

double rm8AudioStreamPlayed()
{
	if(!g_dev || !g_stream.on) return 0;
	SDL_LockAudioDevice(g_dev);
	double t = (g_stream.consumed + g_stream.frac) / g_stream.rate;
	SDL_UnlockAudioDevice(g_dev);
	return t;
}

double rm8AudioStreamQueued()
{
	if(!g_dev || !g_stream.on) return 0;
	SDL_LockAudioDevice(g_dev);
	double t = ((double)(g_stream.buf.size() - g_stream.head) / g_stream.ch - g_stream.frac) / g_stream.rate;
	SDL_UnlockAudioDevice(g_dev);
	return std::max(0.0, t);
}

void rm8AudioStreamClose()
{
	if(!g_dev) return;
	SDL_LockAudioDevice(g_dev);
	g_stream = Stream();
	SDL_UnlockAudioDevice(g_dev);
}

#else	// no SDL: silent stubs (offscreen viewer)

bool rm8AudioInit(const std::string &){ return false; }
void rm8AudioSetLevels(int, int){}
void rm8AudioShutdown(){}
int rm8SndPlay(int, int, float){ return 0; }
int rm8SndPlayLocal(int, int, float){ return 0; }
void rm8SndStop(int){}
void rm8SndStopAll(){}
bool rm8SndPlaying(int){ return false; }
void rm8AudioSetDeterministic(bool){}
void rm8AudioDetTick(float){}
void rm8AudioDetSync(){}

void rm8MusicPlay(int, bool){}
void rm8MusicStop(){}
void rm8MusicPlayBoard(const std::string &, bool){}
void rm8MusicResumeBoard(){}
void rm8AudioPause(bool){}
bool rm8AudioStreamOpen(int, int){ return false; }
void rm8AudioStreamPush(const float *, int){}
double rm8AudioStreamPlayed(){ return 0; }
double rm8AudioStreamQueued(){ return 0; }
void rm8AudioStreamClose(){}

#endif
