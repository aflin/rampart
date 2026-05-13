# ruby sample for rampart-treesitter tests.
# Covers: method, singleton_method, class, module.

class Greeter
  def initialize(name)
    @name = name
  end

  def greet
    "hi #{@name}"
  end

  def farewell
    "bye #{@name}"
  end

  def self.factory(name)
    new(name)
  end
end

class FancyGreeter < Greeter
  def initialize(name, suffix)
    super(name)
    @suffix = suffix
  end

  def greet
    super + " " + @suffix
  end
end

module Greetings
  def self.hello
    "hello"
  end

  def self.goodbye
    "goodbye"
  end

  module Nested
    def self.deep
      "deep"
    end
  end
end

def top_level_method
  42
end
