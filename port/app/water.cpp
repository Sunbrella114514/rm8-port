// Water: planar reflection + caustic bump distortion, after CWater_Render @0x477e90:
//   stage 0  BUMPENVMAP with water\caustNN (32 frames, 0.03 s each), texcoords = camera-space position x texture matrix
//            (scale 0.01/0.05, translation dt*speed*dir), bump matrix = map value (0.04)
//   stage 1  reflection camera render target (CWater_CreateCamera 512x512), projected, MODULATE diffuse, alpha = diffuse
//   The reflection camera mirrors the main camera about the water plane (CWater_UpdateLighting @0x477bb0); behind the
//   mirrored scene it shows the sky map water\entironmentN.
// [A] the reflection pass draws the sky and the articles only (no clip plane, so the terrain is left out).
#include "scene.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>

using namespace rw;
using namespace rw::gl3;

namespace {

int32 u_reflVP = -1, u_bumpParam = -1;
Shader *shWater;
gl3::ObjPipeline *waterPipe;
Camera *reflCam;
Texture *reflTex;
Texture *caust[32];
float reflVP[16];
float bumpParam[4];
float bumpScale = 0.04f;
float g_time;

const char *water_vert_src =
"uniform mat4 u_reflVP;\n"
"uniform vec4 u_bumpParam;\n"
"VSIN(ATTRIB_POS)	vec3 in_pos;\n"
"VSOUT vec4 v_color;\n"
"VSOUT vec2 v_bump;\n"
"VSOUT vec4 v_refl;\n"
"VSOUT float v_fog;\n"
"void main(void){\n"
"	vec4 Vertex = u_world * vec4(in_pos, 1.0);\n"
"	vec4 Cam = u_view * Vertex;\n"
"	gl_Position = u_proj * Cam;\n"
"	v_bump = vec2(Cam.x*u_bumpParam.x, Cam.y*u_bumpParam.y) + u_bumpParam.zw;\n"
"	v_refl = u_reflVP * Vertex;\n"
"	v_color = in_color * u_matColor;\n"
"	v_fog = DoFog(gl_Position.w);\n"
"}\n";

const char *water_frag_src =
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"
"uniform vec4 u_waterBump;\n"
"FSIN vec4 v_color;\n"
"FSIN vec2 v_bump;\n"
"FSIN vec4 v_refl;\n"
"FSIN float v_fog;\n"
"void main(void){\n"
"	vec2 b = texture(tex0, v_bump).rg * 2.0 - 1.0;\n"
"	vec2 uv = v_refl.xy / v_refl.w * 0.5 + 0.5 + b * u_waterBump.x;\n"
"	vec4 r = texture(tex1, uv);\n"
"	vec4 color = vec4(r.rgb * v_color.rgb, v_color.a);\n"
"	color.rgb = mix(u_fogColor.rgb, color.rgb, v_fog);\n"
"	FRAGCOLOR(color);\n"
"}\n";

int32 u_waterBump = -1;

void mul44(float *o, const float *a, const float *b)	// column-major o = a * b
{
	for(int c = 0; c < 4; c++) for(int r = 0; r < 4; r++){
		float s = 0; for(int k = 0; k < 4; k++) s += a[k*4 + r] * b[c*4 + k];
		o[c*4 + r] = s;
	}
}

void waterRenderCB(Atomic *atomic, gl3::InstanceDataHeader *header)
{
	uint32 flags = atomic->geometry->flags;
	setWorldMatrix(atomic->getFrame()->getLTM());
	lightingCB(atomic);
	setUniform(u_reflVP, reflVP);
	setUniform(u_bumpParam, bumpParam);
	float wb[4] = { bumpScale, 0, 0, 0 };
	setUniform(u_waterBump, wb);
	setupVertexInput(header);
	gl3::InstanceData *inst = header->inst;
	int32 n = header->numMeshes;
	while(n--){
		Material *m = inst->material;
		setMaterial(flags, m->color, m->surfaceProps);
		setTexture(0, caust[(int)(g_time / 0.03f) & 31]);
		setTexture(1, reflTex);
		rw::SetRenderState(VERTEXALPHA, 1);
		shWater->use();
		drawInst(header, inst);
		inst++;
	}
	setTexture(1, nil);
	teardownVertexInput(header);
}

}	// namespace

void rm8WaterRegister()
{
	u_reflVP = registerUniform("u_reflVP", UNIFORM_MAT4);
	u_bumpParam = registerUniform("u_bumpParam", UNIFORM_VEC4);
	u_waterBump = registerUniform("u_waterBump", UNIFORM_VEC4);
}

void rm8WaterInit()
{
	const char *vs[] = { shaderDecl, header_vert_src, water_vert_src, nil };
	const char *fs[] = { shaderDecl, "#define NO_ALPHATEST\n", header_frag_src, water_frag_src, nil };
	shWater = Shader::create(vs, fs);
	if(!shWater) rm8Log("water shader failed to compile");
	waterPipe = makeDefaultPipeline();
	waterPipe->renderCB = waterRenderCB;
}

// state per scene
static Atomic *g_water;
static Texture *g_sky;
static float g_waterY;
static float g_flow[3], g_speed;
static float g_bumpOff[2];
static float g_lightScale = 1.0f;	// [V] map water +0x18 -> CWater +0x578: light colours x this in the reflection

