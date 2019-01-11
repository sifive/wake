# Wake Tutorial

This tutorial assumes you have wake and git installed in your path.
Code sections are intended to be copy-pasted into a terminal.

## Invoking wake


    mkdir tutorial
    cd tutorial
    wake --init .
    wake '5 + 6'

This sequence of commands creates a new workspace managed by wake.
The `init` option is used to create an initial `wake.db` to record the state
of the build in this workspace.
Whenever you run wake, it searches for a `wake.db` in parent directories.
The first `wake.db` found defines what wake considers to be the workspace.
You can thus safely run wake in any sub-directory of tutorial and wake
will be aware of all the relevant dependencies and rules.

The final output of wake run on an expression is always of the form
`expression: type = value`. In this case, 5 + 6 results in value 11,
which is an `Integer`.

    git init .
    echo 'global def hello = "Hello World"' > tutorial.wake
    git add tutorial.wake
    wake hello

Wake processes all versioned wake files (`*.wake`) which are in the workspace.
A very common mistake is to create a new wake file and run wake without first
adding that file to version control. In that case, the file will not be read,
and you will likely get an error like `Variable reference xyz is unbound`.

The syntax `def x = y` introduces a new variable `x` whose value is equal
to `y`. In this case, `y` is just a `String`. The `global` is necessary to
make `"hello"` available to other wake files, including what we type on the
command-line.

## Compiling a CPP file

    echo 'int main() { return 0; }' > main.cpp
    echo 'global def build x = compileC "native-cpp11-release" ("-I.", Nil) Nil "main.cpp"' > tutorial.wake
    git add main.cpp
    wake 'build 0'

Let's just ignore the wake file for the moment and focus on how wake behaves.
First, by default wake prints out the jobs it has executed.
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
    wake 'build 0'

Notice that wake does not rebuild the object file in this case.  It
checked that the hash of the input has not changed and concluded that
the existing output would have been reproduced by gcc. You can see
what files wake is inspecting using `wake -v`.

    rm *.o
    wake 'build 0'

If the object file is removed, or main.cpp modified, then wake will,
of course, rebuild the object file.

    wake 'compileC'
    cat tutorial.wake

Now let's turn our attention back to the wake file.
As before, `def` introduces a new variable, `build` this time.
In this case, however, `build` is a function with a single argument `x`,
which it ignores. Build steps should typically be functions because we
don't want them to run unless specifically invoked.

build invokes the compileC function. In wake `f x y` is read as
function `f` run on `x` and `y`. In C this would be `f(x, y)`. So
compileC is being run on four arguments. 

Notice that the type of compileC is `String => List String => List String => String => String`.
This should be read as "a function that takes a String, then a List of
Strings, then another List of Strings, another String, and finally returns a
String."

Indeed, we can see in our use of compileC, we passed a String for the
first and last arguments. The second and third arguments are Lists.
`Nil` is the empty List and `(x, y, Nil)` is a List with x and y.

The arguments to compileC are:
  1. the build variant as a String
  2. a list of additional compiler options
  3. a list of legal input files (more on this later)
  4. the name of the file to compile

The output of compileC is the name of the object file produced.

## Compiling and linking two CPP files

    echo 'int helper() { return 42; }' > help.cpp
    git add help.cpp
    cat > tutorial.wake <<EOF
    def variant = "native-cpp11-release"
    global def build _ =
      def main = compileC variant ("-I.", Nil) Nil "main.cpp"
      def help = compileC variant ("-I.", Nil) Nil "help.cpp"
      linkO variant ("-lm", Nil) (main, help, Nil) "tutorial"
    EOF
    wake 'build 0'

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
    wake 'build 0'

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
    wake 'build 0'

Of course, if you change `main.cpp` meaningfully, both it will be recompiled
and the program re-linked.

## Using header files

    echo 'int helper();' > helper.h
    echo -e '#include "helper.h"\nint main() { return helper(); }' > main.cpp
    git add helper.h
    cat > tutorial.wake <<EOF
    def variant = "native-cpp11-release"
    global def build _ =
      def headers = sources here '.*\.h'
      def main = compileC variant ("-I.", Nil) headers "main.cpp"
      def help = compileC variant ("-I.", Nil) headers "help.cpp"
      linkO variant ("-lm", Nil) (main, help, Nil) "tutorial"
    EOF
    wake 'build 0'

Recall that the third argument to `compileC` is a list of legal input files.
Wake forbids jobs from reading files in the workspace that are not declared
inputs.  This means that if you include header files, they must be
declared in the list of legal inputs passed to compileC or the compile
will fail.

In this example, we've used the `sources` command to find all the header
files in the same directory and pass them as legal inputs to gcc.  The
keyword `here` expands to the directory of the wake file.  The second
argument to `sources` is a regular expression to select which files to
return. We've used `''`s here which define strings with escapes disabled.
If we had used `""`s we would have had to write `".*\\.h"`, instead.

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

    cat > tutorial.wake <<EOF
    def variant = "native-cpp11-release"
    global def build _ =
      def headers = sources here '.*\.h'
      def compile = compileC variant ("-I.", Nil) headers
      def objects = map compile (sources here '.*\.cpp')
      linkO variant ("-lm", Nil) objects "tutorial"
    EOF
    wake 'build 0'

