<?php
/* php sample for rampart-treesitter tests.
 * Covers: function_definition, method_declaration, class_declaration,
 * interface_declaration, trait_declaration, enum_declaration,
 * namespace_definition.
 * Uses the php_only grammar (pure-PHP files, no inline HTML). */

namespace App\Sample;

class Greeter {
    private string $name;

    public function __construct(string $name) {
        $this->name = $name;
    }

    public function greet(): string {
        return "hi {$this->name}";
    }

    public function farewell(): string {
        return "bye {$this->name}";
    }

    public static function factory(string $name): self {
        return new self($name);
    }
}

interface Greetable {
    public function greet(): string;
}

trait Loggable {
    public function log(string $msg): void {
        echo $msg;
    }
}

enum Status {
    case OK;
    case ERR;
    case PENDING;
}

function topLevelFn(int $x): int {
    return $x * 2;
}

function anotherFn(): void {
    /* no-op */
}
