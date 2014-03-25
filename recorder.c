#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>

#include <gdk/gdk.h>
#if defined (GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#elif defined (GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#elif defined (GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartz.h>
#endif

// #define FILE_LOCATION "/home/calvinhmw/Spring2014/CS414/mps/mp1_test/camera/recorded_video.mkv"
// #define FILE_NAME "/recorded_video.mkv"
 #define FILE_NAME_MKV "recorded_video.mkv"
 #define FILE_NAME_AVI "recorded_video.avi"

gchar* filename=NULL;


/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *camera_video_src;
  GstElement *camera_audio_src;
  GstElement *video_filter;
  GstElement *audio_filter;
  GstElement *video_queue_store, *video_encoder, *muxer, *video_file_sink;
  GstElement *screen_queue_play, *video_sink;
  GstElement *audio_queue;
  GstElement *audio_encoder;
  GstElement *video_tee;
  GstCaps *caps;
  GstCaps *audio_caps;

  GstPadTemplate *tee_src_pad_template, *muxer_src_pad_template, *muxer_src_audioPad_template;
  GstPad *encoder_video_1_pad, *encoder_audio_pad, *muxer_video_1_pad, *muxer_audio_pad;
  GstPad *tee_video_play_pad, *tee_video_encode_pad; 
  GstPad *video_queue_play_pad, *video_queue_encode_pad;


	///gui
  //GtkWidget *slider;              /* Slider widget to keep track of current position */
  GtkWidget *text;
  GtkWidget *time_text;	
  GtkTextBuffer *buffer;
  GtkTextBuffer *buffer2;
  GtkWidget *streams_list;        /* Text widget to display info about the streams */
  gulong slider_update_signal_id; /* Signal ID for the slider update signal */
   
  GstState state;                 /* Current state of the pipeline */
  gint64 duration;                /* Duration of the clip, in nanoseconds */
} CustomData;


enum vf
{
 mpeg4,
 jpeg
} video_format;

enum af
{
 vorbis,
 alaw,
 mulaw
} audio_format;

enum mf
{
 mkv,
 avi
} muxer_format;
static void usage(int argc, char **argv)
{
        
         printf( "Usage: %s [video_format] [audio_format] [muxer_format] [filename]\n\n"
                 "[video_format] [audio_format] [muxer_format]\n"
                 "m | mpeg4       v | vorbis     m | mkv  \n"
                 "j | jpeg        a | alaw       a | avi  \n"
                 "                m | mulaw               \n", argv[0]);
}

int start_time=0;
char strbuf[20];
int ready=0;


static void get_file_name(GtkWidget   *w,
                          GtkEntry    *entry)
{
  const gchar *entry_text;
  entry_text = gtk_entry_get_text (GTK_ENTRY (entry));
  if(strlen(entry_text)>0) 
  {
    asprintf(&filename, "%s", entry_text);
    printf ("Entry contents: %s\n", filename);
  }
  gtk_main_quit();
}
static void create_file_ui (void) {
    GtkWidget *window;
    GtkWidget *vbox;
    GtkWidget *entry;
    GtkWidget *button_ok;
    GtkWidget *button_cancel;
    GtkWidget *label= gtk_label_new("Enter the file name:");

    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_size_request (GTK_WIDGET (window), 400, 200);
    gtk_window_set_title (GTK_WINDOW (window), "Start ");
    g_signal_connect (window, "destroy",
                      G_CALLBACK (gtk_main_quit), NULL);
    g_signal_connect_swapped (window, "delete-event",
                              G_CALLBACK (gtk_widget_destroy), 
                              window);

    vbox = gtk_vbox_new (FALSE, 0);
    gtk_container_add (GTK_CONTAINER (window), vbox);

    gtk_box_pack_start (GTK_BOX(vbox), label, TRUE, TRUE, 0);


    entry = gtk_entry_new ();
    gtk_entry_set_max_length (GTK_ENTRY (entry), 50);
    gtk_box_pack_start (GTK_BOX(vbox), entry, TRUE, TRUE, 0);

    button_ok = gtk_button_new_from_stock (GTK_STOCK_OK );
    g_signal_connect(button_ok, "clicked",
                              G_CALLBACK (get_file_name),
                              entry);
    gtk_box_pack_start (GTK_BOX (vbox), button_ok, TRUE, TRUE, 0);

     button_cancel = gtk_button_new_from_stock (GTK_STOCK_CANCEL );
    g_signal_connect_swapped(GTK_WIDGET(button_cancel), "clicked",
                              G_CALLBACK (gtk_widget_destroy),
                              window);

    gtk_box_pack_start (GTK_BOX (vbox), button_cancel, TRUE, TRUE, 0);

    gtk_widget_show_all(window);
    gtk_main();
    gtk_widget_destroy(window);
}

