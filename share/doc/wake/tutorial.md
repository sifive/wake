# Wake Tutorial

This tutorial assumes you have wake and git installed in your path.
Code sections are intended to be copy-pasted into a terminal.

## Invoking wake


    mkdir ~/tutorial
    cd ~/tutorial
    wake --init .
    wake -x '5 + 6'

This sequence of commands creates a new workspace managed by wake.
The `--init` option is used to create an initial `wake.db` to record the state
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

    echo 'def hello = "Hello World"' > tutorial.wake
    wake -x 'hello'

Wake processes all wake files (`*.wake`) which are in the workspace,
so our new file `tutorial.wake` is available to us from the command-line.
The syntax `def x = y` introduces a new variable `x` whose value is equal
to `y`. In this case, `y` is just a `String`.

## Data types and pattern matching

So far, we've gotten a lot done with primitive types (`Integer`, `String`,
...) and `Lists`. However, wake does allow you to define your own data types.
These can then be analyzed using pattern matching.

Consider the follow program:

    cat >> tutorial.wake <<'EOF'
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

When, as above, we only care about a single constructor of a type, we can use
the `require` keyword instead. If the object passed to the `require` is of a
different constructor, the value is passed up a level and the evaluation is
returned from early (similar to a `return` statement in C-derived languages).

    def addDogYears years pet =
        require Dog age = pet
        Dog (age + years * 7)

Note that this destructor pattern (`Dog age`, `Cat x`) not only works for
`match` and `require`, but also `def` when the type only has a single
constructor/destructor. The number of new variable names needs to match the
number of parameters to the constructor, but `_` or something starting with it
can be used for parameters which you don't care about using.

    def renameCat name pet =
        require Cat _ = pet
        Cat name

## Compiling a CPP file

    git init .
    echo 'int main() { return 0; }' > main.cpp
    git add main.cpp
    cat > tutorial.wake <<'EOF'
    from wake import _
    from gcc_wake import compileC
    def build _args =
        require Pass main =
            source "main.cpp"
        compileC "native-cpp11-release" ("-I.", Nil) Nil main
    EOF
    wake build

The git commands are truly important; wake finds files through the git index.
However, let's just ignore the contents of`tutorial.wake` for the moment and
focus on how wake behaves.

First, notice that we did not use `-x` when invoking wake this time.
The default operation of wake invokes the subcommand (in this case `build`)
specified on the command-line, with any additional command-line arguments
passed to that function.

Each executed job is recorded, and we can retrieve all the information about
them as desired. In this case, that means we see that it ran g++ to produce an
object file.

    wake --last

As the build instructions become more complex, the `--last` output becomes
larger and harder to sort through. You can always pipe it through less or your
favorite pager, or you can filter it by the specific files involved.

    wake -i main.cpp
    wake -o main.native-cpp11-release.o

Notice that the listed environment is much simpler than the actual environment
of your system; wake tries to minimize anything which might cause two runs to
give different results.

If the resulting object file is removed, or main.cpp modified, then wake will,
of course, rebuild the object file.

    rm *.o
    wake build

Because of the underlying database, wake can be a bit smarter about rebuilds:

    touch main.cpp
    wake build

Notice that wake does not rebuild the object file in this case.  It
checked that the hash of the input has not changed and concluded that
the existing output would have been reproduced by gcc. You can see
what files wake is inspecting using `wake -v --last` or another selector.

    cat tutorial.wake

Now let's turn our attention back to the wake file. The `from ... import ...`
lines indicate that we want to use something from an external package. Any file
which doesn't specify *any* imports will automatically import everything in the
standard `wake` package, but as soon as we ask for something else (like
`compileC` which was defined in a file that said it was part of `gcc_wake`), we
need to explicitly list the `wake` import ourselves.

As before, `def` introduces a new variable, `build` this time.
In this case, however, `build` is a function with a single argument `_args`,
which it ignores (the leading underscore silences a compiler warning). Build
steps should typically be functions -- even if this means giving them a
"useless" argument -- because we don't want them to run unless specifically
invoked.

