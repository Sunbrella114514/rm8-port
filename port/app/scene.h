// Shared RichMan8 scene code (desktop + Android): pack access, texture loading, scene building and rendering.
// Scenes come from scripts/export_scene.py (scene.txt + ground_LL.png).
#pragma once
#include <rw.h>
#include <string>
#include <vector>
#include <map>

void rm8Log(const char *fmt, ...);
// [A] folder of the user's own files (settings, profile, story box, save dir): desktop %APPDATA%/RM8 (one-time copy from the old place next to scenes/), RM8_DATA_DIR overrides, Android = the data dir
std::string rm8UserDataDir(const std::string &scenesRoot);
// [A] source.edition of manifest.json: "sc" = Steam (Simplified, GBK), "tc" = the old edition (Traditional, Big5), "" = unknown (no manifest)
void rm8SetDataEdition(const std::string &edition);
const std::string &rm8DataEdition();

// Register the RenderWare plugins the game's assets need (call after Engine::init, before Engine::open).
void rm8RegisterPlugins();
// Open Clump/clump0000.dat and Texture/tex0000.dat (+tex0001.dat) under gameDir.
bool rm8OpenPacks(const std::string &gameDir);
// Install the texture read callback (call after Engine::start).
void rm8InstallTextureCallback();
int rm8MissingTextures();

struct Rm8ArtAnim {
	rw::Clump *clump = nullptr;
	rw::HAnimHierarchy *hier = nullptr;
	std::string name;
	bool once = false;					// play once and hold the last frame (new-game buildings)
	std::vector<rw::Atomic*> cpuSkin;			// skinned atomics above the GPU bone limit, skinned on the CPU
	std::vector<std::vector<rw::V3d>> basePos, baseNrm;	// their bind-pose vertices / normals
};

struct Rm8CpuSkin { std::vector<rw::Atomic*> atoms; std::vector<std::vector<rw::V3d>> basePos, baseNrm; };	// skinned atomics above the GPU bone limit, skinned on the CPU
Rm8CpuSkin rm8CpuSkinPrepare(rw::Clump *c, rw::HAnimHierarchy *h);	// call after the hierarchy was attached / set on the skinned atomics
void rm8CpuSkinApply(Rm8CpuSkin &r, rw::HAnimHierarchy *h);		// after every hierarchy update

struct Rm8UVTarget {				// vertices of one geometry that use a UV-animated material
	rw::Geometry *geo = nullptr;
	rw::Material *mat = nullptr;
	std::vector<rw::int32> verts;
	std::vector<rw::TexCoords> base;
};

struct Rm8RoadNode { int tx, ty; float x, y, z; int road, event; int terra = -1; };
// board feedback (boardfx.cpp), from the "icon" / "aact" / "asnd" lines of scene.txt
struct Rm8IconTex { int action; float time; std::string material, texture; };	// role.bin timed material event: at `time` of `action` the material gets that texture
struct Rm8IconFx { int action; float time; int effect, hook, type; };			// role.bin timed effect event: at `time` of `action` particle effect `effect` starts on frame [@hook]
struct Rm8Icon { int tx, ty; rw::Clump *clump = nullptr; std::map<int, std::string> anims; std::vector<Rm8IconTex> tex; std::vector<Rm8IconFx> fx; };	// event icon on a road tile: role.bin actions 0..3 + their timed events
struct Rm8ArtSound { int action, sound, flag; float time; };					// role.bin sound event of an article action
struct Rm8ArtActs { std::map<int, std::string> anims; std::vector<Rm8ArtSound> sounds; };	// map article: actions (0 = the idle loop) and sound events
struct Rm8TerraDef { int type = 1, street = 0, level0 = 0; float pos[3] = {0, 0, 0}, yaw = 0; };	// plot (scripts/export_scene.py)
struct Rm8ArtDef { std::string dff, texdir; std::map<int, std::string> anims; };

