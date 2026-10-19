// ============================================================================
// sound/coro.h -- a coroutine: the sound processor's program runs as one, so it can stop in the middle of a tick (at a
// port read the CPU has not decided yet) and go on when the CPU's time reaches it. Strict hand-over: exactly one side
// runs at a time, so it is as deterministic as a function call. Windows: fibers; elsewhere: a thread and semaphores.
// ============================================================================
#ifndef CT_SOUND_CORO_H
#define CT_SOUND_CORO_H

typedef struct SndCoro SndCoro;

SndCoro* SndCoroNew(void (*fn)(void* arg), void* arg);   // created suspended at fn's start
void     SndCoroResume(SndCoro* c);                     // run it until it yields or fn returns
void     SndCoroYield(SndCoro* c);                      // (inside) back to the resumer
int      SndCoroDone(const SndCoro* c);                 // fn returned
void     SndCoroFree(SndCoro* c);                       // a coroutine not done is abandoned (its stack freed)

#endif
