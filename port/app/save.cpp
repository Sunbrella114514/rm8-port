// Save / load: the original record format + the port extension block. See save.h and docs/save_port.md.
#include "save.h"
#include "collect.h"
#include "story.h"
#include "ui.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- registries
struct ExtReg { std::string tag; int order; void (*w)(Rm8SaveW &); void (*r)(Rm8SaveR &); void (*after)(); };
std::vector<ExtReg> &exts(){ static std::vector<ExtReg> v; return v; }

std::string g_dir, g_scenes, g_game;
std::map<std::string, int> g_stageId;		// scene folder -> stage id (scenes/stages.txt; download maps 0x15 + index)
std::map<int, std::string> g_stageDir, g_stageName;
struct RoleName { std::string name, pic; };
std::map<int, RoleName> g_roleName;
std::vector<unsigned char> g_loaded;		// the parsed file (rm8SaveParse -> rm8SaveApply)
Rm8SaveInfo g_info;

const int WONDER_TABLE = 0x6c;
const int STREETS = 136;
const char EXT_MAGIC[4] = {'R', 'M', '8', 'X'};
const int EXT_VERSION = 1;

// ---------------------------------------------------------------- native-encoded names of the stock objects (the original's encoding) + the original's plot order
// [A] scenes/save_tables.txt (made by rm8_extract, rm8x/savetables.py; docs/assets_legal.md section 8 item 4) replaces the former compiled-in tables (generated from the game data):
//   encoding <cp950|gbk>  /  code <unicode hex> <2-byte native code hex>  /  terra <map dir> <wonders> <n> <n indices>
// the old edition's records hold Big5 names (cp950), the Steam edition's GBK; the table says which
struct B5 { unsigned short u, b; };
std::vector<B5> g_b5;
std::string g_saveEncoding;
struct TerraOrder { std::string dir; std::vector<int> order; int n = -1, wonders = -1; };
std::vector<TerraOrder> g_terraTab;

void loadSaveTables(const std::string &path)
{
	g_b5.clear(); g_terraTab.clear(); g_saveEncoding.clear();
	std::ifstream f(path); std::string line;
	if(!f){ rm8Log("save: %s is missing (rm8_extract data_format 2 writes it): stock names and the plot order cannot be converted", path.c_str()); return; }
	while(std::getline(f, line)){
		if(line.empty() || line[0] == '#') continue;
		std::istringstream ss(line); std::string kw; ss >> kw;
		if(kw == "encoding") ss >> g_saveEncoding;
		else if(kw == "code"){ unsigned u = 0, b = 0; ss >> std::hex >> u >> b; if(ss) g_b5.push_back({(unsigned short)u, (unsigned short)b}); }
		else if(kw == "terra"){
			TerraOrder t; ss >> t.dir >> t.wonders >> t.n;
			for(int i = 0, v; i < t.n && (ss >> v); i++) t.order.push_back(v);
			if(ss && (int)t.order.size() == t.n) g_terraTab.push_back(std::move(t));
		}
	}
	rm8Log("save: %s: encoding %s, %d name codes, %d plot orders", path.c_str(), g_saveEncoding.c_str(), (int)g_b5.size(), (int)g_terraTab.size());
}

}	// namespace

