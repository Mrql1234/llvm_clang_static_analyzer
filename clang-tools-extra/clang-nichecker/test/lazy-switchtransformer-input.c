int choose(int value, int *result) {
  switch (value) {
  case 0:
    *result = 10;
  case 1:
    while (*result < 12) {
      ++*result;
      break;
    }
    break;
  case 2:
    *result = 20;
  default:
    *result += 1;
  }
  return *result;
}