//======================================={========================================
/* Functions below print the Capabilities in a human-friendly format */
static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) {
  gchar *str = gst_value_serialize (value);
   
  g_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}
   
static void print_caps (const GstCaps * caps, const gchar * pfx) {
  guint i;
   
  g_return_if_fail (caps != NULL);
   
  if (gst_caps_is_any (caps)) {
    g_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    g_print ("%sEMPTY\n", pfx);
    return;
  }
   
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);
     
    g_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}
   
/* Prints information about a Pad Template, including its Capabilities */
static void print_pad_templates_information (GstElementFactory * factory) {
  const GList *pads;
  GstStaticPadTemplate *padtemplate;
   
  g_print ("Pad Templates for %s:\n", gst_element_factory_get_longname (factory));
  if (!factory->numpadtemplates) {
    g_print ("  none\n");
    return;
  }
   
  pads = factory->staticpadtemplates;
  while (pads) {
    padtemplate = (GstStaticPadTemplate *) (pads->data);
    pads = g_list_next (pads);
     
    if (padtemplate->direction == GST_PAD_SRC)
      g_print ("  SRC template: '%s'\n", padtemplate->name_template);
    else if (padtemplate->direction == GST_PAD_SINK)
      g_print ("  SINK template: '%s'\n", padtemplate->name_template);
    else
      g_print ("  UNKNOWN!!! template: '%s'\n", padtemplate->name_template);
     
    if (padtemplate->presence == GST_PAD_ALWAYS)
      g_print ("    Availability: Always\n");
    else if (padtemplate->presence == GST_PAD_SOMETIMES)
      g_print ("    Availability: Sometimes\n");
    else if (padtemplate->presence == GST_PAD_REQUEST) {
      g_print ("    Availability: On request\n");
    } else
      g_print ("    Availability: UNKNOWN!!!\n");
     
    if (padtemplate->static_caps.string) {
      g_print ("    Capabilities:\n");
      print_caps (gst_static_caps_get (&padtemplate->static_caps), "      ");
    }
     
    g_print ("\n");
  }
}
   
/* Shows the CURRENT capabilities of the requested pad in the given element */
static void print_pad_capabilities (GstElement *element, gchar *pad_name) {
  GstPad *pad = NULL;
  GstCaps *caps = NULL;
   
  /* Retrieve pad */
  pad = gst_element_get_static_pad (element, pad_name);
  if (!pad) {
    g_printerr ("Could not retrieve pad '%s'\n", pad_name);
    return;
  }
   
  /* Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet) */
  caps = gst_pad_get_negotiated_caps (pad);
  if (!caps)
    caps = gst_pad_get_caps_reffed (pad);
   
  /* Print and free */
  g_print ("Caps for the %s pad:\n", pad_name);
  print_caps (caps, "      ");
  gst_caps_unref (caps);
  gst_object_unref (pad);
}

//==========================================}=======================================


