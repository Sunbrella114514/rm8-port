"""RM8 patches to librw for the Android (GLES) build. Idempotent: run again = no-op.
1) SDL2 backend: really create the GL context inside the profile loop and fall back when it fails;
   GLES-only on Android, RW_GL_FORCE_GLES=1 forces GLES on desktop (to test the phone path on PC).
   Adds GLES 3.0 (#version 300 es) next to 3.1 and 2.0.
2) Skinning: bone array size injected at runtime (RW_MAXBONES) from GL_MAX_VERTEX_UNIFORM_VECTORS,
   exported as rw::gl3::skinMaxBones; RW_SKIN_MAXBONES=n overrides (to simulate small phone GPUs).
   Bone buffer 64 -> 256 (game models use up to 194 bones).
3) readTGAMemory: decode TGA data already in memory.
usage: python -I apply_librw_android.py <librw dir>
"""
import os, sys

root = sys.argv[1]
def patch(rel, pairs):
    p = os.path.join(root, rel)
    s = open(p, encoding='utf-8').read()
    changed = False
    for old, new in pairs:
        if new in s:
            continue
        assert s.count(old) >= 1, (rel, old[:80])
        s = s.replace(old, new, 1)
        changed = True
    if changed:
        open(p, 'w', encoding='utf-8', newline='').write(s)
    print(rel, 'patched' if changed else 'already patched')

