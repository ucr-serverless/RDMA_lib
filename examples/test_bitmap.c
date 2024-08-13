#include "bitmap.h"
#include <stdio.h>

#include <stdlib.h>
int main(void)
{
    printf("sizeof(bitmap_type) = %zd\n", sizeof(bitmap_type));

    const int testlen = 250; // number of bits in the bitmap
    bitmap *B = bitmap_allocate(testlen);

    for (int i = 0; i < bitmap_wordlength; i++)
    {
        printf("%d", bitmap_read(B, i));
    }
    printf("\n");
    for (int i = 0; i < bitmap_wordlength; i++)
    {
        bitmap_set(B, i);
    }
    for (int i = 0; i < bitmap_wordlength; i++)
    {
        printf("%d", bitmap_read(B, i));
    }
    printf("\n");
    for (int i = 0; i < bitmap_wordlength; i++)
    {
        bitmap_clear(B, i);
    }
    for (int i = 0; i < bitmap_wordlength; i++)
    {
        printf("%d", bitmap_read(B, i));
    }
    printf("\n");

    bitmap_print_bit(B);
    for (int i = 0; i < testlen; i += 4)
    {
        bitmap_set(B, i);
    }
    bitmap_print_bit(B);
    for (int i = 0; i < testlen; i += 4)
    {
        bitmap_set(B, i);
    }
    bitmap_print_bit(B);
    for (int i = 0; i < testlen - 1; i += 1)
    {
        bitmap_set(B, i + 1);
    }
    bitmap_print_bit(B);
    for (int i = 0; i < testlen - 2; i += 2)
    {
        bitmap_set(B, i + 2);
    }
    bitmap_print_bit(B);
    for (int i = 0; i < testlen - 3; i += 3)
    {
        bitmap_set(B, i + 3);
    }
    bitmap_print_bit(B);
    for (int i = 0; i < testlen - 2; i += 4)
    {
        bitmap_clear(B, i + 2);
    }
    bitmap_print_bit(B);
    for (int i = 0; i < testlen; i += 4)
    {
        bitmap_clear(B, i);
    }
    bitmap_print_bit(B);

    bitmap_deallocate(B);
    B = NULL; // no dangling pointers!

    return 0;
}
