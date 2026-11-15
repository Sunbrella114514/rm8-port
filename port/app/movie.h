// Cutscene engine of the story mode (the original's Movie / CTime Python modules and the CUi_SpeakAside script object), see docs/story_mode.md section 2.
// [V] = verified in the decompile (address), [M] meaning inferred, [H] hypothesis, [A] port adaptation.
//
// The scripts are the original Pysrc\daXX_N.pyc (generators called `rolemove`); python.cpp runs them with the embedded CPython 2.4 and forwards every
// Movie.* call to the rm8Mv* functions below. This file owns the scene (articles, paper dolls, camera, subtitles, bubbles, sound) and the timing.
// Handles are integers (the original passes object addresses).
#pragma once
#include "scene.h"
#include <functional>
#include <string>
#include <vector>

// scenes/movie_arts.txt (scripts/export_story.py) + the role definitions of scenes/roles.txt (paper dolls); once at start
bool rm8MovieInit(const std::string &scenesRoot);
bool rm8MovieAvailable();
// Plays the scripts one after the other ([V] CApp_PlayMovieChapter @0x4050a0 / PlayMovieRange @0x405400: the names in movie.bin order) on the (empty)
// scene `sc` seen by `cam`; a tap / click / key skips the current script only ([V] CApp_OnMouseButton @0x4059f0 -> Cls_5dff48_vf13 @0x515b50).
// onDone runs after the last script (or at once when nothing could be played). `first` = the original's initial wait before the first step is applied.
bool rm8MovieStart(Rm8Scene &sc, rw::Camera *cam, const std::vector<std::string> &scripts, std::function<void()> onDone);
bool rm8MovieActive();
void rm8MovieUpdate(float dt);
void rm8MovieRender(rw::Camera *cam);		// between beginUpdate / endUpdate; draws the 3D parts and the UI (subtitles, bubbles)
void rm8MovieSkip();				// the click: ends the running script (ignored within 0.5 s of the previous one)
void rm8MovieSkipAll();				// Esc / Android back: ends the running script and the queued ones
void rm8MovieAbort();				// ends everything without calling onDone
float rm8MovieElapsed();			// seconds since the first script started (test hooks)
const char *rm8MovieCurrent();			// name of the running script ("" none)

// test hook (rm8_app.cpp, after the frame was drawn): RM8_MOVIE_SHOTS="<t>,<t>,..." writes movie_<script>_<t>.png (RM8_MENU_SHOTDIR or .) when the movie clock passes t;
// RM8_MOVIE_SKIP="<t>,..." skips the running script at those movie seconds (like a click); RM8_MOVIE_TRACE=1 logs every Movie call
void rm8MovieTestHooks(int w, int h);

// ---- the Python API (python.cpp wrappers; handles > 0, 0 = failed). docs/story_mode.md 2.4 for the addresses
int rm8MvRoleCreate(int roleIndex, float x, float y, float z, int type);	// py_Movie_RoleCreate @0x5125d0 (roleIndex = Id - 1)
void rm8MvRoleDel(int h);							// @0x512820
void rm8MvRoleMove(int h, float x, float y, float z);				// @0x512990
void rm8MvRoleRotate(int h, int dir);						// @0x512a20
void rm8MvRoleFace(int h, int dir);						// @0x512a90
void rm8MvRoleSetAnim(int h, int anim, int count);				// @0x512b00
void rm8MvRoleSpeak(int h, int strId);						// @0x512b70
void rm8MvRoleStopSpeak(int h);							// @0x512be0
void rm8MvRoleSetPos(int h, float x, float y, float z);				// @0x512c40
void rm8MvRoleSetSpeed(int h, float s);						// @0x512cd0
void rm8MvRoleSetScale(int h, float sx, float sy, float sz);			// @0x512d30
int rm8MvArtCreate(int art, float x, float y, float z);				// py_Movie_ArtCreate @0x512db0
void rm8MvArtSetAnim(int h, int anim, int count);				// @0x512ea0
void rm8MvArtDel(int h);							// @0x512f10
void rm8MvArtMove(int h, float x, float y, float z);				// @0x513090
void rm8MvArtRotate(int h, float angle);					// @0x513120
void rm8MvArtSpeak(int h, int strId);						// @0x513190
void rm8MvArtStopSpeak(int h);							// @0x5131f0
void rm8MvArtSetPos(int h, float x, float y, float z);				// @0x513250
void rm8MvArtSetSpeed(int h, float s);						// @0x5132e0
void rm8MvArtSetScale(int h, float sx, float sy, float sz);			// @0x513340
void rm8MvPlaySound(int id, int loops);						// @0x5133c0
void rm8MvPlayMusic(int id);							// @0x513430
void rm8MvSetPic(bool front, const std::string &name);				// SetBackPic @0x513490 / SetFrontPic @0x513500 (no-op, docs 2.3)
void rm8MvDelPic(bool front);							// @0x513570 / @0x5135a0
void rm8MvSpeakAside(int strId);						// @0x5135d0
void rm8MvStopSpeakAside();							// @0x513620
void rm8MvSetCamera(float x, float y, float z, float pitch, float yaw);		// @0x513650
void rm8MvQuit();								// @0x5136d0
void rm8MvWait(float seconds);							// CTime.Wait @0x5160b0: the timer is set as given (not scaled)

// python.cpp: the generator of a script (module.rolemove())
bool rm8PyMovieOpen(const char *module);
int rm8PyMovieStep();				// next(): 1 = yielded, 0 = finished (StopIteration), -1 = error
void rm8PyMovieClose();
