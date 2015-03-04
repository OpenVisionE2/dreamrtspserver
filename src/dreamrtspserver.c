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

typedef struct {
	gboolean enabled;
	GstElement *afilter, *vfilter;
	GstElement *payloader;
	GstElement *tsmux;
	GstElement *tcpsink;
	GstElement *upstreambin;
	gulong inject_id;
	char token[TOKEN_LEN];
} DreamTCPupstream;

typedef struct {
	gboolean enabled;
	GstRTSPServer *server;
	GstRTSPMountPoints *mounts;
	GstRTSPMediaFactory *factory;
	GstRTSPMedia * media;
	GstElement *aappsrc, *vappsrc;
	GstClockTime rtsp_start_pts, rtsp_start_dts;
	gchar *rtsp_user, *rtsp_pass;
	GList *clients_list;
	gchar *rtsp_port;
	gchar *rtsp_path;
	guint source_id;
} DreamRTSPserver;

typedef struct {
	GMainLoop *loop;

	GstElement *pipeline, *aappsink, *vappsink;
	GstElement *asrc, *vsrc, *aq, *vq, *aparse, *vparse;
	GstElement *atee, *vtee;

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
  "    <property type='b' name='state' access='read'/>"
  "    <property type='i' name='clients' access='read'/>"
  "    <property type='i' name='audioBitrate' access='readwrite'/>"
  "    <property type='i' name='videoBitrate' access='readwrite'/>"
  "    <property type='i' name='framerate' access='readwrite'/>"
  "    <property type='i' name='width' access='read'/>"
  "    <property type='i' name='height' access='read'/>"
  "    <property type='s' name='path' access='read'/>"
  "  </interface>"
  "</node>";

gboolean create_source_pipeline(App *app);
gboolean pause_source_pipeline(App *app);
DreamRTSPserver *create_rtsp_server(App *app);
gboolean enable_rtsp_server(App *app, const gchar *path, guint32 port, const gchar *user, const gchar *pass);
gboolean disable_rtsp_server(App *app);
gboolean start_rtsp_pipeline(App *app);
gboolean stop_rtsp_pipeline(App *app);
gboolean enable_tcp_upstream(App *app, const gchar *upstream_host, guint32 upstream_port, const gchar *token);
gboolean disable_tcp_upstream(App *app);
gboolean destroy_pipeline(App *app);

static gboolean gst_set_framerate(App *app, int value)
{
	GstCaps *caps = NULL;
	GstStructure *structure;
	gboolean ret = FALSE;

	if (!app->pipeline)
		goto out;

	g_object_get (G_OBJECT (app->vsrc), "caps", &caps, NULL);

	if (!GST_IS_CAPS(caps))
		goto out;

	GST_INFO("gst_set_framerate %d old caps %" GST_PTR_FORMAT, value, caps);

	structure = gst_caps_steal_structure (caps, 0);
	if (!structure)
		goto out;

	if (value)
		gst_structure_set (structure, "framerate", GST_TYPE_FRACTION, value, 1, NULL);

	gst_caps_append_structure (caps, structure);
	GST_INFO("new caps %" GST_PTR_FORMAT, caps);
	g_object_set (G_OBJECT (app->vsrc), "caps", caps, NULL);
	ret = TRUE;

out:
	if (caps)
		gst_caps_unref(caps);
	return ret;
}

static gboolean gst_set_resolution(App *app, int width, int height)
{
	GstCaps *caps = NULL;
	GstStructure *structure;
	gboolean ret = FALSE;

	if (!app->pipeline)
		goto out;

	g_object_get (G_OBJECT (app->vsrc), "caps", &caps, NULL);

	if (!GST_IS_CAPS(caps))
		goto out;

	GST_INFO("old caps %" GST_PTR_FORMAT, caps);

	structure = gst_caps_steal_structure (caps, 0);
	if (!structure)
		goto out;

	if (width && height)
	{
		gst_structure_set (structure, "width", G_TYPE_INT, width, NULL);
		gst_structure_set (structure, "height", G_TYPE_INT, height, NULL);
	}
	gst_caps_append_structure (caps, structure);
	GST_INFO("new caps %" GST_PTR_FORMAT, caps);
	g_object_set (G_OBJECT (app->vsrc), "caps", caps, NULL);
	ret = TRUE;

out:
	if (caps)
		gst_caps_unref(caps);
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

	GST_INFO("current caps %" GST_PTR_FORMAT, caps);

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
		if (!gst_structure_get_int (structure, prop_name, value))
			*value = 0;
	}
	else
		goto out;

	GST_INFO("%s.%s = %i", element_name, prop_name, *value);
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

	if (g_strcmp0 (property_name, "state") == 0)
	{
		GstState state;
		return g_variant_new_boolean ( !!app->pipeline  );
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

	if (g_strcmp0 (property_name, "audioBitrate") == 0)
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

			g_variant_get (parameters, "(bsuss)", &state, &path, &port, &user, &pass);
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

			g_variant_get (parameters, "(bsus)", &state, &upstream_host, &upstream_port, &token);
			GST_DEBUG("app->pipeline=%p, enableUpstream state=%i host=%s port=%i token=%s", app->pipeline, state, upstream_host, upstream_port, token);

			if (state == TRUE && !app->tcp_upstream->enabled)
				result = enable_tcp_upstream(app, upstream_host, upstream_port, token);
			else if (state == FALSE && app->tcp_upstream->enabled)
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

	guint registration_id;
	GError *error = NULL;

	GST_DEBUG ("aquired dbus (\"%s\" @ %p)", name, connection);

	registration_id =
	g_dbus_connection_register_object (connection,
					   object_name,
				    introspection_data->interfaces[0],
				    &interface_vtable,
				    user_data,    // Optional user data
				    NULL,    // Func. for freeing user data
				    &error);
} // on_bus_acquired

static void on_name_acquired (GDBusConnection *connection,
			      const gchar     *name,
			      gpointer         user_data)
{
	GST_DEBUG ("aquired dbus name (\"%s\")", name);
} // on_name_acquired

static void on_name_lost (GDBusConnection *connection,
			  const gchar     *name,
			  gpointer         user_data)
{
	App *app = user_data;

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
				GST_INFO_OBJECT(app, "state transition %s -> %s", gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));

				GstStateChange transition = (GstStateChange)GST_STATE_TRANSITION(old_state, new_state);

				switch(transition)
				{
					case GST_STATE_CHANGE_READY_TO_PAUSED:
					{
						if (GST_MESSAGE_SRC (message) == GST_OBJECT (app->pipeline))
						{
						}
					}	break;
					default:
					{
					}	break;
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
	GST_INFO("no more clients -> media unprepared!");
	stop_rtsp_pipeline(app);
	app->rtsp_server->media = NULL;
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
	g_mutex_lock (&app->rtsp_mutex);
	app->rtsp_server->media = media;
	GstElement *element = gst_rtsp_media_get_element (media);
	app->rtsp_server->aappsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), "aappsrc");
	app->rtsp_server->vappsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), "vappsrc");
	gst_object_unref(element);
	g_signal_connect (media, "unprepared", (GCallback) media_unprepare, app);
	g_object_set (app->rtsp_server->aappsrc, "format", GST_FORMAT_TIME, NULL);
	g_object_set (app->rtsp_server->vappsrc, "format", GST_FORMAT_TIME, NULL);
	app->rtsp_server->rtsp_start_pts = app->rtsp_server->rtsp_start_dts = GST_CLOCK_TIME_NONE;
	start_rtsp_pipeline(app);
	g_mutex_unlock (&app->rtsp_mutex);
}

