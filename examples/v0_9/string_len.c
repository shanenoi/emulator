static const char message[] = "hello";

int main(void) {
    const char *p = message;
    int len = 0;
    while (*p != '\0') {
        len++;
        p++;
    }
    return len;
}
