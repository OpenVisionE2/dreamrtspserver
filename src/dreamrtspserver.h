/*
 * GStreamer dreamrtspserver
 * Copyright 2015 Andreas Frisch <fraxinas@opendreambox.org>
 *
 * This program is licensed under the Creative Commons
 * Attribution-NonCommercial-ShareAlike 3.0 Unported
 * License. To view a copy of this license, visit
 * http://creativecommons.org/licenses/by-nc-sa/3.0/ or send a letter to
 * Creative Commons,559 Nathan Abbott Way,Stanford,California 94305,USA.
 *
 * Alternatively, this program may be distributed and executed on
 * hardware which is licensed by Dream Property GmbH.
 *
 * This program is NOT free software. It is open source, you are allowed
 * to modify it (if you keep the license), but it may not be commercially
 * distributed other than under the conditions noted above.
 */

#ifndef __DREAMRTSPSERVER_H__
#define __DREAMRTSPSERVER_H__

#include <string.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/app/app.h>

GST_DEBUG_CATEGORY (dreamrtspserver_debug);
#define GST_CAT_DEFAULT dreamrtspserver_debug

#define TOKEN_LEN 36

#define AAPPSINK "aappsink"
#define VAPPSINK "vappsink"
#define TSAPPSINK "tsappsink"

#define ES_AAPPSRC "es_aappsrc"
#define ES_VAPPSRC "es_vappsrc"
#define TS_APPSRC "ts_appsrc"

#define TS_PACK_SIZE 188
#define TS_PER_FRAME 7
#define BLOCK_SIZE   TS_PER_FRAME*188
#define TOKEN_LEN    36

#define MAX_OVERRUNS 5
#define OVERRUN_TIME G_GINT64_CONSTANT(15)*GST_SECOND
#define BITRATE_AVG_PERIOD G_GINT64_CONSTANT(5)*GST_SECOND

#define RESUME_DELAY 20

#define AUTO_BITRATE TRUE

G_BEGIN_DECLS

typedef enum {
        INPUT_MODE_LIVE = 0,
        INPUT_MODE_HDMI_IN = 1,
        INPUT_MODE_BACKGROUND = 2
} inputMode;

typedef enum {
        UPSTREAM_STATE_DISABLED = 0,
        UPSTREAM_STATE_CONNECTING = 1,
        UPSTREAM_STATE_WAITING = 2,
        UPSTREAM_STATE_TRANSMITTING = 3,
        UPSTREAM_STATE_OVERLOAD = 4,
        UPSTREAM_STATE_ADJUSTING = 5
} upstreamState;

typedef struct {
	GstElement *tcpsink;
	gulong inject_id;
	char token[TOKEN_LEN+1];
	upstreamState state;
	guint overrun_counter;
	GstClockTime overrun_period, measure_start;
	guint id_signal_waiting;
	gulong id_resume, id_bitrate_measure;
	gsize bitrate_sum;
	gint bitrate_avg;
	gboolean auto_bitrate;
} DreamTCPupstream;

typedef struct {
	gint audioBitrate, videoBitrate;
	guint framerate, width, height;
} SourceProperties;

typedef struct {
	GDBusConnection *dbus_connection;
	GMainLoop *loop;
	GstElement *pipeline;
	GstElement *asrc, *vsrc, *aparse, *vparse;
	GstElement *tsmux, *tsq;
	DreamTCPupstream *tcp_upstream;
	GMutex rtsp_mutex;
	GstClock *clock;
	SourceProperties source_properties;
} App;

static const gchar service[] = "com.dreambox.RTSPserver";
static const gchar object_name[] = "/com/dreambox/RTSPserver";
static GDBusNodeInfo *introspection_data = NULL;

static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='com.dreambox.RTSPserver'>"
  "    <method name='enableUpstream'>"
  "      <arg type='b' name='state' direction='in'/>"
  "      <arg type='s' name='host' direction='in'/>"
  "      <arg type='u' name='port' direction='in'/>"
  "      <arg type='s' name='token' direction='in'/>"
  "      <arg type='b' name='result' direction='out'/>"
  "    </method>"
  "    <method name='setResolution'>"
  "      <arg type='i' name='width' direction='in'/>"
  "      <arg type='i' name='height' direction='in'/>"
  "    </method>"
  "    <property type='i' name='upstreamState' access='read'/>"
  "    <property type='i' name='audioBitrate' access='readwrite'/>"
  "    <property type='i' name='videoBitrate' access='readwrite'/>"
  "    <property type='i' name='framerate' access='readwrite'/>"
  "    <property type='i' name='width' access='read'/>"
  "    <property type='i' name='height' access='read'/>"
  "    <property type='i' name='inputMode' access='readwrite'/>"
  "    <property type='b' name='autoBitrate' access='readwrite'/>"
  "    <signal name='sourceReady'/>"
  "    <signal name='encoderError'/>"
  "    <signal name='upstreamStateChanged'>"
  "      <arg type='i' name='state' direction='out'/>"
  "    </signal>"
  "    <signal name='tcpBitrate'>"
  "      <arg type='i' name='kbps' direction='out'/>"
  "    </signal>"
  "  </interface>"
  "</node>";

static gboolean gst_get_capsprop(App *app, GstElement *element, const gchar* prop_name, guint32 *value);
static gboolean gst_set_inputmode(App *app, inputMode input_mode);
static gboolean gst_set_framerate(App *app, int value);
static gboolean gst_set_resolution(App *app, int width, int height);
static gboolean gst_set_bitrate (App *app, GstElement *source, gint32 value);
static void get_source_properties (App *app);
static void apply_source_properties (App *app);

static void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer user_data);
static void on_name_acquired (GDBusConnection *connection, const gchar *name, gpointer user_data);
static void on_name_lost (GDBusConnection *connection, const gchar *name, gpointer user_data);
static GVariant *handle_get_property (GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GError **, gpointer);
static gboolean handle_set_property (GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *, GError **, gpointer);
static void handle_method_call (GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *, GDBusMethodInvocation *, gpointer);
static void send_signal (App *app, const gchar *signal_name, GVariant *parameters);

static gboolean message_cb (GstBus * bus, GstMessage * message, gpointer user_data);
static GstPadProbeReturn cancel_waiting_probe (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data);
static GstPadProbeReturn bitrate_measure_probe (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data);
gboolean upstream_set_waiting(App *app);
gboolean upstream_resume_transmitting(App *app);
static GstPadProbeReturn inject_authorization (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data);
static GstPadProbeReturn pad_probe_unlink_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data);
static void queue_underrun (GstElement *, gpointer);
static void queue_overrun (GstElement *, gpointer);
gboolean auto_adjust_bitrate(App *app);

gboolean create_source_pipeline(App *app);
gboolean halt_source_pipeline(App *app);
gboolean pause_source_pipeline(App *app);
gboolean unpause_source_pipeline(App *app);
gboolean enable_tcp_upstream(App *app, const gchar *upstream_host, guint32 upstream_port, const gchar *token);
gboolean disable_tcp_upstream(App *app);
gboolean destroy_pipeline(App *app);
gboolean quit_signal(gpointer loop);

G_END_DECLS

#endif /* __DREAMRTSPSERVER_H__ */
