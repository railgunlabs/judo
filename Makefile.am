SUBDIRS = man src include examples

EXTRA_DIST = autogen.sh LICENSE CMakeLists.txt JudoConfig.cmake.in README.md

AUTOMAKE_OPTIONS = subdir-objects

# Install, but do not distribute the judo_config.h header in the release archive.
nodist_include_HEADERS = judo_config.h
