# subtle

tiny oo language, based on prototypes and method calls.
features operator overloading!

    # Comments start with '#'
    let Point = {};
    Point.setSlot("+") {|other|
        return {x: this.x + other.x,
                y: this.y + other.y};
    };
    let p1 = {x: 1, y: 2};  # <-- Object literals
    let p2 = {x: 3, y: 4};
    p1.setProto(Point);
    p2.setProto(Point);
    let result = p1 + p2;
    assert result.x == 4;
    assert result.y == 6;

## running

    $ git clone ...
    $ cd ...
    $ make release
    $ ./subtle
    >

## design

**massive wip, expect many bugs.**

1. (almost) everything is a method call. for example:
   ```
   x + y
   ```
   is just syntactic sugar for:
   ```
   let slot = Object.getSlot.call(x, "+")
   slot.call(x, y)
   ```
   in fact most of the operations are handled by just one opcode, `OP_INVOKE`
   which does the above in the interpreter. even 'getters' and 'setters' are
   method calls:
   ```
   x.attr == x.getSlot("attr")
   (x.attr = y) == x.setSlot("attr", y)
   ```
   the only exception to this rule is `==`, `!=`, and `!` which are special cased
   to work with `nil`, which has no prototype.

2. everything is an object. for example:
   ```
   1.proto == Number
   Number.proto == Object
   Object.proto == nil
   ```
   lookups follow the prototype chain of the object,
   and terminate whenever we see a `nil`.
   currently the vm doesn't handle cycles in the prototype chain.
   you can play with the prototypes using `setProto` and `proto`:
   ```
   let x = {}
   let y = {}
   x.setProto(y)
   y.a = 1
   x.a # == 1
   x.proto # == y
   ```
   careful that overriding `.proto` doesn't actually update the object's
   internal prototype...
   ```
   x.proto = x
   Object.proto.call(x) # == y
   ```
