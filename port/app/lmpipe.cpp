// Lightmap render path for librw GL3: base texture x lighting x lightmap x 2, as the game's D3D9 stage setup
// (Render_DrawObjectWithLight @0x40ee40: stage n = lightmap, MODULATE2X, UV = world position x lightmap matrix).
// Used for terrain leaves (.ltm segment A) and articles (.ltm segment B). Works with GL3 and GLES.
#include "scene.h"
#include <unordered_map>
#include <cstring>
#include <cstdlib>
#include <cstdio>

using namespace rw;
using namespace rw::gl3;

namespace {

int32 u_lmMatrix = -1, u_clipY = -1, u_lmMul = -1;
float g_clip[4] = {0, 0, 0, 0};	// x = world y threshold, y = enabled (reflection pass)
Shader *shLm[4], *shSkinLm[4];	// [fullLight*2 + alphaTest]
gl3::ObjPipeline *defLmPipe, *skinLmPipe;
struct LM { Texture *tex; float m[16]; bool mul = false; };
std::unordered_map<Atomic*, LM> g_lm;

const char *lm_vert_src =
"uniform mat4 u_lmMatrix;\n"
"uniform vec4 u_lmMul;\n"
"VSIN(ATTRIB_POS)	vec3 in_pos;\n"
"VSOUT vec4 v_color;\n"
"VSOUT vec2 v_tex0;\n"
"VSOUT vec2 v_lm;\n"
"VSOUT float v_fog;\n"
"VSOUT float v_worldY;\n"
"void main(void){\n"
"#ifdef SKIN\n"
"	vec3 SkinVertex = vec3(0.0, 0.0, 0.0);\n"
"	vec3 SkinNormal = vec3(0.0, 0.0, 0.0);\n"
"	for(int i = 0; i < 4; i++){\n"
"		SkinVertex += (u_boneMatrices[int(in_indices[i])] * vec4(in_pos, 1.0)).xyz * in_weights[i];\n"
"		SkinNormal += (mat3(u_boneMatrices[int(in_indices[i])]) * in_normal) * in_weights[i];\n"
"	}\n"
"	vec4 Vertex = u_world * vec4(SkinVertex, 1.0);\n"
"	vec3 Normal = mat3(u_world) * SkinNormal;\n"
"#else\n"
"	vec4 Vertex = u_world * vec4(in_pos, 1.0);\n"
"	vec3 Normal = mat3(u_world) * in_normal;\n"
"#endif\n"
"	gl_Position = u_proj * u_view * Vertex;\n"
"	v_tex0 = in_tex0;\n"
"	v_lm = (u_lmMatrix * Vertex).xy;\n"
"	v_worldY = Vertex.y;\n"
"	v_color = in_color;\n"
"	if(u_lmMul.x > 0.5){\n"
"		v_color.rgb = in_color.rgb * (u_ambLight.rgb*surfAmbient + DoDynamicLight(Vertex.xyz, Normal)*surfDiffuse);\n"	// terrain: D3D fixed function, the vertex colour is the ambient / diffuse material
"	}else if(u_lmMul.x > -0.5){\n"
"		v_color.rgb += u_ambLight.rgb*surfAmbient;\n"
"		v_color.rgb += DoDynamicLight(Vertex.xyz, Normal)*surfDiffuse;\n"
"	}\n"
"	v_color = clamp(v_color, 0.0, 1.0);\n"
"	v_color *= u_matColor;\n"
"	v_fog = DoFog(gl_Position.w);\n"
"}\n";

const char *lm_frag_src =
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"
"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"
"uniform vec4 u_clipY;\n"
"FSIN vec2 v_lm;\n"
"FSIN float v_fog;\n"
"FSIN float v_worldY;\n"
"void main(void){\n"
"	if(u_clipY.y > 0.5 && v_worldY < u_clipY.x) discard;\n"
"	vec4 color = v_color*texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));\n"
"	vec3 lm = texture(tex1, vec2(v_lm.x, 1.0-v_lm.y)).rgb;\n"
"	color.rgb = clamp(color.rgb * lm * 2.0, 0.0, 1.0);\n"	// D3DTOP_MODULATE2X
"	color.rgb = mix(u_fogColor.rgb, color.rgb, v_fog);\n"
"	DoAlphaTest(color.a);\n"
"	FRAGCOLOR(color);\n"
"}\n";

void renderMeshes(Atomic *atomic, gl3::InstanceDataHeader *header, const LM &lm, Shader **set)
{
	uint32 flags = atomic->geometry->flags;
	setWorldMatrix(atomic->getFrame()->getLTM());
	int32 vsBits = lightingCB(atomic);
	setUniform(u_lmMatrix, (void*)lm.m);
	setUniform(u_clipY, g_clip);
	{ static const bool unlit = getenv("RM8_TERRAIN_UNLIT") != nullptr; float mv[4] = {lm.mul ? (unlit ? -1.0f : 1.0f) : 0.0f, 0, 0, 0}; setUniform(u_lmMul, mv); }	// RM8_TERRAIN_UNLIT=1: the old unlit terrain (comparison)
	setupVertexInput(header);
	if(set == shSkinLm) uploadSkinMatrices(atomic);
	setTexture(1, lm.tex);
	gl3::InstanceData *inst = header->inst;
	int32 n = header->numMeshes;
	while(n--){
		Material *m = inst->material;
		setMaterial(flags, m->color, m->surfaceProps);
		setTexture(0, m->texture);
		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);
		set[((vsBits & VSLIGHT_MASK) ? 2 : 0) + (getAlphaTest() ? 1 : 0)]->use();
		drawInst(header, inst);
		inst++;
	}
	setTexture(1, nil);
	teardownVertexInput(header);
}

