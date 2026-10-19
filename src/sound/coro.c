// sound/coro.c -- SndCoro (coro.h): Windows' fibers; elsewhere ucontext's makecontext / swapcontext (Linux, macOS); on
// Android (no ucontext before API 28) a thread taking turns with its resumer.
#if !defined(_WIN32) && !defined(__ANDROID__)
#define _XOPEN_SOURCE 700   // (macOS declares the ucontext routines only with it; before any header)
#endif
#include "sound/coro.h"

#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct SndCoro {
  void* fiber;
  void* caller;
  void (*fn)(void*);
  void* arg;
  int done;
};

static void WINAPI fiberMain(void* p) {
  SndCoro* c = (SndCoro*)p;
  c->fn(c->arg);
  c->done = 1;
  for (;;) SwitchToFiber(c->caller);
}

SndCoro* SndCoroNew(void (*fn)(void*), void* arg) {
  SndCoro* c = (SndCoro*)calloc(1, sizeof *c);
  if (!c) return NULL;
  c->fn = fn; c->arg = arg;
  c->fiber = CreateFiber(1 << 20, fiberMain, c);
  if (!c->fiber) { free(c); return NULL; }
  return c;
}
void SndCoroResume(SndCoro* c) {
  if (!c || c->done) return;
  if (!IsThreadAFiber()) ConvertThreadToFiber(NULL);
  c->caller = GetCurrentFiber();
  SwitchToFiber(c->fiber);
}
void SndCoroYield(SndCoro* c) { SwitchToFiber(c->caller); }
int SndCoroDone(const SndCoro* c) { return !c || c->done; }
void SndCoroFree(SndCoro* c) { if (!c) return; DeleteFiber(c->fiber); free(c); }

#elif !defined(__ANDROID__)
#include <ucontext.h>

struct SndCoro {
  ucontext_t ctx, caller;
  void* stack;
  void (*fn)(void*);
  void* arg;
  int done, begun;
};

static SndCoro* g_starting;   // (makecontext passes ints only: the coroutine being entered, read once at its start)
static void ctxMain(void) {
  SndCoro* c = g_starting;
  c->fn(c->arg);
  c->done = 1;
  for (;;) swapcontext(&c->ctx, &c->caller);
}

static int ctxMake(SndCoro* c, void* stack, size_t n) {   // (getcontext returns once here: nothing after it to clobber)
  if (getcontext(&c->ctx)) return 0;
  c->ctx.uc_stack.ss_sp = stack;
  c->ctx.uc_stack.ss_size = n;
  c->ctx.uc_link = NULL;
  makecontext(&c->ctx, ctxMain, 0);
  return 1;
}
SndCoro* SndCoroNew(void (*fn)(void*), void* arg) {
  SndCoro* c = (SndCoro*)calloc(1, sizeof *c);
  if (!c) return NULL;
  c->fn = fn; c->arg = arg;
  c->stack = malloc(1 << 20);
  if (!c->stack || !ctxMake(c, c->stack, 1 << 20)) { free(c->stack); free(c); return NULL; }
  return c;
}
void SndCoroResume(SndCoro* c) {
  if (!c || c->done) return;
  if (!c->begun) { c->begun = 1; g_starting = c; }
  swapcontext(&c->caller, &c->ctx);
}
void SndCoroYield(SndCoro* c) { swapcontext(&c->ctx, &c->caller); }
int SndCoroDone(const SndCoro* c) { return !c || c->done; }
void SndCoroFree(SndCoro* c) { if (!c) return; free(c->stack); free(c); }   // (a program never left: its stack dropped)

#else
#include <pthread.h>

// A thread that takes turns with its resumer: a mutex, a condition and whose turn it is (`turn`: 1 the coroutine's).
// (POSIX's unnamed semaphores are not everywhere: a condition it is.)
struct SndCoro {
  pthread_t th;
  pthread_mutex_t mu;
  pthread_cond_t cv;
  int turn;
  void (*fn)(void*);
  void* arg;
  int done, quit;
};

static void waitTurn(SndCoro* c, int want) {   // (mu held)
  while (c->turn != want) pthread_cond_wait(&c->cv, &c->mu);
}
static void* threadMain(void* p) {
  SndCoro* c = (SndCoro*)p;
  pthread_mutex_lock(&c->mu);
  waitTurn(c, 1);
  int quit = c->quit;
  pthread_mutex_unlock(&c->mu);
  if (!quit) c->fn(c->arg);
  pthread_mutex_lock(&c->mu);
  c->done = 1;
  c->turn = 0;
  pthread_cond_broadcast(&c->cv);
  pthread_mutex_unlock(&c->mu);
  return NULL;
}

SndCoro* SndCoroNew(void (*fn)(void*), void* arg) {
  SndCoro* c = (SndCoro*)calloc(1, sizeof *c);
  if (!c) return NULL;
  c->fn = fn; c->arg = arg;
  pthread_mutex_init(&c->mu, NULL);
  pthread_cond_init(&c->cv, NULL);
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 1 << 20);
  if (pthread_create(&c->th, &at, threadMain, c)) {
    pthread_attr_destroy(&at); pthread_cond_destroy(&c->cv); pthread_mutex_destroy(&c->mu); free(c); return NULL;
  }
  pthread_attr_destroy(&at);
  return c;
}
void SndCoroResume(SndCoro* c) {
  if (!c || c->done) return;
  pthread_mutex_lock(&c->mu);
  c->turn = 1;
  pthread_cond_broadcast(&c->cv);
  waitTurn(c, 0);
  pthread_mutex_unlock(&c->mu);
}
void SndCoroYield(SndCoro* c) {
  pthread_mutex_lock(&c->mu);
  c->turn = 0;
  pthread_cond_broadcast(&c->cv);
  waitTurn(c, 1);
  int quit = c->quit;
  pthread_mutex_unlock(&c->mu);
  if (quit) {
    pthread_mutex_lock(&c->mu);
    c->done = 1; c->turn = 0;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->mu);
    pthread_exit(NULL);
  }
}
int SndCoroDone(const SndCoro* c) { return !c || c->done; }
void SndCoroFree(SndCoro* c) {
  if (!c) return;
  if (!c->done) {   // abandoned mid-program: woken to leave
    pthread_mutex_lock(&c->mu);
    c->quit = 1; c->turn = 1;
    pthread_cond_broadcast(&c->cv);
    waitTurn(c, 0);
    pthread_mutex_unlock(&c->mu);
  }
  pthread_join(c->th, NULL);
  pthread_cond_destroy(&c->cv); pthread_mutex_destroy(&c->mu);
  free(c);
}
#endif
