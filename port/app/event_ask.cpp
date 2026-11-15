// Question point (event 2, "AskPoint"), after the task object Question_InitContainer @0x4b2ae0 (vtable 0x5E46F8; vf16 Cls_5e46f8__Init
// @0x4bd5f0 = Question_InitUI @0x4bcbe0 + Question_LoadResources @0x4bd1f0, Update @0x4bd610, answer vf12 @0x4bde90) and the computer path
// AI_Event_Question @0x4f7fa0. Data: Config\questions_normal.ini (201 questions, answer A is always right; shuffled on display),
// exported to scenes/questions.txt by scripts/export_questions.py.
// The reward levels 6 (a free level on an own plot / an own plot loses a level), 7 (all others lose a card / half of the own cards), 8 (collect from / pay everybody) and 9 (a free
// plot / an own empty plot is lost) with their picking probabilities (Question_PickRewardLevel @0x4bf0d0) are in pickLevel / applyEffect; the messages 0x38 0x39 0x29 0x2A are
// in events.cpp. Not drawn: the tick sound of the countdown.
#include "event_int.h"
#include "audio.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

namespace ev {

namespace {

struct Question { std::string q, a[4]; };	// a[0] is the right answer
std::vector<Question> g_questions;

// [V] Question_PickRewardLevel @0x4bf0d0 (the current role): the range is 100, 80 without cards (role +0x320 < 1); -15 unless the role owns a plot of level 0..4 and one of
// level 1..5 (Role_CountOwnTerras(9, 0, 4) / (9, 1, 5)); -5 unless a plot has no owner and the role owns an empty plot (TerraMgr_CountUnownedTerra, (9, 0, 0)).
// r = rand() % range: 0..14 level 0, 15..24 1, 25..29 2, 30..44 3, 45..49 4, then (with cards) 50..64 level 5 (a card), then 6 (15 wide, needs the plots), 7 (5 wide, needs
// cards), 8 (5 wide), 9 (5 wide, needs the plots); anything above: level 0
int pickLevel(int seat)
{
	if(const char *fl = getenv("RM8_ASK_LEVEL")) return atoi(fl);	// test hook: a fixed level
	int cards = rm8CardCount(seat), c1 = 0, c2 = 0, c3 = 0, unowned = 0;
	for(int t = 0; t < rm8GameTerraCount(); t++){
		int o = rm8GameTerraOwner(t), lv = rm8GameTerraLevel(t);
		if(o < 0) unowned++;
		if(o != seat) continue;
		if(lv >= 0 && lv <= 4) c1++;
		if(lv >= 1 && lv <= 5) c2++;
		if(lv == 0) c3++;
	}
	bool landA = c1 > 0 && c2 > 0, landB = unowned > 0 && c3 > 0;
	int range = cards < 1 ? 80 : 100;
	if(!landA) range -= 15;
	if(!landB) range -= 5;
	int r = rnd() % range;
	if(r < 15) return 0;
	if(r < 25) return 1;
	if(r < 30) return 2;
	if(r < 45) return 3;
	if(r < 50) return 4;
	int b = 50;
	if(cards >= 1){ if(r < 65) return 5; b = 65; }	// [V] a card reward / punishment needs a card
	if(landA){ b += 15; if(r < b) return 6; }
	if(cards >= 1){ b += 5; if(r < b) return 7; }
	if(r < b + 5) return 8;
	if(landB && r < b + 10) return 9;
	return 0;
}

// a random plot (CPlayerTag__FindRandomProperty @0x48da70 / TerraMgr_GetRandomTerra @0x4f1e60): -1 none
int randomPlot(int owner, int minLevel, int maxLevel)
{
	std::vector<int> v;
	for(int t = 0; t < rm8GameTerraCount() && v.size() < 128; t++)
		if(rm8GameTerraOwner(t) == owner && rm8GameTerraLevel(t) >= minLevel && rm8GameTerraLevel(t) <= maxLevel) v.push_back(t);
	return v.empty() ? -1 : v[rnd() % v.size()];
}

// [V] Question_ApplyReward @0x4bdef0 / Question_ApplyPunish @0x4be790: queued role messages
void applyEffect(int seat, int level, bool reward)
{
	switch(level){
	case 0: rm8TurnEnqueue(seat, reward ? 0x17 : 0x16, 500, 0); break;	// Role_AddMoney / Role_SubMoney (cash)
	case 1: rm8TurnEnqueue(seat, reward ? 0x17 : 0x16, 1000, 0); break;
	case 2: rm8TurnEnqueue(seat, reward ? 0x17 : 0x16, 3000, 0); break;
	case 3: rm8TurnEnqueue(seat, reward ? 7 : 8, 30, 0); break;		// tickets
	case 4: rm8TurnEnqueue(seat, reward ? 7 : 8, 50, 0); break;
	case 5: rm8TurnEnqueue(seat, reward ? 4 : 5, -1, 1); break;		// a card by difficulty / one random card removed
	case 6:
		if(reward){	// [V] 0xC(0, 1): Role_BuildOnLand(CPlayerTag__FindRandomProperty(9, 0, 4), 1): a free level on a random own plot of level 0..4 (if the role has one)
			int t = randomPlot(seat, 0, 4);
			if(t >= 0) rm8TurnEnqueue(seat, 0xC, t, 1);
		}else{		// [V] 0xD(0, 1): Role_StartLandLevelDown(FindRandomProperty(9, 1, 5), 1): a random own built plot loses a level
			int t = randomPlot(seat, 1, 5);
			if(t >= 0) rm8TurnEnqueue(seat, 0xD, t, 1);
		}
		break;
	case 7:
		if(reward){	// [V] every other role (status != 7, in turn order from the next one) that holds cards loses one: its queue gets "<name><str#3929>" (str 5614), 5(-1, 1) and the hand-off 0x2E to the next victim (the last one back to this role)
			std::vector<int> v; int n = rm8TurnRoleCount();
			for(int k = 1; k < n; k++){ int s = (seat + k) % n; if(rm8TurnStatus(s) != 7 && rm8CardCount(s) > 0) v.push_back(s); }
			if(!v.empty()){
				for(size_t k = 0; k < v.size(); k++){
					queueText(v[k], std::string(rm8TurnName(v[k])) + plain(rm8Str(5614)));
					rm8TurnEnqueue(v[k], 5, -1, 1);
					rm8TurnEnqueue(v[k], 0x2E, (k + 1 < v.size() ? v[k + 1] : seat) + 1, 0);
				}
				rm8TurnEnqueue(seat, 0x2E, v[0] + 1, 0);
			}
		}else{		// [V] 5(-1, count / 2 + count % 2): half of the own cards (rounded up)
			int c = rm8CardCount(seat);
			rm8TurnEnqueue(seat, 5, -1, c / 2 + (c & 1));
		}
		break;
	case 8:	// [V] 0x38(10000, 0): Role_CollectMoneyFromAllOthers - the code collects 10000 from every other role although the text says 1000 (str 4513) / 0x39(1000, 0): pays 1000 to each
		rm8TurnEnqueue(seat, reward ? 0x38 : 0x39, reward ? 10000 : 1000, 0); break;
	case 9:	// [V] 0x2A(0, 0): a random unowned plot becomes the role's (Role_StartEffectOnOtherTerra) / 0x29(0, 0): a random own empty plot loses its owner (Role_StartEffectOnOwnTerra)
		if(reward){ int t = randomPlot(-1, 0, 5); if(t >= 0) rm8TurnEnqueue(seat, 0x2A, t, 0); }
		else{ int t = randomPlot(seat, 0, 0); if(t >= 0) rm8TurnEnqueue(seat, 0x29, t, 0); }
		break;
	}
}

// [V] Question_InitUI @0x4bcbe0: texts string 4505 + level (reward) / 4516 + level (punish), "%s" = the role's name
std::string effectText(int seat, int level, bool reward)
{
	return plainFmt((reward ? 4505 : 4516) + level, rm8TurnName(seat));
}

struct AskTask : Task {
	float timer = 0;		// [V] +8
	int state = 0;			// [V] +0x34: 0 start, 1 curtain, 2, 10, 3 answering, 5 result
	float left = 5.0f;		// [V] +0x38
	int correct = -1;		// [V] +0x3C: widget ID of the right answer
	int level = 0;			// [V] +0xA048
	int voice = 0;
	Npc *npc = nullptr;		// [V] article 0x417 (event_npc.cpp)
	bool busy(){ return voice > 0 && rm8SndPlaying(voice); }
	// [V] vf13 Cls_5e46f8__Reset @0x4BF350: timer (+8) = 0, the voice (+0x24) stopped; always 1
	bool skip() override { timer = 0; if(voice){ rm8SndStop(voice); voice = 0; } return true; }
	void txt(const char *n, const std::string &s){ rm8UiSetTextByName(ui, n, s); }
	void bubble(int strId, bool playVoice = true)
	{
		int snd = 0; std::string t = plain(rm8Str(strId), &snd);
		txt("Bobo", wrap(t, 22));
		if(playVoice){ rm8SndStop(voice); voice = snd ? rm8SndPlay(snd) : 0; }
	}