Having to list all cpp files is cumbersome.  Probably you've organized
your codebase so that all the files in the current directory should be
linked together.  This example demonstrates how to support that.

Notice that we've defined `compile` to be `compileC` with every argument
supplied EXCEPT the name of the file to compile.  We could now write
`compile "main.cpp"` to compile a single cpp file, saving some typing.

However, we can also use the `map` function to save even more!  We use the
`sources` function to find all the cpp files.  That gives us a list of
strings.  `compile` is a function which needs to take only one more
`String`.  What `map` does is apply the function supplied as its first
argument to every element in the list supplied as its second argument. 
Thus, `objects` is now a list of all the object files created by compiling
all the cpp files.  Our wake file is now both smaller and will automatically
work when new cpp files are added.

## Using libraries with pkg-config

    cat > tutorial.wake <<EOF
    def variant = "native-cpp11-release"
    global def build _ =
      def headers = sources here '.*\.h'
      def compile = compileC variant (cflags "zlib") headers
      def objects = map compile (sources here '.*\.cpp')
      linkO variant (libs "zlib") objects "tutorial"
    EOF
    wake 'build 0'

It's pretty common for programs to depend on system libraries.  These days,
most well maintained libraries supply a pkg-config file (`*.pc`) that helps
authors get the command-line arguments right without worrying where the
library was installed.

Wake has a pair of helper methods that make this easy, as shown.

## Dynamically creating a header file

    cat >> tutorial.wake <<EOF
    global def info_h _ =
      def cmdline = which "uname", "-sr", Nil
      def os = job cmdline Nil
      def body = "#define OS {os.stdout}#define WAKE {version}\n"
      write 0644 "{here}/info.h" body # create with mode: rw-r--r--
    EOF
    wake 'info_h 0'

This example creates a header file suitable for inclusion in our build. 
The produced header includes the operating system the build ran on and the
version of wake used in the build.  We can make this non-source header file
available by changing `tutorial.wake` to include:

      def headers = info_h 0, sources here '.*\.h'

To understand what's happening in this example, let's break down all the new
methods being leveraged. 

`which` is a function which searches wake's path for the named program.  On
most systems, `which "uname" = "/bin/uname"`.  Using `which` buys us a bit
of indirection and is usually good form to use with jobs.

`job` is the main method wake uses to invoke a job.  It takes two arguments,
a list of strings for the command-line and a list of strings of legal
inputs.  As with `compileC` and `linkO`, every file in the workspace which
the job needs must be listed in this second argument, or the job will not
have access to them.  Indeed, both `compileC` and `linkO` are implemented by
using `job` internally. The value returned by `job` can be accessed in many
ways. `job.stdout` provides the standard output from the command.
`job.status` is an `Integer` equal to the job's exit status. `job.outputs`
returns a list of string pairs, with the output filename and it's hash.

The `body` variable is created using string interpolation.  Inside a `""`
string, you can include wake expressions within `{}`s and they will be
inserted into the string.  In this example, we fill in the desired variables
into the string body.  `version` is just a `String` with the current wake
version.

Finally, we create the output file `info.h` with the desired contents.
`write` returns the file name created once the file has been written.
This way, anything that depends on the return of our `info_h` method will
have to wait until `info.h` has been saved to disk. We also have an example
of a comment, `# ...`, which translates the magic octal value `0644` to
something more human-readable.

## Target execution depending on prior targets

Consider a build system where there is a program `feature-detect` that needs
to be compiled. Once that has been compiled, it gets run to determine which
features the system supports. Then, based on the output of `feature-detect`,
the build proceeds to build the `main-program`. This is the sort of example
that is impossible in a system like make, but fairly straight-forward in
wake:

    def build_feature_detect _ =
      def object = compileC variant Nil Nil "feature-detect.cpp"
      linkO variant Nil (object, Nil) "feature-detect"

    def run_feature_detect _ =
      def cmdline = (build_feature_detect 0), "--detect-stuff-to-build", Nil
      def detect = job cmdline ("some-config-file", Nil)
      tokenize " " detect.stdout

    global def complex_build _ =
      def headers = sources here '.*\.h'
      def compile = compileC variant (cflags "zlib") headers
      def objects = map compile (run_feature_detect 0)
      linkO variant (libs "zlib") objects "tutorial"

## Publish/Subscribe

    cat >>tutorial.wake <<EOF
    publish animal = "Cat"
    publish animal = "Dog"
    publish animal = replace "u" "o" "Mouse"
    global def animals = subscribe animal
    EOF
    wake 'animals'

Wake includes a publish/subscribe interface to support accumulating
information between multiple files. `publish x = y` adds `y` to the list of
things which will be returned by a `subscribe x` expression. Note that
`animal` is not a variable; it is a topic, which is in a different
namespace than normal variables.

