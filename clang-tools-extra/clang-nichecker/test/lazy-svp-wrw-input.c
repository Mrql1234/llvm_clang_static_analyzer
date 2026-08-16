int state;

// priority 1
void interrupt_low(void) {
  state = 1;
  state = 2;
}

// priority 2
void interrupt_high(void) {
  int observed;
  observed = state;
}

int main(void) {
  return 0;
}
