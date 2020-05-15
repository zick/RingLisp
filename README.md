# Ring Lisp
Lisp implementation which allocates cons cells on a **ring buffer**.

## How to use
```
% g++ --std=c++17 -o ringlisp ringlisp.cc
% ./ringlisp
> (car '(a b c))
a
> (cdr '(a b c))
(b c)
> (cons 1 (cons 2 (cons 3 ())))
(1 2 3)
> (defun fact (n) (if (eq n 0) 1 (* n (fact (- n 1)))))
fact
> (fact 10)
3628800
> (defun gen (n) (lambda (m) (setq n (+ n m))))
gen
> (setq x (gen 100))
<expr>
> (x 10)
110
> (x 90)
200
> (x 300)
500
> (defun sum (n) (if (eq n 0) 0 (+ (sum (- n 1)) n)))
sum
> (sum 10)
55
> (sum 100)
... generation: 1
<stale value: 7f8aaed02780>
```

## Why ring buffer? What happens?
Even though Ring Lisp doesn't have garbage collector,
it can (virtually) create an infinite number of cons cells.
Because cons cells are allocated on a ring buffer,
old cons cells are reclaimed (reused)
**even if they are still alive**.

If you use a stale pointer to a reclaimed cons cell,
Ring Lisp returns an error: *stale value*.
See this example:
```
> (defun sum (n) (if (eq n 0) 0 (+ (sum (- n 1)) n)))
sum
> (sum 100)
... generation: 1
<stale value: 7f8aaed02780>
```
The value of `(sum 100)` is a stale value because alive cons cells are
reclaimed during recursive calls
(note that environments are represented by association lists).
The messsage `... generation: 1` indicates that
the ring buffer is full and
old cons cells will be reclaimed.
To avoid this kind of errors,
Ring Lisp requires you to write code not depending on old cons cells.

## How to write code not depending on old cons cells
Let's investigate the code which doesn't work:
```lisp
(defun sum (n)
  (if (eq n 0)
      0
      (+ (sum (- n 1))
         n)))
```
This function uses `n` after a recursive call.
Function calls create cons cells for creating local environments
and temporary data.
So the recursion can destroy the binding of `n`
(represented by an association list).

To avoid such a issue, we should write tail recursive functions:
```lisp
(defun sum (n acc)
  (if (eq n 0)
      acc
      (sum (- n 1) (+ n acc))))
```
Unforunately Ring Lisp still can return a stale value
because the function `sum` itself can be destroyed.
In Ring Lisp, functions/closures are represented by lists.
So they can be destroyed due to recursive calls.

The `sum` depends on local variables `n` and `acc`,
builtin function `eq`, `-`, and `+`, and
a global function `sum`.
The local variables aren't destroyed before the recursive call.
The builtin functions aren't destroyed
because they are out of the ring buffer.
So only `sum` has a problem in this code.

