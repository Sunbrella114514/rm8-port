// Enhanced AI / house rules ("增強人機 / 自訂規則", rules_plus), docs/difficulty_ai_analysis.md part B and its implementation section.
// Everything in here is [R] = a house rule that does not exist in the original game. The switch is OFF by default; with it off nothing in this module
// touches a shared random stream, a save chunk or a rule (the port behaves like the original, byte for byte). The rule set belongs to the GAME
// (new game: the option; loaded game: the record's RULE chunk, a record without one = the original rules), so story mode follows the switch too.
//
// ALL tunables of the house rules are in Rm8PlusTuning below (one table, three columns = difficulty 1 / 2 / 3 of the game's difficulty snapshot,
// rm8RulesLevel()). The original difficulty effects (docs A1.3) stay as they are; these numbers only scale the new rules.
#pragma once
#include <cstdint>	// [N] int32_t money values
#include <string>

struct Rm8PlusTuning {
	// ------------------------------------------------------------------ [R] B1 loans (index 0 / 1 / 2 = difficulty 1 / 2 / 3)
	// Python Borrow() is still asked (every character keeps its own tendency); its answer is then vetoed or reduced here.
	// liquid = cash + deposit; "x PI" = multiplied by the price index
	int32_t loanGateLiquid[3]      = {150000, 80000, 50000};	// [N] no loan while liquid >= gate x PI (difficulty 3 = the script's own "very short of money" line)
	int  loanIgnoreGatePct[3]   = {30, 10, 0};		// % chance that the gate is ignored (the original behaviour for this decision)
	int32_t loanTopUpTarget[3]     = {0, 120000, 100000};	// [N] x PI: the amount = the smallest tier (100000 / 200000 / 300000) that lifts liquid to this; 0 = keep the script's amount
	int32_t loanMaxAmount[3]       = {300000, 200000, 300000};	// the top-up never exceeds this
	// early repayment (at the bank, stopping or passing): liquid >= principal x repayPct / 100 + repayMargin x PI; repayPct 0 = never (difficulty 1)
	int  repayPct[3]            = {0, 120, 110};
	int32_t repayMargin[3]         = {0, 70000, 50000};	// [N] tuned 2026-11-17: 40000/30000 left the AI below the loan gate right after repaying (borrow-repay loops); 70000/50000 cut loans and bankruptcies at difficulty 2/3
	int  repayMinAgeDays[3]     = {0, 5, 5};		// not on the day the loan was taken (no borrow-and-repay ping-pong)
	bool repayOnPass            = true;			// the AI may also repay when it only passes the bank (the original passing event 99)
	// repayment reserve: within reserveDays of the due date a purchase / building must leave liquid >= principal x reservePct / 100; 0 days = off
	int  reserveDays[3]         = {0, 10, 20};
	int  reservePct[3]          = {110, 110, 110};

	// ------------------------------------------------------------------ [R] B2 hospital exit: anti-camping (the same for every difficulty and for human victims too)
	int  antiCampGraceTurns     = 3;	// a blast within this many own turns after leaving the hospital halves the stay (4 -> 2 -> 1, minimum 1): days = max(1, days >> chain)
	int  antiCampBubbleTurns    = 2;	// leaving after a stay that was cut to antiCampFloorDays gives a bubble: blasts are ignored for this many own turns
	int  antiCampFloorDays      = 1;
	// AI trap on the hospital exit road (mine 16 / time bomb 17): chance per qualifying turn (index = difficulty); the original hospital block of planCards is kept
	int  exitTrapPct[3]         = {0, 30, 60};
	int  exitTrapNodes          = 3;	// the first nodes of the exit branch (counted from the hospital node, the junction is not used) that may take the trap
	int  exitTrapMaxDays        = 1;	// the victim leaves within this many days (days left of the stay <= this)

