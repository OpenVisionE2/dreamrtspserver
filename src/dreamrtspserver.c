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

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	GstRTSPServer *server;
	GstRTSPMountPoints *mounts;
	GstRTSPMediaFactory *factory;

	gst_init (&argc, &argv);

	loop = g_main_loop_new (NULL, FALSE);
	server = gst_rtsp_server_new ();
	mounts = gst_rtsp_server_get_mount_points (server);

	factory = gst_rtsp_media_factory_new ();
	gst_rtsp_media_factory_set_launch (factory, "( dreamvideosource ! h264parse ! rtph264pay name=pay0 pt=96   dreamaudiosource ! aacparse ! rtpmp4apay name=pay1 pt=97 )");

	gst_rtsp_mount_points_add_factory (mounts, "/stream", factory);
	g_object_unref (mounts);

	gst_rtsp_server_attach (server, NULL);

	g_print ("dreambox encoder stream ready at rtsp://127.0.0.1:8554/stream\n");
	g_main_loop_run (loop);

	return 0;
}
