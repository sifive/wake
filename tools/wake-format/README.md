# wake-format

`wake-format` is a tool for automatically applying the wake style guide to wake source files. It is intentionally opinionated to create a singular correct formatting standard.

## Usage

The standard usage of `wake-format` is to format a set of wake files in place.

`wake-format -i source.wake source2.wake source3.wake`

`wake-format` supports other flows for various use cases. The `-n`/`--dry-run` flag is ideal for CI/CD to check if the source needs to be formatted. It exits with a non-zero status code when a file would be changed by `wake-format`

Running `wake-format` without `-i` will emit the file to `stdout` instead of overwritting it.

# Documentation

The information below should be helpful to contribute to `wake-format`

## Building

`wake-format` is built in the same manner as `wake`. Running `make` or `./bin/wake build default` from within the wake dir will produce the `wake-format` binary.

The locally build `wake-format` can be found at `./bin/wake-format.native-cpp14-release`

## wake-format Fluent API/DSL

`wake-format` relies heavily on a custom fluent API/DSL to generate the formatting rules. Usage of the API is documented below. See [here](TODO) for design decisions, invariants, and engineering tradeoffs considered.

### Usage

All formatting rules start by creating a basic formatting object using `fmt()`. From there functions are added to describe the expectations of the current [CST node](TODO) and to return the appropiate text.

```c++
auto basic_fmt = fmt()
    .space()
    .newline();
```

creates a formatter that returns a space and a newline.

Once a formatter has been defined, the `format()` function is used to run the formatter and return the formated source. The source is returned as a [wcl::doc](TODO) for efficient concatenation and geometry lookup. `format()` takes a `ctx_t` and a `CSTElement`. The `ctx_t` (context) tracks the context (such as nest level) that all subnodes should be formatted in, and the `CSTElement` is the current node being formatted.

```c++
ctx_t ctx;
CSTElement node = ""; // An empty node

wcl::doc doc = fmt()
  .space()
  .newline()
  .format(ctx, node);

std::string s = doc.as_string(); // " \n"
```

`format()` expects to consume the entire `CSTElement` node and will `assert()` if that is not the case. Asserting may seem heavily handed, however this is a very powerful method for identifying and fixing nodes with unexpected contents. 

### API Functions & Examples

**CSTElement Manipulation**

Manipulation of the current state of the formatter relating to CSTElement nodes.

**`token(cst_id_t id)`**

`assert()`s the current node.id() is `id` then emits the string representation of the node.

```c++
CSTElement node = "def";
auto doc = fmt()
  .token(TOKEN_KW_DEF)
  .space()
  .format(ctx, node);
doc.as_string(); // "def "
```

**`token(cst_id_t id, const char* str)`**

`assert()`s the current node.id() is `id` then emits `str`.

```c++
CSTElement node = "def";
auto doc = fmt()
  .token(TOKEN_KW_DEF, "foo")
  .space()
  .format(ctx, node);
doc.as_string(); // "foo "
```

**`next()`**

Moves the current node forward by one element.

```c++
CSTElement node = "def id = 5";
auto doc = fmt() // -> def
  .next()        // -> " "
  .next()        // -> id
  .next()        // -> " "
  .next()        // -> =
  .next()        // -> " "
  .next();       // 5
```

**Control Flow**

Conditional formatting based on the state at a given point in the formatter.

**`fmt_if(Predicate, Formatter)`**

