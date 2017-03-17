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

#include "dreamrtspserver.h"
#include "gstdreamrtsp.h"

#define QUEUE_DEBUG \
		guint cur_bytes, cur_buf; \
		guint64 cur_time; \
		g_object_get (t->tstcpq, "current-level-bytes", &cur_bytes, NULL); \
		g_object_get (t->tstcpq, "current-level-buffers", &cur_buf, NULL); \
		g_object_get (t->tstcpq, "current-level-time", &cur_time, NULL);


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

static gboolean gst_set_inputmode(App *app, inputMode input_mode)
{
	if (!app->pipeline)
		return FALSE;
	if (!GST_IS_ELEMENT(app->asrc) ||
	    !GST_IS_ELEMENT(app->vsrc))
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
	GstCaps *oldcaps = NULL;
	GstCaps *newcaps = NULL;
	GstStructure *structure;
	gboolean ret = FALSE;

	if (!app->pipeline)
		return FALSE;
	if (!GST_IS_ELEMENT(app->vsrc))
		return FALSE;

	g_object_get (G_OBJECT (app->vsrc), "caps", &oldcaps, NULL);
	if (!GST_IS_CAPS(oldcaps))
		return FALSE;

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
	GstCaps *oldcaps = NULL;
	GstCaps *newcaps = NULL;
	GstStructure *structure;
	gboolean ret = FALSE;

	if (!app->pipeline)
		return FALSE;
	if (!GST_IS_ELEMENT(app->vsrc))
		return FALSE;

	g_object_get (G_OBJECT (app->vsrc), "caps", &oldcaps, NULL);
	if (!GST_IS_CAPS(oldcaps))
		return FALSE;

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

static gboolean gst_set_profile(App *app, int value)
{
	GstCaps *oldcaps = NULL;
	GstCaps *newcaps = NULL;
	GstStructure *structure;
	gboolean ret = FALSE;

	if (!app->pipeline)
		return FALSE;
	if (!GST_IS_ELEMENT(app->vsrc))
		return FALSE;

	g_object_get (G_OBJECT (app->vsrc), "caps", &oldcaps, NULL);
	if (!GST_IS_CAPS(oldcaps))
		return FALSE;

	GST_DEBUG("set profile %d... old caps %" GST_PTR_FORMAT, value, oldcaps);

	newcaps = gst_caps_make_writable(oldcaps);
	structure = gst_caps_steal_structure (newcaps, 0);
	if (!structure)
		goto out;

	if (value == 1)
		gst_structure_set (structure, "profile", G_TYPE_STRING, "high", NULL);
	else
		gst_structure_set (structure, "profile", G_TYPE_STRING, "main", NULL);

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

static gboolean gst_get_capsprop(App *app, GstElement *element, const gchar* prop_name, guint32 *value)
{
	GstCaps *caps = NULL;
	const GstStructure *structure;
	gboolean ret = FALSE;

	if (!app->pipeline)
		return FALSE;
	if (!GST_IS_ELEMENT(element))
		return FALSE;

	g_object_get (G_OBJECT (element), "caps", &caps, NULL);
	if (!GST_IS_CAPS(caps))
		return FALSE;

	if (gst_caps_is_empty(caps))
		goto out;

	GST_DEBUG ("current caps %" GST_PTR_FORMAT, caps);

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
	else if (g_strcmp0 (prop_name, "profile") == 0 && value)
	{
		const gchar* profile = gst_structure_get_string(structure, "profile");
		*value = 0;
		if (!profile)
			GST_WARNING("profile missing in caps! returned main profile");
		else if (g_strcmp0 (profile, "high") == 0)
			*value = 1;
		else if (g_strcmp0 (profile, "main") == 0)
			*value = 1;
		else
			GST_WARNING("unknown profile '%s' in caps! returned main profile", profile);
	}
	else if ((g_strcmp0 (prop_name, "width") == 0 || g_strcmp0 (prop_name, "height") == 0) && value)
	{
		if (!gst_structure_get_uint(structure, prop_name, value))
			*value = 0;
	}
	else
		goto out;

	GST_DEBUG ("%" GST_PTR_FORMAT"'s %s = %i", element, prop_name, *value);
	ret = TRUE;
out:
	if (GST_IS_CAPS(caps))
		gst_caps_unref(caps);
	return ret;
}

static void get_source_properties (App *app)
{
	SourceProperties *p = &app->source_properties;
	if (GST_IS_ELEMENT(app->asrc))
		g_object_get (G_OBJECT (app->asrc), "bitrate", &p->audioBitrate, NULL);
	if (GST_IS_ELEMENT(app->vsrc))
	{
		g_object_get (G_OBJECT (app->vsrc), "bitrate", &p->videoBitrate, NULL);
		g_object_get (G_OBJECT (app->vsrc), "gop-length", &p->gopLength, NULL);
		g_object_get (G_OBJECT (app->vsrc), "gop-scene", &p->gopOnSceneChange, NULL);
		g_object_get (G_OBJECT (app->vsrc), "open-gop", &p->openGop, NULL);
		g_object_get (G_OBJECT (app->vsrc), "bframes", &p->bFrames, NULL);
		g_object_get (G_OBJECT (app->vsrc), "pframes", &p->pFrames, NULL);
		g_object_get (G_OBJECT (app->vsrc), "slices", &p->slices, NULL);
		g_object_get (G_OBJECT (app->vsrc), "level", &p->level, NULL);

		gst_get_capsprop(app, app->vsrc, "width", &p->width);
		gst_get_capsprop(app, app->vsrc, "height", &p->height);
		gst_get_capsprop(app, app->vsrc, "framerate", &p->framerate);
		gst_get_capsprop(app, app->vsrc, "profile", &p->profile);
	}
}

static void apply_source_properties (App *app)
{
	SourceProperties *p = &app->source_properties;
	if (GST_IS_ELEMENT(app->asrc))
	{
		if (p->audioBitrate)
			g_object_set (G_OBJECT (app->asrc), "bitrate", p->audioBitrate, NULL);
	}
	if (GST_IS_ELEMENT(app->vsrc))
	{
		if (p->videoBitrate)
			g_object_set (G_OBJECT (app->vsrc), "bitrate", p->videoBitrate, NULL);

		g_object_set (G_OBJECT (app->vsrc), "gop-length", p->gopLength, NULL);
		g_object_set (G_OBJECT (app->vsrc), "gop-scene", p->gopOnSceneChange, NULL);
		g_object_set (G_OBJECT (app->vsrc), "open-gop", p->openGop, NULL);
		g_object_set (G_OBJECT (app->vsrc), "bframes", p->bFrames, NULL);
		g_object_set (G_OBJECT (app->vsrc), "pframes", p->pFrames, NULL);
		g_object_set (G_OBJECT (app->vsrc), "slices", p->slices, NULL);
		g_object_set (G_OBJECT (app->vsrc), "level", p->level, NULL);

		if (p->framerate)
			gst_set_framerate(app, p->framerate);
		if (p->width && p->height)
			gst_set_resolution(app,  p->width, p->height);
		gst_set_profile(app, p->profile);
	}
}

static gboolean gst_set_int_property (App *app, GstElement *source, const gchar* key, gint32 value, gboolean zero_allowed)
{
	if (!GST_IS_ELEMENT (source) || (!value && !zero_allowed))
		return FALSE;

	g_object_set (G_OBJECT (source), key, value, NULL);

	gint32 checkvalue = 0;

	g_object_get (G_OBJECT (source), key, &checkvalue, NULL);

	if (value != checkvalue)
		return FALSE;

	get_source_properties(app);
	return TRUE;
}

static gboolean gst_set_boolean_property (App *app, GstElement *source, const gchar* key, gboolean value)
{
	if (!GST_IS_ELEMENT (source))
		return FALSE;

	g_object_set (G_OBJECT (source), key, value, NULL);

	gboolean checkvalue = FALSE;

	g_object_get (G_OBJECT (source), key, &checkvalue, NULL);

	if (value != checkvalue)
		return FALSE;

	get_source_properties(app);
	return TRUE;
}

static gboolean gst_set_bitrate (App *app, GstElement *source, gint32 value)
{
	return gst_set_int_property(app, source, "bitrate", value, FALSE);
}

static gboolean gst_set_gop_length (App *app, gint32 value)
{
	return gst_set_int_property(app, app->vsrc, "gop-length", value, TRUE);
}

static gboolean gst_set_gop_on_scene_change (App *app, gboolean value)
{
	return gst_set_boolean_property(app, app->vsrc, "gop-scene", value);
}

static gboolean gst_set_open_gop (App *app, gboolean value)
{
	return gst_set_boolean_property(app, app->vsrc, "open-gop", value);
}

static gboolean gst_set_bframes (App *app, gint32 value)
{
	return gst_set_int_property(app, app->vsrc, "bframes", value, TRUE);
}

static gboolean gst_set_pframes (App *app, gint32 value)
{
	return gst_set_int_property(app, app->vsrc, "pframes", value, TRUE);
}

static gboolean gst_set_slices (App *app, gint32 value)
{
	return gst_set_int_property(app, app->vsrc, "slices", value, TRUE);
}

static gboolean gst_set_level (App *app, gint32 value)
{
	return gst_set_int_property(app, app->vsrc, "level", value, TRUE);
}

gboolean upstream_resume_transmitting(App *app)
{
	DreamTCPupstream *t = app->tcp_upstream;
	GST_INFO_OBJECT (app, "resuming normal transmission...");
	t->state = UPSTREAM_STATE_TRANSMITTING;
	send_signal (app, "upstreamStateChanged", g_variant_new("(i)", UPSTREAM_STATE_TRANSMITTING));
	t->overrun_counter = 0;
	t->overrun_period = GST_CLOCK_TIME_NONE;
	t->id_signal_waiting = 0;
	if (t->id_signal_keepalive)
		g_source_remove (t->id_signal_keepalive);
	t->id_signal_keepalive = 0;
	return G_SOURCE_REMOVE;
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

	if (g_strcmp0 (property_name, "sourceState") == 0)
	{
		if (app->pipeline)
		{
			GstState state = GST_STATE_VOID_PENDING;
			gst_element_get_state (GST_ELEMENT(app->pipeline), &state, NULL, 1*GST_USECOND);
			return g_variant_new_int32 ((int)state);
		}
	}
	else if (g_strcmp0 (property_name, "upstreamState") == 0)
	{
		if (app->tcp_upstream)
			return g_variant_new_int32 (app->tcp_upstream->state);
	}
	else if (g_strcmp0 (property_name, "hlsState") == 0)
	{
		if (app->hls_server)
			return g_variant_new_int32 (app->hls_server->state);
	}
	else if (g_strcmp0 (property_name, "inputMode") == 0)
	{
		inputMode input_mode = -1;
		if (GST_IS_ELEMENT(app->asrc))
		{
			g_object_get (G_OBJECT (app->asrc), "input_mode", &input_mode, NULL);
			return g_variant_new_int32 (input_mode);
		}
	}
	else if (g_strcmp0 (property_name, "rtspClientCount") == 0)
	{
		if (app->rtsp_server)
			return g_variant_new_int32 (g_list_length(app->rtsp_server->clients_list));
	}
	else if (g_strcmp0 (property_name, "uriParameters") == 0)
	{
		if (app->rtsp_server)
			return g_variant_new_string (app->rtsp_server->uri_parameters);
	}
	else if (g_strcmp0 (property_name, "audioBitrate") == 0)
	{
		gint rate = 0;
		if (GST_IS_ELEMENT(app->asrc))
		{
			g_object_get (G_OBJECT (app->asrc), "bitrate", &rate, NULL);
			return g_variant_new_int32 (rate);
		}
	}
	else if (g_strcmp0 (property_name, "videoBitrate") == 0)
	{
		gint rate = 0;
		if (GST_IS_ELEMENT(app->vsrc))
		{
			g_object_get (G_OBJECT (app->vsrc), "bitrate", &rate, NULL);
			return g_variant_new_int32 (rate);
		}
	}
	else if (g_strcmp0 (property_name, "gopLength") == 0)
	{
		gint length = 0;
		if (GST_IS_ELEMENT(app->vsrc))
		{
			g_object_get (G_OBJECT (app->vsrc), "gop-length", &length, NULL);
			return g_variant_new_int32 (length);
		}
	}
	else if (g_strcmp0 (property_name, "gopOnSceneChange") == 0)
	{
		gboolean enabled = FALSE;
		if (GST_IS_ELEMENT(app->vsrc))
		{
			g_object_get (G_OBJECT (app->vsrc), "gop-scene", &enabled, NULL);
			return g_variant_new_boolean (enabled);
		}
	}
	else if (g_strcmp0 (property_name, "openGop") == 0)
	{
		gboolean enabled = FALSE;
		if (GST_IS_ELEMENT(app->vsrc))
		{
			g_object_get (G_OBJECT (app->vsrc), "open-gop", &enabled, NULL);
			return g_variant_new_boolean (enabled);
		}
	}
	else if (g_strcmp0 (property_name, "bFrames") == 0)
	{
		gint bframes = 0;
		if (GST_IS_ELEMENT(app->vsrc))
		{
			g_object_get (G_OBJECT (app->vsrc), "bframes", &bframes, NULL);
			return g_variant_new_int32 (bframes);
		}
	}
	else if (g_strcmp0 (property_name, "pFrames") == 0)
	{
		gint pframes = 0;
		if (GST_IS_ELEMENT(app->vsrc))
		{
			g_object_get (G_OBJECT (app->vsrc), "pframes", &pframes, NULL);
			return g_variant_new_int32 (pframes);
		}
	}
	else if (g_strcmp0 (property_name, "slices") == 0)
	{
		gint slices = 0;
		if (GST_IS_ELEMENT(app->vsrc))
		{
			g_object_get (G_OBJECT (app->vsrc), "slices", &slices, NULL);
			return g_variant_new_int32 (slices);
		}
	}
	else if (g_strcmp0 (property_name, "level") == 0)
	{
		gint level = 0;
		if (GST_IS_ELEMENT(app->vsrc))
		{
			g_object_get (G_OBJECT (app->vsrc), "level", &level, NULL);
			return g_variant_new_int32 (level);
		}
	}
	else if (g_strcmp0 (property_name, "width") == 0 || g_strcmp0 (property_name, "height") == 0 || g_strcmp0 (property_name, "framerate") == 0 || g_strcmp0 (property_name, "profile") == 0)
	{
		guint32 value;
		if (gst_get_capsprop(app, app->vsrc, property_name, &value))
			return g_variant_new_int32(value);
		GST_WARNING("can't handle_get_property name=%s", property_name);
	}
	else if (g_strcmp0 (property_name, "autoBitrate") == 0)
	{
		if (app->tcp_upstream)
			return g_variant_new_boolean(app->tcp_upstream->auto_bitrate);
	}
	else if (g_strcmp0 (property_name, "path") == 0)
	{
		if (app->rtsp_server)
		        return g_variant_new_string (app->rtsp_server->rtsp_ts_path);
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
		if (gst_set_bitrate (app, app->asrc, g_variant_get_int32 (value)))
			return 1;
	}
	else if (g_strcmp0 (property_name, "videoBitrate") == 0)
	{
		if (gst_set_bitrate (app, app->vsrc, g_variant_get_int32 (value)))
			return 1;
	}
	else if (g_strcmp0 (property_name, "gopLength") == 0)
	{
		if (gst_set_gop_length (app, g_variant_get_int32 (value)))
			return 1;
	}
	else if (g_strcmp0 (property_name, "gopOnSceneChange") == 0)
	{
		if (gst_set_gop_on_scene_change (app, g_variant_get_boolean (value)))
			return 1;
	}
	else if (g_strcmp0 (property_name, "openGop") == 0)
	{
		if (gst_set_open_gop (app, g_variant_get_boolean (value)))
			return 1;
	}
	else if (g_strcmp0 (property_name, "bFrames") == 0)
	{
		if (gst_set_bframes (app, g_variant_get_int32 (value)))
			return 1;
	}
	else if (g_strcmp0 (property_name, "pFrames") == 0)
	{
		if (gst_set_pframes (app, g_variant_get_int32 (value)))
			return 1;
	}
	else if (g_strcmp0 (property_name, "slices") == 0)
	{
		if (gst_set_slices (app, g_variant_get_int32 (value)))
			return 1;
	}
	else if (g_strcmp0 (property_name, "level") == 0)
	{
		if (gst_set_level (app, g_variant_get_int32 (value)))
			return 1;
	}
	else if (g_strcmp0 (property_name, "framerate") == 0)
	{
		if (gst_set_framerate(app, g_variant_get_int32 (value)))
			return 1;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "[RTSPserver] can't set property '%s' to %d", property_name, g_variant_get_int32 (value));
		return 0;
	}
	else if (g_strcmp0 (property_name, "profile") == 0)
	{
		if (gst_set_profile(app, g_variant_get_int32 (value)))
			return 1;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "[RTSPserver] can't set property '%s' to %d", property_name, g_variant_get_int32 (value));
		return 0;
	}
	else if (g_strcmp0 (property_name, "autoBitrate") == 0)
	{
		if (app->tcp_upstream)
		{
			gboolean enable = g_variant_get_boolean(value);
			if (app->tcp_upstream->state == UPSTREAM_STATE_OVERLOAD)
				upstream_resume_transmitting(app);
			app->tcp_upstream->auto_bitrate = enable;
			return 1;
		}
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

			if (state == TRUE && app->rtsp_server->state >= RTSP_STATE_DISABLED)
				result = enable_rtsp_server(app, path, port, user, pass);
			else if (state == FALSE && app->rtsp_server->state >= RTSP_STATE_IDLE)
                        {
				result = disable_rtsp_server(app);
				if (app->tcp_upstream->state == UPSTREAM_STATE_DISABLED && app->hls_server->state == HLS_STATE_DISABLED)
				{
					destroy_pipeline(app);
					create_source_pipeline(app);
				}
                        }
		}
		g_dbus_method_invocation_return_value (invocation,  g_variant_new ("(b)", result));
	}
	else if (g_strcmp0 (method_name, "enableHLS") == 0)
	{
		gboolean result = FALSE;
		if (app->pipeline)
		{
			gboolean state;
			guint32 port;
			const gchar *user, *pass;

			g_variant_get (parameters, "(bu&s&s)", &state, &port, &user, &pass);
			GST_DEBUG("app->pipeline=%p, enableHLS state=%i port=%i user=%s pass=%s", app->pipeline, state, port, user, pass);

			if (state == TRUE && app->hls_server->state >= HLS_STATE_DISABLED)
				result = enable_hls_server(app, port, user, pass);
			else if (state == FALSE && app->hls_server->state >= HLS_STATE_IDLE)
                        {
				result = disable_hls_server(app);
				if (app->tcp_upstream->state == UPSTREAM_STATE_DISABLED && app->rtsp_server->state == RTSP_STATE_DISABLED)
				{
					destroy_pipeline(app);
					create_source_pipeline(app);
				}
                        }
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
			GST_DEBUG("app->pipeline=%p, enableUpstream state=%i host=%s port=%i token=%s (currently in state %u)", app->pipeline, state, upstream_host, upstream_port, token, app->tcp_upstream->state);

			if (state == TRUE && app->tcp_upstream->state == UPSTREAM_STATE_DISABLED)
				result = enable_tcp_upstream(app, upstream_host, upstream_port, token);
			else if (state == FALSE && app->tcp_upstream->state >= UPSTREAM_STATE_CONNECTING)
			{
				result = disable_tcp_upstream(app);
				if (app->rtsp_server->state == RTSP_STATE_DISABLED)
				{
					destroy_pipeline(app);
					create_source_pipeline(app);
				}
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
		handle_set_property,
		{ 0, }
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

// 	DREAMRTSPSERVER_LOCK (app);
	GST_TRACE_OBJECT (app, "message %" GST_PTR_FORMAT "", message);
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
				send_signal (app, "sourceStateChanged", g_variant_new("(i)", (int) new_state));
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
// 					DREAMRTSPSERVER_UNLOCK (app);
					disable_tcp_upstream(app);
					destroy_pipeline(app);
				}
				if (err->code == GST_RESOURCE_ERROR_WRITE)
				{
					send_signal (app, "upstreamStateChanged", g_variant_new("(i)", UPSTREAM_STATE_FAILED));
					GST_INFO ("element %s: %s -> this means PEER DISCONNECTED", name, err->message);
					GST_DEBUG ("Additional ERROR debug info: %s", debug);
// 					DREAMRTSPSERVER_UNLOCK (app);
					disable_tcp_upstream(app);
					if (app->rtsp_server->state == RTSP_STATE_DISABLED)
					{
						destroy_pipeline(app);
						create_source_pipeline(app);
					}
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
#if 1
			if (err->domain == GST_STREAM_ERROR && err->code == GST_STREAM_ERROR_ENCODE)
			{
				GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"dreamrtspserver-encode-error");
				g_main_loop_quit (app->loop);
				return FALSE;
			}
#endif
			g_error_free (err);
			g_free (debug);
			g_free (name);
			break;
		}
		case GST_MESSAGE_EOS:
			g_print ("Got EOS\n");
// 			DREAMRTSPSERVER_UNLOCK (app);
			g_main_loop_quit (app->loop);
			return FALSE;
		default:
			break;
	}