struct Rm8Scene {
	rw::World *world = nullptr;
	std::vector<rw::Atomic*> atomics;	// ground leaves, roads, water
	std::vector<rw::Clump*> clumps;		// placed articles
	std::vector<std::string> clumpNames;
	std::vector<rw::Light*> lights;
	std::vector<Rm8ArtAnim> anims;		// animated articles (role.bin animation 0, looped)
	std::vector<rw::Material*> uvMats;	// UV-animated materials (each advanced once per frame)
	std::vector<Rm8UVTarget> uvTargets;
	std::vector<Rm8RoadNode> rnodes;	// road nodes (tiles with a road word), see roles.cpp
	std::vector<Rm8TerraDef> terras;	// land plots (game.cpp)
	std::map<std::pair<int,int>, int> buildingArt;	// (building type 1..4, level 1..5) -> art id
	std::map<int, int> streetPrice;		// street.bin ID -> street factor (%)
	std::map<int, std::string> streetName;	// street.bin ID -> name (UTF-8)
	std::map<int, std::string> buildingName;	// building.bin Id -> name (UTF-8)
	std::map<int, Rm8ArtDef> arts;		// models placed by the rules (houses, signs)
	struct PadRange { rw::Atomic *a; int first, count; };
	std::map<int, std::vector<PadRange>> terraPads;	// terra index -> its pad vertices (owner colour)
	std::vector<Rm8Icon> icons;		// event icons (boardfx.cpp)
	std::vector<rw::Clump*> mapArticles;	// the scenery articles of the map file (+0xC4 != 0 in the original: the ones that can turn see-through, boardfx.cpp)
	std::map<rw::Clump*, Rm8ArtActs> artActs;	// map articles with several actions (boardfx.cpp: click animations, sound events)
	std::map<rw::Clump*, int> artOwner;	// map article record [17] >= 0: the role the article belongs to ("aown" line; the chain-store sign art 0x45b, story.cpp)
	std::vector<std::pair<rw::Clump*, std::string>> hiddenArts;	// articles switched off with rm8SceneSetArticleActive (the original's +0xC4 == 0)
	int MW = 64, MH = 64;
	std::string mapId;		// scene directory name ("01" .. "11", "download201"..), for the board music (boardfx.cpp)
	int nLeaf = 0, nRoadQuads = 0, nArt = 0, nArtFail = 0, nSkinToCpu = 0, nAnimated = 0, nAnimFail = 0, nUVAnimated = 0, nPadQuads = 0, nCliffs = 0, nArtLm = 0, nPrt = 0;
};
// Pack access for other modules: a clump read with textures from texdir; a shared animation (owned by the cache);
// a texture with one reference owned by the caller.
rw::Clump *rm8LoadClump(const std::string &dff, const std::string &texdir);
rw::Animation *rm8LoadAnim(const std::string &key);
rw::Texture *rm8PackTexture(const std::string &key);
rw::Texture *rm8PackTextureLumaAlpha(const std::string &key);	// [A] the same, alpha from the brightest colour channel when the image has none (particle sprites with an INVSRCALPHA destination)
bool rm8PackRead(const std::string &key, std::vector<unsigned char> &out);	// raw texture pack entry (the minigames read alpha for hit tests)
bool rm8LoadScene(const std::string &sceneDir, Rm8Scene &s);
// add / remove a placed article at run time (houses, signs); anim "-" = none, mode loop/once/loopr
rw::Clump *rm8SceneAddArticle(Rm8Scene &s, const std::string &dff, const std::string &td, const float pos[3], const float scale[3], float ry,
                              const std::string &anim, const std::string &uva, const std::string &mode);
