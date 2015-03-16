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

GST_DEBUG_CATEGORY (dreamrtspserver_debug);
#define GST_CAT_DEFAULT dreamrtspserver_debug

#define TOKEN_LEN 36

typedef enum {
        INPUT_MODE_LIVE = 0,
        INPUT_MODE_HDMI_IN = 1,
        INPUT_MODE_BACKGROUND = 2
} inputMode;

typedef enum {
        UPSTREAM_STATE_DISABLED = 0,
        UPSTREAM_STATE_CONNECTING = 1,
        UPSTREAM_STATE_RUNNING = 2
} upstreamState;

typedef struct {
	GstElement *tcpsink;
	gulong inject_id;
	char token[TOKEN_LEN+1];
	upstreamState state;
	int removing;
} DreamTCPupstream;

typedef struct {
	GMainLoop *loop;

	GstElement *pipeline;
	GstElement *asrc, *vsrc, *aparse, *vparse;
	GstElement *tsmux, *tsq;

	DreamTCPupstream *tcp_upstream;
	GMutex rtsp_mutex;
	GstClock *clock;
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
  "  </interface>"
  "</node>";

gboolean create_source_pipeline(App *app);
gboolean pause_source_pipeline(App *app);
gboolean enable_tcp_upstream(App *app, const gchar *upstream_host, guint32 upstream_port, const gchar *token);
gboolean disable_tcp_upstream(App *app);
gboolean destroy_pipeline(App *app);

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
	ret = TRUE;

out:
	if (GST_IS_CAPS(oldcaps))
		gst_caps_unref(oldcaps);
	if (GST_IS_CAPS(newcaps))
		gst_caps_unref(newcaps);
	return ret;
}