T = '\t'
# ---------------- gl3device.cpp ----------------
old_loop = (
"\tint i;\n"
"\tfor(i = 0; profiles[i].gl; i++){\n"
"\t\tSDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profiles[i].gl);\n"
"\t\tSDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, profiles[i].major);\n"
"\t\tSDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, profiles[i].minor);\n"
"\n"
"\t\tif(mode->flags & VIDEOMODEEXCLUSIVE) {\n"
"\t\t\twin = SDL_CreateWindow(glGlobals.winTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, mode->mode.w, mode->mode.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);\n"
"\t\t\tif (win)\n"
"\t\t\t\tSDL_SetWindowDisplayMode(win, &mode->mode);\n"
"\t\t} else {\n"
"\t\t\twin = SDL_CreateWindow(glGlobals.winTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, glGlobals.winWidth, glGlobals.winHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);\n"
"\t\t\tif (win)\n"
"\t\t\t\tSDL_SetWindowDisplayMode(win, NULL);\n"
"\t\t}\n"
"\t\tif(win){\n"
"\t\t\tgl3Caps.gles = profiles[i].gl == SDL_GL_CONTEXT_PROFILE_ES;\n"
"\t\t\tgl3Caps.glversion = profiles[i].major*10 + profiles[i].minor;\n"
"\t\t\tbreak;\n"
"\t\t}\n"
"\t}\n"
"\tif(win == nil){\n"
"\t\tRWERROR((ERR_GENERAL, SDL_GetError()));\n"
"\t\treturn 0;\n"
"\t}\n"
"\tctx = SDL_GL_CreateContext(win);\n")
new_loop = (
"\tint i;\n"
"\t// RM8 patch: GLES only on Android (RW_GL_FORCE_GLES=1 does the same on desktop), and actually\n"
"\t// create the context inside the loop so that a failing profile falls through to the next one.\n"
"\tint first = 0;\n"
"\tconst char *forceGles = getenv(\"RW_GL_FORCE_GLES\");\t// =30 / =20 also skips newer GLES versions (testing)\n"
"#ifndef __ANDROID__\n"
"\tif(forceGles)\n"
"#endif\n"
"\t\twhile(profiles[first].gl && profiles[first].gl != SDL_GL_CONTEXT_PROFILE_ES) first++;\n"
"\tif(forceGles && atoi(forceGles) >= 20)\n"
"\t\twhile(profiles[first].gl && profiles[first].major*10 + profiles[first].minor > atoi(forceGles)) first++;\n"
"\twin = nil;\n"
"\tctx = nil;\n"
"\tfor(i = first; profiles[i].gl; i++){\n"
"\t\tSDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profiles[i].gl);\n"
"\t\tSDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, profiles[i].major);\n"
"\t\tSDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, profiles[i].minor);\n"
"\n"
"\t\tif(mode->flags & VIDEOMODEEXCLUSIVE) {\n"
"\t\t\twin = SDL_CreateWindow(glGlobals.winTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, mode->mode.w, mode->mode.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);\n"
"\t\t\tif (win)\n"
"\t\t\t\tSDL_SetWindowDisplayMode(win, &mode->mode);\n"
"\t\t} else {\n"
"\t\t\twin = SDL_CreateWindow(glGlobals.winTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, glGlobals.winWidth, glGlobals.winHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);\n"
"\t\t\tif (win)\n"
"\t\t\t\tSDL_SetWindowDisplayMode(win, NULL);\n"
"\t\t}\n"
"\t\tif(win){\n"
"\t\t\tctx = SDL_GL_CreateContext(win);\n"
"\t\t\tif(ctx){\n"
"\t\t\t\tgl3Caps.gles = profiles[i].gl == SDL_GL_CONTEXT_PROFILE_ES;\n"
"\t\t\t\tgl3Caps.glversion = profiles[i].major*10 + profiles[i].minor;\n"
"\t\t\t\tbreak;\n"
"\t\t\t}\n"
"\t\t\tSDL_DestroyWindow(win);\n"
"\t\t\twin = nil;\n"
"\t\t}\n"
"\t}\n"
"\tif(win == nil){\n"
"\t\tRWERROR((ERR_GENERAL, SDL_GetError()));\n"
"\t\treturn 0;\n"
"\t}\n")
old_prof = (
"\t{ SDL_GL_CONTEXT_PROFILE_ES, 3, 1 },\n"
"\t{ SDL_GL_CONTEXT_PROFILE_ES, 2, 0 },\n")
new_prof = (
"\t{ SDL_GL_CONTEXT_PROFILE_ES, 3, 1 },\n"
"\t{ SDL_GL_CONTEXT_PROFILE_ES, 3, 0 },\t// RM8 patch\n"
"\t{ SDL_GL_CONTEXT_PROFILE_ES, 2, 0 },\n")
old_decl = "const char *shaderDecl310es =\n"
new_decl = (
"const char *shaderDecl300es =\t// RM8 patch: GLES 3.0 devices\n"
"\"#version 300 es\\n\"\n"
"\"#define VSIN(index) layout(location = index) in\\n\"\n"
"\"#define VSOUT out\\n\"\n"
"\"#define FSIN in\\n\"\n"
"\"#define FRAGCOLOR(c) (fragColor = c)\\n\"\n"
"\"precision highp float;\\n\"\n"
"\"precision highp int;\\n\";\n"
"const char *shaderDecl310es =\n")
old_sel = (
"\tif(gl3Caps.gles){\n"
"\t\tif(gl3Caps.glversion >= 30)\n"
"\t\t\tshaderDecl = shaderDecl310es;\n")
new_sel = (
"\tif(gl3Caps.gles){\n"
"\t\tif(gl3Caps.glversion == 30)\t// RM8 patch\n"
"\t\t\tshaderDecl = shaderDecl300es;\n"
"\t\telse if(gl3Caps.glversion >= 30)\n"
"\t\t\tshaderDecl = shaderDecl310es;\n")
patch(os.path.join('src', 'gl', 'gl3device.cpp'), [(old_loop, new_loop), (old_prof, new_prof), (old_decl, new_decl), (old_sel, new_sel)])

