AM_CFLAGS = $(GST_CFLAGS) $(GSTRTSPSERVER_CFLAGS)

bin_PROGRAMS = dreamrtspserver

dreamrtspserver_SOURCES = dreamrtspserver.c
dreamrtspserver_LDADD = $(GST_LIBS) $(GSTRTSPSERVER_LIBS) $(GSTAPP_LIBS) $(GIO_LIBS)