	AskTask(int s)
	{
		seat = s;
		if(g_questions.empty()){ rm8Log("events: no questions loaded (scenes/questions.txt)"); return; }
		ui = rm8UiLoad("AskPoint", RM8UI_DIALOG);
		if(!ui) return;
		// [V] Question_InitUI + Question_LoadResources
		timer = 1.6f * waitScale();
		npc = npcCreate(0x417, ui, "NPC"); npcPlay(npc, 0);
		rm8UiSetVisibleN(ui, "RewardLight", false); rm8UiSetVisibleN(ui, "PunishLight", false);
		rm8UiSetVisibleN(ui, "mubu", true);
		bubble(4501, rnd() % 10 < 4);			// the welcome line is spoken in 4 of 10 cases
		const Question &q = g_questions[rnd() % g_questions.size()];	// [V] Question_DisplayQuestion: rand() % count
		txt("Question", wrap(q.q, 18));
		int pos[4], used = 0;				// [V] Question_ShuffleAnswers @0x4bd960: pos[k] = widget index of answer k (A..D), all distinct
		for(int k = 0; k < 4; k++){
			int p;
			do p = rnd() % 4; while(used & (1 << p));
			used |= 1 << p; pos[k] = p;
		}
		correct = pos[0] + 1;
		for(int k = 0; k < 4; k++){ char b[16]; snprintf(b, sizeof b, "Answer%d", pos[k]); txt(b, q.a[k]); rm8UiSetAlignN(ui, b, 1, 1); }	// [A] centred (the UI library defaults buttons to left)
		level = pickLevel(seat);			// [V] Question_DisplayRewardPunish @0x4bdc30
		txt("Reward", plainFmt(6003, effectText(seat, level, true).c_str()));
		txt("Punish", plainFmt(6004, effectText(seat, level, false).c_str()));
		uiTapOnly(ui, {"Answer0", "Answer1", "Answer2", "Answer3"});
		rm8UiOnClick(ui, [this](int id){ answer(id); });
		rm8UiShow(ui, true);
	}
	~AskTask() override { npcDestroy(npc); if(voice) rm8SndStop(voice); rm8UiShow(ui, false); rm8UiOnClick(ui, nullptr); }
	bool ok() const { return ui != nullptr; }
	bool ready() const override { return state == 3; }