//char *text= ;
/* This function is called when an error message is posted on the bus */
static gboolean refresh_ui (CustomData *data) {
  GstFormat fmt = GST_FORMAT_TIME;
  gint64 current = -1;

  /* We do not want to update anything unless we are in the PAUSED or PLAYING states */
  if (data->state < GST_STATE_PAUSED)
    return TRUE;
   //if(data->state == GST_STATE_PLAYING)
	start_time++;
   int minute,second;
   minute=start_time/60;
   second=start_time%60;   
   sprintf(strbuf,"%d:%d\n",minute,second);
	//printf("second=%d\n",second);
//GtkTextBuffer *buffer2;
   //buffer2=gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->time_text));
   gtk_text_buffer_set_text (data->buffer2, strbuf, -1);
  /* If we didn't know it yet, query the stream duration */
  //if (!GST_CLOCK_TIME_IS_VALID (data->duration)) {
    //if (!gst_element_query_duration (data->pipeline, &fmt, &data->duration)) {
      //g_printerr ("Could not query current duration.\n");
    //} else {
      /* Set the range of the slider to the clip duration, in SECONDS */
	//changed
      //gtk_range_set_range (GTK_RANGE (data->slider), 0, (gdouble)data->duration / GST_SECOND);
    //}
  //}
   
 // if (gst_element_query_position (data->pipeline, &fmt, &current)) {
    /* Block the "value-changed" signal, so the slider_cb function is not called
     * (which would trigger a seek the user has not requested) */
	//changed
   // g_signal_handler_block (data->slider, data->slider_update_signal_id);

    /* Set the position of the slider to the current pipeline positoin, in SECONDS */
	//changed
    //gtk_range_set_value (GTK_RANGE (data->slider), (gdouble)current / GST_SECOND);

    /* Re-enable the signal */
	//changed
    //g_signal_handler_unblock (data->slider, data->slider_update_signal_id);
  //}
  return TRUE;
}
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
  /* Pass it to playbin2, which implements XOverlay and will forward it to the video sink */
  //changed
  //gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->playbin2), window_handle);
  gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->video_sink), window_handle);
}
   
/* This function is called when the PLAY button is clicked */
static void play_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, data);
}
   
/* This function is called when the PAUSE button is clicked */
static void pause_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}
   
/* This function is called when the STOP button is clicked */
static void stop_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->pipeline, GST_STATE_READY);
}

static void jpeg_cb (GtkButton *button, CustomData *data) {
  //gst_element_set_state (data->pipeline, GST_STATE_READY);
printf("jpeg chose\n");
  video_format=jpeg;
}
static void mpeg_cb (GtkButton *button, CustomData *data) {
  //gst_element_set_state (data->pipeline, GST_STATE_READY);
printf("mpeg4 chose\n");
  video_format=mpeg4;
}
static void vorbis_cb (GtkButton *button, CustomData *data) {
  //gst_element_set_state (data->pipeline, GST_STATE_READY);
printf("vorbis chose\n");
  audio_format=vorbis;
}
static void alaw_cb (GtkButton *button, CustomData *data) {
  //gst_element_set_state (data->pipeline, GST_STATE_READY);
printf("alaw chose\n");
  audio_format=alaw;
}
static void mulaw_cb (GtkButton *button, CustomData *data) {
  //gst_element_set_state (data->pipeline, GST_STATE_READY);
printf("mulaw chose\n");
  audio_format=mulaw;
}
static void mkv_cb (GtkButton *button, CustomData *data) {
  //gst_element_set_state (data->pipeline, GST_STATE_READY);
printf("mkv chose\n");
  muxer_format=mkv;
}
static void avi_cb (GtkButton *button, CustomData *data) {
  //gst_element_set_state (data->pipeline, GST_STATE_READY);
printf("avi chose\n");
  muxer_format=avi;
}

static void confirm_cb (GtkButton *button, CustomData *data) {
  //gst_element_set_state (data->pipeline, GST_STATE_READY);
  printf("before confirm\n");
  gtk_main_quit ();
//printf("before confirm!!\n");
  //ready=1;
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
/*
static void slider_cb (GtkRange *range, CustomData *data) {
  gdouble value = gtk_range_get_value (GTK_RANGE (data->slider));
  gst_element_seek_simple (data->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      (gint64)(value * GST_SECOND));
}*/
/* Handler for the pad-added signal */
// static void pad_added_handler (GstElement *src, GstPad *pad, CustomData *data);
static void create_ui (CustomData *data) {
	//printf("enter \n");
  GtkWidget *main_window;  /* The uppermost window, containing all other windows */
  GtkWidget *video_window; /* The drawing area where the video will be shown */
  GtkWidget *main_box;     /* VBox to hold main_hbox and the controls */
  GtkWidget *main_hbox;    /* HBox to hold the video_window and the stream info text widget */
  GtkWidget *controls,*control2,*control3,*control4,*control5;     /* HBox to hold the buttons and the slider */
  GtkWidget *play_button, *pause_button, *stop_button; /* Buttons */
   GtkWidget *jpeg_b, *mpeg_b,*vorbis, *alaw, *mulaw, *mkv, *avi,*confirm;
  main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  printf("reach p1\n");
  g_signal_connect (G_OBJECT (main_window), "delete-event", G_CALLBACK (delete_event_cb), data);
   printf("reach p2\n");
  video_window = gtk_drawing_area_new ();
	printf("reach p3\n");
  gtk_widget_set_double_buffered (video_window, FALSE);
  g_signal_connect (video_window, "realize", G_CALLBACK (realize_cb), data);
  g_signal_connect (video_window, "expose_event", G_CALLBACK (expose_cb), data);
   
  play_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PLAY);
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb), data);
   
  pause_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PAUSE);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb), data);
   
  stop_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_STOP);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb), data);
  
   jpeg_b=gtk_toggle_button_new_with_label( "jpeg" );
  g_signal_connect (G_OBJECT (jpeg_b), "clicked", G_CALLBACK (jpeg_cb), data);

 mpeg_b=gtk_toggle_button_new_with_label( "mpeg4" );
  g_signal_connect (G_OBJECT (mpeg_b), "clicked", G_CALLBACK (mpeg_cb), data);

    vorbis=gtk_toggle_button_new_with_label( "vorbis" );
     g_signal_connect (G_OBJECT (vorbis), "clicked", G_CALLBACK (vorbis_cb), data);

   alaw=gtk_toggle_button_new_with_label( "alaw" );
  g_signal_connect (G_OBJECT (alaw), "clicked", G_CALLBACK (alaw_cb), data);

	mulaw=gtk_toggle_button_new_with_label( "mulaw" );
