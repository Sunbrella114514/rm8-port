// Movie playback (video.cpp): the game's Bink movies converted offline to MPEG-1 video + MP2 audio (scripts/convert_videos.py) and decoded
// with pl_mpeg (extern/pl_mpeg, MIT). Video goes to a texture drawn full screen keeping the 4:3 aspect, audio through the port's mixer
// (rm8AudioStream*); the audio clock is the master clock, so picture and sound stay in sync. docs/media_collect.md.
#pragma once
#include "scene.h"

// opens the movie; false if the file is missing / not decodable. Starts the soundtrack (silent without an audio device).
bool rm8VideoOpen(const std::string &path);
void rm8VideoUpdate(float dt);			// decode up to the movie clock and upload the newest frame; call once per frame
void rm8VideoRender(rw::Camera *cam);		// letterboxed picture, between beginUpdate / endUpdate (black bars come from the clear)
bool rm8VideoPlaying();				// open and not finished
void rm8VideoClose();				// stops the sound and frees everything
double rm8VideoTime();				// seconds shown so far
// statistics of the last movie (test log): frames decoded / shown, seconds
struct Rm8VideoStats { int decoded = 0, shown = 0; double seconds = 0, audioSeconds = 0, maxLag = 0; };
Rm8VideoStats rm8VideoStats();
