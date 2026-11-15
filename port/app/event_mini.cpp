// Minigame events 0x12..0x17 on the board (the CMiniGameHost task, docs/minigames/port.md), joined to the event framework.
// [V] Game_UseCard(0x12..0x17) -> CMiniGameHost_ctor @0x50dd20 (event task); vf16 @0x50ddd0 builds the player list (every role that is
//     not bankrupt, the current role first, type 2 human / 1 computer) and runs the DLL; vf1 waits for its end; MiniGameHost_Cleanup
//     @0x50e300 hands out the rewards. A computer landing (AI_OnLandEvent @0x4f79c0) uses MiniGame_ExecuteGame @0x4fcb10 instead, except
//     game 0x15 (pirate) while a human plays: then it falls through to the same task as a human.
#include "event_int.h"
#include "minigame.h"
#include "present.h"
#include "collect.h"
#include "audio.h"
#include <cstdlib>

namespace ev {

namespace {

// [V] the reward order of MiniGameHost_Cleanup @0x50e300 / MiniGame_ExecuteGame @0x4fcb10
void applyReward(int game, const Rm8MiniReward &r)
{
	if(r.seat < 0 || r.seat >= rm8TurnRoleCount()) return;
	rm8Log("events: minigame %d seat %d: tickets %+d, %d card(s), hospital %d", game, r.seat, r.ticketsDelta, (int)r.cards.size(), r.hospitalDays);
	if(r.ticketsDelta > 0) addTickets(r.seat, r.ticketsDelta);
	else if(r.ticketsDelta < 0) subTickets(r.seat, -r.ticketsDelta);
	if(r.textId > 0) queueText(r.seat, plainFmt(r.textId, rm8TurnName(r.seat), r.textArg));
	for(int idx : r.cards) rm8TurnEnqueue(r.seat, 4, idx, 1);	// Role_EnqueueMsg(4, card index): receive with the getcard dialog
	if(r.hospitalDays > 0) rm8TurnEnqueue(r.seat, 0x18, r.hospitalDays, 0);
	if(r.reaction > 0) rm8RoleSay(r.seat, r.reaction);	// [V] FUN_00484890(n) = Role.Speak class n [H]
	if(r.bestScore >= 0) rm8ProfileSetQuest(game - 0x11, r.bestScore);	// [V] Event_SetQuestData(event - 0x11, score) @0x4aa5d0
}

struct MiniTask : Task {
	bool started = false, finished = false;
	float doneT = 0;
	MiniTask(int seat, int game)
	{
		std::vector<Rm8MiniPlayer> pl;
		Rm8MiniPlayer me; me.seat = seat; me.human = rm8TurnHuman(seat); me.charId = rm8TurnCharId(seat); me.outfit = rm8TurnOutfit(seat); me.pic = rm8TurnPic(seat); me.name = rm8TurnName(seat);
		pl.push_back(me);
		for(int k = 0; k < rm8TurnRoleCount(); k++){
			if(k == seat || rm8TurnStatus(k) == 7) continue;
			Rm8MiniPlayer p; p.seat = k; p.human = rm8TurnHuman(k); p.charId = rm8TurnCharId(k); p.outfit = rm8TurnOutfit(k); p.pic = rm8TurnPic(k); p.name = rm8TurnName(k);
			pl.push_back(p);
		}
		rm8UiPushHidden();	// [V] Movie_HideMainUI (CMiniGameHost_vf16 @0x50ddd0): the board HUD and dialogs are hidden while the game runs
		started = rm8MiniStart(game, pl, (unsigned)rnd() + 1u);
		if(!started) rm8UiPopHidden();
		if(!started) rm8Log("events: minigame %d could not start (scenes/minigame data?)", game);
	}
	bool update(float dt) override
	{
		if(!started) return true;
		if(!rm8MiniDone()) return false;
		if((doneT += dt) < 0.5f) return false;	// [A] the last frame stays 0.5 s like the standalone host
		Rm8MiniResult res;
		bool ok = rm8MiniFinish(res);
		rm8UiPopHidden();
		rm8MusicResumeBoard();	// [V] MiniGameHost_Cleanup @0x50e300: the minigame track stops, the board music returns
		if(ok) for(const Rm8MiniReward &r : rm8MiniRewards(res)) applyReward(ev, r);
		return true;
	}
};

}	// namespace

Task *makeMiniTask(int seat, int ev){ return new MiniTask(seat, ev); }

// [V] MiniGame_ExecuteGame @0x4fcb10: false = not handled (game 0x15 with a human in the game: the task runs instead)
bool aiMiniGame(int seat, int ev)
{
	if(ev == 0x15) for(int k = 0; k < rm8TurnRoleCount(); k++) if(rm8TurnStatus(k) != 7 && rm8TurnHuman(k)) return false;
	Rm8MiniReward r = rm8MiniComputer(ev, difficulty(), [](){ return rm8GameRand(); });
	r.seat = seat;
	applyReward(ev, r);
	return true;
}

}	// namespace ev