	// ------------------------------------------------------------------ [R] B6 AI minigame low scores
	int  miniLowPct[3]          = {35, 25, 15};	// % chance that a computer's result is drawn from the low range instead (not game 21)
	// low range: tickets = base + plusRand % span (game 18, 19, 20, 22, 23); game 23 is (base + plusRand % span) x 10
	int  miniLowBase[24]        = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 3, 0, 1, 1};
	int  miniLowSpan[24]        = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 30, 30, 25, 1, 50, 3};

	// ------------------------------------------------------------------ [R] B3 hate (hate[victim][aggressor], a decaying score)
	// An attack adds its weight to the victim's score for the aggressor; the score is multiplied by hateDecay at every new game day. A role "hates" the aggressors whose score is
	// >= hateThreshold (the strongest hateMaxCount of them). Only computers (and managed seats) act on it; the HUD shows it for every seat (the human's too).
	float hateThreshold[3]      = {7, 5, 4};	// tuned 2026-11-18: 9 / 6 / 4 left difficulty 1 without any hated role in 12 long games (scores peak at 8.8 there)
	float hateDecay[3]          = {1.0f, 1.0f, 1.0f};	// per game day; 1 = hate never fades (user, 2026-11-18: hate stays until a more hated role appears)
	int  hateMaxCount[3]        = {1, 2, 3};		// roles one role hates at once
	int  hateActPct[3]          = {35, 60, 90};		// % chance that a decision that CAN be aimed at a hated role is aimed at it (court, magic house, card plans)
	int  hateHumanPct[3]        = {50, 100, 100};		// weight % of an attack made by a human (difficulty 1: the computers forgive the human half as fast)
	int  hateMaxPerDay[3]       = {1, 2, 3};		// at most this many revenge actions per game day on the same target (all haters together: no pile-on)
	int  hateReliefPct[3]       = {0, 0, 0};		// % of the score a revenge action takes away (0: revenge does not calm it down, user 2026-11-18)
	float hateCap               = 1e9f;			// no score above this (effectively none: a new enemy replaces an old one only by a higher score)
	float hateCourtWin          = 3, hateCourtLose = 1;	// the defendant hates the accuser (success / failure of the suit)
	int32_t hateRentUnit           = 2000, hateRentMin = 1500;	// [N] x PI: rent paid to a role adds rent / unit (at most hateRentMax); rent < hateRentMin counts nothing
	float hateRentMax           = 3;
	float hateBlast[4]          = {4, 6, 3, 4};		// victim hit by a missile / nuke / mine / time bomb of the aggressor
	float hatePlotBlast         = 1, hatePlotBlastMax = 4;	// the owner of a plot whose building a missile / nuke destroys: per plot (cap)
	float hateMagic             = 2;			// a bad magic house effect cast by the aggressor
	// card weights by card id (0 = the card is no attack): 1 buy-out 3 demolish 4 swap 6 monster 10 turtle 11 turn 13 stay 20 half-cash 25 grab 27 tax 28 frame-up 34 road bully 37 poor god 38 jinx 41 fool 45 con
	int  hateCard[50]           = {0, 3, 0, 3, 2, 0, 3, 0, 0, 0, 2, 2, 0, 2, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 4, 5, 0, 0, 0, 0, 0, 2, 0, 0, 3, 3, 0, 0, 3, 0, 0, 0, 5, 0, 0, 0, 0};

	// ------------------------------------------------------------------ [R] B4 card combos (AI)
	// comboBudget re-plans per turn (the original plans once); a re-plan only happens after the AI used a card this turn and with comboPct % chance; comboRelax lets the Land-god plan skip
	// its "street closed / seal-up card" condition and use lower thresholds
	int  comboBudget[3]         = {0, 2, 3};
	int  comboPct[3]            = {0, 50, 90};
	int  comboRelax[3]          = {0, 0, 1};
	int32_t comboRentMin           = 2000;		// [N] x PI: a rival's next three nodes must hold an own plot with at least this rent for the turtle + roadblock trap

	// ------------------------------------------------------------------ [R] B5 missiles / nuke at buildings (AI value function planBlast; the human can always pick a building)
	int  blastAiPct[3]          = {0, 50, 100};	// % chance that a qualifying best target is fired at
	int  blastMinMissile[3]     = {6, 6, 4};	// minimum value of the best target (sum of the enemy plot levels, hated owners count x blastHatePct / 100, rivals in the area add blastRoleValue)
	int  blastMinNuke[3]        = {12, 12, 9};
	int  blastHatePct           = 200;
	int  blastRoleValue         = 2;		// a rival (not bubbled / guarded) in the area
	int  blastSelfFactor        = 3;		// own plot level in the area counts -factor x level; a missile never fires with an own plot / the own node inside (like CardPy_18)
	bool blastNukeRange         = true;		// the AI nuke is limited to the card range too (the original CardPy_19.Competence is global)

	// ------------------------------------------------------------------ [R] B3 magic house (AI)
	int  magicSelfGoodPct[3]    = {0, 30, 60};	// no hated target: % chance that the computer casts a GOOD effect on itself instead of a random one on somebody else
};
const Rm8PlusTuning &rm8PlusTuning();

