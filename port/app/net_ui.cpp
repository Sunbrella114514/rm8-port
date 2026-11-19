// [N] Netplay screens (net.h, docs/netplay.md section 5):
//   * the 联机 page (host / join / resume a networked save / player name), the address and name input (keyboard + on-screen keypad; SDL text input brings
//     up the soft keyboard on Android);
//   * the ROOM is the original map menu / role menu (menu.cpp, host editable, clients mirrored); this file only adds a small overlay in the original's dialog
//     style on top of them: "玩家准备 x/y", who is in the room, the host address, the players' names under the seat tags, the preset chat, and the
//     "玩家准备不全，是否强制开启？" confirmation (the exit.ui look: popo frame + OK / CANCEL pictures);
//   * on the board: the netplay status line, the chat button, and the "等待玩家中…" panel of a resumed networked save (seats, who is back, 开始).
// Runtime widgets on canvases (ui.cpp), original pictures only (uvlist 790 frame, 786..789 OK / CANCEL), no new art.
#include "net.h"
#include "ui.h"
#include "turn.h"
#include "menu.h"
#include "events.h"
#include "minigame.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#if __has_include(<SDL.h>)
#include <SDL.h>
#define RM8_NETUI_SDL 1
#endif

using namespace rw;

namespace {

enum Page { P_NONE, P_CHOICE, P_JOIN, P_NAME };
struct Ui {
	Rm8Ui *u = nullptr;
	Page page = P_NONE;
	int bg = 0, title = 0, status = 0;
	// choice
	int bHost = 0, bJoin = 0, bResume = 0, bName = 0, bBack = 0, info = 0;
	// join / name
	int jLabel = 0, jEdit = 0, jKeys[16] = {0}, bConnect = 0, bBack2 = 0, jHint = 0;
	std::string addr, name;
	std::string notice; float noticeT = 0;
	bool textInput = false;
} U;
const char *KEYS[16] = {"1", "2", "3", "删除", "4", "5", "6", "清空", "7", "8", "9", "键盘", ".", "0", ":", ""};
const char *PHRASES[6] = {"你好！", "准备好了", "等一下", "快开始吧", "好棋！", "再见"};

int addW(Rm8Ui *u, const char *type, float x, float y, float w, float h, unsigned bc, int fs, int idx = 9, int pic = -1)
{
	int id = rm8UiAddWidget(u, type, 0, x, y, w, h, pic, bc, fs, idx);
	rm8UiSetAlignN(u, ("dyn" + std::to_string(id)).c_str(), 1, 1);
	return id;
}
int add(const char *type, float x, float y, float w, float h, unsigned bc, int fs, int idx = 9){ return addW(U.u, type, x, y, w, h, bc, fs, idx); }
int button(float x, float y, float w, float h, const char *text, int fs = 28, int idx = 10)
{
	int id = add("button", x, y, w, h, 0xF0405478, fs, idx);
	rm8UiSetText(U.u, id, text); rm8UiSetColor(U.u, id, 0xFFFFF4D0);
	return id;
}
int labelW(Rm8Ui *u, float x, float y, float w, float h, int fs, unsigned col = 0xFFFFFFFF, unsigned bc = 0, int alignH = 1, int idx = 10)
{
	int id = addW(u, "label", x, y, w, h, bc, fs, idx);
	rm8UiSetColor(u, id, col);
	if(alignH != 1) rm8UiSetAlignN(u, ("dyn" + std::to_string(id)).c_str(), alignH, 1);
	rm8UiSetIgnore(u, id, true);
	return id;
}
int label(float x, float y, float w, float h, int fs, unsigned col = 0xFFFFFFFF, unsigned bc = 0, int alignH = 1, int idx = 10){ return labelW(U.u, x, y, w, h, fs, col, bc, alignH, idx); }
void showIds(Rm8Ui *u, std::initializer_list<int> ids, bool v){ for(int id : ids) rm8UiSetVisible(u, id, v); }
void showIds(std::initializer_list<int> ids, bool v){ showIds(U.u, ids, v); }

void setTextInput(bool on)
{
#ifdef RM8_NETUI_SDL
	if(on == U.textInput) return;
	U.textInput = on;
	if(on){
		float x, y, w, h;
		if(rm8UiGetScreenRect(U.u, U.jEdit, x, y, w, h)){ SDL_Rect r = {(int)x, (int)y, (int)w, (int)h}; SDL_SetTextInputRect(&r); }
		SDL_StartTextInput();
	}else SDL_StopTextInput();
#else
	(void)on;
#endif
}

void onClick(int id);
void build()
{
	if(U.u) return;
	U.u = rm8UiCreateCanvas("netlobby", RM8UI_DIALOG);
	if(!U.u) return;
	U.bg = add("plane", 0, 0, 1024, 768, 0xF0101828, 16, 1); rm8UiSetIgnore(U.u, U.bg, true);
	U.title = label(0, 34, 1024, 64, 44, 0xFFFFD45A);
	U.status = label(40, 694, 944, 40, 22, 0xFFFFE6A0);
	// choice
	U.bHost = button(312, 130, 400, 78, "建立主机", 34);
	U.bJoin = button(312, 222, 400, 78, "加入游戏", 34);
	U.bResume = button(312, 314, 400, 66, "读取联机存档", 28);
	U.bName = button(312, 394, 400, 56, "", 22);
	U.bBack = button(312, 464, 400, 60, "返回", 28);
	U.info = label(70, 534, 884, 150, 19, 0xFFC8D4E8);
	// join / name
	U.jLabel = label(112, 120, 800, 40, 26, 0xFFFFFFFF, 0, 0);
	U.jEdit = add("button", 112, 166, 800, 66, 0xFFF4F0E4, 34, 10); rm8UiSetColor(U.u, U.jEdit, 0xFF202020);
	for(int k = 0; k < 15; k++){
		float x = 232 + (k % 4) * 145, y = 252 + (k / 4) * 74;
		U.jKeys[k] = button(x, y, 135, 64, KEYS[k], k % 4 == 3 ? 24 : 30);
	}
	U.jHint = label(112, 552, 800, 36, 18, 0xFFC8D4E8);
	U.bConnect = button(292, 600, 200, 72, "连接", 30);
	U.bBack2 = button(532, 600, 200, 72, "返回", 30);
	rm8UiOnClick(U.u, onClick);
}

void refreshJoin()
{
	if(U.page == P_NAME){
		rm8UiSetText(U.u, U.jEdit, U.name);
		rm8UiSetText(U.u, U.jHint, "最多 12 个字。留空 = 默认名（" + std::string(rm8NetPlayerId() ? "玩家 + 本机编号" : "") + "）。名字保存在 rm8_settings.txt 的 net_name");
		return;
	}
	rm8UiSetText(U.u, U.jEdit, U.addr.empty() ? std::string() : U.addr);
	std::string st = rm8NetJoinState();
	rm8UiSetText(U.u, U.jHint, st.empty() ? "例：192.168.1.5:47000（局域网）或 frp 映射后的 域名:端口" : st);
}

void setPage(Page p)
{
	build();
	if(!U.u) return;
	U.page = p;
	rm8UiShow(U.u, p != P_NONE);
	showIds({U.bHost, U.bJoin, U.bResume, U.bName, U.bBack, U.info}, p == P_CHOICE);
	showIds({U.jLabel, U.jEdit, U.jHint, U.bConnect, U.bBack2}, p == P_JOIN || p == P_NAME);
	for(int k = 0; k < 15; k++) rm8UiSetVisible(U.u, U.jKeys[k], p == P_JOIN);
	setTextInput(p == P_JOIN || p == P_NAME);
	if(p == P_CHOICE){
		rm8UiSetText(U.u, U.title, "联机对战");
		rm8UiSetText(U.u, U.bName, "玩家名：" + rm8NetMyName() + "（点这里修改）");
		char b[900];
		snprintf(b, sizeof b, "建立主机：在原版的地图 / 角色菜单里设置游戏，角色菜单里点 PLAYER 标签可切换“本机玩家 → 等待网络玩家（绿色）→ 电脑”，加入的玩家自动坐进等待的座位；大家准备好后按 O 开始。\\"
		                      "读取联机存档：继续一局主机存过的联机游戏，原来的玩家重新加入后回到自己的座位。\\"
		                      "本机监听端口 %d（rm8_settings.txt 的 net_port）。局域网直接连；外网请用 Sakura FRP 等工具把这个 TCP 端口映射出去。电脑版和安卓版可以互相联机，但要用同一份源码构建、同一版本的游戏数据。", rm8NetPort());
		rm8UiSetText(U.u, U.info, rm8UiWrapText(b, 870.0f, 19));
	}else if(p == P_JOIN){
		rm8UiSetText(U.u, U.title, "加入游戏");
		rm8UiSetText(U.u, U.jLabel, "主机地址（IP:端口）");
		rm8UiSetText(U.u, U.bConnect, "连接");
		if(U.addr.empty()) U.addr = rm8NetLastAddr();
		refreshJoin();
	}else if(p == P_NAME){
		rm8UiSetText(U.u, U.title, "玩家名");
		rm8UiSetText(U.u, U.jLabel, "其他玩家看到的名字（存档里也记这个名字）");
		rm8UiSetText(U.u, U.bConnect, "确定");
		U.name = rm8MenuOptions().netName;
		refreshJoin();
	}
}

void keypad(int k)
{
	const char *t = KEYS[k];
	std::string &s = U.page == P_NAME ? U.name : U.addr;
	if(!strcmp(t, "删除")){ while(!s.empty() && (s.back() & 0xC0) == 0x80) s.pop_back(); if(!s.empty()) s.pop_back(); }
	else if(!strcmp(t, "清空")) s.clear();
	else if(!strcmp(t, "键盘")){
#ifdef RM8_NETUI_SDL
		U.textInput = false; setTextInput(true);
#endif
	}
	else if(s.size() < 120) s += t;
	refreshJoin();
}
size_t utf8Len(const std::string &s){ size_t n = 0; for(unsigned char c : s) if((c & 0xC0) != 0x80) n++; return n; }
void nameAppend(const char *t){ for(const char *c = t; *c; c++){ if((unsigned char)*c < 0x20) continue; if((((unsigned char)*c & 0xC0) != 0x80) && utf8Len(U.name) >= 12) break; U.name += *c; } }
void nameDone(){ std::string n = U.name; while(!n.empty() && n.back() == ' ') n.pop_back(); rm8MenuSetNetName(n); rm8NetUiNotice("玩家名：" + rm8NetMyName(), 3); setPage(P_CHOICE); }

void onClick(int id)
{
	if(U.page == P_CHOICE){
		if(id == U.bHost){
			std::string err;
			if(rm8NetHostListen(rm8NetPort(), err)){ setPage(P_NONE); if(rm8NetAppHooks().hostSetup) rm8NetAppHooks().hostSetup(); }
			else rm8NetUiNotice("无法建立主机：" + err, 6);
		}else if(id == U.bJoin) setPage(P_JOIN);
		else if(id == U.bResume){ setPage(P_NONE); if(rm8NetAppHooks().hostLoad) rm8NetAppHooks().hostLoad(); }
		else if(id == U.bName) setPage(P_NAME);
		else if(id == U.bBack){ setPage(P_NONE); if(rm8NetAppHooks().backToMenu) rm8NetAppHooks().backToMenu(); }
	}else if(U.page == P_JOIN){
		for(int k = 0; k < 15; k++) if(id == U.jKeys[k]){ keypad(k); return; }
		if(id == U.bConnect){ rm8NetJoin(U.addr); refreshJoin(); }
		else if(id == U.bBack2){ rm8NetLeave("取消"); setPage(P_CHOICE); }
		else if(id == U.jEdit){ U.textInput = false; setTextInput(true); }
	}else if(U.page == P_NAME){
		if(id == U.bConnect) nameDone();
		else if(id == U.bBack2) setPage(P_CHOICE);
		else if(id == U.jEdit){ U.textInput = false; setTextInput(true); }
	}
}

// ======================================================================== the room overlay over the original menus
struct Room {
	Rm8Ui *u = nullptr;
	int panel = 0, title = 0, lines = 0, addr = 0, bChat = 0, seatName[4] = {0, 0, 0, 0}, note = 0;
	int phr[6] = {0, 0, 0, 0, 0, 0};
	int cFrame = 0, cText = 0, cOk = 0, cCancel = 0;
	bool chat = false, confirm = false;
	int startReq = 0;	// 1 start, 2 force start: done in the next tick (not inside a click handler of the menus that the start closes)
	int screen = -1;
	float autoT = 0; bool autoReadySent = false;
} R;
const float SEAT_TAG_X[4] = {56, 198, 340, 487}, SEAT_TAG_Y[4] = {636, 696, 636, 696};	// chsRoleMenu.ui tags 25..32 (menu.cpp SEAT_X / SEAT_Y)
void roomClick(int id);
void roomBuild()
{
	if(R.u) return;
	R.u = rm8UiCreateCanvas("netroom", RM8UI_DIALOG); rm8UiSetLate(R.u, true);	// [N] drawn after the role menu's 3D seat dolls (the 4th doll covered the address line)	// the menus' layout (a centred 4:3 box): the overlay sits on the same spots of the original screens
	if(!R.u) return;
	rm8UiSetIgnoreN(R.u, "root", true);	// only the overlay's own widgets take the pointer
	R.panel = addW(R.u, "plane", 0, 0, 300, 150, 0, 16, 5, 790); rm8UiSetIgnore(R.u, R.panel, true);	// [V] the popo dialog frame of okcancel.ui / exit.ui (uvlist 790)
	R.title = labelW(R.u, 0, 0, 280, 24, 17, 0xFF8A2A10, 0, 1, 6);
	R.lines = labelW(R.u, 0, 0, 280, 80, 14, 0xFF202020, 0, 0, 6);
	R.addr = labelW(R.u, 0, 0, 280, 18, 13, 0xFF1E5A1E, 0, 1, 6);
	R.bChat = addW(R.u, "button", 0, 0, 64, 26, 0xE0C86428, 14, 7); rm8UiSetText(R.u, R.bChat, "聊天"); rm8UiSetColor(R.u, R.bChat, 0xFFFFFFFF);
	R.note = labelW(R.u, 150, 2, 724, 26, 16, 0xFFFFFFFF, 0xB0602010, 1, 8);
	for(int k = 0; k < 6; k++){ R.phr[k] = addW(R.u, "button", 0, 0, 96, 28, 0xF0FFF0D8, 15, 8); rm8UiSetText(R.u, R.phr[k], PHRASES[k]); rm8UiSetColor(R.u, R.phr[k], 0xFF602010); }
	for(int k = 0; k < 4; k++) R.seatName[k] = labelW(R.u, SEAT_TAG_X[k] - 20, SEAT_TAG_Y[k] + 32, 130, 22, 14, 0xFFFFFFFF, 0xA0602818, 1, 6);
	// [V] the exit.ui layout (popo frame 257 x 128, the text at (22, 14) 213 x 72, OK (786 / 787) at (19, 86), CANCEL (788 / 789) at (175, 86)), [A] a little larger, over the
	// description text of the role menu (the original's place, 395 / 440, is under the 3D seat dolls there)
	R.cFrame = addW(R.u, "plane", 452, 112, 300, 150, 0, 16, 20, 790); rm8UiSetIgnore(R.u, R.cFrame, true);
	R.cText = labelW(R.u, 474, 126, 256, 82, 16, 0xFF202020, 0, 1, 21);
	R.cOk = addW(R.u, "button", 474, 218, 64, 32, 0, 16, 21, 786);
	R.cCancel = addW(R.u, "button", 666, 218, 63, 32, 0, 16, 21, 788);
	rm8UiOnClick(R.u, roomClick);
}
void roomStartAsk(){ R.startReq = 1; }	// [N] "玩家准备不全，是否强制开启?" - a second confirmation starts with the ready players
void roomClick(int id)
{
	if(R.confirm){
		if(id == R.cOk){ R.confirm = false; R.startReq = 2; }
		else if(id == R.cCancel) R.confirm = false;
		return;
	}
	if(id == R.bChat){ R.chat = !R.chat; return; }
	for(int k = 0; k < 6; k++) if(id == R.phr[k]){ rm8NetChat(PHRASES[k]); R.chat = false; return; }
}
std::string stateText(const Rm8NetRoomSeat &s){ return s.kind == 0 ? "" : !s.connected ? "" : s.ready ? "已准备" : "未准备"; }
void roomRefresh()
{
	Rm8NetRoomView v = rm8NetRoomView();
	int screen = v.screen;
	// the panel: map menu = below the description (the orange band above the world map), role menu = right of the dolls, below the outfit row
	float px = screen == 2 ? 460 : 492, py = screen == 2 ? 384 : 290;
	if(screen != R.screen){
		R.screen = screen;
		rm8UiSetRect(R.u, R.panel, px, py, 300, 150);
		rm8UiSetRect(R.u, R.title, px + 10, py + 8, 280, 22);
		rm8UiSetRect(R.u, R.lines, px + 18, py + 32, 268, 82);
		rm8UiSetRect(R.u, R.addr, px + 10, py + 118, 210, 20);
		rm8UiSetRect(R.u, R.bChat, px + 226, py + 116, 60, 24);
		for(int k = 0; k < 6; k++) rm8UiSetRect(R.u, R.phr[k], px + (k % 3) * 100, py - 64 + (k / 3) * 31, 96, 28);
	}
	char t[160];
	if(v.host || v.mySeat < 0) snprintf(t, sizeof t, "联机房间　玩家准备 %d/%d", v.ready, v.need);
	else snprintf(t, sizeof t, "联机房间　%s（按 O %s）", v.meReady ? "你已准备" : "你还没准备", v.meReady ? "取消" : "准备");
	rm8UiSetText(R.u, R.title, t);
	std::string ls;
	int shown = 0;
	for(const Rm8NetRoomPeer &p : v.peers){
		if(shown++ >= 5) break;
		std::string line = p.name + (p.me ? "（你）" : "") + "　";
		if(p.id == 0) line += "主机";
		else if(p.seat >= 0) line += std::to_string(p.seat + 1) + "P " + (p.ready ? "已准备" : "未准备");
		else line += "观战（没有空座位）";
		ls += (ls.empty() ? "" : "\\") + line;
	}
	if(v.need > v.ready && v.host){ int open = 0; for(const Rm8NetRoomSeat &s : v.seats) if(s.kind == 1 && s.owner < 0) open++; if(open) ls += "\\" + std::to_string(open) + " 个座位等待玩家加入…"; }
	rm8UiSetText(R.u, R.lines, ls);
	rm8UiSetText(R.u, R.addr, v.host ? "本机 " + v.addr : std::string());
	for(int k = 0; k < 4; k++){
		bool on = screen == 2 && k < (int)v.seats.size() && v.seats[k].kind != 2;
		rm8UiSetVisible(R.u, R.seatName[k], on);
		if(!on) continue;
		const Rm8NetRoomSeat &s = v.seats[k];
		std::string n = s.kind == 0 ? (s.name.empty() ? "主机" : s.name) + (s.me ? "（你）" : "") : s.owner < 0 ? "等待玩家…" : s.name + (s.me ? "（你）" : "");
		std::string st = stateText(s);
		rm8UiSetText(R.u, R.seatName[k], st.empty() ? n : n + " " + st);
		rm8UiSetBColor(R.u, R.seatName[k], s.kind == 1 ? (s.owner >= 0 && s.ready ? 0xC0207020 : 0xA0206040) : 0xA0602818);
	}
	showIds(R.u, {R.panel, R.title, R.lines, R.addr, R.bChat}, screen != 0);
	for(int k = 0; k < 6; k++) rm8UiSetVisible(R.u, R.phr[k], R.chat && screen != 0);
	showIds(R.u, {R.cFrame, R.cText, R.cOk, R.cCancel}, R.confirm);
	if(R.confirm) rm8UiSetText(R.u, R.cText, rm8UiWrapText("玩家准备不全，是否强制开启？", 250, 16) + "\\" + rm8UiWrapText("（没准备好的座位由电脑接管）", 250, 16));
	rm8UiSetText(R.u, R.note, U.noticeT > 0 ? U.notice : std::string());
	rm8UiSetVisible(R.u, R.note, U.noticeT > 0 && !U.notice.empty());
}

// ======================================================================== the in-game line, chat button and the waiting panel
struct Hud { Rm8Ui *u = nullptr; int line = 0, note = 0, bChat = 0, phr[6] = {0, 0, 0, 0, 0, 0}; bool chat = false; } H;
struct Wait {
	Rm8Ui *u = nullptr;
	int frame = 0, title = 0, hint = 0, pic[4] = {0, 0, 0, 0}, text[4] = {0, 0, 0, 0}, tog[4] = {0, 0, 0, 0}, bStart = 0;
	int cFrame = 0, cText = 0, cOk = 0, cCancel = 0; bool confirm = false;
} WT;
void hudClick(int id)
{
	if(id == H.bChat){ H.chat = !H.chat; return; }
	for(int k = 0; k < 6; k++) if(id == H.phr[k]){ rm8NetChat(PHRASES[k]); H.chat = false; return; }
}
void waitClick(int id)
{
	if(WT.confirm){
		if(id == WT.cOk){ WT.confirm = false; rm8NetWaitStart(true); }
		else if(id == WT.cCancel) WT.confirm = false;
		return;
	}
	for(int k = 0; k < 4; k++) if(id == WT.tog[k]){ rm8NetWaitToggle(k); return; }
	if(id == WT.bStart){ if(!rm8NetWaitStart(false)) WT.confirm = true; }
}
std::string rolePic(int id){ std::string n, p; if(rm8MenuRoleInfo(id, n, p)) return p; return std::string(); }
std::string roleNm(int id){ std::string n, p; if(rm8MenuRoleInfo(id, n, p)) return n; return "角色" + std::to_string(id); }
void waitBuild()
{
	if(WT.u) return;
	WT.u = rm8UiCreateCanvas("netwait", RM8UI_DIALOG);
	if(!WT.u) return;
	rm8UiSetIgnoreN(WT.u, "root", true);
	WT.frame = addW(WT.u, "plane", 222, 150, 580, 400, 0, 16, 5, 790); rm8UiSetIgnore(WT.u, WT.frame, true);
	WT.title = labelW(WT.u, 242, 168, 540, 40, 28, 0xFF8A2A10, 0, 1, 6);
	WT.hint = labelW(WT.u, 252, 206, 520, 26, 15, 0xFF404040, 0, 1, 6);
	for(int k = 0; k < 4; k++){
		float y = 238 + k * 66;
		WT.pic[k] = addW(WT.u, "plane", 262, y, 58, 58, 0, 16, 6); rm8UiSetIgnore(WT.u, WT.pic[k], true);
		WT.text[k] = labelW(WT.u, 330, y + 4, 320, 52, 17, 0xFF202020, 0, 0, 6);
		WT.tog[k] = addW(WT.u, "button", 652, y + 13, 120, 32, 0xE0C86428, 15, 7); rm8UiSetColor(WT.u, WT.tog[k], 0xFFFFFFFF);
	}
	WT.bStart = addW(WT.u, "button", 432, 504, 160, 36, 0xE0C86428, 20, 7); rm8UiSetText(WT.u, WT.bStart, "开始游戏"); rm8UiSetColor(WT.u, WT.bStart, 0xFFFFFFFF);
	WT.cFrame = addW(WT.u, "plane", 371, 360, 281, 150, 0, 16, 20, 790); rm8UiSetIgnore(WT.u, WT.cFrame, true);
	WT.cText = labelW(WT.u, 391, 372, 241, 86, 15, 0xFF202020, 0, 1, 21);
	WT.cOk = addW(WT.u, "button", 392, 466, 64, 32, 0, 16, 21, 786);
	WT.cCancel = addW(WT.u, "button", 566, 466, 63, 32, 0, 16, 21, 788);
	rm8UiOnClick(WT.u, waitClick);
}
void waitRender()
{
	Rm8NetWaitView v = rm8NetWaitView();
	if(!v.on){ if(WT.u) rm8UiShow(WT.u, false); WT.confirm = false; return; }
	waitBuild();
	if(!WT.u) return;
	rm8UiSetText(WT.u, WT.title, "等待玩家中…");
	rm8UiSetText(WT.u, WT.hint, v.host ? (v.missing ? "还有 " + std::to_string(v.missing) + " 个座位没人。可以等他们，也可以先开始（由电脑托管，回来后交还）" : "所有座位都有人了，可以开始") : "原来的玩家会自动回到自己的座位，主机按开始后继续");
	for(int k = 0; k < 4; k++){
		bool on = k < (int)v.rows.size();
		showIds(WT.u, {WT.pic[k], WT.text[k]}, on);
		rm8UiSetVisible(WT.u, WT.tog[k], on && v.host && v.rows[k].toggle);
		if(!on) continue;
		const Rm8NetWaitRow &r = v.rows[k];
		std::string pic = rolePic(r.charId);
		rm8UiSetPicFile(WT.u, WT.pic[k], pic.empty() ? "" : "prt\\npc\\" + pic + "_12.tga");
		std::string s;
		switch(r.state){
		case RM8W_LOCAL: s = r.name + "（主机本机）"; break;
		case RM8W_SAVED: s = r.present ? r.name + " 已回来" : "等待 " + r.name + " 回来…"; break;
		case RM8W_OPEN: s = r.owner >= 0 ? r.name + (r.present ? " 已加入" : " 正在载入…") : "空座位（新玩家加入即坐这里）"; break;
		case RM8W_COMPUTER: s = "电脑"; break;
		case RM8W_COMPUTER_OPEN: s = r.owner >= 0 ? r.name + (r.present ? " 已加入（接手电脑座位）" : " 正在载入…") : "电脑座位，开放给新玩家"; break;
		}
		rm8UiSetText(WT.u, WT.text[k], std::to_string(k + 1) + "P " + roleNm(r.charId) + "\\" + s);
		if(v.host && r.toggle) rm8UiSetText(WT.u, WT.tog[k], r.state == RM8W_LOCAL ? "让给别人" : r.state == RM8W_COMPUTER ? "开放给玩家" : r.state == RM8W_COMPUTER_OPEN ? "保持电脑" : "由本机来玩");
	}
	rm8UiSetVisible(WT.u, WT.bStart, v.host && !WT.confirm);
	showIds(WT.u, {WT.cFrame, WT.cText, WT.cOk, WT.cCancel}, WT.confirm);
	if(WT.confirm) rm8UiSetText(WT.u, WT.cText, rm8UiWrapText("还有玩家没回来，是否先开始？（他们的座位由电脑托管，回来后交还）", 235, 15));
	rm8UiShow(WT.u, true);
}

// ---- [N] N4: "XXX 正在玩小游戏…" over the board while a minigame runs on its player's device (the popo frame of the original dialogs, the player's portrait)
struct MiniWait { Rm8Ui *u = nullptr; int frame = 0, pic = 0, title = 0, text = 0, hint = 0; } MW;
// the minigame's name: cut out of its reward text ("%s玩<name>小遊戲，獲得%d點券", string.bin 5314 + n, as infoscreens.cpp does for events 18 / 19); a short fallback
std::string miniName(int game)
{
	static const char *fb[6] = {u8"快手小廚師", u8"記憶挖寶", u8"打地鼠", u8"插海盜", u8"分裂球", u8"踩蛋"};
	if(game < 18 || game > 23) return u8"小游戏";
	int sid = game + 0x14b0; if(sid > 0x14c5) sid++;
	const std::string &s = rm8Str(sid);
	size_t a = s.find(u8"玩");
	if(a != std::string::npos){
		a += strlen(u8"玩");
		size_t b = std::min(s.find(u8"小遊", a), s.find(u8"小游", a));
		if(b != std::string::npos && b > a && b - a <= 24) return s.substr(a, b - a);
	}
	return fb[game - 18];
}
void miniRender()
{
	Rm8MiniWaitInfo mi;
	bool on = rm8EventMiniWaiting(&mi) && !mi.playing;
	if(!on){ if(MW.u) rm8UiShow(MW.u, false); return; }
	if(!MW.u){
		MW.u = rm8UiCreateCanvas("netmini", RM8UI_DIALOG);
		if(!MW.u) return;
		MW.frame = addW(MW.u, "plane", 252, 262, 520, 210, 0, 16, 5, 790);	// [V] the popo dialog frame (uvlist 790)
		MW.pic = addW(MW.u, "plane", 282, 300, 96, 96, 0, 16, 6);
		MW.title = labelW(MW.u, 392, 286, 360, 40, 26, 0xFF8A2A10, 0, 0, 6);
		MW.text = labelW(MW.u, 392, 330, 360, 70, 19, 0xFF202020, 0, 0, 6);
		MW.hint = labelW(MW.u, 282, 412, 470, 44, 15, 0xFF404040, 0, 1, 6);
		rm8UiSetIgnoreAll(MW.u, true);	// a notice only: taps go to the board (the chat button, the view)
	}
	int ps = mi.group && mi.turnSeat >= 0 && !mi.finished ? mi.turnSeat : mi.seat;	// 插海盗 with everybody: whose turn it is
	std::string pic = rolePic(rm8TurnCharId(ps));
	rm8UiSetPicFile(MW.u, MW.pic, pic.empty() ? "" : "prt\\npc\\" + pic + "_12.tga");
	rm8UiSetText(MW.u, MW.title, u8"小游戏「" + miniName(mi.game) + u8"」");
	std::string who = rm8NetSeatName(ps);
	int o = rm8NetSeatOwner(ps);
	if(mi.group){	// this device does not show the game (it ended here / it came back in the middle of it): the turns go on, the result comes for everybody at once
		if(mi.finished) rm8UiSetText(MW.u, MW.text, rm8UiWrapText(u8"插海盗结束，等待其他玩家看完结果…", 360, 19));
		else if(!mi.turnHuman) rm8UiSetText(MW.u, MW.text, rm8UiWrapText(u8"第 " + std::to_string(mi.turn + 1) + u8" 剑：电脑 " + who + u8" 插剑", 360, 19));
		else if(o == rm8NetLocalPeer()) rm8UiSetText(MW.u, MW.text, rm8UiWrapText(u8"第 " + std::to_string(mi.turn + 1) + u8" 剑：轮到你，由电脑代插", 360, 19));
		else rm8UiSetText(MW.u, MW.text, rm8UiWrapText(u8"第 " + std::to_string(mi.turn + 1) + u8" 剑：等待 " + who + u8" 插剑…", 360, 19));
		rm8UiSetText(MW.u, MW.hint, u8"大家一起玩插海盗　结果出来后所有人同时继续");
		rm8UiShow(MW.u, true);
		return;
	}
	if(mi.local) rm8UiSetText(MW.u, MW.text, rm8UiWrapText(u8"小游戏结束，正在同步结果…", 360, 19));
	else if(o < 0 || !rm8NetPeerConnected(o)) rm8UiSetText(MW.u, MW.text, rm8UiWrapText(who + u8" 断线了，改由电脑决定小游戏结果…", 360, 19));
	else rm8UiSetText(MW.u, MW.text, rm8UiWrapText(who + u8" 正在玩小游戏…", 360, 19));
	rm8UiSetText(MW.u, MW.hint, u8"已等待 " + std::to_string(mi.frames / 60) + u8" 秒　结果出来后所有人同时继续");
	rm8UiShow(MW.u, true);
}
struct MiniShot { bool init = false; std::string prefix; float at = 10; unsigned done = 0; } g_ms;

// ---- test driver RM8_NET_UI_AUTO="tok,tok,..." (one token every 1.5 s, real pointer taps): host, joinpage, resume, addr:<text>, connect, ready, start,
// force, cancel, leave, chat:<k>, waitstart, waitforce, waittoggle:<seat>, name:<text>, shot:<file.png>, wait:<s>
struct UiAuto { std::vector<std::string> tok; size_t pos = 0; float t = 0; bool init = false; std::string shot; } g_ua;
bool tapIn(Rm8Ui *u, int id)
{
	float x, y, w, h;
	if(!u || !id || !rm8UiVisible(u) || !rm8UiGetScreenRect(u, id, x, y, w, h)) return false;
	float cx = x + w / 2, cy = y + h / 2;
	rm8UiPointerMove(cx, cy); rm8UiPointerDown(cx, cy); rm8UiPointerUp(cx, cy);
	return true;
}
bool tapId(int id){ return tapIn(U.u, id); }
void menuKey(bool o)	// a key press of the original menus (O / Esc)
{
#ifdef RM8_NETUI_SDL
	SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_KEYDOWN; e.key.keysym.sym = o ? SDLK_o : SDLK_ESCAPE; rm8MenuEvent(e, 1024, 768);
#else
	(void)o;
#endif
}
void uiAutoStep(float dt)
{
	if(!g_ua.init){
		g_ua.init = true;
		if(const char *e = getenv("RM8_NET_UI_AUTO")){ std::string cur; for(const char *c = e;; c++){ if(*c == ',' || !*c){ if(!cur.empty()) g_ua.tok.push_back(cur); cur.clear(); if(!*c) break; } else cur += *c; } }
	}
	if(g_ua.pos >= g_ua.tok.size() || (g_ua.t += dt) < 1.5f) return;
	g_ua.t = 0;
	std::string t = g_ua.tok[g_ua.pos++];
	bool ok = true;
	bool menus = rm8MenuNetMode() != RM8MENU_NET_OFF;
	if(t == "host") ok = U.page == P_CHOICE && tapId(U.bHost);
	else if(t == "joinpage") ok = U.page == P_CHOICE && tapId(U.bJoin);
	else if(t == "resume") ok = U.page == P_CHOICE && tapId(U.bResume);
	else if(t.compare(0, 5, "addr:") == 0){ U.addr = t.substr(5); refreshJoin(); }
	else if(t.compare(0, 5, "name:") == 0){ ok = U.page == P_CHOICE && tapId(U.bName); if(ok){ U.name = t.substr(5); nameDone(); } }
	else if(t == "connect") ok = U.page == P_JOIN && tapId(U.bConnect);
	else if(t == "ready" || t == "start"){ ok = menus; if(ok) menuKey(true); }	// the O key of the original menus
	else if(t == "force") ok = R.confirm && tapIn(R.u, R.cOk);
	else if(t == "cancel") ok = R.confirm && tapIn(R.u, R.cCancel);
	else if(t == "leave"){ ok = menus; if(ok) menuKey(false); }
	else if(t.compare(0, 5, "chat:") == 0){ int k = atoi(t.c_str() + 5) % 6; Rm8Ui *u = menus ? R.u : H.u; int b = menus ? R.bChat : H.bChat; int p = menus ? R.phr[k] : H.phr[k]; ok = tapIn(u, b); if(ok){ if(menus) roomRefresh(); else { H.chat = true; rm8UiSetVisible(H.u, p, true); } ok = tapIn(u, p); } }
	else if(t == "waitstart") ok = tapIn(WT.u, WT.bStart);
	else if(t == "waitforce") ok = WT.confirm && tapIn(WT.u, WT.cOk);
	else if(t.compare(0, 11, "waittoggle:") == 0){ int k = atoi(t.c_str() + 11); ok = k >= 0 && k < 4 && tapIn(WT.u, WT.tog[k]); }
	else if(t.compare(0, 5, "shot:") == 0) g_ua.shot = t.substr(5);
	else if(t.compare(0, 5, "wait:") == 0) g_ua.t = -(float)atof(t.c_str() + 5);
	rm8Log("net ui auto: %s%s (page %d)", t.c_str(), ok ? "" : " - not possible now, retried", (int)U.page);
	if(!ok) g_ua.pos--;
}

}	// namespace

