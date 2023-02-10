= Learning Wake as a Scala programmer
:toc:

This document describes Wake from the perspective of a programmer who is already familiar with Scala.
Wake is a functional programming language, and it is very similar to the functional aspects of Scala.

Wake is, however, "purer" than Scala,
so while Scala allows for a blend of functional, imperative, and object-oriented styles,
Wake more strictly enforces a functional style.
In fact, the only side effects in Wake are printing, running subprocesses, and panicking.

Since Wake does not support function overloading, the same function name cannot be defined more than once with different arguments.
Therefore many of the built-in collection types define functions that are prefixed with the first initial of the type name.
For example, while the `List` type has a `map` function, the `Option` type has an `omap` function, and the `Result` type has an `rmap` function.

See the following sections to learn about how various concepts in Scala map to Wake.


== Primitive types

Wake generally has familiar syntax for primitive types.
Syntax for more advanced functionality of strings -- such as interpolation and multi-line strings -- differs between Wake and Scala.
See <<table-primitives>> for more information.

[[table-primitives]]
.Primitive types in Scala and Wake
[%autowidth.stretch,cols="m,a,m,a"]
|===
| Scala Type | Scala Syntax | Wake Type | Wake Syntax


| Int
|
[source,scala]
----
1
----
| Integer
|
[source,wake]
----
1
----


| Double
|
[source,scala]
----
1.0
----
| Double
|
[source,wake]
----
1.0
----


| String
|
[source,scala]
----
"Plain string"

s"${Interpolated} string"

"""
Multi-
line
string
"""

s"""
Multi-line string
with ${interpolation}
"""
----
| String
|
[source,wake]
----
"Plain string"

"{Interpolated} string"

"
Multi-
line
string
"

"%
Multi-line string
with custom delimiter
%"

"%
Multi-line string
with custom delimiter
and %{interpolation}
%"
----


| Boolean
|
[source,scala]
----
true
false
----
| Boolean
|
[source,wake]
----
True
False
----

|===



== Option

Both Scala and Wake have option types that are named `Option`.
Wake has similar functions as Scala for manipulating options types.
See <<table-option>>.

[[table-option]]
.Option in Scala and Wake
[%autowidth.stretch,cols=",a,a"]
|===
| Description | Scala | Wake


| Constructors
|
[source,scala]
----
Some(1)
None
----
|
[source,wake]
----
Some 1
None
----


| Mapping over option
|
[source,scala]
----
val foo = Some(1)
val bar = foo.map(x => x + 1)
val baz = foo.map(_ + 1)
----
|
[source,wake]
----
def foo = Some 1
def bar = foo \| omap (\x x + 1)
def baz = foo \| omap (_ + 1)
----


| Get or else
|
[source,scala]
----
val foo = opt.getOrElse(2)
----
|
[source,wake]
----
def foo = opt \| getOrElse 2
----


| Testing for Some/None
|
[source,scala]
----
val foo = opt.isDefined
val bar = opt.isEmpty
----
|
[source,wake]
----
def foo = opt.isSome
def bar = opt.isNone
----

|===


== List

Both Scala and Wake have a list type named `List`, corresponding to a linked-list structure.
Unlike Scala, Wake does not define a constructor with a variable number of arguments.
Instead, it is idiomatic in Wake to define lists using its "cons" operator `,`.

[[table-list]]
.List type in Scala and Wake
[%autowidth.stretch,cols=",a,a"]
|===
| Description | Scala | Wake


| Empty list
|
[source,scala]
----
Nil
----
|
[source,wake]
----
Nil
----


| Defining a list
|
[source,scala]
----
List(1, 2, 3)
// Or equivalently
1 :: 2 :: 3 :: Nil
----
|
[source,wake]
----
1, 2, 3, Nil
----


| Map
|
[source,scala]
----
val bar = foo.map(_ + 1)
----
|
[source,wake]
----
def bar = foo \| map (_ + 1)
----


| Flat map
|
[source,scala]
----
val words = List("foo", "bar")
val chars = words.flatMap(_.toList)
----
|
[source,wake]
----
def words = "foo", "bar", Nil
def chars = words \| mapFlat explode
----

|===


== Try or Either

A very important data type in Wake is `Result`, representing the success or failure of an action.
It is equivalent to Scala's `Try` (or Scala's `Either`), with `Result a b` describing an object that can either be a `Pass a` or a `Fail b`.
However, because Wake is designed to describe the execution of build commands, `Result` objects are ubiquitous in Wake code.

In addition Wake defines an `Error` type, consisting of a `Cause` string and a `Stack` trace, and it is common for the `Fail` type parameter to be an `Error` type.

See <<table-result>> for more information.

[[table-result]]
.Scala's Try type compared to Wake's Result type
[%autowidth.stretch,cols=",a,a"]
|===
| Description | Scala | Wake

| Constructor
|
[source,scala]
----
Success(1)
Failure(exceptionObject)
----
|
[source,wake]
----
Pass 1
Fail exceptionObject
# Construct an Error object with stack trace
Fail (makeError "My error")
----


| Mapping on successful case
|
[source,scala]
----
val result = Success(1)
val newResult = result.map(_ + 1)
----
|
[source,wake]
----
def result = Pass 1
def newResult = result \| rmap (_ + 1)
----


| Checking for success/failure
|
[source,scala]
----
result.isSuccess
result.isFailure
----
|
[source,wake]
----
result.isPass
result.isFail
----


| Flat map
|
[source,scala]
----
// Could fail if d == 0
def reciprocal(d: Double): Try[Double] = Try { 1.0 / d }

val result = Success(1)
val newResult = result.flatMap(reciprocal)
----
|
[source,wake]
----
def reciprocal = match _
  0.0 = Fail (makeError "Divide by zero")
  d   = Pass (1.0 / d)

val result = Pass 1
def newResult = result \| rmapPass reciprocal
----

|===



== Case classes

Similar to Scala's case classes, Wake has a product type known as a tuple.
This construct has a special `tuple` keyword, which then defines members and automatically defines getter and "setter" functions by prefixing `get` or `edit` and the tuple name.
Tuples are immutable, so the "setter" functions actually return a new tuple instance with the field set to the new value.

Tuples have another important purpose in Wake.
Because Wake lacks the ability to pass function arguments by name, function overloading, or default argument values,
tuples may be used as a way to simulate these features.
This can also be used to preserve backwards compatibility,
since new tuple members can added without changing the signature of a function.

See <<table-case-class>> for examples.

[[table-case-class]]
.Comparison of Scala case classes and Wake tuples
[%autowidth.stretch,cols="a,a"]
|===
| Scala | Wake

|
[source,scala]
----
case class Foo(myString: String, myInt: Int)

val foo = Foo("foo", 0)
val myFooString = foo.myString
val fooCopy = foo.copy(myInt = 1)
----
|
[source,wake]
----
tuple Foo =
  global MyString: Strings
  global MyInt: Integer

def foo = Foo "foo" 0
def myFooString = foo \| getFooMyString
def myFooCopy = foo \| setFooMyInt 1
----
|===
