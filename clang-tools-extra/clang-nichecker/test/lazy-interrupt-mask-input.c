extern void disable_isr(int priority);
extern void enable_isr(int priority);

int state;

// priority 1
void interrupt_low(void) {
  state = 1;
}

// priority 2
void interrupt_high(void) {
  state = 2;
}

int main(void) {
  disable_isr(-1);
  enable_isr(-1);
  state = 0;
  return 0;
}
