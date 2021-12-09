# subtle

tiny oo language, based on prototypes and method calls.
features operator overloading!

```cfg
# Comments start with '#'
let Point = {};
Point init = Fn new {|x, y|
    this x = x;
    this y = y;
    return this;
};
Point + = Fn new {|other|  # yes, this is allowed
    return Point clone init(this x + other x, this y + other y);
};
let p1 = Point clone init(1, 2);
let p2 = Point clone init(3, 4);
let result = p1 + p2;
assert result x == 4;
assert result y == 6;
```

## running

```sh
$ git clone ...
$ cd ...
$ make release
$ ./subtle
>
```

## hacking

```sh
$ make cute
$ ./subtle
> 1 + 1;
== script ==
0000    1 OP_CONSTANT         0 1
0003    | OP_CONSTANT         0 1
0006    | OP_INVOKE           1 "+" (1 args)
0010    | OP_POP
0011    | OP_NIL
0012    | OP_RETURN
...
```

## todo

- [x] benchmark hashtable implementation -- performance is good enough
- [x] implement recursive `vm_get_slot`
  - this allows us to get rid of hacks in `OP_EQ`, `OP_NEQ`, `OP_NOT`
  - there is no reason why `nil` can't be a regular object
- [ ] implement `super`: this is trickier than expected. naively, one would think:
  ```cfg
  obj foo = Fn new{|bar|
    super foo(bar) # <=> call this.proto.foo(bar) with the context being `this`
  };
  ```
  but the issue arises when we do:
  ```cfg
  let x = obj clone;
  x foo(1); # <-- this will segfault the VM, because
            # it will do x proto foo === obj foo,
            # and attempt to do x proto foo again...
  ```
  currently the solution is to keep a `whence` field on the callframe, to keep track of where a function was found. this means that it won't work with a custom `getSlot`, as we can lose whence information if e.g. the slot was dynamically generated. we either have to:
  1. bite the bullet and implement a best-effort `whence`.
  2. not implement `super` at all.
