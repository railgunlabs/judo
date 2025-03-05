EXTRA_DIST = CMakeLists.txt

noinst_PROGRAMS = scanner
scanner_SOURCES = scanner.c $(top_srcdir)/src/judo_stdin.c
scanner_LDADD = ../src/libjudo.a
scanner_CFLAGS = -I$(top_srcdir) -I$(top_srcdir)/include

if HAVE_PARSER
noinst_PROGRAMS = parser
parser_SOURCES = parser.c $(top_srcdir)/src/judo_stdin.c
parser_LDADD = ../src/libjudo.a
parser_CFLAGS = -I$(top_srcdir) -I$(top_srcdir)/include
endif
