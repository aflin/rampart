-- lua sample for rampart-treesitter tests.
-- Covers: function_declaration (top-level + member-style + nested),
-- local_function (local function foo).

function add(a, b)
    return a + b
end

function no_args()
    return 42
end

-- Member-style function definition; the name "M.method" comes out as
-- a single function_declaration symbol (lua grammar treats the dotted
-- path as one identifier here).
local M = {}

function M.method(x)
    return x * 2
end

function M.other()
    return "other"
end

-- local function (still parses as function_declaration in tree-sitter-lua)
local function helper(x)
    return x + 1
end

function top_level_last()
    return "last"
end
