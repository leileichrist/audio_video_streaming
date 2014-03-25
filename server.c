#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/pbutils/pbutils.h>

#include <gdk/gdk.h>
#if defined (GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#elif defined (GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#elif defined (GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartz.h>
#endif

#define FILE_NAME "/recorded_video.mkv"
#define TEMP_FILE_NAME "video.mkv"

gchar* video_codec=NULL;
gchar* audio_codec=NULL;
gchar* container_format=NULL;
char* FILE_LOCATION=NULL;


typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *filesrc;
  GstElement *demuxer;
  GstElement *video_filter;
  GstElement *audio_filter;
  GstElement *video_decoder, *audio_decoder;
  GstElement *audio_queue;
  GstElement *video_queue;
  GstElement *video_sink, *audio_sink;
  GstCaps *video_caps;
  GstCaps *audio_caps;
  GstDiscoverer *discoverer;

  GtkWidget *slider;              /* Slider widget to keep track of current position */
  GtkWidget *streams_list;        /* Text widget to display info about the streams */
  gulong slider_update_signal_id; /* Signal ID for the slider update signal */
  
  GstState state;                 /* Current state of the pipeline */
  gint64 duration;                /* Duration of the clip, in nanoseconds */

  GtkWidget *time_text; 
  GtkTextBuffer *buffer;

} CustomData;

static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data);
static void print_tag_foreach (const GstTagList *tags, const gchar *tag, gpointer user_data);
static void find_tag_foreach (const GstTagList *tags, const gchar *tag, gpointer user_data);

static void realize_cb (GtkWidget *widget, CustomData *data) {
  GdkWindow *window = gtk_widget_get_window (widget);
  guintptr window_handle;
  
  if (!gdk_window_ensure_native (window))
    g_error ("Couldn't create native window needed for GstXOverlay!");
  
  /* Retrieve window handler from GDK */
#if defined (GDK_WINDOWING_WIN32)
  window_handle = (guintptr)GDK_WINDOW_HWND (window);
#elif defined (GDK_WINDOWING_QUARTZ)
  window_handle = gdk_quartz_window_get_nsview (window);
#elif defined (GDK_WINDOWING_X11)
  window_handle = GDK_WINDOW_XID (window);
#endif
  /* Pass it to pipeline, which implements XOverlay and will forward it to the video sink */
  gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->video_sink), window_handle);
}
  
/* This function is called when the PLAY button is clicked */
static void play_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}
  
/* This function is called when the PAUSE button is clicked */
static void pause_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}
  
/* This function is called when the STOP button is clicked */
static void stop_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->pipeline, GST_STATE_READY);
}


static void ff_cb(GtkButton *button, CustomData *data)
{
  gint64 current=-1;
  GstFormat fmt = GST_FORMAT_TIME;

  if (gst_element_query_position (data->pipeline, &fmt, &current)) 
  {
    current+=3*GST_SECOND;
    // if(current>data->duration) current=data->duration;

    gst_element_seek_simple (data->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                                    current );

    /* Block the "value-changed" signal, so the slider_cb function is not called
     * (which would trigger a seek the user has not requested) */
    g_signal_handler_block (data->slider, data->slider_update_signal_id);
    /* Set the position of the slider to the current pipeline positoin, in SECONDS */
    gtk_range_set_value (GTK_RANGE (data->slider), (gdouble)current / GST_SECOND);
    /* Re-enable the signal */
    g_signal_handler_unblock (data->slider, data->slider_update_signal_id);
  }

  g_print("fast forwarding to %d second \n", (int)(current/GST_SECOND ) );

  return;
}

