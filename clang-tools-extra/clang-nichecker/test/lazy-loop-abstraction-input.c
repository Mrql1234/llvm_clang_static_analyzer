extern int nondet_int(void);
extern void __CPROVER_assume(int);

int reset_or_increment(int limit);
int increment_from_initial(int limit);

int step_to_bound(int limit) {
  int value = 0;
  while (value < limit) {
    value++;
  }
  return value;
}

int main(void) {
  return step_to_bound(10) + reset_or_increment(8) + increment_from_initial(5);
}

int reset_or_increment(int limit) {
  int value = 0;
  for (int index = 0; index < limit; index++) {
    if (index == 3)
      value = 7;
    else
      value += 2;
  }
  return value;
}

int increment_from_initial(int limit) {
  int i = 0;
  int initial = 2;
  while (i < limit) {
    i += initial;
  }
  return i;
}
