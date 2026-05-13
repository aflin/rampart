{- haskell sample for rampart-treesitter tests.

   Covers: module, signature, function, data_type, newtype,
   type_synonym, class, instance.

   QUIRK: tree-sitter-haskell uses the same `function` node type for
   function DEFINITIONS and for function-TYPE expressions inside
   signatures. So `foo :: Int -> Int` produces both a `signature`
   for foo AND a phantom `function` row whose name is "Int" (or
   similar). Same in class bodies where type signatures introduce
   phantom rows. Locked in the test expected list. -}

module Demo where

foo :: Int -> Int
foo x = x + 1

bar :: String -> String -> String
bar a b = a ++ " " ++ b

data Color = Red | Green | Blue

newtype Wrap a = Wrap a

type Name = String

class Greet a where
    greet :: a -> String

instance Greet Int where
    greet n = show n

topLevelLast :: Int
topLevelLast = 42