static void rw_cb(GtkButton *button, CustomData *data)
{
  gint64 current=-1;
  GstFormat fmt = GST_FORMAT_TIME;

  if (gst_element_query_position (data->pipeline, &fmt, &current)) 
  {
    current-=5*GST_SECOND;
    if(current<0) current=0;
    gst_element_seek_simple (data->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                                    current );

    /* Block the "value-changed" signal, so the slider_cb function is not called
     * (which would trigger a seek the user has not requested) */
    g_signal_handler_block (data->slider, data->slider_update_signal_id);
    /* Set the position of the slider to the current pipeline positoin, in SECONDS */
    gtk_range_set_value (GTK_RANGE (data->slider), (gdouble)current / GST_SECOND);
    /* Re-enable the signal */
    g_signal_handler_unblock (data->slider, data->slider_update_signal_id);
  }

  g_print("rewinding to %d second \n", (int)(current/GST_SECOND ) );

  return;
}

  
/* This function is called when the main window is closed */
static void delete_event_cb (GtkWidget *widget, GdkEvent *event, CustomData *data) {
  stop_cb (NULL, data);
  gtk_main_quit ();
}
  
/* This function is called everytime the video window needs to be redrawn (due to damage/exposure,
 * rescaling, etc). GStreamer takes care of this in the PAUSED and PLAYING states, otherwise,
 * we simply draw a black rectangle to avoid garbage showing up. */
static gboolean expose_cb (GtkWidget *widget, GdkEventExpose *event, CustomData *data) {
  if (data->state < GST_STATE_PAUSED) {
    GtkAllocation allocation;
    GdkWindow *window = gtk_widget_get_window (widget);
    cairo_t *cr;
    
    /* Cairo is a 2D graphics library which we use here to clean the video window.
     * It is used by GStreamer for other reasons, so it will always be available to us. */
    gtk_widget_get_allocation (widget, &allocation);
    cr = gdk_cairo_create (window);
    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
    cairo_fill (cr);
    cairo_destroy (cr);
  }
  
  return FALSE;
}
  
/* This function is called when the slider changes its position. We perform a seek to the
 * new position here. */
static void slider_cb (GtkRange *range, CustomData *data) {
  gdouble value = gtk_range_get_value (GTK_RANGE (data->slider));
  gst_element_seek_simple (data->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      (gint64)(value * GST_SECOND));
}

  
static void file_ok_sel( GtkWidget        *w,
                         GtkFileSelection *fs )
{
    asprintf(&FILE_LOCATION, "%s", gtk_file_selection_get_filename (GTK_FILE_SELECTION (fs)));
    g_print ("selecting file: ... %s\n",  FILE_LOCATION );
    gtk_main_quit();
}

/* This creates all the GTK+ widgets that compose our application, and registers the callbacks */
static void create_ui (CustomData *data) {
  GtkWidget *main_window;  /* The uppermost window, containing all other windows */
  GtkWidget *video_window; /* The drawing area where the video will be shown */
  GtkWidget *main_box;     /* VBox to hold main_hbox and the controls */
  GtkWidget *main_hbox;    /* HBox to hold the video_window and the stream info text widget */
  GtkWidget *controls;     /* HBox to hold the buttons and the slider */
  GtkWidget *play_button, *pause_button, *stop_button, *fast_forward, *rewind_button; /* Buttons *//* Buttons */


  main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (main_window), "delete-event", G_CALLBACK (delete_event_cb), data);
  
  video_window = gtk_drawing_area_new ();
  gtk_widget_set_double_buffered (video_window, FALSE);
  g_signal_connect (video_window, "realize", G_CALLBACK (realize_cb), data);
  g_signal_connect (video_window, "expose_event", G_CALLBACK (expose_cb), data);
  
  play_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PLAY);
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb), data);
  
  pause_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PAUSE);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb), data);
  
  stop_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_STOP);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb), data);

  fast_forward = gtk_button_new_from_stock(GTK_STOCK_MEDIA_FORWARD);
  g_signal_connect(G_OBJECT(fast_forward), "clicked", G_CALLBACK(ff_cb), data);

  rewind_button= gtk_button_new_from_stock(GTK_STOCK_MEDIA_REWIND);
  g_signal_connect(G_OBJECT(rewind_button), "clicked", G_CALLBACK(rw_cb), data);
  
  data->slider = gtk_hscale_new_with_range (0, 100, 1);
  gtk_scale_set_draw_value (GTK_SCALE (data->slider), 0);
  data->slider_update_signal_id = g_signal_connect (G_OBJECT (data->slider), "value-changed", G_CALLBACK (slider_cb), data);
  
   data->time_text=gtk_text_view_new ();
   data->buffer=gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->time_text));
   gtk_text_buffer_set_text (data->buffer, "00:00", -1);
   data->buffer=gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->time_text));

  data->streams_list = gtk_text_view_new ();
  gtk_text_view_set_editable (GTK_TEXT_VIEW (data->streams_list), FALSE);
  
  controls = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (controls), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), rewind_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), fast_forward, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), data->slider, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (controls), data->time_text, FALSE, FALSE, 0);

  main_hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), video_window, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), data->streams_list, FALSE, FALSE, 2);
  
  main_box = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), main_hbox, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), controls, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (main_window), main_box);
  gtk_window_set_default_size (GTK_WINDOW (main_window), 640, 480);
  
   gtk_widget_show_all (main_window);
}
  
