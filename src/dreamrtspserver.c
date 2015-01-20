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
 * hardware which is licensed by Dream Multimedia GmbH.
 *
 * This program is NOT free software. It is open source, you are allowed
 * to modify it (if you keep the license), but it may not be commercially
 * distributed other than under the conditions noted above.
 */

#include <string.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/app/app.h>
#include <gst/rtsp-server/rtsp-server.h>

GST_DEBUG_CATEGORY (dreamrtspserver_debug);
#define GST_CAT_DEFAULT dreamrtspserver_debug

typedef struct {
	GMainLoop *loop;
	GstRTSPServer *server;
	GstRTSPMountPoints *mounts;
	GstRTSPMediaFactory *factory;
	GMutex rtsp_mutex;

	GstElement *pipeline, *aappsink, *vappsink, *aappsrc, *vappsrc;
	GstClockTime rtsp_start_pts, rtsp_start_dts;

	guint clients_count;
} DreamRTSPserver;

static const gchar *rtsp_port = "554";
static const gchar service[] = "com.dreambox.RTSPserver";
static const gchar object_name[] = "/com/dreambox/RTSPserver";
static GDBusNodeInfo *introspection_data = NULL;
static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='com.dreambox.RTSPserver'>"
  "    <method name='setEnabled'>"
  "      <arg type='b' name='state' direction='in'/>"
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
  "  </interface>"
  "</node>";

static gboolean gst_set_framerate(DreamRTSPserver *s, int value)
{
	GstElement *element;
	GstCaps *caps;
	GstStructure *structure;

	element = gst_bin_get_by_name(GST_BIN(s->pipeline), "dreamvideosource");
	if (!element)
		return FALSE;

	g_object_get (G_OBJECT (element), "caps", &caps, NULL);

	if (!GST_IS_CAPS(caps))
		return FALSE;

	GST_INFO("gst_set_framerate %d old caps %" GST_PTR_FORMAT, value, caps);

	structure = gst_caps_steal_structure (caps, 0);
	if (!structure)
		return FALSE;

	if (value)
		gst_structure_set (structure, "framerate", GST_TYPE_FRACTION, value, 1, NULL);

	gst_caps_append_structure (caps, structure);
	GST_INFO("new caps %" GST_PTR_FORMAT, caps);
	g_object_set (G_OBJECT (element), "caps", caps, NULL);
	return TRUE;
}

static gboolean gst_set_resolution(DreamRTSPserver *s, int width, int height)
{
	GstElement *element;
	GstCaps *caps;
	GstStructure *structure;

	element = gst_bin_get_by_name(GST_BIN(s->pipeline), "dreamvideosource");
	if (!element)
		return FALSE;

	g_object_get (G_OBJECT (element), "caps", &caps, NULL);

	if (!GST_IS_CAPS(caps))
		return FALSE;

	GST_INFO("old caps %" GST_PTR_FORMAT, caps);

	structure = gst_caps_steal_structure (caps, 0);
	if (!structure)
		return FALSE;

	if (width && height)
	{
		gst_structure_set (structure, "width", G_TYPE_INT, width, NULL);
		gst_structure_set (structure, "height", G_TYPE_INT, height, NULL);
	}
	gst_caps_append_structure (caps, structure);
	GST_INFO("new caps %" GST_PTR_FORMAT, caps);
	g_object_set (G_OBJECT (element), "caps", caps, NULL);
	return TRUE;
}

static gboolean gst_get_capsprop(DreamRTSPserver *s, const gchar* element_name, const gchar* prop_name, guint32 *value)
{
	GstElement *element;
	GstCaps *caps;
	const GstStructure *structure;

	element = gst_bin_get_by_name(GST_BIN(s->pipeline), element_name);
	if (!element)
		return FALSE;

	g_object_get (G_OBJECT (element), "caps", &caps, NULL);

	if (!GST_IS_CAPS(caps))
		return FALSE;

	GST_INFO("current caps %" GST_PTR_FORMAT, caps);

	structure = gst_caps_get_structure (caps, 0);
	if (!structure)
		return FALSE;

	if (g_strcmp0 (prop_name, "framerate") == 0 && value)
	{
		const GValue *framerate = gst_structure_get_value (structure, "framerate");
		*value = gst_value_get_fraction_numerator (framerate);
	}
	else if ((g_strcmp0 (prop_name, "width") == 0 || g_strcmp0 (prop_name, "height") == 0) && value)
	{
		gst_structure_get_int (structure, prop_name, value);
	}
	else
		return FALSE;

	gst_caps_unref(caps);

	GST_INFO("%s.%s = %i", element_name, prop_name, *value);

	return TRUE;
}

