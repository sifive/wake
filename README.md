[![][release-badge]][release] [![][ci-badge]][ci] [![][apache-2]](LICENSE)

# What is this?

Wake is a build orchestration tool and language.
If you have a build whose steps cannot be adequately expressed in
make/tup/bazel/etc, you probably need wake.
If you don't want to waste time rebuilding things that don't need it,
or that your colleagues already built, you might appreciate wake.

# Wake features:
  - dependent job execution

    Which jobs to run next can depend on the results of previous jobs.  For
    example, when you run configure in a traditional automake system, this
    typically affects what will be built by make.  Similarly cmake.  These
    two-stage build systems are necessary because make job selection cannot
    depend on the result of a prior build step.  In complicated builds,
    two-stages are sometimes not enough. In wake, all jobs may be dependent.

  - dependency analysis

    In classic build systems, you must specify all the inputs and outputs of
    a job you want to run.  If you under-specify the inputs, the build is
    not reproducible; it might fail to compile files that need recompilation
    and the job might fail non-deterministically on systems which run more
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

  - intrinsically-parallel language

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

# Installing dependencies

On Debian/Ubuntu (wheezy or later):

    sudo apt-get install makedev fuse libfuse-dev libsqlite3-dev libgmp-dev libncurses5-dev pkg-config git g++ gcc libre2-dev dash

On Redhat (6.6 or later):

    sudo yum install epel-release epel-release centos-release-scl
    # On RHEL6: sudo yum install devtoolset-6-gcc devtoolset-6-gcc-c++
    sudo yum install makedev fuse fuse-devel sqlite-devel gmp-devel ncurses-devel pkgconfig git gcc gcc-c++ re2-devel dash

On FreeBSD (12 or later):

    pkg install gmake pkgconf gmp re2 sqlite3 fusefs-libs dash
    echo 'fuse_load="YES"' >> /boot/loader.conf
    echo 'vfs.usermount=1' >> /etc/sysctl.conf
    pw groupmod operator -m YOUR-NON-ROOT-USER
    reboot

On Mac OS with Mac Ports installed:

    sudo port install osxfuse sqlite3 gmp re2 ncurses pkgconfig dash

On Mac OS with Home Brew installed:

    brew install gmp re2 pkgconfig dash
    
Fuse is slightly more complicated, it requires permissions.

    brew tap homebrew/cask
    brew cask install osxfuse
    
You should see something like the following, and MacOS may ask for your password.
    
    You must reboot for the installation of osxfuse to take effect.

    System Extension Blocked
    "The system extension required for mounting FUSE volumes could not be loaded.
    Please open the Security & Privacy System Preferences pane, go to the General preferences and allow loading system software from developer "Benjamin Fleischer".

    Then try mounting the volume again."

Give FUSE permission to run as stated in the instructions and you should be good to go.

# Building wake

    git clone https://github.com/sifive/wake.git
    cd wake
    git tag                 # See what versions exist
    #git checkout master    # Use development branch (e.g. recent bug fix)
    #git checkout v0.9      # Check out a specific version, like v0.9
    make
    ./bin/wake 'install "'$HOME'/stuff"' # or wherever

External dependencies:
 - c++ 11		>= 4.7	GPLv3		https://www.gnu.org/software/gcc/
 - dash			>= 0.5	BSD 3-clause	http://gondor.apana.org.au/~herbert/dash/
 - sqlite3-dev		>= 3.6	public domain	https://www.sqlite.org/
 - libgmp-dev		>= 4.3	LGPL v3		https://gmplib.org
 - libfuse-dev		>= 2.8	LGPL v2.1	https://github.com/libfuse/libfuse
 - libre2-dev		>= 2013	BSD 3-clause	https://github.com/google/re2
 - libncurses5-dev	>= 5.7	MIT		https://www.gnu.org/software/ncurses/

Optional dependencies:
 - re2c			>= 1.0	public domain	http://re2c.org
 - utf8proc		>= 2.0	MIT 		https://juliastrings.github.io/utf8proc/

Internal dependencies:
 - gopt			TFL		http://www.purposeful.co.uk/software/gopt/
 - SipHash		public domain	https://github.com/veorq/SipHash
 - BLAKE2		public domain	https://github.com/BLAKE2/libb2
 - whereami		WTFPLV2		https://github.com/gpakosz/whereami

# Documentation

Documentation for wake can be found in [share/doc/wake](share/doc/wake).

 - Try the [Tutorial](share/doc/wake/tutorial.md) for a step-by-step
   introduction.
 - The [Quick Reference Guide](share/doc/wake/quickref.md) is handy overview
   of wake syntax in cheat-sheet form.
 - The [Annotated Source Code](https://sifive.github.io/wake/) of wake can
   be useful when trying to understand the standard library.

[release-badge]: https://img.shields.io/github/tag/sifive/wake.svg?label=release
[release]: https://github.com/sifive/wake/releases/latest
[ci-badge]: https://circleci.com/gh/sifive/wake/tree/master.svg?style=shield
[ci]: https://circleci.com/gh/sifive/wake/tree/master
[apache-2]: https://img.shields.io/badge/license-Apache%202-blue.svg