/* This function is called periodically to refresh the GUI */
static gboolean refresh_ui (CustomData *data) {
  GstFormat fmt = GST_FORMAT_TIME;
  gint64 current = -1;
  gchar* buf=NULL;
  
  /* We do not want to update anything unless we are in the PAUSED or PLAYING states */
  if (data->state < GST_STATE_PAUSED)
    return TRUE;
  
  /* If we didn't know it yet, query the stream duration */
  if (!GST_CLOCK_TIME_IS_VALID (data->duration)) {
    if (!gst_element_query_duration (data->pipeline, &fmt, &data->duration)) {  
      g_printerr ("Could not query current duration.\n");
    } else {
      /* Set the range of the slider to the clip duration, in SECONDS */
      gtk_range_set_range (GTK_RANGE (data->slider), 0, (gdouble)data->duration / GST_SECOND);
      g_print("setting slider length to %f ", (gdouble)data->duration / GST_SECOND);
    }
  }

  // gst_element_query_duration (data->pipeline, &fmt, &data->duration);
  //     g_print("setting slider length to %f ", (gdouble)data->duration / GST_SECOND);


  if (gst_element_query_position (data->pipeline, &fmt, &current)) {
    /* Block the "value-changed" signal, so the slider_cb function is not called
     * (which would trigger a seek the user has not requested) */
    g_signal_handler_block (data->slider, data->slider_update_signal_id);
    /* Set the position of the slider to the current pipeline positoin, in SECONDS */
    gtk_range_set_value (GTK_RANGE (data->slider), (gdouble)current / GST_SECOND);
    /* Re-enable the signal */
    g_signal_handler_unblock (data->slider, data->slider_update_signal_id);
  }
  gdouble curr=(gdouble)current / GST_SECOND;
  int  curr_=(int)curr;

  asprintf(&buf, "%d:%d", curr_, (int)(curr-curr_)*100 );
  gtk_text_buffer_set_text(data->buffer, buf, -1);


  // g_print("current: %d \n", (int)(current/GST_SECOND) );
  return TRUE;
}

/* This function is called when new metadata is discovered in the stream */
static void tags_cb (GstElement *pipeline, gint stream, CustomData *data) {
  /* We are possibly in a GStreamer working thread, so we notify the main
   * thread of this event through a message in the bus */
  printf("IN TAGS_CB!\n");
  gst_element_post_message (pipeline,
    gst_message_new_application (GST_OBJECT (pipeline),
      gst_structure_new ("tags-changed", NULL)));
}
  
/* This function is called when an error message is posted on the bus */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;
  
  /* Print error details on the screen */
  gst_message_parse_error (msg, &err, &debug_info);
  g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
  g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
  g_clear_error (&err);
  g_free (debug_info);
  
  /* Set the pipeline to READY (which stops playback) */
  gst_element_set_state (data->pipeline, GST_STATE_READY);
}
  
