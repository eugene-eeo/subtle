# subtle

Tiny OO, prototype-based language inspired by
[Wren](https://github.com/wren-lang/wren) and
[IO](https://iolanguage.org/).
Features prototypal inheritance, fibers, operator overloading,
recursive inheritance (cycles in the prototype chain are allowed),
and multiple inheritance.

    # Comments start with '#'
    let Point = {}; # semicolons are optional
    Point.init = Fn.new{|x, y|
        self.x = x
        self.y = y
    }
    # (some) dots are optional
    Point.+ = Fn new {|other|  # yes, this is allowed
        return Point new(self x + other x, self y + other y)
    }
    let p1 = Point new(1, 2)
    let p2 = Point new(3, 4)
    let result = p1 + p2
    assert result x == 4
    assert result y == 6

## running

    $ git clone https://github.com/eugene-eeo/subtle
    $ cd subtle
    $ make release
    $ ./subtle
    >

## hacking

    $ make cute
    $ valgrind -s ./subtle
    > 1 + 1;
    == script ==
    0000    1 OP_CONSTANT         0 1
    0003    | OP_CONSTANT         0 1
    0006    | OP_INVOKE           1 "+" (1 args)
    0010    | OP_POP
    0011    | OP_NIL
    0012    | OP_RETURN
    ...
