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

#include <string.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/app/app.h>
#include <gst/rtsp-server/rtsp-server.h>

GST_DEBUG_CATEGORY (dreamrtspserver_debug);
#define GST_CAT_DEFAULT dreamrtspserver_debug

#define DEFAULT_RTSP_PORT 554
#define DEFAULT_RTSP_PATH "/stream"
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

#define MAX_OVERRUNS 20
#define OVERRUN_TIME G_GINT64_CONSTANT(10)*GST_SECOND

typedef enum {
        INPUT_MODE_LIVE = 0,
        INPUT_MODE_HDMI_IN = 1,
        INPUT_MODE_BACKGROUND = 2
} inputMode;

typedef enum {
        RTSP_STATE_DISABLED = 0,
        RTSP_STATE_IDLE = 1,
        RTSP_STATE_RUNNING = 2
} rtspState;

typedef enum {
        UPSTREAM_STATE_DISABLED = 0,
        UPSTREAM_STATE_CONNECTING = 1,
        UPSTREAM_STATE_WAITING = 2,
        UPSTREAM_STATE_TRANSMITTING = 3,
        UPSTREAM_STATE_OVERLOAD = 4
} upstreamState;

typedef struct {
	GstElement *tstcpq;
	GstElement *tcpsink;
	gulong inject_id;
	char token[TOKEN_LEN+1];
	upstreamState state;
	guint overrun_counter;
	GstClockTime overrun_period;
	guint id_signal_waiting;
} DreamTCPupstream;

typedef struct {
	GstRTSPServer *server;
	GstRTSPMountPoints *mounts;
	GstRTSPMediaFactory *es_factory, *ts_factory;
	GstRTSPMedia *es_media, *ts_media;
	GstElement *artspq, *vrtspq, *tsrtspq;
	GstElement *es_aappsrc, *es_vappsrc;
	GstElement *ts_appsrc;
	GstElement *aappsink, *vappsink, *tsappsink;
	GstClockTime rtsp_start_pts, rtsp_start_dts;
	gchar *rtsp_user, *rtsp_pass;
	GList *clients_list;
	gchar *rtsp_port;
	gchar *rtsp_path;
	guint source_id;
	rtspState state;
} DreamRTSPserver;

typedef struct {
	GDBusConnection *dbus_connection;
	GMainLoop *loop;

	GstElement *pipeline;
	GstElement *asrc, *vsrc;
	GstElement *aparse, *vparse;
	GstElement *aq, *vq;
	GstElement *atee, *vtee, *tstee;
	GstElement *tsmux;

	DreamTCPupstream *tcp_upstream;
	DreamRTSPserver *rtsp_server;
	GMutex rtsp_mutex;
	GstClock *clock;
} App;

static const gchar service[] = "com.dreambox.RTSPserver";
static const gchar object_name[] = "/com/dreambox/RTSPserver";
static GDBusNodeInfo *introspection_data = NULL;

static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='com.dreambox.RTSPserver'>"
  "    <method name='enableRTSP'>"
  "      <arg type='b' name='state' direction='in'/>"
  "      <arg type='s' name='path' direction='in'/>"
  "      <arg type='u' name='port' direction='in'/>"
  "      <arg type='s' name='user' direction='in'/>"
  "      <arg type='s' name='pass' direction='in'/>"
  "      <arg type='b' name='result' direction='out'/>"
  "    </method>"
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
  "    <property type='i' name='rtspState' access='read'/>"
  "    <property type='i' name='upstreamState' access='read'/>"
  "    <property type='i' name='clients' access='read'/>"
  "    <property type='i' name='audioBitrate' access='readwrite'/>"
  "    <property type='i' name='videoBitrate' access='readwrite'/>"
  "    <property type='i' name='framerate' access='readwrite'/>"
  "    <property type='i' name='width' access='read'/>"
  "    <property type='i' name='height' access='read'/>"
  "    <property type='i' name='inputMode' access='readwrite'/>"
  "    <signal name='sourceReady'/>"
  "    <signal name='encoderError'/>"
  "    <signal name='upstreamStateChanged'>"
  "      <arg type='i' name='state' direction='out'/>"
  "    </signal>"
  "  </interface>"
  "</node>";

gboolean create_source_pipeline(App *app);
gboolean halt_source_pipeline(App *app);
gboolean pause_source_pipeline(App *app);
gboolean unpause_source_pipeline(App *app);
DreamRTSPserver *create_rtsp_server(App *app);
gboolean enable_rtsp_server(App *app, const gchar *path, guint32 port, const gchar *user, const gchar *pass);
gboolean disable_rtsp_server(App *app);
gboolean start_rtsp_pipeline(App *app);
gboolean enable_tcp_upstream(App *app, const gchar *upstream_host, guint32 upstream_port, const gchar *token);
gboolean disable_tcp_upstream(App *app);
gboolean destroy_pipeline(App *app);
static void queue_underrun (GstElement *, gpointer);
static void queue_overrun (GstElement *, gpointer);

static gboolean gst_set_inputmode(App *app, inputMode input_mode)
{
	if (!app->pipeline)
		return FALSE;

	g_object_set (G_OBJECT (app->asrc), "input_mode", input_mode, NULL);
	g_object_set (G_OBJECT (app->vsrc), "input_mode", input_mode, NULL);

	inputMode ret1, ret2;
	g_object_get (G_OBJECT (app->asrc), "input_mode", &ret1, NULL);
	g_object_get (G_OBJECT (app->vsrc), "input_mode", &ret2, NULL);

	if (input_mode != ret1 || input_mode != ret2)
		return FALSE;

	GST_DEBUG("set input_mode %d", input_mode);
	return TRUE;
}

static gboolean gst_set_framerate(App *app, int value)
{
	GstCaps *oldcaps, *newcaps;
	GstStructure *structure;
	gboolean ret = FALSE;

	if (!app->pipeline)
		goto out;

	g_object_get (G_OBJECT (app->vsrc), "caps", &oldcaps, NULL);

	if (!GST_IS_CAPS(oldcaps))
		goto out;

	GST_DEBUG("set framerate %d fps... old caps %" GST_PTR_FORMAT, value, oldcaps);

	newcaps = gst_caps_make_writable(oldcaps);
	structure = gst_caps_steal_structure (newcaps, 0);
	if (!structure)
		goto out;

	if (value)
		gst_structure_set (structure, "framerate", GST_TYPE_FRACTION, value, 1, NULL);

	gst_caps_append_structure (newcaps, structure);
	GST_INFO("new caps %" GST_PTR_FORMAT, newcaps);
	g_object_set (G_OBJECT (app->vsrc), "caps", newcaps, NULL);
	newcaps = NULL;
	ret = TRUE;

out:
	if (oldcaps && GST_IS_CAPS(oldcaps))
		gst_caps_unref(oldcaps);
	if (newcaps && GST_IS_CAPS(newcaps))
		gst_caps_unref(newcaps);
	return ret;
}

static gboolean gst_set_resolution(App *app, int width, int height)
{
	GstCaps *oldcaps = NULL, *newcaps = NULL;
	GstStructure *structure;
	gboolean ret = FALSE;

	if (!app->pipeline)
		goto out;

	g_object_get (G_OBJECT (app->vsrc), "caps", &oldcaps, NULL);

	if (!GST_IS_CAPS(oldcaps))
		goto out;

	GST_DEBUG("set new resolution %ix%i... old caps %" GST_PTR_FORMAT, width, height, oldcaps);

	newcaps = gst_caps_make_writable(oldcaps);
	structure = gst_caps_steal_structure (newcaps, 0);
	if (!structure)
		goto out;

	if (width && height)
	{
		gst_structure_set (structure, "width", G_TYPE_INT, width, NULL);
		gst_structure_set (structure, "height", G_TYPE_INT, height, NULL);
	}
	gst_caps_append_structure (newcaps, structure);
	GST_INFO("new caps %" GST_PTR_FORMAT, newcaps);
	g_object_set (G_OBJECT (app->vsrc), "caps", newcaps, NULL);
	ret = TRUE;

out:
	if (oldcaps && GST_IS_CAPS(oldcaps))
		gst_caps_unref(oldcaps);
	if (newcaps && GST_IS_CAPS(newcaps))
		gst_caps_unref(newcaps);
	return ret;
}

