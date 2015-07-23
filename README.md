README
======

Introduction
------------

CDK, which stands for C/C++ Development Kit, is a plugin for the Geany
IDE which adds advanced features for C and C++ (and eventually
Objective-C) languages. Making use of Clang's C library providing
access to the compiler internals allows the plugin to provide
intelligent features that just aren't feasible to do inside Geany's
core code for each language it supports.

The plugin is currently under active development and to be considered
experimental.

Features
--------

The planned features for the CDK plugin include:

  - Semantic syntax higlighting (partially implemented)
  - Improved intellisense-style autocompletion (partially implemented)
  - Real-time diagnostic reporting
  - Improved navigation between files and symbols
  - Automatic code-formatting
  - Smart refactoring features
  - Whatever other useful features are possible with libclang

Dependencies
------------

CDK obviously depends on the Geany API/library. Additionally, it
requires Geany to be built against GTK+ 3 (by configuring it with the
`--enable-gtk3` flag). Until the plugin is stabilized, the development
version of Geany (from Git) will be used as the target version.

The other notable dependency is Clang/LibClang. Many Linux
distributions provide packages for it, and the LLVM project which Clang
is a part of also provides repositories and packages for a number of
other platforms. Depending on how/where libclang and its development
headers were installed, you may have to set some environment variables
when configuring the build system. For example on my Ubuntu machine
using LLVM's repositories, with Clang 3.6, I need to configure CDK like
this:

    $ CFLAGS="-I/usr/lib/llvm-3.6/include" \
      LDFLAGS="-L/usr/lib/llvm-3.6/lib" \
        ./configure

In the future, CDK may start depending on Clang's unstable C++ API to
gain access to the required functionality as initial research seems to
indicate some features won't be possible to implement using the plain
C API.

Usage
-----

When the plugin is activated, it adds a tab to Geany's Project
preferences dialog named CDK. Inside this tab is where you can
configure a project to be supported by CDK.

The current configuration GUI is very basic and so quite annoying to
use. It has two text boxes, one for compiler flags and one for a list
of files that should be handled by the CDK plugin. Expect the GUI and
way of configuring the plugin to change drastically to make it more
user-friendly.

### Compiler Flags

This text box should contain the "cflags" which would be passed to the
Clang compiler on the command line. It seems to also handle putting the
full command, including the compiler executable.

Note: Depending on your Clang installation, you may have to add its
standard library header directory to the include search path (using the
`-I` flag). At least on Ubuntu, Clang seems to have problems finding
some of the platform-specific stdlib headers it provides on its own.

Hint: If you're compiling code that depends on libraries that provide a
`pkg-config` file, you can copy and paste the output of that utility
into the compiler flags text box.

### Source Files

This text box is where you tell the CDK plugin which files are part of
the project. The files should be listed each on their own line and
should be absolute or relative to the project's base directory.

These files correspond 1:1 with Clang "translation units" and they
represent the files which, when opened in Geany, will be parsed and
processed in order to provide the advanced features.
