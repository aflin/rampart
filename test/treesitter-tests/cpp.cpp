/* cpp sample for rampart-treesitter tests.
 * Covers: function_definition, class_specifier, struct_specifier,
 * enum_specifier, namespace_definition, type_definition.
 * Edge cases: template functions (must return function name, NOT the
 * return-type's type_identifier), qualified out-of-class method defs
 * (`int ns::Foo::bar()` should pick "bar", not "ns" or "Foo"). */

#include <vector>

namespace app {

    class Greeter {
    public:
        Greeter(const std::string& name) : name_(name) {}
        std::string greet() const;
    private:
        std::string name_;
    };

    /* out-of-class method definition; name should resolve to "greet" */
    std::string Greeter::greet() const {
        return "hi " + name_;
    }

    struct Point {
        int x;
        int y;
    };

    enum Color { RED, GREEN, BLUE };

    typedef int callback_id;

    /* template function — name should be "identity", NOT "T" */
    template<typename T>
    T identity(T value) {
        return value;
    }

    /* free function with stl return type */
    std::vector<int> make_range(int n) {
        return std::vector<int>(n);
    }

}  // namespace app

int main() {
    app::Greeter g("world");
    return 0;
}
