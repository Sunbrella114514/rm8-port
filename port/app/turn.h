// Internal interfaces between the turn flow (roles.cpp), the rules (game.cpp) and the embedded Python (python.cpp).
// Seats are the indices of the spawned characters (the game's +0x1A8).
#pragma once
#include "scene.h"
#include <cstdint>

// [N] money / tickets / prices are int32_t like the original 32-bit game (C++ long is 32-bit on Windows, 64-bit on Android).
// Sums and products that can leave the int32 range use these: the two's-complement wrap of x86 add / imul, identical on every
// platform and without the undefined behaviour of a signed overflow
inline int32_t rm8Add32(int32_t a, int32_t b){ return (int32_t)((uint32_t)a + (uint32_t)b); }
inline int32_t rm8Sub32(int32_t a, int32_t b){ return (int32_t)((uint32_t)a - (uint32_t)b); }
inline int32_t rm8Mul32(int32_t a, int32_t b){ return (int32_t)((uint32_t)a * (uint32_t)b); }
// [N] double -> int32 like x86 cvttsd2si / the original's ftol: truncation, out of range or NaN = 0x80000000 (C++ would be undefined)
inline int32_t rm8F2I32(double v){ return v > -2147483649.0 && v < 2147483648.0 ? (int32_t)v : INT32_MIN; }

// ---- turn flow (roles.cpp)
int rm8TurnRoleCount();
int rm8TurnCurrent();
int rm8TurnCharId(int seat);			// roleinfo Id 1..16
int rm8TurnSex(int seat);			// [V] roleinfo +0x304 (config sex): 0 female, 1 male (the festival cards)
const char *rm8TurnAiScript(int seat);		// roleinfo AI (e.g. "AI_01")
bool rm8TurnHuman(int seat);			// [V] +0x1B0 == 0 (a seat taken over by the computer is NOT human: it runs through the computer's code)
bool rm8TurnAuto(int seat);			// [V] +0x1B0 == 2: a human seat played by the computer (auto play / 托管), docs/autoplay.md
bool rm8TurnIsPlayer(int seat);			// [V] +0x1B0 != 1: a human or a taken-over seat (the survivor counts of the bankruptcy / new-day checks)
bool rm8AutoKey(int seat);			// [V] key F1..F4 = seat 0..3: toggle the auto play of that seat (true if something changed)
bool rm8AutoRelease();				// [V] Game_SelectNextPlayer @0x4a6d60: any key while the current role walks gives the next auto seat back (true = the key is consumed)
bool rm8AutoSupplyOpen();			// [V] gsys button 1: ask "auto play?" (supply.ui); a confirmed answer hands the current role to the computer
bool rm8AutoSet(int seat, bool on);		// [N] netplay: the host hands an absent player's seat to the computer / back (Role_Enable / DisableAutoPlay in any state)
void rm8RolesFixCameraYaw(float yaw);		// [N] netplay: the rules' camera yaw is exactly `yaw` (no atan2f); < 0 = from the camera (single player)


