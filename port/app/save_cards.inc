// save / load hooks of cards.cpp (included at the end of cards.cpp). See save.h, docs/save_port.md.
// Role record fields owned here: the 8 card slots (+0x2B4: the card Id, -1 empty), the turtle days (+0x1CC), the stay flag (+0x1D0), the status days
// (+0x1D8: sleep / hospital / prison) and the bubble counter (+0x28C). New card state (missiles, bodyguard ...) belongs in an extension chunk of this module.
#include "save.h"

void rm8SaveCardsRoleGet(int seat, Rm8SaveRole &r)
{
	if(seat < 0) return;
	Deck &d = deck(seat);
	for(int k = 0; k < 8; k++) r.cards[k] = d.id[k] > 0 ? d.id[k] : -1;
	Extra &x = X(seat);
	r.turtle = x.turtle; r.stay = x.stay; r.statusDays = x.days; r.bubble = x.bubble; r.sarah = x.guard;	// the bodyguard counter of 莎拉 (role 16: *(+0x5F0)+0x98)
}
void rm8SaveCardsRoleSet(int seat, const Rm8SaveRole &r)
{
	if(seat < 0) return;
	Deck &d = deck(seat);
	for(int k = 0; k < 8; k++){ int c = r.cards[k]; d.id[k] = c > 0 && (g_defs.empty() || def(c)) ? c : 0; d.ready[k] = false; }
	d.tempId = 0; d.tempSlot = -1;
	Extra &x = X(seat);
	x.turtle = std::max(0, r.turtle); x.stay = r.stay; x.days = std::max(0, r.statusDays); x.bubble = std::max(0, r.bubble); x.guard = rm8TurnCharId(seat) == 16 ? std::max(0, r.sarah) : 0;
}
