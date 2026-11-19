// save / load hooks of event_bank.cpp (included at the end of event_bank.cpp): the loan record of the role (+0x298, 7 ints [V] @0x47ec90 / samples record_03 / 08):
// principal, year, month, day, weekday (1..7, 7 = Sunday), day count of that day, (interest payments << 16) | 3 months. Role record field owned by the events.
#include "save.h"

void rm8SaveEventsRoleGet(int seat, Rm8SaveRole &r)
{
	if(seat < 0) return;
	const ev::Loan &L = ev::loanOf(seat);
	for(int &v : r.loan) v = 0;
	if(L.principal <= 0) return;
	r.loan[0] = (int)L.principal; r.loan[1] = L.y; r.loan[2] = L.m; r.loan[3] = L.d;
	int32_t z = rm8SaveCivilDays(L.y, L.m, L.d); int w0 = (int)(((z + 4) % 7 + 7) % 7);	// [N] int32_t
	r.loan[4] = w0 == 0 ? 7 : w0;
	int y, m, d, wd; rm8GameDate(y, m, d, wd);
	r.loan[5] = rm8GameDay() - (int)(rm8SaveCivilDays(y, m, d) - z);
	r.loan[6] = (L.count << 16) | 3;
}
void rm8SaveEventsRoleSet(int seat, const Rm8SaveRole &r)
{
	if(seat < 0) return;
	ev::Loan &L = ev::loanOf(seat);
	L = ev::Loan();
	if(r.loan[0] <= 0) return;
	L.principal = r.loan[0]; L.y = r.loan[1]; L.m = r.loan[2]; L.d = r.loan[3]; L.count = (r.loan[6] >> 16) & 0xffff;
}
