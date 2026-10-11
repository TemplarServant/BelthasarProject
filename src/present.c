// present.c - see present.h. The SDL_Renderer backend and the OpenGL 3.3 backend; the OpenGL functions are loaded
// through SDL_GL_GetProcAddress (no GL headers, no loader library, nothing linked).
#include "present.h"
#include "present_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- the OpenGL subset this needs ----------------------------------------------------------------------------------
#ifdef _WIN32
#define CTGL_API __stdcall
#else
#define CTGL_API
#endif
typedef unsigned int GLenum_; typedef unsigned int GLuint_; typedef int GLint_; typedef int GLsizei_;
typedef unsigned int GLbitfield_; typedef float GLfloat_;
#define CTGL_TEXTURE_2D 0x0DE1
#define CTGL_RGB 0x1907
#define CTGL_RGB8 0x8051
#define CTGL_UNSIGNED_BYTE 0x1401
#define CTGL_TEXTURE_MAG_FILTER 0x2800
#define CTGL_TEXTURE_MIN_FILTER 0x2801
#define CTGL_TEXTURE_WRAP_S 0x2802
#define CTGL_TEXTURE_WRAP_T 0x2803
#define CTGL_NEAREST 0x2600
#define CTGL_LINEAR 0x2601
#define CTGL_CLAMP_TO_EDGE 0x812F
#define CTGL_UNPACK_ALIGNMENT 0x0CF5
#define CTGL_PACK_ALIGNMENT 0x0D05
#define CTGL_READ_FRAMEBUFFER 0x8CA8
#define CTGL_DRAW_FRAMEBUFFER 0x8CA9
#define CTGL_COLOR_ATTACHMENT0 0x8CE0
#define CTGL_FRAMEBUFFER_COMPLETE 0x8CD5
#define CTGL_COLOR_BUFFER_BIT 0x4000
#define CTGL_SCISSOR_TEST 0x0C11
#define CTGL_BGRA 0x80E1
#define CTGL_BACK 0x0405
#define CTGL_VERSION 0x1F02
#define CTGL_RENDERER 0x1F01
static struct {
  void (CTGL_API* GenTextures)(GLsizei_, GLuint_*);
  void (CTGL_API* DeleteTextures)(GLsizei_, const GLuint_*);
  void (CTGL_API* BindTexture)(GLenum_, GLuint_);
  void (CTGL_API* TexParameteri)(GLenum_, GLenum_, GLint_);
  void (CTGL_API* TexImage2D)(GLenum_, GLint_, GLint_, GLsizei_, GLsizei_, GLint_, GLenum_, GLenum_, const void*);
  void (CTGL_API* TexSubImage2D)(GLenum_, GLint_, GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, const void*);
  void (CTGL_API* PixelStorei)(GLenum_, GLint_);
  void (CTGL_API* GenFramebuffers)(GLsizei_, GLuint_*);
  void (CTGL_API* DeleteFramebuffers)(GLsizei_, const GLuint_*);
  void (CTGL_API* BindFramebuffer)(GLenum_, GLuint_);
  void (CTGL_API* FramebufferTexture2D)(GLenum_, GLenum_, GLenum_, GLuint_, GLint_);
  GLenum_ (CTGL_API* CheckFramebufferStatus)(GLenum_);
  void (CTGL_API* BlitFramebuffer)(GLint_, GLint_, GLint_, GLint_, GLint_, GLint_, GLint_, GLint_, GLbitfield_, GLenum_);
  void (CTGL_API* ClearColor)(GLfloat_, GLfloat_, GLfloat_, GLfloat_);
  void (CTGL_API* Clear)(GLbitfield_);
  void (CTGL_API* Viewport)(GLint_, GLint_, GLsizei_, GLsizei_);
  void (CTGL_API* Disable)(GLenum_);
  void (CTGL_API* ReadBuffer)(GLenum_);
  void (CTGL_API* ReadPixels)(GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, void*);
  const unsigned char* (CTGL_API* GetString)(GLenum_);
} gl;
static int glLoad(void) {
#define L(name) if (!(*(void**)&gl.name = SDL_GL_GetProcAddress("gl" #name))) { fprintf(stderr, "[video] OpenGL: no gl%s\n", #name); return 0; }
  L(GenTextures) L(DeleteTextures) L(BindTexture) L(TexParameteri) L(TexImage2D) L(TexSubImage2D) L(PixelStorei)
  L(GenFramebuffers) L(DeleteFramebuffers) L(BindFramebuffer) L(FramebufferTexture2D) L(CheckFramebufferStatus)
  L(BlitFramebuffer) L(ClearColor) L(Clear) L(Viewport) L(Disable) L(ReadBuffer) L(ReadPixels) L(GetString)
#undef L
  return 1;
}

