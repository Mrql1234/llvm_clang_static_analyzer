#include <pthread.h>
#include <time.h>

extern void __VERIFIER_error(void);
extern int pthread_cond_wait_1(pthread_cond_t *, pthread_mutex_t *);
extern int pthread_cond_wait_2(pthread_cond_t *, pthread_mutex_t *);
extern int pthread_barrier_wait_1(pthread_barrier_t *);
extern int pthread_barrier_wait_2(pthread_barrier_t *);

int increment(int value) { return value + 1; }
int decrement(int value) { return value - 1; }

int dispatch(int (*operation)(int), int value) {
  int result;
  result = operation(value);
  return result;
}

void waits(pthread_cond_t *condition, pthread_mutex_t *mutex,
           pthread_barrier_t *barrier, const struct timespec *deadline) {
  pthread_cond_wait(condition, mutex);
  if (pthread_cond_timedwait(condition, mutex, deadline))
    pthread_barrier_wait(barrier);
}

void reach_error(void) {
  goto ERROR;
ERROR:
  return;
}