void rm8NetUiOpen(){ U.noticeT = U.noticeT > 0 ? U.noticeT : 0; setPage(rm8NetIsClient() ? P_JOIN : P_CHOICE); }
void rm8NetUiHide(){ setTextInput(false); if(U.u) rm8UiShow(U.u, false); U.page = P_NONE; }
bool rm8NetUiVisible(){ return U.page != P_NONE; }
void rm8NetUiNotice(const std::string &text, float seconds){ U.notice = text; U.noticeT = seconds; }

bool rm8NetUiEvent(const SDL_Event &e, int w, int h)
{
#ifdef RM8_NETUI_SDL
	if(U.page == P_NONE) return false;
	bool edit = U.page == P_JOIN || U.page == P_NAME;
	switch(e.type){
	case SDL_KEYDOWN:
		if(e.key.keysym.scancode == SDL_SCANCODE_AC_BACK || e.key.keysym.sym == SDLK_ESCAPE){
			if(U.page == P_JOIN){ rm8NetLeave("取消"); setPage(P_CHOICE); }
			else if(U.page == P_NAME) setPage(P_CHOICE);
			else { setPage(P_NONE); if(rm8NetAppHooks().backToMenu) rm8NetAppHooks().backToMenu(); }
		}
		else if(edit && e.key.keysym.sym == SDLK_BACKSPACE){ keypad(3); }
		else if(U.page == P_JOIN && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)){ rm8NetJoin(U.addr); refreshJoin(); }
		else if(U.page == P_NAME && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)) nameDone();
		else if(edit && (e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_v){ if(char *c = SDL_GetClipboardText()){ if(U.page == P_NAME) nameAppend(c); else { U.addr += c; if(U.addr.size() > 120) U.addr.resize(120); } SDL_free(c); refreshJoin(); } }
		return true;
	case SDL_TEXTINPUT:
		if(U.page == P_JOIN){ for(const char *c = e.text.text; *c; c++) if(U.addr.size() < 120 && (unsigned char)*c >= 0x20) U.addr += *c; refreshJoin(); }
		else if(U.page == P_NAME){ nameAppend(e.text.text); refreshJoin(); }
		return true;
	case SDL_MOUSEMOTION: if(e.motion.which != SDL_TOUCH_MOUSEID) rm8UiPointerMove((float)e.motion.x, (float)e.motion.y); return true;
	case SDL_MOUSEBUTTONDOWN: if(e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_LEFT) rm8UiPointerDown((float)e.button.x, (float)e.button.y); return true;
	case SDL_MOUSEBUTTONUP: if(e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_LEFT) rm8UiPointerUp((float)e.button.x, (float)e.button.y); return true;
	case SDL_FINGERDOWN: rm8UiPointerMove(e.tfinger.x * w, e.tfinger.y * h); rm8UiPointerDown(e.tfinger.x * w, e.tfinger.y * h, false); return true;
	case SDL_FINGERUP: rm8UiPointerUp(e.tfinger.x * w, e.tfinger.y * h); return true;
	case SDL_FINGERMOTION: case SDL_MOUSEWHEEL: case SDL_KEYUP: case SDL_TEXTEDITING: return true;
	default: return false;
	}
