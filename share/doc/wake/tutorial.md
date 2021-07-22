# Wake Tutorial

This tutorial assumes you have wake and git installed in your path.
Code sections are intended to be copy-pasted into a terminal.

## Invoking wake


    mkdir ~/tutorial
    cd ~/tutorial
    wake --init .
    wake -x '5 + 6'

This sequence of commands creates a new workspace managed by wake.
The `init` option is used to create an initial `wake.db` to record the state
of the build in this workspace.
Whenever you run wake, it searches for a `wake.db` in parent directories.
The first `wake.db` found defines what wake considers to be the workspace.
You can thus safely run wake in any sub-directory of `tutorial` and wake
will be aware of all the relevant dependencies and rules.

The output of wake run on `-x 'expression'` is the result of evaluating that expression.
In this case, `5 + 6` results in value `11`.
Wake will report more information when run in verbose mode: `wake -v`.

    wake -vx '5 + 6'

The verbose output is of the form `expression: type = value`. The above
will give `5 + 6: Integer = 11`. As before, `5 + 6` results in `11`,
which is an `Integer`.

    git init .
    echo 'def hello = "Hello World"' > tutorial.wake
    git add tutorial.wake
    wake -x 'hello'

Wake processes all wake files (`*.wake`) which are in the workspace,
so our new file `tutorial.wake` is available to us from the command-line.
The syntax `def x = y` introduces a new variable `x` whose value is equal
to `y`. In this case, `y` is just a `String`.

## Compiling a CPP file

    echo 'int main() { return 0; }' > main.cpp
    echo 'def build x = compileC "native-cpp11-release" ("-I.", Nil) Nil (source "main.cpp")' > tutorial.wake
    git add main.cpp
    wake build

Let's just ignore the wake file for the moment and focus on how wake behaves.
First, notice that we did not use `-x` when invoking wake this time.
The default operation of wake invokes the subcommand (in this case `build`)
specified on the command-line, with any additional command-line arguments
passed to that function.

By default wake prints out the jobs it has executed.
In this case, that means we see that it ran g++ to produce an object file.

    wake -o main.native-cpp11-release.o

We can ask wake to tell us about which job used a file as an output.
Notice that the command-line and environment was captured.
If either of these change, wake will rebuild the file.

    wake -i main.cpp

We can also ask wake to tell us about which job used a file as an input.
For header files, this can be a large number of files, so you might want
to pipe the output to less.

    touch main.cpp
    wake build

Notice that wake does not rebuild the object file in this case.  It
checked that the hash of the input has not changed and concluded that
the existing output would have been reproduced by gcc. You can see
what files wake is inspecting using `wake -v`.

    rm *.o
    wake build

If the object file is removed, or main.cpp modified, then wake will,
of course, rebuild the object file.

    wake -vx 'compileC'
    cat tutorial.wake

Now let's turn our attention back to the wake file.
As before, `def` introduces a new variable, `build` this time.
In this case, however, `build` is a function with a single argument `x`,
which it ignores. Build steps should typically be functions because we
don't want them to run unless specifically invoked.

build invokes the compileC function. In wake `f x y` is read as
function `f` run on `x` and `y`. In C this would be `f(x, y)`. So
compileC is being run on four arguments.

Notice that the type of compileC is `(variant: String) => (extraFlags: List String) => (headers: List Path) => (cfile: Path) => Path`.
This should be read as "a function that takes a `String` named `variant`,
then a `List` of `Strings` named `extraFlags`, then a `List` of `Paths` named `headers`, 
another Path named `cfile`, and finally returns a Path."

Indeed, we can see in our use of compileC, we passed a String for the
first argument and `source` which produces a Path for the last argument.
The second and third arguments are Lists.
`Nil` is the empty List and `(x, y, Nil)` is a List with x and y.

The arguments to compileC are:
  1. the build variant as a String
  2. a list of additional compiler options
  3. a list of legal input files (more on this later)
  4. the name of the file to compile

The output of compileC is the `Path` of the object file produced.