g_signal_connect (G_OBJECT (mulaw), "clicked", G_CALLBACK (mulaw_cb), data);

   mkv=gtk_toggle_button_new_with_label( "mkv" );
g_signal_connect (G_OBJECT (mkv), "clicked", G_CALLBACK (mkv_cb), data);

	avi=gtk_toggle_button_new_with_label( "avi" );
g_signal_connect (G_OBJECT (avi), "clicked", G_CALLBACK (avi_cb), data);

confirm=gtk_toggle_button_new_with_label( "confirm" );
g_signal_connect (G_OBJECT (confirm), "clicked", G_CALLBACK (confirm_cb), data);
	//changed
	/* 
  data->slider = gtk_hscale_new_with_range (0, 100, 1);
  gtk_scale_set_draw_value (GTK_SCALE (data->slider), 0);
  data->slider_update_signal_id = g_signal_connect (G_OBJECT (data->slider), "value-changed", G_CALLBACK (slider_cb), data);*/
   data->text=gtk_text_view_new ();
   data->time_text=gtk_text_view_new ();
   data->buffer=gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text));
   gtk_text_buffer_set_text (data->buffer, "Record Time ", -1);
data->buffer2=gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->time_text));
   gtk_text_buffer_set_text (data->buffer2, "00:00", -1);
  data->streams_list = gtk_text_view_new ();
  gtk_text_view_set_editable (GTK_TEXT_VIEW (data->streams_list), FALSE);
   
  controls = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (controls), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), data->text, TRUE, TRUE, 2);
  gtk_box_pack_start (GTK_BOX (controls), data->time_text, TRUE, TRUE, 2);

	control2 = gtk_hbox_new (FALSE, 0);
   gtk_box_pack_start (GTK_BOX (control2), jpeg_b, TRUE, TRUE, 2);
gtk_box_pack_start (GTK_BOX (control2), mpeg_b, TRUE, TRUE, 2);

control3 = gtk_hbox_new (FALSE, 0);
gtk_box_pack_start (GTK_BOX (control3), vorbis, TRUE, TRUE, 2);
gtk_box_pack_start (GTK_BOX (control3), alaw, TRUE, TRUE, 2);
gtk_box_pack_start (GTK_BOX (control3), mulaw, TRUE, TRUE, 2);

control4 = gtk_hbox_new (FALSE, 0);
gtk_box_pack_start (GTK_BOX (control4), mkv, TRUE, TRUE, 2);
gtk_box_pack_start (GTK_BOX (control4), avi, TRUE, TRUE, 2);

control5 = gtk_hbox_new (FALSE, 0);
gtk_box_pack_start (GTK_BOX (control5), confirm, TRUE, TRUE, 2);

  main_hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), video_window, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), data->streams_list, FALSE, FALSE, 2);
   
  main_box = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), main_hbox, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), controls, FALSE, FALSE, 0);
