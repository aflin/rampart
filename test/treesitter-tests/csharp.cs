/* csharp sample for rampart-treesitter tests.
 * Covers: namespace_declaration, class_declaration, struct_declaration,
 * interface_declaration, enum_declaration, record_declaration,
 * delegate_declaration, method_declaration, constructor_declaration,
 * property_declaration. */

using System;

namespace App.Sample {

    public class Greeter {
        public string Name { get; set; }

        public Greeter(string name) {
            Name = name;
        }

        public string Greet() => $"hi {Name}";

        public static Greeter Factory(string name) {
            return new Greeter(name);
        }
    }

    public struct Point {
        public int X { get; }
        public int Y { get; }
        public Point(int x, int y) { X = x; Y = y; }
    }

    public interface IService {
        void Run();
    }

    public enum Color { Red, Green, Blue }

    public record PersonRecord(string Name, int Age);

    public delegate void EventHandler(object sender, EventArgs e);

}