/* This function is called when an End-Of-Stream message is posted on the bus.
 * We just set the pipeline to READY (which stops playback) */
static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  g_print ("End-Of-Stream reached.\n");
  gst_element_set_state (data->pipeline, GST_STATE_READY);
}
  
/* This function is called when the pipeline changes states. We use it to
 * keep track of the current state. */
static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    data->state = new_state;
    g_print ("State set to %s\n", gst_element_state_get_name (new_state));
    if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
      /* For extra responsiveness, we refresh the GUI as soon as we reach the PAUSED state */
      refresh_ui (data);
    }
  }
}

/* Extract metadata from all the streams and write it to the text widget in the GUI */
static void analyze_streams (CustomData *data) {
  gint i;
  GstTagList *tags;
  gchar *str, *total_str;
  guint rate;
  gint n_video, n_audio, n_text;
  GtkTextBuffer *text;
  
  /* Clean current contents of the widget */
  text = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->streams_list));
  gtk_text_buffer_set_text (text, "", -1);
  
  /* Read some properties */
  g_object_get (data->pipeline, "n-video", &n_video, NULL);
  g_object_get (data->pipeline, "n-audio", &n_audio, NULL);
  g_object_get (data->pipeline, "n-text", &n_text, NULL);
  
  for (i = 0; i < n_video; i++) {
    tags = NULL;
    /* Retrieve the stream's video tags */
    g_signal_emit_by_name (data->pipeline, "get-video-tags", i, &tags);
    if (tags) {
      total_str = g_strdup_printf ("video stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      gst_tag_list_get_string (tags, GST_TAG_VIDEO_CODEC, &str);
      total_str = g_strdup_printf ("  codec: %s\n", str ? str : "unknown");
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      g_free (str);
      gst_tag_list_free (tags);
    }
  }
  
  for (i = 0; i < n_audio; i++) {
    tags = NULL;
    /* Retrieve the stream's audio tags */
    g_signal_emit_by_name (data->pipeline, "get-audio-tags", i, &tags);
    if (tags) {
      total_str = g_strdup_printf ("\naudio stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      if (gst_tag_list_get_string (tags, GST_TAG_AUDIO_CODEC, &str)) {
        total_str = g_strdup_printf ("  codec: %s\n", str);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (total_str);
        g_free (str);
      }
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        total_str = g_strdup_printf ("  language: %s\n", str);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (total_str);
        g_free (str);
      }
      if (gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &rate)) {
        total_str = g_strdup_printf ("  bitrate: %d\n", rate);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (total_str);
      }
      gst_tag_list_free (tags);
    }
  }
  
  for (i = 0; i < n_text; i++) {
    tags = NULL;
    /* Retrieve the stream's subtitle tags */
    g_signal_emit_by_name (data->pipeline, "get-text-tags", i, &tags);
    if (tags) {
      total_str = g_strdup_printf ("\nsubtitle stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        total_str = g_strdup_printf ("  language: %s\n", str);
        gtk_text_buffer_insert_at_cursor (text, total_str, -1);
        g_free (total_str);
        g_free (str);
      }
      gst_tag_list_free (tags);
    }
  }
}
  
/* This function is called when an "application" message is posted on the bus.
 * Here we retrieve the message posted by the tags_cb callback */
static void application_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  if (g_strcmp0 (gst_structure_get_name (msg->structure), "tags-changed") == 0) {
    /* If the message is the "tags-changed" (only one we are currently issuing), update
     * the stream info GUI */
    analyze_streams (data);
  }
}