static GstFlowReturn handover_payload (GstElement * appsink, gpointer user_data)
{
	App *app = user_data;
	DreamRTSPserver *r = app->rtsp_server;

	GstAppSrc* appsrc = NULL;
	if ( appsink == app->vappsink )
		appsrc = GST_APP_SRC(r->vappsrc);
	else if ( appsink == app->aappsink )
		appsrc = GST_APP_SRC(r->aappsrc);

	g_mutex_lock (&app->rtsp_mutex);
	if (appsrc && g_list_length(r->clients_list) > 0) {
		GstSample *sample = gst_app_sink_pull_sample (GST_APP_SINK (appsink));
		GstBuffer *buffer = gst_sample_get_buffer (sample);
		GstCaps *caps = gst_sample_get_caps (sample);

		GST_LOG("original PTS %" GST_TIME_FORMAT " DTS %" GST_TIME_FORMAT " @ %"GST_PTR_FORMAT"", GST_TIME_ARGS (GST_BUFFER_PTS (buffer)), GST_TIME_ARGS (GST_BUFFER_DTS (buffer)), appsrc);
		if (r->rtsp_start_pts == GST_CLOCK_TIME_NONE) {
			if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))
			{
				GST_LOG("GST_BUFFER_FLAG_DELTA_UNIT dropping!");
				gst_sample_unref(sample);
				g_mutex_unlock (&app->rtsp_mutex);
				return GST_FLOW_OK;
			}
			else if (appsink == app->vappsink)
			{
				r->rtsp_start_pts = GST_BUFFER_PTS (buffer);
				r->rtsp_start_dts = GST_BUFFER_DTS (buffer);
				GST_INFO("frame is IFRAME! set rtsp_start_pts=%" GST_TIME_FORMAT " rtsp_start_dts=%" GST_TIME_FORMAT " @ %"GST_PTR_FORMAT"", GST_TIME_ARGS (GST_BUFFER_PTS (buffer)), GST_TIME_ARGS (GST_BUFFER_DTS (buffer)), appsrc);
			}
		}
		if (GST_BUFFER_PTS (buffer) < r->rtsp_start_pts)
			GST_BUFFER_PTS (buffer) = 0;
		else
			GST_BUFFER_PTS (buffer) -= r->rtsp_start_pts;
		GST_BUFFER_DTS (buffer) -= r->rtsp_start_dts;
		//    GST_LOG("new PTS %" GST_TIME_FORMAT " DTS %" GST_TIME_FORMAT "", GST_TIME_ARGS (GST_BUFFER_PTS (buffer)), GST_TIME_ARGS (GST_BUFFER_DTS (buffer)));

		GstCaps *oldcaps = gst_app_src_get_caps (appsrc);
		if (!oldcaps || !gst_caps_is_equal (oldcaps, caps))
		{
			GST_LOG("CAPS changed! %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, oldcaps, caps);
			gst_app_src_set_caps (appsrc, caps);
		}
		gst_app_src_push_buffer (appsrc, gst_buffer_ref(buffer));
		gst_sample_unref (sample);
	}
	else
	{
		if ( gst_debug_category_get_threshold (dreamrtspserver_debug) >= GST_LEVEL_DEBUG)
			GST_LOG("no rtsp clients, discard payload!");
		else
			g_print (".");
	}
	g_mutex_unlock (&app->rtsp_mutex);

	return GST_FLOW_OK;
}