int rm8TurnNode(int seat);			// road node the character is on (from)
int rm8TurnStatus(int seat);			// [V] +0x1B4: 0 normal ... 7 bankrupt
void rm8TurnSetStatus(int seat, int st);
void rm8TurnEnqueue(int seat, int cmd, int a, int b);
void rm8TurnEnqueueAt(int seat, int pos, int cmd, int a, int b);	// [V] Role_EnqueueMsgAt (0 = front)
void rm8TurnClearQueue(int seat);
bool rm8TurnQueueHas(int seat, int cmd, bool headOnly);	// the queue's first message (headOnly) or any message has this cmd (the original scans +0x5D0)
void rm8TurnSetState(int seat, int state);	// [V] +0x1B8
int rm8TurnState(int seat);
int rm8TurnDiceMax(int seat);			// [V] +0x22C dice count (vehicle)
int rm8TurnDiceCount(int seat);			// [V] +0x230 dice of the next throw (the slot toggles of go.ui decide it)
bool rm8TurnDiceSlotOn(int seat, int slot);	// [V] slot flag 1..3 (slot 1 is always on)
bool rm8TurnToggleDiceSlot(int seat, int slot);	// [V] Ui_ToggleDiceSlotOnSelect @0x4e8410 / Role_ToggleDiceSlot @0x48a1c0: slot 2 / 3 on <-> off (current role, state 6)
void rm8TurnPlayAnim(int seat, int anim, bool once);
void rm8TurnFocus(const float pos[3]);
bool rm8TurnEventDieStart(int seat);		// [V] Role_StartDiceSceneCount1 @0x484d70: one 3D die thrown for a role in state 9 (event dialogs), state 4 while it rolls
int rm8TurnEventDieTake(int seat);		// the finished die's value (1..6) once, else 0; the role is back in state 9
bool rm8TurnEventDieBusy(int seat);
void rm8TurnSkip(int seat);			// [V] Role_SkipTurnToNextRole
int rm8TurnSkipRequest(float *left = nullptr);	// [V] Role_TryStopMoveUntilDone @0x4986b0: the first waiting role ends its wait (seat or -1), skip.cpp
const char *rm8TurnName(int seat);		// roleinfo section name (UTF-8)
const char *rm8TurnPic(int seat);		// roleinfo picName (p_NN)
// [V] what Role_SpawnCharacterModel @0x48b410 reads: world position, roleinfo offset (+0x344) and scale (+0x338), outfit (+0x1AC); false if no such seat
bool rm8TurnRoleLook(int seat, float pos[3], float off[3], float scale[3], int &outfit);
void rm8TurnFollowerDebut(int seat);		// [V] vf36 of the class of 伊格魯 (@0x47e3b0): its monster companion runs in from the right at the game-start debut
int rm8TurnOutfit(int seat);			// [V] role +0x1ac: the outfit 0..4 (4 = swimsuit models, artId + 0xC0)
// [V] Role_CloseModalUi @0x4889b0: write the dialog result into the first queued message with b == 0x80000000
void rm8TurnModalResult(int seat, int value);
// ---- road / placement access for the cards (roles.cpp). Nodes are road node indices (Rm8Scene::rnodes)
int rm8TurnNodeTo(int seat);			// [V] +0x44: the node the role walks to next
int rm8TurnLinks(int node, int *out, int max);	// neighbour nodes (road graph links), returns the count
bool rm8TurnNodePos(int node, float p[3]);
void rm8TurnHotelWalk(int seat, bool enter);	// [V] messages 0x13 / 0x14 (CPlayerTag vf34 / vf35 with 0 @0x4852e0 / 0x4853a0): the role walks into / out of the hotel (state 0xb / 0xc); the end of the walk-in = rm8CardsEnterHotel
void rm8TurnFollowerRunOff(int seat);		// [V] vf27 @0x47dfa0: the scene carries the role off (ambulance / police car / UFO): the companion of 伊格魯 / 莎拉 gets the camera's right vector as its velocity
void rm8TurnTurnAround(int seat);		// [V] vf16 Path_ComputeDistance @0x47b5f0: walk the other way (Role.Turn)
void rm8TurnPlaceAt(int seat, int node, int to);	// [V] vf15 Path_SetPoints @0x47b670 + Game_MovePlayerToPosition: put the role on `node` heading to `to`
void rm8TurnSwapPlaces(int a, int b);
void rm8TurnSetVehicle(int seat, int vehicle);	// [V] Role_SetTrfTool @0x48a0c0: dice count, model set and speed of the vehicle (1 walk, 2 bike, 3 car)		// [V] CPlayerTag__OnPositionSwap @0x490c60
int rm8TurnSelectNext(int prev, int cur);	// [V] Path_SelectRandomNext @0x47b8e0 (npc.cpp: the robot doll walks the road graph)
void rm8TurnPossessSync(int seat, bool possessed);	// [V] Role_SetPossession @0x48b6e0 (npc.cpp): speed[0] while possessed; released: the models and speed of the current vehicle

