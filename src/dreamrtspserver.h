/*
 * GStreamer dreamrtspserver
 * Copyright 2015-2016 Andreas Frisch <fraxinas@opendreambox.org>
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
#include <errno.h>
#include <sys/stat.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/app/app.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <libsoup/soup.h>
#include "gstdreamrtsp.h"

GST_DEBUG_CATEGORY (dreamrtspserver_debug);
#define GST_CAT_DEFAULT dreamrtspserver_debug

#define DEFAULT_RTSP_PORT 554
#define DEFAULT_RTSP_PATH "/stream"
#define RTSP_ES_PATH_SUFX "-es"

#define HLS_PATH "/tmp/hls"
#define HLS_FRAGMENT_DURATION 2
#define HLS_FRAGMENT_NAME "segment%05d.ts"
#define HLS_PLAYLIST_NAME "dream.m3u8"

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
#define BITRATE_AVG_PERIOD G_GINT64_CONSTANT(6)*GST_SECOND

#define RESUME_DELAY 20

#define AUTO_BITRATE TRUE

#define WATCHDOG_TIMEOUT 5

#if HAVE_UPSTREAM
	#pragma message("building with mediator upstream feature")
#else
	#pragma message("building without mediator upstream feature")
#endif

#define DREAMRTSPSERVER_LOCK(obj) G_STMT_START {   \
    GST_LOG_OBJECT (obj,                           \
                    "LOCKING from thread %p",      \
                    g_thread_self ());             \
    g_mutex_lock (&obj->rtsp_mutex);               \
    GST_LOG_OBJECT (obj,                           \
                    "LOCKED from thread %p",       \
                    g_thread_self ());             \
} G_STMT_END

#define DREAMRTSPSERVER_UNLOCK(obj) G_STMT_START { \
    GST_LOG_OBJECT (obj,                           \
                    "UNLOCKING from thread %p",    \
                    g_thread_self ());             \
    g_mutex_unlock (&obj->rtsp_mutex);             \
} G_STMT_END

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
        UPSTREAM_STATE_ADJUSTING = 5,
	UPSTREAM_STATE_FAILED = 9
} upstreamState;

typedef enum {
        RTSP_STATE_DISABLED = 0,
        RTSP_STATE_IDLE = 1,
        RTSP_STATE_RUNNING = 2
} rtspState;

typedef enum {
        HLS_STATE_DISABLED = 0,
        HLS_STATE_IDLE = 1,
	HLS_STATE_RUNNING = 2
} hlsState;

typedef struct {
	GstElement *tstcpq, *tcpsink;
	char token[TOKEN_LEN+1];
	upstreamState state;
	guint overrun_counter;
	GstClockTime overrun_period, measure_start;
	guint id_signal_overrun, id_signal_waiting, id_signal_keepalive;
	gulong id_resume, id_bitrate_measure;
	gsize bitrate_sum;
	gint bitrate_avg;
	gboolean auto_bitrate;
} DreamTCPupstream;

typedef struct {
	GstDreamRTSPServer *server;
	GstRTSPMountPoints *mounts;
	GstDreamRTSPMediaFactory *es_factory, *ts_factory;
	GstRTSPMedia *es_media, *ts_media;
	GstElement *artspq, *vrtspq, *tsrtspq;
	GstElement *es_aappsrc, *es_vappsrc;
	GstElement *ts_appsrc;
	GstElement *aappsink, *vappsink, *tsappsink;
	GstClockTime rtsp_start_pts, rtsp_start_dts;
	gchar *rtsp_user, *rtsp_pass;
	GList *clients_list;
	gchar *rtsp_port;
	gchar *rtsp_ts_path, *rtsp_es_path;
	guint source_id;
	rtspState state;
	gchar *uri_parameters;
} DreamRTSPserver;

typedef struct {
	gint32 audioBitrate, videoBitrate, gopLength, bFrames, pFrames, slices;
	guint framerate, width, height, profile, level;
	gboolean gopOnSceneChange, openGop;
} SourceProperties;

typedef struct {
	GstElement *queue;
	GstElement *hlssink;
	hlsState state;
	SoupServer *soupserver;
	SoupAuthDomain *soupauthdomain;
	guint port;
	gchar *hls_user, *hls_pass;
	guint id_timeout;
} DreamHLSserver;

typedef struct {
	GDBusConnection *dbus_connection;
	GMainLoop *loop;
	GstElement *pipeline;
	GstElement *asrc, *vsrc, *aparse, *vparse;
	GstElement *tsmux, *tstee;
	GstElement *aq, *vq;
	GstElement *atee, *vtee;
	DreamTCPupstream *tcp_upstream;
	DreamRTSPserver *rtsp_server;
	DreamHLSserver *hls_server;
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
  "    <signal name='ping' />"
  "    <signal name='sourceStateChanged'>"
  "      <arg type='i' name='state' direction='out'/>"
  "    </signal>"
  "    <property type='i' name='sourceState' access='read'/>"
  "    <property type='i' name='audioBitrate' access='readwrite'/>"
  "    <property type='i' name='videoBitrate' access='readwrite'/>"
  "    <property type='i' name='gopLength' access='readwrite'/>"
  "    <property type='b' name='gopOnSceneChange' access='readwrite'/>"
  "    <property type='b' name='openGop' access='readwrite'/>"
  "    <property type='i' name='bFrames' access='readwrite'/>"
  "    <property type='i' name='pFrames' access='readwrite'/>"
  "    <property type='i' name='slices' access='readwrite'/>"
  "    <property type='i' name='framerate' access='readwrite'/>"
  "    <property type='i' name='inputMode' access='readwrite'/>"
  "    <property type='i' name='profile' access='readwrite'/>"
  "    <property type='i' name='level' access='readwrite'/>"
  "    <property type='i' name='width' access='read'/>"
  "    <property type='i' name='height' access='read'/>"
  "    <method name='setResolution'>"
  "      <arg type='i' name='width' direction='in'/>"
  "      <arg type='i' name='height' direction='in'/>"
  "    </method>"
  "    <method name='enableHLS'>"
  "      <arg type='b' name='state' direction='in'/>"
  "      <arg type='u' name='port' direction='in'/>"
  "      <arg type='s' name='user' direction='in'/>"
  "      <arg type='s' name='pass' direction='in'/>"
  "      <arg type='b' name='result' direction='out'/>"
  "    </method>"
  "    <signal name='hlsStateChanged'>"
  "      <arg type='i' name='state' direction='out'/>"
  "    </signal>"
  "    <property type='i' name='hlsState' access='read'/>"
  #if HAVE_UPSTREAM
  "    <method name='enableUpstream'>"
  "      <arg type='b' name='state' direction='in'/>"
  "      <arg type='s' name='host' direction='in'/>"
  "      <arg type='u' name='port' direction='in'/>"
  "      <arg type='s' name='token' direction='in'/>"
  "      <arg type='b' name='result' direction='out'/>"
  "    </method>"
  "    <signal name='upstreamStateChanged'>"
  "      <arg type='i' name='state' direction='out'/>"
  "    </signal>"
  "    <property type='i' name='upstreamState' access='read'/>"
  "    <signal name='tcpBitrate'>"
  "      <arg type='i' name='kbps' direction='out'/>"
  "    </signal>"
#endif
  "    <method name='enableRTSP'>"
  "      <arg type='b' name='state' direction='in'/>"
  "      <arg type='s' name='path' direction='in'/>"
  "      <arg type='u' name='port' direction='in'/>"
  "      <arg type='s' name='user' direction='in'/>"
  "      <arg type='s' name='pass' direction='in'/>"
  "      <arg type='b' name='result' direction='out'/>"
  "    </method>"
  "    <signal name='rtspClientCountChanged'>"
  "      <arg type='i' name='count' direction='out'/>"
  "      <arg type='s' name='host' direction='out'/>"
  "    </signal>"
  "    <property type='i' name='rtspClientCount' access='read'/>"
  "    <signal name='uriParametersChanged'>"
  "      <arg type='s' name='parameters' direction='out'/>"
  "    </signal>"
  "    <signal name='rtspStateChanged'>"
  "      <arg type='i' name='state' direction='out'/>"
  "    </signal>"
  "    <property type='i' name='rtspState' access='read'/>"
  "    <property type='s' name='uriParameters' access='read'/>"
  "    <property type='b' name='autoBitrate' access='readwrite'/>"
  "    <signal name='encoderError'/>"
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

void assert_tsmux(App *app);
gboolean assert_state(App *app, GstElement *element, GstState targetstate);

static gboolean message_cb (GstBus * bus, GstMessage * message, gpointer user_data);
static GstPadProbeReturn cancel_waiting_probe (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data);
static GstPadProbeReturn bitrate_measure_probe (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data);
gboolean upstream_keep_alive(App *app);
gboolean upstream_set_waiting(App *app);
gboolean upstream_resume_transmitting(App *app);
static GstPadProbeReturn inject_authorization (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data);
static void queue_underrun (GstElement *, gpointer);
static void queue_overrun (GstElement *, gpointer);
static void auto_adjust_bitrate(App *app);

gboolean create_source_pipeline(App *app);
gboolean halt_source_pipeline(App *app);
gboolean pause_source_pipeline(App *app);
gboolean unpause_source_pipeline(App *app);
gboolean destroy_pipeline(App *app);
gboolean watchdog_ping(gpointer user_data);
gboolean quit_signal(gpointer loop);
gboolean get_dot_graph (gpointer user_data);

DreamHLSserver *create_hls_server(App *app);
gboolean enable_hls_server(App *app, guint port, const gchar *user, const gchar *pass);
gboolean start_hls_pipeline(App *app);
gboolean stop_hls_pipeline(App *app);
gboolean disable_hls_server(App *app);
gboolean hls_client_timeout (gpointer user_data);
static void soup_do_get (SoupServer *server, SoupMessage *msg, const char *path, App *app);
static void soup_server_callback (SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *context, gpointer data);

gboolean enable_tcp_upstream(App *app, const gchar *upstream_host, guint32 upstream_port, const gchar *token);
gboolean disable_tcp_upstream(App *app);

DreamRTSPserver *create_rtsp_server(App *app);
gboolean enable_rtsp_server(App *app, const gchar *path, guint32 port, const gchar *user, const gchar *pass);
gboolean disable_rtsp_server(App *app);
gboolean start_rtsp_pipeline(App *app);

static void encoder_signal_lost(GstElement *, gpointer user_data);

G_END_DECLS

#endif /* __DREAMRTSPSERVER_H__ */