Skipping to the end, `build` invokes the compileC function which we imported
earlier. In wake `f x y` is read as function `f` run on `x` and `y`. In C this
would be `f(x, y)`. So compileC is being run on four arguments.

    wake --in gcc_wake -vx compileC

Notice that the type of compileC is `(variant: String) => (extraFlags: List String) => (headers: List Path) => (cfile: Path) => Result (List Path) Error`.
This should be read as "a function that takes a `String` named `variant`,
then a `List` of `Strings` named `extraFlags`, then a `List` of `Paths` named
`headers`, another Path named `cfile`, and finally returns a `List Path` if
nothing failed but an `Error` if something did."

Indeed, we can see in our use of compileC, we passed a `String` for the
first argument and `main` -- which is indeed a `Path` -- for the last argument.
The second and third arguments are `Lists`; `Nil` is the empty list and `(x, y, Nil)`
is a list with two elements x and y. We'll look at the returned value later.

Back to the `require` line, `Pass` is a constructor to the `Result` type which
takes a single argument. Unlike compileC, `source` returns a `Result Path Error`
(i.e. a single `Path` instead of multiple). If wake can't find `main.cpp` --
likely because it hasn't been added to git -- then we don't have any need or
desire to try compiling a non-existent file, so if the `require` gets passed a
`Fail` instead of a `Pass` (the second constructor to `Result`; we'll cover that
more later) we break out of `build` early and return that failure.

Unlike `compileC`, the `source` function returns a `Result Path Error` so its
`Pass` constructor and destructor (and thus `main`) only wrap a single `Path`.
This is also where the git commands come into play, as `source` only searches
the git index to find files. Don't forget to add anything you create!

## Compiling and linking two CPP files

    echo 'int helper() { return 42; }' > help.cpp
    git add help.cpp
    cat > tutorial.wake <<'EOF'
    from wake import _
    from gcc_wake import compileC linkO
    def variant = "native-cpp11-release"
    def build _ =
        def mainResult =
            require Pass mainSrc =
                source "main.cpp"
            compileC variant ("-I.", Nil) Nil mainSrc
        def helpResult =
            require Pass helpSrc =
                source "help.cpp"
            compileC variant ("-I.", Nil) Nil helpSrc
        def tutorialResult =
            require Pass main = mainResult
            require Pass help = helpResult
            linkO variant ("-lm", Nil) (main, help, Nil) "tutorial"
        tutorialResult
    EOF
    wake build

We've changed a few things.  Trivial stuff first: We put the build variant
into a shared variable so we can change it more easily.  We replaced the
argument to build with `_`, which means we care so little about it that we
don't even need to give the argument a name.

In wake, the last line is always the value of the define.  As `build` is a
function, you could say the last line is the return value.  All lines except the
last line must themselves define new variables.  Also, wake is whitespace
sensitive.  It knows that `main` and `help` are variables defined inside
`tutorialResult`, while `variant` will have a much broader scope.

    rm *.o
    wake build

Blink and you'll miss it.  Wake ran both the compile for `main.cpp` and
`help.cpp` at the same time.  Even though our wake file said to compile
`main.cpp` first and then compile `help.cpp`, wake understands that these
steps can be run in parallel.  Conversely, because `main` and `help` are
needed by the linking step, that step will wait for both object files to
compile.

Note that in order to achieve that parallelism, we had to define `mainResult`
and `helpResult` as `def` objects, and only unwrap the `Result Path Error`
afterward. In order for `require` to define values or return early based on the
exact constructor, it enforces a degree of serialization on the code. This
pattern of an initial `def` with its accompanying `require` inside the specific
block(s) which use it allows wake itself to determine what values depend on
which others, and enables the best concurrency through the build. The `source`
calls happen fast enough that we don't particularly care about concurrency.

However, wake probably did NOT re-link the objects of the program this
time.  That's because wake remembers the hashes of the objects it gave to
the linker last time.  The rebuilt object files have the same hashes, so
there was no need to re-link the program.  Similarly, whitespace-only
changes to the files will not cause a re-link.

    echo 'int main() { return 2; }' > main.cpp
    wake build

