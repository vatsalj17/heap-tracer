#include <stdlib.h>

int main(void) {
    void *ptr;
    void *ptr2 = realloc(ptr, 100);
    void *ptr3 = realloc(ptr2, 43);
    void *ptr4 = realloc(ptr2, 434334344);
    if (ptr4 == NULL) return 1;
    free(ptr3);
    free(ptr4);
    free(ptr);
    void *p = malloc(15);
    p = realloc(p, 334);
    void *z = malloc(0);
    free(z);
    free(p);
    free(NULL);
}
