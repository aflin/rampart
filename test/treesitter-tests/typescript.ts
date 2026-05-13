/* typescript sample for rampart-treesitter tests.
 * Covers: function_declaration, class_declaration, method_definition,
 * interface_declaration, type_alias_declaration, enum_declaration. */

function add(a: number, b: number): number {
    return a + b;
}

function noArgs(): number {
    return 42;
}

class Greeter<T> {
    private name: T;
    constructor(name: T) {
        this.name = name;
    }
    greet(): string {
        return `hi ${this.name}`;
    }
}

interface Listener {
    onEvent(name: string): void;
}

type StringOrNum = string | number;

type Callback<T> = (value: T) => void;

enum Status {
    OK = 0,
    ERR = 1,
    PENDING = 2,
}

const enum InlineEnum {
    A,
    B,
}

function topLevelLast<T>(x: T): T {
    return x;
}