gtk_box_pack_start (GTK_BOX (main_box), control2, FALSE, FALSE, 0);
gtk_box_pack_start (GTK_BOX (main_box), control3, FALSE, FALSE, 0);
gtk_box_pack_start (GTK_BOX (main_box), control4, FALSE, FALSE, 0);
gtk_box_pack_start (GTK_BOX (main_box), control5, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (main_window), main_box);
  gtk_window_set_default_size (GTK_WINDOW (main_window), 640, 480);
   printf("reach p 99\n");
  gtk_widget_show_all (main_window);
	printf("reach final\n");
}
int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;
  char* FILE_LOCATION=NULL;
/*
  if(argc!=5){
      usage(argc, argv);
      exit(1);
  }
*/
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) != NULL)
    fprintf(stdout, "Current working dir: %s\n", cwd);
  else
  {
    perror("getcwd_error");
    return -1;
  }
 
  gtk_init (&argc, &argv);

  create_file_ui();

  if(!filename) 
  {
    g_print("file name invalid!\n");
    return -1;
  }

  gst_init(&argc, &argv);



  data.duration = GST_CLOCK_TIME_NONE;
  /* Create pipeline and attach a callback to it's
   * message bus */
  data.pipeline = gst_pipeline_new("test-camera");
  
  /* Create elements */
  /* Camera video stream comes from a Video4Linux driver */
  data.camera_video_src = gst_element_factory_make("v4l2src", "camera_video_src");
  /*audio source*/
  data.camera_audio_src = gst_element_factory_make("alsasrc", "camera_audio_src");
  /* Colorspace filter is needed to make sure that sinks understands
   * the stream coming from the camera */
  data.video_filter = gst_element_factory_make("ffmpegcolorspace", "video_filter");

  data.audio_filter = gst_element_factory_make("audioconvert","audio_filter");
  /* Tee that copies the stream to multiple outputs */
  // tee = gst_element_factory_make("tee", "tee");
  data.video_tee = gst_element_factory_make ("tee", "video_tee");

  /* Queue creates new thread for the stream */
  data.screen_queue_play = gst_element_factory_make("queue", "screen_queue_play");

  /*queue for storing video data to file*/
   data.video_queue_store=gst_element_factory_make("queue", "video_queue_store");

  data.audio_queue=gst_element_factory_make("queue", "audio_queue");

  /* Sink that shows the image on screen. Xephyr doesn't support XVideo
   * extension, so it needs to use ximagesink, but the device uses
   * xvimagesink */
  data.video_sink = gst_element_factory_make("xvimagesink", "screen_sink");

  create_ui (&data);

  bus = gst_element_get_bus (data.pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, &data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, &data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, &data);
  g_signal_connect (G_OBJECT (bus), "message::application", (GCallback)application_cb, &data);
  gst_object_unref (bus);
  printf("ready=%d",ready);
 // while(!ready){};
  gtk_main ();

