/* javascript sample for rampart-treesitter tests.
 * Covers: function_declaration, class_declaration, method_definition. */

function add(a, b) {
    return a + b;
}

function noArgs() {
    return 42;
}

class Greeter {
    constructor(name) {
        this.name = name;
    }
    greet() {
        return "hi " + this.name;
    }
    farewell() {
        return "bye " + this.name;
    }
}

class FancyGreeter extends Greeter {
    constructor(name, suffix) {
        super(name);
        this.suffix = suffix;
    }
    greet() {
        return super.greet() + " " + this.suffix;
    }
}

/* Anonymous fn expressions and arrows are NOT function_declarations and
 * should be skipped. Verifying they don't pollute the symbol set. */
var anon = function() { return 1; };
var arrow = (x) => x * 2;

function topLevelLast(x) { return x; }