void rm8WaterSetup(Atomic *water, Texture *sky, float y, float bump, const float dir[3], float speed, float lightScale, Texture *(*load)(const std::string&))
{
	g_lightScale = lightScale;
	g_water = water; g_sky = sky; g_waterY = y; bumpScale = bump; g_speed = speed;
	memcpy(g_flow, dir, sizeof g_flow); g_bumpOff[0] = g_bumpOff[1] = 0;
	for(int i = 0; i < 32 && !caust[i]; i++){
		char key[64]; snprintf(key, sizeof key, "water\\caust%02d.tga", i);
		caust[i] = load(key);
	}
	if(water && shWater) water->pipeline = waterPipe;
}

void rm8WaterClear(){ g_water = nullptr; if(g_sky) g_sky->destroy(); g_sky = nullptr; }

void rm8WaterUpdate(float dt)
{
	// [V] CWater_UpdateAnimation: caustic frame every 0.03 s; texture matrix translated by dt*speed*dir
	g_time += dt;
	g_bumpOff[0] += dt * g_speed * g_flow[0];
	g_bumpOff[1] += dt * g_speed * g_flow[1];
}

void rm8RenderReflection(Rm8Scene &s, Camera *main)
{
	if(!g_water || !shWater) return;
	if(!reflCam){
		reflCam = Camera::create();
		reflCam->setFrame(Frame::create());
		reflCam->frameBuffer = Raster::create(512, 512, 32, Raster::CAMERATEXTURE | Raster::C8888);
		reflCam->zBuffer = Raster::create(512, 512, 0, Raster::ZBUFFER);
		reflTex = Texture::create(reflCam->frameBuffer);
		reflTex->setFilter(Texture::LINEAR);
		reflTex->setAddressU(Texture::CLAMP); reflTex->setAddressV(Texture::CLAMP);
	}
	// mirror the main camera about y = water height (rows: right, up, at, pos with y negated; pos.y -> 2h - y)
	Matrix m = *main->getFrame()->getLTM();
	m.right.y = -m.right.y; m.up.y = -m.up.y; m.at.y = -m.at.y; m.pos.y = 2.0f * g_waterY - m.pos.y;
	m.flags = 0;
	reflCam->getFrame()->transform(&m, COMBINEREPLACE);
	reflCam->setViewWindow(&main->viewWindow);
	reflCam->setNearPlane(main->nearPlane); reflCam->setFarPlane(main->farPlane);
	World *w = main->world;
	if(reflCam->world != w){ if(reflCam->world) reflCam->world->removeCamera(reflCam); w->addCamera(reflCam); }

	RGBA sky = {90, 140, 200, 255};
	reflCam->clear(&sky, Camera::CLEARIMAGE | Camera::CLEARZ);
	reflCam->beginUpdate();
	if(g_sky){	// sky map behind everything, scrolled with the camera position ([A] simplified CWater_UpdateLighting)
		Im2DVertex v[4]; uint16 idx[6] = {0, 1, 2, 2, 1, 3};
		float z = im2d::GetFarZ(), su = main->getFrame()->getLTM()->pos.x / 2000.0f, sv = main->getFrame()->getLTM()->pos.z / 2000.0f;
		for(int i = 0; i < 4; i++){
			v[i].setScreenX(i % 2 ? 512.0f : 0.0f); v[i].setScreenY(i / 2 ? 512.0f : 0.0f); v[i].setScreenZ(z);
			v[i].setCameraZ(reflCam->farPlane); v[i].setRecipCameraZ(1.0f / reflCam->farPlane);
			v[i].setColor(255, 255, 255, 255);
			v[i].setU((i % 2) + su, 1.0f / reflCam->farPlane); v[i].setV((i / 2) + sv, 1.0f / reflCam->farPlane);
		}
		SetRenderStatePtr(TEXTURERASTER, g_sky->raster);
		SetRenderState(TEXTUREADDRESS, Texture::WRAP);
		SetRenderState(ZTESTENABLE, 0); SetRenderState(ZWRITEENABLE, 0);
		im2d::RenderIndexedPrimitive(PRIMTYPETRILIST, v, 4, idx, 6);
		SetRenderState(ZTESTENABLE, 1); SetRenderState(ZWRITEENABLE, 1);
	}
	// [V] CWater_RenderReflection @0x478340: ambient and directional light colours scaled by the map's light scale
	std::vector<std::pair<Light*, RGBAf>> saved;
	for(Light *l : s.lights){ saved.push_back({l, l->color}); l->setColor(l->color.red * g_lightScale, l->color.green * g_lightScale, l->color.blue * g_lightScale); }
	uint32 cull = GetRenderState(CULLMODE);
	SetRenderState(CULLMODE, CULLFRONT);	// the mirror flips the winding
	rm8RenderArticles(s);
	rm8PrtRender(reflCam);	// [V] particles are drawn into the water reflection too (0x478340)
	rm8LmSetClip(true, g_waterY);	// terrain (leaves, cliffs) above the water only: the lightmap shader discards below
	for(Atomic *a : s.atomics) if(a != g_water && rm8LmHas(a)) a->render();
	rm8LmSetClip(false, 0);
	SetRenderState(CULLMODE, cull);
	reflCam->endUpdate();
	for(auto &sl : saved) sl.first->setColor(sl.second.red, sl.second.green, sl.second.blue);
	mul44(reflVP, (float*)&reflCam->devProj, (float*)&reflCam->devView);
	bumpParam[0] = 0.01f; bumpParam[1] = 0.05f;
	bumpParam[2] = g_bumpOff[0]; bumpParam[3] = g_bumpOff[1];
}