// ---- the rule set of the running game
bool rm8RulesOn();			// the house rules are on in this game
int  rm8RulesLevel();			// 1..3 the difficulty snapshot of the game (only meaningful when on)
// a game starts (new game or the board of a load, rm8_app.cpp loadMap): plus = the option (rules_plus in rm8_settings.txt) or RM8_RULES_PLUS=0/1; the snapshot = rm8Difficulty().
// A load then overwrites the set from the record (RULE chunk). seed starts the rules' own random stream.
void rm8RulesBeginGame(unsigned seed);
void rm8RulesReseed(unsigned seed);	// an interactive load draws a fresh stream (like the dice / rules / python streams)
int  rm8PlusRand();			// [R] the house rules' own random stream (0..32767); never the shared ones

// ---- B1 loans (event_bank.cpp / game.cpp call these; all return the original answer when the rules are off)
void rm8RulesNoteStoryLoan(int seat);	// the story script recorded a loan for the seat (大老千 day 1): the house rules never touch it
bool rm8RulesStoryLoan(int seat);	// a story-scripted loan is open for the seat
void rm8RulesLoanTaken(int seat);	// a normal loan was taken: forgets the story-loan mark
// the script's Borrow() answered `asked` (> 0): returns the amount to lend (0 = the AI does not borrow now)
int32_t rm8RulesLoanDecide(int seat, int32_t liquid, int32_t asked);	// [N] int32_t
// early repayment wanted? (principal > 0, not a story loan, loan age in days)
bool rm8RulesRepayWanted(int seat, int32_t liquid, int32_t principal, int ageDays, bool passing);	// [N] int32_t
// a purchase / building of `price` would leave too little for the loan due in daysLeft days: true = the AI must not do it
bool rm8RulesReserveVeto(int seat, int32_t liquid, int32_t price, int32_t principal, int daysLeft);	// [N] int32_t

// ---- B6
// the score of a computer's minigame (game 18 / 19 / 20 / 22 / 23): `orig` = the original result; returns it, or (with the difficulty's chance) a low one that is never above it; other games unchanged
int rm8RulesMiniScore(int game, int orig);

// ---- B3 hate (all of it is a no-op while the house rules are off)
void rm8HateNote(int victim, int aggressor, float weight, const char *why);	// victim was attacked by aggressor: hate[victim][aggressor] += weight (x hateHumanPct for a human aggressor)
float rm8HateScore(int victim, int aggressor);
// the roles `seat` hates now (score >= threshold, not bankrupt), strongest first, at most hateMaxCount; returns the count
int  rm8HateList(int seat, int out[8]);
// a computer decision that can be aimed at the hated role `target` is about to be: rolls hateActPct and the per-day cap; true = aim it (the hate is relieved, the day counter counts)
bool rm8HateAct(int seat, int target, const char *what);
void rm8HateNewDay();			// decay (game.cpp rm8GameOnNewRound)
// the time bomb that sits on a role remembers who laid it (npc_act.cpp); the pending owner of the mine / bomb a role just stepped on
int  rm8HateTotalLog();			// [A] test: number of hate notes so far