	void result(bool right)		// [V] Question_OnCorrect @0x4becd0 / Question_OnWrong @0x4bedf0
	{
		rm8SndPlay(right ? 0x142 : 0x140, 1, 0.3f);
		rm8UiSetVisibleN(ui, right ? "RewardLight" : "PunishLight", true);
		bubble(right ? 4503 : 4504); npcPlay(npc, right ? 2 : 3);
		applyEffect(seat, level, right);
		if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: seat %d answered %s: level %d", seat, right ? "right" : "wrong", level);
	}

	void answer(int id)
	{
		if(state != 3 || id < 1 || id > 4) return;
		state = 4;
		result(id == correct);
		state = 5; timer = 4.9f * waitScale();	// [V] Anim_ApplyDifficultyScale(0x409ccccd = 4.9)
	}
	void click(const std::string &n) override { int i; if(sscanf(n.c_str(), "Answer%d", &i) == 1) answer(i + 1); }

	bool update(float dt) override
	{
		if(state == 3){
			left -= dt / 1.5f;			// [V] +0x38 -= dt / 1.5: the five seconds last 7.5 s of game time
			int pic = left > 4 ? 0x37 : left > 3 ? 0x36 : left > 2 ? 0x35 : left > 1 ? 0x34 : left > 0 ? 0x33 : 0x3C;	// [V] Question_UpdateTimer @0x4bef10
			rm8UiSetPicN(ui, "Time", pic);
			if(left < 0){ state = 4; result(false); state = 5; timer = 6.9f * waitScale(); }	// [V] time up = wrong (scale 0x40dccccd = 6.9)
			return false;
		}
		timer -= dt;
		if(timer <= 0 && !busy()){
			timer = 1.6f * waitScale();
			if(state == 0){ state = 1; bubble(4502); npcPlay(npc, 1); }
			else if(state == 1){ txt("Bobo", "..."); rm8UiSetPicN(ui, "mubu", 0x306); timer = 0.1f * waitScale(); state = 2; }	// [V] the curtain opens in two steps
			else if(state == 2){ rm8UiSetPicN(ui, "mubu", 0x307); timer = 0.1f * waitScale(); state = 10; }
			else if(state == 10){ state = 3; rm8UiSetVisibleN(ui, "mubu", false); }
			else if(state == 5) return true;
		}
		return false;
	}
};

}	// namespace

bool loadQuestions(const std::string &file)
{
	std::ifstream f(file);
	if(!f){ rm8Log("events: no %s (run scripts/export_questions.py)", file.c_str()); return false; }
	g_questions.clear();
	std::string line;
	while(std::getline(f, line)){
		if(!line.empty() && line.back() == '\r') line.pop_back();
		Question q; std::stringstream ss(line);
		if(!std::getline(ss, q.q, '\t')) continue;
		bool ok = true;
		for(int k = 0; k < 4; k++) ok &= (bool)std::getline(ss, q.a[k], '\t');
		if(ok) g_questions.push_back(q);
	}
	rm8Log("events: %d questions", (int)g_questions.size());
	return !g_questions.empty();
}

Task *makeAskTask(int seat)
{
	AskTask *t = new AskTask(seat);
	if(!t->ok()){ delete t; return nullptr; }	// [V] a loader that fails deletes the task
	return t;
}

// [V] AI_Event_Question @0x4f7fa0: level by Question_PickRewardLevel, Python Answer(0) != 0 = right; the reward / punishment messages are
// queued and the info text "<str#264> / <str#265> <reward text>" (strings 264 / 265) goes to the front of the queue; always returns 1
bool aiQuestion(int seat)
{
	int level = pickLevel(seat);
	long r = 0;
	rm8PyCallAI(rm8TurnAiScript(seat), "Answer", 0, r);
	bool right = r != 0;
	applyEffect(seat, level, right);
	queueTextAt(seat, 0, plainFmt(right ? 264 : 265, effectText(seat, level, right).c_str()));
	if(getenv("RM8_DEBUG_EVENTS")) rm8Log("events: seat %d (computer) answers %s: level %d", seat, right ? "right" : "wrong", level);
	return true;
}

}	// namespace ev