struct CtPresenter {
  int backend;
  SDL_Window* win;
  int picW, picH;
  char name[160];
  // SDL_Renderer
  SDL_Renderer* ren;
  SDL_Texture* tex;
  int logical;               // headless: the logical size does the placing
  int sdlLinear;             // the texture's current scale mode
  // Vulkan
  CtVk* vk;
  // OpenGL
  SDL_GLContext ctx;
  GLuint_ glTex, glFbo;
  int glLinear;
};

static int sdlStart(CtPresenter* p, const CtPresentConfig* c) {
  if (c->sdlDriver && *c->sdlDriver) SDL_SetHint(SDL_HINT_RENDER_DRIVER, c->sdlDriver);
  Uint32 rf = c->software ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | (c->vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
  p->ren = SDL_CreateRenderer(p->win, -1, rf);
  if (!p->ren) p->ren = SDL_CreateRenderer(p->win, -1, SDL_RENDERER_SOFTWARE);
  if (!p->ren) return 0;
  p->tex = SDL_CreateTexture(p->ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, p->picW, p->picH);
  if (!p->tex) return 0;
  p->logical = c->logical;
  if (p->logical) SDL_RenderSetLogicalSize(p->ren, p->picW, p->picH);
  SDL_SetTextureScaleMode(p->tex, SDL_ScaleModeNearest);
  SDL_RendererInfo ri; memset(&ri, 0, sizeof ri); SDL_GetRendererInfo(p->ren, &ri);
  snprintf(p->name, sizeof p->name, "SDL_Renderer (%s)", ri.name ? ri.name : "?");
  p->backend = CT_BACKEND_SDL;
  return 1;
}

static int glStart(CtPresenter* p, const CtPresentConfig* c) {
  p->ctx = SDL_GL_CreateContext(p->win);
  if (!p->ctx) { fprintf(stderr, "[video] OpenGL: no context (%s)\n", SDL_GetError()); return 0; }
  SDL_GL_MakeCurrent(p->win, p->ctx);
  if (!glLoad()) return 0;
  SDL_GL_SetSwapInterval(c->vsync ? 1 : 0);
  gl.GenTextures(1, &p->glTex);
  gl.BindTexture(CTGL_TEXTURE_2D, p->glTex);
  gl.TexParameteri(CTGL_TEXTURE_2D, CTGL_TEXTURE_WRAP_S, CTGL_CLAMP_TO_EDGE);
  gl.TexParameteri(CTGL_TEXTURE_2D, CTGL_TEXTURE_WRAP_T, CTGL_CLAMP_TO_EDGE);
  gl.TexParameteri(CTGL_TEXTURE_2D, CTGL_TEXTURE_MIN_FILTER, CTGL_NEAREST);
  gl.TexParameteri(CTGL_TEXTURE_2D, CTGL_TEXTURE_MAG_FILTER, CTGL_NEAREST);
  gl.PixelStorei(CTGL_UNPACK_ALIGNMENT, 1);
  gl.TexImage2D(CTGL_TEXTURE_2D, 0, CTGL_RGB8, p->picW, p->picH, 0, CTGL_RGB, CTGL_UNSIGNED_BYTE, NULL);
  gl.GenFramebuffers(1, &p->glFbo);                     // the frame as a READ framebuffer: one blit places it
  gl.BindFramebuffer(CTGL_READ_FRAMEBUFFER, p->glFbo);
  gl.FramebufferTexture2D(CTGL_READ_FRAMEBUFFER, CTGL_COLOR_ATTACHMENT0, CTGL_TEXTURE_2D, p->glTex, 0);
  if (gl.CheckFramebufferStatus(CTGL_READ_FRAMEBUFFER) != CTGL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "[video] OpenGL: the frame's framebuffer is incomplete\n"); return 0; }
  gl.BindFramebuffer(CTGL_READ_FRAMEBUFFER, 0);
  const char* ver = (const char*)gl.GetString(CTGL_VERSION), *rn = (const char*)gl.GetString(CTGL_RENDERER);
  snprintf(p->name, sizeof p->name, "OpenGL %s (%s)", ver ? ver : "?", rn ? rn : "?");
  p->backend = CT_BACKEND_OPENGL;
  return 1;
}
static void glStop(CtPresenter* p) {
  if (p->ctx) {
    if (p->glFbo) gl.DeleteFramebuffers(1, &p->glFbo);
    if (p->glTex) gl.DeleteTextures(1, &p->glTex);
    SDL_GL_DeleteContext(p->ctx);
  }
  p->ctx = NULL; p->glFbo = p->glTex = 0;
}

static SDL_Window* makeWindow(const CtPresentConfig* c, Uint32 api) {
  Uint32 f = SDL_WINDOW_RESIZABLE | api;
  if (c->fullscreen) f |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  if (c->hidden) f |= SDL_WINDOW_HIDDEN;
  return SDL_CreateWindow(c->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, c->w, c->h, f);
}