# ---------------- skinning ----------------
old_open = "\tconst char *vs[] = { shaderDecl, header_vert_src, skin_vert_src, nil };\n\tconst char *vs_fullLight[] = { shaderDecl, \"#define DIRECTIONALS\\n#define POINTLIGHTS\\n#define SPOTLIGHTS\\n\", header_vert_src, skin_vert_src, nil };\n"
new_open = (
"\t// RM8 patch: size the bone array from the GPU's vertex uniform budget (Adreno GLES: 256 vec4).\n"
"\t// About 60 vec4 are used by header.vert (matrices, 4 x 8 light arrays, material), keep 64 spare.\n"
"\tGLint vecs = 0;\n"
"\tglGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vecs);\n"
"\tif(glGetError() != GL_NO_ERROR || vecs <= 0){\n"
"\t\tGLint comps = 0;\n"
"\t\tglGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &comps);\n"
"\t\tvecs = comps / 4;\n"
"\t}\n"
"\tint32 mb = (vecs - 64) / 4;\n"
"\tif(mb > RM8_MAXBONES) mb = RM8_MAXBONES;\n"
"\tif(mb < 16) mb = 16;\n"
"\tif(const char *e = getenv(\"RW_SKIN_MAXBONES\")){ int v = atoi(e); if(v >= 8 && v <= RM8_MAXBONES) mb = v; }\n"
"\tskinMaxBones = mb;\n"
"\tstatic char bonesDef[48];\n"
"\tsnprintf(bonesDef, sizeof(bonesDef), \"#define RW_MAXBONES %d\\n\", (int)mb);\n"
"\tconst char *vs[] = { shaderDecl, bonesDef, header_vert_src, skin_vert_src, nil };\n"
"\tconst char *vs_fullLight[] = { shaderDecl, bonesDef, \"#define DIRECTIONALS\\n#define POINTLIGHTS\\n#define SPOTLIGHTS\\n\", header_vert_src, skin_vert_src, nil };\n")
old_buf = "static float skinMatrices[64*16];\n"
new_buf = ("#define RM8_MAXBONES 256\t// RM8 patch: was 64, game models use up to 194 bones\n"
           "static float skinMatrices[RM8_MAXBONES*16];\n"
           "int32 skinMaxBones = 64;\t// RM8 patch: set in skinOpen from the GPU limits\n")
patch(os.path.join('src', 'gl', 'gl3skin.cpp'), [
    (old_buf, new_buf), (old_open, new_open),
    ('registerUniform("u_boneMatrices", UNIFORM_MAT4, 64)', 'registerUniform("u_boneMatrices", UNIFORM_MAT4, RM8_MAXBONES)'),
    ('\t\tassert(skin->numBones == hier->numNodes);\n', '\t\tassert(skin->numBones == hier->numNodes);\n\t\tassert(hier->numNodes <= RM8_MAXBONES);\n'),
    ('\t\tfor(i = 0; i < skin->numBones; i++){\n\t\t\tm->setIdentity();', '\t\tfor(i = 0; i < skin->numBones && i < RM8_MAXBONES; i++){\n\t\t\tm->setIdentity();'),
])
patch(os.path.join('src', 'gl', 'rwgl3plg.h'), [("void uploadSkinMatrices(Atomic *atomic);\n",
      "void uploadSkinMatrices(Atomic *atomic);\nextern int32 skinMaxBones;\t// RM8 patch: max bones the GPU skin shader supports (models above need a CPU path)\n")])
# the uniform data pool must hold 256 bone matrices (4096 floats) plus the other uniforms (release builds overflow silently)
patch(os.path.join('src', 'gl', 'gl3shader.cpp'), [("static float uniformData[512*4];\t// seems enough\n",
      "static float uniformData[2048*4];\t// RM8 patch: was 512*4, too small for 256 bone matrices\n")])
for f in (os.path.join('src', 'gl', 'shaders', 'skin.vert'), os.path.join('src', 'gl', 'shaders', 'skin_gl.inc')):
    patch(f, [("uniform mat4 u_boneMatrices[64];", "uniform mat4 u_boneMatrices[RW_MAXBONES];")])

# ---------------- 3) TGA from memory (no temp files on Android) ----------------
patch(os.path.join('src', 'tga.cpp'), [
    ("Image*\nreadTGA(const char *filename)\n{\n\tTGAHeader header;\n\tImage *image;\n\tint depth = 0, palDepth = 0;\n\tuint32 length;\n\tuint8 *data = getFileContents(filename, &length);\n\tassert(data != nil);\n\tStreamMemory file;",
     "// RM8 patch: readTGA split so that TGA data already in memory can be decoded (caller keeps ownership)\n"
     "Image*\nreadTGA(const char *filename)\n{\n\tuint32 length;\n\tuint8 *data = getFileContents(filename, &length);\n\tassert(data != nil);\n"
     "\tImage *image = readTGAMemory(data, length);\n\trwFree(data);\n\treturn image;\n}\n\n"
     "Image*\nreadTGAMemory(uint8 *data, uint32 length)\n{\n\tTGAHeader header;\n\tImage *image;\n\tint depth = 0, palDepth = 0;\n\tStreamMemory file;"),
    ("\tfile.close();\n\trwFree(data);\n\treturn image;\n}\n\nvoid\nwriteTGA(",
     "\tfile.close();\n\treturn image;\n}\n\nvoid\nwriteTGA("),
])
patch(os.path.join('src', 'rwobjects.h'), [("Image *readTGA(const char *filename);\n",
      "Image *readTGA(const char *filename);\nImage *readTGAMemory(uint8 *data, uint32 length);\t// RM8 patch\n")])