#else
	(void)e; (void)w; (void)h; return false;
#endif
}

Rm8NetUiResult rm8NetUiUpdate(float dt)
{
	if(U.page == P_NONE) return RM8NET_UI_CLOSED;
	if(U.noticeT > 0) U.noticeT -= dt;
	std::string st = U.noticeT > 0 ? U.notice : std::string();
	if(U.page == P_JOIN) refreshJoin();
	uiAutoStep(dt);
	if(st.empty() && U.page == P_JOIN) st = rm8NetJoinState();
	if(U.u) rm8UiSetText(U.u, U.status, st);
	return U.page == P_NONE ? RM8NET_UI_CLOSED : RM8NET_UI_RUNNING;
}

void rm8NetUiRender(Camera *cam){ if(U.u && U.page != P_NONE) rm8UiRender(cam); }

// the room overlay: called by the app every menu frame while a room is open on the original menus
void rm8NetUiMenuTick(float dt)
{
	if(U.noticeT > 0) U.noticeT -= dt;
	roomBuild();
	if(!R.u) return;
	bool on = rm8MenuNetMode() != RM8MENU_NET_OFF && rm8NetSession();
	if(!on){ rm8UiShow(R.u, false); R.confirm = false; R.chat = false; R.startReq = 0; return; }
	if(R.startReq){ int k = R.startReq; R.startReq = 0; if(rm8NetLobbyStart(k == 2)) return; R.confirm = true; }
	roomRefresh();
	if(!rm8UiVisible(R.u)) rm8UiShow(R.u, true);	// the menus hide every UI on a page change: back on top
	// test hooks: RM8_NET_AUTOREADY=1 (a client presses O once), RM8_NET_AUTOSTART=<n> (the host starts once n players are ready; RM8_NET_AUTOSTART_FORCE=<s> force-starts after s seconds)
	R.autoT += dt;
	Rm8NetRoomView v = rm8NetRoomView();
	if(getenv("RM8_NET_AUTOREADY") && atoi(getenv("RM8_NET_AUTOREADY")) && rm8NetIsClient() && v.valid && v.mySeat >= 0 && !R.autoReadySent && R.autoT > 1.0f){ R.autoReadySent = true; if(!v.meReady) rm8NetRoomReady(); }
	if(const char *as = getenv("RM8_NET_AUTOSTART")) if(rm8NetIsHost() && v.valid && v.screen == 2){
		const char *fs = getenv("RM8_NET_AUTOSTART_FORCE");
		if(v.ready >= atoi(as) && v.ready > 0 && R.autoT > 2.0f) rm8NetLobbyStart(false);
		else if(fs && R.autoT > (float)atof(fs)) rm8NetLobbyStart(true);
	}
	uiAutoStep(dt);
}
bool rm8NetUiMenuEvent(const SDL_Event &e)
{
#ifdef RM8_NETUI_SDL
	if(e.type != SDL_KEYDOWN) return false;
	if(R.confirm){	// the force start question: O = yes, Esc / back = no
		if(e.key.keysym.sym == SDLK_o || e.key.keysym.sym == SDLK_RETURN){ R.confirm = false; R.startReq = 2; return true; }
		if(e.key.keysym.scancode == SDL_SCANCODE_AC_BACK || e.key.keysym.sym == SDLK_ESCAPE){ R.confirm = false; return true; }
		return true;
	}
#else
	(void)e;
#endif
	return false;
}
void rm8NetUiRoomStartAsk(){ roomStartAsk(); }

