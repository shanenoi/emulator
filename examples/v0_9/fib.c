static volatile int fib_input = 10;

static int fib_recursive(int n) {
    if (n <= 1) {
        return n;
    }
    return fib_recursive(n - 1) + fib_recursive(n - 2);
}

int main(void) {
    return fib_recursive(fib_input);
}