CtPresenter* CtPresentCreate(const CtPresentConfig* c) {
  CtPresenter* p = (CtPresenter*)calloc(1, sizeof *p);
  if (!p) return NULL;
  p->picW = c->picW; p->picH = c->picH;
  if (c->backend == CT_BACKEND_OPENGL && !c->software) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    p->win = makeWindow(c, SDL_WINDOW_OPENGL);
    if (p->win && glStart(p, c)) return p;
    fprintf(stderr, "[video] OpenGL unavailable -- SDL_Renderer instead\n");
    glStop(p);
    if (p->win) SDL_DestroyWindow(p->win);
    p->win = NULL;
  } else if (c->backend == CT_BACKEND_VULKAN && !c->software) {
#ifdef CT_HAVE_VULKAN
    p->win = makeWindow(c, SDL_WINDOW_VULKAN);
    if (p->win && CtVkStart(p->win, c->vsync, p->picW, p->picH, &p->vk)) {
      snprintf(p->name, sizeof p->name, "%s", CtVkName(p->vk));
      p->backend = CT_BACKEND_VULKAN;
      return p;
    }
    fprintf(stderr, "[video] Vulkan unavailable -- SDL_Renderer instead\n");
    if (p->win) SDL_DestroyWindow(p->win);
    p->win = NULL;
#else
    fprintf(stderr, "[video] built without Vulkan (tools/get_vulkan_headers.ps1) -- SDL_Renderer instead\n");
#endif
  }
  p->win = makeWindow(c, 0);
  if (!p->win || !sdlStart(p, c)) { CtPresentDestroy(p); return NULL; }
  return p;
}

void CtPresentDestroy(CtPresenter* p) {
  if (!p) return;
#ifdef CT_HAVE_VULKAN
  if (p->vk) CtVkStop(p->vk);
#endif
  glStop(p);
  if (p->tex) SDL_DestroyTexture(p->tex);
  if (p->ren) SDL_DestroyRenderer(p->ren);
  if (p->win) SDL_DestroyWindow(p->win);
  free(p);
}

SDL_Window*   CtPresentWindow(CtPresenter* p) { return p ? p->win : NULL; }
const char*   CtPresentName(const CtPresenter* p) { return p ? p->name : ""; }
SDL_Renderer* CtPresentSdlRenderer(CtPresenter* p) { return p && p->backend == CT_BACKEND_SDL ? p->ren : NULL; }
void*         CtPresentGlContext(CtPresenter* p) { return p && p->backend == CT_BACKEND_OPENGL ? p->ctx : NULL; }
struct CtVk*  CtPresentVk(CtPresenter* p) { return p && p->backend == CT_BACKEND_VULKAN ? p->vk : NULL; }

void CtPresentUpload(CtPresenter* p, const uint8_t* rgb) {
  if (!p) return;
#ifdef CT_HAVE_VULKAN
  if (p->backend == CT_BACKEND_VULKAN) { CtVkUpload(p->vk, rgb); return; }
#endif
  if (p->backend == CT_BACKEND_OPENGL) {
    gl.BindTexture(CTGL_TEXTURE_2D, p->glTex);
    gl.PixelStorei(CTGL_UNPACK_ALIGNMENT, 1);
    gl.TexSubImage2D(CTGL_TEXTURE_2D, 0, 0, 0, p->picW, p->picH, CTGL_RGB, CTGL_UNSIGNED_BYTE, rgb);
  } else SDL_UpdateTexture(p->tex, NULL, rgb, p->picW * 3);
}

