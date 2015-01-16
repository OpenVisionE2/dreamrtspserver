AM_CFLAGS = $(GST_CFLAGS) $(GSTRTSPSERVER_CFLAGS)

bin_PROGRAMS = dreamrtspserver

dreamrtspserver_SOURCES = dreamrtspserver.c
dreamrtspserver_LDADD = $(GST_LIBS) $(GSTRTSPSERVER_LIBS) $(GSTAPP_LIBS) $(GIO_LIBS)

dbus_confdir = `pkg-config --print-errors --variable sysconfdir dbus-1`/dbus-1/system.d
dbus_conf_DATA = dreamrtsp.conf
