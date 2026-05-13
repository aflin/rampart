/* c sample for rampart-treesitter tests.
 * Covers: function_definition, struct_specifier, enum_specifier,
 * union_specifier, type_definition. Includes the known-quirk case
 * where typedef-wrapped anonymous structs emit a phantom "(anonymous)"
 * struct_specifier row. */

#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

static int read_record(const char *path, void *out) {
    return 0;
}

struct Point {
    int x;
    int y;
};

enum Color { RED, GREEN, BLUE };

typedef struct Wrapper {
    int value;
} Wrapper;

/* QUIRK: anonymous struct inside typedef — produces both a
 * type_definition named 'AnonPoint' AND a struct_specifier named
 * '(anonymous)'. Locking in current behavior. */
typedef struct {
    int x;
    int y;
} AnonPoint;

union Variant {
    int as_int;
    float as_float;
};

typedef int callback_id;

int main(int argc, char *argv[]) {
    struct Point p = {1, 2};
    return add(p.x, p.y);
}