void CtPresentSetPicSize(CtPresenter* p, int w, int h) {
  if (!p || w <= 0 || h <= 0 || (w == p->picW && h == p->picH)) return;
#ifdef CT_HAVE_VULKAN
  if (p->backend == CT_BACKEND_VULKAN) { if (CtVkSetPicSize(p->vk, w, h)) { p->picW = w; p->picH = h; } return; }
#endif
  if (p->backend == CT_BACKEND_OPENGL) {
    gl.BindTexture(CTGL_TEXTURE_2D, p->glTex);
    gl.TexImage2D(CTGL_TEXTURE_2D, 0, CTGL_RGB8, w, h, 0, CTGL_RGB, CTGL_UNSIGNED_BYTE, NULL);
    p->picW = w; p->picH = h;
    return;
  }
  SDL_Texture* t = SDL_CreateTexture(p->ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
  if (!t) return;
  SDL_DestroyTexture(p->tex); p->tex = t;
  SDL_SetTextureScaleMode(p->tex, p->sdlLinear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
  if (p->logical) SDL_RenderSetLogicalSize(p->ren, w, h);
  p->picW = w; p->picH = h;
}

void CtPresentOutputSize(CtPresenter* p, int* w, int* h) {
  *w = p ? p->picW : 0; *h = p ? p->picH : 0;
  if (!p) return;
#ifdef CT_HAVE_VULKAN
  if (p->backend == CT_BACKEND_VULKAN) { CtVkOutputSize(p->vk, w, h); return; }
#endif
  if (p->backend == CT_BACKEND_OPENGL) SDL_GL_GetDrawableSize(p->win, w, h);
  else SDL_GetRendererOutputSize(p->ren, w, h);
}

void CtPresentDraw(CtPresenter* p, const SDL_Rect* dst, int linear) {
  if (!p) return;
#ifdef CT_HAVE_VULKAN
  if (p->backend == CT_BACKEND_VULKAN) { CtVkDraw(p->vk, dst, linear); return; }
#endif
  if (p->backend == CT_BACKEND_OPENGL) {
    int ow, oh; SDL_GL_GetDrawableSize(p->win, &ow, &oh);
    gl.BindFramebuffer(CTGL_DRAW_FRAMEBUFFER, 0);
    gl.Disable(CTGL_SCISSOR_TEST);
    gl.Viewport(0, 0, ow, oh);
    gl.ClearColor(0, 0, 0, 1); gl.Clear(CTGL_COLOR_BUFFER_BIT);
    SDL_Rect d = dst ? *dst : (SDL_Rect){ 0, 0, ow, oh };
    gl.BindFramebuffer(CTGL_READ_FRAMEBUFFER, p->glFbo);
    // GL's rows count up from the bottom: the frame's row 0 (its top) goes to the rectangle's top edge, oh - d.y
    gl.BlitFramebuffer(0, 0, p->picW, p->picH, d.x, oh - d.y, d.x + d.w, oh - d.y - d.h, CTGL_COLOR_BUFFER_BIT,
                       linear ? CTGL_LINEAR : CTGL_NEAREST);
    gl.BindFramebuffer(CTGL_READ_FRAMEBUFFER, 0);
    return;
  }
  if (linear != p->sdlLinear) { SDL_SetTextureScaleMode(p->tex, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest); p->sdlLinear = linear; }
  SDL_SetRenderDrawColor(p->ren, 0, 0, 0, 255);
  SDL_RenderClear(p->ren);
  SDL_RenderCopy(p->ren, p->tex, NULL, (dst && !p->logical) ? dst : NULL);
}

void CtPresentSwap(CtPresenter* p) {
  if (!p) return;
#ifdef CT_HAVE_VULKAN
  if (p->backend == CT_BACKEND_VULKAN) { CtVkSwap(p->vk); return; }
#endif
  if (p->backend == CT_BACKEND_OPENGL) SDL_GL_SwapWindow(p->win);
  else SDL_RenderPresent(p->ren);
}

void CtPresentSetVsync(CtPresenter* p, int on) {
  if (!p) return;
#ifdef CT_HAVE_VULKAN
  if (p->backend == CT_BACKEND_VULKAN) { CtVkSetVsync(p->vk, on); return; }
#endif
  if (p->backend == CT_BACKEND_OPENGL) SDL_GL_SetSwapInterval(on ? 1 : 0);
  else SDL_RenderSetVSync(p->ren, on ? 1 : 0);
}

int CtPresentSaveBMP(CtPresenter* p, const char* path) {
  if (!p) return 0;
#ifdef CT_HAVE_VULKAN
  if (p->backend == CT_BACKEND_VULKAN) return CtVkSaveBMP(p->vk, path);
#endif
  int ow, oh; CtPresentOutputSize(p, &ow, &oh);
  SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!sf) return 0;
  int ok = 0;
  if (p->backend == CT_BACKEND_OPENGL) {
    uint8_t* tmp = (uint8_t*)malloc((size_t)ow * oh * 4);
    if (tmp) {
      gl.BindFramebuffer(CTGL_READ_FRAMEBUFFER, 0);
      gl.ReadBuffer(CTGL_BACK);
      gl.PixelStorei(CTGL_PACK_ALIGNMENT, 4);
      gl.ReadPixels(0, 0, ow, oh, CTGL_BGRA, CTGL_UNSIGNED_BYTE, tmp);
      for (int y = 0; y < oh; y++)                       // GL's bottom row first
        memcpy((uint8_t*)sf->pixels + (size_t)y * sf->pitch, tmp + (size_t)(oh - 1 - y) * ow * 4, (size_t)ow * 4);
      free(tmp); ok = 1;
    }
  } else ok = SDL_RenderReadPixels(p->ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch) == 0;
  if (ok) ok = SDL_SaveBMP(sf, path) == 0;
  SDL_FreeSurface(sf);
  return ok;
}
