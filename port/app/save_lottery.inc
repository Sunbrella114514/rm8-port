// save / load hooks of event_lottery.cpp (included at the end of event_lottery.cpp): [V] FUN_004c8090 = the prize pool (+0x50), then 4 lists (one per seat) of the
// numbers that seat holds (count + numbers); the free numbers are 1..32 without the held ones
#include "save.h"

void rm8SaveLotteryWrite(Rm8SaveW &w)
{
	ev::ensure();
	w.i32((int)ev::g_pool);
	for(int g = 0; g < 4; g++){ w.i32((int)ev::g_held[g].size()); for(int n : ev::g_held[g]) w.i32(n); }
}
bool rm8SaveLotteryRead(Rm8SaveR &r)
{
	ev::ensure();
	ev::g_pool = r.i32();
	for(auto &h : ev::g_held) h.clear();
	for(int g = 0; g < 4; g++){
		int n = r.i32();
		if(r.bad || n < 0 || n > 64) return false;
		for(int i = 0; i < n; i++) ev::g_held[g].push_back(r.i32());
	}
	ev::g_avail.clear();
	for(int i = 1; i <= 32; i++) if(ev::ownerOf(i) < 0) ev::g_avail.push_back(i);
	return !r.bad;
}
