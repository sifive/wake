# Logging & Console Output

This article gives a high level overview of how wake logging works,
and other things that impact what you see on the console when wake is executing or running database commands.
Check out [print.wake](https://github.com/sifive/wake/blob/master/share/wake/lib/core/print.wake)
for commented code.

## Loggers

Wake has a concept of loggers,
to which bytes can be written during wake execution by various sources.
Based on command line flags, each logger's output will be directed to
zero or more of `wake`'s own output streams, possibly with additional formatting applied.
The wake codebase and standard libraries often refer to a _logger_ as a `logLevel`.

What's the difference between the different loggers?
Which of wake's own output streams the results are sent to.
This can influence what you see on your console when you run wake.
The logger, in combination with the command line flags you invoke wake with,
controls *what* you see. The logger also controls what it looks like (what color it is).
It also controls which *actual* stream it is going to coming out of the executing `wake` process
(stderr or stdout, or other file descriptors 3-5).

`wake` has a number of built in loggers in wake which all start with `log...`.
You can also make your own logger using `mkLogLevel`.
You can use this to customize the color you want it to be shown as (if at all),
and giving more fine-grained control over which wake output stream it should go to.

For stdout, the following table should be read top to bottom considering the command line arguments you've passed to wake.
If you match a line in the table, stop!
This is what will appear on the console as you watch wake execute, and what is sent to wake's stdout:

|Flag               | logDebug | logInfo | logEcho | logReport | logWarning | logError | logNever | mkLogLevel "foo" Green|
|-------------------|----------|---------|---------|-----------|------------|----------|----------|----------------------
|Color on Console   |  Blue    |  Dim    |  default| Magenta   |  Yellow    |   Red    |          |      Green      |          
|--stdout="foo"     |          |         |         |           |            |          |          |         x       |
|--stdout="foo,info" |         |    x    |         |           |            |          |          |         x       |
|--debug            |      x   |    x    |    x    |     x     |      x     |    x     |          |                 |
|--verbose          |          |    x    |    x    |     x     |      x     |    x     |          |                 |
|--quiet            |          |         |         |           |            |    x     |          |                 |
|--no-tty<sup>*</sup>|         |         |    x    |     x     |      x     |    x     |          |                 |
|(default)          |          |         |         |     x     |      x     |    x     |          |                 |

<sup>*</sup> `--no-tty` also prevents any coloring from being applied.


Unless overridden with `--stderr=...` the *only* thing that ever appears on stderr coming out of wake is `logError`,
and it always appears there.

The additional command line arguments `--fd:3`, `--fd:4`, `--fd:5` can be used to output to file descriptors 3, 4, 5.
These must be opened. One example shell command would be, for some wake code that had `mkLogLevel "foo"`:

```
wake --fd3="foo" 3&>foo.txt
```

These can be concatenated with commas and include the normal log levels, so to capture both `mkLogLevel "foo"` and `logInfo` to a file:

```
wake --fd3="foo,info" 3&>foo.txt
```

## What Gets Sent To Loggers

### Job Execution

Every `Job` run by wake tracks its own stdout and stderr,
and when the underlying process prints to stdout or stderr,
it will go to the `Job`-specific stdout and stderr,
not the stdout and stderr of the `wake` process itself.
Every `Job` also has an "echo", which is the command that the job is executing.

When writing your wake code,
you can control which logger these `Job`-specific stdout, stderr, and echo are sent to.
This will influence whether they appear on the `wake` process's own stdout,
stderr, or other arbitrary file descriptors.
These can be controlled with `setPlanStderr`, `setPlanStdout`, and `setPlanEcho` before you `runJob`.
These default to `logInfo`, `logError`, and `logEcho`, respectively.

Regardless of to which logger you direct the `Job`'s `Stderr`, `Stdout`, and `Echo`,
`wake` will store all the text in its database,
discarding the information about the logger it was destined for at the time it was stored.
It will however know whether it came from the `Job`'s stderr/stdout/echo.


### Other Things that Appear on Wake's Output Streams During Execution

We have already covered the stdout, stderr, and echo of executed `Job`s.

The results of calling `print`/`println` within `wake` code are sent to `logReport` by default.
To send them somewhere else, use `printLevel`, `printlnLevel` instead.

The final output of whatever wake function or expression is called is always directed to
wake's `stdout` unless `--quiet` is set.
This is outside of the logger mechanism, so it is not possible to redirect this within wake
code or with command line flags.

Similarly, the "spinner" and progress bar is always sent to wake's `stdout` unless `--no-tty`
or `--quiet` is set.

## What is Displayed to user on Wake Database Commands

The `--verbose` command line argument has a different functionality when used with data base commands (`--last`, `-o`, `--failed`, etc).

Running `wake <database command> --verbose` will show ALL of stdout and stdin from all `Job`s,
regardless of what logger they were directed to.
Running a database command without `--verbose` will show NO stdout or stdin from any `Job`.
The command executed will always be displayed regardless of command line arguments.

Other things that are observed on stdout from executing wake (the results of `print` or `println`, etc)
are not stored in the database, so will never be revealed with any combination of flags on database commands.
