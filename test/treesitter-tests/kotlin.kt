// kotlin sample for rampart-treesitter tests.
// Covers: function_declaration, class_declaration, object_declaration,
// property_declaration, type_alias. Uses simple_identifier as name
// field (kotlin grammar doesn't use plain "name").

package com.example

class Greeter(val name: String) {
    fun greet(): String = "hi $name"

    fun farewell(): String {
        return "bye $name"
    }

    companion object {
        fun factory(name: String): Greeter = Greeter(name)
    }
}

class FancyGreeter(name: String, val suffix: String) : Greeter(name) {
    fun decoratedGreet(): String = "$suffix: ${greet()}"
}

interface Greet {
    fun greet(): String
}

object Singleton {
    val constant: Int = 42
    fun doSomething() {}
}

enum class Color { RED, GREEN, BLUE }

typealias UserMap = Map<String, Int>

fun topLevel(x: Int): Int = x * 2

fun topLevelLast(): String = "last"
