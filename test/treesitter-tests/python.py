# python sample for rampart-treesitter tests.
# Covers: function_definition, class_definition. Including the decorated
# variant — function_definition nested inside a decorated_definition
# wrapper must still surface via the recursive walk.

import os

def hello(name):
    """Return a greeting."""
    return f"hi {name}"

def no_args():
    return 42

class Greeter:
    def __init__(self, name):
        self.name = name

    def greet(self):
        return f"hi {self.name}"

    def farewell(self):
        return f"bye {self.name}"

    @staticmethod
    def factory(name):
        return Greeter(name)

class FancyGreeter(Greeter):
    def __init__(self, name, suffix):
        super().__init__(name)
        self.suffix = suffix

    def greet(self):
        return super().greet() + " " + self.suffix

@some_decorator
def decorated(x):
    return x * 2

@first
@second
def double_decorated(x):
    return x

def top_level_last(x):
    return x