static GVariant *handle_get_property (GDBusConnection  *connection,
		const gchar      *sender,
		const gchar      *object_path,
		const gchar      *interface_name,
		const gchar      *property_name,
		GError          **error,
		gpointer          user_data)
{
	DreamRTSPserver *s = user_data;

	GST_DEBUG("dbus get property %s from %s", property_name, sender);

	if (g_strcmp0 (property_name, "state") == 0)
	{
		GstState state;
		gst_element_get_state (s->pipeline, &state, NULL, 1*GST_SECOND);
		return g_variant_new_boolean (state == GST_STATE_PLAYING);
	}
	else if (g_strcmp0 (property_name, "clients") == 0)
	{
		return g_variant_new_int32 (s->clients_count);
	}
	else if (g_strcmp0 (property_name, "audioBitrate") == 0)
	{
		GstElement *source = gst_bin_get_by_name(GST_BIN(s->pipeline), "dreamaudiosource");
		gint rate = 0;
		if (source)
			g_object_get (G_OBJECT (source), "bitrate", &rate, NULL);
		return g_variant_new_int32 (rate);
	}
	else if (g_strcmp0 (property_name, "videoBitrate") == 0)
	{
		GstElement *source = gst_bin_get_by_name(GST_BIN(s->pipeline), "dreamvideosource");
		gint rate = 0;
		if (source)
			g_object_get (G_OBJECT (source), "bitrate", &rate, NULL);
		return g_variant_new_int32 (rate);
	}
	else if (g_strcmp0 (property_name, "width") == 0 || g_strcmp0 (property_name, "height") == 0 || g_strcmp0 (property_name, "framerate") == 0)
	{
		guint32 value;
		if (gst_get_capsprop(s, "dreamvideosource", property_name, &value))
			return g_variant_new_int32(value);
		GST_WARNING("can't handle_get_property name=%s", property_name);
		return g_variant_new_int32(0);
	}
	g_set_error (error, G_IO_ERROR,	G_IO_ERROR_FAILED, "[RTSPserver] Invalid property '%s'", property_name);
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
	DreamRTSPserver *s = user_data;

	gchar *valstr = g_variant_print (value, TRUE);
	GST_DEBUG("dbus set property %s = %s from %s", property_name, valstr, sender);
	g_free (valstr);

	if (g_strcmp0 (property_name, "audioBitrate") == 0)
	{
		GstElement *source = gst_bin_get_by_name(GST_BIN(s->pipeline), "dreamaudiosource");
		g_object_set (G_OBJECT (source), "bitrate", g_variant_get_int32 (value), NULL);
		return 1;
	}
	else if (g_strcmp0 (property_name, "videoBitrate") == 0)
	{
		GstElement *source = gst_bin_get_by_name(GST_BIN(s->pipeline), "dreamvideosource");
		g_object_set (G_OBJECT (source), "bitrate", g_variant_get_int32 (value), NULL);
		return 1;
	}
	else if (g_strcmp0 (property_name, "framerate") == 0)
	{
		if (gst_set_framerate(s, g_variant_get_int32 (value)))
			return 1;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "[RTSPserver] can't set property '%s' to %d", property_name, g_variant_get_int32 (value));
	}
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "[RTSPserver] Invalid property: '%s'", property_name);
	} // unknown property
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
	DreamRTSPserver *s = user_data;

	gchar *paramstr = g_variant_print (parameters, TRUE);
	GST_DEBUG("dbus handle method %s %s from %s", method_name, paramstr, sender);
	g_free (paramstr);

	// Method one: HelloWorld
	if (g_strcmp0 (method_name, "setEnabled") == 0)
	{
		gboolean val;
		GVariant *result;
		g_variant_get (parameters, "(b)", &val);
		GstState newstate = val ? GST_STATE_PLAYING : GST_STATE_PAUSED;

		if (gst_element_set_state (s->pipeline, newstate) == GST_STATE_CHANGE_FAILURE)
			result = g_variant_new ("(b)", FALSE);
		else
			result = g_variant_new ("(b)", TRUE);
		g_dbus_method_invocation_return_value (invocation, result);
	}
	else if (g_strcmp0 (method_name, "setResolution") == 0)
	{
		int width, height;
		g_variant_get (parameters, "(ii)", &width, &height);

		if (gst_set_resolution(s, width, height))
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
	DreamRTSPserver *s = user_data;

	GST_WARNING ("lost dbus name (\"%s\" @ %p)", name, connection);
// 	g_main_loop_quit (s->loop);
} // on_name_lost