Applies `Formatter` if `Predicate` returns `true`. See [Predicate](#predicate) for more Predicate examples.

```c++
CSTElement node = "def id = 5";
auto doc = fmt()
  .fmt_if(TOKEN_KW_DEF, fmt().token(TOKEN_KW_DEF, "foo"))
  .fmt_if(TOKEN_COMMENT, fmt().token(TOKEN_COMMENT, "bar"))
  .format(ctx, node);
doc.as_string(); // "foo"
```

**`fmt_if_else(Predicate, Formatter if_fmt, Formatter else_fmt)`**

Applies `if_fmt` if `Predicate` returns `true`, `else_fmt` otherwise. See [Predicate](#predicate) for more Predicate examples.

```c++
CSTElement node = "def id = 5";
auto doc = fmt()
  .fmt_if_else(
    TOKEN_COMMENT, 
    fmt().token(TOKEN_COMMENT, "foo"),
    fmt().token(TOKEN_KW_DEF, "bar"))
  .format(ctx, node);
doc.as_string(); // "bar"
```

**`fmt_while(Predicate, Formatter)`**

Applies `Formatter` as long as `Predicate` returns `true`

```c++
CSTElement node = "def def def";
auto doc = fmt()
  .fmt_while(
    TOKEN_KW_DEF, 
    fmt().token(TOKEN_KW_DEF, "bar").ws())
  .format(ctx, node);
doc.as_string(); // "bar bar bar"
```

**`fmt_if_fits(Formatter if, Formatter else)`**

`fmt_if_else` where `Predicate = FitsPredicate`. See [Fits](#fits) for details.

```c++
CSTElement node = "def id = 5";
auto doc = fmt()
  .fmt_if_fits(
    fmt().token(TOKEN_KW_DEF, "a very very long string"),
    fmt().token(TOKEN_KW_DEF, "bar"))
  .format(ctx, node);
doc.as_string(); // "bar"
```

**Walkers**

Dispatch formatting to something else.

`Walker`s must have the type signature `(ctx_t ctx, CSTElement node) -> wcl::doc` The typical use case is to recursivly call into some other function that _walks_ a specific type of `CSTElement`.


**`walk(Walker)`**

Unconditionally applies the `Walker`

```c++
CSTElement node = "def id = 5";
auto doc = fmt()
  .walk([this](ctx_t ctx, CSTElement node) {
    return walk_def(ctx, node);
  })
  .format(ctx, node);
doc.as_string(); // whatever walk_def() returns
```

**`walk(Predicate, Walker)`**

Applies `Walker` if `Predicate` returns `true`. See [Predicate](#predicate) for more Predicate examples.

```c++
CSTElement node = "def id = 5";
auto doc = fmt()
  .walk(CST_DEF, [this](ctx_t ctx, CSTElement node) {
    return walk_def(ctx, node);
  })
  .format(ctx, node);
doc.as_string(); // whatever walk_def() returns
```

**`walk_children(Formatter)`**

Applies `Formatter` to every child of the current node

```c++
CSTElement node = "  \n   \n";
auto body_fmt = fmt()
  .fmt_if(TOKEN_WS, fmt().next())
  .fmt_if(TOKEN_NL, fmt().next());
auto doc = fmt()
  .walk_children(body_fmt)
  .format(ctx, node);
doc.as_string(); // ""
```

**Whitespace**

Manipulating and interacting with whitespace.

**`consume_wsnl()`**

Moves the current node past all consecutive whitespace and newlines.

```c++
CSTElement node = "def id       \n = 5";
auto doc = fmt()  // -> def
  .next()         // -> " "
  .next()         // -> id
  .next()         // -> "       "
  .consume_wsnl() // -> =
  .next()         // -> " "
  .next();        // -> 5
```

**`ws()`**

`assert()`s the current node is a TOKEN_WS, consumes it, then emits a _single_ space. TOKEN_WS may represent several consecutive spaces.

```c++
CSTElement node = "     \n";
ctx_t ctx = { nest_level: 1};
auto doc = fmt()
  .ws()
  .newline()
  .consume_wsnl()
  .format(ctx, node);
doc.as_string(); // " \n    "
```

**`nl()`**

`assert()`s the current node is a TOKEN_NL, consumes it, then emits a `\n` and `ctx.nest_level * space_per_indent` spaces

```c++
CSTElement node = "     \n";
ctx_t ctx = { nest_level: 1};
auto doc = fmt()
  .ws()
  .nl()
  .format(ctx, node);
doc.as_string(); // " \n    "
```

**`space(uint8_t count = 1)`**

Emits `count` spaces. `count` defaults to `1`

```c++
CSTElement node = "";
auto doc = fmt()
  .space()
  .space(5)
  .format(ctx, node);
doc.as_string(); // "      "
```

**`newline()`**

Emits a `\n` and `ctx.nest_level * space_per_indent` spaces

```c++
CSTElement node = "";
ctx_t ctx = { nest_level: 1};
auto doc = fmt()
  .newline()
  .format(ctx, node);
doc.as_string(); // "\n    "
```

**`nest(Formatter)`**

Increases `ctx.nest_level` by `1` for `Formatter`

```c++
CSTElement node = "";
ctx_t ctx = { nest_level: 0};
auto doc = fmt()
  .nest(fmt()
    .newline()) // nest_level = 1
  .newline() // nest_level = 0
  .format(ctx, node);
doc.as_string(); // "\n    \n"
```

**Misc**

**`join(Formatter)`**

Joins two formatters into one.

```c++
CSTElement node = "";
auto f = fmt().space().space()
auto doc = fmt()
  .space()
  .space()
  .join(f)
  .format(ctx, node);
doc.as_string(); // "    "
```

**`escape(Function)`**

Escape out of the formatter API and manually manage the interal formatting state. `node` must be manually advanced. _Use with caution_

`Function` must have signature `(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) -> void`

```c++
CSTElement node = "def id = 5";
auto doc = fmt()
  .escape([](wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    builder.append("some string");
    node.nextSiblingElement();
  })
  .next().next().next().next().next().next()
  .format(ctx, node);
doc.as_string(); // "some string"
```

### Predicates

There are four forms of allowed predicates with varying levels of power and terseness. The more terse, the less power and vice versa. They are listed below in order of increasing power.

**Single `cst_id_t` Token Id**

Accepts one id and returns true if the current node matches the it.

```c++
auto f = fmt()
  .fmt_if(TOKEN_COMMENT, fmt());
```

**`cst_id_t` Token Id List**

Accepts an initialzer list of ids and returns true if the current node matches any of the ids

```c++
auto f = fmt()
  .fmt_if({TOKEN_ID, TOKEN_COMMENT}, fmt());
```

**`cst_id_t` Token Id Parameter Function**

Accepts a function with signature `(cst_id_t) -> bool`

```c++
bool is_id(cst_id_t type) {
  return type == CST_ID;
}

auto f = fmt()
  .fmt_if(is_id, fmt())
  .fmt_if([](cst_id_t type) {
    return type == CST_APP;
  }, fmt());
```

**Full Parameter Function**

Accepts a function with signature `(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) -> bool`

```c++
bool is_id(wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
  return node.id() == CST_ID;
}

auto f = fmt()
  .fmt_if(is_id, fmt())
  .fmt_if([](wcl::doc_builder& builder, ctx_t ctx, CSTElement& node) {
    return node.id() == CST_APP;
  }, fmt());
```

**Special Built In Predicates**

Below are built-in predicates meant for internal use.

#### ConstPredicate

Always returns either true or false, set at construction time. Useful for creating a non-predicate version of a predicate function.

```c++
auto pred = ConstPredicate(true);
```

#### FitsPredicate

Determines if a given formatter `fits` if appended to the current builder. Fitting is defined as being less than/equal to the max column width for the line of text. Used internally for `fmt_if_fits`

```c++
// FMT needs to be derived via a template function
auto pred = FitsPredicate<FMT>(fmt())
```

## Fits

TODO: jake

## Document (wcl::doc)

`wcl::doc` is an efficient structure for concatenating UTF8 strings and looking up their geometry. All geometry functions and concatenation of two documents can be completed in O(1) running time. Constructing a document with a string, and converting the document back into a string take O(n) running time. Transitions between `wcl::doc` and `std::string` should be minimized as much as possible. The primary way a `wcl::doc` is built in the formatter is using a `wcl::doc_builder` which has convient `append` and `build` functions. See below for example usage of `wcl::doc_builder`

```c++
  wcl::doc_builder bdr;
  bdr.append("def");
  bdr.append(" ");
  bdr.append("foo");
  bdr.append(" = ");
  bdr.append("bar");
  bdr.append("\n    ");

  bdr.append("def bat = baz");

  bdr.newline_count() // 1
  bdr.height() // 2
  bdr.last_width() // 17
  bdr.first_width() // 13
  bdr.max_width() // 17

  wcl::doc doc = std::move(bdr).build();

  doc.as_string() // "def foo = bar\n    def bat = baz"

  doc.newline_count() // 1
  doc.height() // 2
  doc.last_width() // 17
  doc.first_width() // 13
  doc.max_width() // 17
```

## CSTElement

`CSTElement` is the core data type for parts of the `CST`(Concrete Syntax Tree). It maintains very precise details about the original source file such as exact whitespace, newlines, and comments. The CST is however, parsed into a tree structure allowing is to be walked similiarly to an AST. CSTElements are one of two types, either a `node` (`element.isNode() == true`) or a `token`. Nodes have one or more children which may be either nodes or tokens. Tokens are terminal and thus have no children. The formatter has a `walk_XX` function for every possible node that returns a `wcl::doc`. The `walk_XX` functions are all mutually recursive and the progession through them mirrors the shape of the source being formatted.

Using a node is primarly done by looking at its `id()` and its children. Using a token is done by looking at its `id()`.

```c++
CSTElement node = parse("def x = 5")
assert(node.id() == CST_DEF);

CSTElement child = node.firstChildElement(); // returns the first child
assert(child.id() == TOKEN_KW_DEF); // def
child.nextSiblingElement(); // advances child in place

assert(child.id() == TOKEN_WS); // " "
child.nextSiblingElement(); // advances child in place

assert(child.id() == CST_ID); // CST_ID(x) node
walk_id(child);
child.nextSiblingElement(); // advances child in place
```

## CTX_T

TODO: jake

## Memoization

TODO: jake

## Design, Invariants, and Tradeoffs

TODO: jake