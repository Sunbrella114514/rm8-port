// save / load hooks of roles.cpp (included at the end of roles.cpp). See save.h, docs/save_port.md.
// The role record fields owned here: the two road nodes (Role +0x40 / +0x44: indices into Rm8Scene::rnodes, which is the original's TerraMgr+0 order - all 22
// roles of the 5 sample records stand on adjacent nodes), AI flag, status (+0x1B4), turn state (+0x1B8), the message queue (+0x5D0), vehicle (+0x22C), dice (+0x230).
#include "save.h"

int rm8SaveRolesCurrentRaw(){ return g_cur; }
int rm8SaveRolesOutfit(int seat){ return okSeat(seat) ? g_role[seat].color : 0; }

void rm8SaveRolesRoleGet(int seat, Rm8SaveRole &o)
{
	if(!okSeat(seat)) return;
	const Role &r = g_role[seat];
	o.p40 = r.from; o.p44 = r.to; o.ai = r.human ? 0 : r.autoPlay ? 2 : 1;	// [V] +0x1B0 is stored raw (CPlayerTag__SaveToStream @0x491fb0): 2 = a seat played by the computer
	o.status = r.intro ? 8 : r.status; o.state = r.state;	// [V] +0x1B4 == 8: placed at the game start, the introduction is still to come (r.intro)
	o.qa.clear(); o.qb.clear();
	for(const Role::Msg &m : r.queue){ Rm8SaveMsg q; q.cmd = m.cmd; q.a = m.a; q.b = m.b; o.qa.push_back(q); }
	o.vehicle = r.diceMax; o.dice = r.diceCount;
}

void rm8SaveRolesRoleSet(int seat, const Rm8SaveRole &o)
{
	if(!okSeat(seat) || g_nodes.empty()) return;
	Role &r = g_role[seat];
	int N = (int)g_nodes.size();
	int from = o.p40, to = o.p44;
	if(from < 0 || from >= N) from = r.from;
	if(to < 0 || to >= N) to = r.to;
	static const bool allAi = getenv("RM8_LOAD_ALLAI") != nullptr;	// test hook: a loaded game played by the computer only (long continuation runs)
	static const bool human0 = getenv("RM8_LOAD_HUMAN0") != nullptr;	// test hook: seat 0 is the human of a loaded computer-only record
	r.human = (o.ai == 0 && !allAi) || (human0 && seat == 0);
	r.autoPlay = o.ai == 2 && !allAi && !human0;	// [V] CPlayerTag__LoadFromStream @0x4923b0 reads +0x1B0 raw: 2 stays a seat played by the computer
	r.from = from; r.to = to; r.pos = nodePos(from); r.steps = 0;
	r.intro = o.status == 8; r.status = o.status == 8 ? 0 : o.status; r.state = o.state;
	r.queue.clear();
	for(const Rm8SaveMsg &q : o.qa) r.queue.push_back({q.cmd, q.a, q.b});
	r.waitT = 0; r.spinning = false; r.tw = false; r.twScale = r.twTScale = 1; r.twOff = r.twTOff = {0, 0, 0};
	int want = std::max(1, std::min(3, o.vehicle));
	if(want != r.diceMax) rm8TurnSetVehicle(seat, want);	// [V] Role_SetTrfTool @0x48a0c0 (model set + speed; the dice count follows)
	if(r.diceMax != want) diceSetMax(r, want);
	diceSetCount(r, std::max(1, std::min(want, o.dice)));	// [V] CPlayerTag__LoadFromStream @0x4923b0: Role_SetTrfTool(vehicle), then Dice_SetCount(count) (only when count <= max): the count is stored, the slot flags are rebuilt (slots 1..count on)
	if(r.from != r.to) calcAngle(r, nodePos(r.from), nodePos(r.to), false);
	playAnim(r, 2, false); updateAnim(r, 0); placeModel(r);
}

