int helper(int value) {
  if (value > 0)
    goto done;
  value += 10;
done:
  return value + 1;
}

int main(void) {
  int result;
  result = helper(4);
  result = helper(result - 5);
  return result;
}
