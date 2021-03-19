## Wake Tutorial

### Invoking wake

1. Create a wake database: `wake --init .`

2. Run command for wake: `wake -vx '<function_name> <parameter_if_any>'`

3. To know about various wake options run `wake --help`. `v` stands for verbosity and `x` stands for execute the expression.

4. Create a file with `.wake` extension and copy the functions mentioned in this tutorial and run them using `wake` command in the terminal.  

5. For more information on wake library click [here](https://sifive.github.io/wake/)

### wake `Hello World`
* If you want to display a string you can do that using the below function by writing the string to be displayed inside "" and call the function abc . Ex: `wake -x 'abc'`. This will call the function `abc` and prints `Hello World`. 
```
def abc = "Hello World"
```

* The other way to do this is `wake -x '"Hello World"'`

### Defining Functions
A function may be defined using the keyword `def`. Simple function definition takes the form
`def <function name> <parameter list> = <expression>`

* Below function  double takes an argment x and gives the output by multiplying the x by 2
```
def double x = 2*x
```

* Below function triple takes an argument x and gives the output by multiplying the x by 3
```
def triple x = 3*x
```

* Function inc increments the values by 1 and returns the value
```
def inc x = x+1
```

* Function adda takes an argument of type string and concatenates it with b and gives the output. `{ }` is the string interpolation operator
```
def adda s = "{s}b"
```

* Function times4 takes an argument x and applies the function double twice on x
```
def times4 x = double (double x)
```

* Function aveI(averageOfInteger) takes 2 arguments x and y and adds two values and divide the result by 2. This function shows the other was of defining the parameters using lambda operator. The traditional way of defining the parameter is `def aveI x y = (x+y)/2`. Either of them are logically same.
```
def aveI = \x\y (x+y)/2
```

* Function aveR(averageOfReal) takes 2 arguments of type double and divides the result by 2
```
def aveR = \x\y (x +. y) /. 2.0
```

* Function aveM(Modulus) adds 2 values and provides the remainder 
```
def aveM = \x\y (x+y) % 2
```

* Function duplicate concantenates or copies a given string twice
```
def duplicate s  ="{s}{s}"
```

* integerToUnicode is a `wake built-in function` which takes the ASCII input and provides its equivalent character
```
wake -x 'integerToUnicode 99'
```

* unicodeToInteger is a `wake built-in function` which gives the ASCII value of the given character.
```
wake -x 'unicodeToInteger "a"'
```

* strlen is a `wake built-in function` which returns the length of the given string.
```
wake -x 'strlen "abcde"'
```

* `|`is the pipeFn which takes the argument from the left hand side of the `|` and provides it to the right hand side function. This makes the function to be more readable. `(a(b(c(d))))` is represented as `d | c | b | a`. In both the cases d is evaluated first then followed by c , b and a.

### Wake Built-In Functions
Few of the commonly used built-in functions in this tutorial is given below
* `explode` breaks the string into a list of string character.
* `take` takes first `n` elements in the list.
* `drop` drops first `n` elements in the list.
* `reverse` reverses the elements in the list.
* `head` returns the first element in the list.
* `tail` returns the list by removing the `head`.
* `cat` concatenates the elements in list of string to a string. 
* `prepend` places the given data at the begining(head) of the given list
* `append` appends the given data at the end of the given list  

* Below function takes a string and returns the middle character of the string.
```
def middle fullstring =
  def len = if (strlen fullstring % 2 == 0) then (strlen fullstring / 2) else  ((strlen fullstring / 2) + 1)
  def outputstr = explode fullstring | take len | reverse | head 
  outputstr
```

* Below function takes a string and returns the string removing the first and last character.
```
def dtrunc fullstring =
  def len = strlen fullstring
  def outputstr = explode fullstring | take (len-1) | tail | cat
  outputstr
```

* Function incfirst takes a string and a character string and replaces the first character of the string with the given character string
```
def incfirst fullstring chrstr = explode fullstring | tail | prepend chrstr | cat
```

* Function reorder takes a string , split the string and then reverses the splitted string and then concantenates both (Ex: hangover -> overhang)
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
### Tuple
A tuple is collection of items of different types. `get`,`set` and `edit` are the three inbuilt methods that can be used with the tuples.
* Below example shows the declaration of a tuple. 
```
tuple Collection =
  global IntValue : Integer
  global DoubleValue : Double
  global StringValue : String
```

`set<tuple name><field Name>` 
`set` operation sets(writes) value to the tuple field
`get<tuple name><field Name>` 
`get` operation gets(reads) value to the tuple field
`edit<tuple name><field Name>` 
`edit` operation moidfies value to the tuple field

### List
A List is a sequence of items of same type. Empty list returns `Nil`. `head` returns the first element in the list. `tail` returns the rest of the list without the `head`. 
* Example shows how to prepend the list of integers with the given value
```
def d = 1,2,3,Nil
def listprepend data1 = prepend data1 d
```

* Example creates a list of given elements
```
def listcreate xx = xx,xx,xx,Nil
```
* Try out these examples
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

* Below example shows how to declare a list of strings and apply map function to it. Map is a predefined function that maps to the every element in the list
```
def words = "ache", "vile", "amid", "evil", "ogre" , Nil
def mapping words = words | map roll
def mapping2 words = map exch words
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

### Type Inference
* Here the paramater c has to be string since function strlen as been applied to it, and b has to be integer as it is being added and a also should be an integer because it is list of integers 
```
def madeup a b c =  (b + strlen c), a 
```

* Below example multiplies a given number by 6. The other way to implement this is `def times6 x = double x | triple`
```
def times6 x = triple (double x)
```

* Funtion tea shows the way to pass a function as a argument 
```
def tea f = f 4
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

* Adds 2 integer values and gives out the result
```
def sumadd x y = x+y 
```
### Pattern Matching
```
def past = match _
  "run" = "ran"
  n = cat(n,"ed",Nil)
```

### Recursive function
Using recursive functions we can achieve the sort of results which would require loops in a traditional language. Recursive functions tend to be much shorter and clearer. A recursive function is one which calls itself either directly or indirectly. Traditionally, the first recursive function considered is factorial.
* Using if statement as well as using match function
`def facto1 n = if n == 0 then 1 else  n * facto (n-1)`

* facto 5 = 5*4*3*2*1 = 120
```
def facto = match _
  0 = 1
  n = n * facto (n-1) 
```

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
`def listto n = listfrom n | reverse`

* listodd 7 = (7,5,3,1,Nil)
`def listodd n = if(n<0) then Nil else if(n%2 ==1) then n,listodd (n-2) else n`

* sumenes 8 = 8+6+4+2+0 = 20
`def sumevens n = if (n==0) then 0 else if (n%2 ==0) then n + sumevens (n-2) else n`

* power 2 3 = 8
`def power x n = x^n`

```
def natstring = "succ"
def nat n = match n 
  0 = "zero"
  n = "{natstring}({nat(n-1)})"
```

* Adds all the elements in a list and provids the sum
```
def sum n = match n 
  Nil = 0
  h,t = h + sum t
```

* concantenates 2 lists using ++ operator
`wake -x '(1,2,3,Nil) ++ (4,5,6,Nil)'`

* Doubles every element in the list
```
def doublelist n = match n
    Nil = Nil
    h,t = 2*h, doublelist t
```
* The other way of multiplying every element in a list
`def doublelist2 = map (2*_)`

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

* Returns the length of a list
`def length n = len n`

* Multiples every element in a list and gives the result
`def prodlist n = foldr (_ * _)1 n`

* vallist ("1","2","3","4",Nil) = (1,2,3,4,Nil)
`def vallist n =n | map (\x x | int | getOrElse 0)`

* spacelist ("1","2","3",Nil) = ("1","","2","","3","",Nil) 
`def spacelist n = n | mapFlat (\x(x,"",Nil))`

* flatten converts list if lists into a list
`wake -x 'flatten ((1,2,3,Nil),(4,5,6,Nil),Nil)'`

* Returns the last element in the list
`def last1 n = n | reverse | head`

* Finds a given element in a given list or else returns None 
`wake -x '(1,2,3,1,Nil)| find (\x x==2)'`

* Multiply every element in list by 4
`wake -x '(1,2,3,Nil)|map (\x 4*x)'`

* Counts the no of 1's in a given list
`def count1 n = n | filter (\x x == 1) | len  `

* Creates a list of Pairs
`def zipop n m =  zip n m`

* Concantenates 2 lists by taking elements from each lists alternatively
`def altern n m = zip n m | mapFlat (\x (x.getPairFirst, x.getPairSecond,Nil) )`

* Functions same as above using match function
```
def altern1 m n = match m n
  _      Nil    = Nil
  Nil    _      = Nil
  (h,t) (h1,t1) = h,h1, altern1 t t1    
```

* Returns the head from list (list Integers)
`def hdify n = n | map (\x x | head | getOrElse 0)`

* Returns the tail from list (list Integers)
`def tlify n = n | map (\x x | tail)`

* Outputs the extra element in the list by comapring the length of the given 2 lists
```
def diff n m = match n m 
  Nil x = x
  x Nil = x
  (_,t)(_,t1) = diff t t1
```

* Concantenates the given string with "ed"
`def past1 n = "{n}ed"`

* Compares the given string and outputs the appropriate string(Pattern Matching)
```
def past2 = match _
  "run" = "ran"
  "swim" = "swam"
  n = "{n}ed"
```

* Multiplies every element in a list by 2
`def multiply n = n | map (\x 2*x)`

* Returns the element in the list of the given index n
`def index n l = l | drop n | head`

* Takes first n elements from the given list
`def takeN n m = m | take n`

* Drops first n elements from the given list
`def dropN n m = m | drop n`

* Creates a list with n as its first element and m as the last element 
`def upto n m = m-n+1 | seq | map (\x x+n)`

* Adds a space after a given string     
`def addspace s = "{s} "`

* Convert the String separated by spaces  into list of string 
`def lex s = s | tokenize ` ``

* Compares the given string and outputs the appropriate string(Pattern Matching)
```
def franglais = match _
  "house" = "maison"
  "dog" = "chien"
  "beware" = "regarde"
  "at" = "dans"
  "the"= "le"
   n = n
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
`def doublistmap n = n | map (\n 2*n)`

* Increment every element in the list using map function
`def inclistmap n = n | map (\n n+1) `

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
### Map
map is a higher order function which takes a function and applies the same to every element in the list.
* Concantenates every list of string with "io"
`def map1 n = n | map (\s "{s}io")`

* Creates a list (list Integers) from a given list of integers
`def map2 n = n | map (\x x,Nil)`

* Outputs the first element from the list (list Integers)
`def map3 n = n | map (\x x | head | getOrElse 0)`

* Creates a list of strings by talking the last character of a string
`def map4 n = n | map (\x last x)`

* Multiplies every element of the list by 3
`def ftrl n = n | map (\x 3*x)`

* Takes the first character from the list of strings and creates a new list
`def fhel n = n | map (\x first x)`

* removes first 2 characters from the list of strings and creates a new list
```
def tailstring n = n | explode | drop 1 |cat
def fttl n = n | map (\x tailstring x)
```

* Prepend and Appends every element in a list of string by "s" "m"
`def fsml n = n | map (\x "s{x}m")`

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

* Find the sum of elements in a list using foldr function. Here n refers to the deafult value which the function retursn if the list l is empty(Nil).
`def suma l n= l | foldr(_+_)n`

* Nested Function: Example
```
def foo n = match n 
  0 = "toff"
  n = bar(n-1)
def bar n = match n
  0 = "beerut"
  n = foo(n-1)
```

* Finds the mean in the given list of integers
`def mean l = l | foldr(_+_)0 | (\x x/ len l)`

* Finds the median in the given list of integers
```
def median l = 
  def length = if (len l %2 ==0) then ((len l) /2)-1 else ((len l)/2)
  l | drop length | head
```

### Queues
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
Tree is already implemented in wake by default.
* Creates a Tree from a list
`wake -x 'seq 5 |map str| listToTree (_ <=>* _)'`
`wake -x 'listToTree (_ <=> _) (seq 20)'`

