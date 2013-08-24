/* Generic video mixer plugin
 * Copyright (C) 2004, 2008 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-videomixer
 *
 * Videomixer2 can accept AYUV, ARGB and BGRA video streams. For each of the requested
 * sink pads it will compare the incoming geometry and framerate to define the
 * output parameters. Indeed output video frames will have the geometry of the
 * biggest incoming video stream and the framerate of the fastest incoming one.
 *
 * Videomixer will do colorspace conversion.
 * 
 * Individual parameters for each input stream can be configured on the
 * #GstVideoMixer2Pad.
 *
 * <refsect2>
 * <title>Sample pipelines</title>
 * |[
 * gst-launch-1.0 \
 *   videotestsrc pattern=1 ! \
 *   video/x-raw,format=AYUV,framerate=\(fraction\)10/1,width=100,height=100 ! \
 *   videobox border-alpha=0 top=-70 bottom=-70 right=-220 ! \
 *   videomixer name=mix sink_0::alpha=0.7 sink_1::alpha=0.5 ! \
 *   videoconvert ! xvimagesink \
 *   videotestsrc ! \
 *   video/x-raw,format=AYUV,framerate=\(fraction\)5/1,width=320,height=240 ! mix.
 * ]| A pipeline to demonstrate videomixer used together with videobox.
 * This should show a 320x240 pixels video test source with some transparency
 * showing the background checker pattern. Another video test source with just
 * the snow pattern of 100x100 pixels is overlayed on top of the first one on
 * the left vertically centered with a small transparency showing the first
 * video test source behind and the checker pattern under it. Note that the
 * framerate of the output video is 10 frames per second.
 * |[
 * gst-launch-1.0 videotestsrc pattern=1 ! \
 *   video/x-raw, framerate=\(fraction\)10/1, width=100, height=100 ! \
 *   videomixer name=mix ! videoconvert ! ximagesink \
 *   videotestsrc !  \
 *   video/x-raw, framerate=\(fraction\)5/1, width=320, height=240 ! mix.
 * ]| A pipeline to demostrate bgra mixing. (This does not demonstrate alpha blending). 
 * |[
 * gst-launch-1.0 videotestsrc pattern=1 ! \
 *   video/x-raw,format =I420, framerate=\(fraction\)10/1, width=100, height=100 ! \
 *   videomixer name=mix ! videoconvert ! ximagesink \
 *   videotestsrc ! \
 *   video/x-raw,format=I420, framerate=\(fraction\)5/1, width=320, height=240 ! mix.
 * ]| A pipeline to test I420
 * |[
 * gst-launch-1.0 videomixer name=mixer sink_1::alpha=0.5 sink_1::xpos=50 sink_1::ypos=50 ! \
 *   videoconvert ! ximagesink \
 *   videotestsrc pattern=snow timestamp-offset=3000000000 ! \
 *   "video/x-raw,format=AYUV,width=640,height=480,framerate=(fraction)30/1" ! \
 *   timeoverlay ! queue2 ! mixer. \
 *   videotestsrc pattern=smpte ! \
 *   "video/x-raw,format=AYUV,width=800,height=600,framerate=(fraction)10/1" ! \
 *   timeoverlay ! queue2 ! mixer.
 * ]| A pipeline to demonstrate synchronized mixing (the second stream starts after 3 seconds)
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "videomixer2.h"
#include "videoconvert.h"

#ifdef DISABLE_ORC
#define orc_memset memset
#else
#include <orc/orcfunctions.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_videomixer2_debug);
#define GST_CAT_DEFAULT gst_videomixer2_debug

#define FORMATS " { AYUV, BGRA, ARGB, RGBA, ABGR, Y444, Y42B, YUY2, UYVY, "\
                "   YVYU, I420, YV12, NV12, NV21, Y41B, RGB, BGR, xRGB, xBGR, "\
                "   RGBx, BGRx } "

#define DEFAULT_PAD_ZORDER 0
#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_ALPHA  1.0
enum
{
  PROP_PAD_0,
  PROP_PAD_ZORDER,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_ALPHA
};

/* GstVideoMixer2 */
#define DEFAULT_BACKGROUND VIDEO_MIXER2_BACKGROUND_CHECKER
enum
{
  PROP_0,
  PROP_BACKGROUND
};

#define GST_TYPE_VIDEO_MIXER2_BACKGROUND (gst_videomixer2_background_get_type())
static GType
gst_videomixer2_background_get_type (void)
{
  static GType video_mixer_background_type = 0;

  static const GEnumValue video_mixer_background[] = {
    {VIDEO_MIXER2_BACKGROUND_CHECKER, "Checker pattern", "checker"},
    {VIDEO_MIXER2_BACKGROUND_BLACK, "Black", "black"},
    {VIDEO_MIXER2_BACKGROUND_WHITE, "White", "white"},
    {VIDEO_MIXER2_BACKGROUND_TRANSPARENT,
        "Transparent Background to enable further mixing", "transparent"},
    {0, NULL, NULL},
  };

  if (!video_mixer_background_type) {
    video_mixer_background_type =
        g_enum_register_static ("GstVideoMixer2Background",
        video_mixer_background);
  }
  return video_mixer_background_type;
}

static void
gst_videomixer2_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVideoMixer2 *mix = GST_VIDEO_MIXER2 (object);

  switch (prop_id) {
    case PROP_BACKGROUND:
      g_value_set_enum (value, mix->background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videomixer2_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVideoMixer2 *mix = GST_VIDEO_MIXER2 (object);

  switch (prop_id) {
    case PROP_BACKGROUND:
      mix->background = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#define gst_videomixer2_parent_class parent_class
G_DEFINE_TYPE (GstVideoMixer2, gst_videomixer2, GST_TYPE_BASE_MIXER);

static GstBasemixerPad *
gst_videomixer2_create_new_pad (GstBasemixer * basemixer,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{

}

/* GObject boilerplate */
static void
gst_videomixer2_class_init (GstVideoMixer2Class * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBasemixerClass *basemixer_class = (GstBasemixerClass *) klass;

  gobject_class->get_property = gst_videomixer2_get_property;
  gobject_class->set_property = gst_videomixer2_set_property;

  basemixer_class->create_new_pad = gst_videomixer2_create_new_pad;

  g_object_class_install_property (gobject_class, PROP_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_TYPE_VIDEO_MIXER2_BACKGROUND,
          DEFAULT_BACKGROUND, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "Video mixer 2",
      "Filter/Editor/Video",
      "Mix multiple video streams", "Wim Taymans <wim@fluendo.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_videomixer2_init (GstVideoMixer2 * mix)
{
  mix->background = DEFAULT_BACKGROUND;
  /* initialize variables */
}

/* Element registration */
static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_videomixer2_debug, "videomixer", 0,
      "video mixer");

  gst_video_mixer_init_blend ();

  return gst_element_register (plugin, "videomixer", GST_RANK_PRIMARY,
      GST_TYPE_VIDEO_MIXER2);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videomixer,
    "Video mixer", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