# ---------------- 4) CMake: Android has no desktop OpenGL package; link GLESv3 + EGL ----------------
patch(os.path.join('src', 'CMakeLists.txt'), [
    ("    set(OpenGL_GL_PREFERENCE GLVND)\n    find_package(OpenGL)\n",
     "    set(OpenGL_GL_PREFERENCE GLVND)\n    if(ANDROID)\t# RM8 patch\n        target_link_libraries(librw PRIVATE GLESv3 EGL)\n    else()\n    find_package(OpenGL)\n"),
    ("        message(FATAL_ERROR \"find_package(OpenGL) failed.\")\n    endif()\n",
     "        message(FATAL_ERROR \"find_package(OpenGL) failed.\")\n    endif()\n    endif()\t# RM8 patch (ANDROID)\n"),
])

# ---------------- 5) 24-bit depth buffer (SDL defaults to 16 bits; the paper-doll characters' parts are a few
# units apart and z-fight at a distance of 800 with 16 bits). Both SDL window paths.
p = os.path.join(root, 'src', 'gl', 'gl3device.cpp'); s = open(p, encoding='utf-8').read()
old = "\tSDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, glGlobals.numSamples);\n"
new = old + "\tSDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);\t// RM8 patch\n"
if new not in s:
    assert s.count(old) == 2
    s = s.replace(old, new); open(p, 'w', encoding='utf-8', newline='').write(s); print('gl3device.cpp depth 24 patched')
else: print('gl3device.cpp depth 24 already patched')

# ---------------- 6) immediate-mode uploads sized to the data: the stock code orphans the whole 10000-element
# buffers (280 KB for Im2D) before every draw, which costs ~0.1 ms per call on phone drivers (the UI makes dozens)
p = os.path.join(root, 'src', 'gl', 'gl3immed.cpp'); s = open(p, encoding='utf-8').read()
reps = [
    ("glBufferData(GL_ARRAY_BUFFER, STARTVERTICES*sizeof(Im2DVertex), nil, GL_STREAM_DRAW);\n\tglBufferSubData(GL_ARRAY_BUFFER, 0, numVertices*sizeof(Im2DVertex), vertices);",
     "glBufferData(GL_ARRAY_BUFFER, numVertices*sizeof(Im2DVertex), vertices, GL_STREAM_DRAW);\t// RM8 patch"),
    ("glBufferData(GL_ELEMENT_ARRAY_BUFFER, STARTINDICES*2, nil, GL_STREAM_DRAW);\n\tglBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, numIndices*2, indices);",
     "glBufferData(GL_ELEMENT_ARRAY_BUFFER, numIndices*2, indices, GL_STREAM_DRAW);\t// RM8 patch"),
    ("glBufferData(GL_ARRAY_BUFFER, STARTVERTICES*sizeof(Im3DVertex), nil, GL_STREAM_DRAW);\n\tglBufferSubData(GL_ARRAY_BUFFER, 0, numVertices*sizeof(Im3DVertex), vertices);",
     "glBufferData(GL_ARRAY_BUFFER, numVertices*sizeof(Im3DVertex), vertices, GL_STREAM_DRAW);\t// RM8 patch"),
]
n = 0
for old, new in reps:
    n += s.count(old); s = s.replace(old, new)
if n:
    open(p, 'w', encoding='utf-8', newline='').write(s)
print('gl3immed.cpp sized uploads:', n, 'replaced')
