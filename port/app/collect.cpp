// Collections and profile progress, see collect.h. [V] = verified in the decompile (address), [A] = port adaptation.
#include "collect.h"
#include "cards.h"
#include "ui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace {

// ---- the game's GameDate object (this = the object behind Event_*; field offsets as in the accessors @0x4aa190..0x4aa5d0)
//   +role*0x40 flag (unlock), +4 level, +0xC + k*4 item k (1..9); +0x800 + i*4 event i (maps 1..11); +0x840 + i*4 quest data (1..9); +0x880..0x88C flags
struct Profile {
	int unlock[17] = {0}, level[17] = {0}, item[17][10] = {{0}};
	int ev[12] = {0};
	int a80c = 0, a830 = 0;		// +0x80C (= event 3, map 3 "unlocked" once set) and +0x830 (= event 12): the file's two magic flags
	int quest[10] = {0};
	int flags[4] = {0, 0, 0, 0};
} g_p;
Rm8Box g_box;
std::string g_gamedatePath, g_boxPath;
bool g_inited = false;

// [V] magic tables of RichMan8.exe (a boolean is stored as a specific number): unlock of roles 11..16 @0x5E3D44, items of role r @0x5E3D84 + r*4,
// level of role r (5 values, index = level) @0x5E3DC8 + (r-1)*0x14, map flags 0x2e1 / 0x112 (GameDate_Load @0x4a99e0)
const int T_UNLOCK[17] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4576, 938, 63, 5162, 5632, 12976};
const int T_CLEAR[17] = {0, 7349, 9027, 764, 62, 176, 93, 5377, 888, 1245, 4344, 8356, 9, 333, 4576, 1432, 8634};
const int T_STAGE[17][5] = {{0}, {87, 1674, 9387, 740, 12783}, {4376, 874, 194, 90, 505}, {6235, 18, 937, 5624, 8843}, {562, 42, 157, 4735, 12901},
	{9235, 7235, 54, 124, 452}, {9237, 264, 3556, 2, 666}, {5238, 937, 5623, 41, 9190}, {1765, 452, 6348, 7356, 3}, {937, 1376, 547, 819, 43201},
	{23, 8376, 6325, 391, 653}, {9375, 54, 2138, 3876, 72}, {7364, 376, 2847, 219, 321}, {9876, 658, 46, 285, 1079}, {935, 5482, 28, 2647, 2213},
	{8376, 352, 2312, 219, 91}, {645, 4319, 298, 777, 54}};

void setDefaults()	// [V] Event_InitializeData @0x4aa0b0
{
	g_p = Profile();
	for(int r = 1; r <= 10; r++) g_p.unlock[r] = 1;
	for(int i = 1; i <= 11; i++) if(i != 3) g_p.ev[i] = 1;
}

bool rd(FILE *f, int &v){ return fread(&v, 4, 1, f) == 1; }

// [V] GameDate_Load @0x4a99e0: 181 ints; unreadable / short file = the defaults stay
bool loadGameDate(const std::string &path)
{
	FILE *f = fopen(path.c_str(), "rb");
	if(!f) return false;
	fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
	if(sz != 181 * 4){ fclose(f); return false; }
	Profile p; for(int r = 1; r <= 10; r++) p.unlock[r] = 1;
	int v = 0; bool ok = true;
	for(int r = 11; r <= 16; r++){ ok &= rd(f, v); p.unlock[r] = v == T_UNLOCK[r] ? 1 : 0; }
	for(int r = 1; r <= 16; r++){
		ok &= rd(f, v);
		for(int k = 0; k < 5; k++) if(v == T_STAGE[r][k]){ p.level[r] = k; break; }
		for(int k = 1; k < 10; k++){ ok &= rd(f, v); p.item[r][k] = v == T_CLEAR[r] ? 1 : 0; }
	}
	ok &= rd(f, v); p.a80c = v == 0x2e1;
	ok &= rd(f, v); p.a830 = v == 0x112;
	for(int i = 1; i < 10; i++) ok &= rd(f, p.quest[i]);
	for(int i = 0; i < 4; i++) ok &= rd(f, p.flags[i]);
	fclose(f);
	if(!ok) return false;
	for(int i = 1; i <= 11; i++) p.ev[i] = (i == 3) ? p.a80c : 1;	// [H] the file only keeps events 3 and 12; the others stay as initialised
	p.ev[3] = p.a80c;
	g_p = p;
	return true;
}