Of course, if you change `main.cpp` meaningfully, it will be recompiled (without
also recompiling `help.cpp`) and the program re-linked.

## Using header files

    echo 'int helper();' > helper.h
    echo -e '#include "helper.h"\nint main() { return helper(); }' > main.cpp
    git add helper.h
    cat > tutorial.wake <<'EOF'
    from wake import _
    from gcc_wake import compileC linkO
    def variant = "native-cpp11-release"
    def build _ =
        require Pass headers =
            sources here `.*\.h`
        def mainResult =
            require Pass mainSrc =
                source "main.cpp"
            compileC variant ("-I.", Nil) headers mainSrc
        def helpResult =
            require Pass helpSrc =
                source "help.cpp"
            compileC variant ("-I.", Nil) headers helpSrc
        def tutorialResult =
            require Pass main = mainResult
            require Pass help = helpResult
            linkO variant ("-lm", Nil) (main, help, Nil) "tutorial"
        tutorialResult
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
    from wake import _
    from gcc_wake import compileC linkO
    def variant = "native-cpp11-release"
    def build _ =
        def compile =
            require Pass headers =
                sources here `.*\.h`
            compileC variant ("-I.", Nil) headers
        def objectsResult =
            require Pass srcFiles =
                sources here `.*\.cpp`
            findFail (map compile srcFiles)
        def tutorialResult =
            require Pass objects = objectsResult
            linkO variant ("-lm", Nil) objects "tutorial"
        tutorialResult
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

In either case, this allows us to write `compile cppFile` to
compile a single cpp file, saving some typing.
However, we can use the `map` function to save even more! We use the
`sources` function to find all the cpp files. That gives us a `List` of
`Paths`. Recall that`compile` is a function that takes one argument, a `Path`.
`map` applies the function supplied as its first
argument to every element of the `List` supplied as its second argument.
Thus, `objects` is now a `List` of all the potential object files created by
compiling all the cpp files.

After the `map`, we're left with a `List (Result Path Error)` -- several
compilations which would return a single `Path` each if they succeed, but which
may individually fail. However, `require` can only unwrap the outermost type, so
we need to switch the `List` and the `Result`. This is exactly what `findFail`
does: if any of the inner computations failed, then the entire thing fails, but
otherwise it returns the successes as a `Result (List Path) Error`.

Our wake file is now both smaller and will automatically work when new cpp files
are added.

## Using libraries with pkg-config

    cat > tutorial.wake <<'EOF'
    from wake import _
    from gcc_wake import compileC linkO
    def variant = "native-cpp11-release"
    def build _ =
        def zlib =
            pkgConfig "zlib"
            | getOrElse (makeSysLib "")
        def compile =
            require Pass headers =
                sources here `.*\.h`
            compileC variant zlib.getSysLibCFlags headers
        def objectsResult =
            require Pass srcFiles =
                sources here `.*\.cpp`
            findFail (map compile srcFiles)
        def tutorialResult =
            require Pass objects = objectsResult
            linkO variant zlib.getSysLibLFlags objects "tutorial"
        tutorialResult
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
        def cmdline =
            which "uname", "-sr", Nil
        def os =
            job cmdline Nil
        def str =
            os.getJobStdout
            | getWhenFail ""
        def body =
            "#define OS {str}#define WAKE {version}\n"
        write "{here}/info.h" body # create with mode: rw-r--r--
    EOF
    wake info_h

This example creates a header file suitable for inclusion in our build.
The produced header includes the operating system the build ran on and the
version of wake used in the build.  We can make this non-source header file
available by changing `tutorial.wake` to include:

        require Pass headers =
            require Pass hFiles =
                sources here `.*\.h`
            require Pass infoFile =
                info_h Unit
            Pass (infoFile, hFiles)

