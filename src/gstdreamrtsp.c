/*
 * dreamrtspserver
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

#include "gstdreamrtsp.h"

#define GST_DREAM_RTSP_CLIENT_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_DREAM_RTSP_CLIENT, GstDreamRTSPClientPrivate))

struct _GstDreamRTSPClientPrivate
{
	gchar *service_reference;
};

G_DEFINE_TYPE (GstDreamRTSPClient, gst_dream_rtsp_client, GST_TYPE_RTSP_CLIENT);

GST_DEBUG_CATEGORY_STATIC (rtsp_server_debug);
#define GST_CAT_DEFAULT rtsp_server_debug

static gchar *make_path_extract_sref_from_uri (GstRTSPClient * client, const GstRTSPUrl * uri);

static void gst_dream_rtsp_client_class_init (GstDreamRTSPClientClass * klass)
{
	GObjectClass *gobject_class;
	GstRTSPClientClass *rtsp_client_class = GST_RTSP_CLIENT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (GstDreamRTSPClientPrivate));
	
	rtsp_client_class->make_path_from_uri = make_path_extract_sref_from_uri;
	
	GST_DEBUG_CATEGORY_INIT (rtsp_server_debug, "dreamrtspserver",
			GST_DEBUG_BOLD | GST_DEBUG_FG_YELLOW | GST_DEBUG_BG_BLUE,
			"Dreambox RTSP server daemon");
}

static void gst_dream_rtsp_client_init (GstDreamRTSPClient * client)
{
	GstDreamRTSPClientPrivate *priv = GST_DREAM_RTSP_CLIENT_GET_PRIVATE (client);
	client->priv = priv;
	GST_WARNING_OBJECT (client, "Client is initialized");
}

static gchar *make_path_extract_sref_from_uri (GstRTSPClient * client, const GstRTSPUrl * uri)
{
	gchar *path;
	path = g_strdup (uri->abspath);
	GstDreamRTSPClientPrivate *priv = GST_DREAM_RTSP_CLIENT_GET_PRIVATE (client);
	priv->service_reference = g_strdup(uri->query);
	return path;
}

#define GST_DREAM_RTSP_SERVER_GET_PRIVATE(obj)  \
       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTSP_SERVER, GstDreamRTSPServerPrivate))

#define GST_DREAM_RTSP_SERVER_GET_LOCK(server)  (&(GST_DREAM_RTSP_SERVER_CAST(server)->priv->lock))
#define GST_DREAM_RTSP_SERVER_LOCK(server)      (g_mutex_lock(GST_DREAM_RTSP_SERVER_GET_LOCK(server)))
#define GST_DREAM_RTSP_SERVER_UNLOCK(server)    (g_mutex_unlock(GST_DREAM_RTSP_SERVER_GET_LOCK(server)))

struct _GstDreamRTSPServerPrivate
{
	GMutex lock;
};

#define gst_dream_rtsp_server_parent_class parent_class
G_DEFINE_TYPE (GstDreamRTSPServer, gst_dream_rtsp_server, GST_TYPE_RTSP_SERVER);

static GstRTSPClient *gst_dream_rtsp_create_client (GstRTSPServer * server);
static void gst_dream_rtsp_server_finalize (GObject * object);

static void gst_dream_rtsp_server_init (GstDreamRTSPServer *server)
{
	GstDreamRTSPServerPrivate *priv = GST_DREAM_RTSP_SERVER_GET_PRIVATE (server);
	server->priv = priv;
	g_mutex_init (&priv->lock);
	
	GST_WARNING_OBJECT (server, "dream_rtsp_server_init lock@%p GstDreamRTSPServer@%p", priv->lock, server);
}

static void gst_dream_rtsp_server_class_init (GstDreamRTSPServerClass * klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GstRTSPServerClass *parent_class = GST_RTSP_SERVER_CLASS (klass);

	g_type_class_add_private (klass, sizeof (GstDreamRTSPServerPrivate));

	parent_class->create_client = gst_dream_rtsp_create_client;
	gobject_class->finalize = gst_dream_rtsp_server_finalize;

	GST_DEBUG_CATEGORY_INIT (rtsp_server_debug, "dreamrtspserver",
		GST_DEBUG_BOLD | GST_DEBUG_FG_YELLOW | GST_DEBUG_BG_BLUE,
		"Dreambox RTSP server daemon");

	GST_WARNING_OBJECT (klass, "Server class is initialized");
}

static GstRTSPClient *gst_dream_rtsp_create_client (GstRTSPServer * server)
{
	GstDreamRTSPServer *dream_rtsp_server = GST_DREAM_RTSP_SERVER (server);
	GstDreamRTSPClient *client;
	client = g_object_new(GST_TYPE_DREAM_RTSP_CLIENT, NULL);
	
	GstDreamRTSPServerPrivate *priv = dream_rtsp_server->priv;

	GstRTSPSessionPool *spool = gst_rtsp_server_get_session_pool (server);
	gst_rtsp_client_set_session_pool (GST_RTSP_CLIENT(client), spool);
	g_object_unref (spool);

	GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points (server);
	gst_rtsp_client_set_mount_points (GST_RTSP_CLIENT(client), mounts);
	g_object_unref (mounts);

	GstRTSPAuth *auth = gst_rtsp_server_get_auth (server);
	if (auth)
	{
		gst_rtsp_client_set_auth (GST_RTSP_CLIENT(client), auth);
		g_object_unref (auth);
	}

	GstRTSPThreadPool *tpool = gst_rtsp_server_get_thread_pool (server);
	gst_rtsp_client_set_thread_pool (GST_RTSP_CLIENT(client), tpool);
	g_object_unref (tpool);

	return GST_RTSP_CLIENT (client);
}

static void gst_dream_rtsp_server_finalize (GObject * object)
{
	GstDreamRTSPServer *server = GST_DREAM_RTSP_SERVER (object);
	GstDreamRTSPServerPrivate *priv = server->priv;
	GST_DEBUG_OBJECT (object, "finalize server");
	g_mutex_clear (&priv->lock);
	G_OBJECT_CLASS (gst_dream_rtsp_server_parent_class)->finalize (object);
}

#define GST_DREAM_RTSP_MEDIA_FACTORY_GET_PRIVATE(obj)  \
(G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_DREAM_RTSP_MEDIA_FACTORY, GstDreamRTSPMediaFactoryPrivate))

struct _GstDreamRTSPMediaFactoryPrivate
{
	GMutex lock;
};

typedef struct
{
	GstDreamRTSPMediaFactory *factory;
	guint pt;
} FactoryData;

enum
{
	SIGNAL_URI_PARAMETRIZED,
	SIGNAL_LAST
};

static guint gst_dream_rtsp_media_factory_signals[SIGNAL_LAST] = { 0 };

static void
free_data (FactoryData * data)
{
	g_object_unref (data->factory);
	g_free (data);
}

static const gchar *factory_key = "GstDreamRTSPMediaFactory";

static void gst_dream_rtsp_media_factory_finalize (GObject * obj);
static GstRTSPMedia *rtsp_dream_media_factory_construct (GstRTSPMediaFactory * factory, const GstRTSPUrl * url);

G_DEFINE_TYPE (GstDreamRTSPMediaFactory, gst_dream_rtsp_media_factory, GST_TYPE_RTSP_MEDIA_FACTORY);

static void gst_dream_rtsp_media_factory_uri_parametrized (GstRTSPMediaFactory *self, gchar *parameters)
{
	if (parameters)
		g_signal_emit (self, gst_dream_rtsp_media_factory_signals[SIGNAL_URI_PARAMETRIZED], 0, parameters);
}

static void
gst_dream_rtsp_media_factory_class_init (GstDreamRTSPMediaFactoryClass * klass)
{
	GObjectClass *gobject_class;
	GstRTSPMediaFactoryClass *mediafactory_class;

	g_type_class_add_private (klass, sizeof (GstDreamRTSPMediaFactoryPrivate));

	gobject_class = G_OBJECT_CLASS (klass);
	mediafactory_class = GST_RTSP_MEDIA_FACTORY_CLASS (klass);

	gobject_class->finalize = gst_dream_rtsp_media_factory_finalize;

	mediafactory_class->construct = rtsp_dream_media_factory_construct;

	gst_dream_rtsp_media_factory_signals[SIGNAL_URI_PARAMETRIZED] =
		g_signal_new ("uri-parametrized", G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(GstDreamRTSPMediaFactoryClass, uri_parametrized),
		NULL, NULL, g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 1, G_TYPE_STRING);

	GST_DEBUG_CATEGORY_INIT (rtsp_server_debug, "dreamrtspserver",
		GST_DEBUG_BOLD | GST_DEBUG_FG_YELLOW | GST_DEBUG_BG_BLUE,
		"Dreambox RTSP server daemon");
}

static void
gst_dream_rtsp_media_factory_init (GstDreamRTSPMediaFactory * factory)
{
	GstDreamRTSPMediaFactoryPrivate *priv =
	GST_DREAM_RTSP_MEDIA_FACTORY_GET_PRIVATE (factory);
	GST_DEBUG_OBJECT (factory, "new");
	factory->priv = priv;
	g_mutex_init (&priv->lock);
}

static void
gst_dream_rtsp_media_factory_finalize (GObject * obj)
{
	GstDreamRTSPMediaFactory *factory = GST_DREAM_RTSP_MEDIA_FACTORY (obj);
	GstDreamRTSPMediaFactoryPrivate *priv = factory->priv;

	GST_DEBUG_OBJECT (factory, "finalize");

	g_mutex_clear (&priv->lock);

	G_OBJECT_CLASS (gst_dream_rtsp_media_factory_parent_class)->finalize (obj);
}

static GstRTSPMedia *
rtsp_dream_media_factory_construct (GstRTSPMediaFactory * factory, const GstRTSPUrl * url)
{
	GstRTSPMedia *media;
	GstElement *element, *pipeline;
	GstRTSPMediaFactoryClass *klass;

	GST_WARNING_OBJECT (factory, "abspath=%s, query=%s", url->abspath, url->query);
	
	klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

	if (!klass->create_pipeline)
		goto no_create;

	element = gst_rtsp_media_factory_create_element (factory, url);
	if (element == NULL)
		goto no_element;

	/* create a new empty media */
	media = GST_RTSP_MEDIA (gst_rtsp_media_new (element));

	gst_rtsp_media_collect_streams (media);

	pipeline = klass->create_pipeline (factory, media);
	if (pipeline == NULL)
		goto no_pipeline;

	gst_dream_rtsp_media_factory_uri_parametrized(factory, url->query);

	return media;

	/* ERRORS */
	no_create:
	{
		g_critical ("no create_pipeline function");
		return NULL;
	}
	no_element:
	{
		g_critical ("could not create element");
		return NULL;
	}
	no_pipeline:
	{
		g_critical ("can't create pipeline");
		g_object_unref (media);
		return NULL;
	}
}

GstDreamRTSPMediaFactory *
gst_dream_rtsp_media_factory_new ()
{
	GstDreamRTSPMediaFactory *result;

	result = g_object_new (GST_TYPE_DREAM_RTSP_MEDIA_FACTORY, NULL);

	return result;
}