// [V] GameDate_Save @0x4a9d60
void saveGameDate()
{
	if(g_gamedatePath.empty()) return;
	FILE *f = fopen(g_gamedatePath.c_str(), "wb");
	if(!f){ rm8Log("collect: cannot write %s", g_gamedatePath.c_str()); return; }
	auto wr = [&](int v){ fwrite(&v, 4, 1, f); };
	for(int r = 11; r <= 16; r++) wr(g_p.unlock[r] ? T_UNLOCK[r] : 0);
	for(int r = 1; r <= 16; r++){
		wr(T_STAGE[r][std::max(0, std::min(4, g_p.level[r]))]);
		for(int k = 1; k < 10; k++) wr(g_p.item[r][k] ? T_CLEAR[r] : 0);
	}
	wr(g_p.a80c ? 0x2e1 : 0); wr(g_p.a830 ? 0x112 : 0);
	for(int i = 1; i < 10; i++) wr(g_p.quest[i]);
	for(int i = 0; i < 4; i++) wr(g_p.flags[i]);
	fclose(f);
}

bool validRole(int r){ return r >= 1 && r <= 16; }
// [A] test override RM8_UNLOCK_ALL=1: every role / outfit / puzzle piece / map reads as unlocked, nothing is written to the profile
bool unlockAll(){ static const bool on = getenv("RM8_UNLOCK_ALL") != nullptr && getenv("RM8_UNLOCK_ALL")[0] != '0'; return on; }

}	// namespace