static gboolean gst_get_capsprop(App *app, const gchar* element_name, const gchar* prop_name, guint32 *value)
{
	GstElement *element = NULL;
	GstCaps *caps = NULL;
	const GstStructure *structure;
	gboolean ret = FALSE;

	if (!app->pipeline)
		goto out;

	element = gst_bin_get_by_name(GST_BIN(app->pipeline), element_name);
	if (!element)
		goto out;

	g_object_get (G_OBJECT (element), "caps", &caps, NULL);

	if (!GST_IS_CAPS(caps))
		goto out;

	GST_DEBUG("current caps %" GST_PTR_FORMAT, caps);

	structure = gst_caps_get_structure (caps, 0);
	if (!structure)
		goto out;

	if (g_strcmp0 (prop_name, "framerate") == 0 && value)
	{
		const GValue *framerate = gst_structure_get_value (structure, "framerate");
		if (GST_VALUE_HOLDS_FRACTION(framerate))
			*value = gst_value_get_fraction_numerator (framerate);
		else
			*value = 0;
	}
	else if ((g_strcmp0 (prop_name, "width") == 0 || g_strcmp0 (prop_name, "height") == 0) && value)
	{
		if (!gst_structure_get_uint (structure, prop_name, (guint*)value))
			*value = 0;
	}
	else
		goto out;

	GST_DEBUG("%s.%s = %i", element_name, prop_name, *value);
	ret = TRUE;
out:
	if (element)
		gst_object_unref(element);
	if (caps)
		gst_caps_unref(caps);
	return ret;
}

static GVariant *handle_get_property (GDBusConnection  *connection,
				      const gchar      *sender,
				      const gchar      *object_path,
				      const gchar      *interface_name,
				      const gchar      *property_name,
				      GError          **error,
				      gpointer          user_data)
{
	App *app = user_data;

	GST_DEBUG("dbus get property %s from %s", property_name, sender);

	if (g_strcmp0 (property_name, "rtspState") == 0)
	{
		if (app->rtsp_server)
			return g_variant_new_int32 (app->rtsp_server->state);
	}
	if (g_strcmp0 (property_name, "upstreamState") == 0)
	{
		if (app->tcp_upstream)
			return g_variant_new_int32 (app->tcp_upstream->state);
	}
	else if (g_strcmp0 (property_name, "inputMode") == 0)
	{
		inputMode input_mode = -1;
		if (app->asrc)
			g_object_get (G_OBJECT (app->asrc), "input_mode", &input_mode, NULL);
		return g_variant_new_int32 (input_mode);
	}
	else if (g_strcmp0 (property_name, "clients") == 0)
	{
		return g_variant_new_int32 (g_list_length(app->rtsp_server->clients_list));
	}
	else if (g_strcmp0 (property_name, "audioBitrate") == 0)
	{
		gint rate = 0;
		if (app->asrc)
			g_object_get (G_OBJECT (app->asrc), "bitrate", &rate, NULL);
		return g_variant_new_int32 (rate);
	}
	else if (g_strcmp0 (property_name, "videoBitrate") == 0)
	{
		gint rate = 0;
		if (app->vsrc)
			g_object_get (G_OBJECT (app->vsrc), "bitrate", &rate, NULL);
		return g_variant_new_int32 (rate);
	}
	else if (g_strcmp0 (property_name, "width") == 0 || g_strcmp0 (property_name, "height") == 0 || g_strcmp0 (property_name, "framerate") == 0)
	{
		guint32 value;
		if (gst_get_capsprop(app, "dreamvideosource0", property_name, &value))
			return g_variant_new_int32(value);
		GST_WARNING("can't handle_get_property name=%s", property_name);
		return g_variant_new_int32(0);
	}
	else if (g_strcmp0 (property_name, "path") == 0)
	{
		return g_variant_new_string (app->rtsp_server->rtsp_path);
	}
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "[RTSPserver] Invalid property '%s'", property_name);
	return NULL;
} // handle_get_property

static gboolean handle_set_property (GDBusConnection  *connection,
				     const gchar      *sender,
				     const gchar      *object_path,
				     const gchar      *interface_name,
				     const gchar      *property_name,
				     GVariant         *value,
				     GError          **error,
				     gpointer          user_data)
{
	App *app = user_data;

	gchar *valstr = g_variant_print (value, TRUE);
	GST_DEBUG("dbus set property %s = %s from %s", property_name, valstr, sender);
	g_free (valstr);

	if (g_strcmp0 (property_name, "inputMode") == 0)
	{
		inputMode input_mode = g_variant_get_int32 (value);
		if (input_mode >= INPUT_MODE_LIVE && input_mode <= INPUT_MODE_BACKGROUND )
		{
			if (gst_set_inputmode(app, input_mode))
				return 1;
		}
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "[RTSPserver] can't set input_mode to %d", input_mode);
		return 0;
	}
	else if (g_strcmp0 (property_name, "audioBitrate") == 0)
	{
		if (app->asrc)
		{
			g_object_set (G_OBJECT (app->asrc), "bitrate", g_variant_get_int32 (value), NULL);
			return 1;
		}
	}
	else if (g_strcmp0 (property_name, "videoBitrate") == 0)
	{
		if (app->vsrc)
		{
			g_object_set (G_OBJECT (app->vsrc), "bitrate", g_variant_get_int32 (value), NULL);
			return 1;
		}
	}
	else if (g_strcmp0 (property_name, "framerate") == 0)
	{
		if (gst_set_framerate(app, g_variant_get_int32 (value)))
			return 1;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "[RTSPserver] can't set property '%s' to %d", property_name, g_variant_get_int32 (value));
		return 0;
	}
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "[RTSPserver] Invalid property: '%s'", property_name);
		return 0;
	} // unknown property
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "[RTSPserver] Wrong state - can't set property: '%s'", property_name);
	return 0;
} // handle_set_property

static void handle_method_call (GDBusConnection       *connection,
				const gchar           *sender,
				const gchar           *object_path,
				const gchar           *interface_name,
				const gchar           *method_name,
				GVariant              *parameters,
				GDBusMethodInvocation *invocation,
				gpointer               user_data)
{
	App *app = user_data;

	gchar *paramstr = g_variant_print (parameters, TRUE);
	GST_DEBUG("dbus handle method %s %s from %s", method_name, paramstr, sender);
	g_free (paramstr);
	if (g_strcmp0 (method_name, "enableRTSP") == 0)
	{
		gboolean result = FALSE;
		if (app->pipeline)
		{
			gboolean state;
			guint32 port;
			const gchar *path, *user, *pass;

			g_variant_get (parameters, "(b&su&s&s)", &state, &path, &port, &user, &pass);
			GST_DEBUG("app->pipeline=%p, enableRTSP state=%i path=%s port=%i user=%s pass=%s", app->pipeline, state, path, port, user, pass);

			if (state == TRUE)
				result = enable_rtsp_server(app, path, port, user, pass);
			else if (state == FALSE)
				result = disable_rtsp_server(app);
		}
		g_dbus_method_invocation_return_value (invocation,  g_variant_new ("(b)", result));
	}
	else if (g_strcmp0 (method_name, "enableUpstream") == 0)
	{
		gboolean result = FALSE;
		if (app->pipeline)
		{
			gboolean state;
			const gchar *upstream_host, *token;
			guint32 upstream_port;

			g_variant_get (parameters, "(b&su&s)", &state, &upstream_host, &upstream_port, &token);
			GST_DEBUG("app->pipeline=%p, enableUpstream state=%i host=%s port=%i token=%s", app->pipeline, state, upstream_host, upstream_port, token);

			if (state == TRUE && app->tcp_upstream->state == UPSTREAM_STATE_DISABLED)
				result = enable_tcp_upstream(app, upstream_host, upstream_port, token);
			else if (state == FALSE && app->tcp_upstream->state >= UPSTREAM_STATE_CONNECTING)
				result = disable_tcp_upstream(app);
		}
		g_dbus_method_invocation_return_value (invocation,  g_variant_new ("(b)", result));
	}
	else if (g_strcmp0 (method_name, "setResolution") == 0)
	{
		int width, height;
		g_variant_get (parameters, "(ii)", &width, &height);
		if (gst_set_resolution(app, width, height))
			g_dbus_method_invocation_return_value (invocation, NULL);
		else
			g_dbus_method_invocation_return_error (invocation, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "[RTSPserver] can't set resolution %dx%d", width, height);
	}
	// Default: No such method
	else
	{
		g_dbus_method_invocation_return_error (invocation, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "[RTSPserver] Invalid method: '%s'", method_name);
	} // if it's an unknown method
} // handle_method_call