## Compiling and linking two CPP files

    echo 'int helper() { return 42; }' > help.cpp
    git add help.cpp
    cat > tutorial.wake <<'EOF'
    def variant = "native-cpp11-release"
    def build _ =
      def main = compileC variant ("-I.", Nil) Nil (source "main.cpp")
      def help = compileC variant ("-I.", Nil) Nil (source "help.cpp")
      linkO variant ("-lm", Nil) (main, help, Nil) "tutorial"
    EOF
    wake build

We've changed a few things.  Trivial stuff first: We put the build variant
into a shared variable so we can change it more easily.  We replaced the
argument to build with `_`, which means we don't care about it, so don't
give the argument a name.

build now uses multiple lines.  In wake, the last line is always the value
of the define.  As `build` is a function, you could say the last line is the
return value.  All lines except the last line must themselves define new
variables.  Also, wake is whitespace sensitive.  It knows that `main` and
`help` are variables defined inside `build`, while `variant` is not.

    rm *.o
    wake build

Blink and you'll miss it.  Wake ran both the compile for `main.cpp` and
`help.cpp` at the same time.  Even though our wake file said to compile
`main.cpp` first and then compile `help.cpp`, wake understands that these
steps can be run in parallel.  Conversely, because `main` and `help` are
needed by the linking step, that step will wait for both object files to
compile.

However, wake probably did NOT re-link the objects of the program this
time.  That's because wake remembers the hashes of the objects it gave to
the linker last time.  The rebuilt object files have the same hashes, so
there was no need to re-link the program.  Similarly, whitespace only
changes to the files will not cause a re-link.

    echo 'int main() { return 2; }' > main.cpp
    wake build

Of course, if you change `main.cpp` meaningfully, both it will be recompiled
and the program re-linked.

## Using header files

    echo 'int helper();' > helper.h
    echo -e '#include "helper.h"\nint main() { return helper(); }' > main.cpp
    git add helper.h
    cat > tutorial.wake <<'EOF'
    def variant = "native-cpp11-release"
    def build _ =
      def headers = sources here `.*\.h`
      def main = compileC variant ("-I.", Nil) headers (source "main.cpp")
      def help = compileC variant ("-I.", Nil) headers (source "help.cpp")
      linkO variant ("-lm", Nil) (main, help, Nil) "tutorial"
    EOF
    wake build

Recall that the third argument to `compileC` is a list of legal input files.
Wake forbids jobs from reading files in the workspace that are not declared
inputs.  This means that if you include header files, they must be
declared in the list of legal inputs passed to compileC or the compile
will fail.

In this example, we've used the `sources` command to find all the header
files in the same directory and pass them as legal inputs to gcc.  The
keyword `here` expands to the directory of the wake file.  The second
argument to `sources` is a regular expression to select which files to
return. We've used ``` `` ```s here which define regular expression literals.
These literals are similar to strings with escapes disabled.
If we expressed this as a `String` using `""`s we would have to write `".*\\.h"`
and it would require a function call to convert it to a regular expression.
In addition, the parser verifies that regular expression literals are legal.

Note that source files are those files tracked by git. Wake will never
return built files from a call to `sources`, helping repeatability.

In a classic Makefile it would be considered bad form to list all header
files as dependencies for all cpp files.  That's because make would
recompile every cpp file whenever any header file changes.  In wake, we
don't have this problem.  Wake monitors jobs to see which files they
actually used and remembers this for later builds.  Therefore, it's best in
wake to err on the side of caution (and convenience) by just listing all the
headers in directories that are interesting to the cpp files.

    wake -o main.native-cpp11-release.o

For this file, wake recorded that it needed both `main.cpp` and `helper.h`.

    wake -o help.native-cpp11-release.o

For this file, wake recorded that it only needed `help.cpp`, despite
`helper.h` being a legal input.

## Map and partial function evaluation

    cat > tutorial.wake <<'EOF'
    def variant = "native-cpp11-release"
    def build _ =
      def headers = sources here `.*\.h`
      def compile = compileC variant ("-I.", Nil) headers
      def objects = map compile (sources here `.*\.cpp`)
      linkO variant ("-lm", Nil) objects "tutorial"
    EOF
    wake build