void rm8CollectInit(const std::string &dataDir, const std::string &gameDir)
{
	std::string d = dataDir;
	if(!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
	g_gamedatePath = d + "rm8_gamedate.bin"; g_boxPath = d + "rm8_box.bin";
	setDefaults();
	if(loadGameDate(g_gamedatePath)) rm8Log("collect: profile %s", g_gamedatePath.c_str());
	else if(getenv("RM8_IMPORT_GAMEDATE") && loadGameDate(gameDir + "/Config/gamedate.bin")) rm8Log("collect: imported %s/Config/gamedate.bin (not written back to the game folder)", gameDir.c_str());
	g_box = Rm8Box();
	if(FILE *f = fopen(g_boxPath.c_str(), "rb")){
		int v[21];
		if(fread(v, 4, 21, f) == 21){
			g_box.stage = v[0]; g_box.cash[0] = v[1]; g_box.cash[1] = v[2]; g_box.points[0] = v[3]; g_box.points[1] = v[4];
			for(int i = 0; i < 16; i++) g_box.cards[i / 8][i % 8] = v[5 + i];
		}
		fclose(f);
	}
	g_inited = true;
}

int rm8ProfileLevel(int r){ return validRole(r) ? (unlockAll() ? 4 : g_p.level[r]) : 0; }
void rm8ProfileSetMaxLevel(int r, int lv){ if(validRole(r) && g_p.level[r] < lv){ g_p.level[r] = std::min(lv, 4); saveGameDate(); } }
bool rm8ProfileRoleUnlocked(int r){ return validRole(r) && (g_p.unlock[r] != 0 || unlockAll()); }
void rm8ProfileUnlockRole(int r){ if(validRole(r) && !g_p.unlock[r]){ g_p.unlock[r] = 1; saveGameDate(); } }
bool rm8ProfileMapUnlocked(int t){ return t >= 1 && t <= 11 && (g_p.ev[t] != 0 || unlockAll()); }
int rm8ProfileQuest(int n){ return n >= 1 && n <= 9 ? g_p.quest[n] : 0; }
void rm8ProfileSetQuest(int n, int v)
{
	if(n < 1 || n > 9 || g_p.quest[n] > v) return;	// [V] @0x4aa5d0: stored when old <= new
	g_p.quest[n] = std::min(v, 9999); saveGameDate();
}

bool rm8ProfileBitFree(int bit){ return bit >= 0 && bit < 32 && !(g_p.flags[0] & (1 << bit)); }
bool rm8ProfileSetBit(int bit)
{
	if(bit < 0 || bit > 31 || (g_p.flags[0] & (1 << bit))) return false;
	g_p.flags[0] |= 1 << bit;
	int n = 0; while(n < 11 && (g_p.flags[0] & (1 << n))) n++;
	if(n > 10) g_p.unlock[15] = 1;
	saveGameDate(); return true;
}
int rm8ProfileIncrementCounter(){ g_p.flags[1]++; if(g_p.flags[1] > 3) g_p.unlock[13] = 1; saveGameDate(); return g_p.flags[1]; }
bool rm8ProfileSetFlag2(int bit)
{
	if(bit < 0 || bit > 31 || (g_p.flags[2] & (1 << bit))) return false;
	g_p.flags[2] |= 1 << bit;
	int n = 0; while(n < 4 && (g_p.flags[2] & (1 << n))) n++;
	if(n > 3) g_p.unlock[11] = 1;
	saveGameDate(); return true;
}
bool rm8ProfileSetFlag3(int i)
{
	unsigned b = 1u << (i & 31);
	if((g_p.flags[3] & b) || !(i == 0 || (i == 1 && (g_p.flags[3] & 1)) || (i == 2 && (g_p.flags[3] & 1) && (g_p.flags[3] & 2)))) return false;
	g_p.flags[3] |= b;
	if(i == 2) g_p.unlock[14] = 1;
	saveGameDate(); return true;
}

bool rm8PuzzleHas(int r, int k){ return validRole(r) && k >= 1 && k <= 9 && (g_p.item[r][k] != 0 || unlockAll()); }
int rm8PuzzleCount(int r){ int n = 0; for(int k = 1; k < 10; k++) if(rm8PuzzleHas(r, k)) n++; return n; }
int rm8PuzzlePic(int r, int k){ return (r - 1) * 9 + 0x12d + k; }

bool rm8PuzzleGrant(int role, int piece)
{
	if(piece <= 0 || piece >= 10 || !validRole(role)) return false;	// [V] 0x491830: only 1..9
	if(g_p.item[role][piece]) return false;	// [V] Event_SetItemData @0x4aa300: 0 -> 1 (and save); an owned piece changes nothing
	g_p.item[role][piece] = 1;
	saveGameDate();
	if(!g_inited) rm8Log("collect: profile not initialised, the piece is not persisted");
	return true;
}

bool rm8PuzzleCollect(int seat, int piece)
{
	if(piece <= 0 || piece >= 10) return false;
	int role = rm8TurnCharId(seat);
	if(!validRole(role)) return false;
	bool isNew = rm8PuzzleGrant(role, piece);
	// [V] 0x4918a6..0x4918f8: sprintf(buf, str434, name of the role, piece), Msg_CreateTextEntry(buf, 0, -1, picture), Role_EnqueueMsgAt(0, 0x1b, entry, 0).
	rm8TurnEnqueueAt(seat, 0, 0x1B, rm8GamePushTextPic(rm8StrFmt(434, rm8TurnName(seat), piece), rm8PuzzlePic(role, piece)), 0);	// the entry carries the piece picture: gtinfo (picture beside the text)
	rm8Log("collect: seat %d (role %d) gets puzzle piece %d (%s, %d / 9)", seat, role, piece, isNew ? "new" : "owned", rm8PuzzleCount(role));
	return isNew;
}

// ---- storage box
Rm8Box &rm8Box(){ return g_box; }

void rm8BoxSave()
{
	if(g_boxPath.empty()) return;
	FILE *f = fopen(g_boxPath.c_str(), "wb");
	if(!f){ rm8Log("collect: cannot write %s", g_boxPath.c_str()); return; }
	int v[21] = {g_box.stage, g_box.cash[0], g_box.cash[1], g_box.points[0], g_box.points[1]};
	for(int i = 0; i < 16; i++) v[5 + i] = g_box.cards[i / 8][i % 8];
	fwrite(v, 4, 21, f); fclose(f);
}
void rm8BoxClear(){ g_box = Rm8Box(); rm8BoxSave(); }

namespace {
bool addCard(int side, int id)	// [V] CardArray_FindSlotAndAdd @0x4abed0 / SlotArray2C_FindSlotAndAdd @0x4abf50: first slot < 1
{
	for(int i = 0; i < 8; i++) if(g_box.cards[side][i] < 1){ g_box.cards[side][i] = id; return true; }
	return false;
}
}

bool rm8BoxStoreCard(int cardId)
{
	bool ok = addCard(0, cardId);
	if(ok) rm8BoxSave();
	return ok;	// [V] full: Role_StoreCardToBox shows string 112 "<str#112>"
}

void rm8BoxAddReward(int cash, int points, const int *cards, int n)
{
	if(cash > 0) g_box.cash[0] += cash;
	if(points > 0) g_box.points[0] += points;
	for(int i = 0; i < n; i++) if(cards[i] > 0) addCard(0, cards[i]);
	rm8BoxSave();
}

bool rm8BoxMoveCash(){ int v = g_box.cash[0]; if(v <= 0) return false; g_box.cash[0] = 0; g_box.cash[1] = v; rm8BoxSave(); return true; }
bool rm8BoxMovePoints(){ int v = g_box.points[0]; if(v <= 0) return false; g_box.points[0] = 0; g_box.points[1] = v; rm8BoxSave(); return true; }

bool rm8BoxMoveCard(int from, int slot)
{
	if(from < 0 || from > 1 || slot < 0 || slot > 7) return false;
	int id = g_box.cards[from][slot];
	if(id <= 0 || !addCard(1 - from, id)) return false;
	g_box.cards[from][slot] = 0;
	rm8BoxSave();
	return true;
}

int rm8BoxMoveAll(int from)
{
	int n = 0;
	for(int i = 0; i < 8; i++){
		int id = g_box.cards[from][i];
		if(id > 0 && addCard(1 - from, id)){ g_box.cards[from][i] = 0; n++; }
	}
	if(n) rm8BoxSave();
	return n;
}

void rm8BoxMergePending()
{
	g_box.cash[0] += g_box.cash[1]; g_box.points[0] += g_box.points[1];	// [V] 0x4ac020
	for(int i = 0; i < 8; i++) if(g_box.cards[1][i] > 0) addCard(0, g_box.cards[1][i]);
	g_box.cash[1] = g_box.points[1] = 0; memset(g_box.cards[1], 0, sizeof g_box.cards[1]);	// [V] u_Obj_ResetSlotArrays @0x4abfd0
	rm8BoxSave();
}

void rm8BoxApplyToSeat(int seat)
{
	for(int i = 0; i < 8; i++) if(g_box.cards[1][i] > 0) rm8CardAdd(seat, g_box.cards[1][i]);
	if(g_box.cash[1] > 0) rm8GameAddMoney(seat, g_box.cash[1], false);	// [V] Role_SetMoney(0, carried + stage cash): the cash purse
	if(g_box.points[1] > 0) rm8GameAddPoints(seat, g_box.points[1]);
	g_box.cash[1] = g_box.points[1] = 0; memset(g_box.cards[1], 0, sizeof g_box.cards[1]);
	rm8BoxSave();
}