static void send_signal (App *app, const gchar *signal_name, GVariant *parameters)
{
	if (app->dbus_connection)
	{
		GST_DEBUG ("sending signal name=%s parameters=%s", signal_name, parameters?g_variant_print (parameters, TRUE):"[not given]");
		g_dbus_connection_emit_signal (app->dbus_connection, NULL, object_name, service, signal_name, parameters, NULL);
	}
	else
		GST_DEBUG ("no dbus connection, can't send signal %s", signal_name);
}

static void on_bus_acquired (GDBusConnection *connection,
			     const gchar     *name,
			     gpointer        user_data)
{
	static GDBusInterfaceVTable interface_vtable =
	{
		handle_method_call,
		handle_get_property,
		handle_set_property
	};

	GError *error = NULL;
	GST_DEBUG ("aquired dbus (\"%s\" @ %p)", name, connection);
	g_dbus_connection_register_object (connection, object_name, introspection_data->interfaces[0], &interface_vtable, user_data, NULL, &error);
} // on_bus_acquired

static void on_name_acquired (GDBusConnection *connection,
			      const gchar     *name,
			      gpointer         user_data)
{
	App *app = user_data;
	app->dbus_connection = connection;
	GST_DEBUG ("aquired dbus name (\"%s\")", name);
	if (gst_element_set_state (app->pipeline, GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS)
		GST_ERROR ("Failed to bring state of source pipeline to READY");
} // on_name_acquired

static void on_name_lost (GDBusConnection *connection,
			  const gchar     *name,
			  gpointer         user_data)
{
	App *app = user_data;
	app->dbus_connection = NULL;
	GST_WARNING ("lost dbus name (\"%s\" @ %p)", name, connection);
	//  g_main_loop_quit (app->loop);
} // on_name_lost

static gboolean message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
	App *app = user_data;

	g_mutex_lock (&app->rtsp_mutex);
	switch (GST_MESSAGE_TYPE (message)) {
		case GST_MESSAGE_STATE_CHANGED:
		{
			GstState old_state, new_state;
			gst_message_parse_state_changed(message, &old_state, &new_state, NULL);
			if (old_state == new_state)
				break;

			if (GST_MESSAGE_SRC(message) == GST_OBJECT(app->pipeline))
			{
				GST_DEBUG_OBJECT(app, "state transition %s -> %s", gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
				GstStateChange transition = (GstStateChange)GST_STATE_TRANSITION(old_state, new_state);
				switch(transition)
				{
					case GST_STATE_CHANGE_NULL_TO_READY:
						send_signal (app, "sourceReady", NULL);
						break;
					default:
						break;
				}
			}
			break;
		}
		case GST_MESSAGE_ERROR:
		{
			GError *err = NULL;
			gchar *name, *debug = NULL;
			name = gst_object_get_path_string (message->src);
			gst_message_parse_error (message, &err, &debug);
			if (err->domain == GST_RESOURCE_ERROR)
			{
				if (err->code == GST_RESOURCE_ERROR_READ)
				{
					GST_INFO ("element %s: %s", name, err->message);
					send_signal (app, "encoderError", NULL);
					g_mutex_unlock (&app->rtsp_mutex);
					disable_tcp_upstream(app);
					destroy_pipeline(app);
				}
				if (err->code == GST_RESOURCE_ERROR_WRITE)
				{
					GST_INFO ("element %s: %s -> this means PEER DISCONNECTED", name, err->message);
					GST_LOG ("Additional ERROR debug info: %s", debug);
					disable_tcp_upstream(app);
				}
			}
			else
			{
				GST_ERROR ("ERROR: from element %s: %s", name, err->message);
				if (debug != NULL)
					GST_ERROR ("Additional debug info: %s", debug);
				GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"dreamrtspserver-error");
			}
			g_error_free (err);
			g_free (debug);
			g_free (name);
			break;
		}
		case GST_MESSAGE_WARNING:
		{
			GError *err = NULL;
			gchar *name, *debug = NULL;
			name = gst_object_get_path_string (message->src);
			gst_message_parse_warning (message, &err, &debug);
			GST_WARNING ("WARNING: from element %s: %s", name, err->message);
			if (debug != NULL)
				GST_WARNING ("Additional debug info: %s", debug);
			g_error_free (err);
			g_free (debug);
			g_free (name);
			break;
		}
		case GST_MESSAGE_EOS:
			g_print ("Got EOS\n");
			g_mutex_unlock (&app->rtsp_mutex);
			g_main_loop_quit (app->loop);
			return FALSE;
		default:
			break;
	}
	g_mutex_unlock (&app->rtsp_mutex);
	return TRUE;
}

static void media_unprepare (GstRTSPMedia * media, gpointer user_data)
{
	App *app = user_data;
	DreamRTSPserver *r = app->rtsp_server;
	GST_INFO("no more clients -> media unprepared!");

	g_mutex_lock (&app->rtsp_mutex);
	if (media == r->es_media)
	{
		r->es_media = NULL;
		r->es_aappsrc = r->es_vappsrc = NULL;
	}
	else if (media == r->ts_media)
	{
		r->ts_media = NULL;
		r->ts_appsrc = r->ts_appsrc = NULL;
	}
	if (!r->es_media && !r->ts_media)
	{
		if (app->tcp_upstream->state == UPSTREAM_STATE_DISABLED)
			halt_source_pipeline(app);
		r->state = RTSP_STATE_IDLE;
	}
	g_mutex_unlock (&app->rtsp_mutex);
}

static void client_closed (GstRTSPClient * client, gpointer user_data)
{
	App *app = user_data;
	app->rtsp_server->clients_list = g_list_remove(g_list_first (app->rtsp_server->clients_list), client);
	GST_INFO("client_closed  (number of clients: %i)", g_list_length(app->rtsp_server->clients_list));
}

static void client_connected (GstRTSPServer * server, GstRTSPClient * client, gpointer user_data)
{
	App *app = user_data;
	app->rtsp_server->clients_list = g_list_append(app->rtsp_server->clients_list, client);
	const gchar *ip = gst_rtsp_connection_get_ip (gst_rtsp_client_get_connection (client));
	GST_INFO("client_connected %" GST_PTR_FORMAT " from %s  (number of clients: %i)", client, ip, g_list_length(app->rtsp_server->clients_list));
	g_signal_connect (client, "closed", (GCallback) client_closed, app);
}

static void media_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media, gpointer user_data)
{
	App *app = user_data;
	DreamRTSPserver *r = app->rtsp_server;
	g_mutex_lock (&app->rtsp_mutex);

	if (factory == r->es_factory)
	{
		r->es_media = media;
		GstElement *element = gst_rtsp_media_get_element (media);
		r->es_aappsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), ES_AAPPSRC);
		r->es_vappsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), ES_VAPPSRC);
		gst_object_unref(element);
		g_signal_connect (media, "unprepared", (GCallback) media_unprepare, app);
		g_object_set (r->es_aappsrc, "format", GST_FORMAT_TIME, NULL);
		g_object_set (r->es_vappsrc, "format", GST_FORMAT_TIME, NULL);
	}
	else if (factory == r->ts_factory)
	{
		r->ts_media = media;
		GstElement *element = gst_rtsp_media_get_element (media);
		r->ts_appsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), TS_APPSRC);
		r->ts_appsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), TS_APPSRC);
		gst_object_unref(element);
		g_signal_connect (media, "unprepared", (GCallback) media_unprepare, app);
		g_object_set (r->ts_appsrc, "format", GST_FORMAT_TIME, NULL);
	}
	r->rtsp_start_pts = r->rtsp_start_dts = GST_CLOCK_TIME_NONE;
	r->state = RTSP_STATE_RUNNING;
	start_rtsp_pipeline(app);
	g_mutex_unlock (&app->rtsp_mutex);
}

gboolean upstream_set_waiting(App *app)
{
	app->tcp_upstream->overrun_counter = 0;
	app->tcp_upstream->overrun_period = GST_CLOCK_TIME_NONE;
	app->tcp_upstream->state = UPSTREAM_STATE_WAITING;
	send_signal (app, "upstreamStateChanged", g_variant_new("(i)", UPSTREAM_STATE_WAITING));
	g_signal_handlers_disconnect_by_func(app->tcp_upstream->tstcpq, G_CALLBACK (queue_overrun), app);
	g_signal_connect (app->tcp_upstream->tstcpq, "underrun", G_CALLBACK (queue_underrun), app);
	pause_source_pipeline(app);
	return FALSE;
}