static gboolean gst_set_resolution(App *app, int width, int height)
{
	GstCaps *oldcaps, *newcaps;
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
	if (GST_IS_CAPS(oldcaps))
		gst_caps_unref(oldcaps);
	if (GST_IS_CAPS(newcaps))
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
		return g_variant_new_int32 (0);
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
	if (g_strcmp0 (method_name, "enableUpstream") == 0)
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
			{
				result = disable_tcp_upstream(app);
				destroy_pipeline(app);
				create_source_pipeline(app);
			}
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

	GError *error = NULL;
	GST_DEBUG ("aquired dbus (\"%s\" @ %p)", name, connection);
	g_dbus_connection_register_object (connection, object_name, introspection_data->interfaces[0], &interface_vtable, user_data, NULL, &error);
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
// 	App *app = user_data;

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
					g_mutex_unlock (&app->rtsp_mutex);
					disable_tcp_upstream(app);
					destroy_pipeline(app);
					create_source_pipeline(app);
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

gboolean create_source_pipeline(App *app)
{
	GST_INFO_OBJECT(app, "create_source_pipeline");
	g_mutex_lock (&app->rtsp_mutex);
	app->pipeline = gst_pipeline_new (NULL);
	app->asrc = gst_element_factory_make ("dreamaudiosource", "dreamaudiosource0");
	app->vsrc = gst_element_factory_make ("dreamvideosource", "dreamvideosource0");

	app->aparse = gst_element_factory_make ("aacparse", NULL);
	app->vparse = gst_element_factory_make ("h264parse", NULL);
	
	app->tsmux = gst_element_factory_make ("mpegtsmux", NULL);
	app->tsq   = gst_element_factory_make ("queue", "tstcpqueue");

	if (!(app->asrc && app->vsrc && app->aparse && app->vparse))
	{
		g_error ("Failed to create source pipeline element(s):%s%s%s%s", app->asrc?"":" dreamaudiosource", app->vsrc?"":" dreamvideosource", app->aparse?"":" aacparse", app->vparse?"":" h264parse");
	}
	
	g_object_set (G_OBJECT (app->tsq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);

	gst_bin_add_many (GST_BIN (app->pipeline), app->asrc, app->vsrc, app->aparse, app->vparse, NULL);
	gst_bin_add_many (GST_BIN (app->pipeline), app->tsmux, app->tsq, NULL);
	gst_element_link_many (app->asrc, app->aparse, NULL);
	gst_element_link_many (app->vsrc, app->vparse, NULL);

	GstPadLinkReturn ret;
	GstPad *srcpad, *sinkpad;

	srcpad = gst_element_get_static_pad (app->aparse, "src");
	sinkpad = gst_element_get_compatible_pad (app->tsmux, srcpad, NULL);
	ret = gst_pad_link (srcpad, sinkpad);
	if (ret != GST_PAD_LINK_OK)
	{
		GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", srcpad, sinkpad);
		return FALSE;
	}

	gst_object_unref (srcpad);
	gst_object_unref (sinkpad);

	srcpad = gst_element_get_static_pad (app->vparse, "src");
	sinkpad = gst_element_get_compatible_pad (app->tsmux, srcpad, NULL);
	ret = gst_pad_link (srcpad, sinkpad);
	if (ret != GST_PAD_LINK_OK)
	{
		GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", srcpad, sinkpad);
		return FALSE;
	}

	gst_object_unref (srcpad);
	gst_object_unref (sinkpad);

	if (!gst_element_link (app->tsmux, app->tsq)) {
		g_error ("Failed to link tsmux to tsqueue");
	}

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

gboolean upstream_connect_cb(App *app)
{
	if (app->tcp_upstream->state == UPSTREAM_STATE_CONNECTING)
	{
		GST_INFO ("upstream connect succeeded new upstreamState UPSTREAM_STATE_RUNNING");
	}
	else
		GST_INFO ("upstream connect failed.");
	return FALSE;
}

static GstPadProbeReturn inject_authorization (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	GstBuffer *token_buf = gst_buffer_new_wrapped (app->tcp_upstream->token, TOKEN_LEN);
	GstPad * srcpad = gst_element_get_static_pad (app->tsq, "src");

	GST_INFO ("injecting authorization on pad %s:%s, created token_buf %" GST_PTR_FORMAT "", GST_DEBUG_PAD_NAME (sinkpad), token_buf);
	gst_pad_remove_probe (sinkpad, app->tcp_upstream->inject_id);
	g_timeout_add_seconds (5, (GSourceFunc) upstream_connect_cb, app);
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
		t->removing = 0;
		t->state = UPSTREAM_STATE_CONNECTING;

		t->tcpsink = gst_element_factory_make ("tcpclientsink", NULL);

		if (!t->tcpsink)
		g_error ("Failed to create tcp upstream element: %s", t->tcpsink?"":"  tcpclientsink" );

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
			gst_object_unref (t->tcpsink);
			t->state = UPSTREAM_STATE_DISABLED;
			return FALSE;
		}

		gst_bin_add (GST_BIN (app->pipeline), t->tcpsink);

		if (!gst_element_link (app->tsq, t->tcpsink)) {
			g_error ("Failed to link tsqueue to tcpclientsink");
		}

		if (strlen(token))
		{
			GstPad *sinkpad = gst_element_get_static_pad (t->tcpsink, "sink");
			strcpy(t->token, token);
			t->inject_id = gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) inject_authorization, app, NULL);
			gst_object_unref (sinkpad);
		}
		
		sret = gst_element_set_state (t->tcpsink, GST_STATE_PAUSED);
		GST_DEBUG_OBJECT(app, "gst_element_set_state (t->tcpsink, GST_STATE_PAUSED) = %i", sret);
		
// 		if (sret == GST_STATE_CHANGE_ASYNC)
// 		{
// 			GstState state;
// 			gst_element_get_state (GST_ELEMENT (t->tcpsink), &state, NULL, 3*GST_SECOND);
// 			GST_DEBUG_OBJECT(app, "%" GST_PTR_FORMAT"'s state=%s", t->tcpsink, gst_element_state_get_name (state));
// 		}

		sret = gst_element_set_state (app->pipeline, GST_STATE_PLAYING);
		GST_DEBUG_OBJECT(app, "gst_element_set_state (app->pipeline, GST_STATE_PLAYING) = %i", sret);

		if (sret == GST_STATE_CHANGE_FAILURE)
		{
			GST_ERROR_OBJECT (app, "GST_STATE_CHANGE_FAILURE for upstream pipeline");
			goto fail;
		}
		else if (sret == GST_STATE_CHANGE_ASYNC)
		{
			GstState state;
//			gst_element_get_state (GST_ELEMENT(t->tcpsink), &state, NULL, 3*GST_SECOND);
//			GST_DEBUG_OBJECT(app, "app->tcpsink's state=%s", gst_element_state_get_name (state));
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

gboolean pause_source_pipeline(App* app)
{
	GST_INFO_OBJECT(app, "pause_source_pipeline");
	if (gst_element_set_state (app->asrc, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL && gst_element_set_state (app->vsrc, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL)
		return TRUE;
	GST_WARNING("can't set sources to GST_STATE_PAUSED!");
	return FALSE;
}

static GstPadProbeReturn pad_probe_unlink_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	DreamTCPupstream *t = app->tcp_upstream;
	GstPadProbeReturn ret;

	GST_DEBUG_OBJECT (pad, "event_probe_unlink_cb removing=%i, type=%i", t->removing, info->type);

	GstElement *element = gst_pad_get_parent_element(pad);
	
	if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_IDLE && element && element == t->tcpsink)
	{
		GST_DEBUG_OBJECT (pad, "GST_PAD_PROBE_TYPE_IDLE -> unlink and remove tcpsink");
		
		pause_source_pipeline(app);
		
		gst_element_unlink(app->tsq, t->tcpsink);
		gst_bin_remove (GST_BIN (app->pipeline), t->tcpsink);

		gst_element_set_state (t->tcpsink, GST_STATE_NULL);
		gst_object_unref (t->tcpsink);
		
		GstStateChangeReturn sret = gst_element_set_state (app->pipeline, GST_STATE_PAUSED);
		GST_INFO_OBJECT(app, "set_state paused ret=%i", sret);

		GST_INFO("tcp_upstream disabled!");
		t->state = UPSTREAM_STATE_DISABLED;
		
		ret = GST_PAD_PROBE_REMOVE;
	}
	return ret;

}

gboolean disable_tcp_upstream(App *app)
{
	GST_DEBUG("disable_tcp_upstream");
	DreamTCPupstream *t = app->tcp_upstream;
	if (t->state >= UPSTREAM_STATE_CONNECTING)
	{
		gst_object_ref (t->tcpsink);

		GstPad *sinkpad;

		sinkpad = gst_element_get_static_pad (t->tcpsink, "sink");
		gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, pad_probe_unlink_cb, app, NULL);
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

	app.tcp_upstream = malloc(sizeof(DreamTCPupstream));
	app.tcp_upstream->state = UPSTREAM_STATE_DISABLED;

	app.loop = g_main_loop_new (NULL, FALSE);
	g_unix_signal_add(SIGINT, quit_signal, app.loop);

	g_main_loop_run (app.loop);

	destroy_pipeline(&app);

	g_main_loop_unref (app.loop);

	free(app.tcp_upstream);

	g_mutex_clear (&app.rtsp_mutex);

	g_bus_unown_name (owner_id);
	g_dbus_node_info_unref (introspection_data);

	GST_DEBUG("exit");
	return 0;
}
