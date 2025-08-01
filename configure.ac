# Specify the project and version + the minimum automake version required.
AC_PREREQ(2.66)
AC_INIT([judo], [1.0.0])

# Instruct automake to place all intermediary files into a seperate directory.
# If this isn't specified, then it will pollute the main directory.
AC_CONFIG_AUX_DIR([config])

# Sanity check to verify this is indeed the source directory.
AC_CONFIG_SRCDIR(src/judo_scan.c)

# Initialize automake and use "foreign" so it doesn't attmpet to look for or
# create a GNU project structure: COPYING, NEWS, etc...
AM_INIT_AUTOMAKE([foreign])

# Check for the --enable-json-parser option.
AC_ARG_ENABLE([json-parser],
    [AS_HELP_STRING([--enable-json-parser], [for building an in-memory JSON tree (enabled by default)])],
    [enable_parser=$enableval],
    [enable_parser=yes]) # Default to enabling the parser.

# Check for the --enable-json-standard option.
AC_ARG_ENABLE([json-standard],
    [AS_HELP_STRING([--enable-json-standard=json5|rfc8259|rfc4627], [set the JSON standard (defaults to JSON5)])],
    [json_standard=$enableval],
    [json_standard=json5])  # Default to "json5" if no value is provided

# Check for the --enable-json-comments option.
AC_ARG_ENABLE([json-comments],
    [AS_HELP_STRING([--enable-json-comments], [enable JavaScript style comments (implictly enabled with JSON5)])],
    [AC_SUBST([WITH_COMMENTS], [1])],
    [AC_SUBST([WITH_COMMENTS], [0])])

# Check for the --enable-json-trailing-commas option.
AC_ARG_ENABLE([json-trailing-commas],
    [AS_HELP_STRING([--enable-json-trailing-commas], [enable trailing commas after the last JSON value in an object or array (implictly enabled in JSON5)])],
    [AC_SUBST([WITH_TRAILING_COMMAS], [1])],
    [AC_SUBST([WITH_TRAILING_COMMAS], [0])])

# Check for the --enable-json-float-storage option.
AC_ARG_ENABLE([json-float-storage],
    [AS_HELP_STRING([--enable-json-float-storage=float|double|longdouble|disabled], [set the floating point storage type (auto-detected by default)])],
    [float_storage=$enableval],
    [float_storage=auto])  # Default to infering whether floats are available.

# Check for the --enable-json-maximum-depth option.
AC_ARG_ENABLE([json-maximum-depth],
    [AS_HELP_STRING([--enable-json-maximum-depth=depth], [set the maximum nesting depth a JSON structure can be (defaults to 16)])],
    [maximum_nesting=$enableval],
    [maximum_nesting=16])  # Default to a maximum nesting depth of 16.

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
LT_INIT([win32-dll])

# Perform all tests in C.
AC_LANG([C])

# Enable the parser interface.
AS_IF([test "$enable_parser" = "yes"], [
  AC_SUBST([ENABLE_PARSER], [1])
])
AM_CONDITIONAL([HAVE_PARSER], [test "$enable_parser" = "yes"])

# If this option is specified without a value, then autotools defaults
# to "yes" -- in which case fallback on the default value.
AS_IF([test "$maximum_nesting" = "yes"], [
  maximum_nesting=16
])
AC_SUBST([MAXIMUM_NESTING_DEPTH], [$maximum_nesting])

# Check which JSON standard the user specified.
AS_IF([test "$json_standard" = "json5"], [
  AC_SUBST([ENABLE_JSON5], [1])
  AC_SUBST([ENABLE_RFC8259], [0])
  AC_SUBST([ENABLE_RFC4627], [0])
], [
  AS_IF([test "$json_standard" = "rfc8259"], [
    AC_SUBST([ENABLE_JSON5], [0])
    AC_SUBST([ENABLE_RFC8259], [1])
    AC_SUBST([ENABLE_RFC4627], [0])
  ], [
    AS_IF([test "$json_standard" = "rfc4627"], [
        AC_SUBST([ENABLE_JSON5], [0])
        AC_SUBST([ENABLE_RFC8259], [0])
        AC_SUBST([ENABLE_RFC4627], [1])
    ], [
        AC_MSG_ERROR([please specify a valid JSON5 standard])
    ])
  ])
])

have_floats="no"

# Check for long double.
AS_IF([test "$float_storage" = "auto"], [
  AC_MSG_CHECKING(for long double storage)
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM(
      [[
        #include <math.h>
      ]],[[
        long double x;
      ]]
    )], [have_floats="yes"])
  AC_MSG_RESULT($have_floats)
  AS_IF([test "$have_floats" = "yes"], [
    float_storage=longdouble
  ])
])

# Check for double.
AS_IF([test "$float_storage" = "auto"], [
  AC_MSG_CHECKING(for double storage)
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM(
      [[
        #include <math.h>
      ]],[[
        double x;
      ]]
    )], [have_floats="yes"])
  AC_MSG_RESULT($float_storage)
  AS_IF([test "$have_floats" = "yes"], [
    float_storage=double
  ])
])

# Check for float.
AS_IF([test "$float_storage" = "auto"], [
  AC_MSG_CHECKING(for float storage)
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM(
      [[
        #include <math.h>
      ]],[[
        float x;
      ]]
    )], [have_floats="yes"])
  AC_MSG_RESULT($float_storage)
  AS_IF([test "$have_floats" = "yes"], [
    float_storage=float
  ])
])

# Substitute variables depending on which float type was found.
AS_IF([test "$float_storage" = "float"], [
  AC_SUBST([HAVE_FLOAT], [1])
  AC_SUBST([HAVE_DOUBLE], [0])
  AC_SUBST([HAVE_LONGDOUBLE], [0])
], [
  AS_IF([test "$float_storage" = "double"], [
    AC_SUBST([HAVE_FLOAT], [0])
    AC_SUBST([HAVE_DOUBLE], [1])
    AC_SUBST([HAVE_LONGDOUBLE], [0])
  ], [
    AS_IF([test "$float_storage" = "longdouble"], [
        AC_SUBST([HAVE_FLOAT], [0])
        AC_SUBST([HAVE_DOUBLE], [0])
        AC_SUBST([HAVE_LONGDOUBLE], [1])
    ], [
        AS_IF([test "$float_storage" = "disabled"], [
            AC_SUBST([HAVE_FLOAT], [0])
            AC_SUBST([HAVE_DOUBLE], [0])
            AC_SUBST([HAVE_LONGDOUBLE], [0])
        ], [
            AC_MSG_ERROR([please specify a valid floating point storage type])
        ])
    ])
  ])
])

AC_CONFIG_FILES([
  Makefile
  man/Makefile
  include/Makefile
  src/Makefile
  examples/Makefile
  judo_config.h
  judo.pc
])
AC_OUTPUT