// the netplay line of the board: the status (whose decision, remote players, reconnect / resync), the last notice, the chat button; the waiting panel
void rm8NetHudRender(Camera *cam)
{
	(void)cam;
	if(!rm8NetActive()){ if(H.u) rm8UiShow(H.u, false); if(WT.u) rm8UiShow(WT.u, false); if(MW.u) rm8UiShow(MW.u, false); return; }
	if(!H.u){
		H.u = rm8UiCreateCanvas("nethud", RM8UI_HUD);	// HUD mode: the root lets every tap through
		if(!H.u) return;
		H.line = rm8UiAddWidget(H.u, "label", 0, 212, 112, 600, 34, -1, 0xB0101828, 22, 9);
		H.note = rm8UiAddWidget(H.u, "label", 0, 162, 150, 700, 30, -1, 0xA0281818, 19, 9);
		rm8UiSetColor(H.u, H.line, 0xFFFFE6A0); rm8UiSetColor(H.u, H.note, 0xFFFFFFFF);
		rm8UiSetAlignN(H.u, ("dyn" + std::to_string(H.line)).c_str(), 1, 1); rm8UiSetAlignN(H.u, ("dyn" + std::to_string(H.note)).c_str(), 1, 1);
		rm8UiSetIgnoreAll(H.u, true);
		H.bChat = rm8UiAddWidget(H.u, "button", 0, 820, 114, 56, 30, -1, 0xE0C86428, 16, 10); rm8UiSetText(H.u, H.bChat, "聊天"); rm8UiSetColor(H.u, H.bChat, 0xFFFFFFFF);
		rm8UiSetAlignN(H.u, ("dyn" + std::to_string(H.bChat)).c_str(), 1, 1);
		for(int k = 0; k < 6; k++){ H.phr[k] = rm8UiAddWidget(H.u, "button", 0, 820, 150 + k * 32, 96, 28, -1, 0xF0FFF0D8, 15, 10); rm8UiSetText(H.u, H.phr[k], PHRASES[k]); rm8UiSetColor(H.u, H.phr[k], 0xFF602010); rm8UiSetAlignN(H.u, ("dyn" + std::to_string(H.phr[k])).c_str(), 1, 1); }
		rm8UiOnClick(H.u, hudClick);
	}
	static auto lastT = std::chrono::steady_clock::now(); auto nowT = std::chrono::steady_clock::now(); float dt = std::min(0.25f, std::chrono::duration<float>(nowT - lastT).count()); lastT = nowT;
	if(U.noticeT > 0) U.noticeT -= dt;	// (the board does not run rm8NetUiUpdate)
	std::string line = rm8NetStatusLine();
	rm8UiSetText(H.u, H.line, line);
	rm8UiSetVisible(H.u, H.line, !line.empty());
	rm8UiSetText(H.u, H.note, U.noticeT > 0 ? U.notice : std::string());
	rm8UiSetVisible(H.u, H.note, U.noticeT > 0 && !U.notice.empty());
	if(rm8MiniActive()) H.chat = false;	// [N] N4: a minigame on this device takes the input (no chat button over it)
	rm8UiSetVisible(H.u, H.bChat, !rm8MiniActive());
	for(int k = 0; k < 6; k++) rm8UiSetVisible(H.u, H.phr[k], H.chat);
	rm8UiShow(H.u, true);
	waitRender();
	miniRender();	// [N] N4
	uiAutoStep(dt);
}

