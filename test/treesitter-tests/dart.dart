/* dart sample for rampart-treesitter tests.
 * Covers: class_definition, constructor_signature, function_signature
 * (top-level + class methods — both emit as function_signature since
 * method_signature wraps it in the AST and we dedupe by skipping
 * method_signature), enum_declaration, mixin_declaration,
 * extension_declaration. */

class Greeter {
    final String name;

    Greeter(this.name);

    String greet() => "hi $name";

    String farewell() {
        return "bye $name";
    }

    static Greeter factory(String name) => Greeter(name);
}

class FancyGreeter extends Greeter {
    final String suffix;
    FancyGreeter(String name, this.suffix) : super(name);

    @override
    String greet() => "${super.greet()} $suffix";
}

mixin Logger {
    void log(String msg) {
        print(msg);
    }
}

extension StringExt on String {
    String reverse() {
        return split('').reversed.join();
    }
}

enum Color { red, green, blue }

void topLevel(int x) {
    print(x * 2);
}

int topLevelLast() {
    return 42;
}