gboolean create_source_pipeline(App *app)
{
	GST_INFO_OBJECT(app, "create_source_pipeline");
	g_mutex_lock (&app->rtsp_mutex);
	app->pipeline = gst_pipeline_new (NULL);
	app->asrc = gst_element_factory_make ("dreamaudiosource", "dreamaudiosource0");
	app->vsrc = gst_element_factory_make ("dreamvideosource", "dreamvideosource0");

	app->aparse = gst_element_factory_make ("aacparse", NULL);
	app->vparse = gst_element_factory_make ("h264parse", NULL);

	app->aq = gst_element_factory_make ("queue", NULL);
	app->vq = gst_element_factory_make ("queue", NULL);

	app->atee = gst_element_factory_make ("tee", NULL);
	app->vtee = gst_element_factory_make ("tee", NULL);

	if (!(app->asrc && app->vsrc && app->aparse && app->vparse && app->aq && app->vq && app->atee && app->vtee))
	{
		g_error ("Failed to create source pipeline element(s):%s%s%s%s%s%s%s%s", app->asrc?"":" dreamaudiosource", app->vsrc?"":" dreamvideosource", app->aparse?"":" aacparse",
			app->vparse?"":" h264parse", app->aq?"":" aqueue", app->vq?"":" vqueue", app->atee?"":" atee", app->vtee?"":" vtee");
	}

	app->aappsink = gst_element_factory_make ("appsink", AAPPSINK);
	app->vappsink = gst_element_factory_make ("appsink", VAPPSINK);

	GstElement *appsrc, *vpay, *apay, *udpsrc;
	appsrc = gst_element_factory_make ("appsrc", NULL);
	vpay = gst_element_factory_make ("rtph264pay", NULL);
	apay = gst_element_factory_make ("rtpmp4apay", NULL);
	udpsrc = gst_element_factory_make ("udpsrc", NULL);

	if (!(app->aappsink && app->vappsink && appsrc && vpay && apay && udpsrc))
		g_error ("Failed to create rtsp element(s):%s%s%s%s%s%s", app->aappsink?"":" aappsink", app->vappsink?"":" vappsink", appsrc?"":" appsrc", vpay?"": "rtph264pay", apay?"":" rtpmp4apay", udpsrc?"":" udpsrc" );
	else
	{
		gst_object_unref (appsrc);
		gst_object_unref (vpay);
		gst_object_unref (apay);
		gst_object_unref (udpsrc);
	}

	gst_bin_add_many (GST_BIN (app->pipeline), app->asrc, app->vsrc, app->aparse, app->vparse, app->aq, app->vq, app->atee, app->vtee, NULL);
	gst_element_link_many (app->asrc, app->aparse, app->aq, app->atee, NULL);
	gst_element_link_many (app->vsrc, app->vparse, app->vq, app->vtee, NULL);

	g_object_set (G_OBJECT (app->aq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);
	g_object_set (G_OBJECT (app->vq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);

// 	g_object_set (G_OBJECT (app->aappsink), "emit-signals", TRUE, NULL);
	g_object_set (G_OBJECT (app->aappsink), "emit-signals", FALSE, NULL);
	g_object_set (G_OBJECT (app->aappsink), "enable-last-sample", FALSE, NULL);
// 	g_object_set (G_OBJECT (app->aappsink), "sync", FALSE, NULL);
	g_signal_connect (app->aappsink, "new-sample", G_CALLBACK (handover_payload), app);

// 	g_object_set (G_OBJECT (app->vappsink), "emit-signals", TRUE, NULL);
	g_object_set (G_OBJECT (app->vappsink), "emit-signals", FALSE, NULL);
	g_object_set (G_OBJECT (app->vappsink), "enable-last-sample", FALSE, NULL);
// 	g_object_set (G_OBJECT (app->vappsink), "sync", FALSE, NULL);
	g_signal_connect (app->vappsink, "new-sample", G_CALLBACK (handover_payload), app);

	gst_bin_add_many (GST_BIN (app->pipeline), app->aappsink, app->vappsink, NULL);
	GstPad *teepad, *sinkpad;
	GstPadLinkReturn ret;
	teepad = gst_element_get_request_pad (app->atee, "src_%u");
	sinkpad = gst_element_get_static_pad (app->aappsink, "sink");
	ret = gst_pad_link (teepad, sinkpad);
	if (ret != GST_PAD_LINK_OK)
	{
		GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", teepad, sinkpad);
		return FALSE;
	}
	gst_object_unref (teepad);
	gst_object_unref (sinkpad);
	teepad = gst_element_get_request_pad (app->vtee, "src_%u");
	sinkpad = gst_element_get_static_pad (app->vappsink, "sink");
	ret = gst_pad_link (teepad, sinkpad);
	if (ret != GST_PAD_LINK_OK)
	{
		GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", teepad, sinkpad);
		return FALSE;
	}
	gst_object_unref (teepad);
	gst_object_unref (sinkpad);

	GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (app->pipeline));
	gst_bus_add_signal_watch (bus);
	g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (message_cb), app);
	gst_object_unref (GST_OBJECT (bus));

	app->clock = gst_system_clock_obtain();
	gst_pipeline_use_clock(GST_PIPELINE (app->pipeline), app->clock);

	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"create_source_pipeline");
	g_mutex_unlock (&app->rtsp_mutex);
	return gst_element_set_state (app->pipeline, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS;
}