static void queue_underrun (GstElement * queue, gpointer user_data)
{
	App *app = user_data;
	GST_DEBUG_OBJECT (queue, "queue underrun");
	if (queue == app->tcp_upstream->tstcpq && app->rtsp_server->state != RTSP_STATE_IDLE)
	{
		if (unpause_source_pipeline(app))
		{
			g_signal_handlers_disconnect_by_func (queue, G_CALLBACK (queue_underrun), app);
			g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), app);
			app->tcp_upstream->state = UPSTREAM_STATE_TRANSMITTING;
			send_signal (app, "upstreamStateChanged", g_variant_new("(i)", UPSTREAM_STATE_TRANSMITTING));
			if (app->tcp_upstream->overrun_period == GST_CLOCK_TIME_NONE)
				app->tcp_upstream->overrun_period = gst_clock_get_time (app->clock);
		}
	}
}

static void queue_overrun (GstElement * queue, gpointer user_data)
{
	App *app = user_data;
	DreamTCPupstream *t = app->tcp_upstream;
	if (queue == t->tstcpq && app->rtsp_server->state != RTSP_STATE_IDLE)
	{
		if (t->state == UPSTREAM_STATE_CONNECTING)
		{
			GST_DEBUG_OBJECT (queue, "initial queue overrun after connect");
			upstream_set_waiting (app);
		}
		else if (t->state == UPSTREAM_STATE_TRANSMITTING)
		{
			t->overrun_counter++;

			if (app->tcp_upstream->id_signal_waiting)
				g_source_remove (app->tcp_upstream->id_signal_waiting);

			GstClockTime now = gst_clock_get_time (app->clock);
			GST_DEBUG_OBJECT (queue, "queue overrun during transmit... %i (max %i) overruns within %" GST_TIME_FORMAT "", t->overrun_counter, MAX_OVERRUNS, GST_TIME_ARGS (now-app->tcp_upstream->overrun_period));
			if (t->overrun_counter >= MAX_OVERRUNS)
			{
				app->tcp_upstream->state = UPSTREAM_STATE_OVERLOAD;
				send_signal (app, "upstreamStateChanged", g_variant_new("(i)", UPSTREAM_STATE_OVERLOAD));
			}
			else
			{
				app->tcp_upstream->id_signal_waiting = g_timeout_add_seconds (5, (GSourceFunc) upstream_set_waiting, app);
			}

			if (now > t->overrun_period+OVERRUN_TIME)
			{
				t->overrun_counter = 0;
				t->overrun_period = now;
			}
		}
	}
}

static GstFlowReturn handover_payload (GstElement * appsink, gpointer user_data)
{
	App *app = user_data;
	DreamRTSPserver *r = app->rtsp_server;

	g_mutex_lock (&app->rtsp_mutex);
	GstAppSrc *appsrc = NULL;
	if ( appsink == r->vappsink )
		appsrc = GST_APP_SRC(r->es_vappsrc);
	else if ( appsink == r->aappsink )
		appsrc = GST_APP_SRC(r->es_aappsrc);
	else if ( appsink == r->tsappsink )
		appsrc = GST_APP_SRC(r->ts_appsrc);

	GstSample *sample = gst_app_sink_pull_sample (GST_APP_SINK (appsink));
	if (appsrc && g_list_length(r->clients_list) > 0) {
		GstBuffer *buffer = gst_sample_get_buffer (sample);
		GstCaps *caps = gst_sample_get_caps (sample);

		GST_LOG_OBJECT(appsink, "original PTS %" GST_TIME_FORMAT " DTS %" GST_TIME_FORMAT " @ %"GST_PTR_FORMAT"", GST_TIME_ARGS (GST_BUFFER_PTS (buffer)), GST_TIME_ARGS (GST_BUFFER_DTS (buffer)), appsrc);
		if (r->rtsp_start_pts == GST_CLOCK_TIME_NONE) {
			if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))
			{
				GST_LOG("GST_BUFFER_FLAG_DELTA_UNIT dropping!");
				gst_sample_unref(sample);
				g_mutex_unlock (&app->rtsp_mutex);
				return GST_FLOW_OK;
			}
			else if (appsink == r->vappsink)
			{
				r->rtsp_start_pts = GST_BUFFER_PTS (buffer);
				r->rtsp_start_dts = GST_BUFFER_DTS (buffer);
				GST_LOG_OBJECT(appsink, "frame is IFRAME! set rtsp_start_pts=%" GST_TIME_FORMAT " rtsp_start_dts=%" GST_TIME_FORMAT " @ %"GST_PTR_FORMAT"", GST_TIME_ARGS (GST_BUFFER_PTS (buffer)), GST_TIME_ARGS (GST_BUFFER_DTS (buffer)), appsrc);
			}
		}
		if (GST_BUFFER_PTS (buffer) < r->rtsp_start_pts)
			GST_BUFFER_PTS (buffer) = 0;
		else
			GST_BUFFER_PTS (buffer) -= r->rtsp_start_pts;
		GST_BUFFER_DTS (buffer) -= r->rtsp_start_dts;
		//    GST_LOG("new PTS %" GST_TIME_FORMAT " DTS %" GST_TIME_FORMAT "", GST_TIME_ARGS (GST_BUFFER_PTS (buffer)), GST_TIME_ARGS (GST_BUFFER_DTS (buffer)));

		GstCaps *oldcaps;

		oldcaps = gst_app_src_get_caps (appsrc);
		if (!oldcaps || !gst_caps_is_equal (oldcaps, caps))
		{
			GST_LOG("CAPS changed! %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, oldcaps, caps);
			gst_app_src_set_caps (appsrc, caps);
		}
		gst_app_src_push_buffer (appsrc, gst_buffer_ref(buffer));
	}
	else
	{
		if ( gst_debug_category_get_threshold (dreamrtspserver_debug) >= GST_LEVEL_LOG)
			GST_LOG_OBJECT(appsink, "no rtsp clients, discard payload!");
		else
			g_print (".");
	}
	gst_sample_unref (sample);
	g_mutex_unlock (&app->rtsp_mutex);

	return GST_FLOW_OK;
}

void assert_tsmux(App *app)
{
	if (app->tsmux)
		return;

	GST_DEBUG_OBJECT (app, "inserting tsmux");

	app->tsmux = gst_element_factory_make ("mpegtsmux", NULL);
	gst_bin_add (GST_BIN (app->pipeline), app->tsmux);

	GstPad *sinkpad, *srcpad;
	GstPadLinkReturn ret;

	srcpad = gst_element_get_static_pad (app->aq, "src");
	sinkpad = gst_element_get_compatible_pad (app->tsmux, srcpad, NULL);
	ret = gst_pad_link (srcpad, sinkpad);
	if (ret != GST_PAD_LINK_OK)
		g_error ("couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", srcpad, sinkpad);
	gst_object_unref (srcpad);
	gst_object_unref (sinkpad);

	srcpad = gst_element_get_static_pad (app->vq, "src");
	sinkpad = gst_element_get_compatible_pad (app->tsmux, srcpad, NULL);
	ret = gst_pad_link (srcpad, sinkpad);
	if (ret != GST_PAD_LINK_OK)
		g_error ("couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", srcpad, sinkpad);
	gst_object_unref (srcpad);
	gst_object_unref (sinkpad);

	if (!gst_element_link (app->tsmux, app->tstee))
		g_error ("couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", app->tsmux, app->tstee);
}