if(video_format==mpeg4)
  { 
    printf("encoding video with mpeg4 format ...\n");
   data.video_encoder = gst_element_factory_make("ffenc_mpeg4", "video_encoder");
  }
  else if(video_format==jpeg){
    printf("encoding video with jpeg format...\n");
    data.video_encoder = gst_element_factory_make("jpegenc", "video_encoder");

  }
  else
  { printf("video format error\n");
   return -1;
	}

  if(audio_format==vorbis)
  {
    data.audio_encoder=gst_element_factory_make("vorbisenc", "audio_encoder");
    printf("encoding audio with vorbis format ... \n");    
  }
  else if(audio_format==alaw){
    data.audio_encoder=gst_element_factory_make("alawenc", "audio_encoder");
    printf("encoding audio with alaw format\n");    
  }
  else if(audio_format==mulaw){
    printf("encoding audio with mulaw format ...\n");
    data.audio_encoder=gst_element_factory_make("mulawenc", "audio_encoder");    
  }
  else
  { printf("audio format error\n");
   return -1;
  }

  if(muxer_format ==mkv  )
  {
    printf("compressing to .mkv format ..\n");
    data.muxer = gst_element_factory_make("matroskamux", "muxer");
    asprintf(&FILE_LOCATION, "%s/%s.mkv", cwd, filename);
  }
  else{
    printf("compressing to .avi format ...\n");
    data.muxer = gst_element_factory_make("avimux", "muxer");
    asprintf(&FILE_LOCATION, "%s/%s.avi", cwd, filename);
  }
  printf("file location: %s\n", FILE_LOCATION);
  data.video_file_sink= gst_element_factory_make("filesink", "recorded_video");

  /* Check that elements are correctly initialized */
  if(!(data.pipeline && data.camera_video_src && data.video_sink && data.video_filter && data.screen_queue_play
    && data.camera_audio_src && /*data.audio_sink &&*/ data.audio_filter && data.audio_queue && data.audio_encoder
    && data.video_tee && data.video_queue_store && data.video_encoder && data.muxer && data.video_file_sink ))
  {
    g_critical("Couldn't create pipeline elements");
    return FALSE;
  }

  g_object_set (data.video_file_sink, "location", FILE_LOCATION, NULL);


  /* Set image sink to emit handoff-signal before throwing away
   * it's buffer */
   //g_object_set(G_OBJECT(data.video_sink), "signal-handoffs", TRUE, NULL);
  
  /* Add elements to the pipeline. This has to be done prior to
   * linking them */
  gst_bin_add_many(GST_BIN(data.pipeline), data.camera_video_src, data.video_filter,data.video_tee ,data.screen_queue_play,
  data.video_sink, data.video_queue_store ,data.camera_audio_src, /*data.audio_sink ,*/ data.audio_encoder,data.audio_filter , data.audio_queue,
  data.video_encoder, data.muxer, data.video_file_sink ,NULL);
  
  /* Specify what kind of video is wanted from the camera */
  data.caps = gst_caps_new_simple("video/x-raw-rgb",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 480,
      NULL);

  data.audio_caps = gst_caps_new_simple("audio/x-raw-int",
      "rate", G_TYPE_INT, 44100,
      "channels", G_TYPE_INT, 2,
      "depths", G_TYPE_INT, 16,
      NULL);
 

  /* Link the camera source and colorspace filter using capabilities
   * specified */
  
  if(!gst_element_link_filtered(data.camera_video_src, data.video_filter, data.caps))
  {
      gst_caps_unref(data.caps);
    return FALSE;
  }
  

  // if(!gst_element_link_many(data.camera_video_src,data.video_filter,NULL))
  // {
  //   gst_object_unref(data.pipeline);
  //   return FALSE;
  // }

  if(!gst_element_link_filtered(data.camera_audio_src, data.audio_filter, data.audio_caps))
  {
    gst_caps_unref(data.audio_caps);
    return FALSE;
  }

  /* Connect Colorspace Filter -> Tee */
  if(!gst_element_link_many(data.video_filter, data.video_tee, NULL))
  {
    g_printerr("filter and tee cannot be linked!.\n");
    gst_object_unref (data.pipeline);
    return FALSE;
  }

  if(!gst_element_link_many(data.audio_filter, data.audio_queue, data.audio_encoder,/*data.audio_sink,*/ NULL))
  {
    g_printerr("audio filter, audio_queue, and audio_encoder cannot be linked!\n");
    gst_object_unref (data.pipeline);
    return FALSE;
  }


   if (gst_element_link_many(data.video_queue_store, data.video_encoder, NULL) != TRUE ||
    gst_element_link_many(data.muxer, data.video_file_sink, NULL) != TRUE ||
    gst_element_link_many(data.screen_queue_play, data.video_sink, NULL) != TRUE
    )
   {
    g_printerr("muxer cannot be linked with file or screen queue cannot be linked with video_sink.\n");
    gst_object_unref(data.pipeline);
    return -1;
   }


  /* Manually link the Tee, which has "Request" pads */
  data.tee_src_pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(data.video_tee), "src%d");
  //link tee src0---queue1 sink
  data.tee_video_play_pad = gst_element_request_pad(data.video_tee, data.tee_src_pad_template, NULL, NULL);
  g_print("Obtained request pad (%s) for tee video branch.\n", gst_pad_get_name(data.tee_video_play_pad));
  data.video_queue_play_pad = gst_element_get_static_pad(data.screen_queue_play, "sink");
  g_print("Obtained request video_queue_play_pad (%s) for tee video branch.\n", gst_pad_get_name(data.video_queue_play_pad));

  //link tee src1---queue1 sink
  data.tee_video_encode_pad = gst_element_request_pad(data.video_tee, data.tee_src_pad_template, NULL, NULL);
  g_print("2 Obtained request pad (%s) for tee video branch.\n", gst_pad_get_name(data.tee_video_encode_pad));
  data.video_queue_encode_pad = gst_element_get_static_pad(data.video_queue_store, "sink");
  g_print("2 Obtained request video_queue_encode_pad (%s) for tee video branch.\n", gst_pad_get_name(data.video_queue_encode_pad));


  if (
    gst_pad_link(data.tee_video_encode_pad, data.video_queue_encode_pad) != GST_PAD_LINK_OK
    ) {
    g_printerr("Tee1 could not be linkedUUUUUU.\n");
    gst_object_unref(data.pipeline);
    return -1;
  }

  if (gst_pad_link(data.tee_video_play_pad, data.video_queue_play_pad) != GST_PAD_LINK_OK
    ) {
    g_printerr("Tee1 could not be linkedooooooo.\n");
    gst_object_unref(data.pipeline);
    return -1;
  }  
  gst_object_unref(data.video_queue_play_pad);
  gst_object_unref(data.video_queue_encode_pad);

  /* Manually link the video_encoder and mux element, which has "Request" pads */
  data.muxer_src_pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(data.muxer), "video_%d");
  //link video_1_encoder src----muxer  sink
  data.muxer_video_1_pad = gst_element_request_pad(data.muxer, data.muxer_src_pad_template, NULL, NULL);
  
  g_print("3 Obtained request pad (%s) for muxer_video_1_pad branch.\n", gst_pad_get_name(data.muxer_video_1_pad));
  data.encoder_video_1_pad = gst_element_get_static_pad(data.video_encoder, "src");
  g_print("3 Obtained request video_queue_1_pad (%s) for tee video branch.\n", gst_pad_get_name(data.encoder_video_1_pad));

  //printf("=======[%d]===========", gst_pad_link(data.encoder_video_1_pad, data.muxer_video_1_pad));
  if (gst_pad_link(data.encoder_video_1_pad, data.muxer_video_1_pad) != GST_PAD_LINK_OK
    ) 
  {
    g_printerr("Tee1 could not be linkedVVVVVVV.\n");
    gst_object_unref(data.pipeline);
    return -1;
  }

  //link audio_encoder src----muxer  sink
  data.muxer_src_audioPad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(data.muxer),"audio_%d");
  data.muxer_audio_pad = gst_element_request_pad(data.muxer, data.muxer_src_audioPad_template, NULL, NULL);
  g_print("4 Obtained request audio pad (%s) for muxer .\n", gst_pad_get_name(data.muxer_audio_pad));
  data.encoder_audio_pad = gst_element_get_static_pad(data.audio_encoder, "src");
  g_print("4 Obtained request  audio pad (%s) for encoder \n", gst_pad_get_name(data.encoder_audio_pad));


