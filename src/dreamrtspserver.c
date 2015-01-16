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
#include <stdio.h>
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
  "    <property type='b' name='state' access='read'/>"
  "    <property type='i' name='clients' access='read'/>"
  "    <property type='i' name='audioBitrate' access='readwrite'/>"
  "    <property type='i' name='videoBitrate' access='readwrite'/>"
  "  </interface>"
  "</node>";

static GVariant *handle_get_property (GDBusConnection  *connection,
		const gchar      *sender,
		const gchar      *object_path,
		const gchar      *interface_name,
		const gchar      *property_name,
		GError          **error,
		gpointer          user_data)
{
	DreamRTSPserver *s = user_data;

	fprintf (stderr, "[RTSPserver] "
	"handle_get_property (%p,\"%s\",\"%s\",\"%s\",\"%s\",(error),%p)\n",
		connection, sender, object_path, interface_name, property_name,
	user_data);

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
		GstElement *source = gst_bin_get_by_name(GST_BIN(s->pipeline), "dreamaudiosource");
		gint rate = 0;
		if (source)
			g_object_get (G_OBJECT (source), "bitrate", &rate, NULL);
		return g_variant_new_int32 (rate);
	}
	g_set_error (error,
		G_IO_ERROR,
	G_IO_ERROR_FAILED,
	"[RTSPserver] Invalid property '%s'",
	property_name);
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
	fprintf (stderr, "[RTSPserver] "
	"handle_set_property (%p,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",(error),%p)\n",
		 connection, sender, object_path, interface_name, property_name,
	  valstr, user_data);
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
	else
	{
		g_set_error (error,
			     G_IO_ERROR,
	       G_IO_ERROR_FAILED,
	       "[RTSPserver] Invalid property: '%s'",
	       property_name);
		return 0;
	} // unknown property
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
	fprintf (stderr, "[RTSPserver] "
	"handle_method_call (%p,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",(invocation),%p)\n",
		connection, sender, object_path, interface_name, method_name,
	paramstr, user_data);
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
	} // if it's HelloWorld

	// Default: No such method
	else
	{
		g_dbus_method_invocation_return_error (invocation,
							G_IO_ERROR,
					G_IO_ERROR_INVALID_ARGUMENT,
					"[RTSPserver] Invalid method: '%s'",
					method_name);
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

	// A bit of (optional) notification
	fprintf (stderr, "[RTSPserver] on_bus_acquired (%p, \"%s\", %p)\n",
		connection, name, user_data);

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
	fprintf (stderr, "[RTSPserver] on_name_acquired (%p, \"%s\", %p)\n",
		connection, name, user_data);
} // on_name_acquired

static void on_name_lost (GDBusConnection *connection,
	const gchar     *name,
	gpointer         user_data)
{
	DreamRTSPserver *s = user_data;

	fprintf (stderr, "[RTSPserver] on_name_lost (%p, \"%s\", %p)\n",
		connection, name, user_data);
	// Things seem to have gone badly wrong, so give up
	g_main_loop_quit (s->loop);
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
							g_print ("dreambox encoder stream ready at rtsp://127.0.0.1:8554/stream\n");
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
	s->clients_count--;
	g_print("media_unprepare, clients_count=%i\n", s->clients_count);
}

static void media_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media, gpointer user_data)
{
	g_print("media_configure\n");
	DreamRTSPserver *s = user_data;

	GstElement *element = gst_rtsp_media_get_element (media);

	g_mutex_lock (&s->rtsp_mutex);
	s->aappsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), "aappsrc");
	s->vappsrc = gst_bin_get_by_name_recurse_up (GST_BIN (element), "vappsrc");
	g_signal_connect (media, "unprepared", (GCallback) media_unprepare, s);
	g_object_set (s->aappsrc, "format", GST_FORMAT_TIME, NULL);
	g_object_set (s->vappsrc, "format", GST_FORMAT_TIME, NULL);
	s->rtsp_start_pts = s->rtsp_start_dts = GST_CLOCK_TIME_NONE;
	s->clients_count++;
	g_mutex_unlock (&s->rtsp_mutex);
}

static GstFlowReturn handover_payload (GstElement * appsink, gpointer user_data)
{
	DreamRTSPserver *s = user_data;

	GstElement * appsrc = NULL;
	if ( appsink == s->vappsink )
		appsrc = s->vappsrc;
	else if ( appsink == s->aappsink )
		appsrc = s->aappsrc;

	g_mutex_lock (&s->rtsp_mutex);
	if (appsrc) {
		g_print("#");
		GstSample *sample = gst_app_sink_pull_sample (GST_APP_SINK (appsink));
		GstBuffer *buffer = gst_sample_get_buffer (sample);
		GstCaps *caps = gst_sample_get_caps (sample);

		GstBuffer *tmp;
		tmp = gst_buffer_copy (buffer);
		g_print ("%" GST_TIME_FORMAT "\n", GST_TIME_ARGS (GST_BUFFER_DTS (tmp)));
		if (s->rtsp_start_pts == GST_CLOCK_TIME_NONE) {
			s->rtsp_start_pts = GST_BUFFER_PTS (tmp);
			s->rtsp_start_dts = GST_BUFFER_DTS (tmp);
		}
		if (GST_BUFFER_PTS (tmp) < s->rtsp_start_pts) GST_BUFFER_PTS (tmp) = 0;
		else GST_BUFFER_PTS (tmp) -= s->rtsp_start_pts;
		GST_BUFFER_DTS (tmp) -= s->rtsp_start_dts;

		gst_app_src_set_caps (GST_APP_SRC (appsrc), caps);
		gst_app_src_push_buffer (GST_APP_SRC (appsrc), tmp);
		gst_sample_unref (sample);
	}
	else
		g_print (".");
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
	s.mounts = gst_rtsp_server_get_mount_points (s.server);

	s.factory = gst_rtsp_media_factory_new ();
	gst_rtsp_media_factory_set_launch (s.factory, "( appsrc name=vappsrc ! h264parse ! rtph264pay name=pay0 pt=96   appsrc name=aappsrc ! aacparse ! rtpmp4apay name=pay1 pt=97 )");

	gst_rtsp_mount_points_add_factory (s.mounts, "/stream", s.factory);
	g_object_unref (s.mounts);

	g_signal_connect (s.factory, "media-configure", (GCallback) media_configure, &s);

	gst_rtsp_server_attach (s.server, NULL);

	g_mutex_init (&s.rtsp_mutex);

	s.loop = g_main_loop_new (NULL, FALSE);

	bus = gst_pipeline_get_bus (GST_PIPELINE (s.pipeline));
	gst_bus_add_signal_watch (bus);
	g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (message_cb), &s);
	gst_object_unref (GST_OBJECT (bus));

	g_main_loop_run (s.loop);

	gst_element_set_state (s.pipeline, GST_STATE_NULL);

	g_main_loop_unref (s.loop);
	gst_object_unref (s.pipeline);

	g_mutex_clear (&s.rtsp_mutex);

	g_bus_unown_name (owner_id);
	g_dbus_node_info_unref (introspection_data);
}
