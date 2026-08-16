#include <pthread.h>

int state;

void interrupt_timer(void) {
  state = 2;
}

int main(void) {
  while (1) {
    state = 0;
  }
}