Having to list all cpp files is cumbersome. You have probably organized
your codebase so that all the files in the current directory should be
linked together.  This example demonstrates how to support that.

Notice that we've defined `compile` to be `compileC` with every argument
supplied EXCEPT the `Path` of the file to compile.
This is known as "partial function evaluation" or "currying".
Thus, `compile` is a function that takes a `Path` and returns a `Path`.
We could equivalently express `compile` as:

    def compile x = compileC variant ("-I.", Nil) headers x

The argument `x` here is a bit more explicit, but not strictly necessary.

In either case, this allows us to write `compile (source "main.cpp")` to
compile a single cpp file, saving some typing.
However, we can use the `map` function to save even more! We use the
`sources` function to find all the cpp files. That gives us a `List` of
`Paths`. Recall that`compile` is a function that takes one argument, a `Path`.
`map` applies the function supplied as its first
argument to every element of the `List` supplied as its second argument.
Thus, `objects` is now a `List` of all the object files created by compiling
all the cpp files.  Our wake file is now both smaller and will automatically
work when new cpp files are added.

## Using libraries with pkg-config

    cat > tutorial.wake <<'EOF'
    def variant = "native-cpp11-release"
    def build _ =
      def headers = sources here `.*\.h`
      def zlib = pkgConfig "zlib" | getOrElse (makeSysLib "")
      def compile = compileC variant zlib.getSysLibCFlags headers
      def objects = sources here `.*\.cpp` | map compile
      linkO variant zlib.getSysLibLFlags objects "tutorial"
    EOF
    wake build

It's pretty common for programs to depend on system libraries.  These days,
most well maintained libraries supply a pkg-config file (`*.pc`) that helps
authors get the command-line arguments right without worrying where the
library was installed.

Wake has a helper function `pkgConfig` which returns a value that we can query
for information about the library like the cflags and lflags as shown. Don't
worry about the `| getOrElse ...` yet, more on that later.

## Dynamically creating a header file

    cat >> tutorial.wake <<'EOF'
    def info_h _ =
      def cmdline = which "uname", "-sr", Nil
      def os = job cmdline Nil
      def str = os.getJobStdout | getWhenFail ""
      def body = "#define OS {str}#define WAKE {version}\n"
      write "{here}/info.h" body # create with mode: rw-r--r--
    EOF
    wake info_h

This example creates a header file suitable for inclusion in our build.
The produced header includes the operating system the build ran on and the
version of wake used in the build.  We can make this non-source header file
available by changing `tutorial.wake` to include:

      def headers = info_h 0, sources here `.*\.h`

To understand what's happening in this example, let's break down all the new
methods being leveraged.

`which` is a function which searches wake's path for the named program.  On
most systems, `which "uname" = "/bin/uname"`.  Using `which` buys us a bit
of indirection and is usually good form to use with jobs.

`job` is the main method wake uses to invoke a job.  It takes two arguments,
a list of strings for the command-line and a list of paths of legal
inputs.  As with `compileC` and `linkO`, every file in the workspace which
the job needs must be listed in this second argument, or the job will not
have access to them.  Indeed, both `compileC` and `linkO` are implemented by
using `job` internally.

The value returned by `job` can be accessed in many ways. In this case, we use
`job.getJobStdout` to get the standard output from the command.
We'll address the `| getWhenFail ""` later. For now just note that since jobs can fail,
we're using the empty string `""` as a default. There is other information we can get from `job`.
`job.getJobStatus` is an `Integer` equal to the job's exit status.
`job.getJobOutputs` returns a `List` of `Paths` created by the job.

The `body` variable is created using string interpolation.  Inside a `""`
string, you can include wake expressions within `{}`s and they will be
inserted into the string.  In this example, we fill in the desired variables
into the string body.  `version` is just a `String` with the current wake
version.

Finally, we create the output file `info.h` with the desired contents.
`write` returns the path of created file once the file has been written.
This way, anything that depends on the return of our `info_h` method will
have to wait until `info.h` has been saved to disk. We also have an example
of a comment, `# ...`, reminding us of the default permissions used.

