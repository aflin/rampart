(* ocaml sample for rampart-treesitter tests.
 *
 * Covers: value_definition (let foo = ...), type_binding,
 * module_binding, exception_definition (name is a constructor_name).
 *)

let add a b = a + b

let no_args () = 42

type color = Red | Green | Blue

type point = { x : int; y : int }

module M = struct
    let bar y = y * 2
    let baz () = "baz"
end

module Other = struct
    let helper () = 42
end

exception NotFound
exception InvalidInput of string

let top_level_last x = x