// 	DREAMRTSPSERVER_UNLOCK (app);
	return TRUE;
}

static void media_unprepare (GstRTSPMedia * media, gpointer user_data)
{
	App *app = user_data;
	DreamRTSPserver *r = app->rtsp_server;
	GST_INFO("no more clients -> media unprepared!");

// 	DREAMRTSPSERVER_LOCK (app);
	if (media == r->es_media)
	{
		r->es_media = NULL;
		r->es_aappsrc = r->es_vappsrc = NULL;
	}
	else if (media == r->ts_media)
	{
		r->ts_media = NULL;
		r->ts_appsrc = NULL;
	}
	if (!r->es_media && !r->ts_media)
	{
		if (app->tcp_upstream->state == UPSTREAM_STATE_DISABLED && app->hls_server->state == HLS_STATE_DISABLED)
			halt_source_pipeline(app);
		if (r->state == RTSP_STATE_RUNNING)
		{
			GST_DEBUG ("set RTSP_STATE_IDLE");
			send_signal (app, "rtspStateChanged", g_variant_new("(i)", RTSP_STATE_IDLE));
			r->state = RTSP_STATE_IDLE;
		}
	}
// 	DREAMRTSPSERVER_UNLOCK (app);
}

static void client_closed (GstRTSPClient * client, gpointer user_data)
{
	App *app = user_data;
	app->rtsp_server->clients_list = g_list_remove(g_list_first (app->rtsp_server->clients_list), client);
	gint no_clients = g_list_length(app->rtsp_server->clients_list);
	GST_INFO("client_closed  (number of clients: %i)", no_clients);
	send_signal (app, "rtspClientCountChanged", g_variant_new("(is)", no_clients, ""));
}

