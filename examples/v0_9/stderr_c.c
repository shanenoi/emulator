static const char message[] = "error from c\n";

static long fake_write(long fd, const char *buffer, unsigned long length) {
    register long x0 asm("x0") = fd;
    register const char *x1 asm("x1") = buffer;
    register unsigned long x2 asm("x2") = length;
    register long x8 asm("x8") = 64;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

int main(void) {
    return (int)(fake_write(2, message, sizeof(message) - 1u) - (long)(sizeof(message) - 1u));
}
