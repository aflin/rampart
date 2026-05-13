/* java sample for rampart-treesitter tests.
 * Covers: method_declaration, constructor_declaration, class_declaration,
 * interface_declaration, enum_declaration, annotation_type_declaration,
 * record_declaration. */

package com.example;

public class Greeter {
    private final String name;

    public Greeter(String name) {
        this.name = name;
    }

    public String greet() {
        return "hi " + name;
    }

    public String farewell() {
        return "bye " + name;
    }

    public static Greeter factory(String name) {
        return new Greeter(name);
    }
}

class GreeterImpl extends Greeter implements Listener {
    public GreeterImpl(String name) {
        super(name);
    }
    @Override
    public void onEvent() {
        // ...
    }
}

interface Listener {
    void onEvent();
}

enum Color {
    RED, GREEN, BLUE;
    public boolean isWarm() { return this == RED; }
}

@interface MyAnnotation {
    String value();
}

record Point(int x, int y) { }
