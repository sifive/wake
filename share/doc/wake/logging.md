# Logging & Console Output

This article gives a high level overview of how wake logging works,
and other things that impact what you see on the console when wake is executing or running database commands.
Check out [print.wake](https://github.com/sifive/wake/blob/master/share/wake/lib/core/print.wake)
for commented code.

## Loggers & Log Levels

Wake has a concept of loggers,
to which text can be written during wake execution by various sources.
A logger combines a string log level (e.g. "error", "info", "my_custom_level")
and a display format (color and intensity).
There can be multiple loggers with the same log level,
though colors may not display as expected.

|Term Used In this Document | What wake code/libraries call it |
----------------------------|----------------------------------|
|  logger                   | `LogLevel`                       |
|  log level                | `LogLevel.name`                  |

## Using & Creating Loggers

`wake` has a number of built in loggers in which all start with `log...`.
You can also make your own logger using `mkLogLevel`.
You can use this to customize the color you want it to be shown as (if at all)
and give more fine-grained control over which wake output stream it should go to.


| wake `def`        | Color/Intensity | log level |
|-------------------|-----------------|-----------|
| logDebug          |     Blue        | "debug"   |
| logInfo           |     Dim         | "info"    |
| logEcho           |      N/A        | "echo"    |
| logReport         |    Magenta      | "report"  |
| logWarning        |    Yellow       | "warning" |
| logError          |     Red         | "error"   |
| logNever          |     N/A         | "null"    |
| mkLogLevel "foo" (Some Green) |  Green     | "foo"     |
| mkLogLevel2 "bar" (Some Green) (Some Bright) | Bright Green   | "bar"      |

## Directing Logger Output

Based on command line flags and its log level,
each logger's output will be directed to zero or more of wake's outputs streams.

For `wake`'s own stdout, the logger's color & intensity are used to apply
additional formatting to the output before displaying it.

For understanding which streams will appear on wake's stdout,
the following table should be read top to bottom considering the command line arguments you've passed to wake.
If you match a line in the table, stop!
This is what will appear on the console as you watch wake execute,
i.e. what is sent to ``wake`'s stdout:

| log level           | "debug"  | "info"  | "echo"  | "report"  | "warning"  | "error"  | "null"  |  "foo" |
|---------------------|----------|---------|---------|-----------|------------|----------|---------|--------|
|--stdout="foo"       |          |         |         |           |            |          |         |    x   |
|--stdout="foo,info"  |          |    x    |         |           |            |          |         |    x   |
|--debug              |      x   |    x    |    x    |     x     |      x     |    x     |         |        |
|--verbose            |          |    x    |    x    |     x     |      x     |    x     |         |        |
|--quiet              |          |         |         |           |            |    x     |         |        |
|--no-tty<sup>*</sup> |          |         |    x    |     x     |      x     |    x     |         |        |
|(default)            |          |         |         |     x     |      x     |    x     |         |        |

<sup>*</sup> `--no-tty` also prevents any coloring from being applied.

Unless overridden with `--stderr=...`,
the only logger output that ever appears on wake's stderr is from those with log level "error"
(e.g. `logError`).

The additional command line arguments `--fd:3`, `--fd:4`, `--fd:5` can be used to output to file descriptors 3, 4, 5.
One example shell command would be, for some wake code that had `mkLogLevel "foo"`:

```
wake --fd:3="foo" mywakecode 3>foo.txt
```

These can be concatenated with commas and include the normal log levels, so to capture both `mkLogLevel "foo"` and `logInfo` to a file:

```
wake --fd:3="foo,info" mywakecode 3>foo_and_info.txt
```

## What Gets Sent To Loggers

### Job Execution

Every `Job` run by wake tracks its own stdout and stderr,
and when the underlying process prints to stdout or stderr,
it will go to the `Job`-specific stdout and stderr,
not the stdout and stderr of the `wake` process itself.

When writing your wake code,
you can control which loggers these `Job`-specific stdout and stderr are sent to.
This will influence whether they appear on the `wake` process's own stdout,
stderr, or other arbitrary file descriptors.
These can be controlled with `setPlanStdout`, `setPlanStderr` before you `runJob`.
These default to `logInfo` and `logError` respectively.

Regardless of the logger to which you direct the `Job`'s `Stderr` and `Stdout`,
`wake` will store all the text in its database,
discarding the information about the logger it was destined for at the time it was stored.
It will however know whether it came from the `Job`'s stderr/stdout.

### Other Things that Appear on Wake's Output Streams During Execution

In addition to stdout and stderr, each executing `Job` will send the command
it is executing to a logger. The logger it is sent to by defalt is `logEcho`,
and this can be overridden with `setPlanEcho`.

The results of calling `print`/`println` within `wake` code are sent to `logReport` by default.
To send them somewhere else, use `printLevel`, `printlnLevel` instead.

The final output of whatever wake function or expression is called is always directed to
wake's `stdout` unless `--quiet` is set.
This is outside of the logger mechanism,
so it is not possible to redirect this within wake code or with command line flags.

Similarly, the progress bar is always sent to wake's `stdout` unless `--no-tty` is set.

## What is Displayed to user on Wake Database Commands

When used with a database command (`--last, --failed, --job`, etc),
the `--verbose` and `--metadata` flags impact what is shown per-job by the database.
They do not impact the selection of the `Job`s for which that information will be shown.

The default behavior is to show stdout and stderr for each `Job`
in an approximation to how they would be shown while executing the `Job`,
without a lot of "clutter" (aka metadata).
No information about the logger the stdout/stderr were destined for during execution
is recorded in the database and thus the colors setings of the logger for stdout
during execution are not used to influence the displayed output for database commands.

Using the `--metadata` flag will show a more "database" view of the output,
without showing each `Job`'s stdout or stderr.

Using the `--verbose` flag will show the database output as well as each `Job`'s stdout and stderr.

Other things that are observed on stdout from executing wake (the results of `print` or `println`, etc)
are not stored in the database, so will never be revealed with any combination of flags on database commands.

## Considerations for `printlnLevel` vs `setPlanStdout`

When writing wake code where one wants to see a `Job`'s stderr/stdout
regardless of whether that job executes in a given `wake` execution
(e.g. a job which collects and reports passing test results),
it is better to  `setPlanStdout logNever`, then explicitly `getJobStdout` and
send the result to a `printlnLevel`.
This is because the `Job`'s stdout is only sent to the logger if it actually executes
(vs its outputs being retrieved from cache),
whereas the `printLevel` is dependent on just the wake execution.
However, `setJobStdout` may still be preferred in some cases over `println`/`printlnLevel`,
because the latter will only print the entire output after the `Job` has finished,
while `setPlanStdout` allows it to be streamed line-by-line as it's printed by the `Job`.