In general, user-defined functions can be destroyed.
We can't avoid it.
So we need to **copy** functions before they are destroyed.
Ring Lisp doesn't provide a way to copy normal functions
defined by `defun` or `lambda` special forms
because they have references to outer environments
(we can't copy environments).
Instead, Ring Lisp provides *copiable functions*
which are defined as normal lists.

Ring Lisp treats lists whose first elements are
`lambda` as functions.
```
> (setq f '(lambda (x) (cons x x)))
(lambda (x) (cons x x))
> (f 1)
(1 . 1)
> (setq g (list 'lambda (list 'y) (list 'cons 'y nil)))
(lambda (y) (cons y nil))
> (g 2)
(2)
```
They are just lists.
This `lambda` is not a special form.
It's just a symbol.
They don't have references to outer environments
unlike closures defined by `defun` or `lambda` special forms.
Some people might feel uncomfortable about it.
If you feel so, please wrap it around `eval` in your heart like
`(eval '((lambda (x) (cons x x)) 1))`.
It doesn't work in Ring Lisp but works in Common Lisp.

Ring Lisp provides function `copy` which deeply copies cons cells
by recursively copying car and cdr
(the name comes from LISP I and LISP 1.5).
We can use the `copy` to copy functions.
```
> (setq f '(lambda (x) (cons x x)))
(lambda (x) (cons x x))
> (f 1)
(1 . 1)
> ((copy f) 2)
(2 . 2)
```
If you feel uncomfortable about it,
please wrap it around `eval` in your heart like
```
(eval `(,(copy-tree '(lambda (x) (cons x x))) 2))
```
It doesn't work in Ring Lisp but works in Common Lisp.

Now that we're ready to write code which copies itself.
To write a recursive function, we will add another argument
which receives the function itself.

```lisp
(setq sum '(lambda (fn n acc)
  (if (eq n 0)
      acc
      (fn (copy fn) (- n 1) (+ n acc)))))
```
To call this function, we can write like this:
```
> (sum sum 100 0)
... generation: 2
... generation: 3
... generation: 4
... generation: 5
5050
```
Even though all cons cells are reclaimed (reused)
at least 3 times,
it returns the correct value.
This function depends on builtin functions, and local variables
which are newly created every time.
It doesn't depend on old cons cells on the ring buffer.

To be more accurate, it depends on an older function.
In the expression `(fn (copy fn) ...)`,
`(copy fn)` creates a new cons cells, but `fn` doesn't.
We can fix it by introducing a local variable:
```lisp
(setq sum '(lambda (fn n acc)
  (if (eq n 0)
      acc
      ((lambda (f)
         (f f (- n 1) (+ n acc)))
       (copy fn)))))
```
but it's not mandatory.
It's OK to depend on some old cons cells
if we can copy them before they are destroyed.
Note that the inner `lambda` is a normal closure
which has a reference to the outer environment,
so it can access `n` and `acc`.

## How to write non-iterative functions
Ring Lisp requires you to make functions tail recursive.
However some functions such as *Ackermann function* can't
be directly converted to a tail recursive functions.

```lisp
;;; non-tail-recursive Ackermann function
(defun ack (m n)
  (if (eq m 0)
      (+ n 1)
      (if (eq n 0)
          (ack (- m 1) 1)
          (ack (- m 1) (ack m (- n 1))))))
```

At the last line, there's a recursive call
inside a recursive call.
To make it tail recursive, we can use a stack
(represented as a list).

```lisp
(defun ack (m n s)
  (if (eq m 0)
      (if (eq s nil)
          (+ n 1)
          (ack (car s) (+ n 1) (cdr s)))
      (if (eq n 0)
          (ack (- m 1) 1 s)
          (ack m (- n 1) (cons (- m 1) s)))))
```

Instead of calculating
`(ack (- m 1) (ack m (- n 1)))` at a time,
it pushes `(- m 1)` on the stack and calculates only `(ack m (- n 1))`.
Once it finishes calculating `(ack m (- n 1))`,
it pops the `(- m 1)` from the stack
and calculates `(ack (- m 1) (ack m (- n 1)))`.

Now that we can convert it to *Ring Lisp style*:
```lisp
(setq ack '(lambda (fn m n s)
    (if (eq m 0)
        (if (eq s nil)
            (+ n 1)
            (fn (copy fn) (car s) (+ n 1) (copy (cdr s))))
        (if (eq n 0)
            (fn (copy fn) (- m 1) 1 (copy s))
            (fn (copy fn) m (- n 1)
                (cons (- m 1) (copy s)))))))
```
You can call it like `(ack ack 2 1 nil)`.
Note that it copies not only the function but also the stack,
otherwise the stack can be destroyed.

If you don't like managing a stack like this,
you can use continuation passing style:

```lisp
(defun ack (m n k)
  (if (eq m 0)
      (k (+ n 1))
      (if (eq n 0)
          (ack (- m 1) 1 k)
          (ack m (- n 1) (lambda (n) (ack (- m 1) n k))))))
```

Instead of using a stack, it creates a closure
which receives the value of `(ack m (- n 1))` and
calculates `(ack (- m 1) (ack m (- n 1)))`.
Note that the closure has a reference to the outer environment,
so it can access `m` and `k`.

To make it Ring Lisp style,
you need to convert the closure to a list begginng with `lambda`.
Otherwise closures can be destroyed.
However lists don't have references to outer environments.
So you need to embed these values into the list.

```lisp
(setq ack '(lambda (fn m n k)
  (if (eq m 0)
      (k (+ n 1))
      (if (eq n 0)
          (fn (copy fn) (- m 1) 1 (copy k))
          (fn (copy fn) m (- n 1)
              (list 'lambda '(n)
                (list (copy fn) (list 'quote (copy fn))
                  (- m 1) 'n (list 'quote (copy k)))))))))
