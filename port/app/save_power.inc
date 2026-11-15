// save / load hooks of event_power.cpp (included at the end of event_power.cpp): [V] FUN_004dfec0 / FUN_004da700 = 4 floats (the share of each seat) + the chairman's
// seat (-1 none); the ticket center is DAT_006403c8, the power company DAT_006403cc. The consumption level of the ticket center goes into the extension chunk.
#include "save.h"

void rm8SaveCompanyWrite(Rm8SaveW &w, bool power)
{
	ev::init();
	const ev::Company &c = power ? ev::g_pw : ev::g_tc;
	for(int i = 0; i < 4; i++) w.f32(c.share[i]);
	w.i32(c.chairman);
}
bool rm8SaveCompanyRead(Rm8SaveR &r, bool power)
{
	ev::init();
	ev::Company &c = power ? ev::g_pw : ev::g_tc;
	for(int i = 0; i < 8; i++) c.share[i] = 0;
	for(int i = 0; i < 4; i++) c.share[i] = r.f32();
	c.chairman = r.i32();
	if(c.chairman < -1 || c.chairman >= rm8TurnRoleCount()) c.chairman = -1;
	return !r.bad;
}
static void saveCompanyExtWrite(Rm8SaveW &w){ ev::init(); w.i32(ev::g_pw.level); w.i32(ev::g_tc.level); }
static void saveCompanyExtRead(Rm8SaveR &r){ int a = 1, b = 1; r.opt(a); r.opt(b); if(a >= 1 && a <= 5) a *= 10; if(b >= 1 && b <= 5) b *= 10; ev::g_pw.level = a; ev::g_tc.level = b; }	// port saves before the fix stored 1..5
RM8_SAVE_EXT(CMPY, 40, saveCompanyExtWrite, saveCompanyExtRead);