## Customizing job invocation

    cat >> tutorial.wake <<'EOF'
    def hax _ =
      "env"
      | makePlan "print environment" Nil
      | setPlanEnvironment ("HAX=peanut", "FOO=bar", Nil)
      | runJob
      | getJobStdout
      | getWhenFail ""
      | println
    EOF
    wake hax

Until now, we've been running processes with the default execution plan.
This means that all environment variables are removed and the job is
executed in the root of the workspace. However, it is possible to
customize the environment used by a job.

The underlying job execution model of wake uses two phases. First one
constructs a `Plan` object which describes what should be executed.
The `Plan` is then transformed into a `Job` by `runJob`. Before one
calls `runJob`, one can change various properties, like the environment
variables in this example.

This example also demonstrates the syntax `x | f 0 | g`. This should be
read like the pipe operator in a shell script. Ultimately, it will be
evaluated as `g (f 0 x)`, but when chaining together multiple
transformations to data, the pipe syntax becomes more readable.

Finally, `println` is very useful when debugging wake code. However,
be forewarned that the execution order of wake is not sequential!
This can result in `print` output that does not appear to follow the
definition order of your build program.

## Target execution depending on prior targets

Consider a build system where there is a program `feature-detect` that needs
to be compiled. Once that has been compiled, it gets run to determine which
features the system supports. Then, based on the output of `feature-detect`,
the build proceeds to build the `main-program`. This is the sort of example
that is impossible in a system like make, but fairly straight-forward in
wake:

    def build_feature_detect _ =
      def object = compileC variant Nil Nil (source "feature-detect.cpp")
      linkO variant Nil (object, Nil) "feature-detect"

    def run_feature_detect _ =
      def cmdline = (build_feature_detect 0).getPathName, "--detect-stuff-to-build", Nil
      def detect = job cmdline (source "some-config-file", Nil)
      def stdout = detect.getJobStdout | getWhenFail ""
      def filenames = tokenize ` ` stdout
      map source filenames

    def complex_build _ =
      def headers = sources here `.*\.h`
      def zlib = pkgConfig "zlib" | getOrElse (makeSysLib "")
      def compile = compileC variant zlib.getSysLibCFlags headers
      def objects = map compile (run_feature_detect 0)
      linkO variant zlib.getSysLibLFlags objects "tutorial"


## Publish/Subscribe

    cat >>tutorial.wake <<'EOF'
    topic animal: String
    publish animal = "Cat", Nil
    publish animal = "Dog", Nil
    publish animal = replace `u` "o" "Mouse", Nil
    def animals = subscribe animal
    EOF
    wake -x 'animals'

Wake includes a publish/subscribe interface to support accumulating
information between multiple files. `publish x = y` adds `y` to the `List` of
things which will be returned by a `subscribe x` expression. Note that
`animal` is not a variable; it is a topic, which is in a different
namespace than normal variables. Note that `y` must be a `List`.

This API can be used to accumulate all the unit tests in the workspace into
a single location that runs them all at once.  Keep in mind that the
published `List` can be of any type (including functions and data types), so
the types of workspace-wide information that can be accumulated this way is
wide open.  However, all publishes to a particular topic must agree to use
the same type in the `List`, or the files will not type check.

## Data types and pattern matching

So far, we've gotten a lot done with primitive types (`Integer`, `String`,
...) and `Lists`. However, wake does allow you to define your own data types.
These can then be analyzed using pattern matching.

Consider the follow program:

    cat >>tutorial.wake <<'EOF'
    data Animal =
      Cat (name: String)
      Dog (age: Integer)

    def strAnimal a = match a
      Cat x = "a cat called {x}"
      Dog y = "a {str y}-year-old dog"
    EOF
    wake -vx 'Cat'
    wake -x 'strAnimal (Dog 12)'
    wake -x 'strAnimal (Cat "Fluffy")'