```

To embed values into a list, we can use `list`
(unfortunately Ring Lisp doesn't support backquote).
Note that we need to write `quote` not to evaluate lists
which represent functions.
This function requires tons of cons cells because
it embeds whole `ack` twice into a continuation every time.
You can improve it by changing a continuation to receive `ack`:

```lisp
(setq ack '(lambda (fn m n k)
  (if (eq m 0)
      (k (+ n 1) fn)
      (if (eq n 0)
          (fn (copy fn) (- m 1) 1 (copy k))
          (fn (copy fn) m (- n 1)
              (list 'lambda '(n f)
                (list 'f 'f (- m 1) 'n
                      (list 'quote (copy k)))))))))
```

You can call it like `(ack ack 2 1 '(lambda (x _) x))`.
In this version, the continuation doesn't contain `ack`.
So it's much more space efficient.

This example also implies we can carry multiple functions
by arguments.
So we can write more general program now.

## Metacircular Evaluator
Surprisingly we can write a metacircular evaluator in Ring Lisp.
It's a simple *Lisp in Lisp* but we need to
* make functions tail recursive
* manage a stack by ourselves
* pass functions via arguments

It's possible to write such a program by hand,
but it's tough to write functions with
many arguments again and again.
I recommend to write tail recursive functions
which manage a stack (see eval.lisp),
then convert them to Ring Lisp style by a
code translation program (see compile.lisp).
This repository includes them, so you can try it.

```
% ./compile.sh -e "(cdr '(a b c))" | ./ringlisp -n 1024000
> (b c)
```

The expression `(cdr '(a b c))` is evaluated
by the metacircular evaluator.
Note that we need to increase the size of the ring buffer
by `-n` option.

Interestingly, we can write *normal* Lisp code in
this metacircular evaluator.

```
% cat sum100.lisp
((lambda ()
  (defun sum (n acc)
    (if (eq n 0)
        acc
        (sum (- n 1) (+ n acc))))
  (sum 100 0)))
% ./compile.sh < sum100.lisp | ./ringlisp -n 1024000
> ... generation: 1
... generation: 2
5050
```

As you can see, this `sum` doesn't copy itself but it works.
This is because the metacircular evaluator virtually
manage cons cells (it copies alive cons cells and leaves dead ones).

## Related Work
### Linear Lisp
[Linear Lisp](http://home.pipeline.com/~hbaker1/LinearLisp.html)
is a Lisp which doesn't require garbage collection.
In Linear Lisp, all local variables must be referenced exactly once.
In order to utilize a value more than once, it must be copied.
A cons cell can be (safely) reclaimed once it's referenced.
A traditional Lisp interpreter can be written in Linear Lisp.
The association list of variable bindings must be destroyed
in order to search it.
Even the Lisp program during evaluation must be destroyed.
That's somewhat similar to the metacircular evaluator in Ring Lisp.

### Cheney on the M.T.A.
[Cheney on the M.T.A.](http://home.pipeline.com/~hbaker1/CheneyMTA.html)
proposed an idea for compiling Scheme into C
without "trampoline".
Scheme program is translated into continuation-passing style,
so the target C functions never return.
It allocate objects on the C stack.
When the stack is about to overflow, alive objects are copied to heap
and stack is unwound by longjmp.
Ring Lisp uses "never return" style
but it uses a ring buffer instead of stack
(and it doesn't have heap or garbage collector).

## Author's Note
Of course, Ring Lisp is not a practical program.
It's a kind of joke.
I just wanted to implement Lisp on a ring buffer,
and I also wanted to know how I can write
Lisp code without garbage collection.
My important finding is:

*If we implement Lisp without garbege collector,
we will implement garbage collector in Lisp.*

It might be obvious but it's fun to realize it.
I didn't intend to implement the metacirtular evaluator.
I even didn't think I could implement it (with reasonable effort).
Once I found *function cloning technique*,
I found out I could write many varieties of programs
*relatively* easily.
At the same time I also found out it's a kind of garbage colletion
(it doesn't support structure sharing or circular lists though).

-- zick
