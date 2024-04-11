#include "magicfile.h"
#include <stdlib.h>
#include "eprintf.h"

magic_t magic;

void InitMagic(void) {
    magic = magic_open(MAGIC_MIME_TYPE);
    if (magic == NULL)
        eprintf("magic_open:");
    if (magic_load(magic, NULL) != 0)
        eprintf("magic_load: %s", magic_error(magic));
}