static GstPadProbeReturn inject_authorization (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	GError *err = NULL;
	GstBuffer *token_buf = gst_buffer_new_wrapped (app->tcp_upstream->token, TOKEN_LEN);
	GstPad * srcpad = gst_element_get_static_pad (app->tcp_upstream->payloader, "src");
	
	GST_INFO ("injecting authorization on pad %s:%s, created token_buf %" GST_PTR_FORMAT "", GST_DEBUG_PAD_NAME (sinkpad), token_buf);
	gst_pad_remove_probe (sinkpad, app->tcp_upstream->inject_id);
	
	GstFlowReturn pret = gst_pad_push (srcpad, gst_buffer_ref(token_buf));
	
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

	if (!t->enabled)
	{
		t->afilter = gst_element_factory_make ("capsfilter", "afilter");
		t->vfilter = gst_element_factory_make ("capsfilter", "vfilter");
		t->tsmux = gst_element_factory_make ("mpegtsmux", NULL);
		t->payloader = gst_element_factory_make ("gdppay", NULL);
		t->tcpsink = gst_element_factory_make ("tcpclientsink", NULL);

		if (!(t->payloader && t->afilter && t->vfilter && t->tsmux && t->tcpsink ))
		g_error ("Failed to create tcp upstream element(s):%s%s%s%s%s", t->payloader?"":"  gdppay", t->afilter?"":"  audio filter", t->vfilter?"":"  video filter ", t->tsmux?"":"  mpegtsmux", t->tcpsink?"":"  tcpclientsink" );

		g_object_set (t->tcpsink, "host", upstream_host, NULL);
		g_object_set (t->tcpsink, "port", upstream_port, NULL);
		gchar *check_host;
		guint32 check_port;
		g_object_get (t->tcpsink, "host", &check_host, NULL);
		g_object_get (t->tcpsink, "port", &check_port, NULL);
		if (g_strcmp0 (upstream_host, check_host))
		{
			GST_ERROR_OBJECT (app, "couldn't set upstream_host %s", upstream_host);
			goto fail;
		}
		if (upstream_port != check_port)
		{
			GST_ERROR_OBJECT (app, "couldn't set upstream_port %d", upstream_port);
			goto fail;
		}

		gchar *capsstr;

		gst_bin_add_many (GST_BIN(app->pipeline), t->afilter, t->vfilter, t->tsmux, t->payloader, t->tcpsink, NULL);

		GstPadLinkReturn ret;
		GstPad *srcpad, *sinkpad;
		srcpad = gst_element_get_request_pad (app->atee, "src_%u");
// 		sinkpad = gst_element_get_static_pad (t->upstreambin, "asink");
		sinkpad = gst_element_get_static_pad (t->afilter, "sink");
		ret = gst_pad_link (srcpad, sinkpad);
		gst_object_unref (srcpad);
		gst_object_unref (sinkpad);
		if (ret != GST_PAD_LINK_OK)
		{
			GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", srcpad, sinkpad);
			goto fail;
		}
		srcpad = gst_element_get_request_pad (app->vtee, "src_%u");
// 		sinkpad = gst_element_get_static_pad (t->upstreambin, "vsink");
		sinkpad = gst_element_get_static_pad (t->vfilter, "sink");
		ret = gst_pad_link (srcpad, sinkpad);
		if (ret != GST_PAD_LINK_OK)
		{
			GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", srcpad, sinkpad);
			goto fail;
		}
		gst_object_unref (srcpad);
		gst_object_unref (sinkpad);

		GstCaps *fltcaps;
		capsstr = g_strdup_printf ("audio/mpeg"/*, token=(string)%s", token*/);
		fltcaps = gst_caps_from_string (capsstr);
		g_object_set (t->afilter, "caps", fltcaps, NULL);
		g_free(capsstr);

		srcpad = gst_element_get_static_pad (t->afilter, "src");
		sinkpad = gst_element_get_compatible_pad (t->tsmux, srcpad, fltcaps);
		ret = gst_pad_link (srcpad, sinkpad);
		gst_caps_unref(fltcaps);
		if (ret != GST_PAD_LINK_OK)
		{
			GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", srcpad, sinkpad);
			goto fail;
		}

		gst_object_unref (srcpad);
		gst_object_unref (sinkpad);

		capsstr = g_strdup_printf ("video/x-h264"/*, token=(string)%s", token*/);
		fltcaps = gst_caps_from_string (capsstr);
		g_object_set (t->vfilter, "caps", fltcaps, NULL);
		g_free(capsstr);

		srcpad = gst_element_get_static_pad (t->vfilter, "src");
		sinkpad = gst_element_get_compatible_pad (t->tsmux, srcpad, fltcaps);
		ret = gst_pad_link (srcpad, sinkpad);
		gst_caps_unref(fltcaps);
		if (ret != GST_PAD_LINK_OK)
		{
			GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", srcpad, sinkpad);
			goto fail;
		}

		gst_object_unref (srcpad);
		gst_object_unref (sinkpad);

		if (!gst_element_link_many (t->tsmux, t->payloader, t->tcpsink, NULL)) {
			g_error ("Failed to link tsmux to tcpclientsink");
		}

		if (strlen(token))
		{
			sinkpad = gst_element_get_static_pad (t->tcpsink, "sink");
			strcpy(t->token, token);
			t->inject_id = gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) inject_authorization, app, NULL);
			gst_object_unref (sinkpad);
		}

		GstStateChangeReturn sret = gst_element_set_state (t->tcpsink, GST_STATE_READY);
		if (sret == GST_STATE_CHANGE_FAILURE)
		{
			GST_ERROR_OBJECT (app, "failed to set tcpsink to GST_STATE_READY. %s:%d probably refused connection", upstream_host, upstream_port);
			goto fail;
		}

		sret = gst_element_set_state (app->pipeline, GST_STATE_PLAYING);
		GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"enable_tcp_upstream");
		if (sret == GST_STATE_CHANGE_FAILURE)
		{
			GST_ERROR_OBJECT (app, "GST_STATE_CHANGE_FAILURE for upstream pipeline");
			goto fail;
		}
		else if (sret == GST_STATE_CHANGE_ASYNC)
		{
			GstState state;
			gst_element_get_state (GST_ELEMENT(app->pipeline), &state, NULL, 3*GST_SECOND);

			GValue item = G_VALUE_INIT;
				GstIterator* iter = gst_bin_iterate_elements(GST_BIN(app->pipeline));
				while (GST_ITERATOR_OK == gst_iterator_next(iter, (GValue*)&item))
				{
					GstElement *elem = g_value_get_object(&item);
					gst_element_get_state (elem, &state, NULL, GST_USECOND);
					if ( state != GST_STATE_PLAYING)
						GST_INFO_OBJECT(app, "%" GST_PTR_FORMAT"'s state=%s", elem, gst_element_state_get_name (state));
				}
				gst_iterator_free(iter);
			if (state != GST_STATE_PLAYING)
			{
				GST_ERROR_OBJECT (app, "state != GST_STATE_PLAYING");
				goto fail;
			}
		}
		GST_INFO_OBJECT(app, "enabled tcp upstream, pipeline is PLAYING");
		t->enabled = TRUE;
		return TRUE;
	}
	return FALSE;

