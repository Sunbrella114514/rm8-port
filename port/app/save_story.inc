// save / load hooks of story.cpp (included at the end of story.cpp). See save.h, docs/save_port.md, docs/story_mode.md 1.15.
// Role record fields owned here (the hero's story flags): +0x374 block (9 ints: [0] debt handled / meetings (+0x374), [1] park visits (+0x378), [2] park chest (+0x37C),
// [3] the 金泡菜 rescue counter (+0x380), [4] 莎拉's hospital / prison visits (+0x384)), the used-card flags (+0x4C0 + card Id), the hospital flag (+0x5C0) and
// the roll-of-18 flag (+0x5CC). The storage box (sflb section) is written by save.cpp from rm8Box().
#include "save.h"

void rm8SaveStoryRoleGet(int seat, Rm8SaveRole &r)
{
	if(seat < 0) return;
	const SeatStory &x = ss(seat);
	r.f374[0] = x.debt ? 1 : x.meet; r.f374[1] = x.park; r.f374[2] = x.chest ? 1 : 0; r.f374[3] = seat == 0 && g_rescued ? 1 : 0; r.f374[4] = x.firstHosp;
	for(int i = 0; i < 64; i++) r.used[i] = x.usedCard[i] ? 1 : 0;
	r.f5c0[0] = x.hospital ? 1 : 0; r.f5cc = x.steps18 ? 1 : 0;
}
void rm8SaveStoryRoleSet(int seat, const Rm8SaveRole &r)
{
	if(seat < 0) return;
	SeatStory &x = ss(seat);
	x.debt = r.f374[0] != 0; x.meet = r.f374[0]; x.park = r.f374[1]; x.chest = r.f374[2] != 0; x.firstHosp = r.f374[4];
	if(seat == 0) g_rescued = r.f374[3] != 0;
	for(int i = 0; i < 64; i++) x.usedCard[i] = r.used[i] != 0;
	x.hospital = r.f5c0[0] != 0; x.steps18 = r.f5cc != 0;
}

// the board of a loaded record: the story setup was confirmed in the original's menus before the record was written (the pass entry is found again from the hero and
// the stage), so the story rules are on without the menu run
void rm8SaveStoryLoadSetup(bool story, int level){ g_confirmed = story; if(story) g_level = level; }
