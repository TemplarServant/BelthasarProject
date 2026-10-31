// sound/coro.c -- SndCoro (coro.h): Windows' fibers; elsewhere ucontext's makecontext / swapcontext (Linux, macOS); on
// Android (bionic has no ucontext) a switch of its own on arm64 and x86_64, the two ABIs the APK carries, and on any
// other ABI a thread taking turns with its resumer.
#if !defined(_WIN32) && !defined(__ANDROID__)
#define _XOPEN_SOURCE 700   // (macOS declares the ucontext routines only with it; before any header)
#endif
#include "sound/coro.h"

#include <stdlib.h>

// CT_SND_CORO_ASM (a build define): the Android switch on another x86_64 / arm64 system -- how it is checked on a desktop.
#if (defined(__ANDROID__) || defined(CT_SND_CORO_ASM)) && (defined(__aarch64__) || defined(__x86_64__)) && !defined(_WIN32)
#define SND_CORO_SWITCH 1
#endif

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

#elif defined(SND_CORO_SWITCH)
// The switch does what a fiber or swapcontext does (the callee-saved registers pushed on the running stack, its pointer
// stored, the other stack's taken and its registers popped) with no system call. A thread taking turns costs two
// wake-ups a switch, tens of microseconds on a phone, and a song's upload makes ~10,000 switches (one per handshake),
// enough to stall the picture.
// ct_snd_switch(save, to): *save = this side's stack pointer, then run on `to` until switched back.
// ct_snd_start: where a new coroutine's first switch returns, with the SndCoro in x19 / rbx.
struct SndCoro {
  void* sp;         // the coroutine's stack pointer while it is suspended
  void* callerSp;   // the resumer's while the coroutine runs
  void* stack;
  void (*fn)(void*);
  void* arg;
  int done;
};
#define SND_STACK (1 << 20)
__attribute__((visibility("hidden"))) void ct_snd_switch(void** save, void* to);
__attribute__((visibility("hidden"))) void ct_snd_start(void);
__attribute__((visibility("hidden"), used, noreturn)) void ct_snd_main(SndCoro* c) {
  c->fn(c->arg);
  c->done = 1;
  for (;;) ct_snd_switch(&c->sp, c->callerSp);
}
#if defined(__aarch64__)
// AAPCS64: x19-x28, x29 (frame), x30 (link) and d8-d15 are the callee's to keep; 160 bytes, sp 16-aligned throughout.
__asm__(
  ".text\n.p2align 2\n"
  ".globl ct_snd_switch\n.hidden ct_snd_switch\n.type ct_snd_switch, %function\n"
  "ct_snd_switch:\n"
  "  sub sp, sp, #160\n"
  "  stp x19, x20, [sp, #0]\n  stp x21, x22, [sp, #16]\n  stp x23, x24, [sp, #32]\n  stp x25, x26, [sp, #48]\n"
  "  stp x27, x28, [sp, #64]\n  stp x29, x30, [sp, #80]\n  stp d8, d9, [sp, #96]\n  stp d10, d11, [sp, #112]\n"
  "  stp d12, d13, [sp, #128]\n  stp d14, d15, [sp, #144]\n"
  "  mov x9, sp\n  str x9, [x0]\n  mov sp, x1\n"
  "  ldp x19, x20, [sp, #0]\n  ldp x21, x22, [sp, #16]\n  ldp x23, x24, [sp, #32]\n  ldp x25, x26, [sp, #48]\n"
  "  ldp x27, x28, [sp, #64]\n  ldp x29, x30, [sp, #80]\n  ldp d8, d9, [sp, #96]\n  ldp d10, d11, [sp, #112]\n"
  "  ldp d12, d13, [sp, #128]\n  ldp d14, d15, [sp, #144]\n"
  "  add sp, sp, #160\n  ret\n"
  ".size ct_snd_switch, .-ct_snd_switch\n"
  ".globl ct_snd_start\n.hidden ct_snd_start\n.type ct_snd_start, %function\n"
  "ct_snd_start:\n  mov x0, x19\n  bl ct_snd_main\n  brk #0\n"
  ".size ct_snd_start, .-ct_snd_start\n");
enum { SND_FRAME = 160 };
static void sndFrame(SndCoro* c, void** f) {   // x19 = the coroutine, x29 = 0 (the chain's end), x30 = ct_snd_start
  f[0] = c; f[10] = NULL; f[11] = (void*)ct_snd_start;
}
#else
// System V x86_64: rbx, rbp, r12-r15, and the SSE / x87 control words (MXCSR, FCW). After the `ret` that pops the
// return address the stack pointer is the frame's top, 16-aligned, so ct_snd_start's `call` keeps the ABI's alignment.
__asm__(
  ".text\n.p2align 4\n"
  ".globl ct_snd_switch\n.hidden ct_snd_switch\n.type ct_snd_switch, @function\n"
  "ct_snd_switch:\n"
  "  pushq %rbp\n  pushq %rbx\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
  "  subq $8, %rsp\n  stmxcsr (%rsp)\n  fnstcw 4(%rsp)\n"
  "  movq %rsp, (%rdi)\n  movq %rsi, %rsp\n"
  "  ldmxcsr (%rsp)\n  fldcw 4(%rsp)\n  addq $8, %rsp\n"
  "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbx\n  popq %rbp\n  ret\n"
  ".size ct_snd_switch, .-ct_snd_switch\n"
  ".globl ct_snd_start\n.hidden ct_snd_start\n.type ct_snd_start, @function\n"
  "ct_snd_start:\n  movq %rbx, %rdi\n  call ct_snd_main\n  ud2\n"
  ".size ct_snd_start, .-ct_snd_start\n");
enum { SND_FRAME = 64 };
static void sndFrame(SndCoro* c, void** f) {   // [MXCSR | FCW] r15 r14 r13 r12 rbx rbp, then the return address
  ((unsigned*)f)[0] = 0x1F80; ((unsigned short*)f)[2] = 0x037F;   // (the ABI's initial control words)
  f[5] = c; f[6] = NULL; f[7] = (void*)ct_snd_start;
}
#endif
SndCoro* SndCoroNew(void (*fn)(void*), void* arg) {
  SndCoro* c = (SndCoro*)calloc(1, sizeof *c);
  if (!c) return NULL;
  c->fn = fn; c->arg = arg;
  c->stack = malloc(SND_STACK);
  if (!c->stack) { free(c); return NULL; }
  char* top = (char*)(((size_t)c->stack + SND_STACK) & ~(size_t)15);
  void** f = (void**)(top - SND_FRAME);
  for (int i = 0; i < SND_FRAME / (int)sizeof(void*); i++) f[i] = NULL;
  sndFrame(c, f);
  c->sp = f;
  return c;
}
void SndCoroResume(SndCoro* c) {
  if (!c || c->done) return;
  ct_snd_switch(&c->callerSp, c->sp);
}
void SndCoroYield(SndCoro* c) { ct_snd_switch(&c->sp, c->callerSp); }
int SndCoroDone(const SndCoro* c) { return !c || c->done; }
void SndCoroFree(SndCoro* c) { if (!c) return; free(c->stack); free(c); }   // (a program never left: its stack dropped)

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
