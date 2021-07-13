## Wake Tutorial

This tutorial will teach you the basics of Wake language. After completeing this tutorial, you will have typed and executed many basic wake functions.

### Invoking wake

1. Create a folder inside `/home` directory. `mkdir /home/<your_name>/wake_tutorial`.

2. `cd` into the directory. `cd /home/<your_name>/wake_tutorial`.

3. Create a wake database: `wake --init .`.After this you will see a `wake.db` file created inside the folder.

4. Create a file with `.wake` extension and copy the functions mentioned in this tutorial and run using `wake` command in the terminal as mentioned in Step 5.  

5. Run command for wake: `wake -vx '<function_name> <parameter_if_any>'`. Ex: `wake -vx 'double 2'`.
   
   `v` stands for verbosity and `x` stands for execute the expression.

6. To know about various wake options run `wake --help`. 

7. For more information on wake library click [here](https://sifive.github.io/wake/).

### wake `Hello World`

Copy the below function into a `.wake` file you created in step 4
```
def abc = "Hello World"
```
Then execute the following command 
```
wake -x 'abc'
```
This will call the function `abc` and print `Hello World`.

* You can also do this without a `.wake` file by doing it all in one command: `wake -x '"Hello World"'`

### Defining Functions
A function should be defined using the keyword `def`. Simple function definition takes the form
`def <function name> <parameter list> = <expression>`

We cannot specify explicitly the return type and type of the parameters. When the function body is of multi-line the return value is specified by the last line in the function.

Functions can be defined with the keyword `global` which makes the function accessible from other .wake files other than the one where it is defined.

We can arbitrarily deeply put `def` inside another `def`. The internal `def` is bound to the function itself and cannot be accesible outside the function.

Note: The only thing you can define are functions using `def` keyword. In wake we do everything with functions.

#### Few operators that work on Integer data type
* `+` Addition Operator
* `-` Subtraction Operator
* `*` Multiplication Operator
* `/` Division Operator
* `^` Exponent Operator
* `%` Modulus Operator
* `>>` Right shift Operator
* `<<` Left shift Operator

#### Few operators that work on Double data type
* `+.` Addition Operator
* `-.` Subtraction Operator
* `*.` Multiplication Operator
* `/.` Division Operator
* `^.` Exponent Operator

* Below function  double takes an argment x and gives the output by multiplying the x by 2.
```
def double x = 2*x
```

* Below function triple takes an argument x and gives the output by multiplying the x by 3.
```
def triple x = 3*x
```

* Function inc increments the values by 1 and returns the value.
```
def inc x = x+1
```

* You can use parenthesis to nest function calls. Function times4 takes an argument x and applies the function double twice on x.
```
def times4 x = double (double x)
```

* Adds 2 integer values and gives out the result.
```
def sumadd x y = x+y 
```

* power 2 3 = 8
```
def power x n = x^n
```

#### lambda operator (\)
The lambda operator `\` is an alternative way to define the parameters of a function. 

* Function aveI(averageOfInteger) takes 2 arguments x and y. It adds the two values and divides the result by 2.Previously we saw that we could define this as `def aveI x y = (x+y)/2`.The function below shows the lambda operator way of defining the parameters. The two definitions are logically the same.
```
def aveI = \x\y (x+y)/2
```

* Function aveR(averageOfReal) takes 2 arguments of type double and divides the result by 2.
```
def aveR = \x\y (x +. y) /. 2.0
```

* Function aveM(Modulus) adds 2 values and provides the remainder.
```
def aveM = \x\y (x+y) % 2
```

### Wake Built-In Functions
Few of the commonly used built-in functions in this tutorial is given below
#### Operation on String
* `explode` breaks a string into a list of string character.
* `strlen` returns the length of the given string.
* `cat` concatenates the elements in list of string to a string. 

* strlen returns the length of the given string.
```
wake -x 'strlen "abcde"'
```

* Function addb takes an argument of type string and concatenates it with b and gives the output. `{ }` is the string interpolator operator.
```
def addb s = "{s}b"
```

* Function duplicate concantenates or copies a given string twice.
```
def duplicate s  ="{s}{s}"
```

#### Operation on List
* `take` takes first `n` elements in the list.
* `drop` drops first `n` elements in the list.
* `reverse` reverses the elements in the list.
* `head` returns the first element in the list.
* `tail` returns the list by removing the `head`.
* `prepend` places the given data at the begining(head) of the given list.
* `append` appends the given data at the end of the given list.
* `++` concatenates any number of list into a single list.
* `len` returns the length of the given list.
* `flatten` converts the list of list into a list.
* `mapFlat` applies a function to each element in the list and builds a new list from the resulting elements.
* `filter`  applies a function and builds a new list from it.
* `zip` takes 2 lists and converts them to list of Pairs.

#### Miscellaneous Built-In Functions 
* integerToUnicode is a `wake built-in function` which takes the ASCII input and provides its equivalent character.
```
wake -x 'integerToUnicode 99'
```

* unicodeToInteger is a `wake built-in function` which gives the ASCII value of the given character.
```
wake -x 'unicodeToInteger "a"'
```

### List
A List is a sequence of items of same type. Empty list returns `Nil`. `head` returns the first element in the list. `tail` returns the rest of the list without the `head`.

* Example creates a list of given elements.
```
def listcreate xx = xx,xx,xx,Nil
```
 
* Example shows how to prepend the list of integers with the given value.
```
def d = 1,2,3,Nil
def listprepend data1 = prepend data1 d
```

* `Note: Parenthesis in wake is used to enforce a specific order of evaluation(precedence of operators) in expressions.`
* concantenates 2 lists using ++ operator
```
wake -x '(1,2,3,Nil) ++ (4,5,6,Nil)'
```
### Option
`Option` is a data type in wake. It will either return `Some x` which is a value `x` or `None` which means nothing.

### Pair
* `Pair` carry 2 fields which is `First` and `Second`. The datatype of the `First` and `Second` can be anything and datatype is determined while we are creating a `Pair`

* `getPairFirst` and `getPairSecond` are the 2 helper functions that are used to access the first and second data within a Pair. It has other helper functions such as `setPairFirst` , `setPairSecond` , `editPairFirst` and `editPairSecond`.

### Type Inference
`wake` does not allow you to specify the types of parameters or return values. All types are inferred.

* Here:
** the parameter `c` has to be string since function `strlen` has been applied to it.
** `b` has to be integer as it is being added to the output of `strlen c`.
** `a` is a list of integers because it prepends the output of `(b + strlen c)`.

```
def madeup a b c =  (b + strlen c), a 
```

### Parenthesis vs Pipe
* `|`is the pipeFn which takes the argument from the left hand side of the `|` and provides it to the right hand side function. This makes the function to be more readable. `(a(b(c(d))))` is represented as `d | c | b | a`. In both the cases d is evaluated first then followed by c , b and a.
```
def reversestring s = cat (reverse (explode (s)))
```

The same can be expressed with the pipe function as below
```
def reversestring s = s | explode | reverse | cat
```
#### `if`..`then`..`else`..
It is similar to the traditional `if` construct we use in C language. `if(condition == true)` `then (do this)` `else (do something else)`

* Below function takes a string and returns the middle character of the string.
```
def middle fullstring =
  def slength = if (strlen fullstring % 2 == 0) then (strlen fullstring / 2) else  ((strlen fullstring / 2) + 1)
  def outputstr = explode fullstring | take slength | reverse | head 
  outputstr
```

* Below function takes a string and returns the string removing the first and last character.
```
def dtrunc fullstring =
  def slength = strlen fullstring
  def outputstr = explode fullstring | take (slength-1) | tail | cat
  outputstr
```

* Function incfirst takes a string and a character and replaces the first character of the string with the given character.
```
def incfirst fullstring chrstr = explode fullstring | tail | prepend chrstr | cat
```

* Function reorder takes a string , split the string and then reverses the splitted string and then concantenates both (Ex: hangover -> overhang).
```
def reorder fullstring = 
  def Pair firststr laststr = 
    explode fullstring
    | splitAt (strlen fullstring / 2) 
  cat (laststr ++ firststr)                         
```

* Function dubmid takes a string, duplicates the middle character and gives out the string (Ex: hapy -> happy))
```
def dubmid fullstring =
  def Pair firststr laststr = 
    explode fullstring
    | splitAt (strlen fullstring / 2)
  def dupli = laststr | take 1
  cat (firststr ++ dupli ++ laststr)
```

* Takes a string and then removes the last character in a string
```
wake -x '"been"| explode | take 3| cat'
```

* Replaces the first charcter with the character "c"
```
wake -x '"bad"| explode | drop 1| prepend "c" | cat' 
```

* Try out these examples which act on strings
```
wake -x '"south" | explode | take 1| cat'
wake -x '"north"| explode | tail | take 1 | cat'
wake -x '"east" | explode| reverse | take 1 |cat'
wake -x '"west" | explode | reverse | tail |take 1| cat'
```

* Below are the set of examples which shows how to extract specific characters from a string
```
def first s = s | explode | take 1| cat
def second s = s | explode | tail | take 1 | cat
def third s = s | explode| tail | tail | take 1 |cat
def fourth s =  s | explode | tail | tail | tail | take 1| cat
def last s = s| explode | reverse |take 1 | cat
```

* Try out these different set of examples and capture the result
```
def roll s = "{fourth s}{first s}{second s}{third s}"
def exch s = "{second s}{first s}{third s}{fourth s}"
```

* Try out these examples and record the results
```
def what fullstring = fullstring| roll | roll | roll| exch |  roll
def fb fullstring = fullstring | roll |roll | roll
def fc fullstring = fullstring | roll | exch |fb
def fd fullstring = fullstring | exch | fc | exch
```

* This examples convert the string "seat" -> "eats"
```
wake -x 'cat((tail (explode "seat")) ++(take 1(explode "seat")))'
```

### Tuple
A tuple is collection of items of different types.
* Below example shows the declaration of a tuple. 
```
tuple Collection =
  global IntValue : Integer
  global DoubleValue : Double
  global StringValue : String
```

`get`,`set` and `edit` are the three inbuilt methods that can be used with any Tuple.

`set<tuple name><field Name>` 
`set` operation sets(writes) value to the tuple field
`get<tuple name><field Name>` 
`get` operation gets(reads) value of the tuple field
`edit<tuple name><field Name>` 
`edit` operation moidifies value to the tuple field

```
def collection = Collection 1 0.3 "example"
def myCollectionStringValue = collection | getCollectionStringValue
def myCollectionDoubleValue = collection | setCollectionDoubleValue 0.6
def myCollectionIntValue    = collection | editCollectionIntValue (\x x+1)
```

### Higher Order function
A function which does one of the following is a higher order function
1. Takes one or more functions as argument
2. Returns a function as its result

* Funtion tea shows the way to pass a function as a argument 
```
def tea f = f 4
```
* `Map is the best example for a Higher Order Function` 

### Map
Map is a higher order function which takes a function and applies the same to every element in the list.

* Below example shows how to declare a list of strings and apply map function to it. Map is a predefined function that maps to the every element in the list
```
def words = "ache", "vile", "amid", "evil", "ogre" , Nil
def mapping words = words | map roll
def mapping2 words = map exch words
```

* Try out these examples and record the result
```
def op1 = triple (inc 1)
def op2 = inc 1 | triple
def op3 = double (inc (triple 1))
def op4 = triple (inc (double 1))
def op5 = tea double
def op6 = tea inc
def op7 = tea inc | double 
def op8 = map double (1,2,3,Nil)
def op9 = map double (1,2,3,Nil) | map inc
```

### Pattern Matching
The first thing that comes to mind is string matching. It is similar to the Switch statement where it will check for the matching case and outputs appropriate results.
```
def past = match _
  "run" = "ran"
  n = cat(n,"ed",Nil)
```
* `_` here means that it will do a match on any input that has been passed to  the function as an argument. So that we dont need to define any parameter and we can do a match by just placing `_`

* `If the argument passed to this function is `run` then the output will be `ran`. If the argument passed is other than `ran`  then it will cat `ed` with the input passed and output the result`

* Compares the given string and outputs the appropriate string
```
def past2 = match _
  "run" = "ran"
  "swim" = "swam"
  n = "{n}ed"
```

* Compares the given string and outputs the appropriate string
```
def franglais = match _
  "house" = "maison"
  "dog" = "chien"
  "beware" = "regarde"
  "at" = "dans"
  "the"= "le"
   n = n
```

### Recursive function
Using recursive functions we can achieve the sort of results which would require loops in a traditional language. Recursive functions tend to be much shorter and clearer. A recursive function is one which calls itself either directly or indirectly. Traditionally, the first recursive function considered is factorial.
* Using if statement as well as using match function
```
def facto1 n = if n == 0 then 1 else  n * facto (n-1)
```

```
def facto = match _
  0 = 1
  n = n * facto (n-1) 
```

* Ex: `facto 5 ` 
```
facto = 5 * facto (5-1)
      = 5 * 4 * facto (4-1)
      = 5 * 4 * 3 * facto (3-1)
      = 5 * 4 * 3 * 2 * facto (2-1)
      = 5 * 4 * 3 * 2 * 1 * facto (1-1)
      = 5 * 4 * 3 * 2 * 1 * 1
      = 120
```

#### Recursion on Integers
* Try out these recursive functions and record the result
```
def t1 = match _
  0 = 0
  n = 2 + t(n-1)

def d1 = match _
  0 = "de"
  n = cat ("do",d1(n-1),"da",Nil)

def h n = match n
  0 = 1
  n = h(n-1) + h(n-1) 

def j = match _
  0 = Nil
  n = (n % 2),j(n / 2)

def m a b = match a b
  _ 0 = 0
  a b = a + (m a (b - 1))

def f = match _
  0 = 0
  n = 1 - f (n-1)

def g = match _
  0 = 0
  n = g (n-1) + 2 * (n-1)

def l = match _
  0 = 0
  n = n % 10 + l (n / 10) 
```

* sumto 4 = 4+3+2+1+0
```
def sumto n = match n
  0 = 0
  n = n + sumto (n-1)
```
			
* listfrom 4 = (4,3,2,1,Nil)
```
def listfrom n = match n
  0 = Nil
  n = n , listfrom (n - 1)
```

* strcopy "ab" 4 = "abababab"
```
def strcopy str n = match n 
  0 = ""
  n = "{str}{strcopy str (n-1)}"
```

* listcopy 7 4 = (7,7,7,7,Nil)
```
def listcopy l n = match n
  0 = Nil
  n = l ,listcopy l (n-1)
```

* listto 4 = (1,2,3,4,Nil)
```
def listto n = listfrom n | reverse
```

* listodd 7 = (7,5,3,1,Nil)
```
def listodd n = if(n<0) then Nil else if(n%2 ==1) then n,listodd (n-2) else Nil
```

* sumenes 8 = 8+6+4+2+0 = 20
```
def sumevens n = if (n==0) then 0 else if (n%2 ==0) then n + sumevens (n-2) else n
```

```
def natstring = "succ"
def nat n = match n 
  0 = "zero"
  n = "{natstring}({nat(n-1)})"
```
#### Recursion On Lists
* Adds all the elements in a list and provids the sum
```
def sum n = match n 
  Nil = 0
  h,t = h + sum t
```
* Ex: `sum (1,2,3,Nil)`

```
sum = 1 + sum (2,3,Nil)
    = 1 + 2 + sum (3,Nil)
    = 1 + 2 + 3 + sum (Nil)
    = 1 + 2 + 3 + 0
    = 6
```

* Doubles every element in the list
```
def doublelist n = match n
    Nil = Nil
    h,t = 2*h, doublelist t
```
* The other way of multiplying every element in a list
```
def doublelist2 = map (2*_)
```

* Duplicates every element in a list
```
def dupelist n = match n
    Nil = Nil
    h,t = (h,h,Nil)++ dupelist t
```

* Below are the examples which does similar functionality as above
```
def dupelist2 = foldr (\x \y x,x,y) Nil

def dupelist1 n = mapFlat (\x (x,x,Nil) ) n

def dupelist3 = mapFlat (\x (x,x,Nil) ) 
```
#### More Examples
* Returns the length of a list
```
def length n = len n
```

* Multiples every element in a list and gives the result. `_` here means that it will multiply the data whatever being passed to the function `foldr`.
```
def prodlist n = foldr (_ * _)1 n
```

* vallist ("1","2","3","4",Nil) = (1,2,3,4,Nil)
```
def vallist n =n | map (\x x | int | getOrElse 0)
```

* spacelist ("1","2","3",Nil) = ("1","","2","","3","",Nil) 
```
def spacelist n = n | mapFlat (\x(x,"",Nil))
```

* flatten converts list of lists into a list
```
wake -x 'flatten ((1,2,3,Nil),(4,5,6,Nil),Nil)'
```

* Returns the last element in the list
```
def last1 n = n | reverse | head
```

* Finds a given element in a given list or else returns None 
```
wake -x '(1,2,3,1,Nil)| find (\x x==2)'
```

* Multiply every element in list by 4
```
wake -x '(1,2,3,Nil)|map (\x 4*x)'
```

* Counts the no of 1's in a given list
```
def count1 n = n | filter (\x x == 1) | len  
```

* Creates a list of Pairs
```
def zipop n m =  zip n m
```

* Concantenates 2 lists by taking elements from each lists alternatively
```
def altern n m = zip n m | mapFlat (\x (x.getPairFirst, x.getPairSecond,Nil) )
```

* Functions same as above using match function
```
def altern1 m n = match m n
  _      Nil    = Nil
  Nil    _      = Nil
  (h,t) (h1,t1) = h,h1, altern1 t t1    
```

* Returns the head from list (list Integers)
```
def hdify n = n | map (\x x | head | getOrElse 0)
```

* Returns the tail from list (list Integers)
```
def tlify n = n | map (\x x | tail)
```

* Outputs the extra element in the list by comapring the length of the given 2 lists
```
def diff n m = match n m 
  Nil x = x
  x Nil = x
  (_,t)(_,t1) = diff t t1
```

* Concantenates the given string with "ed"
```
def past1 n = "{n}ed"
```

* Multiplies every element in a list by 2
```
def multiply n = n | map (\x 2*x)
```

* Returns the element in the list of the given index n
```
def index n l = l | drop n | head
```

* Takes first n elements from the given list
```
def takeN n m = m | take n
```

* Drops first n elements from the given list
```
def dropN n m = m | drop n
```

* Creates a list with n as its first element and m as the last element 
```
def upto n m = m-n+1 | seq | map (\x x+n)
```

* Adds a space after a given string     
```
def addspace s = "{s} "
```

* Convert the String separated by spaces  into list of string 
```
def lex s = s | tokenize ` `
```

* Doubles every element in the list
```
def doublist n = match n 
  Nil = Nil
  h,t = 2*h , doublist t
```

* Increment the given list elements by 1
```
def inclist n = match n
  Nil = Nil
  h,t = (h+1), inclist t
```

* Doubles every element in the list using map function
```
def doublistmap n = n | map (\n 2*n)
```

* Increment every element in the list using map function
```
def inclistmap n = n | map (\n n+1) 
```

* Returns a list with the leading spaces removed
```
def dropspace n = match n
  Nil = Nil
  h,t = if (h ==* " ") then dropspace t else (h,t)
```
* Returns just the leading spaces
```
def takespace n = match n 
  Nil = Nil
  h,t = if (h ==* " ") then h,takespace t else Nil
```

* Concantenates every list of string with "io"
```
def map1 n = n | map (\s "{s}io")
```

* Creates a list (list Integers) from a given list of integers
```
def map2 n = n | map (\x x,Nil)
```
* `getOrElse` returns the `Option`s value if it exists or else it will return a value(`0` in this case) passed along with it if the `Option` is empty

* Outputs the first element from the list (list Integers)
```
def map3 n = n | map (\x x | head | getOrElse 0)
```

* Creates a list of strings by talking the last character of a string
```
def map4 n = n | map (\x last x)
```

* Multiplies every element of the list by 3
```
def ftrl n = n | map (\x 3*x)
```

* Takes the first character from the list of strings and creates a new list
```
def fhel n = n | map (\x first x)
```

* removes first 2 characters from the list of strings and creates a new list
```
def tailstring n = n | explode | drop 1 |cat
def fttl n = n | map (\x tailstring x)
```

* Prepend and Appends every element in a list of string by "s" "m"
```
def fsml n = n | map (\x "s{x}m")
```

* Try out these and record the output
```
def queuePut x r = foldl (\a\b b,a)x r
def queuePut1 r = foldl (\a\b b,a) Nil r
```

* Try out these examples
```
def listexample1 l e = l ++ (e,Nil)
def listexample2 l = l | head | getOrElse 0 
def listexample3 l = l | tail
```
`wake -x '("abc","def",Nil) | map(explode)|map ( head)'`

* Find the sum of elements in a list using foldr function. Here n refers to the deafult value which the function returns if the list l is empty(Nil).
```
def suma l n= l | foldr(_+_)n
```

* Finds the mean in the given list of integers
```
def mean l = l | foldr(_+_)0 | (\x x/ len l)
```

* Finds the median in the given list of integers
```
def median l = 
  def length = if (len l %2 ==0) then ((len l) /2)-1 else ((len l)/2)
  l | drop length | head
```

#### Mutually Recursive Functions: Example
```
def foo n = match n 
  0 = "toff"
  n = bar(n-1)
def bar n = match n
  0 = "beerut"
  n = foo(n-1)
```

### Creating your own Data types
* Example shows how to create a data type of our own
```
global data Direction =
  North
  South
  East
  West
```

* Example shows the data type Money which can be in the form of Cash or the Cheque.
```
global data Money =
  Cash Integer
  Cheque (Pair String Double)
```
```
def amount  = Cash 50
def chequeamount = Cheque (Pair "HDFC" 350.00)
```

### Queues
Wake doesnt have a built-in queue. Examples shows how to create a queue and few functions that can act on queue. 
The front of the queue is at the right, nearest the bus stop, items are added to the left. Consider the bus queue shown, boris is at the front of the queue, ivan is last. 
`"ivan" $$ "tanya" $$ "boris" $$ P`
* Created a data type Queue which is nothing but a FIFO.
```
data Queue a = 
  P
  a $$ Queue a
```

The operations on a queue are front and remove. front returns the element at the front of the queue (without altering the queue), remove returns the rest of the queue with the first element removed. Note that both of these are strictly functions not procedures, remove does not change an existing queue it simply returns part of the queue, the original is still intact after the function call.

* front returns the first element in a queue
```
def front = match _
  x$$P = x$$P
  P = P
  x$$q = front q
```

* remove returns the rest of the queue leaving the first element in the queue
```
def remove = match _
  x$$P = P
  P = P
  x$$q = x $$ (remove q)
```

* Converts list into a queue
```
def l2q = match _
  Nil = P
  h,t = h $$ (l2q t)
```

* Converts queue to a list
```
def q2l = match _
  P = Nil
  x$$q = x,(q2l q)
```

* Reverses the order of the queue
```
def doomsday = match _
  P = P
  q = front q $$ doomsday (remove q)
```

## Tree
Tree is also one of the data structure that represent hierarchical data. The topmost node is known as root node. Every node contains some data.
Tree is already implemented in wake by default.
* Creates a Tree from a list
```
wake -x 'seq 5 |map str| listToTree (_ <=>* _)'
```

```
wake -x 'listToTree (_ <=> _) (seq 20)'`
```