// ---- rules (game.cpp)
void rm8GameNewGame(Rm8Scene &s, unsigned seed);	// after the characters are spawned
void rm8GameClear(Rm8Scene &s);
// [A] start options chosen in the menus (setup block +0x0C..+0x1C, docs/rules_land.md 1.1); applied by the next rm8GameNewGame.
// The defaults are the original defaults (Game_InitOptions @0x5031c0) = what the port used before the menus existed.
struct Rm8GameOptions { int cash = 100000, tickets = 50, vehicle = 0, timeLimit = 2, winCond = 5; };
void rm8GameSetOptions(const Rm8GameOptions &o);
Rm8GameOptions rm8GameGetOptions();
int rm8GameDaysLeft();					// [V] Date_CalcDaysDiff(end date, today) @0x431680; 100 for an unlimited game
void rm8RolesSetOutfits(const std::vector<int> &outfits);	// [V] seat outfits 0..4 (setup seat +4 -> the body colour texture A%02d_body_f_%02d, Asset_LoadCharacterTexture @0x4934a0) for the next rm8RolesSpawn
void rm8RolesSetHumans(unsigned mask);		// seats (bit mask) played by humans in the next rm8RolesSpawn, besides its humanSeat argument
void rm8TurnSetDiceMax(int seat, int n);	// [V] start vehicle: +0x22C = vehicle (1 walk .. 3 car)
bool rm8GameMessage(int seat, int cmd, int a, int b);	// a message the turn flow does not handle; true if handled
void rm8GameOnNewRound();				// [V] Game_TurnNextRole wrapped to seat 0
int rm8GameTakeLeader();					// [V] Game_FindLeaderByWinCondition @0x4a6090: the winner of the end date (-1 none): the turn goes to it
bool rm8GameCheckBankruptcy(int seat);			// [V] Role_CheckBankruptcy @0x48ee10
bool rm8GameOver();
int32_t rm8GameCash(int seat);	// [N] int32_t
int32_t rm8GameDeposit(int seat);
int32_t rm8GamePI();
int rm8GameDay();
int rm8GameShape(int seat);				// [V] +0x1BC possession (0 none)
int rm8GameTerraCount();
int rm8GameTerraOwner(int t);
int rm8GameTerraLevel(int t);
int rm8GameTerraType(int t);
int rm8GameTerraStreet(int t);
int32_t rm8GameTerraCharge(int t);	// [N] int32_t
int32_t rm8GameTerraRent(int t, bool chain);
int rm8GameNodeTerra(int node);
int rm8GameTerraNode(int t);				// [R] rules_plus B5: the first road node whose plot is t (-1 none): a building pick is turned into the node the card scripts expect
int rm8GameNodeCount();
bool rm8GameNodeTile(int node, int &tx, int &ty);	// the road node's tile
std::string rm8GameMapId();				// "01".."11", "download201".. of the loaded map
rw::Clump *rm8GameAddModel(const std::string &dff, const std::string &texdir, const float pos[3], float yaw, const std::string &anim, bool loop);
rw::Clump *rm8GameAddArt(int art, const float pos[3], float yaw, int anim);	// a building.bin art (scene art table)
int rm8GameBuildingArt(int id, int level);	// building.bin art of (building id, level), 0 none
void rm8GameRemoveModel(rw::Clump *c);
// ---- card effects on the rules (game.cpp, called by cards.cpp)
int rm8GameNodeEvent(int node);				// road node event icon id (0 none)
bool rm8GameTerraPos(int t, float p[3]);
void rm8GameSetTerra(int t, int owner, int level, int build);	// owner -2 / level -1 / build -1 = keep; updates the model
void rm8GameSetShape(int seat, int shape);		// +0x1BC possession
int rm8GameInvisible(int seat);				// +0x1E4 invisible turns left (no rent)
void rm8GameSetInvisible(int seat, int turns);
int32_t rm8GameTerraPrice(int t);				// [N] [V] Terra_CalcPrice(1) @0x4efc80
bool rm8GameStreetClosed(int street);			// +0x108 > 0
bool rm8GameTerraModel(int t, rw::Clump *&c, int &art);	// the plot's current article (building or owner sign) and its art id; false = none
int rm8GameTerraBuild(int t);				// building type: 1 house, 2 institute, 3 chain store, 4 hotel (0 none)
std::string rm8GameTerraTitle(int t);			// the auction's name of a plot (street name / building name / "<str#14>")
bool rm8GameTerraModelDef(int t, std::string &dff, std::string &texdir, std::string &anim);	// the plot's building model (false = empty plot)
int32_t rm8GameAuctionBase(int t);				// [N] Terra_CalcPrice(1) / 2
void rm8GameTerraAdjustPct(int t, int pct);		// [V] Terra_AdjustPriceByPercent @0x4eed70 (+-10 by the news): the plot's price percent (+0x48)
int32_t rm8GameRentPaid(int seat);				// [N] [V] +0x33C rent paid in total (balance screen)
int rm8GameBadDays(int seat);				// [V] +0x340 unlucky days (balance screen)
void rm8GameNoteBadDay(int seat);			// [V] Role_RunTurnStartStep @0x4842f0 step 9: +0x340++ (status 1 / 3 / 4 / 5, the sleepwalk)
void rm8GameStreetClose(int street, int days);		// [V] Terra_LoadCloseIcon @0x42d3b0 (rent exempt for `days`)
void rm8GameStreetFlow(int street, int pct);		// [V] Terra_SetHeight @0x42d4c0 (street factor x pct%, 9 days)
void rm8GameDate(int &year, int &month, int &day, int &weekday);	// weekday 1..7 (7 Sunday), 0 = first day Sunday
int32_t rm8GamePoints(int seat);	// [N] int32_t
// money / points: Add* ignore v <= 0; SubMoney takes cash then deposit when depositFirst = false (rules_land.md)
void rm8GameAddMoney(int seat, int32_t v, bool deposit);	// [N] int32_t
void rm8GameSubMoney(int seat, int32_t v, bool depositFirst);
void rm8GameSetMoney(int seat, int32_t cash, int32_t deposit);
void rm8GameAddPoints(int seat, int32_t v);
int rm8GameRand();					// the rules RNG (MSVC rand sequence)
void rm8GameReseed(unsigned s);			// a loaded game draws new rules random numbers (the original does not save rand())
// texts for queued dialog (cmd 1, b = 0x80 okcancel) / info (0x1B) messages: a = rm8GamePushText(utf8)
int rm8GamePushText(const std::string &utf8);
int rm8GamePushTextPic(const std::string &utf8, int uvlId);	// an info entry with a picture (e.g. a puzzle piece): shown with gtinfo.ui (text 1, picture 2)
int rm8GamePushTextPicFile(const std::string &utf8, const std::string &pack);	// the entry carries a picture NAME (Msg_CreateTextEntry +0x100, e.g. "prt\npc\p_18_01.tga" for npc\p_18_01): gtinfo vf+0xc
const std::string &rm8GameText(int id);
// HUD numbers
bool rm8GameHud(int seat, int32_t &cash, int32_t &deposit);	// [N] int32_t

