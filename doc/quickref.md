

## Wake Quickref

### Invoking wake

1. A database must be created: `wake --init .`

1. The commands available to wake are from
the wake library (share/wake/lib) and all files *.wake in
the wake.db directory and below.

1. wake is invoked normally, although arguments are generally quoted.
    ```
    wake 'runThing "a string"'
    ```

1. Other useful flags
    1. `wake -v <varible>` to get type information
    1. `wake -o <file>` to get dependency information for job which write file
    1. `wake -i <file>` to get dependency information for job which read file
    1. `wake -g` To get a list of all global symbols


### Syntax

- Variables and functions defined the same way.
```
def foo x =
    x + 2 

def bar = 
    foo 7
```

* Only one statement is allowed per function. Multiple defs are allowed.

```
def foo x =
    def two = 2
    def add2 y =
        y + two
    add2 x
``` 

* To make any definition visible in another file, use global

```
global def foo a b =
    a + b
```

* |, ., and →
    * the pipe character feeds an argument to a function, like in shells scripts.
    * `a 0 (b 1 2 (c 3 d))` becomes `d | c 3 | b 1 2 | a 0`
    * '.' is similar, but is used to calls look like OOP
    * `length string` would become `string.length`
    * wake fully supports unicode. A common use of this is in creating a Pair object
    * 'a → b' is the same as Pair a b
    * Be aware that there other supported unicode functions.

### functional programming constructs

Wake is generally functional.

* first class function/partials
    * All functions are first class object. Partials can be created by leaving off
    arguments to a function.
    
    ```
    def foo a b = 
      a + b
        
    def foo2 = foo 2
       
     def bar a =
      foo2 a
    ```

* pure functions
    * functions usually have no side effects and are reordered.
    
* statements
    * all commands return a value, and can be used interchangeably
    
    ```
    def x = if False 6 else 4
    ```

* pattern matching
    * There is a match statement
    
    ```
    def fib x = match x
      0 = 0
      1 = 1
      2 = 2
      x = fib(x-1) + fib(x-2)
        
    def collatz = match _
      1 = 4
      2 = 1
      4 = 2
      x if x % 2 == 0 = x / 2
      _ = 3 * x + 1
    ```

* anonymous functions (lambda). Both forms work.
    ```
    (\x x + 2)
    
    (_ + 2)
    ```
    
* common list functions (meanings are standard)

|       |     |     |     |     |
| ---   | --- | --- | --- | --- |
| empty |   head | tail | map | foldl |
|scanl | foldr | scanr | ++ (cocatenate lists) | reverse |
| flatten | len | exists | forall | filter| 
| zip| 

* printing output (for debugging)
    * `println thing`
    * `print thing`
    * to format an object for printing: `format thing`

### Data Types

####primitives
* String -> "A String" : a string interpreted as unicode
    * a "" string also allows string interpolation using {}
        * for example  `"{foo}{4 + 3}"`
        * also \ is interpreted in a double quoted string
    * 'a binary string' : a raw binary string
* List a -> 1,2,3,Nil (a list of items seperated by commas ended by nil)
* Integer -> 7
* Double -> 3.2
* Tuple -> see below
* JSON -> see below
* Job -> see below
* Path -> see below

* There are tree and vector type. Read the code if these sound useful.

#### Tuples

Tuple are how wake defines record types, much like a struct in c/c++. The name of
a tuple data type begins with a capital letter.

```bash

global tuple Bob =
    global First: Integer
    global Second: Double
    global Third: String

```

Defining a tuple creates several methods for each field:

```
set<tuple name><field name>
get<tuple name><field name>
edit<tuple name><field name>

setBobFirst a b
getBobFirst a
editBobFirst a b
```

#### Json

```bash
# The JSON data type
global data JValue =
  JString  String
  JInteger Integer
  JDouble  Double
  JBoolean Boolean
  JNull
  JObject  (List (Pair String JValue))
  JArray   (List JValue)
```

Wake has many useful function for extracting data from a json structure.


#### jobs

Jobs are how wake runs external programs. They are created by executing a Plan with 
a Runner. The Plan is a tuple describing how to run the program, and the Runner is 
a function which reads the Plan and runs the program. 

Jobs provide wake's central useful functionality. Wake can be viewed as a functional
programming language for running jobs.

```
global def planit =
  makePlan ('touch','foo',Nil) Nil

global def runit _ =
  planit
  | runJob
  | getJobStdout
  | format
  | println
```

#### sources and Path ojbects

Sources are the set of files in git.

A Path is either a) a file in git or b) a file produced from a build step

To get a path, use the source function. For example, all the header files in this directory:

```
source `.*\.h` here
```

### Wakisms

* database
    * wake uses an sqlite3 database to track dependencies, job information and 
    various other detail. This should be treated as an read-only resource.
* FUSE
    * wake uses a FUSE filesystem to track what files are actually opened when running
    a job. 
    * this can be disabled for systems that can't run FUSE
* no guarantee of ordering
    * There is no guarantee of ordering if two statements don't depend on each other. This is
    wake is able to parallelize programs.
* publish/subscribe
    * the publish and subscribe commands are used for global values. A published queue has
    global scope.
    ```
    publish xfoo = "bar", Nil
    publish xfoo = "baz", Nil
    
    global def readit _ =
        subscribe xfoo
    ```

* environment
    * the environment is in a published queue, and does not pick up the environment
    from the parent process. It can also be accessed from the global variable `environment`.
    