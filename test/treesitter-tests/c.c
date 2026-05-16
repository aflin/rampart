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

/* Typedef-return-type function definition. The walk_collect fix
 * narrows the name search to the declarator subtree so a
 * type_identifier return type (here, my_ret_t) can't be mistaken
 * for the function name via depth-first match. */
typedef int my_ret_t;

my_ret_t fancy_returner(int x) {
    return x + 1;
}

/* Pointer return with typedef — exercises pointer_declarator descent
 * (c_proto_declarator walks through pointer_declarator wrappers to
 * find the innermost function_declarator). */
char *typed_ptr_returner(int n) {
    (void)n;
    return 0;
}

/* Function PROTOTYPES (declarations) — should be captured with
 * kind="function_declaration". */
int prototype_a(int x);
my_ret_t prototype_b(const char *s, int n);
char *prototype_c(void);

/* Variable / extern declarations must NOT appear in the symbol list.
 * If they ever do, c_proto_declarator's function_declarator check
 * regressed. */
int some_global_var = 42;
my_ret_t another_var;
extern int an_extern_var;
