void *memcpy_tork(void *dst, const void *src, unsigned long n) {
    unsigned long i;
    for (i = 0; i < n; i++)
        ((unsigned char*)dst)[i] = ((unsigned char*)src)[i];
    return dst;
}