// the stock names are UTF-8 in the port, Big5 (old edition) or GBK (Steam edition) in the record (stock object +0x000, 0x100 bytes); used by stock.cpp
// (the function names say Big5 for historical reasons: the codes come from scenes/save_tables.txt)
std::string rm8SaveBig5FromUtf8(const std::string &s)
{
	std::string out;
	for(size_t i = 0; i < s.size();){
		unsigned char c = (unsigned char)s[i]; unsigned cp; int n;
		if(c < 0x80){ out += (char)c; i++; continue; }
		if((c & 0xE0) == 0xC0){ cp = c & 0x1F; n = 1; } else if((c & 0xF0) == 0xE0){ cp = c & 0x0F; n = 2; } else { cp = c & 7; n = 3; }
		for(int k = 1; k <= n && i + k < s.size(); k++) cp = cp << 6 | ((unsigned char)s[i + k] & 0x3F);
		i += n + 1;
		bool found = false;
		for(const B5 &e : g_b5) if(e.u == cp){ out += (char)(e.b >> 8); out += (char)(e.b & 255); found = true; break; }
		if(!found) out += '?';
	}
	return out;
}
std::string rm8SaveUtf8FromBig5(const char *p, size_t n)
{
	std::string out;
	for(size_t i = 0; i < n && p[i];){
		unsigned char c = (unsigned char)p[i];
		if(c < 0x80){ out += (char)c; i++; continue; }
		if(i + 1 >= n){ break; }
		unsigned b = c << 8 | (unsigned char)p[i + 1]; i += 2;
		unsigned cp = '?';
		for(const B5 &e : g_b5) if(e.b == b){ cp = e.u; break; }
		if(cp < 0x80) out += (char)cp;
		else if(cp < 0x800){ out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
		else { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
	}
	return out;
}

Rm8SaveHooks &rm8SaveHooks(){ static Rm8SaveHooks h; return h; }

void rm8SaveRegisterExt(const char *tag, int order, void (*write)(Rm8SaveW &), void (*read)(Rm8SaveR &), void (*afterLoad)())
{
	ExtReg e; e.tag = std::string(tag).substr(0, 4); e.order = order; e.w = write; e.r = read; e.after = afterLoad;
	exts().push_back(e);
	std::stable_sort(exts().begin(), exts().end(), [](const ExtReg &a, const ExtReg &b){ return a.order < b.order; });
}

// ======================================================================== paths, names
namespace {

std::string mapDirOfId(int id)
{
	auto it = g_stageDir.find(id);
	return it == g_stageDir.end() ? std::string() : it->second;
}
int mapIdOfDir(const std::string &dir)
{
	auto it = g_stageId.find(dir);
	if(it != g_stageId.end()) return it->second;
	if(dir.size() == 2) return atoi(dir.c_str());
	return 0;
}
const int *terraOrderOf(const std::string &dir, int &n, int &wonders)
{
	for(const TerraOrder &e : g_terraTab) if(dir == e.dir){ n = e.n; wonders = e.wonders; return e.order.data(); }
	n = wonders = -1; return nullptr;
}
int rd32(const std::vector<unsigned char> &d, size_t off){ int v = 0; if(off + 4 <= d.size()) memcpy(&v, d.data() + off, 4); return v; }

bool readFile(const std::string &path, std::vector<unsigned char> &out)
{
	FILE *f = fopen(path.c_str(), "rb");
	if(!f) return false;
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	out.resize(n > 0 ? (size_t)n : 0);
	bool ok = n <= 0 || fread(out.data(), 1, out.size(), f) == out.size();
	fclose(f);
	return ok && n > 0;
}
bool writeFileAtomic(const std::string &path, const std::vector<unsigned char> &d)
{
	std::error_code ec;
	fs::create_directories(fs::path(path).parent_path(), ec);
	std::string tmp = path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if(!f) return false;
	bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
	ok = fclose(f) == 0 && ok;
	if(!ok){ fs::remove(tmp, ec); return false; }
	fs::rename(tmp, path, ec);
	if(ec){ fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec); fs::remove(tmp, ec); if(ec) return false; }
	return true;
}

}	// namespace

void rm8SaveInit(const std::string &dataDir, const std::string &scenesRoot, const std::string &gameDir)
{
	fs::path d(dataDir);
	g_dir = (d / "save").string();
	g_scenes = scenesRoot; g_game = gameDir;
	loadSaveTables(scenesRoot + "/save_tables.txt");
	std::error_code ec;
	fs::create_directories(g_dir, ec);
	g_stageId.clear(); g_stageDir.clear(); g_stageName.clear(); g_roleName.clear();
	std::ifstream sf(scenesRoot + "/stages.txt"); std::string line;
	while(std::getline(sf, line)){
		std::istringstream ss(line); std::string kw, dir, name; int id = 0, clime = 0;
		if(ss >> kw >> id >> dir >> clime >> name && kw == "stage"){ std::replace(name.begin(), name.end(), '_', ' '); g_stageId[dir] = id; g_stageDir[id] = dir; g_stageName[id] = name; }
	}
	std::vector<std::string> dl;
	for(auto &e : fs::directory_iterator(scenesRoot, ec)){
		std::string n = e.path().filename().string();
		if(n.rfind("download", 0) == 0 && fs::exists(e.path() / "scene.txt")) dl.push_back(n);
	}
	std::sort(dl.begin(), dl.end());
	for(size_t i = 0; i < dl.size(); i++){ int id = 0x15 + (int)i; g_stageId[dl[i]] = id; g_stageDir[id] = dl[i]; g_stageName[id] = dl[i]; }	// [V] the map menu's numbering (@0x4a9020)
	std::ifstream rf(scenesRoot + "/roles.txt");
	while(std::getline(rf, line)){
		if(line.rfind("role ", 0) != 0) continue;
		std::istringstream ss(line); std::vector<std::string> tk; std::string w;
		while(ss >> w) tk.push_back(w);
		if(tk.size() < 5) continue;
		RoleName &r = g_roleName[atoi(tk[1].c_str())]; r.pic = tk[tk.size() - 3]; r.name = tk.back();
	}
	rm8Log("save: folder %s, %d stages, %d roles", g_dir.c_str(), (int)g_stageDir.size(), (int)g_roleName.size());
	if(getenv("RM8_IMPORT_SAVES") && !gameDir.empty()){	// test / convenience: copy the original's records once (read only access to the game folder)
		for(auto &e : fs::directory_iterator(fs::path(gameDir) / "save", ec)){
			std::string n = e.path().filename().string();
			if(n.rfind("record_", 0) == 0 && n.size() > 4 && n.substr(n.size() - 4) == ".sav" && n != "record_temp.sav" && !fs::exists(fs::path(g_dir) / n))
				fs::copy_file(e.path(), fs::path(g_dir) / n, fs::copy_options::skip_existing, ec);
		}
	}
}

std::string rm8SaveDir(){ return g_dir; }
std::string rm8SaveScenesDir(){ return g_scenes; }

std::string rm8SaveSlotPath(int slot, bool multi)
{
	char b[64];
	if(slot <= 0) snprintf(b, sizeof b, "record_temp.sav");
	else snprintf(b, sizeof b, multi ? "record_m%02d.sav" : "record_%02d.sav", slot);	// [V] CPlayerTag__SaveGameRecord @0x492850
	return (fs::path(g_dir) / b).string();
}

// [V] Save_ReadSlotHeader @0x4062a0: version > 6, "r8recrd", then the 0x28 byte header
bool rm8SaveSlotInfo(int slot, bool multi, Rm8SaveSlotInfo &o)
{
	std::vector<unsigned char> d;
	if(!readFile(rm8SaveSlotPath(slot, multi), d) || d.size() < 12 + 0x28) return false;
	if(rd32(d, 0) <= 6 || memcmp(d.data() + 4, "r8recrd", 8) != 0) return false;
	auto u16 = [&](size_t off){ unsigned short v = 0; memcpy(&v, d.data() + 12 + off, 2); return (int)v; };
	o.year = u16(0); o.month = u16(2); o.day = u16(6); o.hour = u16(8); o.minute = u16(10);	// SYSTEMTIME: year, month, dayOfWeek, day, hour, minute
	o.map = rd32(d, 12 + 0x10);
	int n = rd32(d, 12 + 0x14);
	o.charIds.clear();
	for(int k = 0; k < n && k < 4; k++) o.charIds.push_back(rd32(d, 12 + 0x18 + 4 * k));
	return true;
}

std::string rm8SaveSlotLabel(int slot, bool multi)
{
	Rm8SaveSlotInfo i;
	if(!rm8SaveSlotInfo(slot, multi, i)) return rm8Str(7102);	// "空"
	std::string name;
	if(multi){ auto it = g_stageName.find(i.map); name = it != g_stageName.end() ? it->second : "?"; }	// [V] Game_DisplaySaveSlotList @0x4ac420: the stage name (free) / the first role's name (story)
	else if(!i.charIds.empty()){ auto it = g_roleName.find(i.charIds[0]); name = it != g_roleName.end() ? it->second.name : "?"; }
	return rm8StrFmt(7101, name.c_str(), i.month, i.day, i.hour, i.minute);
}

// ======================================================================== writing
namespace {

void putU16(Rm8SaveW &w, int v){ unsigned short s = (unsigned short)v; w.raw(&s, 2); }

// the role record [V] Role_Save @0x491fb0 (docs/save_format.md section 6)
void writeRole(Rm8SaveW &w, const Rm8SaveRole &r, int charId)
{
	w.i32(r.p40); w.i32(r.p44); w.i32(r.ai); w.i32(r.status); w.i32(r.state); w.i32(r.shape); w.i32(r.bullyBy); w.i32(r.turtle); w.i32(r.stay);
	w.i32(r.statusDays); w.i32(r.possDays); w.i32(r.bullyDays); w.i32(r.invisible);
	w.i32((int)r.qa.size()); for(const Rm8SaveMsg &m : r.qa){ w.i32(m.cmd); w.i32(m.a); w.i32(m.b); }
	w.i32((int)r.qb.size()); for(const Rm8SaveMsg &m : r.qb){ w.i32(m.cmd); w.i32(m.a); w.i32(m.b); }
	w.i32(r.vehicle); w.i32(r.dice); w.i32(r.tickets); w.i32(r.cash); w.i32(r.deposit);
	for(int v : r.loan) w.i32(v);
	for(int v : r.cards) w.i32(v);
	w.i32((int)r.holds.size());
	for(const Rm8SaveHold &h : r.holds){ w.i32(h.stock); if(h.stock != -1){ w.i32(h.qty); w.f32(h.last); w.f32(h.cost); } }
	for(int v : r.rent) w.i32(v);
	w.i32(r.bomb); w.i32(r.bubble);
	for(int v : r.f374) w.i32(v);
	for(int v : r.used) w.i32(v);
	for(int v : r.f5c0) w.i32(v);
	w.i32(r.f5cc);
	if(charId == 16) w.i32(r.sarah);	// [V] class 0x5e2918 (role 16) writes one more int
}

bool readRole(Rm8SaveR &r, Rm8SaveRole &o, int charId, int version)
{
	o.p40 = r.i32(); o.p44 = r.i32(); o.ai = r.i32(); o.status = r.i32(); o.state = r.i32(); o.shape = r.i32(); o.bullyBy = r.i32(); o.turtle = r.i32(); o.stay = r.i32();
	o.statusDays = r.i32(); o.possDays = r.i32(); o.bullyDays = r.i32(); o.invisible = r.i32();
	for(int q = 0; q < 2; q++){
		int n = r.i32();
		if(n < 0 || n > 4096) return false;
		std::vector<Rm8SaveMsg> &v = q ? o.qb : o.qa; v.clear();
		for(int i = 0; i < n; i++){ Rm8SaveMsg m; m.cmd = r.i32(); m.a = r.i32(); m.b = r.i32(); v.push_back(m); }
	}
	o.vehicle = r.i32(); o.dice = r.i32(); o.tickets = r.i32(); o.cash = r.i32(); o.deposit = r.i32();
	for(int &v : o.loan) v = r.i32();
	for(int &v : o.cards) v = r.i32();
	int nh = r.i32();
	if(nh < 0 || nh > 4096) return false;
	o.holds.clear();
	for(int i = 0; i < nh; i++){
		Rm8SaveHold h; h.stock = r.i32();
		if(h.stock != -1){ h.qty = r.i32(); h.last = r.f32(); h.cost = r.f32(); }
		o.holds.push_back(h);
	}
	for(int &v : o.rent) v = r.i32();
	o.bomb = r.i32(); o.bubble = r.i32();
	for(int &v : o.f374) v = r.i32();
	for(int &v : o.used) v = r.i32();
	for(int &v : o.f5c0) v = r.i32();
	o.f5cc = r.i32();
	if(charId == 16 && version >= 8) o.sarah = r.i32();
	return !r.bad;
}

// [A] defaults of the wonder table (0x6c bytes = 3 x 0x24: index, flag, owner seat, 4 floats, 50000, 0.1f) from record_01: nothing built
void tableEntry(int i, int out[9])
{
	float f01 = 0.1f; int f; memcpy(&f, &f01, 4);
	int d[9] = {i, i == 1 ? 1 : 0, -1, 0, 0, 0, 0, 50000, f};
	if(rm8SaveHooks().wonderTable) rm8SaveHooks().wonderTable(i, out);
	else memcpy(out, d, sizeof d);
}

}	// namespace

bool rm8SaveBoardOn(){ return rm8TurnRoleCount() > 0 && rm8RolesCurrent() >= 0 && rm8GameNodeCount() > 0; }
bool rm8SaveMulti(){ return !rm8StoryActive(); }

int rm8SaveWonderCount()
{
	const char *dir = rm8SaveGameMapDir();
	int n = 0, wn = 0; terraOrderOf(dir ? dir : "", n, wn);
	if(rm8SaveHooks().wonderCount) return rm8SaveHooks().wonderCount(mapIdOfDir(dir ? dir : ""));
	return std::max(0, wn);
}

int rm8SaveTerraPort(int k)
{
	const char *dir = rm8SaveGameMapDir();
	int n = 0, wn = 0; const int *order = dir ? terraOrderOf(dir, n, wn) : nullptr;
	return order && k >= 0 && k < n ? order[k] : k;
}

bool rm8SaveBuild(std::vector<unsigned char> &out, int currentOverride, bool zeroTime)
{
	if(!rm8SaveBoardOn()) return false;
	Rm8SaveW w;
	int n = rm8TurnRoleCount();
	bool story = rm8StoryActive();
	Rm8GameOptions o = rm8SaveGameOptions();
	std::string dir = rm8SaveGameMapDir() ? rm8SaveGameMapDir() : "";
	int mapId = mapIdOfDir(dir);
	// header [V] Game_WriteRecordToFile @0x4a7540
	w.i32(9); w.raw("r8recrd\0", 8);
	{
		time_t t = time(nullptr); struct tm *lt = localtime(&t); struct tm z; memset(&z, 0, sizeof z);
		if(zeroTime || !lt){ z.tm_year = 70; z.tm_mon = 0; z.tm_mday = 1; lt = &z; }
		int dow = zeroTime ? 0 : lt->tm_wday;
		putU16(w, zeroTime ? 0 : lt->tm_year + 1900); putU16(w, zeroTime ? 0 : lt->tm_mon + 1); putU16(w, dow); putU16(w, zeroTime ? 0 : lt->tm_mday);
		putU16(w, zeroTime ? 0 : lt->tm_hour); putU16(w, zeroTime ? 0 : lt->tm_min); putU16(w, zeroTime ? 0 : lt->tm_sec); putU16(w, 0);
	}
	w.i32(mapId); w.i32(n);
	for(int k = 0; k < 4; k++) w.i32(k < n ? rm8TurnCharId(k) : 0);
	rm8SaveGameWriteHeader(w);
	// setup [V] FUN_004a8740
	w.i32(story ? 1 : 0); w.i32(mapId); w.i32(story ? rm8StorySetupLevel() : 0);
	w.i32(o.cash); w.i32(o.tickets); w.i32(o.vehicle); w.i32(o.timeLimit); w.i32(o.winCond);
	w.i32(n);
	for(int k = 0; k < n; k++){ w.i32(rm8TurnCharId(k)); w.i32(rm8SaveRolesOutfit(k)); w.i32(rm8TurnIsPlayer(k) ? 0 : 1); }	// a seat taken over by the computer stays a human seat in the setup block (only +0x1B0 of its role record is 2, docs/autoplay.md)
	if(story){	// [V] Game_SaveBinaryData @0x4abd60: level, cash {stored, carried}, tickets {stored, carried}, 8 + 8 cards
		Rm8Box &b = rm8Box();
		w.i32(b.stage); w.i32(b.cash[0]); w.i32(b.cash[1]); w.i32(b.points[0]); w.i32(b.points[1]);
		for(int s = 0; s < 2; s++) for(int k = 0; k < 8; k++) w.i32(b.cards[s][k]);
	}
	rm8SaveStockWrite(w);
	// roles [V] FUN_00498730
	w.i32(currentOverride >= 0 ? currentOverride : rm8SaveRolesCurrentRaw());
	for(int k = 0; k < n; k++){
		Rm8SaveRole r;
		rm8SaveGameRoleGet(k, r); rm8SaveRolesRoleGet(k, r); rm8SaveCardsRoleGet(k, r); rm8SaveNpcRoleGet(k, r); rm8SaveStoryRoleGet(k, r);
		rm8SaveStockRoleGet(k, r); rm8SaveEventsRoleGet(k, r);
		writeRole(w, r, rm8TurnCharId(k));
	}
	rm8SaveGameWriteTerra(w);
	rm8SaveGameWriteStreets(w);
	rm8SaveNpcWrite(w);
	rm8SaveCompanyWrite(w, false);	// [H] DAT_006403c8 = the ticket center (functions 0x4dfec0 .. 0x4e0340 are the TicketCenter_*)
	rm8SaveCompanyWrite(w, true);	// DAT_006403cc = the power company (0x4da700)
	rm8SaveLotteryWrite(w);
	for(int i = 0; i < 3; i++){ int e[9]; tableEntry(i, e); for(int v : e) w.i32(v); }
	// the port extension block
	Rm8SaveW x;
	for(const ExtReg &e : exts()){
		Rm8SaveW c; e.w(c);
		x.raw(e.tag.c_str(), 4); x.i32((int)c.b.size()); x.raw(c.b.data(), c.b.size());
	}
	w.raw(EXT_MAGIC, 4); w.i32(EXT_VERSION); w.i32((int)x.b.size()); w.raw(x.b.data(), x.b.size());
	out.swap(w.b);
	return true;
}

bool rm8SaveWriteFile(const std::string &path)
{
	std::vector<unsigned char> d;
	if(!rm8SaveBuild(d)) return false;
	bool ok = writeFileAtomic(path, d);
	rm8Log("save: %s %s (%d bytes)", ok ? "wrote" : "FAILED to write", path.c_str(), (int)d.size());
	return ok;
}

// ======================================================================== parsing / validation
namespace {

struct Sec { size_t game = 0, setup = 0, sflb = 0, stocks = 0, roles = 0, terra = 0, streets = 0, npcs = 0, tc = 0, pc = 0, lot = 0, wond = 0, end = 0; };

// walks the record structure (no side effects): the offsets of every section; false = truncated / inconsistent
bool walk(const std::vector<unsigned char> &d, int version, int T, int W, std::vector<int> &charIds, bool &story, Sec &s, std::string &err)
{
	Rm8SaveR r(d.data(), d.size());
	auto fail = [&](const char *what){ err = std::string("damaged record: ") + what; return false; };
	s.game = 12 + 0x28 + 0;	// the 12 game state ints start after the 0x28 byte header (version 4 + magic 8 + 0x28)
	r.pos = 12 + 0x28; r.skip(12 * 4);
	s.setup = r.pos;
	int flag = r.i32(); r.skip(7 * 4);	// flag, map, +8, cash, tickets, vehicle, limit, win cond: 8 ints, the count follows
	r.pos = s.setup + 8 * 4;
	int cnt = r.i32();
	if(r.bad || cnt < 1 || cnt > 4) return fail("player count");
	charIds.clear();
	for(int k = 0; k < cnt; k++){ charIds.push_back(r.i32()); r.skip(8); }
	story = flag != 0;
	s.sflb = r.pos;
	if(flag) r.skip(0x54);
	s.stocks = r.pos;
	for(int q = 0; q < 2; q++){
		int n = r.i32();
		if(r.bad || n < 0 || n > 64) return fail("stock count");
		r.skip((size_t)n * 0x37c);
	}
	s.roles = r.pos;
	r.skip(4);
	for(int k = 0; k < cnt; k++){
		r.skip(13 * 4);
		for(int q = 0; q < 2; q++){ int n = r.i32(); if(r.bad || n < 0 || n > 4096) return fail("queue"); r.skip((size_t)n * 12); }
		r.skip(5 * 4 + 7 * 4 + 8 * 4);
		int nh = r.i32(); if(r.bad || nh < 0 || nh > 4096) return fail("holdings");
		for(int h = 0; h < nh; h++){ int st = r.i32(); if(st != -1) r.skip(12); }
		r.skip((8 + 2 + 9 + 64 + 3 + 1) * 4);
		if(charIds[k] == 16 && version >= 8) r.skip(4);
		if(r.bad) return fail("role record");
	}
	s.terra = r.pos;
	for(int t = 0; t < T; t++){
		r.skip(8); unsigned ty = r.u32(); int lv = r.i32();
		if((ty >> 16) == 2 && lv >= 1 && lv <= 5) r.skip(8);	// [V] the research institute's Building +0x14
	}
	r.skip((size_t)W * 12);
	if(r.bad) return fail("terra");
	s.streets = r.pos; r.skip((size_t)STREETS * 16);
	s.npcs = r.pos;
	{
		int n = r.i32(); if(r.bad || n < 0 || n > 4096) return fail("npc count");
		for(int k = 0; k < n; k++){ int id = r.i32(); if(id == 0x11) r.skip(4); r.skip(8); }
	}
	s.tc = r.pos; r.skip(20);
	s.pc = r.pos; r.skip(20);
	s.lot = r.pos; r.skip(4);
	for(int g = 0; g < 4; g++){ int n = r.i32(); if(r.bad || n < 0 || n > 100000) return fail("lottery"); r.skip((size_t)n * 4); }
	s.wond = r.pos; r.skip(WONDER_TABLE);
	s.end = r.pos;
	if(r.bad) return fail("truncated");
	return true;
}

}	// namespace

bool rm8SaveParse(const std::string &path, Rm8SaveInfo &info, std::string &err)
{
	info = Rm8SaveInfo(); g_loaded.clear();
	if(!readFile(path, g_loaded)){ err = "cannot read " + path; return false; }
	const std::vector<unsigned char> &d = g_loaded;
	if(d.size() < 12 + 0x28 + 12 * 4 + 36 || rd32(d, 0) <= 6 || memcmp(d.data() + 4, "r8recrd", 8) != 0){ err = "not a RichMan8 record"; return false; }
	int version = rd32(d, 0);
	int mapId = rd32(d, 12 + 0x10);
	info.mapDir = mapDirOfId(mapId);
	if(info.mapDir.empty() && mapId >= 1 && mapId <= 11){ char b[8]; snprintf(b, sizeof b, "%02d", mapId); info.mapDir = b; }
	int tn = 0, wn = 0; const int *order = terraOrderOf(info.mapDir, tn, wn);
	if(!order){ err = "no scene / plot table for map id " + std::to_string(mapId); return false; }
	info.terra = tn; info.wonders = wn;
	if(rm8SaveHooks().wonderCount) info.wonders = rm8SaveHooks().wonderCount(mapId);
	Sec s; std::vector<int> ids; bool story = false;
	if(!walk(d, version, info.terra, info.wonders, ids, story, s, err)) return false;
	Rm8SaveR r(d.data(), d.size()); r.pos = s.setup;
	Rm8Setup &st = info.setup;
	st.story = r.i32() != 0; st.map = r.i32(); info.storyLevel = r.i32();
	st.cash = r.i32(); st.tickets = r.i32(); st.vehicle = r.i32(); st.timeLimit = r.i32(); st.winCond = r.i32();
	int cnt = r.i32();
	st.seats.clear();
	for(int k = 0; k < cnt; k++){ Rm8Seat q; q.charId = r.i32(); q.outfit = r.i32(); q.computer = r.i32() != 0; st.seats.push_back(q); }
	r.pos = s.roles; info.current = r.i32();
	info.original = !(s.end + 12 <= d.size() && memcmp(d.data() + s.end, EXT_MAGIC, 4) == 0);
	g_info = info;
	return true;
}

// ======================================================================== applying
bool rm8SaveApply(std::string &err)
{
	const std::vector<unsigned char> &d = g_loaded;
	if(d.empty()){ err = "nothing parsed"; return false; }
	int version = rd32(d, 0);
	int n = rm8TurnRoleCount();
	if(n != (int)g_info.setup.seats.size()){ err = "the board has " + std::to_string(n) + " seats, the record " + std::to_string(g_info.setup.seats.size()); return false; }
	if(rm8GameTerraCount() != g_info.terra){ err = "the scene has a different plot count"; return false; }
	Sec s; std::vector<int> ids; bool story = false;
	int tn = 0, wn = 0; terraOrderOf(g_info.mapDir, tn, wn);
	if(!walk(d, version, g_info.terra, g_info.wonders, ids, story, s, err)) return false;
	Rm8SaveR r(d.data(), d.size());
	bool ok = true;
	r.pos = s.game;
	ok &= rm8SaveGameReadHeader(r, g_info.setup.timeLimit);
	if(story){
		Rm8SaveR b(d.data(), d.size()); b.pos = s.sflb;
		Rm8Box &bx = rm8Box();
		bx.stage = b.i32(); bx.cash[0] = b.i32(); bx.cash[1] = b.i32(); bx.points[0] = b.i32(); bx.points[1] = b.i32();
		for(int q = 0; q < 2; q++) for(int k = 0; k < 8; k++) bx.cards[q][k] = b.i32();
		rm8BoxSave();
	}
	r.pos = s.stocks; ok &= rm8SaveStockRead(r);
	r.pos = s.roles; int cur = r.i32();
	for(int k = 0; k < n; k++){
		Rm8SaveRole rec;
		if(!readRole(r, rec, rm8TurnCharId(k), version)){ err = "role record"; return false; }
		rm8SaveRolesRoleSet(k, rec); rm8SaveGameRoleSet(k, rec); rm8SaveCardsRoleSet(k, rec); rm8SaveStockRoleSet(k, rec);
		rm8SaveStoryRoleSet(k, rec); rm8SaveEventsRoleSet(k, rec); rm8SaveNpcRoleSet(k, rec);
	}
	r.pos = s.terra; ok &= rm8SaveGameReadTerra(r);
	r.pos = s.streets; ok &= rm8SaveGameReadStreets(r);
	r.pos = s.npcs; ok &= rm8SaveNpcRead(r);
	r.pos = s.tc; ok &= rm8SaveCompanyRead(r, false);
	r.pos = s.pc; ok &= rm8SaveCompanyRead(r, true);
	r.pos = s.lot; ok &= rm8SaveLotteryRead(r);
	if(rm8SaveHooks().wonderTableSet){
		Rm8SaveR t(d.data(), d.size()); t.pos = s.wond;
		for(int i = 0; i < 3; i++){ int e[9]; for(int &v : e) v = t.i32(); rm8SaveHooks().wonderTableSet(i, e); }
	}
	// the port extension block
	if(!g_info.original){
		Rm8SaveR x(d.data(), d.size()); x.pos = s.end + 4;
		int ver = x.i32(), len = x.i32();
		(void)ver;
		size_t end = std::min(d.size(), x.pos + (size_t)std::max(0, len));
		struct Chunk { std::string tag; size_t off, len; };
		std::vector<Chunk> chunks;
		while(x.pos + 8 <= end){
			char tag[5] = {0, 0, 0, 0, 0}; memcpy(tag, d.data() + x.pos, 4); x.pos += 4;
			int l = x.i32();
			if(l < 0 || x.pos + (size_t)l > end) break;
			chunks.push_back({tag, x.pos, (size_t)l}); x.pos += l;
		}
		for(const ExtReg &e : exts()){
			for(const Chunk &c : chunks) if(c.tag == e.tag){ Rm8SaveR cr(d.data() + c.off, c.len); e.r(cr); break; }
		}
		for(const ExtReg &e : exts()) if(e.after) e.after();
	}else for(const ExtReg &e : exts()) if(e.after) e.after();
	if(!ok){ err = "a section failed to load"; return false; }
	if(const char *rs = getenv("RM8_SAVE_RESAVE")){	// test hook: the record written again right after the load (before the turn flow resumes): the compatibility check against the original's bytes
		std::vector<unsigned char> d;
		if(rm8SaveBuild(d, cur, true)) writeFileAtomic(rs, d);
	}
	ok = rm8SaveRolesFinish(cur);
	rm8Log("save: loaded (%s), day %d, seat %d%s", g_info.original ? "original record" : "port record", rm8GameDay(), cur, cur >= n ? " (day wrap)" : "");
	return ok;
}

// ======================================================================== boundaries: auto save, manual saves, state dumps
namespace {
int g_pendSlot = -1; bool g_pendMulti = false;
std::string g_loadPath; bool g_loadReq = false;
bool g_notice = false;
}

void rm8SaveRequestLoad(const std::string &path){ g_loadPath = path; g_loadReq = true; }
bool rm8SaveTakeLoad(std::string &path){ if(!g_loadReq) return false; g_loadReq = false; path = g_loadPath; return true; }
bool rm8SavePending(){ return g_pendSlot >= 0; }

bool rm8SaveRequest(int slot, bool multi)
{
	if(!rm8SaveBoardOn() || slot < 1 || slot > 10) return false;
	g_pendSlot = slot; g_pendMulti = multi;
	int cur = rm8TurnCurrent();
	if(rm8TurnState(cur) == 6 && rm8TurnHuman(cur)){ rm8SaveBoundary(1); return true; }
	rm8Log("save: slot %d waits for a quiet point (the human's dice / the day change)", slot);
	return false;
}

void rm8SaveBoundary(int kind)
{
	if(!rm8SaveBoardOn()) return;
	int n = rm8TurnRoleCount();
	std::error_code ec;
	if(kind == 0){
		// [V] Game_SwitchRecordSlot @0x4a7380 (Game_TurnNextRole @0x4971a0 when the turn passes the last seat): the previous record_temp is copied to slot 1 and the new one is
		// written as record_temp (the original's first call, with the day count 0, writes both at once). [A] the call of the game start (day 0) is skipped: the port
		// builds the board first and applies the story start values afterwards, so that record would lack them; the first record is written after day 1
		std::string temp = rm8SaveSlotPath(0, true), slot1 = rm8SaveSlotPath(1, rm8SaveMulti());
		if(!getenv("RM8_NO_AUTOSAVE") && rm8GameDay() > 0){
			std::vector<unsigned char> d;
			if(rm8SaveBuild(d, n)){
				if(fs::exists(temp)) fs::copy_file(temp, slot1, fs::copy_options::overwrite_existing, ec);
				writeFileAtomic(temp, d);
			}
		}
		if(const char *dump = getenv("RM8_SAVE_DUMP")){	// test hook: the record of every day change (time zeroed) for the round-trip comparison
			std::vector<unsigned char> d;
			if(rm8SaveBuild(d, n, true)){
				char b[64]; snprintf(b, sizeof b, "day%04d.bin", rm8GameDay());
				writeFileAtomic((fs::path(dump) / b).string(), d);
			}
		}
	}else if(kind == 2){
		static const char *dumpT = getenv("RM8_SAVE_DUMPT");	// test hook: the record at every turn start (the divergence search of a load test)
		if(dumpT){
			std::vector<unsigned char> d;
			if(rm8SaveBuild(d, rm8TurnCurrent(), true)){
				char b[64]; snprintf(b, sizeof b, "t%04d_s%d.bin", rm8GameDay(), rm8TurnCurrent());
				writeFileAtomic((fs::path(dumpT) / b).string(), d);
			}
		}
		return;
	}else if(kind == 3){
		static const char *dumpT = getenv("RM8_SAVE_DUMPT");
		if(dumpT){
			static int serial = 0; (void)serial;
			std::vector<unsigned char> d;
			if(rm8SaveBuild(d, rm8TurnCurrent(), true)){
				char b[64]; snprintf(b, sizeof b, "r%04d_s%d.bin", rm8GameDay(), rm8TurnCurrent());
				writeFileAtomic((fs::path(dumpT) / b).string(), d);
			}
		}
		return;
	}else if(kind == 1){
		if(const char *dump = getenv("RM8_SAVE_DUMP1")){	// test hook: the record at every human dice wait
			std::vector<unsigned char> d;
			if(rm8SaveBuild(d, -1, true)){
				char b[64]; snprintf(b, sizeof b, "turn%04d_%d.bin", rm8GameDay(), rm8TurnCurrent());
				writeFileAtomic((fs::path(dump) / b).string(), d);
			}
		}
	}
	if(kind == 1){	// test hook RM8_SAVE_AT=<slot>@<day>: a manual save at the first human dice wait of that day (the real request path: rm8SaveRequest -> this boundary)
		static const char *at = getenv("RM8_SAVE_AT"); static bool done = false;
		int slot = 0, day = 0;
		if(at && !done && sscanf(at, "%d@%d", &slot, &day) == 2 && rm8GameDay() >= day){ done = true; g_pendSlot = slot; g_pendMulti = rm8SaveMulti(); }
	}
	if(g_pendSlot >= 1){
		std::vector<unsigned char> d;
		if(rm8SaveBuild(d, kind == 0 ? n : -1)){
			std::string p = rm8SaveSlotPath(g_pendSlot, g_pendMulti);
			bool ok = writeFileAtomic(p, d);
			rm8Log("save: slot %d %s (%s, %d bytes)", g_pendSlot, ok ? "saved" : "FAILED", p.c_str(), (int)d.size());
			g_notice = ok;
		}
		g_pendSlot = -1;
	}
}

void rm8SaveBoardEnded()
{
	std::error_code ec;
	if(!g_dir.empty()) fs::remove(rm8SaveSlotPath(0, true), ec);	// [V] Game_Cleanup @0x4a1340 / Game_Shutdown @0x4a15d0: DeleteFileA("save\record_temp.sav")
	g_pendSlot = -1;
}