gboolean create_source_pipeline(App *app)
{
	GST_INFO_OBJECT(app, "create_source_pipeline");
	g_mutex_lock (&app->rtsp_mutex);
	app->pipeline = gst_pipeline_new (NULL);

	GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (app->pipeline));
	gst_bus_add_signal_watch (bus);
	g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (message_cb), app);
	gst_object_unref (GST_OBJECT (bus));

	app->asrc = gst_element_factory_make ("dreamaudiosource", "dreamaudiosource0");
	app->vsrc = gst_element_factory_make ("dreamvideosource", "dreamvideosource0");

	app->aparse = gst_element_factory_make ("aacparse", NULL);
	app->vparse = gst_element_factory_make ("h264parse", NULL);

	app->atee = gst_element_factory_make ("tee", "atee");
	app->vtee = gst_element_factory_make ("tee", "vtee");
	app->tstee = gst_element_factory_make ("tee", "tstee");

	app->aq = gst_element_factory_make ("queue", "aqueue");
	app->vq = gst_element_factory_make ("queue", "vqueue");

	app->tsmux = gst_element_factory_make ("mpegtsmux", NULL);

	if (!(app->asrc && app->vsrc && app->aparse && app->vparse && app->aq && app->vq && app->atee && app->vtee))
	{
		g_error ("Failed to create source pipeline element(s):%s%s%s%s%s%s%s%s%s", app->asrc?"":" dreamaudiosource", app->vsrc?"":" dreamvideosource", app->aparse?"":" aacparse",
			app->vparse?"":" h264parse", app->aq?"":" aqueue", app->vq?"":" vqueue", app->atee?"":" atee", app->vtee?"":" vtee", app->tsmux?"":"  mpegtsmux");
	}
	gst_object_unref(app->tsmux);
	app->tsmux = NULL;

	GstElement *appsink, *appsrc, *vpay, *apay, *udpsrc;
	appsink = gst_element_factory_make ("appsink", NULL);
	appsrc = gst_element_factory_make ("appsrc", NULL);
	vpay = gst_element_factory_make ("rtph264pay", NULL);
	apay = gst_element_factory_make ("rtpmp4apay", NULL);
	udpsrc = gst_element_factory_make ("udpsrc", NULL);

	if (!(appsink && appsrc && vpay && apay && udpsrc))
		g_error ("Failed to create rtsp element(s):%s%s%s%s%s", appsink?"":" appsink", appsrc?"":" appsrc", vpay?"": "rtph264pay", apay?"":" rtpmp4apay", udpsrc?"":" udpsrc" );
	else
	{
		gst_object_unref (appsink);
		gst_object_unref (appsrc);
		gst_object_unref (vpay);
		gst_object_unref (apay);
		gst_object_unref (udpsrc);
	}

	gst_bin_add_many (GST_BIN (app->pipeline), app->asrc, app->aparse, app->atee, app->aq, NULL);
	gst_bin_add_many (GST_BIN (app->pipeline), app->vsrc, app->vparse, app->vtee, app->vq, NULL);
	gst_bin_add (GST_BIN (app->pipeline), app->tstee);
	gst_element_link_many (app->asrc, app->aparse, app->atee, NULL);
	gst_element_link_many (app->vsrc, app->vparse, app->vtee, NULL);

	GstPad *teepad, *sinkpad;
	GstPadLinkReturn ret;

	teepad = gst_element_get_request_pad (app->atee, "src_%u");
	sinkpad = gst_element_get_static_pad (app->aq, "sink");
	ret = gst_pad_link (teepad, sinkpad);
	if (ret != GST_PAD_LINK_OK)
		g_error ("couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", teepad, sinkpad);
	gst_object_unref (teepad);
	gst_object_unref (sinkpad);

	teepad = gst_element_get_request_pad (app->vtee, "src_%u");
	sinkpad = gst_element_get_static_pad (app->vq, "sink");
	ret = gst_pad_link (teepad, sinkpad);
	if (ret != GST_PAD_LINK_OK)
		g_error ("couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", teepad, sinkpad);
	gst_object_unref (teepad);
	gst_object_unref (sinkpad);

	app->clock = gst_system_clock_obtain();
	gst_pipeline_use_clock(GST_PIPELINE (app->pipeline), app->clock);

	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"create_source_pipeline");
	g_mutex_unlock (&app->rtsp_mutex);
	return gst_element_set_state (app->pipeline, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS;
}

static GstPadProbeReturn inject_authorization (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	GstBuffer *token_buf = gst_buffer_new_wrapped (app->tcp_upstream->token, TOKEN_LEN);
	GstPad * srcpad = gst_element_get_static_pad (app->tcp_upstream->tstcpq, "src");

	GST_INFO ("injecting authorization on pad %s:%s, created token_buf %" GST_PTR_FORMAT "", GST_DEBUG_PAD_NAME (sinkpad), token_buf);
	gst_pad_remove_probe (sinkpad, app->tcp_upstream->inject_id);
	gst_pad_push (srcpad, gst_buffer_ref(token_buf));

	return GST_PAD_PROBE_REMOVE;
}

gboolean enable_tcp_upstream(App *app, const gchar *upstream_host, guint32 upstream_port, const gchar *token)
{
	GST_DEBUG_OBJECT(app, "enable_tcp_upstream host=%s port=%i token=%s", upstream_host, upstream_port, token);

	if (!app->pipeline)
	{
		GST_ERROR_OBJECT (app, "failed to enable upstream because source pipeline is NULL!");
		goto fail;
	}

	DreamTCPupstream *t = app->tcp_upstream;

	if (t->state == UPSTREAM_STATE_DISABLED)
	{
		assert_tsmux (app);

		t->id_signal_waiting = 0;
		t->state = UPSTREAM_STATE_CONNECTING;
                send_signal (app, "upstreamStateChanged", g_variant_new("(i)", t->state));

		t->tstcpq   = gst_element_factory_make ("queue", "tstcpqueue");
		t->tcpsink = gst_element_factory_make ("tcpclientsink", NULL);

		if (!(t->tstcpq && t->tcpsink ))
		g_error ("Failed to create tcp upstream element(s):%s%s", t->tstcpq?"":"  ts queue", t->tcpsink?"":"  tcpclientsink" );

		g_object_set (G_OBJECT (t->tstcpq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(4)*GST_SECOND, NULL);
		g_signal_connect (t->tstcpq, "overrun", G_CALLBACK (queue_overrun), app);

		g_object_set (t->tcpsink, "max-lateness", G_GINT64_CONSTANT(3)*GST_SECOND, NULL);
		g_object_set (t->tcpsink, "blocksize", BLOCK_SIZE, NULL);

		g_object_set (t->tcpsink, "qos", TRUE, NULL);
		g_object_set (t->tcpsink, "host", upstream_host, NULL);
		g_object_set (t->tcpsink, "port", upstream_port, NULL);
		gchar *check_host;
		guint32 check_port;
		g_object_get (t->tcpsink, "host", &check_host, NULL);
		g_object_get (t->tcpsink, "port", &check_port, NULL);
		if (g_strcmp0 (upstream_host, check_host))
		{
			g_free (check_host);
			GST_ERROR_OBJECT (app, "couldn't set upstream_host %s", upstream_host);
			goto fail;
		}
		if (upstream_port != check_port)
		{
			GST_ERROR_OBJECT (app, "couldn't set upstream_port %d", upstream_port);
			goto fail;
		}
		g_free (check_host);

		GstStateChangeReturn sret = gst_element_set_state (t->tcpsink, GST_STATE_READY);
		if (sret == GST_STATE_CHANGE_FAILURE)
		{
			GST_ERROR_OBJECT (app, "failed to set tcpsink to GST_STATE_READY. %s:%d probably refused connection", upstream_host, upstream_port);
			gst_object_unref (t->tstcpq);
			gst_object_unref (t->tcpsink);
			t->state = UPSTREAM_STATE_DISABLED;
			send_signal (app, "upstreamStateChanged", g_variant_new("(i)", t->state));
			return FALSE;
		}

		gst_bin_add_many (GST_BIN(app->pipeline), t->tstcpq, t->tcpsink, NULL);

		GstPadLinkReturn ret;
		GstPad *srcpad, *sinkpad;
		srcpad = gst_element_get_request_pad (app->tstee, "src_%u");
		sinkpad = gst_element_get_static_pad (t->tstcpq, "sink");
		ret = gst_pad_link (srcpad, sinkpad);
		gst_object_unref (srcpad);
		gst_object_unref (sinkpad);
		if (ret != GST_PAD_LINK_OK)
		{
			GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", srcpad, sinkpad);
			goto fail;
		}

		if (!gst_element_link (t->tstcpq, t->tcpsink)) {
			GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", t->tstcpq, t->tcpsink);
			goto fail;
		}

		if (strlen(token))
		{
			sinkpad = gst_element_get_static_pad (t->tcpsink, "sink");
			strcpy(t->token, token);
			t->inject_id = gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) inject_authorization, app, NULL);
			gst_object_unref (sinkpad);
		}
		sret = gst_element_set_state (app->pipeline, GST_STATE_PLAYING);

		if (sret == GST_STATE_CHANGE_FAILURE)
		{
			GST_ERROR_OBJECT (app, "GST_STATE_CHANGE_FAILURE for upstream pipeline");
			goto fail;
		}
		else if (sret == GST_STATE_CHANGE_ASYNC)
		{
			GstState state;
                        gst_element_get_state (GST_ELEMENT(app->pipeline), &state, NULL, 3*GST_SECOND);
			GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"enable_tcp_upstream");
			GValue item = G_VALUE_INIT;
				GstIterator* iter = gst_bin_iterate_elements(GST_BIN(app->pipeline));
				while (GST_ITERATOR_OK == gst_iterator_next(iter, (GValue*)&item))
				{
					GstElement *elem = g_value_get_object(&item);
					gst_element_get_state (elem, &state, NULL, GST_USECOND);
					if ( state != GST_STATE_PLAYING)
						GST_DEBUG_OBJECT(app, "%" GST_PTR_FORMAT"'s state=%s", elem, gst_element_state_get_name (state));
				}
				gst_iterator_free(iter);
			if (state != GST_STATE_PLAYING)
			{
				GST_ERROR_OBJECT (app, "state != GST_STATE_PLAYING");
				goto fail;
			}
		}
		GST_INFO_OBJECT(app, "enabled TCP upstream! upstreamState = UPSTREAM_STATE_CONNECTING");
		return TRUE;
	}
	else
		GST_INFO_OBJECT (app, "tcp upstream already enabled! (upstreamState = %i)", t->state);
	return FALSE;

