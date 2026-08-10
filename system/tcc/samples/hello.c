int printf(const char* Format, ...);
void debug(const char* Format, ...);

int main(void) {
    printf("Hello from a TinyCC-compiled EXOS program\n");
    debug("TCC_GLOBAL_HELLO:Hello from a TinyCC-compiled EXOS program");
    return 0;
}
