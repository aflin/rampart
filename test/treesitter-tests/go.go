// go sample for rampart-treesitter tests.
// Covers: function_declaration, method_declaration, type_spec
// (struct, interface, type alias all parse as type_spec under
// type_declaration; we emit at the type_spec level directly).

package main

import "fmt"

func Add(a, b int) int {
    return a + b
}

func noArgs() int {
    return 42
}

type Point struct {
    X, Y int
}

func (p Point) Magnitude() int {
    return p.X + p.Y
}

func (p *Point) Scale(factor int) {
    p.X *= factor
    p.Y *= factor
}

type Greeter interface {
    Greet(name string) string
}

type StringList []string

type Renamed = int

func TopLevelLast(x int) int {
    return x
}

func main() {
    p := Point{1, 2}
    fmt.Println(Add(p.X, p.Y))
}