The `data` keyword introduces a new type, `Animal`.
Types are always capitalized, and use a different namespace from variables.
As defined, `Animal` can either be a `Cat` or a `Dog`,
where `Cat`s have names and `Dog`s have ages.
The general syntax is `data TYPE = (CONS TYPE*)+`.
If we want the new type available to other files, we put an `export` in
front of the `data`, just like with variables.

As wake informs us, `Cat` is a function that takes a `String` and returns an
`Animal`. However, unlike normal functions, `Cat` is also a type constructor.
Type constructors differ from normal variables in that they are capitalized
and can be used in pattern matches.

The function `strAnimal` is an example of pattern matching. It consists of the
keyword `match` followed by the value to pattern match. On the following lines
we indent and provide one or more patterns.
A pattern always starts with a type constructor followed by a
number of values corresponding to the values in the constructor declaration.
These values are followed by `=` and then an expression that is the result
of when the pattern is matched. For example, in `Cat x = "a cat called {x}"`,
`x` is a new variable binding corresponding to `name` in the declaration, thus
it is a `String`. After `=` we have a `String` that is the result whenever `strAnimal`
is passed a `Cat`.

Note that when matching, the patterns must exhaustively catch all possibilities.
For example, it would be illegal to `match` but only provide a pattern for `Cat`.
A useful pattern is to provide a "default" pattern with `_` to match anything:

    def matchWithDefault a = match a
      Cat x = "a cat called {x}"
      _     = "not a cat!"


## Anonymous functions

Generally, one should define functions with a name.
This makes code more readable for other people.
However, sometimes the function is really just an after-thought.
For these cases wake makes it possible to define functions inline.

    wake -vx '\x x^2'
    wake -x 'map (\x x^2) (seq 10)'

The backslash syntax is an easy-to-type stand-in for the lambda symbol, λ.
While we've not talked about it, the wake language implements a "typed lambda
calculus". That's just a fancy way of saying you can pass functions to
functions. This syntax for inline functions is wake's homage to its roots.

The general syntax is `\ PATTERN BODY`. Yes, you can pattern match directly
inside a lambda expression. For that matter, you can do it with `def NAME
PATTERN = BODY` as well. For example:

    wake -x '(\ (Pair x y) x + y) (Pair 1 2)'

To make inline functions even easier to define, wake also supports a syntax
where one specifies the holes `_` in an expression and a function is created
which fills the holes from left to right.

    wake -vx '_ + 4'
    wake -x 'map (_ + 4) (seq 8)'
    wake -x 'seq 1000 | filter (_ % 55 == 0) | map str | catWith " "'

This hole-oriented syntax is not as powerful as lambda expressions, because
each argument can only be used once.  Furthermore, the functions are created
at block boundaries, which include `()`s, which can limit their usefulness.
Nevertheless, this syntax can be convenient.

## Downloading and parsing files

    cat >>tutorial.wake <<'EOF'
    def curl url =
      def file = simplify "{here}/{replace `.*/` '' url}"
      def cmdline = which "curl", "-o", file, url, Nil
      def curl = job cmdline Nil
      curl.getJobOutput

    def mathSymbols _ =
      def helper = match _
        code, _, "Sm", _ = intbase 16 code | omap integerToUnicode
        _                = None
      def url = "https://www.unicode.org/Public/9.0.0/ucd/UnicodeData.txt"
      def lines = curl url | read | getWhenFail "" | tokenize `\n`
      def codes = mapPartial (tokenize `;` _ | helper) lines
      catWith " " codes
    EOF
    wake mathSymbols

Above is a fun example using wake as a hybrid build/programming language.
This wake code downloads the Unicode symbol tables from the Internet using
curl and then grabs all the mathematical symbols from the table, formats
them, and returns the output.

As we've covered before, `job` is used to launch curl to download the table.
The `curl` function also makes use of `replace` with a regular expression
to split the filename out of the URL. `replace` accepts a "replacement" `String`
which it substitutes for every substring that matches the regular expression. We also use
the `simplify` function which transforms paths into canonical form.  In this,
case `simplify` removes the leading `"./"`.

