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