fail:
	disable_tcp_upstream(app);
	return FALSE;
}

DreamRTSPserver *create_rtsp_server(App *app)
{
	DreamRTSPserver *r = malloc(sizeof(DreamRTSPserver));
	r->state = RTSP_STATE_DISABLED;
	r->server = NULL;
	r->ts_factory = r->es_factory = NULL;
	r->ts_media = r->es_media = NULL;
	r->ts_appsrc = r->es_aappsrc = r->es_vappsrc = NULL;
	r->clients_list = NULL;
	return r;
}

gboolean enable_rtsp_server(App *app, const gchar *path, guint32 port, const gchar *user, const gchar *pass)
{
	GST_INFO_OBJECT(app, "enable_rtsp_server path=%s port=%i user=%s pass=%s", path, port, user, pass);

	if (!app->pipeline)
	{
		GST_ERROR_OBJECT (app, "failed to enable rtsp server because source pipeline is NULL!");
		return FALSE;
	}

	g_mutex_lock (&app->rtsp_mutex);
	DreamRTSPserver *r = app->rtsp_server;

	if (r->state == RTSP_STATE_DISABLED)
	{
		r->artspq = gst_element_factory_make ("queue", "rtspaudioqueue");
		r->vrtspq = gst_element_factory_make ("queue", "rtspvideoqueue");
		r->aappsink = gst_element_factory_make ("appsink", AAPPSINK);
		r->vappsink = gst_element_factory_make ("appsink", VAPPSINK);

		gst_bin_add_many (GST_BIN (app->pipeline), r->artspq, r->vrtspq, r->aappsink, r->vappsink,  NULL);
		gst_element_link (r->artspq, r->aappsink);
		gst_element_link (r->vrtspq, r->vappsink);

		g_object_set (G_OBJECT (r->artspq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);
		g_object_set (G_OBJECT (r->vrtspq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);

		g_object_set (G_OBJECT (r->aappsink), "emit-signals", TRUE, NULL);
		g_object_set (G_OBJECT (r->aappsink), "enable-last-sample", FALSE, NULL);
		g_signal_connect (r->aappsink, "new-sample", G_CALLBACK (handover_payload), app);

		g_object_set (G_OBJECT (r->vappsink), "emit-signals", TRUE, NULL);
		g_object_set (G_OBJECT (r->vappsink), "enable-last-sample", FALSE, NULL);
		g_signal_connect (r->vappsink, "new-sample", G_CALLBACK (handover_payload), app);

		r->tsrtspq = gst_element_factory_make ("queue", "tsrtspqueue");
		r->tsappsink = gst_element_factory_make ("appsink", TSAPPSINK);
		gst_bin_add_many (GST_BIN (app->pipeline), r->tsrtspq, r->tsappsink,  NULL);
		gst_element_link (r->tsrtspq, r->tsappsink);

		g_object_set (G_OBJECT (r->tsrtspq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);

		g_object_set (G_OBJECT (r->tsappsink), "emit-signals", TRUE, NULL);
		g_object_set (G_OBJECT (r->tsappsink), "enable-last-sample", FALSE, NULL);
		g_signal_connect (r->tsappsink, "new-sample", G_CALLBACK (handover_payload), app);

		GstPad *teepad, *sinkpad;
		GstPadLinkReturn ret;
		teepad = gst_element_get_request_pad (app->atee, "src_%u");
		sinkpad = gst_element_get_static_pad (r->artspq, "sink");
		ret = gst_pad_link (teepad, sinkpad);
		if (ret != GST_PAD_LINK_OK)
		{
			GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", teepad, sinkpad);
			goto fail;
		}
		gst_object_unref (teepad);
		gst_object_unref (sinkpad);
		teepad = gst_element_get_request_pad (app->vtee, "src_%u");
		sinkpad = gst_element_get_static_pad (r->vrtspq, "sink");
		ret = gst_pad_link (teepad, sinkpad);
		if (ret != GST_PAD_LINK_OK)
		{
			GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", teepad, sinkpad);
			goto fail;
		}
		gst_object_unref (teepad);
		gst_object_unref (sinkpad);
		teepad = gst_element_get_request_pad (app->tstee, "src_%u");
		sinkpad = gst_element_get_static_pad (r->tsrtspq, "sink");
		ret = gst_pad_link (teepad, sinkpad);
		if (ret != GST_PAD_LINK_OK)
		{
			GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", teepad, sinkpad);
			goto fail;
		}
		gst_object_unref (teepad);
		gst_object_unref (sinkpad);

		GstStateChangeReturn sret;
		if (app->tcp_upstream->state == UPSTREAM_STATE_DISABLED)
			sret = gst_element_set_state (app->pipeline, GST_STATE_READY);
		else
			sret = gst_element_set_state (app->pipeline, GST_STATE_PLAYING);

		if (sret == GST_STATE_CHANGE_FAILURE)
		{
			GST_ERROR_OBJECT (app, "state change failure for local rtsp pipeline");
			return FALSE;
		}

		r->server = gst_rtsp_server_new ();
		g_signal_connect (r->server, "client-connected", (GCallback) client_connected, app);

		r->es_factory = gst_rtsp_media_factory_new ();
		gst_rtsp_media_factory_set_launch (r->es_factory, "( appsrc name=" ES_VAPPSRC " ! h264parse ! rtph264pay name=pay0 pt=96   appsrc name=" ES_AAPPSRC " ! aacparse ! rtpmp4apay name=pay1 pt=97 )");
		gst_rtsp_media_factory_set_shared (r->es_factory, TRUE);

		g_signal_connect (r->es_factory, "media-configure", (GCallback) media_configure, app);

		r->ts_factory = gst_rtsp_media_factory_new ();
		gst_rtsp_media_factory_set_launch (r->ts_factory, "( appsrc name=" TS_APPSRC " ! queue ! rtpmp2tpay name=pay0 pt=96 )");
		gst_rtsp_media_factory_set_shared (r->ts_factory, TRUE);

		g_signal_connect (r->ts_factory, "media-configure", (GCallback) media_configure, app);

		g_mutex_unlock (&app->rtsp_mutex);

		gchar *credentials = "";
		if (strlen(user)) {
			r->rtsp_user = g_strdup(user);
			r->rtsp_pass = g_strdup(pass);
			GstRTSPToken *token;
			gchar *basic;
			GstRTSPAuth *auth = gst_rtsp_auth_new ();
			gst_rtsp_media_factory_add_role (r->es_factory, "user", GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE, GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
			gst_rtsp_media_factory_add_role (r->ts_factory, "user", GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE, GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
			token = gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "user", NULL);
			basic = gst_rtsp_auth_make_basic (r->rtsp_user, r->rtsp_pass);
			gst_rtsp_server_set_auth (r->server, auth);
			gst_rtsp_auth_add_basic (auth, basic, token);
			g_free (basic);
			gst_rtsp_token_unref (token);
			credentials = g_strdup_printf("%s:%s@", user, pass);
		}
		else
			r->rtsp_user = r->rtsp_pass = NULL;

		r->rtsp_port = g_strdup_printf("%i", port ? port : DEFAULT_RTSP_PORT);

		gst_rtsp_server_set_service (r->server, r->rtsp_port);

		if (strlen(path))
			r->rtsp_path = g_strdup_printf ("%s%s", path[0]=='/' ? "" : "/", path);
		else
			r->rtsp_path = g_strdup(DEFAULT_RTSP_PATH);

		r->mounts = gst_rtsp_server_get_mount_points (r->server);
		gst_rtsp_mount_points_add_factory (r->mounts, r->rtsp_path, g_object_ref(r->es_factory));
		gst_rtsp_mount_points_add_factory (r->mounts, "/ts", g_object_ref(r->ts_factory));
		r->state = RTSP_STATE_IDLE;
		r->source_id = gst_rtsp_server_attach (r->server, NULL);
		g_print ("dreambox encoder stream ready at rtsp://%s127.0.0.1:%s%s\n", credentials, app->rtsp_server->rtsp_port, app->rtsp_server->rtsp_path);
		return TRUE;
	}
	else
		GST_INFO_OBJECT (app, "rtsp server already enabled!");
	g_mutex_unlock (&app->rtsp_mutex);
	return FALSE;

fail:
	disable_rtsp_server(app);
	return FALSE;
}

gboolean start_rtsp_pipeline(App* app)
{
	GST_DEBUG_OBJECT (app, "start_rtsp_pipeline");

	DreamRTSPserver *r = app->rtsp_server;
	if (r->state == RTSP_STATE_DISABLED)
	{
		GST_ERROR_OBJECT (app, "failed to start rtsp pipeline because rtsp server is not enabled!");
		return FALSE;
	}

	assert_tsmux (app);

	GstStateChangeReturn sret;
	sret = gst_element_set_state (app->pipeline, GST_STATE_PLAYING);
	if (sret == GST_STATE_CHANGE_FAILURE)
	{
		GST_ERROR_OBJECT (app, "GST_STATE_CHANGE_FAILURE for rtsp pipeline");
		return FALSE;
	}
	GST_INFO_OBJECT(app, "started rtsp pipeline, pipeline going into PLAYING");
	return TRUE;
}

static GstPadProbeReturn tsmux_pad_probe_unlink_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;

	GstElement *element = gst_pad_get_parent_element(pad);
	GST_LOG_OBJECT (pad, "tsmux_pad_probe_unlink_cb % "GST_PTR_FORMAT, element);

	if (element && (element == app->aq || element == app->vq))
	{
		GstPad *srcpad, *muxpad;
		srcpad = gst_element_get_static_pad (element, "src");
		muxpad = gst_pad_get_peer (srcpad);
		gst_pad_unlink (srcpad, muxpad);
		gst_element_release_request_pad (app->tsmux, muxpad);
		gst_object_unref (srcpad);
		gst_object_unref (muxpad);

		gst_element_set_state (element, GST_STATE_READY);

		GstState astate, vstate;
		gst_element_get_state (GST_ELEMENT(app->aq), &astate, NULL, 1*GST_USECOND);
		gst_element_get_state (GST_ELEMENT(app->vq), &vstate, NULL, 1*GST_USECOND);

		if (astate == GST_STATE_READY && vstate == GST_STATE_READY)
		{
			gst_object_ref (app->tsmux);
			GstPad *sinkpad;
			sinkpad = gst_element_get_static_pad (app->tsmux, "src");
			gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, tsmux_pad_probe_unlink_cb, app, NULL);
			gst_object_unref (sinkpad);
		}
	}

	else if (element && element == app->tsmux)
	{
		gst_element_unlink (app->tsmux, app->tstee);
		gst_bin_remove (GST_BIN (app->pipeline), app->tsmux);
		gst_element_set_state (app->tsmux, GST_STATE_NULL);
		gst_object_unref (app->tsmux);
		app->tsmux = NULL;
	}
	return GST_PAD_PROBE_REMOVE;
}

gboolean halt_source_pipeline(App* app)
{
	GST_INFO_OBJECT(app, "halt_source_pipeline... setting sources to GST_STATE_READY");

	if (gst_element_set_state (app->asrc, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS && gst_element_set_state (app->vsrc, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS)
	{
		GstPad *sinkpad;
		gst_object_ref (app->aq);
		sinkpad = gst_element_get_static_pad (app->aq, "sink");
		gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, tsmux_pad_probe_unlink_cb, app, NULL);
		gst_object_unref (sinkpad);

		gst_object_ref (app->vq);
		sinkpad = gst_element_get_static_pad (app->vq, "sink");
		gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, tsmux_pad_probe_unlink_cb, app, NULL);
		gst_object_unref (sinkpad);
		return TRUE;
	}
	GST_WARNING("can't set sources to GST_STATE_READY!");
	return FALSE;
}

gboolean pause_source_pipeline(App* app)
{
	GST_INFO_OBJECT(app, "pause_source_pipeline... setting sources to GST_STATE_PAUSED");

	if (gst_element_set_state (app->asrc, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL && gst_element_set_state (app->vsrc, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL)
		return TRUE;
	GST_WARNING("can't set sources to GST_STATE_PAUSED!");
	return FALSE;
}

gboolean unpause_source_pipeline(App* app)
{
	GST_INFO_OBJECT(app, "unpause_source_pipeline... setting sources to GST_STATE_PLAYING");

	if (gst_element_set_state (app->asrc, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE || gst_element_set_state (app->vsrc, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
	{
		GST_WARNING("can't set sources to GST_STATE_PAUSED!");
		return FALSE;
	}
	return TRUE;
}

GstRTSPFilterResult remove_media_filter_func (GstRTSPSession * sess, GstRTSPSessionMedia * session_media, gpointer user_data)
{
	App *app = user_data;
	GstRTSPFilterResult res = GST_RTSP_FILTER_REF;
	GstRTSPMedia *media;
	media = gst_rtsp_session_media_get_media (session_media);
	g_mutex_lock (&app->rtsp_mutex);
	if (media == app->rtsp_server->es_media) {
		GST_DEBUG_OBJECT (app, "matching RTSP media %p in filter, removing...", media);
		res = GST_RTSP_FILTER_REMOVE;
	}
	g_mutex_unlock (&app->rtsp_mutex);
	return res;
}

GstRTSPFilterResult remove_session_filter_func (GstRTSPClient *client, GstRTSPSession * sess, gpointer user_data)
{
	App *app = user_data;
	GList *media_filter_res;
	GstRTSPFilterResult res = GST_RTSP_FILTER_REF;
	media_filter_res = gst_rtsp_session_filter (sess, remove_media_filter_func, app);
	if (g_list_length (media_filter_res) == 0) {
		GST_DEBUG_OBJECT (app, "no more media for session %p, removing...", sess);
		res = GST_RTSP_FILTER_REMOVE;
	}
	g_list_free (media_filter_res);
	return res;
}

GstRTSPFilterResult remove_client_filter_func (GstRTSPServer *server, GstRTSPClient *client, gpointer user_data)
{
	App *app = user_data;
	GList *session_filter_res;
	GstRTSPFilterResult res = GST_RTSP_FILTER_KEEP;
	int ret = g_signal_handlers_disconnect_by_func(client, (GCallback) client_closed, app);
	GST_INFO("client_filter_func %" GST_PTR_FORMAT "  (number of clients: %i). disconnected %i callback handlers", client, g_list_length(app->rtsp_server->clients_list), ret);
	session_filter_res = gst_rtsp_client_session_filter (client, remove_session_filter_func, app);
	if (g_list_length (session_filter_res) == 0) {
		GST_DEBUG_OBJECT (app, "no more sessions for client %p, removing...", app);
		res = GST_RTSP_FILTER_REMOVE;
	}
	g_list_free (session_filter_res);
	return res;
}


static GstPadProbeReturn rtsp_pad_probe_unlink_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	DreamRTSPserver *r = app->rtsp_server;

	GstElement *element = gst_pad_get_parent_element(pad);
	GstElement *appsink;
	if (element == r->vrtspq)
		appsink = r->vappsink;
	else if (element == r->artspq)
		appsink = r->aappsink;
	else if (element == r->tsrtspq)
		appsink = r->tsappsink;

	GST_LOG_OBJECT (pad, "unlink... %" GST_PTR_FORMAT " and % "GST_PTR_FORMAT, element, appsink);

	GstPad *teepad;
	teepad = gst_pad_get_peer(pad);
	gst_pad_unlink (teepad, pad);

	GstElement *tee = gst_pad_get_parent_element(teepad);
	gst_element_release_request_pad (tee, teepad);
	gst_object_unref (teepad);
	gst_object_unref (tee);

	gst_element_unlink (element, appsink);

	GST_LOG_OBJECT (pad, "remove... %" GST_PTR_FORMAT " and % "GST_PTR_FORMAT, element, appsink);

	gst_bin_remove_many (GST_BIN (app->pipeline), element, appsink, NULL);

	GST_LOG_OBJECT (pad, "set state null %" GST_PTR_FORMAT " and % "GST_PTR_FORMAT, element, appsink);

	gst_element_set_state (appsink, GST_STATE_NULL);
	gst_element_set_state (element, GST_STATE_NULL);

	GST_LOG_OBJECT (pad, "unref.... %" GST_PTR_FORMAT " and % "GST_PTR_FORMAT, element, appsink);

	gst_object_unref (element);
	gst_object_unref (appsink);
	element = NULL;
	appsink = NULL;

	if (!r->tsappsink && !r->aappsink && !r->vappsink)
	{
		GST_INFO("!r->tsappsink && !r->aappsink && !r->vappsink");
		if (app->tcp_upstream->state == UPSTREAM_STATE_DISABLED)
			halt_source_pipeline(app);
		GST_INFO("local rtsp server disabled!");
	}
	return GST_PAD_PROBE_REMOVE;
}

gboolean disable_rtsp_server(App *app)
{
	DreamRTSPserver *r = app->rtsp_server;
	GST_DEBUG("disable_rtsp_server %p", r->server);
	if (r->state >= RTSP_STATE_IDLE)
	{
		if (app->rtsp_server->es_media)
			gst_rtsp_server_client_filter(app->rtsp_server->server, (GstRTSPServerClientFilterFunc) remove_client_filter_func, app);
		g_mutex_lock (&app->rtsp_mutex);
		gst_rtsp_mount_points_remove_factory (app->rtsp_server->mounts, app->rtsp_server->rtsp_path);
		GSource *source = g_main_context_find_source_by_id (g_main_context_default (), r->source_id);
		g_source_destroy(source);
// 		g_source_unref(source);
// 		GST_DEBUG("disable_rtsp_server source unreffed");
		if (r->mounts)
			g_object_unref(r->mounts);
		if (r->server)
			gst_object_unref(r->server);
		g_free(r->rtsp_user);
		g_free(r->rtsp_pass);
		g_free(r->rtsp_port);
		g_free(r->rtsp_path);
		r->state = RTSP_STATE_DISABLED;

		gst_object_ref (r->tsrtspq);
		gst_object_ref (r->artspq);
		gst_object_ref (r->vrtspq);
		gst_object_ref (r->tsappsink);
		gst_object_ref (r->aappsink);
		gst_object_ref (r->vappsink);

		GstPad *sinkpad;
		sinkpad = gst_element_get_static_pad (r->tsrtspq, "sink");
		gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, rtsp_pad_probe_unlink_cb, app, NULL);
		gst_object_unref (sinkpad);
		sinkpad = gst_element_get_static_pad (r->artspq, "sink");
		gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, rtsp_pad_probe_unlink_cb, app, NULL);
		gst_object_unref (sinkpad);
		sinkpad = gst_element_get_static_pad (r->vrtspq, "sink");
		gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, rtsp_pad_probe_unlink_cb, app, NULL);
		gst_object_unref (sinkpad);

		g_mutex_unlock (&app->rtsp_mutex);
		GST_INFO("rtsp_server disabled!");
		return TRUE;
	}
	return FALSE;
}

static GstPadProbeReturn upstream_pad_probe_unlink_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	DreamTCPupstream *t = app->tcp_upstream;

	GstElement *element = gst_pad_get_parent_element(pad);
	GST_LOG_OBJECT (pad, "upstream_pad_probe_unlink_cb %" GST_PTR_FORMAT, element);

	if (element == t->tstcpq)
	{
		GstPad *teepad;
		teepad = gst_pad_get_peer(pad);
		gst_pad_unlink (teepad, pad);

		GstElement *tee = gst_pad_get_parent_element(teepad);
		gst_element_release_request_pad (tee, teepad);
		gst_object_unref (teepad);
		gst_object_unref (tee);

		gst_object_ref (t->tcpsink);
		gst_element_unlink (t->tstcpq, t->tcpsink);
		gst_bin_remove_many (GST_BIN (app->pipeline), t->tstcpq, t->tcpsink, NULL);

		gst_element_set_state (t->tcpsink, GST_STATE_NULL);
		gst_element_set_state (t->tstcpq, GST_STATE_NULL);

		gst_object_unref (t->tstcpq);
		gst_object_unref (t->tcpsink);
		t->tstcpq = NULL;
		t->tcpsink = NULL;

		if (app->rtsp_server->state == RTSP_STATE_DISABLED)
			halt_source_pipeline(app);
		GST_INFO("tcp_upstream disabled!");
		t->state = UPSTREAM_STATE_DISABLED;
		send_signal (app, "upstreamStateChanged", g_variant_new("(i)", t->state));
	}
	return GST_PAD_PROBE_REMOVE;
}

gboolean disable_tcp_upstream(App *app)
{
	GST_DEBUG("disable_tcp_upstream");
	DreamTCPupstream *t = app->tcp_upstream;
	if (t->state >= UPSTREAM_STATE_CONNECTING)
	{
		gst_object_ref (t->tstcpq);
		GstPad *sinkpad;
		sinkpad = gst_element_get_static_pad (t->tstcpq, "sink");
		gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, upstream_pad_probe_unlink_cb, app, NULL);
		gst_object_unref (sinkpad);
		return TRUE;
	}
	return FALSE;
}

gboolean destroy_pipeline(App *app)
{
	GST_DEBUG_OBJECT(app, "destroy_pipeline @%p", app->pipeline);
	if (app->pipeline)
	{
		GstStateChangeReturn sret = gst_element_set_state (app->pipeline, GST_STATE_NULL);
		if (sret == GST_STATE_CHANGE_ASYNC)
		{
			GstState state;
			gst_element_get_state (GST_ELEMENT(app->pipeline), &state, NULL, 3*GST_SECOND);
			if (state != GST_STATE_NULL)
				GST_INFO_OBJECT(app, "%" GST_PTR_FORMAT"'s state=%s", app->pipeline, gst_element_state_get_name (state));
		}
		gst_object_unref (app->pipeline);
		gst_object_unref (app->clock);
		GST_INFO_OBJECT(app, "source pipeline destroyed");
		app->pipeline = NULL;
		return TRUE;
	}
	else
		GST_INFO_OBJECT(app, "don't destroy inexistant pipeline");
	return FALSE;
}

gboolean quit_signal(gpointer loop)
{
	GST_INFO_OBJECT(loop, "caught SIGINT");
	g_main_loop_quit((GMainLoop*)loop);
	return FALSE;
}

int main (int argc, char *argv[])
{
	App app;
	guint owner_id;

	gst_init (0, NULL);

	GST_DEBUG_CATEGORY_INIT (dreamrtspserver_debug, "dreamrtspserver",
			GST_DEBUG_BOLD | GST_DEBUG_FG_YELLOW | GST_DEBUG_BG_BLUE,
			"Dreambox RTSP server daemon");

	memset (&app, 0, sizeof(app));
	g_mutex_init (&app.rtsp_mutex);

	introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
	app.dbus_connection = NULL;

	owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
				   service,
			    G_BUS_NAME_OWNER_FLAGS_NONE,
			    on_bus_acquired,
			    on_name_acquired,
			    on_name_lost,
			    &app,
			    NULL);

	if (!create_source_pipeline(&app))
		g_print ("Failed to create source pipeline!");

	app.tcp_upstream = malloc(sizeof(DreamTCPupstream));
	app.tcp_upstream->state = UPSTREAM_STATE_DISABLED;

	app.rtsp_server = create_rtsp_server(&app);

	app.loop = g_main_loop_new (NULL, FALSE);
	g_unix_signal_add(SIGINT, quit_signal, app.loop);

	g_main_loop_run (app.loop);

	free(app.tcp_upstream);
	if (app.rtsp_server->state >= RTSP_STATE_IDLE)
		disable_rtsp_server(&app);
	if (app.rtsp_server->clients_list)
		g_list_free (app.rtsp_server->clients_list);
	free(app.rtsp_server);

	destroy_pipeline(&app);

	g_main_loop_unref (app.loop);

	g_mutex_clear (&app.rtsp_mutex);

	g_bus_unown_name (owner_id);
	g_dbus_node_info_unref (introspection_data);

	return 0;
}