static void client_connected (GstRTSPServer * server, GstRTSPClient * client, gpointer user_data)
{
	App *app = user_data;
	app->rtsp_server->clients_list = g_list_append(app->rtsp_server->clients_list, client);
	const gchar *ip = gst_rtsp_connection_get_ip (gst_rtsp_client_get_connection (client));
	gint no_clients = g_list_length(app->rtsp_server->clients_list);
	GST_INFO("client_connected %" GST_PTR_FORMAT " from %s  (number of clients: %i)", client, ip, no_clients);
	g_signal_connect (client, "closed", (GCallback) client_closed, app);
	send_signal (app, "rtspClientCountChanged", g_variant_new("(is)", no_clients, ip));
}

static void media_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media, gpointer user_data)
{
	App *app = user_data;
	DreamRTSPserver *r = app->rtsp_server;
	DREAMRTSPSERVER_LOCK (app);

	if (GST_DREAM_RTSP_MEDIA_FACTORY (factory) == r->es_factory)
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
	else if (GST_DREAM_RTSP_MEDIA_FACTORY (factory) == r->ts_factory)
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
	send_signal (app, "rtspStateChanged", g_variant_new("(i)", RTSP_STATE_RUNNING));
	GST_DEBUG ("set RTSP_STATE_RUNNING");
	start_rtsp_pipeline(app);
	DREAMRTSPSERVER_UNLOCK (app);
}

static void uri_parametrized (GstDreamRTSPMediaFactory * factory, gchar *parameters, gpointer user_data)
{
	App *app = user_data;
	GST_INFO_OBJECT (app, "parametrized uri query: '%s'", parameters);
	app->rtsp_server->uri_parameters = g_strdup(parameters);
	send_signal (app, "uriParametersChanged", g_variant_new("(s)", app->rtsp_server->uri_parameters));
}

static GstPadProbeReturn cancel_waiting_probe (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	DreamTCPupstream *t = app->tcp_upstream;
	if (((info->type & GST_PAD_PROBE_TYPE_BUFFER) && GST_IS_BUFFER(GST_PAD_PROBE_INFO_BUFFER(info))) ||
	     ((info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST) && gst_buffer_list_length(GST_PAD_PROBE_INFO_BUFFER_LIST(info))))
	{
		QUEUE_DEBUG;
		GST_LOG_OBJECT (app, "cancel upstream_set_waiting timeout because data flow was restored! queue properties current-level-bytes=%d current-level-buffers=%d current-level-time=%" GST_TIME_FORMAT "",
				  cur_bytes, cur_buf, GST_TIME_ARGS(cur_time));
		if (t->id_signal_waiting)
			g_source_remove (t->id_signal_waiting);
		t->id_signal_waiting = 0;
		if (t->id_signal_keepalive)
			g_source_remove (t->id_signal_keepalive);
		t->id_signal_keepalive = 0;
		if (t->id_signal_overrun == 0)
			t->id_signal_overrun = g_signal_connect (t->tstcpq, "overrun", G_CALLBACK (queue_overrun), app);
		t->id_resume = 0;
		return GST_PAD_PROBE_REMOVE;
	}
	else
		GST_WARNING_OBJECT(app, "probed unhandled %" GST_PTR_FORMAT ", dataflow not restored?", info->data);
	return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn bitrate_measure_probe (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	DreamTCPupstream *t = app->tcp_upstream;
	GstClockTime now = gst_clock_get_time (app->clock);
	GstBuffer *buffer = NULL;
	guint idx = 0, num_buffers = 1;
	do {
		if (info->type & GST_PAD_PROBE_TYPE_BUFFER)
		{
			buffer = GST_PAD_PROBE_INFO_BUFFER (info);
		}
		else if (info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST)
		{
			GstBufferList *bufferlist = GST_PAD_PROBE_INFO_BUFFER_LIST (info);
			num_buffers = gst_buffer_list_length (bufferlist);
			buffer = gst_buffer_list_get (bufferlist, idx);
		}
		if (GST_IS_BUFFER(buffer))
			t->bitrate_sum += gst_buffer_get_size (buffer);
		idx++;
	} while (idx < num_buffers);

	QUEUE_DEBUG;
	GST_TRACE_OBJECT(app, "probetype=%i num_buffers=%i data=%" GST_PTR_FORMAT " size was=%zu bitrate_sum=%zu now=%" GST_TIME_FORMAT " avg at %" GST_TIME_FORMAT " queue properties current-level-bytes=%d current-level-buffers=%d current-level-time=%" GST_TIME_FORMAT "",
			info->type, num_buffers, info->data, gst_buffer_get_size (buffer), t->bitrate_sum, GST_TIME_ARGS(now), GST_TIME_ARGS(t->measure_start+BITRATE_AVG_PERIOD), cur_bytes, cur_buf, GST_TIME_ARGS(cur_time));
	if (now > t->measure_start+BITRATE_AVG_PERIOD)
	{
		gint bitrate = t->bitrate_sum*8/GST_TIME_AS_MSECONDS(BITRATE_AVG_PERIOD);
		t->bitrate_avg ? (t->bitrate_avg = (t->bitrate_avg+bitrate)/2) : (t->bitrate_avg = bitrate);
		send_signal (app, "tcpBitrate", g_variant_new("(i)", bitrate));
		t->measure_start = now;
		t->bitrate_sum = 0;
	}
	return GST_PAD_PROBE_OK;
}

gboolean upstream_keep_alive (App *app)
{
	GstBuffer *buf = gst_buffer_new_allocate (NULL, TS_PACK_SIZE, NULL);
	gst_buffer_memset (buf, 0, 0x00, TS_PACK_SIZE);
	GstPad * srcpad = gst_element_get_static_pad (app->tcp_upstream->tstcpq, "src");

	GstState state;
	gst_element_get_state (app->tcp_upstream->tcpsink, &state, NULL, 10*GST_SECOND);
	GST_INFO_OBJECT(app, "tcpsink's state=%s", gst_element_state_get_name (state));
	gst_element_get_state (app->tcp_upstream->tstcpq, &state, NULL, 10*GST_SECOND);
	GST_INFO_OBJECT(app, "tstcpq's state=%s", gst_element_state_get_name (state));

	if ( state == GST_STATE_PAUSED )
	{
		GstStateChangeReturn sret = gst_element_set_state (app->tcp_upstream->tcpsink, GST_STATE_PLAYING);
		GST_DEBUG_OBJECT(app, "gst_element_set_state (tcpsink, GST_STATE_PLAYING) = %i", sret);
		sret = gst_element_set_state (app->tcp_upstream->tstcpq, GST_STATE_PLAYING);
		GST_DEBUG_OBJECT(app, "gst_element_set_state (tstcpq, GST_STATE_PLAYING) = %i", sret);
		GST_INFO ("injecting keepalive %" GST_PTR_FORMAT " on pad %s:%s", buf, GST_DEBUG_PAD_NAME (srcpad));
		gst_pad_push (srcpad, gst_buffer_ref(buf));
		sret = gst_element_set_state (app->tcp_upstream->tcpsink, GST_STATE_PAUSED);
		GST_DEBUG_OBJECT(app, "gst_element_set_state (tcpsink, GST_STATE_PAUSED) = %i", sret);
		sret = gst_element_set_state (app->tcp_upstream->tstcpq, GST_STATE_PAUSED);
		GST_DEBUG_OBJECT(app, "gst_element_set_state (tstcpq, GST_STATE_PAUSED) = %i", sret);
	}

	return G_SOURCE_REMOVE;
}

gboolean upstream_set_waiting (App *app)
{
	DREAMRTSPSERVER_LOCK (app);
	DreamTCPupstream *t = app->tcp_upstream;
	t->overrun_counter = 0;
	t->overrun_period = GST_CLOCK_TIME_NONE;
	t->state = UPSTREAM_STATE_WAITING;
	g_object_set (t->tcpsink, "max-lateness", G_GINT64_CONSTANT(1)*GST_SECOND, NULL);
	send_signal (app, "upstreamStateChanged", g_variant_new("(i)", UPSTREAM_STATE_WAITING));
	g_signal_connect (t->tstcpq, "underrun", G_CALLBACK (queue_underrun), app);
	GstPad *sinkpad = gst_element_get_static_pad (t->tcpsink, "sink");
	if (t->id_resume)
	{
		gst_pad_remove_probe (sinkpad, t->id_resume);
		t->id_resume = 0;
	}
	if (t->id_bitrate_measure)
	{
		gst_pad_remove_probe (sinkpad, t->id_bitrate_measure);
		t->id_bitrate_measure = 0;
	}
	send_signal (app, "tcpBitrate", g_variant_new("(i)", 0));
	gst_object_unref (sinkpad);
	pause_source_pipeline(app);
	t->id_signal_waiting = 0;
	t->id_signal_keepalive = g_timeout_add_seconds (5, (GSourceFunc) upstream_keep_alive, app);
	DREAMRTSPSERVER_UNLOCK (app);
	return G_SOURCE_REMOVE;
}

static void queue_underrun (GstElement * queue, gpointer user_data)
{
	App *app = user_data;
	DreamTCPupstream *t = app->tcp_upstream;
	QUEUE_DEBUG;
	GST_DEBUG_OBJECT (app, "queue underrun! properties: current-level-bytes=%d current-level-buffers=%d current-level-time=%" GST_TIME_FORMAT "", cur_bytes, cur_buf, GST_TIME_ARGS(cur_time));
	if (queue == t->tstcpq && app->rtsp_server->state != RTSP_STATE_RUNNING)
	{
		if (unpause_source_pipeline(app))
		{
			DREAMRTSPSERVER_LOCK (app);
// 			g_object_set (G_OBJECT (t->tstcpq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);
			g_object_set (t->tcpsink, "max-lateness", G_GINT64_CONSTANT(-1), NULL);
			g_signal_handlers_disconnect_by_func (queue, G_CALLBACK (queue_underrun), app);
			t->id_signal_overrun = g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), app);
			t->state = UPSTREAM_STATE_TRANSMITTING;
			send_signal (app, "upstreamStateChanged", g_variant_new("(i)", UPSTREAM_STATE_TRANSMITTING));
			if (t->id_bitrate_measure == 0)
			{
				GstPad *sinkpad = gst_element_get_static_pad (t->tcpsink, "sink");
				t->id_bitrate_measure = gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_BUFFER_LIST, (GstPadProbeCallback) bitrate_measure_probe, app, NULL);
				gst_object_unref (sinkpad);
			}
			t->measure_start = gst_clock_get_time (app->clock);
			t->bitrate_sum = t->bitrate_avg = 0;
			if (t->overrun_period == GST_CLOCK_TIME_NONE)
				t->overrun_period = gst_clock_get_time (app->clock);
			DREAMRTSPSERVER_UNLOCK (app);
		}
	}
}