(Unit is essentially a dummy value to pass something to `info_h` so it generates
the header, without making it seem like the exact value is important.)

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
`write` returns the path of created file if it has been successfully written.
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
        require Pass object =
            require Pass srcFile =
                source "feature-detect.cpp"
            compileC variant Nil Nil srcFile
        linkO variant Nil (object, Nil) "feature-detect"

    def run_feature_detect _ =
        require Pass featureDetectBin =
            build_feature_detect Unit
        require Pass config =
            source "some-config-file"
        def cmdline =
            featureDetectBin.getPathName, "--detect-stuff-to-build", Nil
        def detect =
            job cmdline (config, Nil)
        def stdout =
            detect.getJobStdout
            | getWhenFail ""
        def filenames =
            tokenize ` ` stdout
        map source filenames
        | findFail

    def complex_build _ =
        def headersResult =
            sources here `.*\.h`
        def zlib =
            pkgConfig "zlib"
            | getOrElse (makeSysLib "")
        def compile srcFile =
            require Pass headers = headersResult
            compileC variant zlib.getSysLibCFlags headers srcFile
        def featuresResult =
            run_feature_detect Unit
        def objectsResult =
            require Pass features = featuresResult
            map compile features
            | findFail
        def tutorialResult =
            require Pass objects = objectsResult
            linkO variant zlib.getSysLibLFlags objects "tutorial"
        tutorialResult

## Publish/Subscribe

    cat >> tutorial.wake <<'EOF'
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
namespace than normal variables. Note also that `y` must be a `List`.

For example, this API can be used to accumulate all the unit tests in the
workspace into a single location that runs them all at once.  Keep in mind that
the published `List` can be of any type (including functions and data types), so
the types of workspace-wide information that can be accumulated this way is
wide open.  However, all publishes to a particular topic must agree to use
the same type in the `List`, or the files will not type check.


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

    cat >> tutorial.wake <<'EOF'
    def curl url =
        def file =
            simplify "{here}/{replace `.*/` '' url}"
        def cmdline =
            which "curl", "-o", file, url, Nil
        def curl =
            job cmdline Nil
        curl.getJobOutput

    def mathSymbols _ =
        def helper = match _
            code, _, "Sm", _ =
                intbase 16 code
                | omap integerToUnicode
            _ = None
        def unicodeDataResult =
            curl "https://www.unicode.org/Public/9.0.0/ucd/UnicodeData.txt"
        def lines =
            require Pass unicodeData = unicodeDataResult
            read unicodeData
            | getWhenFail ""
            | tokenize `\n`
        def codes =
            mapPartial (tokenize `;` _ | helper) lines
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

    def myFunction l =
        head l
        | getOrElse 0

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
    def goodRead _ =
        require Pass existing =
            source "existing.txt"
        read existing
    def deniedRead _ =
        require Pass denied =
            source "denied.txt"
        read denied
    def badRead _ =
        require Pass nonexisting =
            source "nonexisting.txt"
        read nonexisting
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
    def safeRead file = match (read file)
        Pass txt = txt
        Fail _ = "Read failed!"
    EOF

This will attempt to read a file and return its contents,
returning `"Read failed!"` if the read failed for any reason.
Similarly to `getOrElse`, there is a function `getWhenFail` that will return the value
in a `Pass` or a default in the case of `Fail`.
We can rewrite the above as simply:

    def safeRead file =
        getWhenFail "Read failed!" (read file)

Or perhaps more clearly using `|`:

    def safeRead file =
        read file
        | getWhenFail "Read failed!"

You may have noticed that the `Results` above contain a type called `Error`.
`Error` is a type that contains a `String` "cause", and a `List` of `Strings` stack trace.
You'll notice the cause is `"nonexisting.txt: not a source file"`, but the stack is just `Nil`.
Wake does not actually maintain a call stack like traditional languages,
so by default `Errors` will contain an empty `List` for the stack.
If you run wake with `-d` (or `--debug`), it will simulate a stack:

    wake -dx 'job (which "gcc", "file.c", Nil) (source "file.c", Nil) | getJobOutputs'

You should see much more information. Note that inspection operations like `-o` or `-i`
are also affected by `-v` and `-d`.

You can construct an `Error` directly, or use `makeError` which simply takes a
`String` cause and will record the Stack. There is even another function
`failWithError` which takes that same `String` and returns a `Result a Error`
for the very common case where you'd wrap the `Error` in a `Fail` immediately
after creating it.

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
