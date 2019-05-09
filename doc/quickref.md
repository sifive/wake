

## Wake Quickref

### Invoking wake
1. Before invoking a wake command, all wakefiles must be
checked in to git. 

1. A database must be created: `wake --init .`

1. The commands available to wake are from
the wake library (share/wake/lib) and all files *.wake in
the current directory and below.

1. wake is invoked normally, although arguments are generally quoted.
```bash
wake 'runThing "a string"'
```

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
    * the pipe character is like $ in haskell, but reversed.
    * `a b c` becomes `c | b | a`
    * '.' is similar, but is used to calls look like OOP
    * 'length string' would become 'string.length'
    * wake fully supports unicode. A common use of this is in creating a Pair object
    * 'a → b' is the same as Pair a b
    * Be aware that there other supported unicode functions.


### functional programming constructs

Wake is generally functional.

* first class function/partials
    * All functions are first class object. Partials can be created by leaving off
    arguments to a function.
    
    ```bash
    def foo a b = 
        a + b
        
    def foo2 = foo 2
       
     def bar a =
        foo2 a
    ```

* pure functions
    * functions usually have no side effect.
    
* statements
    * all commands return a value, and can be used interchangeably
    
    ```
    def x = if False 6 else 4
    ```

* pattern matching
    * There is a match statement
    
    ```bash
    def fib x = match x
        0 = 0
        1 = 1
        2 = 2
        x = fib(x-1) + fib(x-2) 
    ```


### Data Types

####primitives
* string -> "A String"
* array -> 1,2,3,Nil (a list of items seperated by commas ended by nil)
* integer -> 7
* double -> 3.2
* exception -> see below
* json -> see below
* job -> see below
* sources -> see below


##### Exceptions
##### Json
##### jobs
##### sources

### Wakisms

* paths
* dependencies
* database
* FUSE
* no guarantee of ordering
* publish/subscribe
* memoize