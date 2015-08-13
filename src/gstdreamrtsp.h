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

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-client.h>
#include <gst/rtsp-server/rtsp-media.h>

#ifndef __GSTDREAMRTSP_H__
#define __GSTDREAMRTSP_H__

G_BEGIN_DECLS

#define GST_TYPE_DREAM_RTSP_CLIENT              (gst_dream_rtsp_client_get_type ())
#define GST_IS_DREAM_RTSP_CLIENT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DREAM_RTSP_CLIENT))
#define GST_IS_DREAM_RTSP_CLIENT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DREAM_RTSP_CLIENT))
#define GST_DREAM_RTSP_CLIENT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DREAM_RTSP_CLIENT, GstDreamRTSPClientClass))
#define GST_DREAM_RTSP_CLIENT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DREAM_RTSP_CLIENT, GstDreamRTSPClient))
#define GST_DREAM_RTSP_CLIENT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DREAM_RTSP_CLIENT, GstDreamRTSPClientClass))
#define GST_DREAM_RTSP_CLIENT_CAST(obj)         ((GstDreamRTSPClient*)(obj))
#define GST_DREAM_RTSP_CLIENT_CLASS_CAST(klass) ((GstDreamRTSPClientClass*)(klass))

typedef struct _GstDreamRTSPClient GstDreamRTSPClient;
typedef struct _GstDreamRTSPClientClass GstDreamRTSPClientClass;
typedef struct _GstDreamRTSPClientPrivate GstDreamRTSPClientPrivate;

struct _GstDreamRTSPClient {
	GstRTSPClient   parent;

	/*< private >*/
	GstDreamRTSPClientPrivate *priv;
	gpointer _gst_reserved[GST_PADDING];
};

struct _GstDreamRTSPClientClass {
	GstRTSPClientClass  parent_class;

	gchar *         (*make_path_from_uri) (GstRTSPClient *client, const GstRTSPUrl *uri);

	/*< private >*/
	gpointer _gst_reserved[GST_PADDING];
};

GType   gst_dream_rtsp_client_get_type (void);

/* creating the factory */
GstDreamRTSPClient * gst_dream_rtsp_client_new (void);

#define GST_TYPE_DREAM_RTSP_SERVER              (gst_dream_rtsp_server_get_type())
#define GST_IS_DREAM_RTSP_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DREAM_RTSP_SERVER))
#define GST_IS_DREAM_RTSP_SERVER_CLASS(cls)     (G_TYPE_CHECK_CLASS_TYPE((cls), GST_TYPE_DREAM_RTSP_SERVER))
#define GST_DREAM_RTSP_SERVER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_DREAM_RTSP_SERVER, GstDreamRTSPServerClass))
#define GST_DREAM_RTSP_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DREAM_RTSP_SERVER, GstDreamRTSPServer))
#define GST_DREAM_RTSP_SERVER_CLASS(cls)        (G_TYPE_CHECK_CLASS_CAST((cls), GST_TYPE_DREAM_RTSP_SERVER, GstDreamRTSPServerClass))
#define GST_DREAM_RTSP_SERVER_CAST(obj)         ((GstDreamRTSPServer*)(obj))
#define GST_DREAM_RTSP_SERVER_CLASS_CAST(klass) ((GstDreamRTSPServerClass*)(klass))

typedef struct _GstDreamRTSPServer        GstDreamRTSPServer;
typedef struct _GstDreamRTSPServerClass   GstDreamRTSPServerClass;
typedef struct _GstDreamRTSPServerPrivate GstDreamRTSPServerPrivate;

struct _GstDreamRTSPServer
{
	GstRTSPServer base_server;
	GstDreamRTSPServerPrivate *priv;
	gpointer _gst_reserved[GST_PADDING];
};

struct _GstDreamRTSPServerClass
{
	GstRTSPServerClass parent_class;
	
	GstRTSPClient *      (*create_client)      (GstRTSPServer *server);
	
	/* signals */
	void                 (*client_connected)   (GstRTSPServer *server, GstRTSPClient *client);

	/*< private >*/
	gpointer             _gst_reserved[GST_PADDING_LARGE];
};

GType gst_dream_rtsp_server_get_type (void);

#define GST_TYPE_DREAM_RTSP_MEDIA_FACTORY              (gst_dream_rtsp_media_factory_get_type ())
#define GST_IS_DREAM_RTSP_MEDIA_FACTORY(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DREAM_RTSP_MEDIA_FACTORY))
#define GST_IS_DREAM_RTSP_MEDIA_FACTORY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DREAM_RTSP_MEDIA_FACTORY))
#define GST_DREAM_RTSP_MEDIA_FACTORY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DREAM_RTSP_MEDIA_FACTORY, GstDreamRTSPMediaFactoryClass))
#define GST_DREAM_RTSP_MEDIA_FACTORY(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DREAM_RTSP_MEDIA_FACTORY, GstDreamRTSPMediaFactory))
#define GST_DREAM_RTSP_MEDIA_FACTORY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DREAM_RTSP_MEDIA_FACTORY, GstDreamRTSPMediaFactoryClass))
#define GST_DREAM_RTSP_MEDIA_FACTORY_CAST(obj)         ((GstDreamRTSPMediaFactory*)(obj))
#define GST_DREAM_RTSP_MEDIA_FACTORY_CLASS_CAST(klass) ((GstDreamRTSPMediaFactoryClass*)(klass))

typedef struct _GstDreamRTSPMediaFactory GstDreamRTSPMediaFactory;
typedef struct _GstDreamRTSPMediaFactoryClass GstDreamRTSPMediaFactoryClass;
typedef struct _GstDreamRTSPMediaFactoryPrivate GstDreamRTSPMediaFactoryPrivate;

struct _GstDreamRTSPMediaFactory {
	GstRTSPMediaFactory   parent;

	/*< private >*/
	GstDreamRTSPMediaFactoryPrivate *priv;
	gpointer _gst_reserved[GST_PADDING];
};

struct _GstDreamRTSPMediaFactoryClass {
	GstRTSPMediaFactoryClass  parent_class;

	/* signals */
	void (*uri_parametrized)  (GstRTSPMediaFactory *factory, gchar *parameters);

	/*< private >*/
	gpointer _gst_reserved[GST_PADDING];
};

GType                      gst_dream_rtsp_media_factory_get_type (void);

/* creating the factory */
GstDreamRTSPMediaFactory * gst_dream_rtsp_media_factory_new      (void);

G_END_DECLS

#endif /* __GSTDREAMRTSP_H__ */