static void queue_overrun (GstElement * queue, gpointer user_data)
{
	App *app = user_data;
	DreamTCPupstream *t = app->tcp_upstream;
	DREAMRTSPSERVER_LOCK (app);
	if (queue == t->tstcpq/* && app->rtsp_server->state != RTSP_STATE_IDLE*/) //!!!TODO
	{
		QUEUE_DEBUG;
		GST_DEBUG_OBJECT(app, "%" GST_PTR_FORMAT " overrun! properties: current-level-bytes=%d current-level-buffers=%d current-level-time=%" GST_TIME_FORMAT " rtsp_server->state=%i", queue, cur_bytes, cur_buf, GST_TIME_ARGS(cur_time), app->rtsp_server->state);
		GstClockTime now = gst_clock_get_time (app->clock);
		if (t->state == UPSTREAM_STATE_CONNECTING)
		{
			GST_DEBUG_OBJECT (queue, "initial queue overrun after connect");
// 			g_object_set (G_OBJECT (t->tstcpq), "leaky", 0, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, "min-threshold-buffers", 0, NULL);
			g_signal_handlers_disconnect_by_func(t->tstcpq, G_CALLBACK (queue_overrun), app);
			t->id_signal_overrun = 0;
			DREAMRTSPSERVER_UNLOCK (app);
			upstream_set_waiting (app);
			return;
		}
		else if (t->state == UPSTREAM_STATE_TRANSMITTING)
		{
			if (t->id_signal_waiting)
			{
				g_signal_handlers_disconnect_by_func(t->tstcpq, G_CALLBACK (queue_overrun), app);
				t->id_signal_overrun = 0;
				GST_DEBUG_OBJECT (queue, "disconnect overrun callback and wait for timeout or for buffer flow!");
				DREAMRTSPSERVER_UNLOCK (app);
				return;
			}
			t->overrun_counter++;
			GST_DEBUG_OBJECT (queue, "queue overrun during transmit... %i (max %i) overruns within %" GST_TIME_FORMAT "", t->overrun_counter, MAX_OVERRUNS, GST_TIME_ARGS (now-t->overrun_period));
			if (now > t->overrun_period+OVERRUN_TIME)
			{
				t->overrun_counter = 0;
				t->overrun_period = now;
			}
			if (t->overrun_counter >= MAX_OVERRUNS)
			{
				if (t->auto_bitrate)
				{
					t->state = UPSTREAM_STATE_ADJUSTING;
					send_signal (app, "upstreamStateChanged", g_variant_new("(i)", UPSTREAM_STATE_OVERLOAD));
					auto_adjust_bitrate (app);
					t->overrun_period = now;
				}
				else
				{
					t->state = UPSTREAM_STATE_OVERLOAD;
					send_signal (app, "upstreamStateChanged", g_variant_new("(i)", UPSTREAM_STATE_OVERLOAD));
					GST_DEBUG_OBJECT (queue, "auto overload handling disabled, go into UPSTREAM_STATE_OVERLOAD");
					if (t->id_signal_waiting)
						g_source_remove (t->id_signal_waiting);
					t->id_signal_waiting = g_timeout_add_seconds (RESUME_DELAY, (GSourceFunc) upstream_resume_transmitting, app);
				}
			}
			else
			{
				GST_DEBUG_OBJECT (queue, "SET upstream_set_waiting timeout!");
				GstPad *sinkpad = gst_element_get_static_pad (t->tcpsink, "sink");
				t->id_resume = gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_BUFFER_LIST, (GstPadProbeCallback) cancel_waiting_probe, app, NULL);
				gst_object_unref (sinkpad);
				if (t->id_signal_waiting)
					g_source_remove (t->id_signal_waiting);
				t->id_signal_waiting = g_timeout_add_seconds (5, (GSourceFunc) upstream_set_waiting, app);
			}
		}
		else if (t->state == UPSTREAM_STATE_OVERLOAD)
		{
			t->overrun_counter++;
			if (t->id_signal_waiting)
				g_source_remove (t->id_signal_waiting);
			t->id_signal_waiting = g_timeout_add_seconds (5, (GSourceFunc) upstream_resume_transmitting, app);
			GST_DEBUG_OBJECT (queue, "still in UPSTREAM_STATE_OVERLOAD overrun_counter=%i, reset resume transmit timeout!", t->overrun_counter);
		}
		else if (t->state == UPSTREAM_STATE_ADJUSTING)
		{
			if (now < t->overrun_period+BITRATE_AVG_PERIOD)
			{
				GST_DEBUG_OBJECT (queue, "still in grace period, waiting for bitrate adjustment to take effect. %"G_GUINT64_FORMAT" ms remaining", GST_TIME_AS_MSECONDS(t->overrun_period+BITRATE_AVG_PERIOD-now));
			}
			else
			{
				t->overrun_counter++;
				if (t->overrun_counter < MAX_OVERRUNS)
					GST_DEBUG_OBJECT (queue, "still waiting for bitrate adjustment to take effect. overrun_counter=%i", t->overrun_counter);
				else
				{
					GST_DEBUG_OBJECT (queue, "max overruns %i hit again while auto adjusting. -> RE-ADJUST!", t->overrun_counter);
					t->overrun_counter = 0;
					auto_adjust_bitrate (app);
					t->overrun_period = now;
				}
			}
		}
	}
	DREAMRTSPSERVER_UNLOCK (app);
}

static void auto_adjust_bitrate(App *app)
{
	DreamTCPupstream *t = app->tcp_upstream;
	get_source_properties (app);
	SourceProperties *p = &app->source_properties;
	GST_DEBUG_OBJECT (app, "auto overload handling: reduce bitrate from audioBitrate=%i videoBitrate=%i to fit network bandwidth=%i kbit/s", p->audioBitrate, p->videoBitrate, t->bitrate_avg);
	if (p->audioBitrate > 96)
		p->audioBitrate = p->audioBitrate*0.8;
	p->videoBitrate = (t->bitrate_avg - p->audioBitrate) * 0.8;
	GST_INFO_OBJECT (app, "auto overload handling: newAudioBitrate=%i newVideoBitrate=%i newTotalBitrate~%i kbit/s", p->audioBitrate, p->videoBitrate, p->audioBitrate+p->videoBitrate);
	apply_source_properties(app);
	if (t->id_signal_waiting)
		g_source_remove (t->id_signal_waiting);
	t->id_signal_waiting = g_timeout_add_seconds (RESUME_DELAY, (GSourceFunc) upstream_resume_transmitting, app);
	t->overrun_counter = 0;
}

static GstFlowReturn handover_payload (GstElement * appsink, gpointer user_data)
{
	App *app = user_data;
	DreamRTSPserver *r = app->rtsp_server;

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

		GST_LOG_OBJECT(appsink, "%" GST_PTR_FORMAT" @ %" GST_PTR_FORMAT, buffer, appsrc);
		if (r->rtsp_start_pts == GST_CLOCK_TIME_NONE) {
			if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))
			{
				GST_LOG("GST_BUFFER_FLAG_DELTA_UNIT dropping!");
				gst_sample_unref(sample);
//				DREAMRTSPSERVER_UNLOCK (app);
				return GST_FLOW_OK;
			}
			else if (appsink == r->vappsink || appsink == r->tsappsink)
			{
				DREAMRTSPSERVER_LOCK (app);
				r->rtsp_start_pts = GST_BUFFER_PTS (buffer);
				r->rtsp_start_dts = GST_BUFFER_DTS (buffer);
				GST_LOG_OBJECT(appsink, "frame is IFRAME! set rtsp_start_pts=%" GST_TIME_FORMAT " rtsp_start_dts=%" GST_TIME_FORMAT " @ %"GST_PTR_FORMAT"", GST_TIME_ARGS (GST_BUFFER_PTS (buffer)), GST_TIME_ARGS (GST_BUFFER_DTS (buffer)), appsrc);
				DREAMRTSPSERVER_UNLOCK (app);
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
			GST_DEBUG("CAPS changed! %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, oldcaps, caps);
			gst_app_src_set_caps (appsrc, caps);
		}
		gst_app_src_push_buffer (appsrc, gst_buffer_ref(buffer));
	}
	else
	{
		if ( gst_debug_category_get_threshold (dreamrtspserver_debug) >= GST_LEVEL_LOG)
			GST_TRACE_OBJECT(appsink, "no rtsp clients, discard payload!");
// 		else
// 			g_print (".");
	}
	gst_sample_unref (sample);

	return GST_FLOW_OK;
}

gboolean assert_state(App *app, GstElement *element, GstState state)
{
	GstStateChangeReturn sret;
	GST_DEBUG_OBJECT(app, "setting %" GST_PTR_FORMAT"'s state to %s", element, gst_element_state_get_name (state));
	GstState pipeline_state, current_state;
	sret = gst_element_set_state (element, state);
	gchar *elementname = gst_element_get_name(element);
	gchar *dotfilename = g_strdup_printf ("assert_state_%s_to_%s_%i", elementname,gst_element_state_get_name (state), sret);
	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline), GST_DEBUG_GRAPH_SHOW_ALL, dotfilename);
	g_free (elementname);
	switch (sret) {
		case GST_STATE_CHANGE_SUCCESS:
		{
			GST_DEBUG_OBJECT(app, "GST_STATE_CHANGE_SUCCESS on setting %" GST_PTR_FORMAT"'s state to %s ", element, gst_element_state_get_name (state));
			return TRUE;
		}
		case GST_STATE_CHANGE_FAILURE:
		{
			GST_ERROR_OBJECT (app, "GST_STATE_CHANGE_FAILURE when trying to change %" GST_PTR_FORMAT"'s state to %s", element, gst_element_state_get_name (state));
			return FALSE;
		}
		case GST_STATE_CHANGE_ASYNC:
		{
			watchdog_ping (app);
			gboolean fail = FALSE;
			GST_LOG_OBJECT(app, "GST_STATE_CHANGE_ASYNC %" GST_PTR_FORMAT"", element);
			GstClockTime timeout = /*(element == app->pipeline) ?*/ 4 * GST_SECOND/* : GST_MSECOND*/;
			if (element == GST_ELEMENT(app->pipeline))
			{
				gst_element_get_state (GST_ELEMENT(app->pipeline), &pipeline_state, NULL, timeout);
				GST_LOG_OBJECT(app, "GST_STATE_CHANGE_ASYNC got pipeline_state=%s", gst_element_state_get_name (state));
				if (pipeline_state != state)
				{
					GValue item = G_VALUE_INIT;
					GstIterator* iter = gst_bin_iterate_elements(GST_BIN(app->pipeline));
					while (GST_ITERATOR_OK == gst_iterator_next(iter, (GValue*)&item))
					{
						GstElement *elem = g_value_get_object(&item);
						GstElementFactory *factory = gst_element_get_factory (elem);
						gst_element_get_state (elem, &current_state, NULL, GST_MSECOND);
						if (current_state != state && (element == app->pipeline || element == elem))
						{
							if (element == app->pipeline)
							{
								if (gst_element_factory_list_is_type (factory, GST_ELEMENT_FACTORY_TYPE_SINK))
									GST_LOG_OBJECT(app, "GST_STATE_CHANGE_ASYNC %" GST_PTR_FORMAT"'s state=%s (this is a sink element, so don't worry...)", elem, gst_element_state_get_name (current_state));
								else
								{
									GST_WARNING_OBJECT(app, "GST_STATE_CHANGE_ASYNC %" GST_PTR_FORMAT"'s state=%s -> FAIL", elem, gst_element_state_get_name (current_state));
									fail = TRUE;
								}
							}
						}
						else
							GST_LOG_OBJECT(app, "GST_STATE_CHANGE_ASYNC %" GST_PTR_FORMAT"'s state=%s", elem, gst_element_state_get_name (current_state));
					}
					gst_iterator_free(iter);
					if (fail)
					{
						GST_ERROR_OBJECT (app, "pipeline didn't complete GST_STATE_CHANGE_ASYNC to %s within %" GST_TIME_FORMAT ", currently in %s", gst_element_state_get_name (state), GST_TIME_ARGS(timeout), gst_element_state_get_name (pipeline_state));
						return FALSE;
					}
				}
			}
			else
			{
				gst_element_get_state (element, &current_state, NULL, timeout);
				if (current_state == state)
					GST_LOG_OBJECT(app, "GST_STATE_CHANGE_ASYNC on setting %" GST_PTR_FORMAT"'s state to %s SUCCESSFUL!", element, gst_element_state_get_name (state));
				else
				{
					GST_WARNING_OBJECT(app, "GST_STATE_CHANGE_ASYNC on setting %" GST_PTR_FORMAT"'s state to %s failed! now in %s", element, gst_element_state_get_name (state), gst_element_state_get_name (current_state));
					return FALSE;
				}
			}
			return TRUE;
		}
		case GST_STATE_CHANGE_NO_PREROLL:
			GST_WARNING_OBJECT(app, "GST_STATE_CHANGE_NO_PREROLL on setting %" GST_PTR_FORMAT"'s state to %s ", element, gst_element_state_get_name (state));
			break;
		default:
			break;
	}
	return TRUE;
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
	DREAMRTSPSERVER_LOCK (app);
	app->pipeline = gst_pipeline_new ("dreamrtspserver_source_pipeline");

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
        app->tstee = gst_element_factory_make ("tee", "tstee");

	if (!(app->asrc && app->vsrc && app->aparse && app->vparse && app->aq && app->vq && app->atee && app->vtee && app->tsmux && app->tstee))
	{
		g_error ("Failed to create source pipeline element(s):%s%s%s%s%s%s%s%s%s", app->asrc?"":" dreamaudiosource", app->vsrc?"":" dreamvideosource", app->aparse?"":" aacparse",
			app->vparse?"":" h264parse", app->aq?"":" aqueue", app->vq?"":" vqueue", app->atee?"":" atee", app->vtee?"":" vtee", app->tsmux?"":"  mpegtsmux");
	}
	gst_object_unref(app->tsmux);
	app->tsmux = NULL;

	if (!(app->asrc && app->vsrc && app->aparse && app->vparse))
	{
		g_error ("Failed to create source pipeline element(s):%s%s%s%s", app->asrc?"":" dreamaudiosource", app->vsrc?"":" dreamvideosource", app->aparse?"":" aacparse", app->vparse?"":" h264parse");
	}

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

	if (ret != GST_PAD_LINK_OK)
	{
		GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", teepad, sinkpad);
		return FALSE;
	}
	gst_object_unref (teepad);
	gst_object_unref (sinkpad);

	teepad = gst_element_get_request_pad (app->vtee, "src_%u");
	sinkpad = gst_element_get_static_pad (app->vq, "sink");
	ret = gst_pad_link (teepad, sinkpad);

	if (ret != GST_PAD_LINK_OK)
	{
		GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", teepad, sinkpad);
		return FALSE;
	}

	gst_object_unref (teepad);
	gst_object_unref (sinkpad);

	app->clock = gst_system_clock_obtain();
	gst_pipeline_use_clock(GST_PIPELINE (app->pipeline), app->clock);

	apply_source_properties(app);

	g_signal_connect (app->asrc, "signal-lost", G_CALLBACK (encoder_signal_lost), app);

	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"create_source_pipeline");
	DREAMRTSPSERVER_UNLOCK (app);
	return TRUE;
}

