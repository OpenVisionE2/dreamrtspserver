AM_CFLAGS = $(GST_CFLAGS) $(GSTRTSP_CFLAGS) $(GSTRTSPSERVER_CFLAGS) $(LIBSOUP_CFLAGS)

bin_PROGRAMS = dreamrtspserver

dreamrtspserver_SOURCES = dreamrtspserver.c gstdreamrtsp.c
dreamrtspserver_LDADD = $(GST_LIBS) $(GSTRTSP_LIBS) $(GSTRTSPSERVER_LIBS) $(GSTAPP_LIBS) $(GIO_LIBS) $(LIBSOUP_LIBS)

noinst_HEADERS = dreamrtspserver.h gstdreamrtsp.h

dbus_confdir = `pkg-config --print-errors --variable sysconfdir dbus-1`/dbus-1/system.d
dbus_conf_DATA = dreamrtsp.conf

systemd_unitdir = `pkg-config --print-errors --variable systemdsystemunitdir systemd`
systemd_unit_DATA = dreamrtspserver.service
