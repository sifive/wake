# Wake for F# developers

F# and Wake are both ML-dialects, so they share very similar heritages as far as programming languages go. The major differences are that F# supports OOP and imperative programming, while Wake does not, and that Wake is a build system on top of being a programming langauge. In many cases, the difference between the functional parts of F# and Wake are syntactical only. Below is a collection of comparisons to ease the transition between the two.

## Definitions

In F#, bindings are created with `let` and recursive functions are created with `let rec`.

```fsharp
let numberTwo = 2

let rec fibonacci (n: int) : int =
    match n with
    | 0 | 1 -> n
    | n -> fibonacci (n-1) + fibonacci (n-2)
```

In Wake, bindings are created with `def` and do not require any keyword if they are recursive.

```wake
def numberTwo = 2

def fibonacci (n: Integer): Integer =
    match n
        0 = n
        1 = n
        n = fibonacci (n-1) + fibonacci (n-2)
```

## Lists

In F#, cons is `::` and the empty list is `[]`. There is literal list syntax with `[]` and `;` as a separator.

```fsharp
> 1 :: 2 :: 3 :: [];;
val it: int list = [1; 2; 3]

> [1; 2; 3];;
val it: int list = [1; 2; 3]
```

In Wake, cons is [`,`](https://github.com/sifive/wake/blob/master/share/wake/lib/core/list.wake#L43-L50) and the empty list is [`Nil`](https://github.com/sifive/wake/blob/master/share/wake/lib/core/list.wake#L41-L42). There is currently no literal list syntax in Wake. You often need to surround a list value with parentheses, such as `(1, 2, 3, Nil)`.

```wake
> wake -vx '1, 2, 3, Nil'
1, 2, 3, Nil: List Integer = 1, 2, 3, Nil
```

## Discriminated unions vs `data` types

F#
```fsharp
type Shape =
    | Circle of radius: float
    | Square of side: float
    | Rectangle of length: float * width: float
```

Wake
```wake
export data Shape =
    Circle (radius: Double)
    Square (side: Double)
    Rectangle (length: Double) (width: Double)
```

## Pattern Matching

F#
```fsharp
let rec area shape =
    match shape with
    | Circle radius -> System.Math.PI * radius * radius
    | Square side -> area (Rectangle (side, side))
    | Rectangle (length, width) -> length * width
```

Wake
```wake
export def area (shape: Shape) =
    match shape
        Circle radius = pi *. (radius ^. 2.0)
        Square side = area (Rectangle side side)
        Rectangle length width = length *. width
```

## Piping

F#
```fsharp
let square (x: int) = x * x

2 |> square
```

Wake
```wake
export def square (x: Integer) = x * x

2 | square
```

## Records, tuples, pairs etc.

Records in F# are analogous to tuples in Wake.

F#
```fsharp
// This is a record
type Name = {First: string; Last: string}

// This is a value of type `Name`
let name = {First: "John"; Last: "Smith"}

// This is a record with a generic type parameter
type Person<'T> = {Name: Name; JobFunction: 'T}
```

Wake
```wake
# This is a tuple
export tuple Name =
    export First: String
    export Last: String

# This a value of type `Name`
export def name = Name "John" "Smith"

# This is a tuple with a type parameter
export tuple Person a =
    export Name: Name
    export JobFunction: a

# This is a value of type `Person Integer`
export def person = Person (Name "John" "Smith") 4
```

Wake automatically defines and provides functions for accessing the named fields of tuples that are of the form `get<tupleName><fieldName>`. For example,
```wake
> wake -vx 'getNameFirst name'
getNameFirst name: String = "John"
```

## Wake

In Wake, there is no generic or structural equality. Thus, every type has its own operator comparison function. For `Integer`, it's `==`, and for `String`, one equality operator is `==~`.
