// Audio (audio.cpp): the game's CAudioMgr (Miles) with Sound/snd0000.dat (WAV by id) and Music/mus0000.dat (MP3 by id).
// See docs/audio.md. All functions are safe to call from the game thread; without a device they do nothing.
#pragma once
#include <string>

bool rm8AudioInit(const std::string &gameDir);	// opens the packs and the audio device; false = silent
void rm8AudioSetLevels(int music, int sound);	// [V] Music_SetVolumeLevel @0x4061e0 / Sound_SetVolumeLevel @0x406230 (level 0..5 = master 0..1); menu.cpp applyOptions calls it for the option sliders
void rm8AudioShutdown();
// [V] CAudioMgr_PlaySample @0x4703a0 (id, x,y,z, maxDist, minDist, volume, loops): every game call uses position 0,0,0 and the
// listener never moves, so the port plays it as 2D. Returns a handle (0 = failed).
// loops: 1 = once, 0 = forever (Miles loop count; the game also passes 0xFFFFFFFF = forever [H]), n = n times.
// The final volume is volume * the sound master level (init.txt sound / 5) as in the game.
int rm8SndPlay(int id, int loops = 1, float volume = 1.0f);
void rm8SndStop(int handle);		// [V] CAudioMgr_StopSample @0x471730 (a handle of 0 is ignored)
void rm8SndStopAll();			// [V] CAudioMgr_StopSample(0): every playing sample
bool rm8SndPlaying(int handle);
// [N] netplay (docs/netplay.md section 2.6): while on, rm8SndPlay always gives a handle that "plays" for the sample's PCM length (WAV header, x loops), counted down by
// rm8AudioDetTick(the fixed simulation step) - the game's waits on voices no longer depend on the device; the sound itself plays as before. Off = unchanged.
void rm8AudioSetDeterministic(bool on);
void rm8AudioDetTick(float dt);
void rm8AudioDetSync();	// [N] netplay: every deterministic sample counts as ended (a day-end boundary, the resync point: a loaded snapshot starts without them too); the sounds go on playing
// [N] N4: a sample of a minigame that only the local player runs (mg_gfx.cpp): with the deterministic clock on it plays from its own handle range and is not
// counted by that clock (the shared simulation never sees it); otherwise exactly rm8SndPlay
int rm8SndPlayLocal(int id, int loops = 1, float volume = 1.0f);

// [V] CDigitalAudio_PlayBGMFaded @0x473a80 as used by Movie.PlayMusic (@0x513430): one track, Miles loop count 0 = forever
void rm8MusicPlay(int id, bool loop = true);
void rm8MusicStop();			// [V] CDigitalAudio_StopAllBGM @0x474ca0
// [V] Game_LoadMapData @0x4a31f0: the board playlist of a map ("01".."11", "download201".."download204"): track <map id> (15 for map 3
// in the story mode, none for maps >= 12) and track clime + 11; the two alternate, each played once (CDigitalAudio_PlayNextBGM).
void rm8MusicPlayBoard(const std::string &mapName, bool storyMode = false);
// [V] MiniGameHost_Cleanup @0x50e300: CDigitalAudio_StopBGMWithFade + ManageBGMTransition(-5) = back to the board music after a minigame.
// [A] the board playlist of the last rm8MusicPlayBoard starts again (the original resumes the paused track)
void rm8MusicResumeBoard();
void rm8AudioPause(bool paused);	// app in background / foreground
// [A] one generic push stream (the movie soundtrack, app/video.cpp; the game's Bink plays through its own DirectSound at full volume):
// interleaved float samples at `rate`, converted to the device rate while mixing. Played() is the movie clock.
bool rm8AudioStreamOpen(int rate, int channels);
void rm8AudioStreamPush(const float *interleaved, int frames);
double rm8AudioStreamPlayed();		// seconds of the stream the device has taken
double rm8AudioStreamQueued();		// seconds pushed and not yet played
void rm8AudioStreamClose();