static gboolean message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
	DreamRTSPserver *s = user_data;

	switch (GST_MESSAGE_TYPE (message)) {
		case GST_MESSAGE_STATE_CHANGED:
		{
			GstState old_state, new_state;
			gst_message_parse_state_changed(message, &old_state, &new_state, NULL);
			if (old_state == new_state)
				break;

			if (GST_MESSAGE_SRC(message) == GST_OBJECT(s->pipeline))
			{
				GST_INFO("state transition %s -> %s", gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));

				GstStateChange transition = (GstStateChange)GST_STATE_TRANSITION(old_state, new_state);

				switch(transition)
				{
					case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
					{
						if (GST_MESSAGE_SRC (message) == GST_OBJECT (s->pipeline))
						{
							g_print ("dreambox encoder stream ready at rtsp://127.0.0.1:%s/stream\n", rtsp_port);
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
			g_printerr ("ERROR: from element %s: %s\n", name, err->message);
			if (debug != NULL)
				g_printerr ("Additional debug info:\n%s\n", debug);
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
			g_printerr ("WARNING: from element %s: %s\n", name, err->message);
			if (debug != NULL)
				g_printerr ("Additional debug info:\n%s\n", debug);
			g_error_free (err);
			g_free (debug);
			g_free (name);
			break;
		}
		case GST_MESSAGE_EOS:
			g_print ("Got EOS\n");
			g_main_loop_quit (s->loop);
			break;
		default:
			break;
	}
	return TRUE;
}

static void media_unprepare (GstRTSPMedia * media, gpointer user_data)
{
	DreamRTSPserver *s = user_data;
	GST_INFO("media_unprepare");
}

static void client_closed (GstRTSPClient * client, gpointer user_data)
{
	DreamRTSPserver *s = user_data;
	s->clients_count--;
	GST_INFO("client_closed clients_count=%i", s->clients_count);
}

static void client_connected (GstRTSPServer * server, GstRTSPClient * client, gpointer user_data)
{
	DreamRTSPserver *s = user_data;
	s->clients_count++;
	GST_INFO("client_connected %" GST_PTR_FORMAT " clients_count=%i", client, s->clients_count);
	g_signal_connect (client, "closed", (GCallback) client_closed, s);
}

static void media_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media, gpointer user_data)
{
	DreamRTSPserver *s = user_data;
	g_mutex_lock (&s->rtsp_mutex);
	GST_INFO("media_configure");
	GstElement *element = gst_rtsp_media_get_element (media);
	s->aappsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), "aappsrc");
	s->vappsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), "vappsrc");
	g_signal_connect (media, "unprepared", (GCallback) media_unprepare, s);
	g_object_set (s->aappsrc, "format", GST_FORMAT_TIME, NULL);
	g_object_set (s->vappsrc, "format", GST_FORMAT_TIME, NULL);
	s->rtsp_start_pts = s->rtsp_start_dts = GST_CLOCK_TIME_NONE;
	g_mutex_unlock (&s->rtsp_mutex);
}