static int g_extLoop = -1;	// the role the saved frame was on (the record is written in the middle of the role loop) and that frame's time
static float g_extDt = 0;
static bool g_extRoles = false;	// a ROLE chunk was read: the placement below is the saved one, Game_MoveRoleToPosition is not run again
// [V] Game_LoadSavedGame @0x496a90: every role (from the one after the current to the current) gets Game_MoveRoleToPosition(node, pos, 1) and the icon of its node
// the approach effect; then the current role is either the last seat (the saved day wrap: Game_TurnNextRole starts the next day) or keeps its turn
bool rm8SaveRolesFinish(int cur)
{
	int n = (int)g_role.size();
	if(!n) return false;
	int c = cur >= n ? n - 1 : std::max(0, cur);
	if(!g_extRoles){	// an original record: the placement is recomputed
		g_reg.assign(n, -1);
		for(int i = 1; i <= n; i++){
			Role &r = g_role[(c + i) % n];
			if(r.status < 3){ moveRoleToPosition(r, r.from, r.pos, true); fxApproach(r, r.from, true); }
		}
	}else for(Role &r : g_role) if(r.status < 3) fxApproach(r, r.from, true);
	g_extRoles = false;
	for(Role &r : g_role){	// only the role whose turn it is may be in the dispatch / dice states
		if(r.seat != c || cur >= n){ if(r.state != 0) r.state = 0; continue; }
		if(r.state == 6 && !r.human){ r.state = 8; r.queue.insert(r.queue.begin(), {100, 0, 0}); }
		else if(r.state != 6) r.state = 8;
	}
	int loopSeat = g_extLoop; float dt = g_extDt;
	g_extLoop = -1;
	if(loopSeat >= 0){ g_loopSeat = loopSeat; g_frameDt = dt; }	// the record written again inside this call is that of the saved frame
	if(cur >= n){ g_cur = n - 1; turnNextRole(); }
	else{
		g_cur = c;
		Role &r = g_role[g_cur];
		setFocus(r.pos);
		g_arrow.pos = nodePos(r.from); g_arrow.pos.y += 10.0f;
		for(Role &q : g_role) updateAnim(q, 0);
	}
	if(loopSeat >= 0 && dt > 0){	// the rest of the frame the record was written in: the animation / idle timers of the role that wrote it and of the roles after it, then the frame tail
		for(Role &r : g_role){ if(r.seat < loopSeat) continue; g_loopSeat = r.seat; roleStep(r, dt, r.seat == loopSeat); }
		g_loopSeat = -1; frameTail(dt);
		rm8EventUpdate(dt); rm8StoryUpdate(dt);	// the calls that follow rm8RolesUpdate in the application loop (a weekly news task started by the new day first runs here)
	}
	g_loopSeat = -1;
	return true;
}

