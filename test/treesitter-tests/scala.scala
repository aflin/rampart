/* scala sample for rampart-treesitter tests.
 * Covers: object_definition, class_definition, trait_definition,
 * enum_definition (Scala 3), function_definition, function_declaration
 * (abstract), val_definition, type_definition. */

package com.example

object App {
    def main(args: Array[String]): Unit = {
        println("hello")
    }
}

class Greeter(name: String) {
    def greet: String = s"hi $name"

    def farewell: String = {
        s"bye $name"
    }
}

class FancyGreeter(name: String, suffix: String) extends Greeter(name) {
    override def greet: String = s"${super.greet} $suffix"
}

trait Logger {
    def log(s: String): Unit
}

trait Named {
    val name: String
    def display: String = s"named: $name"
}

enum Color {
    case Red, Green, Blue
}

type StringMap = Map[String, String]

val constant: Int = 42

def topLevel(x: Int): Int = x * 2

def topLevelLast(): String = "last"
