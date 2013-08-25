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
#include "videomixer2pad.h"
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

G_DEFINE_TYPE (GstVideomixer2Pad, gst_videomixer2_pad, GST_TYPE_BASE_MIXER_PAD);

static void
gst_videomixer2_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideomixer2Pad *pad = GST_VIDEO_MIXER2_PAD (object);

  switch (prop_id) {
    case PROP_PAD_ZORDER:
      g_value_set_uint (value, pad->zorder);
      break;
    case PROP_PAD_XPOS:
      g_value_set_int (value, pad->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, pad->ypos);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, pad->alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videomixer2_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideomixer2Pad *pad = GST_VIDEO_MIXER2_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      pad->xpos = g_value_get_int (value);
      break;
    case PROP_PAD_YPOS:
      pad->ypos = g_value_get_int (value);
      break;
    case PROP_PAD_ALPHA:
      pad->alpha = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videomixer2_pad_class_init (GstVideomixer2PadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_videomixer2_pad_set_property;
  gobject_class->get_property = gst_videomixer2_pad_get_property;

  g_object_class_install_property (gobject_class, PROP_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_videomixer2_pad_init (GstVideomixer2Pad * mixerpad)
{
  mixerpad->xpos = DEFAULT_PAD_XPOS;
  mixerpad->ypos = DEFAULT_PAD_YPOS;
  mixerpad->alpha = DEFAULT_PAD_ALPHA;
}


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
  GstVideomixer2Pad *new_pad;

  new_pad = g_object_new (GST_TYPE_VIDEO_MIXER2_PAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);

  return GST_BASE_MIXER_PAD (new_pad);
}

static gboolean
gst_videomixer2_modify_src_pad_info (GstBasemixer * mix, GstVideoInfo * info)
{
  GSList *l;
  gint best_width = -1, best_height = -1;
  gboolean ret = FALSE;

  for (l = mix->sinkpads; l; l = l->next) {
    GstBasemixerPad *base_mixer_pad = l->data;
    GstVideomixer2Pad *video_mixer_pad = GST_VIDEO_MIXER2_PAD (base_mixer_pad);
    gint this_width, this_height;
    gint width, height;

    width = GST_VIDEO_INFO_WIDTH (&base_mixer_pad->info);
    height = GST_VIDEO_INFO_HEIGHT (&base_mixer_pad->info);

    if (width == 0 || height == 0)
      continue;

    this_width = width + MAX (video_mixer_pad->xpos, 0);
    this_height = height + MAX (video_mixer_pad->ypos, 0);

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;
  }

  if (best_width > 0 && best_height > 0) {
    gst_video_info_set_format (info, GST_VIDEO_INFO_FORMAT (info),
        best_width, best_height);
    ret = TRUE;
  }

  return ret;
}

static GstFlowReturn
gst_videomixer2_mix_frames (GstBasemixer * mix, GstVideoFrame * outframe)
{
  GSList *l;
  BlendFunction composite;

  /* default to blending */
  composite = mix->blend;
  switch (mix->background) {
    case BASE_MIXER_BACKGROUND_CHECKER:
      mix->fill_checker (outframe);
      break;
    case BASE_MIXER_BACKGROUND_BLACK:
      mix->fill_color (outframe, 16, 128, 128);
      break;
    case BASE_MIXER_BACKGROUND_WHITE:
      mix->fill_color (outframe, 240, 128, 128);
      break;
    case BASE_MIXER_BACKGROUND_TRANSPARENT:
    {
      guint i, plane, num_planes, height;

      num_planes = GST_VIDEO_FRAME_N_PLANES (outframe);
      for (plane = 0; plane < num_planes; ++plane) {
        guint8 *pdata;
        gsize rowsize, plane_stride;

        pdata = GST_VIDEO_FRAME_PLANE_DATA (outframe, plane);
        plane_stride = GST_VIDEO_FRAME_PLANE_STRIDE (outframe, plane);
        rowsize = GST_VIDEO_FRAME_COMP_WIDTH (outframe, plane)
            * GST_VIDEO_FRAME_COMP_PSTRIDE (outframe, plane);
        height = GST_VIDEO_FRAME_COMP_HEIGHT (outframe, plane);
        for (i = 0; i < height; ++i) {
          memset (pdata, 0, rowsize);
          pdata += plane_stride;
        }
      }

      /* use overlay to keep background transparent */
      composite = mix->overlay;
      break;
    }
  }

  for (l = mix->sinkpads; l; l = l->next) {
    GstBasemixerPad *pad = l->data;
    GstVideomixer2Pad *mixer_pad = GST_VIDEO_MIXER2_PAD (pad);

    if (pad->mixed_frame != NULL) {
      composite (pad->mixed_frame, mixer_pad->xpos, mixer_pad->ypos,
          mixer_pad->alpha, outframe);
    }
  }

  return GST_FLOW_OK;
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
  basemixer_class->modify_src_pad_info = gst_videomixer2_modify_src_pad_info;
  basemixer_class->mix_frames = gst_videomixer2_mix_frames;

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