This API can be used to accumulate all the unit tests in the workspace into
a single location that runs them all at once.  Keep in mind that the
published list can be of any type (including functions and data types), so
the types of workspace-wide information that can be accumulated this way is
wide open.  However, all publishes to a particular topic must agree to use
the same type in the list, or the files will not type check.

## Data types and pattern matching

So far, we've gotten a lot done with primitive types (`Integer`, `String`,
...) and Lists. However, wake does allow you to define your own data types.
These can then be analyzed using pattern matching.

Consider the follow program:

    cat >>tutorial.wake <<EOF
    global data Animal =
      Cat String
      Dog Integer

    global def strAnimal a = match a
      Cat x = "a cat called {x}"
      Dog y = "a {y}-year-old dog"
    EOF
    wake 'Cat'
    wake 'strAnimal (Dog 12)'
    wake 'strAnimal (Cat "Fluffy")'

The `data` keyword introduces a new type, `Animal`.
Types are always capitalized, and use a different namespace from variables.
As defined, `Animal` can either be a `Cat` or a `Dog`,
where `Cat`s have names and `Dog`s have ages.
The general syntax is `data TYPE = (CONS TYPE*)+`.
If we want the new type available to other files, we put a `global` in
front of the `data`, just like with variables.

As wake informs us, `Cat` is a variable that takes a `String` and returns an
`Animal`. However, unlike normal variables, `Cat` is also a type constructor.
Type constructors differ from normal variables in that they are capitalized
and can be used in pattern matches.

## Anonymous functions

Generally, one should define functions with a name.
This makes code more readable for other people.
However, sometimes the function is really just an after-thought.
For these cases wake makes it possible to define functions inline.

    wake '\x x^2'
    wake 'map (\x x^2) (seq 10)'

The backslash syntax is an easy-to-type stand-in for the lambda symbol, λ.
While we've not talked about it, the wake language implements a "typed lambda
calculus". That's just a fancy way of saying you can pass functions to
functions. This syntax for inline functions is wake's homage to its roots.

The general syntax is `\ PATTERN BODY`. Yes, you can pattern match directly
inside a lambda expression. For that matter, you can do it with `def NAME
PATTERN = BODY` as well. For example:

    wake '(\ (Pair x y) x + y) (Pair 1 2)'

To make inline functions even easier to define, wake also supports a syntax
where one specifies the holes `_` in an expression and a function is created
which fills the holes from left to right.

    wake '_ + 4'
    wake 'map (_ + 4) (seq 8)'

This hole-oriented syntax is not as powerful as lambda expressions, because
each argument can only be used once.  Furthermore, the functions are created
at block boundaries, which include `()`s, which can limit their usefulness.
Nevertheless, sometimes this syntax can be convenient, too.

## Downloading and parsing files

    cat >>tutorial.wake <<EOF
    def curl url =
      def file = simplify "{here}/{head (extract '.*/(.*)' url)}"
      def cmdline = which "curl", "-o", file, url, Nil
      def curl = job cmdline (here, Nil)
      curl.output

    global def mathSymbols _ =
      def helper = match _
        code, _, class, _ =
          if class ==^ "Sm" then Some (code2str (intbase 16 code)) else None
        _ = None
      def url = "ftp://ftp.unicode.org/Public/UNIDATA/UnicodeData.txt"
      def lines = tokenize "\n" (read (curl url))
      def codes = mapPartial (helper $ tokenize ";" _) lines
      catWith " " codes
    EOF
    wake 'mathSymbols 0'

Above is a fun example using wake as a hybrid build/programming language.
This wake code downloads the Unicode symbol tables from the Internet using
curl and then grabs all the mathematical symbols from the table, formats
them, and returns the output.

As we've covered before, `job` is used to launch curl to download the table.
The `curl` function also makes use of `extract` with a regular expression
to split the filename out of the URL. `extract` returns a `List String`
containing each occurrence of `()` in the regular expression. We also use
the `simplify` function which transforms paths into canonical form. In this,
case `simplify` removes the leading `"./"`.

If you look in `UnicodeData.txt`, you will see that it uses lines with comma
separated values, one for each Unicode code point.  Thus `lines` simply
splits the `String` returned by `read` into each code point description.
For each line, we then split it into the fields and pass the result to
`helper`.

`helper` uses a pattern match to extract the first and third arguments from
the list. If the third argument is of class `"Sm"`, ie: symbols/math, then
wake converts the hexadecimal from the first column into the Unicode code
point for that value and returns a `String` containing the value.

`mapPartial` uses `Option String`s.  Options can either be `None`, which
means no value is available or `Some x` which is a value `x`.  `Option` is
a data type like any other in wake.  It is particularly useful in those
situations where one would use a null pointer in a language like C.  In this
case, we use `Option` to only return values from `helper` where the code
point is a math symbol. `mapPartial` then creates a list out of only those
values helper returned. Try this, for an example:

    wake 'mapPartial (_) (None, Some "xx", Some "yy", None, Nil)'

As a final note, wake files must be UTF-8 encoded.
This means that you can use Unicode symbols in strings, comments,
identifiers, and even operators.
In fact, all the operators we just printed are legal in wake.