static void create_file_ui (CustomData *data) {

  GtkWidget *filew = gtk_file_selection_new("Select a video to play");
  g_signal_connect (filew, "destroy",
                G_CALLBACK (gtk_main_quit), NULL);
    /* Connect the ok_button to file_ok_sel function */
    g_signal_connect (GTK_FILE_SELECTION (filew)->ok_button,
          "clicked", G_CALLBACK (file_ok_sel), (gpointer) filew);
    
    /* Connect the cancel_button to destroy the widget */
    g_signal_connect_swapped (GTK_FILE_SELECTION (filew)->cancel_button,
                        "clicked", G_CALLBACK (gtk_widget_destroy),filew);
    
    /* Lets set the filename, as if this were a save dialog, and we are giving
     a default filename */
    gtk_file_selection_set_filename (GTK_FILE_SELECTION(filew), 
             "video.mkv");
    gtk_widget_show (filew);
    gtk_main();
    gtk_widget_destroy(filew);
}

  
int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;
  GstElementFactory *sink_factory, *source_factory;
  GstMessage *msg;
  GstStateChangeReturn ret;
  GstDiscovererInfo* fileInfo;
  const GstTagList* tagList;
  GError *err = NULL;
  gboolean terminate = FALSE;
  char* filename=NULL;
  char* uri=NULL;
  char cwd[1024];
  gchar* audio_type=NULL;
  gchar* video_type=NULL;
  asprintf(&filename, "%s", TEMP_FILE_NAME);
  gtk_init (&argc, &argv);

  create_file_ui(&data);


  if (getcwd(cwd, sizeof(cwd)) != NULL)
    fprintf(stdout, "Current working dir: %s\n", cwd);
  else
  {
    perror("getcwd_error");
    return -1;
  }

  g_print("file location is :  %s\n", FILE_LOCATION);
  if(asprintf(&uri, "%s%s", "file:", FILE_LOCATION)<0)
  {
    perror("cannot find file directory\n");
    return -1;
  }

  memset (&data, 0, sizeof (data));
  gst_init(&argc, &argv);


  data.discoverer = gst_discoverer_new (5 * GST_SECOND, &err);

  if (!data.discoverer) {
    g_print ("Error creating discoverer instance: %s\n", err->message);
    g_clear_error (&err);
    return -1;
  }

  fileInfo = gst_discoverer_discover_uri(data.discoverer, uri , &err);
  if(!gst_discoverer_info_get_seekable(fileInfo)) 
  {
    g_print("uri cannot be seeked! \n");
    return -1;
  }

  tagList = gst_discoverer_info_get_tags (fileInfo);
  if (tagList) {
    gst_tag_list_foreach (tagList, find_tag_foreach, GINT_TO_POINTER (1));
    gst_tag_list_foreach (tagList, print_tag_foreach, GINT_TO_POINTER (1));

  }

  // printf("%s\n", video_codec);
  // printf("%s\n", audio_codec);
  // printf("%s\n", container_format);

  
  /* Create pipeline and attach a callback to it's
   * message bus */
  data.pipeline = gst_pipeline_new("player");
  
  /* Create elements */
  data.filesrc = gst_element_factory_make("filesrc", "filesrc");
  if(g_str_has_suffix(FILE_LOCATION,"mkv"))
  {
    data.demuxer = gst_element_factory_make("matroskademux", "demuxer");
    printf("file is .mkv type\n");    
  }
  else if(g_str_has_suffix(FILE_LOCATION, "avi"))
  {
    data.demuxer = gst_element_factory_make("avidemux", "demuxer");
    printf("file is .avi type\n");    
  }
  else
  {
    printf("unknown file type !!!\n");
    if(FILE_LOCATION) free(FILE_LOCATION);
    gst_element_set_state (data.pipeline, GST_STATE_NULL);
    gst_object_unref (data.pipeline);
  }

  // if(strstr (video_codec,"MPEG"))
  // {
  //   printf("MPEG video\n");
  //   data.video_decoder = gst_element_factory_make("ffdec_mpeg4", "video_decoder");

  // }
  // else if(strstr (video_codec,"JPEG") ){
  //   printf("JPEG video\n");
  //   data.video_decoder = gst_element_factory_make("jpegdec", "video_decoder");

  // }
  // else{
  //   g_print("unsupported video type!\n");
  //   return -1;
  // }

  // if( g_str_has_prefix (audio_codec, "Vorbis" ) ){
  //   g_print("Vorbis audio\n");
  //   data.audio_decoder = gst_element_factory_make("vorbisdec", "audio_decoder");    
  // }
  // else if( g_str_has_prefix (audio_codec, "Mu" ) ){
  //   g_print("Mulaw audio\n");
  //   data.audio_decoder = gst_element_factory_make("mulawdec", "audio_decoder");    
  // }
  // else if( g_str_has_prefix (audio_codec, "A" ) ){
  //   printf("a-law audio\n");
  //   data.audio_decoder = gst_element_factory_make("alawdec", "audio_decoder");    
  // }
  // else{
  //      g_print("unsupported audio type!\n");
  //      return -1; 
  // }

  data.audio_queue = gst_element_factory_make("queue", "audio_queue");
  data.video_queue = gst_element_factory_make("queue", "video_queue");

  data.video_filter = gst_element_factory_make("ffmpegcolorspace", "video_filter");
  data.audio_filter = gst_element_factory_make("audioconvert","audio_filter");

  data.video_decoder = gst_element_factory_make("jpegdec", "video_decoder");
  data.audio_decoder = gst_element_factory_make("vorbisdec", "audio_decoder");    

  data.video_sink = gst_element_factory_make("xvimagesink", "video_sink");
  data.audio_sink = gst_element_factory_make("autoaudiosink","audio_sink");

    /* Specify what kind of video is wanted at the server */
  data.video_caps = gst_caps_new_simple("video/x-raw-rgb",
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 480,
      NULL);

  data.audio_caps = gst_caps_new_simple("audio/x-raw-int",
      "rate", G_TYPE_INT, 8000,
      "channels", G_TYPE_INT, 2,
      "depths", G_TYPE_INT, 16,
      NULL);

  /* Check that elements are correctly initialized */
  if(!(data.pipeline && data.filesrc && data.demuxer && data.video_decoder && data.audio_decoder &&
  	   data.audio_queue && data.video_filter && data.audio_filter && data.video_queue && data.video_sink && data.audio_sink))
  {
    g_critical("Couldn't create pipeline elements");
    return FALSE;
  }

  g_object_set (data.filesrc, "location", FILE_LOCATION, NULL);

  /* Add elements to the pipeline. This has to be done prior to
   * linking them */
  gst_bin_add_many(GST_BIN(data.pipeline), data.filesrc, data.demuxer, data.video_decoder, data.audio_decoder,
  	   data.audio_queue, data.video_queue, data.video_filter, data.audio_filter, data.video_sink, data.audio_sink, NULL);

  if(!gst_element_link_many(data.filesrc, data.demuxer ,NULL))
  {
    gst_object_unref(data.pipeline);
    return FALSE;
  }

  if(!gst_element_link_filtered(data.video_queue, data.video_filter, data.video_caps ))
  {
    gst_caps_unref(data.video_caps);
    return FALSE;
  }

  if(!gst_element_link_filtered(data.audio_queue, data.audio_filter, data.audio_caps))
  {
    gst_caps_unref(data.audio_caps);
    return FALSE;
  }

  if(!gst_element_link_many(data.video_filter , data.video_decoder, data.video_sink, NULL))
  {
    g_printerr("video filter, video_decoder, and video_sink cannot be linked!.\n");
    gst_object_unref (data.pipeline);
    return FALSE;
  }
  if(!gst_element_link_many( data.audio_filter, data.audio_decoder, data.audio_sink, NULL))
  {
    g_printerr("audio_filter, audio_decoder, and audio_sink cannot be linked!.\n");
    gst_object_unref (data.pipeline);
    return FALSE;
  }


  g_signal_connect (data.demuxer, "pad-added", G_CALLBACK(pad_added_handler), &data);

  // g_signal_connect (G_OBJECT (data.video_sink), "video-tags-changed", (GCallback) tags_cb, &data);
  // g_signal_connect (G_OBJECT (data.audio_sink), "audio-tags-changed", (GCallback) tags_cb, &data);
  // g_signal_connect (G_OBJECT (data.pipeline), "text-tags-changed", (GCallback) tags_cb, &data);

  create_ui(&data);

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data.pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, &data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, &data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, &data);
  g_signal_connect (G_OBJECT (bus), "message::application", (GCallback)application_cb, &data);
  gst_object_unref (bus);


  ret=gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, &data);
  
  /* Start the GTK main loop. We will not regain control until gtk_main_quit is called. */
  gtk_main ();


  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;
}
   
