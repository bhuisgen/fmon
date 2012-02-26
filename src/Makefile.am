prefix = @prefix@
datadir = @datadir@
docdir = @docdir@
localedir = $(datadir)/locale
sysconfdir = @sysconfdir@

DEFS = \
	-DPREFIX=\"$(prefix)\" \
	-DLOCALEDIR=\"$(localedir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	@DEFS@

INCLUDES = \
	-I@top_srcdir@/include \
	${DEPS_CFLAGS}

sbin_PROGRAMS = fmond

noinst_HEADERS = \
	common.h \
	daemon.h \
	fmond.h \
	gettext.h \
	log.h \
	log_console.h \
	log_file.h \
	log_syslog.h \
	utils.h

fmond_SOURCES = \
	daemon.c \
	fmond.c \
	log.c \
	log_console.c \
	log_file.c \
	log_syslog.c \
	utils.c

fmond_LDADD = \
    $(DEPS_LIBS) \
	$(INTLLIBS)

EXTRA_DIST = \
	$(fmond_SOURCES)