fail:
	disable_tcp_upstream(app);
	return FALSE;
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

	if (!r->enabled)
	{
		r->server = gst_rtsp_server_new ();
		g_signal_connect (r->server, "client-connected", (GCallback) client_connected, app);

		r->factory = gst_rtsp_media_factory_new ();
		gst_rtsp_media_factory_set_launch (r->factory, "( appsrc name=vappsrc ! h264parse ! rtph264pay name=pay0 pt=96   appsrc name=aappsrc ! aacparse ! rtpmp4apay name=pay1 pt=97 )");
		gst_rtsp_media_factory_set_shared (r->factory, TRUE);

		g_signal_connect (r->factory, "media-configure", (GCallback) media_configure, app);

		g_mutex_unlock (&app->rtsp_mutex);

		gchar *credentials = "";
		if (strlen(user)) {
			r->rtsp_user = g_strdup(user);
			r->rtsp_pass = g_strdup(pass);
			GstRTSPToken *token;
			gchar *basic;
			GstRTSPAuth *auth = gst_rtsp_auth_new ();
			gst_rtsp_media_factory_add_role (r->factory, "user", GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE, GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
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
		gst_rtsp_mount_points_add_factory (r->mounts, r->rtsp_path, g_object_ref(r->factory));
		r->clients_list = NULL;
		r->media = NULL;
		r->enabled = TRUE;
		r->source_id = gst_rtsp_server_attach (r->server, NULL);
		g_print ("dreambox encoder stream ready at rtsp://%s127.0.0.1:%s%s\n", credentials, app->rtsp_server->rtsp_port, app->rtsp_server->rtsp_path);
		return TRUE;
	}
	return FALSE;
}

gboolean start_rtsp_pipeline(App* app)
{
	GST_INFO_OBJECT(app, "start_rtsp_pipeline");

	DreamRTSPserver *r = app->rtsp_server;
	if (!r->enabled)
	{
		GST_ERROR_OBJECT (app, "failed to start rtsp pipeline because rtsp server is not enabled!");
		return FALSE;
	}

	g_object_set (G_OBJECT (app->aappsink), "emit-signals", TRUE, NULL);
	g_object_set (G_OBJECT (app->vappsink), "emit-signals", TRUE, NULL);

	GstState state;
	GstStateChangeReturn sret;

	sret = gst_element_set_state (app->pipeline, GST_STATE_PLAYING);
	if (sret == GST_STATE_CHANGE_FAILURE)
	{
		GST_ERROR_OBJECT (app, "GST_STATE_CHANGE_FAILURE for rtsp pipeline");
		return FALSE;
	}
	GST_INFO_OBJECT(app, "started rtsp pipeline, pipeline is PLAYING");
	return TRUE;
}

gboolean stop_rtsp_pipeline(App* app)
{
	GST_INFO_OBJECT(app, "stop_rtsp_pipeline");

	g_object_set (G_OBJECT (app->aappsink), "emit-signals", FALSE, NULL);
	g_object_set (G_OBJECT (app->vappsink), "emit-signals", FALSE, NULL);

	if (!app->tcp_upstream->enabled)
		pause_source_pipeline(app);
// 		g_mutex_unlock (&app->rtsp_mutex);
	return TRUE;
}

gboolean pause_source_pipeline(App* app)
{
	GST_INFO_OBJECT(app, "pause_source_pipeline");
	GstStateChangeReturn ret;
	if (gst_element_set_state (app->asrc, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL && gst_element_set_state (app->vsrc, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL)
		return TRUE;
	GST_WARNING("can't set sources to GST_STATE_PAUSED!", ret);
	return FALSE;
}

GstRTSPFilterResult client_filter_func (GstRTSPServer *server, GstRTSPClient *client, gpointer user_data)
{
	App *app = user_data;
	GST_INFO("client_filter_func %" GST_PTR_FORMAT "  (number of clients: %i)", client, g_list_length(app->rtsp_server->clients_list));
	return GST_RTSP_FILTER_REMOVE;
}

gboolean disable_rtsp_server(App *app)
{
	DreamRTSPserver *r = app->rtsp_server;
	GST_INFO("disable_rtsp_server %p", r->server);
	if (r->enabled)
	{
		if (app->rtsp_server->media)
		{
			GList *filter = gst_rtsp_server_client_filter(app->rtsp_server->server, (GstRTSPServerClientFilterFunc) client_filter_func, app);
		}
		gst_rtsp_mount_points_remove_factory (app->rtsp_server->mounts, app->rtsp_server->rtsp_path);
		if (r->mounts)
			g_object_unref(r->mounts);
		if (r->server)
			gst_object_unref(r->server);
		GSource *source = g_main_context_find_source_by_id (g_main_context_default (), r->source_id);
		g_source_destroy(source);
		g_source_unref(source);
		g_free(r->rtsp_user);
		g_free(r->rtsp_pass);
		g_free(r->rtsp_port);
		g_free(r->rtsp_path);
		g_mutex_lock (&app->rtsp_mutex);
		app->rtsp_server->enabled = FALSE;
		g_mutex_unlock (&app->rtsp_mutex);
		GST_INFO("rtsp_server disabled!");
		return TRUE;
	}
	return FALSE;
}

gboolean disable_tcp_upstream(App *app)
{
	GST_INFO("disable_tcp_upstream");
	gboolean ret = FALSE;
	DreamTCPupstream *t = app->tcp_upstream;
	if (app->rtsp_server->enabled)
	{
		gst_element_set_state (t->tsmux, GST_STATE_NULL);
		gst_element_set_state (t->payloader, GST_STATE_NULL);
		gst_element_set_state (t->tcpsink, GST_STATE_NULL);

		GstPad *teepad, *sinkpad;
		sinkpad = gst_element_get_static_pad (t->afilter, "sink");
		teepad = gst_pad_get_peer(sinkpad);
		gst_pad_unlink (teepad, sinkpad);
		gst_element_release_request_pad (app->atee, teepad);
		gst_object_unref (teepad);
		gst_object_unref (sinkpad);
		sinkpad = gst_element_get_static_pad (t->vfilter, "sink");
		teepad = gst_pad_get_peer(sinkpad);
		gst_pad_unlink (teepad, sinkpad);
		gst_element_release_request_pad (app->vtee, teepad);
		gst_object_unref (teepad);
		gst_object_unref (sinkpad);
		gst_element_unlink_many(t->tsmux, t->payloader, t->tcpsink, NULL);
		gst_object_ref(t->afilter);
		gst_object_ref(t->vfilter);
		gst_object_ref(t->tsmux);
		gst_object_ref(t->payloader);
		gst_object_ref(t->tcpsink);
		gst_bin_remove_many (GST_BIN (app->pipeline), t->afilter, t->vfilter, t->tsmux, t->payloader, t->tcpsink, NULL);
	}
	else
	{
		pause_source_pipeline(app);
	}

	if (t->enabled)
	{
		t->enabled = FALSE;
		ret = TRUE;
	}
	return ret;
}

gboolean destroy_pipeline(App *app)
{
	GST_INFO_OBJECT(app, "destroy_pipeline @%p", app->pipeline);
	if (app->pipeline)
	{
		GstStateChangeReturn sret = gst_element_set_state (app->pipeline, GST_STATE_NULL);
		GST_INFO_OBJECT(app, "really do destroy_pipeline sret=%i", sret);
		if (sret == GST_STATE_CHANGE_ASYNC)
		{
			GstState state;
			gst_element_get_state (GST_ELEMENT(app->pipeline), &state, NULL, 3*GST_SECOND);
			GST_INFO_OBJECT(app, "%" GST_PTR_FORMAT"'s state=%s", app->pipeline, gst_element_state_get_name (state));
		}
		gst_object_unref (app->pipeline);
		gst_object_unref (app->clock);
		app->pipeline = NULL;
		return TRUE;
	}
	else
		GST_INFO_OBJECT(app, "don't destroy_pipeline");
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
	if (!create_source_pipeline(&app))
		g_error ("Failed to bring state of source pipeline to READY");

	introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);

	owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
				   service,
			    G_BUS_NAME_OWNER_FLAGS_NONE,
			    on_bus_acquired,
			    on_name_acquired,
			    on_name_lost,
			    &app,
			    NULL);

	g_mutex_init (&app.rtsp_mutex);

	app.tcp_upstream = malloc(sizeof(DreamTCPupstream));
	app.tcp_upstream->enabled = FALSE;

	app.rtsp_server = malloc(sizeof(DreamRTSPserver));
	app.rtsp_server->enabled = FALSE;

	app.loop = g_main_loop_new (NULL, FALSE);

	g_main_loop_run (app.loop);

	destroy_pipeline(&app);

	g_main_loop_unref (app.loop);

	g_mutex_clear (&app.rtsp_mutex);

	free(app.tcp_upstream);
	if (app.rtsp_server->enabled)
		disable_rtsp_server(&app);
	g_list_free (app.rtsp_server->clients_list);
	free(app.rtsp_server);

	g_bus_unown_name (owner_id);
	g_dbus_node_info_unref (introspection_data);

	GST_INFO_OBJECT(&app, "exit");
}
