static volatile const char message[] = "tiny c";

int main(void) {
    int len = 0;
    while (message[len] != '\0') {
        len++;
    }
    return len;
}
