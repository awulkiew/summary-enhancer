****

The test results are now enhanced by Boost.Regression. This code was left here for historical purposes and won't be updated.

****

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
 * time - time limit exceeded
 * file - file too big or not enough space
 * ierr - internal compiler error
 * unkn - unknown failure

================

Usage:

    summary-enhancer [OPTIONS] library...

Pass space separated list of libraries. In sublibs names use hyphen (-) instead of slash (/), e.g. geometry-index

Example:

    summary-enhancer geometry geometry-index geometry-extensions

Options:

    --help                  produce help message
    --connections arg (=5)  max number of connections [1..100]
    --retries arg (=3)      max number of retries [1..10]
    --branch arg (=develop) branch name {develop, master}
    --track-changes         compare failures with the previous run
    --log-format arg (=xml) the format of failures log {xml, binary}
    --send-report           send an email containing the report about the
                            failures
    --save-report           save report to file
    --output-dir arg (=./)  the directory for enhanced summary pages and report
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
