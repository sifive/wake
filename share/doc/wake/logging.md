# Logging

This article gives a high level overview of how wake logging works.
Check out [print.wake](https://github.com/sifive/wake/blob/master/share/wake/lib/core/print.wake)
for commented code.

## How Logging works on Wake Execution commands

Every `Job` run by wake will have its own stdout and stderr,
and when the underlying process prints to stdout or stderr,
it will go the `Job`-specific stdout and stderr,
not the stdout and stderr of the `wake` process itself.

When writing your wake code,
you can control whether these `Job`-specific stdout and stderr are also printed to `wake`'s own stdout,
stderr, or other arbitrary file descriptors.
These can be controlled with `setPlanStderr` and `setPlanStdout` before you `runJob`.
We call this which _logger_ each goes to (wake code often refers to a _logger_ as a `logLevel`).

Regardless of to which logger you direct the `Job`'s `Stderr` and `Stdout`,
`wake` will store all the text in its database,
discarding the information about the logger it was destined for at the time it was stored.
It will however know whether it came from the `Job`'s stderr/stdout.

So what's the difference between the different loggers?
What you see on your console when you run wake.
It controls *what* you see (depending on the command line flags) and what it looks like (what color it is).
It also controls which *actual* stream it is going to coming out of `wake`
(stderr or stdout, or other file descriptors 3-5).

There are a number of built in loggers in wake which all start with `log...`.
You can also make your own logger using `mkLogLevel`.
You can use this to customize the color you want it to be shown as,
or (more commonly) to grab output from a `Job` for consuming within your wake program,
or redirecting outside of wake using shell redirect.

Unless overridden with `--fd:2=...` the *only* thing that ever appears on stderr coming out of wake is `logError`,
and it always appears there.

For stdout, the following table should be read top to bottom considering the command line arguments you've passed to wake.
If you match a line in the table, stop!
This is what will appear on the console as you watch wake execute, and what is sent to wake's stdout:

|Flag               | logDebug | logInfo | logEcho | logReport | logWarning | logError | logNever | mkLogLevel "foo" Green|
|-------------------|----------|---------|---------|-----------|------------|----------|----------|----------------------
|Color on Console   |  Blue    |  Dim    |  default| Magenta   |  Yellow    |   Red    |          |   Green         |          
|--fd:1="foo"       |          |         |         |           |            |          |          |         x       |
|--fd:1="foo,info"  |          |    x    |         |           |            |          |          |         x       |
|--no-execute       |          |         |         |           |            |    x     |          |                 |
|--debug            |      x   |    x    |    x    |     x     |      x     |    x     |          |                 |
|--verbose          |          |    x    |    x    |     x     |      x     |    x     |          |                 |
|--quiet            |          |         |         |           |            |    x     |          |                 |
|--no-tty           |          |         |    x    |     x     |      x     |    x     |          |                 |
|(default)          |          |         |         |     x     |      x     |    x     |          |                 |

`println` and `print` go to `logReport`. You can make print statements that go to other levels with `printlnLevel` or `printLevel`.
These statements are *not* saved to the wake database, no matter which `stream` they are sent to.

Of course one can always manually redirect or tee stdin/stdout to files within the command executed by the `Job`, outside
of these mechanisms.

## How Logging works on Wake Database Commands

The `--verbose` command line argument has a different functionalilty when used with data base commands (`--last`, `-o`, `--failed`, etc).

Running `wake <database command> --verbose` will show ALL of stdout and stdin from all `Job`s,
regardless of what logger they were directed to.
Running a database command without `--verbose` will show NO stdout or stdin from any `Job`.

Recall that results of `print` or `println` are not stored in the database, so will never be revealed with any combination
of flags.
