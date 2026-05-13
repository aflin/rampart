/* swift sample for rampart-treesitter tests.
 * Covers: class_declaration, protocol_declaration, function_declaration,
 * init_declaration, property_declaration, enum_declaration,
 * typealias_declaration. Note that Swift's grammar treats `enum` and
 * `struct` under class_declaration node type. */

import Foundation

class Greeter {
    let name: String

    init(name: String) {
        self.name = name
    }

    func greet() -> String {
        return "hi \(name)"
    }

    func farewell() -> String {
        return "bye \(name)"
    }

    static func factory(name: String) -> Greeter {
        return Greeter(name: name)
    }
}

protocol Listener {
    func onEvent(name: String)
}

enum Color {
    case red
    case green
    case blue
}

struct Point {
    let x: Int
    let y: Int
}

func topLevel(x: Int) -> Int {
    return x * 2
}

typealias UserMap = [String: Int]

func topLevelLast() -> String {
    return "last"
}
