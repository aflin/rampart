// rust sample for rampart-treesitter tests.
// Covers: function_item, struct_item, enum_item, trait_item, type_item,
// mod_item, const_item, static_item, macro_definition.
// impl_item is intentionally NOT in LANGUAGES[] — the methods inside
// surface via the recursive walk as function_items.

use std::collections::HashMap;

fn add(a: i32, b: i32) -> i32 {
    a + b
}

fn no_args() -> i32 {
    42
}

struct Point {
    x: i32,
    y: i32,
}

impl Point {
    fn new(x: i32, y: i32) -> Self {
        Point { x, y }
    }
    fn magnitude(&self) -> i32 {
        self.x + self.y
    }
}

enum Color {
    Red,
    Green,
    Blue,
}

trait Greet {
    fn greet(&self) -> String;
}

mod helpers {
    pub fn util() -> i32 { 7 }
    pub const HELPER_NUM: i32 = 42;
}

const MY_CONST: i32 = 100;

static MY_STATIC: i32 = 200;

type IntMap = HashMap<String, i32>;

macro_rules! say_hi {
    () => { println!("hi"); };
}

fn top_level_last(x: i32) -> i32 {
    x
}