void defLmRenderCB(Atomic *atomic, gl3::InstanceDataHeader *header)
{
	auto it = g_lm.find(atomic);
	if(it == g_lm.end()){ defaultRenderCB(atomic, header); return; }
	renderMeshes(atomic, header, it->second, shLm);
}

void skinLmRenderCB(Atomic *atomic, gl3::InstanceDataHeader *header)
{
	auto it = g_lm.find(atomic);
	if(it != g_lm.end()) renderMeshes(atomic, header, it->second, shSkinLm);
}

}	// namespace

void rm8LmRegister()
{
	// must happen before Engine::start: shaders created earlier would not know this uniform
	u_lmMatrix = registerUniform("u_lmMatrix", UNIFORM_MAT4);
	u_clipY = registerUniform("u_clipY", UNIFORM_VEC4);
	u_lmMul = registerUniform("u_lmMul", UNIFORM_VEC4);
}

void rm8LmInit()
{
	static char bonesDef[160];
	snprintf(bonesDef, sizeof(bonesDef), "#define RW_MAXBONES %d\nuniform mat4 u_boneMatrices[RW_MAXBONES];\n", (int)skinMaxBones);
	const char *lights = "#define DIRECTIONALS\n#define POINTLIGHTS\n#define SPOTLIGHTS\n";
	for(int k = 0; k < 4; k++){
		bool full = k >= 2, at = k & 1;
		const char *vs[] = { shaderDecl, full ? lights : "", header_vert_src, lm_vert_src, nil };
		const char *vss[] = { shaderDecl, "#define SKIN\n", bonesDef, full ? lights : "", header_vert_src, lm_vert_src, nil };
		const char *fs[] = { shaderDecl, at ? "" : "#define NO_ALPHATEST\n", header_frag_src, lm_frag_src, nil };
		shLm[k] = Shader::create(vs, fs);
		shSkinLm[k] = Shader::create(vss, fs);
		if(!shLm[k] || !shSkinLm[k]) rm8Log("lightmap shader %d failed to compile", k);
	}
	defLmPipe = makeDefaultPipeline();
	defLmPipe->renderCB = defLmRenderCB;
}

void rm8LmAttach(Atomic *a, Texture *tex, const float m[16], bool terrainLit)
{
	if(!tex || !shLm[0]) return;
	auto old = g_lm.find(a); if(old != g_lm.end()) old->second.tex->destroy();
	tex->addRef();
	LM lm; lm.tex = tex; memcpy(lm.m, m, sizeof lm.m); lm.mul = terrainLit;
	lm.m[3] = lm.m[7] = lm.m[11] = 0.0f; lm.m[15] = 1.0f;	// RwMatrix pad words hold garbage in the files
	g_lm[a] = lm;
	if(Skin::get(a->geometry) && a->pipeline){	// skinned (GPU): clone the skin pipeline's instancing, our render
		if(!skinLmPipe){
			gl3::ObjPipeline *sp = (gl3::ObjPipeline*)a->pipeline;
			skinLmPipe = gl3::ObjPipeline::create();
			skinLmPipe->instanceCB = sp->instanceCB;
			skinLmPipe->uninstanceCB = sp->uninstanceCB;
			skinLmPipe->pluginID = sp->pluginID;
			skinLmPipe->pluginData = sp->pluginData;
			skinLmPipe->renderCB = skinLmRenderCB;
		}
		a->pipeline = skinLmPipe;
	}else
		a->pipeline = defLmPipe;
}

bool rm8LmHas(Atomic *a){ return g_lm.count(a) != 0; }
void rm8LmSetClip(bool on, float y){ g_clip[0] = y; g_clip[1] = on ? 1.0f : 0.0f; }

void rm8LmClear(){ for(auto &e : g_lm) e.second.tex->destroy(); g_lm.clear(); }
