// sound/coro.c -- SndCoro (coro.h).
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

#else
#include <pthread.h>
#include <semaphore.h>

struct SndCoro {
  pthread_t th;
  sem_t go, back;
  void (*fn)(void*);
  void* arg;
  int done, started, quit;
};

static void* threadMain(void* p) {
  SndCoro* c = (SndCoro*)p;
  sem_wait(&c->go);
  if (!c->quit) c->fn(c->arg);
  c->done = 1;
  sem_post(&c->back);
  return NULL;
}

SndCoro* SndCoroNew(void (*fn)(void*), void* arg) {
  SndCoro* c = (SndCoro*)calloc(1, sizeof *c);
  if (!c) return NULL;
  c->fn = fn; c->arg = arg;
  sem_init(&c->go, 0, 0); sem_init(&c->back, 0, 0);
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 1 << 20);
  if (pthread_create(&c->th, &at, threadMain, c)) { pthread_attr_destroy(&at); free(c); return NULL; }
  pthread_attr_destroy(&at);
  return c;
}
void SndCoroResume(SndCoro* c) {
  if (!c || c->done) return;
  sem_post(&c->go);
  sem_wait(&c->back);
}
void SndCoroYield(SndCoro* c) {
  sem_post(&c->back);
  sem_wait(&c->go);
  if (c->quit) { c->done = 1; sem_post(&c->back); pthread_exit(NULL); }
}
int SndCoroDone(const SndCoro* c) { return !c || c->done; }
void SndCoroFree(SndCoro* c) {
  if (!c) return;
  if (!c->done) { c->quit = 1; sem_post(&c->go); sem_wait(&c->back); }
  pthread_join(c->th, NULL);
  sem_destroy(&c->go); sem_destroy(&c->back);
  free(c);
}
#endif