static void find_tag_foreach (const GstTagList *tags, const gchar *tag, gpointer user_data){
  GValue val = { 0, };
  gchar *str;
  gint depth = GPOINTER_TO_INT (user_data);
   
  gst_tag_list_copy_value (&val, tags, tag);
   
  if (G_VALUE_HOLDS_STRING (&val))
    str = g_value_dup_string (&val);
  else
    str = gst_value_serialize (&val);
   
  if(strcmp(tag, "video-codec")==0){
      asprintf(&video_codec,"%s",str);
  } 
  else if(strcmp(tag, "audio-codec")==0) {
      asprintf(&audio_codec,"%s",str);
  }
  else if(strcmp(tag, "container-format")==0){
      asprintf(&container_format ,"%s",str);
  }
  else;  

  free(str);
  g_value_unset (&val);
}

/*
/* This function will be called by the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
  GstPad *video_queue_sink_pad = gst_element_get_static_pad (data->video_queue, "sink");
  GstPad *audio_queue_sink_pad = gst_element_get_static_pad (data->audio_queue, "sink");

  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;
   
  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
  if (gst_pad_is_linked(video_queue_sink_pad) && gst_pad_is_linked(audio_queue_sink_pad)) {
    g_print ("  We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_get_caps (new_pad);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);

  // g_print ("  It has type '%s' \n", new_pad_type);


  if (g_str_has_prefix (new_pad_type, "audio")) 
  {
    g_print ("  It has type '%s' \n", new_pad_type);
    ret = gst_pad_link (new_pad, audio_queue_sink_pad);
    if (GST_PAD_LINK_FAILED (ret)) 
    {
    	g_print ("  Type is '%s' but link failed.\n", new_pad_type);
  	} 
  	else 
  	{
    	g_print ("  Link succeeded (type '%s').\n", new_pad_type);
  	}
  }
  // else if (g_str_has_prefix (new_pad_type, "image"))
  else
  {
  	g_print ("  It has type '%s' \n", new_pad_type);
    ret = gst_pad_link (new_pad, video_queue_sink_pad);
    if (GST_PAD_LINK_FAILED (ret)) 
    {
    	g_print ("  Type is '%s' but link failed.\n", new_pad_type);
  	} 
  	else 
  	{
    	g_print ("  Link succeeded (type '%s').\n", new_pad_type);
  	}

  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);
   
  /* Unreference the sink pad */
  gst_object_unref (video_queue_sink_pad);
  gst_object_unref (audio_queue_sink_pad);
}




/* Print a tag in a human-readable format (name: value) */
static void print_tag_foreach (const GstTagList *tags, const gchar *tag, gpointer user_data) {
  GValue val = { 0, };
  gchar *str;
  gint depth = GPOINTER_TO_INT (user_data);
   
  gst_tag_list_copy_value (&val, tags, tag);
   
  if (G_VALUE_HOLDS_STRING (&val))
    str = g_value_dup_string (&val);
  else
    str = gst_value_serialize (&val);
   
  g_print ("%*s%s: %s\n", 2 * depth, " ", gst_tag_get_nick (tag), str);
  g_free (str);
   
  g_value_unset (&val);
}
   