void rm8SceneRemoveArticle(Rm8Scene &s, rw::Clump *c);
void rm8SceneDetachArticle(Rm8Scene &s, rw::Clump *c);	// out of the scenery list only (the owner draws it, see present.h rm8FxSetOverlay)
// [V] CArticleMgr vf38 @0x43f9d0 sets the article's +0xC4 flag: an inactive article is not drawn (vf5 @0x43b210), not picked (vf28 @0x43e0e0) and not tested for see-through
void rm8SceneSetArticleActive(Rm8Scene &s, rw::Clump *c, bool on);
bool rm8SceneArticleAnim(Rm8Scene &s, rw::Clump *c, const std::string &anim, bool once);
// [V] Land_SetOwner @0x4eec20 -> CVertexBufObj vf28 @0x42c5c0: every vertex of the plot pad gets the colour
void rm8SceneSetTerraColor(Rm8Scene &s, int terra, rw::RGBA c);
// [V] BuildObj_SetAlpha @0x4efb20: the pad's vertex alpha (0x60 while a plot blinks, 0xFF normal)
void rm8SceneSetTerraAlpha(Rm8Scene &s, int terra, unsigned char alpha);
// [V] the blinking plot's building is modulated by (grey, grey, grey): 0x60 while it blinks, 255 restores (CArticleMgr vf22 @0x440ea0)
void rm8SceneTintArticle(rw::Clump *c, int grey);
// [V] Art_UpdateGlowAlpha @0x441740: an article between the camera and a role is drawn with this opacity (0.4 .. 1) in a later alpha-blended pass
void rm8SceneSetArticleOpacity(rw::Clump *c, float a);
float rm8SceneArticleOpacity(rw::Clump *c);
void rm8DestroyScene(Rm8Scene &s);
// Advance all article animations by dt seconds (call once per frame before rendering).
void rm8UpdateScene(Rm8Scene &s, float dt);
// Render between cam->beginUpdate() and cam->endUpdate().
void rm8RenderScene(Rm8Scene &s, bool deferGlass = false);	// deferGlass: the see-through articles wait for rm8RenderArticleGlass (after the roles)
// the placed articles only, in the game's two passes (atomic user-data render flags); used by the reflection too
void rm8RenderArticles(Rm8Scene &s, bool deferGlass = false);
void rm8RenderArticleGlass();	// the faded articles of the last rm8RenderArticles(deferGlass): z test, no z write, alpha 0.4 (call after the roles are drawn)
// [V] one clump through the article render callback (0x45c6a0 / CArticleMgr__vf5_SetRenderMode @0x43b210): user-data flags, pass 0 then pass 1 (npc.cpp: NPC figures, police car, ambulance)
void rm8RenderArticleClump(rw::Clump *c);
// Lightmap render path (lmpipe.cpp): register before Engine::start, init after.
void rm8LmRegister();
void rm8LmInit();
// terrainLit: the ground / road / cliff vertex buffers of the original are D3D lit with the vertex colour as the material (ambient + one directional light, multiplicative), not the additive RW prelit model
void rm8LmAttach(rw::Atomic *a, rw::Texture *lightmap, const float m[16], bool terrainLit = false);
void rm8LmClear();
void rm8LmSetClip(bool on, float y);
bool rm8LmHas(rw::Atomic *a);
// Water (water.cpp): planar reflection + caustic bump. rm8RenderReflection before the main camera's beginUpdate.
void rm8WaterRegister();
void rm8WaterInit();
void rm8WaterSetup(rw::Atomic *water, rw::Texture *sky, float y, float bump, const float dir[3], float speed, float lightScale, rw::Texture *(*load)(const std::string&));
void rm8WaterClear();
void rm8WaterUpdate(float dt);
void rm8RenderReflection(Rm8Scene &s, rw::Camera *main);
// Orbit camera around (tx, 0, tz); distFactor 1 = whole map.
void rm8PlaceCamera(rw::Camera *cam, const Rm8Scene &s, float yawDeg, float pitchDeg, float distFactor, float tx, float tz);
// Particle effects (particles.cpp), definitions from scripts/export_particles.py (scenes/particles.txt).
bool rm8PrtLoadDefs(const std::string &path);
bool rm8PrtAttach(int eff, rw::Clump *parent, int hook);	// emitter follows the parent's "[@hook]" frame
bool rm8PrtPlace(int eff, const float pos[3]);
// [A] emitter handles (index, -1 = failed) for the effect player; Stop ends the emission, live particles fade out
int rm8PrtPlaceH(int eff, const float pos[3]);
int rm8PrtAttachH(int eff, rw::Clump *parent, int hook);
void rm8PrtStopH(int idx);
void rm8PrtUpdate(float dt);
void rm8PrtRender(rw::Camera *cam);	// after the scene, before the characters
void rm8PrtClear();
int rm8PrtCount();
// Player characters (roles.cpp). Definitions come from scripts/export_roles.py (scenes/roles.txt).
bool rm8RolesLoadDefs(const std::string &rolesTxt);
// Spawn n characters (character ids from ids[], 1..16) on random road nodes of the scene; demo turns start automatically.
void rm8RolesSpawn(Rm8Scene &s, const std::vector<int> &ids, unsigned seed, int humanSeat = -1);
void rm8RolesClear(Rm8Scene &s);
// place one standing character (model 0 fl, 1 fr, 2 br, 3 bl; animation index) for comparison shots
void rm8RolesPlace(Rm8Scene &s, int id, const float pos[3], int model, int anim);
void rm8RolesUpdate(Rm8Scene &s, float dt);
// the dolls face the camera: call with the main camera before rm8RolesUpdate (game camera yaw is 135)
void rm8RolesSetCamera(rw::Camera *cam);
void rm8RolesRender();
// after all 3D: dice in the air / held by a front-facing thrower, and the 2D step counter while walking
void rm8RolesRenderLate(rw::Camera *cam);
// camera focus point set by the turn flow (instant snaps); returns a counter that changes on every snap
int rm8RolesFocus(float pos[3]);
// the human player presses the dice button (state 6); false if it is not their move
bool rm8RolesHumanRoll();
// Dice (dice.cpp)
void rm8DiceSeed(unsigned s);
void rm8DiceStart(const float pos[3], const float dicePos[3], int count, int forced);
int rm8DiceUpdate(float dt, rw::Camera *cam);
bool rm8DiceActive();
bool rm8DiceInHand();
void rm8DiceRender(rw::Camera *cam);
void rm8DiceClear();
// position of character k (for camera focus); false if none
bool rm8RolesGetPos(int k, float pos[3]);
// index of the character whose (demo) turn it is, -1 if none
int rm8RolesCurrent();
extern int rm8GameSpeed;	// init.txt "speed" (1..; game default 4): walking time scale (speed-1)/4+1
