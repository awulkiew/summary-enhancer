summary-enhancer
================

Program downloading Boost libraries regression summary pages and enhancing them for more convenient analysis.

================

The purpose is to change this page of equally-displayed failures:

![Before](before.png)

into meaningful list of specific errors to allow focusing on more important ones first:

![After](after.png)

Failures list:

 * comp - compilation error
 * link - linking error
 * run - run-time error
 * time - compilation time limit exceeded
 * file - compilation ended with "file too big" error
 * ierr - internal compiler error
 * unkn - unknown failure

================

Usage:

    summary-enhancer [OPTIONS] library...

Pass space separated list of libraries. In sublibs names use dash instead of slash, e.g. geometry-index

Example:

    summary-enhancer geometry geometry-index geometry-extensions

Options:

    --help                  produce help message
    --connections arg (=5)  max number of connections [1..100]
    --retries arg (=3)      max number of retries [1..10]
    --branch arg (=develop) branch name {develop, master}
    --verbose               show details
    
================

To compile the code, the following libraries are required:

 * Boost (http://www.boost.org)
 * cpp-netlib (http://cpp-netlib.org)
 * rapid-xml (included in this repo)

================

The program does the following steps:

  1. download required CSS file
  2. create output "result" directory
  3. for each passed library/sublib
    1. download summary page
    2. for each failed test
      1. download the log
      2. check the cause and modify test's entry
