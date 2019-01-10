Wake is a build orchestration tool and language.
If you have a build whose steps cannot be adequately expressed in
make/tup/basel/etc, you probably need wake.
If you don't want to waste time rebuilding things that don't need it,
or that your colleagues already built, you might appreciate wake.

Wake features:
  - dependent job execution

    Which jobs to run next can depend on the results of previous jobs.  For
    example, when you run configure in a traditional automake system, this
    typically affects what will be built by make.  Similarly cmake.  These
    two-stage build systems are necessary because Make job selection cannot
    depend on the result of a prior build step.  In complicated builds,
    two-stages are sometimes not enough. In wake, all jobs may be dependent.

  - dependency analysis

    In classic build systems, you must specify all the inputs and outputs of
    a job you want to run.  If you under-specify the inputs, the build is
    not reproducible; it might fail to compile files that need recompilation
    and the job might fail non-deterministicly on systems which run more
    jobs simultaneously.  If you over-specify the inputs, the build performs
    unnecessary recompilation when those inputs change.  In wake, if you
    under-specify the inputs, the build fails every time.  If you
    over-specify the inputs, wake automatically prunes the unused
    dependencies so the job will not be re-run unless it must.  You almost
    never need to tell wake what files a job builds; it knows.

  - build introspection

    When you have a built workspace, it is helpful to be able to trace the
    provenance of build artefacts.  Wake keeps a database to record what it
    did.  You can query that database at any time to find out exactly how a
    file in your workspace got there.

  - intrinsicly-parallel language

    While your build orchestration files describe a sequence of compilation
    steps, the wake language automatically extracts parallelism.  Everything
    runs at once.  Only true data dependencies cause wake to sequence jobs. 
    Wake handles parallelism for you, so you don't need to think about it.

  - shared build caching

    You just checked-out the master branch and started a build.  However,
    your system runs the same software as your colleague who authored that
    commit.  If wake can prove it's safe, it will just copy the prebuilt
    files and save you time.  This can also translate into pull requests
    whose regression tests pass immediately, increasing productivity.

Installing wake:

  On Mac OS with Mac Ports installed:
    sudo port install osxfuse sqlite3 gmp re2 re2c libutf8proc
  On Debian/Ubuntu:
    sudo apt-get install ...

  git clone https://github.com/sifive/wake.git
  cd wake; make
  ./bin/wake 'install "/usr/local"' # or wherever

External dependencies:
 - c++ 11		GPLv3		http://gcc.gnu.org/...
 - sqlite3-dev		...
 - libgmp-dev
 - libfuse-dev
 - libre2-dev
 - re2c
 - utf8proc

Internal dependencies:
 - argagg		BSD 0 clause	http://
 - MurmurHash3		public domain
 - whereami		WTFPLV2
