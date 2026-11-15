// Event tasks (events.cpp): the game's Game_StartEventTask @0x4a4160 -> CreateGameObject(event) @0x4b1bc0, task
// objects with Update (vf1) and a .ui (vf16); see docs/ui_system.md 5.3 for the event number -> class table and
// docs/events.md for what is implemented.
#pragma once
#include "turn.h"

// a role stopped on a road node whose event icon is eventId (Rm8RoadNode::event, not 7/8); called from the 0x1D
// landing message (game.cpp). Returns true if the event was handled (task started or done); false = ignore it.
bool rm8EventLand(int seat, int eventId, int node);
// [V] u_CPlayerTag_UpdateTurnCountdown @0x485a20: a walking role passes a road node whose event is the bank (5) with steps left:
// event 99 (loan disabled). Returns true when the role must stop (task or AI action started); the walk resumes with message 0x26.
bool rm8EventPass(int seat, int node);
// per frame (game time); a running task ends the turn when done
void rm8EventUpdate(float dt);
bool rm8EventRunning();
bool rm8EventSkip();		// [V] the running task's vf13 (skip.cpp, docs/skip.md): its timer / voice cut short; false = it waits for the player or has no skip
bool rm8EventSkipDialogs();	// [A] the card-received box is hidden by a timer here (the original: the queued message after the 0x65 wait): hide it with the wait
void rm8EventClear();	// new map / new game
void rm8EventSceneClear();	// the scene articles of the events (wonder buildings) go; called by rm8GameClear while the scene is alive
bool rm8EventLoadData(const std::string &scenesDir);	// scenes/questions.txt (scripts/export_questions.py); once at start
// queued role messages owned by the events: 4 receive card, 7 add points, 8 sub points, 0x47 refresh UI, 0x4E pay from deposit
// (called from rm8GameMessage for commands it does not handle); true if handled
bool rm8EventMessage(int seat, int cmd, int a, int b);
// [V] Role_CheckLoanDueAndForceRepay @0x483170, turn start step 5 (message 0x67): loan warnings and forced repayment
void rm8EventTurnStart(int seat);
// before the main camera's beginUpdate (next to rm8RenderReflection): renders the 3D presenters of the open event dialogs offscreen
void rm8EventPreRender(rw::Camera *main);
// [V] Game_OnTurnStart @0x4a3b50 once per new day (after the 5th), with the day of the month: day 15 starts the lottery draw (event 0x62, Game_TriggerHolidayEvent
// @0x4a66b0) for the current role when numbers were sold. Returns true if an event task started (the original then skips the date news).
bool rm8EventDaily(int mday);
// wonders (event_wonder.cpp): [V] Role_TriggerLandLevelEvent @0x4863f0 for a node without an event icon (true = the wonder event started); Wonder_CheckConstruction @0x4e1800;
// the land wonder's rent change, the money wonder's interest divisor (10 / 5 / 20), the card wonder's price (CardShop_CalcCardPrice @0x4e6f90)
bool rm8EventWonderNode(int seat, int node);
// [V] Role_OnArriveCommercialBuilding @0x486260 -> Game_SelectCard(the plot's building): the chain store / hotel of another role, the institute of the own role open their dialog
// (state 9); false when nothing starts. game.cpp checks the conditions (owner, level, status, rent exemption).
bool rm8EventBuilding(int seat, int terra);
void rm8EventInstituteTick();	// [V] TurnStart_UpdateTerrain @0x4f11f0 (new day): the researches of the institutes advance
int rm8EventWonderOwner(int idx);
int rm8EventWonderOwnerRaw(int idx);	// [V] the miracle table entry's owner seat (DAT_006403bc + 8 + idx * 0x24), as story puzzle condition 16 reads it (-1 = none)
long rm8EventWonderRent(int payer, int plotOwner, long rent);
int rm8EventInterestDivisor(int seat);
int rm8EventCardPrice(int seat, int ticket);
long rm8EventLoan(int seat);
void rm8EventSetLoan(int seat, long amount);	// [V] Bank_TrySetLoan @0x47ea00: only the record (principal, today's date, 3 months), no money moves (the story's 大老千 start)	// outstanding loan principal (the money struct +0x298)
