#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

#include "iso.h"

int my_callback(imn_record_t *rec, void *unused) {

    imn_error_t ret_val;
    char *buffer;

    if (rec == NULL) {
        return -1;
    }

    buffer = malloc(4096);
    if (buffer == NULL) {
        return -1;
    }

    ret_val = imn_get_path(rec, buffer, 4096);
    if (ret_val != IMN_OK) {
        return -1;
    }

    printf("%s\n", buffer);
    return 0;
}

int main(int argc, char *argv[]) {

    imn_error_t ret_val;
    imn_iso_t iso;
    imn_callback_t cb;

    if (argc != 2) {
        printf("Usage: %s ISO-FILE\n", argv[0]);
    }
    
    ret_val = imn_init(&iso, argv[1], true);
    if (ret_val != IMN_OK) {
        printf("ERROR NUM: %d\n", ret_val);
        return EXIT_FAILURE;
    }

    cb.fn = my_callback;
    cb.args = NULL;

    ret_val = imn_traverse_dir(&iso, iso.desc->root_dir, &cb, true);
    if (ret_val != IMN_OK) {
        printf("ERROR NUM: %d\n", ret_val);
        return EXIT_FAILURE;
    }
}