// test hook RM8_NET_SETUP="map=<id>;seats=<char><h|n|c>,...;cash=..;tickets=..;vehicle=..;limit=..;win=.." (default: map 1, 6h 1n 5c 3c):
// h = the host's own seat, n = a seat waiting for a network player, c = computer
bool rm8NetTestSetup(Rm8Setup &st, std::vector<int> &kinds)
{
	st = Rm8Setup(); st.story = false; st.map = 1; kinds.clear();
	std::string spec = getenv("RM8_NET_SETUP") ? getenv("RM8_NET_SETUP") : "map=1;seats=6h,1n,5c,3c";
	size_t p = 0;
	while(p < spec.size()){
		size_t e = spec.find(';', p); if(e == std::string::npos) e = spec.size();
		std::string kv = spec.substr(p, e - p); p = e + 1;
		size_t q = kv.find('='); if(q == std::string::npos) continue;
		std::string k = kv.substr(0, q), v = kv.substr(q + 1);
		if(k == "map") st.map = atoi(v.c_str());
		else if(k == "cash") st.cash = atoi(v.c_str());
		else if(k == "tickets") st.tickets = atoi(v.c_str());
		else if(k == "vehicle") st.vehicle = atoi(v.c_str());
		else if(k == "limit") st.timeLimit = atoi(v.c_str());
		else if(k == "win") st.winCond = atoi(v.c_str());
		else if(k == "seats"){
			size_t a = 0;
			while(a < v.size()){
				size_t b = v.find(',', a); if(b == std::string::npos) b = v.size();
				std::string s = v.substr(a, b - a); a = b + 1;
				if(s.empty()) continue;
				Rm8Seat seat; seat.charId = std::max(1, std::min(16, atoi(s.c_str()))); seat.computer = s.back() == 'c';
				if(st.seats.size() < 4){ st.seats.push_back(seat); kinds.push_back(s.back() == 'h' ? 0 : s.back() == 'c' ? 2 : 1); }
			}
		}
	}
	return st.seats.size() >= 2;
}

std::string rm8NetUiTakeShot(){ std::string s = g_ua.shot; g_ua.shot.clear(); return s; }
std::string rm8NetMiniShotDue()	// [N] N4 test hook RM8_NET_MINI_SHOT=<prefix>[,<s>]: every device writes one screen per minigame (the player: the game; the others: the waiting board)
{
	if(!g_ms.init){
		g_ms.init = true;
		if(const char *e = getenv("RM8_NET_MINI_SHOT")){ g_ms.prefix = e; size_t c = g_ms.prefix.rfind(','); if(c != std::string::npos){ g_ms.at = (float)atof(g_ms.prefix.c_str() + c + 1); g_ms.prefix.resize(c); } }
	}
	Rm8MiniWaitInfo mi;
	if(g_ms.prefix.empty() || !rm8EventMiniWaiting(&mi) || mi.serial == g_ms.done || mi.frames < (uint32_t)(g_ms.at * 60)) return std::string();
	if(mi.local && !mi.playing) return std::string();	// the player's device: while its game runs
	g_ms.done = mi.serial;
	return g_ms.prefix + "_g" + std::to_string(mi.game) + "_" + std::to_string(mi.serial) + ".png";
}
