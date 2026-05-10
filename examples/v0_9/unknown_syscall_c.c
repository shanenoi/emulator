static long fake_unknown(void) {
    register long x0 asm("x0") = 0;
    register long x8 asm("x8") = 999;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

int main(void) {
    return (int)(fake_unknown() + 38);
}
