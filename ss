x := 1
while: [ x < 10 ] do: [
    x = x + 1
]
assert: (x == 10) msg: "x != 10"

y := 1
do: [ y = y + 1 ] while: [ y == 1 ]
assert: (y == 2) msg: "y != 2"