static void encoder_signal_lost (GstElement *dreamaudiosource, gpointer user_data)
{
	GST_INFO_OBJECT (dreamaudiosource, "lost encoder signal!");
}

static GstPadProbeReturn inject_authorization (GstPad * sinkpad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;

	GstBuffer *token_buf = gst_buffer_new_wrapped (app->tcp_upstream->token, TOKEN_LEN);
	GstPad * srcpad = gst_element_get_static_pad (app->tcp_upstream->tstcpq, "src");

	GST_INFO ("injecting authorization on pad %s:%s, created token_buf %" GST_PTR_FORMAT "", GST_DEBUG_PAD_NAME (sinkpad), token_buf);
	gst_pad_remove_probe (sinkpad, info->id);
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
		DREAMRTSPSERVER_LOCK (app);

		t->id_signal_overrun = 0;
		t->id_signal_waiting = 0;
		t->id_signal_keepalive = 0;
		t->id_bitrate_measure = 0;
		t->id_resume = 0;
		t->state = UPSTREAM_STATE_CONNECTING;
		send_signal (app, "upstreamStateChanged", g_variant_new("(i)", t->state));

		t->tstcpq  = gst_element_factory_make ("queue", "tstcpqueue");
		t->tcpsink = gst_element_factory_make ("tcpclientsink", NULL);

		if (!(t->tstcpq && t->tcpsink ))
			g_error ("Failed to create tcp upstream element(s):%s%s", t->tstcpq?"":"  ts queue", t->tcpsink?"":"  tcpclientsink" );

		g_object_set (G_OBJECT (t->tstcpq), "leaky", 2, "max-size-buffers", 400, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(0), NULL);

		t->id_signal_overrun = g_signal_connect (t->tstcpq, "overrun", G_CALLBACK (queue_overrun), app);
		GST_TRACE_OBJECT(app, "installed %" GST_PTR_FORMAT " overrun handler id=%u", t->tstcpq, t->id_signal_overrun);

		g_object_set (t->tcpsink, "max-lateness", G_GINT64_CONSTANT(3)*GST_SECOND, NULL);
		g_object_set (t->tcpsink, "blocksize", BLOCK_SIZE, NULL);

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
			DREAMRTSPSERVER_UNLOCK (app);
			return FALSE;
		}

		gst_bin_add_many (GST_BIN(app->pipeline), t->tstcpq, t->tcpsink, NULL);
		if (!gst_element_link (t->tstcpq, t->tcpsink)) {
			GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", t->tstcpq, t->tcpsink);
			goto fail;
		}

// 		if (!assert_state (app, t->tcpsink, GST_STATE_PLAYING) || !assert_state (app, t->tstcpq, GST_STATE_PLAYING))
// 			goto fail;

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

		if (strlen(token))
		{
			sinkpad = gst_element_get_static_pad (t->tcpsink, "sink");
			strcpy(t->token, token);
			gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_BUFFER_LIST, (GstPadProbeCallback) inject_authorization, app, NULL);
			gst_object_unref (sinkpad);
		}
		else
			GST_DEBUG_OBJECT (app, "no token specified!");

		if (!assert_state (app, app->pipeline, GST_STATE_PLAYING))
		{
			GST_ERROR_OBJECT (app, "GST_STATE_CHANGE_FAILURE for TCP upstream");
			return FALSE;
		}
		GST_INFO_OBJECT(app, "enabled TCP upstream! upstreamState = UPSTREAM_STATE_CONNECTING");
		DREAMRTSPSERVER_UNLOCK (app);
		return TRUE;
	}
	else
		GST_INFO_OBJECT (app, "tcp upstream already enabled! (upstreamState = %i)", t->state);
	return FALSE;

fail:
	DREAMRTSPSERVER_UNLOCK (app);
	disable_tcp_upstream(app);
	return FALSE;
}

gboolean
_delete_dir_recursively (GFile *directory, GError **error)
{
	GFileEnumerator *children = NULL;
	GFileInfo *info;
	gboolean ret = FALSE;
	children = g_file_enumerate_children (directory, G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, error);
	if (children == NULL || error)
		goto out;
	info = g_file_enumerator_next_file (children, NULL, error);
	while (info || error) {
		GFile *child;
		const char *name;
		GFileType type;
		if (error)
			goto out;
		name = g_file_info_get_name (info);
		child = g_file_get_child (directory, name);
		type = g_file_info_get_file_type (info);
		GST_LOG ("delete %s", name);
		if (type == G_FILE_TYPE_DIRECTORY)
			ret = _delete_dir_recursively (child, error);
		else if (type == G_FILE_TYPE_REGULAR)
			ret = g_file_delete (child, NULL, error);
		g_object_unref (info);
		g_object_unref (child);
		if (!ret)
			goto out;
		info = g_file_enumerator_next_file (children, NULL, error);
	}
	ret = TRUE;
	g_file_delete (directory, NULL, error);

out:
	if (children)
		g_object_unref (children);
	return ret;
}

gboolean hls_client_timeout (gpointer user_data)
{
	App *app = user_data;
	if (app->hls_server)
	{
		GST_INFO_OBJECT(app, "HLS clients stopped downloading, stopping hls pipeline!");
		stop_hls_pipeline (app);
	}
	return FALSE;
}

static void
soup_do_get (SoupServer *server, SoupMessage *msg, const char *path, App *app)
{
	gchar *hlspath = NULL;
	guint status_code = SOUP_STATUS_NONE;
	struct stat st;

	if (path)
	{
		if (strlen(path) < 1)
			status_code = SOUP_STATUS_BAD_REQUEST;
		if (strlen(path) == 1)
			status_code = SOUP_STATUS_MOVED_PERMANENTLY;
		else
			hlspath = g_strdup_printf ("%s%s", HLS_PATH, path);
	}
	if (app->hls_server->state == HLS_STATE_IDLE && g_strcmp0 (path+1, HLS_PLAYLIST_NAME) == 0)
	{
		DREAMRTSPSERVER_LOCK (app);
		GST_INFO_OBJECT (server, "client requested '%s' but we're idle... start pipeline!", path+1);
		if (!start_hls_pipeline (app))
			status_code = SOUP_STATUS_INTERNAL_SERVER_ERROR;
		else
		{
			GstState state;
			gst_element_get_state (GST_ELEMENT(app->pipeline), &state, NULL, HLS_FRAGMENT_DURATION);
			g_usleep (G_USEC_PER_SEC * (HLS_FRAGMENT_DURATION+1));
			app->hls_server->state = HLS_STATE_RUNNING;
			send_signal (app, "hlsStateChanged", g_variant_new("(i)", HLS_STATE_RUNNING));
		}
		DREAMRTSPSERVER_UNLOCK (app);
	}
	else if (status_code == SOUP_STATUS_NONE && stat (hlspath, &st) == -1) {
		if (errno == EPERM)
			status_code = SOUP_STATUS_FORBIDDEN;
		else if (errno == ENOENT)
			status_code = SOUP_STATUS_NOT_FOUND;
		else
			status_code = SOUP_STATUS_INTERNAL_SERVER_ERROR;
	}
	else if (S_ISDIR (st.st_mode))
		status_code = SOUP_STATUS_SERVICE_UNAVAILABLE;

	if (status_code == SOUP_STATUS_MOVED_PERMANENTLY)
	{
		GST_LOG_OBJECT (server, "client requested /, redirect to %s", HLS_PLAYLIST_NAME);
		soup_message_set_redirect (msg, status_code, HLS_PLAYLIST_NAME);
		return;
	}
	else if (status_code != SOUP_STATUS_NONE)
	{
		GST_WARNING_OBJECT (server, "client requested '%s', error serving '%s', http status code %i", path, hlspath ? hlspath : "", status_code);
		g_free (hlspath);
		soup_message_set_status (msg, status_code);
		return;
	}

	GST_INFO_OBJECT (server, "client requests '%s', serving '%s'...", path, hlspath);

	if (msg->method == SOUP_METHOD_GET) {
		GMappedFile *mapping;
		SoupBuffer *buffer;

		mapping = g_mapped_file_new (hlspath, FALSE, NULL);
		if (!mapping) {
			soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
			return;
		}

		if (g_strrstr (hlspath, ".ts"))
			soup_message_headers_set_content_type (msg->response_headers, "video/MP2T", NULL);
		else
		{
			GstState state;
			gst_element_get_state (app->asrc, &state, NULL, GST_MSECOND);
			if (state != GST_STATE_PLAYING)
			{
				assert_tsmux (app);
				if (!assert_state (app, app->pipeline, GST_STATE_PLAYING))
				{
					soup_message_set_status (msg, SOUP_STATUS_BAD_GATEWAY);
					g_free (hlspath);
					return;
				}
			}
			soup_message_headers_set_content_type (msg->response_headers, "application/x-mpegURL", NULL);
		}
		if (app->hls_server->id_timeout)
			g_source_remove (app->hls_server->id_timeout);
		app->hls_server->id_timeout = g_timeout_add_seconds (5*HLS_FRAGMENT_DURATION, (GSourceFunc) hls_client_timeout, app);

		buffer = soup_buffer_new_with_owner (g_mapped_file_get_contents (mapping),
						     g_mapped_file_get_length (mapping),
						     mapping, (GDestroyNotify)g_mapped_file_unref);
		soup_message_body_append_buffer (msg->response_body, buffer);
		soup_buffer_free (buffer);
	}
	g_free (hlspath);
	soup_message_set_status (msg, SOUP_STATUS_OK);
}