// ---- Python (python.cpp)
bool rm8PyInit(const std::string &pysrcDir);
// call fn(param) in the given AI script module; returns false if the module/function is missing or raised
bool rm8PyCallAI(const char *script, const char *fn, int32_t param, int32_t &result);	// [N] int32_t

// ---- HUD and dialogs (hud.cpp)
void rm8HudInit(const std::vector<std::string> &seatPics);
// [V] the single-die panel of the event dialogs: the go dialog (go.ui, class 0x5E2538 = Unk_GetOrInitGlobalObjectRef @0x494180) with ONE die slot (Cls_5e2538__vf10_Init @0x4e8b80 hides 3 / 4),
// moved to the dialog's dice widget (u_Widget_SetPositionXY at the end of the loaders); its button is Ui_OnEvent_StartDiceScene @0x4e8a80 -> `onThrow`. on = false puts it back.
void rm8HudDieGo(bool on, float px, float py, void (*onThrow)(void *), void *arg);
bool rm8HudDieGoRect(float &x, float &y, float &w, float &h);	// the throw button's screen rectangle (test hook), false when not shown
void rm8HudOpenModal(int seat, int kind, const std::string &text);
void rm8HudCloseModal();
int rm8HudAutoPlayTest(float dt);	// test hook RM8_AUTO_PLAY=<s> / RM8_AUTO_PLAY_OFF=<s> (hud.cpp, docs/autoplay.md): presses the real buttons; returns seat + 1 when the app should push that seat's F-key
void rm8HudInfo(const std::string &text);
void rm8HudInfoPic(const std::string &text, int uvlId);	// [V] Role_ShowInfoDialog @0x488ae0 for an entry with a picture: gtinfo.ui (text id 1, picture id 2)
void rm8HudInfoPicFile(const std::string &text, const std::string &pack);	// [V] Role_ShowInfoDialog @0x488ae0, entry +0x100 != 0: gtinfo.ui, picture by file name (id 2)
bool rm8HudShotTriggered();	// test hook RM8_SHOT_ON_INFO (rm8_app.cpp shot loop)
void rm8HudSeatBankrupt(int seat);	// [V] gmaindlg vf3: seat hexagon -> npc\<pic>_14
void rm8HudRender(rw::Camera *cam);
struct Rm8Ui; Rm8Ui *rm8HudMain();	// gmaindlg (card slots 31..38, buttons 6..10)
