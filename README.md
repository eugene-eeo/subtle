# subtle

Tiny OO, prototype-based language inspired by
[Wren](https://github.com/wren-lang/wren),
[IO](https://iolanguage.org/), and
[Finch](http://finch.stuffwithstuff.com/).
Features prototypal inheritance, fibers, operator overloading,
and recursive inheritance (cycles in the prototype chain are allowed).

    # Comments start with '#'
    Point := {}
    Point :: newWithX: x y: y = [
        p := point clone
        p x = x
        p y = y
    ]
    Point :: + other [
        Point newWithX: (self x + other x) y: (self y + other y)
    ]
    p1 := Point newWithX: 1 y: 2
    p2 := Point newWithX: 3 y: 4
    result := p1 + p2
    assert: (result x == 4) msg: "woah!"
    assert: (result y == 6) msg: "woah!"

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