static gboolean
soup_server_auth_callback (SoupAuthDomain *domain, SoupMessage *msg, const char *username, const char *password, gpointer user_data)
{
	App *app = (App *) user_data;
	DreamHLSserver *h = app->hls_server;
	if (g_strcmp0(h->hls_user, username) == 0 && strcmp(h->hls_pass, password) == 0)
	{
		GST_TRACE_OBJECT (app->hls_server, "authenticated request with credentials %s:%s", username, password);
		return TRUE;
	}
	else
		GST_WARNING_OBJECT (app->hls_server, "denied authentication request with credentials %s:%s", username, password); 
	return FALSE;
}

static void
soup_server_callback (SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *context, gpointer data)
{
	GST_TRACE_OBJECT (server, "%s %s HTTP/1.%d", msg->method, path, soup_message_get_http_version (msg));
	if (msg->method == SOUP_METHOD_GET)
		soup_do_get (server, msg, path, (App *) data);
	else
		soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
	GST_TRACE_OBJECT (server, "  -> %d %s", msg->status_code, msg->reason_phrase);
}

static GstPadProbeReturn hls_pad_probe_unlink_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	DreamHLSserver *h = app->hls_server;

	GstElement *element = gst_pad_get_parent_element(pad);

	GST_DEBUG_OBJECT(pad, "unlink... %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT, element, h->hlssink);

	GstPad *teepad;
	teepad = gst_pad_get_peer(pad);
	gst_pad_unlink (teepad, pad);

	GstElement *tee = gst_pad_get_parent_element(teepad);
	gst_element_release_request_pad (tee, teepad);
	gst_object_unref (teepad);
	gst_object_unref (tee);

	gst_element_unlink (element, h->hlssink);

	GST_DEBUG_OBJECT(pad, "remove... %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT, element, h->hlssink);

	gst_bin_remove_many (GST_BIN (app->pipeline), element, h->hlssink, NULL);

	GST_DEBUG_OBJECT(pad, "set state null %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT, element, h->hlssink);

	gst_element_set_state (h->hlssink, GST_STATE_NULL);
	gst_element_set_state (element, GST_STATE_NULL);

	GST_DEBUG_OBJECT(pad, "unref.... %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT, element, h->hlssink);

	gst_object_unref (element);
	gst_object_unref (h->hlssink);
	h->queue = NULL;
	h->hlssink = NULL;

	if (h->id_timeout)
		g_source_remove (h->id_timeout);

	if (app->tcp_upstream->state == UPSTREAM_STATE_DISABLED && g_list_length (app->rtsp_server->clients_list) == 0)
		halt_source_pipeline(app);

	GST_INFO ("HLS server unlinked!");

	return GST_PAD_PROBE_REMOVE;
}

gboolean stop_hls_pipeline(App *app)
{
	GST_INFO_OBJECT(app, "stop_hls_pipeline");
	DreamHLSserver *h = app->hls_server;
	if (h->state == HLS_STATE_RUNNING)
	{
		DREAMRTSPSERVER_LOCK (app);
		h->state = HLS_STATE_IDLE;
		send_signal (app, "hlsStateChanged", g_variant_new("(i)", HLS_STATE_IDLE));
		gst_object_ref (h->queue);
		gst_object_ref (h->hlssink);
		GstPad *sinkpad;
		sinkpad = gst_element_get_static_pad (h->queue, "sink");
		gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, hls_pad_probe_unlink_cb, app, NULL);
		gst_object_unref (sinkpad);
		DREAMRTSPSERVER_UNLOCK (app);
		GST_INFO("hls server pipeline stopped, set HLS_STATE_IDLE");
		return TRUE;
	}
	else
		GST_INFO("hls server wasn't in HLS_STATE_RUNNING... can't stop");
	return FALSE;
}

gboolean disable_hls_server(App *app)
{
	GST_INFO_OBJECT(app, "disable_hls_server");
	DreamHLSserver *h = app->hls_server;
	if (h->state == HLS_STATE_RUNNING)
		stop_hls_pipeline (app);
	if (h->state == HLS_STATE_IDLE)
	{
		DREAMRTSPSERVER_LOCK (app);
		soup_server_disconnect(h->soupserver);
		if (h->soupauthdomain)
		{
			g_object_unref (h->soupauthdomain);
			g_free(h->hls_user);
			g_free(h->hls_pass);
		}
		g_object_unref (h->soupserver);
		GFile *tmp_dir_file = g_file_new_for_path (HLS_PATH);
		_delete_dir_recursively (tmp_dir_file, NULL);
		g_object_unref (tmp_dir_file);
		h->state = HLS_STATE_DISABLED;
		send_signal (app, "hlsStateChanged", g_variant_new("(i)", HLS_STATE_DISABLED));
		DREAMRTSPSERVER_UNLOCK (app);
		GST_INFO("hls soupserver unref'ed, set HLS_STATE_DISABLED");
		return TRUE;
	}
	else
		GST_INFO("hls server in wrong state... can't disable");
	return FALSE;
}

#if 0
static GstPadProbeReturn _detect_keyframes_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	GstBuffer *buffer;
	guint idx = 0, num_buffers = 1;
	do {
		if (info->type & GST_PAD_PROBE_TYPE_BUFFER)
		{
			buffer = GST_PAD_PROBE_INFO_BUFFER (info);
		}
		else if (info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST)
		{
			GstBufferList *bufferlist = GST_PAD_PROBE_INFO_BUFFER_LIST (info);
			num_buffers = gst_buffer_list_length (bufferlist);
			buffer = gst_buffer_list_get (bufferlist, idx);
		}
		if (GST_IS_BUFFER(buffer) && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))
			GST_INFO_OBJECT (app, "KEYFRAME %" GST_PTR_FORMAT " detected @ %" GST_PTR_FORMAT " num_buffers=%i", buffer, pad, num_buffers);
		idx++;
	} while (idx < num_buffers);
	return GST_PAD_PROBE_OK;
}
#endif

gboolean enable_hls_server(App *app, guint port, const gchar *user, const gchar *pass)
{
	GST_INFO_OBJECT(app, "enable_hls_server port=%i user=%s pass=%s", port, user, pass);
	if (!app->pipeline)
	{
		GST_ERROR_OBJECT (app, "failed to enable hls server because source pipeline is NULL!");
		return FALSE;
	}

	DREAMRTSPSERVER_LOCK (app);
	DreamHLSserver *h = app->hls_server;

	if (h->state == HLS_STATE_DISABLED)
	{
		int r = mkdir (HLS_PATH, DEFFILEMODE);
		if (r == -1 && errno != EEXIST)
		{
			g_error ("Failed to create HLS server directory '%s': %s (%i)", HLS_PATH, strerror(errno), errno);
			goto fail;
		}

		h->port = port;

#if SOUP_CHECK_VERSION(2,48,0)
		h->soupserver = soup_server_new (SOUP_SERVER_SERVER_HEADER, "dreamhttplive", NULL);
		soup_server_listen_all(h->soupserver, port, 0, NULL);
#else
		h->soupserver = soup_server_new (SOUP_SERVER_PORT, h->port, SOUP_SERVER_SERVER_HEADER, "dreamhttplive", NULL);
		soup_server_run_async (h->soupserver);
#endif
		soup_server_add_handler (h->soupserver, NULL, soup_server_callback, app, NULL);

		gchar *credentials = g_strdup("");
		if (strlen(user)) {
			h->hls_user = g_strdup(user);
			h->hls_pass = g_strdup(pass);
			h->soupauthdomain = soup_auth_domain_basic_new (
			SOUP_AUTH_DOMAIN_REALM, "Dreambox HLS Server",
			SOUP_AUTH_DOMAIN_BASIC_AUTH_CALLBACK, soup_server_auth_callback,
			SOUP_AUTH_DOMAIN_BASIC_AUTH_DATA, app,
			SOUP_AUTH_DOMAIN_ADD_PATH, "",
			NULL);
			soup_server_add_auth_domain (h->soupserver, h->soupauthdomain);
			credentials = g_strdup_printf("%s:%s@", user, pass);
		}
		else
		{
			h->hls_user = h->hls_pass = NULL;
			h->soupauthdomain = NULL;
		}

#if SOUP_CHECK_VERSION(2,48,0)
		GSList *uris = soup_server_get_uris(h->soupserver);
		for (GSList *uri = uris; uri != NULL; uri = uri->next) {
			char *str = soup_uri_to_string(uri->data, FALSE);
			GST_INFO_OBJECT(h->soupserver, "SOUP HLS server ready at %s [/%s] (%s)", str, HLS_PLAYLIST_NAME, credentials);
			g_free(str);
			soup_uri_free(uri->data);
		}
		g_slist_free(uris);
#else
		GST_INFO_OBJECT (h->soupserver, "SOUP HLS server ready at http://%s127.0.0.1:%i/%s ...", credentials, soup_server_get_port (h->soupserver), HLS_PLAYLIST_NAME);
#endif

		h->state = HLS_STATE_IDLE;
		send_signal (app, "hlsStateChanged", g_variant_new("(i)", HLS_STATE_IDLE));
		GST_DEBUG ("set HLS_STATE_IDLE");
		g_free (credentials);
		DREAMRTSPSERVER_UNLOCK (app);
		return TRUE;
	}
	else
		GST_INFO_OBJECT (app, "HLS server already enabled!");
	DREAMRTSPSERVER_UNLOCK (app);
	return FALSE;

fail:
	DREAMRTSPSERVER_UNLOCK (app);
	disable_hls_server(app);
	return FALSE;

}

gboolean start_hls_pipeline(App* app)
{
	GST_DEBUG_OBJECT (app, "start_hls_pipeline");

	DreamHLSserver *h = app->hls_server;
	if (h->state == HLS_STATE_DISABLED)
	{
		GST_ERROR_OBJECT (app, "failed to start hls pipeline because hls server is not enabled!");
		return FALSE;
	}

	assert_tsmux (app);

	h->queue = gst_element_factory_make ("queue", "hlsqueue");
	h->hlssink = gst_element_factory_make ("hlssink", "hlssink");
	if (!(h->hlssink && h->queue))
	{
		g_error ("Failed to create HLS pipeline element(s):%s%s", h->hlssink?"":" hlssink", h->queue?"":" queue");
		return FALSE;
	}

	gchar *frag_location, *playlist_location;
	frag_location = g_strdup_printf ("%s/%s", HLS_PATH, HLS_FRAGMENT_NAME);
	playlist_location = g_strdup_printf ("%s/%s", HLS_PATH, HLS_PLAYLIST_NAME);

	g_object_set (G_OBJECT (h->hlssink), "target-duration", HLS_FRAGMENT_DURATION, NULL);
	g_object_set (G_OBJECT (h->hlssink), "location", frag_location, NULL);
	g_object_set (G_OBJECT (h->hlssink), "playlist-location", playlist_location, NULL);
	g_object_set (G_OBJECT (h->queue), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);

	gst_bin_add_many (GST_BIN (app->pipeline), h->queue, h->hlssink,  NULL);
	gst_element_link (h->queue, h->hlssink);

	if (!assert_state (app, h->hlssink, GST_STATE_READY) || !assert_state (app, h->queue, GST_STATE_PLAYING))
		return FALSE;

	GstPad *teepad, *sinkpad;
	GstPadLinkReturn ret;
	teepad = gst_element_get_request_pad (app->tstee, "src_%u");
	sinkpad = gst_element_get_static_pad (h->queue, "sink");
	ret = gst_pad_link (teepad, sinkpad);
	if (ret != GST_PAD_LINK_OK)
	{
		GST_ERROR_OBJECT (app, "couldn't link %" GST_PTR_FORMAT " ! %" GST_PTR_FORMAT "", teepad, sinkpad);
		return FALSE;
	}
	gst_object_unref (teepad);
	gst_object_unref (sinkpad);

	if (app->tcp_upstream->state == UPSTREAM_STATE_WAITING)
		unpause_source_pipeline(app);

	GstStateChangeReturn sret = gst_element_set_state (h->hlssink, GST_STATE_PLAYING);
	GST_DEBUG_OBJECT(app, "explicitely bring hlssink to GST_STATE_PLAYING = %i", sret);

	if (!assert_state (app, app->pipeline, GST_STATE_PLAYING))
	{
		GST_ERROR_OBJECT (app, "GST_STATE_CHANGE_FAILURE for hls pipeline");
		return FALSE;
	}

	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"start_hls_server");
	return TRUE;
}

