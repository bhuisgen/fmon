SUBDIRS = po src data doc

fmon_docdir = $(docdir)

fmon_doc_DATA = \
	AUTHORS \
	COPYING \
	ChangeLog \
	INSTALL \
	NEWS \
	README \
	TODO \
	VERSION

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = \
	$(fmon_doc_DATA) \
	config.rpath \
	m4/ChangeLog \
	autogen.sh
 
dist-hook:

bzdist: dist
	gunzip -c $(distdir).tar.gz | bzip2 > $(distdir).tar.bz2;