If you look in `UnicodeData.txt`, you will see that it uses lines with semicolon
separated values, one for each Unicode code point.  Thus `lines` simply
splits the `String` returned by `read` into each code point description.
For each line, we then split it into the fields and pass the result to
`helper`.

`helper` uses a pattern match to extract the first and third arguments from
the `List`. If the third argument is of class `"Sm"`, ie: symbols/math, then
wake converts the hexadecimal from the first column into the Unicode code
point for that value and returns a `String` containing the value.
It uses the function `intbase` which converts a `String` encoded in a certain
base into an `Option Integer`. `Options` can either be `None`, which
means no value is available, or `Some x`, which is a value `x`. `Option` is
a data type like any other in wake.  It is particularly useful in those
situations where one would use a null pointer in a language like C.
The result is an `Option` since the `String` may not be a valid number in the given base.
`omap` is similar to `map` except it works on `Options` instead of `Lists`.
It will apply the given function to the value in a `Some` or simply return if
the `Option` is a `None`.

`mapPartial` is like `map`, except the function argument returns `Options`.
`mapPartial` will include the values of any `Somes` while ignoring `Nones`.
In this case, we use `Option` to only return values from `helper` where the code
point is a math symbol. `mapPartial` then creates a list out of only those
values helper returned. Try this, for an example:

    wake -x 'mapPartial (_) (None, Some "xx", Some "yy", None, Nil)'

As a final note, wake files must be UTF-8 encoded.
This means that you can use Unicode symbols in strings, comments,
identifiers, and even operators.
In fact, all the operators we just printed are legal in wake.

## Dealing with failure

Previously, we glossed over functions like `getOrElse` and `getWhenFail`.
In this section we will explain those as well as the more general concept
of dealing with operations that can fail in a build flow.

### Option

Many operations can fail like taking the first element (`head`) of a `List`
(it may be empty) or reading a file (it may not exist).
The simplest and most common way we deal with this in wake is with `Option` as we
have seen before.

    def myFunction l = match (head l)
      Some elt = elt
      None     = 0

As in the above, we can use pattern matching on `Option` to deal with the possibility
of `Some` or `None`. In this case, `myFunction` accepts a `List` of `Integers` and
returns the first element of the list or `0` if the list is empty.
A more concise way of accomplishing the same goal is to use `getOrElse` which will
return the value in a `Some` or a default if the `Option` is `None`.

    def myFunction l = head l | getOrElse 0

### Result

`Options` are a great way to deal with things that can fail in one obvious way
(eg. `head` fails when the `List` is empty), but sometimes an operation may have
multiple ways of failing (eg. reading a file, it may not exist or permission may be denied).
wake provides a type called `Result` for dealing with such cases.
`Result` is similar to `Option`: it can be either a `Pass p`,
where `p` represents the value of correct operation, or a `Fail f`,
where `f` is a description of how an operation failed.

    echo "contents" > existing.txt
    echo "contents" > denied.txt
    git add existing.txt denied.txt
    chmod 000 denied.txt
    cat >> tutorial.wake <<'EOF'
    def goodRead _ = read (source "existing.txt")
    def deniedRead _ = read (source "denied.txt")
    def badRead _ = read (source "nonexisting.txt")
    EOF
    wake goodRead
    wake deniedRead
    wake badRead

You should see something like:

    $ wake goodRead
    Pass "contents\n"
    $ wake deniedRead
    Fail (Error "read denied.txt: Permission denied" Nil)
    $ wake badRead
    Fail (Error "nonexisting.txt: not a source file" Nil)

Similarly to how we can `match` on `Options`, we can `match` on `Results`

    cat >> tutorial.wake <<'EOF'
    def safeRead filename = match (read (source filename))
      Pass txt = txt
      Fail _ = "Read failed!"
    EOF

This will attempt to read a file and return its contents,
returning `"Read failed!"` if the read failed for any reason.
Similarly to `getOrElse`, there is a function `getWhenFail` that will return the value
in a `Pass` or a default in the case of `Fail`.
We can rewrite the above as simply:

    def safeRead filename = getWhenFail "Read failed!" (read (source filename))