DreamHLSserver *create_hls_server(App *app)
{
	DreamHLSserver *h = malloc(sizeof(DreamHLSserver));
	send_signal (app, "hlsStateChanged", g_variant_new("(i)", HLS_STATE_DISABLED));
	h->state = HLS_STATE_DISABLED;
	h->queue = NULL;
	h->hlssink = NULL;
	return h;
}

DreamRTSPserver *create_rtsp_server(App *app)
{
	DreamRTSPserver *r = malloc(sizeof(DreamRTSPserver));
	send_signal (app, "rtspStateChanged", g_variant_new("(i)", RTSP_STATE_DISABLED));
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

	DREAMRTSPSERVER_LOCK (app);
	DreamRTSPserver *r = app->rtsp_server;

	if (r->state == RTSP_STATE_DISABLED)
	{
		r->artspq = gst_element_factory_make ("queue", "rtspaudioqueue");
		r->vrtspq = gst_element_factory_make ("queue", "rtspvideoqueue");
		r->aappsink = gst_element_factory_make ("appsink", AAPPSINK);
		r->vappsink = gst_element_factory_make ("appsink", VAPPSINK);

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

		g_object_set (G_OBJECT (r->tsrtspq), "leaky", 2, "max-size-buffers", 0, "max-size-bytes", 0, "max-size-time", G_GINT64_CONSTANT(5)*GST_SECOND, NULL);

		g_object_set (G_OBJECT (r->tsappsink), "emit-signals", TRUE, NULL);
		g_object_set (G_OBJECT (r->tsappsink), "enable-last-sample", FALSE, NULL);
		g_signal_connect (r->tsappsink, "new-sample", G_CALLBACK (handover_payload), app);

		gst_bin_add_many (GST_BIN (app->pipeline), r->artspq, r->vrtspq, r->aappsink, r->vappsink,  NULL);
		gst_element_link (r->artspq, r->aappsink);
		gst_element_link (r->vrtspq, r->vappsink);

		gst_bin_add_many (GST_BIN (app->pipeline), r->tsrtspq, r->tsappsink,  NULL);
		gst_element_link (r->tsrtspq, r->tsappsink);

		GstState targetstate = GST_STATE_READY;

		if (!assert_state (app, r->tsappsink, targetstate) || !assert_state (app, r->aappsink, targetstate) || !assert_state (app, r->vappsink, targetstate))
			goto fail;

		if (!assert_state (app, r->tsrtspq, targetstate) || !assert_state (app, r->artspq, targetstate) || !assert_state (app, r->vrtspq, targetstate))
			goto fail;

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

		if (app->tcp_upstream->state != UPSTREAM_STATE_DISABLED || app->hls_server->state != HLS_STATE_DISABLED)
			targetstate = GST_STATE_PLAYING;

		if (!assert_state (app, app->pipeline, targetstate))
			goto fail;

		r->server = g_object_new (GST_TYPE_DREAM_RTSP_SERVER, NULL);
		g_signal_connect (r->server, "client-connected", (GCallback) client_connected, app);

		r->es_factory = gst_dream_rtsp_media_factory_new ();
		gst_rtsp_media_factory_set_launch (GST_RTSP_MEDIA_FACTORY (r->es_factory), "( appsrc name=" ES_VAPPSRC " ! h264parse ! rtph264pay name=pay0 pt=96   appsrc name=" ES_AAPPSRC " ! aacparse ! rtpmp4apay name=pay1 pt=97 )");
		gst_rtsp_media_factory_set_shared (GST_RTSP_MEDIA_FACTORY (r->es_factory), TRUE);

		g_signal_connect (r->es_factory, "media-configure", (GCallback) media_configure, app);

		r->ts_factory = gst_dream_rtsp_media_factory_new ();
		gst_rtsp_media_factory_set_launch (GST_RTSP_MEDIA_FACTORY (r->ts_factory), "( appsrc name=" TS_APPSRC " ! queue ! rtpmp2tpay name=pay0 pt=96 )");
		gst_rtsp_media_factory_set_shared (GST_RTSP_MEDIA_FACTORY (r->ts_factory), TRUE);

		g_signal_connect (r->ts_factory, "media-configure", (GCallback) media_configure, app);
		g_signal_connect (r->ts_factory, "uri-parametrized", (GCallback) uri_parametrized, app);

		DREAMRTSPSERVER_UNLOCK (app);

		gchar *credentials = g_strdup("");
		if (strlen(user)) {
			r->rtsp_user = g_strdup(user);
			r->rtsp_pass = g_strdup(pass);
			GstRTSPToken *token;
			gchar *basic;
			GstRTSPAuth *auth = gst_rtsp_auth_new ();
			gst_rtsp_media_factory_add_role (GST_RTSP_MEDIA_FACTORY (r->es_factory), "user", GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE, GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
			gst_rtsp_media_factory_add_role (GST_RTSP_MEDIA_FACTORY (r->ts_factory), "user", GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE, GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
			token = gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "user", NULL);
			basic = gst_rtsp_auth_make_basic (r->rtsp_user, r->rtsp_pass);
			gst_rtsp_server_set_auth (GST_RTSP_SERVER(r->server), auth);
			gst_rtsp_auth_add_basic (auth, basic, token);
			g_free (basic);
			gst_rtsp_token_unref (token);
			credentials = g_strdup_printf("%s:%s@", user, pass);
		}
		else
			r->rtsp_user = r->rtsp_pass = NULL;

		r->rtsp_port = g_strdup_printf("%i", port ? port : DEFAULT_RTSP_PORT);

		gst_rtsp_server_set_service (GST_RTSP_SERVER(r->server), r->rtsp_port);

		if (strlen(path))
		{
			r->rtsp_ts_path = g_strdup_printf ("%s%s", path[0]=='/' ? "" : "/", path);
			r->rtsp_es_path = g_strdup_printf ("%s%s%s", path[0]=='/' ? "" : "/", path, RTSP_ES_PATH_SUFX);
		}
		else
		{
			r->rtsp_ts_path = g_strdup(DEFAULT_RTSP_PATH);
			r->rtsp_es_path = g_strdup_printf ("%s%s", DEFAULT_RTSP_PATH, RTSP_ES_PATH_SUFX);
		}

		r->mounts = gst_rtsp_server_get_mount_points (GST_RTSP_SERVER(r->server));
		gst_rtsp_mount_points_add_factory (r->mounts, r->rtsp_ts_path, g_object_ref(GST_RTSP_MEDIA_FACTORY (r->ts_factory)));
		gst_rtsp_mount_points_add_factory (r->mounts, r->rtsp_es_path, g_object_ref(GST_RTSP_MEDIA_FACTORY (r->es_factory)));
		r->state = RTSP_STATE_IDLE;
		send_signal (app, "rtspStateChanged", g_variant_new("(i)", RTSP_STATE_IDLE));
		GST_DEBUG ("set RTSP_STATE_IDLE");
		r->source_id = gst_rtsp_server_attach (GST_RTSP_SERVER(r->server), NULL);
		r->uri_parameters = NULL;
		GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"enabled_rtsp_server");
		g_print ("dreambox encoder stream ready at rtsp://%s127.0.0.1:%s%s\n", credentials, app->rtsp_server->rtsp_port, app->rtsp_server->rtsp_ts_path);
		g_free (credentials);
		return TRUE;
	}
	else
		GST_INFO_OBJECT (app, "rtsp server already enabled!");
	DREAMRTSPSERVER_UNLOCK (app);
	return FALSE;

fail:
	DREAMRTSPSERVER_UNLOCK (app);
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
	if (!assert_state (app, app->pipeline, GST_STATE_PLAYING))
	{
		GST_ERROR_OBJECT (app, "GST_STATE_CHANGE_FAILURE for rtsp pipeline");
		return FALSE;
	}
	GST_INFO_OBJECT(app, "start rtsp pipeline, pipeline going into PLAYING");
	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"start_rtsp_pipeline");
	GstState state;
	gst_element_get_state (GST_ELEMENT(app->pipeline), &state, NULL, 10*GST_SECOND);
	GST_INFO_OBJECT(app, "pipeline state=%s", gst_element_state_get_name (state));
	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"started_rtsp_pipeline");
	return TRUE;
}

static GstPadProbeReturn tsmux_pad_probe_unlink_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;

	GstElement *element = gst_pad_get_parent_element(pad);
	GST_DEBUG_OBJECT(pad, "tsmux_pad_probe_unlink_cb %" GST_PTR_FORMAT, element);

	GstElement *source = NULL;
	if (element == app->aq)
		source = app->asrc;
	else if (element == app->vq)
		source = app->vsrc;
	else if (element == app->tsmux)
	{
		gst_element_unlink (app->tsmux, app->tstee);
		gst_bin_remove (GST_BIN (app->pipeline), app->tsmux);
		gst_element_set_state (app->tsmux, GST_STATE_NULL);
		gst_object_unref (app->tsmux);
		app->tsmux = NULL;
		if (gst_element_set_state (app->pipeline, GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS)
			GST_WARNING_OBJECT (pad, "error bringing pipeline back to ready");
//		DREAMRTSPSERVER_UNLOCK (app);
		GST_DEBUG_OBJECT (pad, "finished unlinking and removing sources and tsmux");
		return GST_PAD_PROBE_REMOVE;
	}

	if (gst_element_set_state (source, GST_STATE_NULL) != GST_STATE_CHANGE_SUCCESS)
	{
		GST_ERROR_OBJECT(pad, "can't set %" GST_PTR_FORMAT "'s state to GST_STATE_NULL", source);
		goto fail;
	}

	GstPad *srcpad, *muxpad;
	srcpad = gst_element_get_static_pad (element, "src");
	muxpad = gst_pad_get_peer (srcpad);
	if (GST_IS_PAD (muxpad))
	{
		GST_DEBUG_OBJECT(pad, "srcpad %" GST_PTR_FORMAT " muxpad %" GST_PTR_FORMAT " tsmux %" GST_PTR_FORMAT, srcpad, muxpad, app->tsmux);
		gst_pad_unlink (srcpad, muxpad);
		gst_element_release_request_pad (app->tsmux, muxpad);
		gst_object_unref (muxpad);
	}
	else
		GST_DEBUG_OBJECT(pad, "srcpad %" GST_PTR_FORMAT "'s peer was already unreffed", srcpad);
	gst_object_unref (srcpad);

	gst_element_set_state (element, GST_STATE_READY);

	GstState state;
	gst_element_get_state (GST_ELEMENT(element), &state, NULL, 2*GST_SECOND);

	if (state != GST_STATE_READY)
	{
		GST_ERROR_OBJECT (app, "%" GST_PTR_FORMAT"'s state = %s (should be GST_STATE_READY)", element, gst_element_state_get_name (state));
		goto fail;
	}

	GstPad *sinkpad = NULL;
	GstElement *nextelem = NULL;
	if (element == app->aq)
	{
		nextelem = app->vq;
		if (GST_IS_ELEMENT(nextelem))
			sinkpad = gst_element_get_static_pad (nextelem, "sink");
	}
	if (element == app->vq)
	{
		nextelem = app->tsmux;
		if (GST_IS_ELEMENT(nextelem))
			sinkpad = gst_element_get_static_pad (nextelem, "src");
	}
	if (GST_IS_PAD(sinkpad) && GST_IS_ELEMENT(nextelem))
	{
		GST_DEBUG_OBJECT(pad, "element %" GST_PTR_FORMAT " is now in GST_STATE_READY, installing idle probe on %" GST_PTR_FORMAT, element, sinkpad);
		gst_object_ref (nextelem);
		gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, tsmux_pad_probe_unlink_cb, app, NULL);
		gst_object_unref (sinkpad);
		return GST_PAD_PROBE_REMOVE;
	}