static GstFlowReturn handover_payload (GstElement * appsink, gpointer user_data)
{
	DreamRTSPserver *s = user_data;

	GstAppSrc* appsrc = NULL;
	if ( appsink == s->vappsink )
		appsrc = GST_APP_SRC(s->vappsrc);
	else if ( appsink == s->aappsink )
		appsrc = GST_APP_SRC(s->aappsrc);

	g_mutex_lock (&s->rtsp_mutex);
	if (appsrc && s->clients_count) {
		GstSample *sample = gst_app_sink_pull_sample (GST_APP_SINK (appsink));
		GstBuffer *buffer = gst_sample_get_buffer (sample);
		GstCaps *caps = gst_sample_get_caps (sample);

		GstBuffer *tmp;
		tmp = gst_buffer_copy (buffer);
		GST_LOG("original PTS %" GST_TIME_FORMAT " DTS %" GST_TIME_FORMAT "", GST_TIME_ARGS (GST_BUFFER_PTS (tmp)), GST_TIME_ARGS (GST_BUFFER_DTS (tmp)));
		if (s->rtsp_start_pts == GST_CLOCK_TIME_NONE) {
			s->rtsp_start_pts = GST_BUFFER_PTS (tmp);
			s->rtsp_start_dts = GST_BUFFER_DTS (tmp);
			GST_INFO("set rtsp_start_pts=%" GST_TIME_FORMAT " rtsp_start_dts=%" GST_TIME_FORMAT "", GST_TIME_ARGS (GST_BUFFER_PTS (tmp)), GST_TIME_ARGS (GST_BUFFER_DTS (tmp)));
		}
		if (GST_BUFFER_PTS (tmp) < s->rtsp_start_pts)
			GST_BUFFER_PTS (tmp) = 0;
		else
			GST_BUFFER_PTS (tmp) -= s->rtsp_start_pts;
		GST_BUFFER_DTS (tmp) -= s->rtsp_start_dts;
// 		GST_LOG("new PTS %" GST_TIME_FORMAT " DTS %" GST_TIME_FORMAT "", GST_TIME_ARGS (GST_BUFFER_PTS (tmp)), GST_TIME_ARGS (GST_BUFFER_DTS (tmp)));

		GstCaps *oldcaps = gst_app_src_get_caps (appsrc);
		if (!oldcaps || !gst_caps_is_equal (oldcaps, caps))
		{
			GST_LOG("CAPS changed! %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, oldcaps, caps);
			gst_app_src_set_caps (appsrc, caps);
		}

		gst_app_src_push_buffer (appsrc, tmp);
		gst_sample_unref (sample);
	}
	else
	{
		if ( gst_debug_category_get_threshold (dreamrtspserver_debug) >= GST_LEVEL_DEBUG)
			GST_LOG("no rtsp clients, discard payload!");
		else
			g_print (".");
	}
	g_mutex_unlock (&s->rtsp_mutex);

	return GST_FLOW_OK;
}

int main (int argc, char *argv[])
{
	DreamRTSPserver s;
	GstBus *bus;
	guint owner_id;

	memset (&s, 0, sizeof(s));

	introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);

	owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
		service,
		G_BUS_NAME_OWNER_FLAGS_NONE,
		on_bus_acquired,
		on_name_acquired,
		on_name_lost,
		&s,
		NULL);

	gst_init (0, NULL);

	GST_DEBUG_CATEGORY_INIT (dreamrtspserver_debug, "dreamrtspserver",
		GST_DEBUG_BOLD | GST_DEBUG_FG_YELLOW | GST_DEBUG_BG_BLUE,
		"Dreambox RTSP server daemon");

	GstElement *asrc, *vsrc, *aq, *vq, *aparse, *vparse;

	s.clients_count = 0;
	s.pipeline = gst_pipeline_new (NULL);

	asrc = gst_element_factory_make ("dreamaudiosource", "dreamaudiosource");
	vsrc = gst_element_factory_make ("dreamvideosource", "dreamvideosource");

	aparse = gst_element_factory_make ("aacparse", NULL);
	vparse = gst_element_factory_make ("h264parse", NULL);

	aq = gst_element_factory_make ("queue", NULL);
	vq = gst_element_factory_make ("queue", NULL);

	s.aappsink = gst_element_factory_make ("appsink", "aappsink");
	s.vappsink = gst_element_factory_make ("appsink", "vappsink");

	GstElement *appsrc, *vpay, *apay, *udpsrc;
	appsrc = gst_element_factory_make ("appsrc", NULL);
	vpay = gst_element_factory_make ("rtph264pay", NULL);
	apay = gst_element_factory_make ("rtpmp4apay", NULL);
	udpsrc = gst_element_factory_make ("udpsrc", NULL);

	if (!asrc || !vsrc || !aparse || !vparse || !aq || !vq || !s.aappsink || !s.vappsink || !appsrc || !vpay || !apay || !udpsrc)
	{
		g_error ("Failed to create element(s):%s%s%s%s%s%s%s%s%s%s%s%s", asrc?"":" dreamaudiosource", vsrc?"":" dreamvideosource", aparse?"":" aacparse", vparse?"":" h264parse", aq?"":" aqueue",
			vq?"":" vqueue", s.aappsink?"":" aappsink", s.vappsink?"":" vappsink", appsrc?"":" appsrc", vpay?"": "rtph264pay", apay?"":" rtpmp4apay", udpsrc?"":" udpsrc" );
	}
	else
	{
		gst_object_unref (appsrc);
		gst_object_unref (vpay);
		gst_object_unref (apay);
		gst_object_unref (udpsrc);
	}

	gst_bin_add_many (GST_BIN (s.pipeline), asrc, vsrc, aparse, vparse, aq, vq, s.aappsink, s.vappsink, NULL);
	gst_element_link_many (asrc, aparse, aq, s.aappsink, NULL);
	gst_element_link_many (vsrc, vparse, vq, s.vappsink, NULL);

	g_object_set (G_OBJECT (aq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);
	g_object_set (G_OBJECT (vq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);

	g_object_set (G_OBJECT (s.aappsink), "emit-signals", TRUE, NULL);
	g_signal_connect (s.aappsink, "new-sample", G_CALLBACK (handover_payload), &s);

	g_object_set (G_OBJECT (s.vappsink), "emit-signals", TRUE, NULL);
	g_signal_connect (s.vappsink, "new-sample", G_CALLBACK (handover_payload), &s);

	s.server = gst_rtsp_server_new ();
	gst_rtsp_server_set_service (s.server, rtsp_port);

	s.mounts = gst_rtsp_server_get_mount_points (s.server);

	s.factory = gst_rtsp_media_factory_new ();
	gst_rtsp_media_factory_set_launch (s.factory, "( appsrc name=vappsrc ! h264parse ! rtph264pay name=pay0 pt=96   appsrc name=aappsrc ! aacparse ! rtpmp4apay name=pay1 pt=97 )");
	gst_rtsp_media_factory_set_shared(s.factory, TRUE);

	if (argc == 3) {
		gchar *rtsp_user = argv[1];
		gchar *rtsp_pass = argv[2];
		GstRTSPToken *token;
		gchar *basic;
		GstRTSPAuth *auth = gst_rtsp_auth_new ();
		gst_rtsp_media_factory_add_role (s.factory, "user", GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE, GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
		token = gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "user", NULL);
		basic = gst_rtsp_auth_make_basic (rtsp_user, rtsp_pass);
		gst_rtsp_server_set_auth (s.server, auth);
		gst_rtsp_auth_add_basic (auth, basic, token);
		g_free (basic);
		gst_rtsp_token_unref (token);
	}

	gst_rtsp_mount_points_add_factory (s.mounts, "/stream", s.factory);
	g_object_unref (s.mounts);

	g_signal_connect (s.factory, "media-configure", (GCallback) media_configure, &s);
	g_signal_connect (s.server, "client-connected", (GCallback) client_connected, &s);

	gst_rtsp_server_attach (s.server, NULL);

	g_mutex_init (&s.rtsp_mutex);

	s.loop = g_main_loop_new (NULL, FALSE);

	bus = gst_pipeline_get_bus (GST_PIPELINE (s.pipeline));
	gst_bus_add_signal_watch (bus);
	g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (message_cb), &s);
	gst_object_unref (GST_OBJECT (bus));
	
	gst_element_set_state (s.pipeline, GST_STATE_PAUSED);

	g_main_loop_run (s.loop);

	gst_element_set_state (s.pipeline, GST_STATE_NULL);

	g_main_loop_unref (s.loop);
	gst_object_unref (s.pipeline);

	g_mutex_clear (&s.rtsp_mutex);

	g_bus_unown_name (owner_id);
	g_dbus_node_info_unref (introspection_data);
}