// extension chunk "ROLE": the roles' RNG stream (walking directions, idle actions), the idle timers that draw from it and - so that the dice are thrown from the very
// same place - the exact positions with the same-node placement (tween offset / scale, registered node) the original recomputes from the node indices alone
static int modelSetOf(const Role &r)
{
	for(int set = 0; set < 3; set++) if(r.def && r.m[0].art == r.def->art[set * 4]) return set;
	return 0;
}
// [V] Role_SetTrfTool @0x48a0c0: the four paper-doll models of another set
static void swapModelSet(Role &r, int set)
{
	if(!g_scene) return;
	Rm8Scene &s = *g_scene;
	for(int j = 0; j < 4; j++){
		Model nm;
		if(!loadModel(s, nm, r.def->art[set * 4 + j], r.def->id, r.color, j < 2)) continue;
		Model &om = r.m[j];
		if(om.clump){ s.world->removeClump(om.clump); om.clump->destroy(); }
		om = nm;
	}
	r.face.clear(); playAnim(r, r.anim, r.once); updateAnim(r, 0); placeModel(r);
}
static void saveRolesExtWrite(Rm8SaveW &w)
{
	w.u32(g_seed); w.i32((int)g_role.size());
	for(const Role &r : g_role){
		w.f32(r.idleT); w.f32(r.idleGap); w.i32(r.cur);
		w.f32(r.pos.x); w.f32(r.pos.y); w.f32(r.pos.z); w.i32(r.tw ? 1 : 0); w.f32(r.twScale); w.f32(r.twTScale);
		w.f32(r.twOff.x); w.f32(r.twOff.y); w.f32(r.twOff.z); w.f32(r.twTOff.x); w.f32(r.twTOff.y); w.f32(r.twTOff.z);
		w.i32(r.seat < (int)g_reg.size() ? g_reg[r.seat] : -1);
		// the animation in progress (an idle action / an expression pose freezes the idle timer, which draws from the RNG): pose, once, idle action, time
		const AnimInterpolator *ip = r.m[r.cur].hier ? r.m[r.cur].hier->interpolator : nullptr;
		w.i32(r.anim); w.i32(r.once ? 1 : 0); w.i32(r.special ? 1 : 0); w.f32(ip && ip->currentAnim && r.anim >= 3 ? ip->currentTime : 0.0f);	// only the poses that end (anim >= 3) have a time that matters
	}
	w.i32(g_loopSeat); w.f32(g_frameDt);
	// the turning of the doll (Anim_RotateSmooth: it blocks the state machine while it spins) and its wait timer
	for(const Role &r : g_role){ w.f32(r.yaw); w.i32(r.spinning ? 1 : 0); w.i32(r.spinTarget); w.f32(r.spinAngle); w.f32(r.spinRate); w.f32(r.waitT); }
	// the model set (roleinfo artId / artId2 / artId3: walking / bike / car) that is loaded: it follows the vehicle except while a role is possessed (Role_SetTrfTool leaves
	// the models alone then); the clips of the sets have different lengths, which the idle actions (and so the RNG) depend on
	for(const Role &r : g_role) w.i32(modelSetOf(r));	// where in the role loop of the frame the record is written (a day-wrap record is written by the role that ends its turn)
}
static void saveRolesExtRead(Rm8SaveR &rd)
{
	struct RS { float idleT = 0, gap = 15, px = 0, py = 0, pz = 0, ts = 1, tts = 1, ox = 0, oy = 0, oz = 0, tx = 0, ty = 0, tz = 0, atime = 0, yaw = 0, sa = 0, sr = 0, wt = 0;
	            int cur = 0, tw = 0, reg = -1, anim = 2, once = 0, special = 0, sp = 0, st = 0, set = -1; bool place = false, pose = false, spin = false; };
	g_seed = rd.u32();
	int n = rd.i32();
	g_extRoles = false;
	if(rd.bad || n < 0 || n > 8) return;
	std::vector<RS> v(n);
	for(int i = 0; i < n && !rd.bad; i++){
		RS &q = v[i];
		q.idleT = rd.f32(); q.gap = rd.f32(); q.cur = rd.i32();
		q.place = rd.opt(q.px) && rd.opt(q.py) && rd.opt(q.pz) && rd.opt(q.tw) && rd.opt(q.ts) && rd.opt(q.tts) && rd.opt(q.ox) && rd.opt(q.oy) && rd.opt(q.oz) && rd.opt(q.tx) && rd.opt(q.ty) && rd.opt(q.tz) && rd.opt(q.reg);
		q.pose = q.place && rd.opt(q.anim) && rd.opt(q.once) && rd.opt(q.special) && rd.opt(q.atime);
	}
	int ls = -1; float fdt = 0;
	if(rd.opt(ls) && rd.opt(fdt)){ g_extLoop = ls; g_extDt = fdt; }
	for(int i = 0; i < n; i++){ RS &q = v[i]; q.spin = rd.opt(q.yaw) && rd.opt(q.sp) && rd.opt(q.st) && rd.opt(q.sa) && rd.opt(q.sr) && rd.opt(q.wt); if(!q.spin) break; }
	for(int i = 0; i < n; i++) if(!rd.opt(v[i].set)) break;
	for(int i = 0; i < n && i < (int)g_role.size(); i++){
		Role &r = g_role[i]; RS &q = v[i];
		r.idleT = q.idleT; r.idleGap = q.gap > 0 ? q.gap : 15;
		if(q.set >= 0 && q.set < 3 && q.set != modelSetOf(r)) swapModelSet(r, q.set);	// the models first: the clips of the pose below belong to them
		if(q.cur >= 0 && q.cur < 4 && q.cur != r.cur){ r.cur = q.cur; r.face.clear(); playAnim(r, 2, false); updateAnim(r, 0); }
		if(q.place){
			r.pos = {q.px, q.py, q.pz}; r.tw = q.tw != 0; r.twScale = q.ts; r.twTScale = q.tts; r.twOff = {q.ox, q.oy, q.oz}; r.twTOff = {q.tx, q.ty, q.tz};
			if(i < (int)g_reg.size()) g_reg[i] = q.reg;
			g_extRoles = true;
		}
		if(q.pose){
			if(q.anim != 2){
				playAnim(r, q.anim, q.once != 0);
				AnimInterpolator *ip = r.m[r.cur].hier ? r.m[r.cur].hier->interpolator : nullptr;
				if(ip && ip->currentAnim && q.atime > 0) ip->addTime(q.atime);
				updateAnim(r, 0);
			}
			r.special = q.special != 0;
		}
		if(q.spin){ r.yaw = q.yaw; r.spinning = q.sp != 0; r.spinTarget = q.st; r.spinAngle = q.sa; r.spinRate = q.sr; r.waitT = q.wt; }
		placeModel(r);
	}
}
RM8_SAVE_EXT(ROLE, 20, saveRolesExtWrite, saveRolesExtRead);