//printf("==========wrong NUM========[%d]=================\n",gst_pad_link(data.encoder_audio_pad, data.muxer_audio_pad));
//return -1;
  if (gst_pad_link(data.encoder_audio_pad, data.muxer_audio_pad) != GST_PAD_LINK_OK
    ) 
  {
    g_printerr("encoder audio pad and muxer audio pad canot be linked!.\n");
    gst_object_unref(data.pipeline);
    return -1;
  }

  gst_object_unref(data.encoder_video_1_pad);
  gst_object_unref(data.encoder_audio_pad);

  printf("\n Pipeline linked ! \n");

  g_object_set (data.camera_video_src, "device", "/dev/video0", NULL);
  printf("creating file %s \n", FILE_LOCATION);
  //gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

  gtk_main ();
  gst_element_release_request_pad(data.video_tee, data.tee_video_play_pad);
  gst_element_release_request_pad(data.video_tee, data.tee_video_encode_pad);
  gst_element_release_request_pad(data.muxer, data.muxer_video_1_pad);
  gst_element_release_request_pad(data.muxer, data.muxer_audio_pad);
  gst_object_unref(data.tee_video_play_pad);
  gst_object_unref(data.tee_video_encode_pad);
  gst_object_unref(data.muxer_video_1_pad);
  gst_object_unref(data.muxer_audio_pad);

  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);

  if(filename) free(filename);
  if(FILE_LOCATION) free(FILE_LOCATION);
  return 0;
}