fail:
//	DREAMRTSPSERVER_UNLOCK (app);
	return GST_PAD_PROBE_REMOVE;
}

gboolean halt_source_pipeline(App* app)
{
	GST_INFO_OBJECT(app, "halt_source_pipeline...");
	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app->pipeline),GST_DEBUG_GRAPH_SHOW_ALL,"halt_source_pipeline_pre");
	GstPad *sinkpad;
	gst_object_ref (app->aq);
	sinkpad = gst_element_get_static_pad (app->aq, "sink");
	gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, tsmux_pad_probe_unlink_cb, app, NULL);
	gst_object_unref (sinkpad);
	return TRUE;
}

gboolean pause_source_pipeline(App* app)
{
	if (app->rtsp_server->state <= RTSP_STATE_IDLE && app->hls_server->state == HLS_STATE_DISABLED)
	{
		GST_INFO_OBJECT(app, "pause_source_pipeline... setting sources to GST_STATE_PAUSED rtsp_server->state=%i hls_server->state=%i", app->rtsp_server->state, app->hls_server->state);
		if (gst_element_set_state (app->asrc, GST_STATE_PAUSED) != GST_STATE_CHANGE_NO_PREROLL || gst_element_set_state (app->vsrc, GST_STATE_PAUSED) != GST_STATE_CHANGE_NO_PREROLL)
		{
			GST_WARNING ("can't set pipeline to GST_STATE_PAUSED!");
			return FALSE;
		}
	}
	else
		GST_DEBUG ("not pausing pipeline because rtsp_server->state=%i hls_server->state=%i", app->rtsp_server->state, app->hls_server->state);
	return TRUE;
}

gboolean unpause_source_pipeline(App* app)
{
	GST_INFO_OBJECT(app, "unpause_source_pipeline... setting sources to GST_STATE_PLAYING");
	if (gst_element_set_state (app->asrc, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE || gst_element_set_state (app->vsrc, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
	{
		GST_WARNING("can't set sources to GST_STATE_PLAYING!");
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
// 	DREAMRTSPSERVER_LOCK (app);
	if (media == app->rtsp_server->es_media || media == app->rtsp_server->ts_media) {
		GST_DEBUG_OBJECT (app, "matching RTSP media %p in filter, removing...", media);
		res = GST_RTSP_FILTER_REMOVE;
	}
// 	DREAMRTSPSERVER_UNLOCK (app);
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
	GstElement *appsink = NULL;
	if (element == r->vrtspq)
		appsink = r->vappsink;
	else if (element == r->artspq)
		appsink = r->aappsink;
	else if (element == r->tsrtspq)
		appsink = r->tsappsink;

	GST_DEBUG_OBJECT(pad, "unlink... %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT, element, appsink);

	GstPad *teepad;
	teepad = gst_pad_get_peer(pad);
	gst_pad_unlink (teepad, pad);

	GstElement *tee = gst_pad_get_parent_element(teepad);
	gst_element_release_request_pad (tee, teepad);
	gst_object_unref (teepad);
	gst_object_unref (tee);

	gst_element_unlink (element, appsink);

	GST_DEBUG_OBJECT(pad, "remove... %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT, element, appsink);

	gst_bin_remove_many (GST_BIN (app->pipeline), element, appsink, NULL);

	GST_DEBUG_OBJECT(pad, "set state null %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT, element, appsink);

	gst_element_set_state (appsink, GST_STATE_NULL);
	gst_element_set_state (element, GST_STATE_NULL);

	GST_DEBUG_OBJECT(pad, "unref.... %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT, element, appsink);

	gst_object_unref (element);
	gst_object_unref (appsink);
	element = NULL;
	appsink = NULL;

	if (!r->tsappsink && !r->aappsink && !r->vappsink)
	{
		GST_INFO("!r->tsappsink && !r->aappsink && !r->vappsink");
		if (app->tcp_upstream->state == UPSTREAM_STATE_DISABLED && app->hls_server->state == HLS_STATE_DISABLED)
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
			gst_rtsp_server_client_filter(GST_RTSP_SERVER(app->rtsp_server->server), (GstRTSPServerClientFilterFunc) remove_client_filter_func, app);
		DREAMRTSPSERVER_LOCK (app);
		gst_rtsp_mount_points_remove_factory (app->rtsp_server->mounts, app->rtsp_server->rtsp_es_path);
		gst_rtsp_mount_points_remove_factory (app->rtsp_server->mounts, app->rtsp_server->rtsp_ts_path);
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
		g_free(r->rtsp_ts_path);
		g_free(r->rtsp_es_path);
		g_free(r->uri_parameters);
		send_signal (app, "rtspStateChanged", g_variant_new("(i)", RTSP_STATE_DISABLED));
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

		DREAMRTSPSERVER_UNLOCK (app);
		GST_INFO("rtsp_server disabled! set RTSP_STATE_DISABLED");
		return TRUE;
	}
	return FALSE;
}

static GstPadProbeReturn upstream_pad_probe_unlink_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
	App *app = user_data;
	DreamTCPupstream *t = app->tcp_upstream;

	GstElement *element = gst_pad_get_parent_element(pad);

	GST_DEBUG_OBJECT (pad, "upstream_pad_probe_unlink_cb %" GST_PTR_FORMAT, element);

	if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_IDLE) && element && element == t->tstcpq)
	{
		GstPad *teepad;
		teepad = gst_pad_get_peer(pad);
		if (!teepad)
		{
			GST_ERROR_OBJECT (pad, "has no peer! tstcpq=%" GST_PTR_FORMAT", tcpsink=%" GST_PTR_FORMAT", tee=%" GST_PTR_FORMAT,t->tstcpq, t->tcpsink, app->tstee);
			return GST_PAD_PROBE_REMOVE;
		}
		GST_DEBUG_OBJECT (pad, "GST_PAD_PROBE_TYPE_IDLE -> unlink and remove tcpsink");
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

		if (app->rtsp_server->state < RTSP_STATE_RUNNING && app->hls_server->state == HLS_STATE_DISABLED)
			halt_source_pipeline(app);
		GST_INFO("tcp_upstream disabled!");
		t->state = UPSTREAM_STATE_DISABLED;
		send_signal (app, "upstreamStateChanged", g_variant_new("(i)", t->state));
	}
	GST_DEBUG_OBJECT (pad, "upstream_pad_probe_unlink_cb returns GST_PAD_PROBE_REMOVE");
	return GST_PAD_PROBE_REMOVE;
}

gboolean disable_tcp_upstream(App *app)
{
	GstState state;
	gst_element_get_state (GST_ELEMENT(app->pipeline), &state, NULL, 3*GST_SECOND);
	GST_DEBUG("disable_tcp_upstream (current pipeline state=%s)", gst_element_state_get_name (state));
	DreamTCPupstream *t = app->tcp_upstream;
	if (t->state >= UPSTREAM_STATE_CONNECTING)
	{
		GstPad *sinkpad;
		if (t->id_bitrate_measure)
		{
			sinkpad = gst_element_get_static_pad (t->tcpsink, "sink");
			gst_pad_remove_probe (sinkpad, t->id_bitrate_measure);
			t->id_bitrate_measure = 0;
			gst_object_unref (sinkpad);
		}
		gst_object_ref (t->tstcpq);
		sinkpad = gst_element_get_static_pad (t->tstcpq, "sink");
		gulong probe_id = gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_IDLE, upstream_pad_probe_unlink_cb, app, NULL);
		GST_DEBUG("added upstream_pad_probe_unlink_cb with probe_id = %lu on %" GST_PTR_FORMAT"", probe_id, sinkpad);
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
		get_source_properties (app);
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

gboolean watchdog_ping(gpointer user_data)
{
	App *app = user_data;
	GST_TRACE_OBJECT(app, "sending watchdog ping!");
	if (app->dbus_connection)
		g_dbus_connection_emit_signal (app->dbus_connection, NULL, object_name, service, "ping", NULL, NULL);
	return TRUE;
}

gboolean quit_signal(gpointer loop)
{
	GST_INFO_OBJECT(loop, "caught SIGINT");
	g_main_loop_quit((GMainLoop*)loop);
	return FALSE;
}

gboolean get_dot_graph (gpointer user_data)
{
	App *app = user_data;
	GST_INFO_OBJECT(app, "caught SIGUSR1, saving pipeline graph...");
	GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (app->pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "dreamrtspserver-sigusr");
	return TRUE;
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
	memset (&app.source_properties, 0, sizeof(SourceProperties));
	app.source_properties.gopLength = 0; //auto
	app.source_properties.gopOnSceneChange = FALSE;
	app.source_properties.openGop = FALSE;
	app.source_properties.bFrames = 2; //default
	app.source_properties.pFrames = 1; //default
	app.source_properties.profile = 0; //main
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

#if WATCHDOG_TIMEOUT > 0
	g_timeout_add_seconds (WATCHDOG_TIMEOUT, watchdog_ping, &app);
#endif

	app.tcp_upstream = malloc(sizeof(DreamTCPupstream));
	app.tcp_upstream->state = UPSTREAM_STATE_DISABLED;
	app.tcp_upstream->auto_bitrate = AUTO_BITRATE;

	app.hls_server = create_hls_server(&app);

	app.rtsp_server = create_rtsp_server(&app);

	app.loop = g_main_loop_new (NULL, FALSE);
	g_unix_signal_add (SIGINT, quit_signal, app.loop);
	g_unix_signal_add (SIGUSR1, (GSourceFunc) get_dot_graph, &app);

	g_main_loop_run (app.loop);

	if (app.tcp_upstream->state > UPSTREAM_STATE_DISABLED)
		disable_tcp_upstream(&app);
	if (app.rtsp_server->state >= RTSP_STATE_IDLE)
		disable_rtsp_server(&app);
	if (app.rtsp_server->clients_list)
		g_list_free (app.rtsp_server->clients_list);

	if (app.hls_server->state >= HLS_STATE_IDLE)
		disable_hls_server(&app);

	free(app.hls_server);
	free(app.rtsp_server);
	free(app.tcp_upstream);

	destroy_pipeline(&app);

	g_main_loop_unref (app.loop);

	g_mutex_clear (&app.rtsp_mutex);

	g_bus_unown_name (owner_id);
	g_dbus_node_info_unref (introspection_data);

	return 0;
}
