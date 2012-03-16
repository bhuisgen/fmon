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

sbin_PROGRAMS = fmon

noinst_HEADERS = \
	common.h \
	daemon.h \
	fmon.h \
	gettext.h \
	log.h \
	log_console.h \
	log_file.h \
	log_syslog.h \
	utils.h

fmon_SOURCES = \
	daemon.c \
	fmon.c \
	log.c \
	log_console.c \
	log_file.c \
	log_syslog.c \
	utils.c

fmon_LDADD = \
    $(DEPS_LIBS) \
	$(INTLLIBS)

EXTRA_DIST = \
	$(fmon_SOURCES)