Or perhaps more clearly using `|`:

    def safeRead filename = source filename | read | getWhenFail "Read failed!"

### BadPath

Besides `Option` and `Result`, there is a 3rd important piece to handling failure: `BadPath`.
Recall that `Path` is the type used to represent paths on the file system.
The function `source` takes a `String` and returns a `Path` for a file under version control.
But what if the file doesn't exist? Previously, we called `source` on a non-existent file,
but we immediately called `read` on the returned `Path`. Try just `source`:

    wake -x 'source "nonexisting.txt"'

You should get `BadPath (Error "nonexisting.txt: not a source file" Nil)`.
Similarly to how `Result` can be either a `Pass` or a `Fail`,
a `Path` can be either a `Path` or a `BadPath`.
While this is technically redundant since `Result` could accomplish the same functionality,
it is much more convenient than having to use `Result` all over the place.
For example, without `BadPath`, source would have to return a `Result`,
and we would have to handle the result in order to `read` it:

    def safeRead filename = match (source filename)
      Pass path = match (read path)
        Pass content = content
        Fail _       = "Read failed!"
      Fail _ = "Read failed!"

Fundamentally, wake is about running jobs on files, and special support for
`BadPath` makes this easier to express.

When you pass `BadPaths` in the visible files to a job, the job will recognize this and
propagate the failure.

    wake -x 'job (which "gcc", "file.c", Nil) (source "file.c", Nil) | getJobOutputs'

Since the `source` will return a `BadPath`, the job will propagate the failure:

    BadPath (Error "file.c: not a source file" Nil), Nil

This means that when passing the outputs from one job to another,
you don't have to worry about jobs failing, wake will handle it for you!

You may have noticed that the `BadPath` above contains a type called `Error`.
`Error` is a type that contains a `String` "cause", and a `List` of `Strings` stack trace.
You'll notice the cause is `"file.c: not a source file"`, but the stack is just `Nil`.
Wake does not actually maintain a call stack like traditional languages,
so by default `Errors` will contain an empty `List` for the stack.
If you run wake with `-d` (or `--debug`), it will simulate a stack:

    wake -dx 'job (which "gcc", "file.c", Nil) (source "file.c", Nil) | getJobOutputs'

You should see much more information. Note that inspection operations like `-o` or `-i`
are also affected by `-v` and `-d`.

You can construct an `Error` directly, or use `makeError` which simply takes a `String`
cause and will record the Stack.

### Packages

When you run wake from the command-line, you have access to all the defined
values in the current directory's package.
However, packages do not have access to each others defines.
The moment you have more than one wake file, you will need to define
packages for your files.
The package system in wake has its [own documentation](tour/packages.adoc).

### Ignore wake source files

There are occasions where you want wake to ignore wake source files (`*.wake`).
Examples include testing, where you could exclude some files from regular usage,
or a directory structure that includes duplicate repository checkouts, where
duplicate symbol definitions would raise an error.

Wake looks for files named `.wakeignore` containing patterns.
The pattern language is shell filename globbing.
One pattern per line, each relative to the path of the `.wakeignore` file.

The concrete syntax is:
- empty lines are ignored
- lines starting with a `#` are comments and are ignored
- `?` matches a single non-slash character
- `*` matches any number (including zero) of non-slash characters
- `[a-z]` matches a single lower-case character
- `/**/` in an expression like `foo/**/bar` stands in for any number of directories (including zero)
- `foo/**` recursively matches all contents of the directory `foo`
- `**/bar` matches all files `bar` contained in this directory or any subdirectory

An example:

    workspace
        ├── wake.db
        ├── repo1
        │   └── foo.wake
        └── repo2
            ├── .wakeignore
            └── repo1
                └── foo.wake

If `repo1` is checked-out out twice like above, then if a `.wakeignore` file
at path `workspace/repo2` contained `repo1/**` then `foo.wake` would not
be read twice.

The following patterns in `workspace/repo2/.wakeignore` would all match
the above `foo.wake`:

    repo1/foo.wake
    repo1/**
    repo1/**/foo.wake
    **/[a-z]?[o].wake
