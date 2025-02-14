EXTRA_DIST = CMakeLists.txt

lib_LIBRARIES = libjudo.a
libjudo_a_SOURCES = judo_scan.c judo_parse.c judo_unidata.c judo_utils.h $(top_srcdir)/include/judo.h $(top_srcdir)/judo_config.h
libjudo_a_CFLAGS = -I$(top_srcdir) -I$(top_srcdir)/include

if HAVE_PARSER
bin_PROGRAMS = judo
judo_SOURCES = judo_main.c judo_stdin.c $(top_srcdir)/include/judo.h $(top_srcdir)/judo_config.h
judo_LDADD = libjudo.a
judo_CFLAGS = -I$(top_srcdir) -I$(top_srcdir)/include
endif

# Install the header.
include_HEADERS = $(top_srcdir)/include/judo.h
