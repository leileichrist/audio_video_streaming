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

gchar* video_codec=NULL;
gchar* audio_codec=NULL;
gchar* container_format=NULL;


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
  GstDiscoverer *discoverer;
} CustomData;

static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data);
static void print_tag_foreach (const GstTagList *tags, const gchar *tag, gpointer user_data);
static void find_tag_foreach (const GstTagList *tags, const gchar *tag, gpointer user_data);

int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  GstDiscovererInfo* fileInfo;
  const GstTagList* tagList;
  GError *err = NULL;
  gboolean terminate = FALSE;
  char* FILE_LOCATION=NULL;
  char* uri=NULL;
  char cwd[1024];
  gchar* audio_type=NULL;
  gchar* video_type=NULL;
 

  if(argc!=2){
    printf("usage: %s filename\n", argv[0] );
      exit(1);
  }
  if (getcwd(cwd, sizeof(cwd)) != NULL)
    fprintf(stdout, "Current working dir: %s\n", cwd);
  else
  {
    perror("getcwd_error");
    return -1;
  }
  if(asprintf(&FILE_LOCATION, "%s/%s", cwd, argv[1])<0)
  {
    perror("cannot find file directory\n");
    return -1;
  }
  g_print("%s\n", FILE_LOCATION);
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
  if(g_str_has_suffix(argv[1],"mkv"))
  {
    data.demuxer = gst_element_factory_make("matroskademux", "demuxer");
    printf("file is .mkv type\n");    
  }
  else if(g_str_has_suffix(argv[1], "avi"))
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

  if(strstr (video_codec,"MPEG"))
  {
    printf("MPEG video\n");
    data.video_decoder = gst_element_factory_make("ffdec_mpeg4", "video_decoder");

  }
  else if(strstr (video_codec,"JPEG") ){
    printf("JPEG video\n");
    data.video_decoder = gst_element_factory_make("jpegdec", "video_decoder");

  }
  else{
    g_print("unsupported video type!\n");
    return -1;
  }

  if( g_str_has_prefix (audio_codec, "Vorbis" ) ){
    g_print("Vorbis audio\n");
    data.audio_decoder = gst_element_factory_make("vorbisdec", "audio_decoder");    
  }
  else if( g_str_has_prefix (audio_codec, "Mu" ) ){
    g_print("Mulaw audio\n");
    data.audio_decoder = gst_element_factory_make("mulawdec", "audio_decoder");    
  }
  else if( g_str_has_prefix (audio_codec, "A" ) ){
    printf("a-law audio\n");
    data.audio_decoder = gst_element_factory_make("alawdec", "audio_decoder");    
  }
  else{
       g_print("unsupported audio type!\n");
       return -1; 
  }


  data.audio_queue = gst_element_factory_make("queue", "audio_queue");
  data.video_queue = gst_element_factory_make("queue", "video_queue");
  data.video_sink = gst_element_factory_make("xvimagesink", "video_sink");
  data.audio_sink = gst_element_factory_make("autoaudiosink","audio_sink");


  /* Check that elements are correctly initialized */
  if(!(data.pipeline && data.filesrc && data.demuxer && data.video_decoder && data.audio_decoder &&
       data.audio_queue && data.video_queue && data.video_sink && data.audio_sink))
  {
    g_critical("Couldn't create pipeline elements");
    return FALSE;
  }

  g_object_set (data.filesrc, "location", FILE_LOCATION, NULL);

  /* Add elements to the pipeline. This has to be done prior to
   * linking them */
  gst_bin_add_many(GST_BIN(data.pipeline),data.filesrc, data.demuxer, data.video_decoder, data.audio_decoder,
       data.audio_queue, data.video_queue, data.video_sink, data.audio_sink, NULL);


  if(!gst_element_link_many(data.filesrc, data.demuxer ,NULL))
  {
    gst_object_unref(data.pipeline);
    return FALSE;
  }

  if(!gst_element_link_many(data.video_queue , data.video_decoder, data.video_sink, NULL))
  {
    g_printerr("video_decoder, queue, and sink cannot be linked!.\n");
    gst_object_unref (data.pipeline);
    return FALSE;
  }
  if(!gst_element_link_many( data.audio_queue, data.audio_decoder, data.audio_sink, NULL))
  {
    g_printerr("audio_decoder, queue, and sink cannot be linked!.\n");
    gst_object_unref (data.pipeline);
    return FALSE;
  }

  g_signal_connect (data.demuxer, "pad-added", G_CALLBACK (pad_added_handler), &data);

  ret=gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Listen to the bus */
  bus = gst_element_get_bus (data.pipeline);
  do {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;
      
      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          terminate = TRUE;
          break;
        case GST_MESSAGE_EOS:
          g_print ("End-Of-Stream reached.\n");
          terminate = TRUE;
          break;
        case GST_MESSAGE_STATE_CHANGED:
          /* We are only interested in state-changed messages from the pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
            g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
          }
          break;
        default:
          /* We should not reach here */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);

  /* Free resources */
  gst_object_unref (bus);
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

  g_print ("  It has type '%s' \n", new_pad_type);

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
   
/* Print information regarding a stream */
static void print_stream_info (GstDiscovererStreamInfo *info, gint depth) {
  gchar *desc = NULL;
  GstCaps *caps;
  const GstTagList *tags;
   
  caps = gst_discoverer_stream_info_get_caps (info);
   
  if (caps) {
    if (gst_caps_is_fixed (caps))
      desc = gst_pb_utils_get_codec_description (caps);
    else
      desc = gst_caps_to_string (caps);
    gst_caps_unref (caps);
  }
   
  g_print ("%*s%s: %s\n", 2 * depth, " ", gst_discoverer_stream_info_get_stream_type_nick (info), (desc ? desc : ""));
   
  if (desc) {
    g_free (desc);
    desc = NULL;
  }
   
  tags = gst_discoverer_stream_info_get_tags (info);
  if (tags) {
    g_print ("%*sTags:\n", 2 * (depth + 1), " ");
    gst_tag_list_foreach (tags, print_tag_foreach, GINT_TO_POINTER (depth + 2));
  }
}
   
/* Print information regarding a stream and its substreams, if any */
static void print_topology (GstDiscovererStreamInfo *info, gint depth) {
  GstDiscovererStreamInfo *next;
   
  if (!info)
    return;
   
  print_stream_info (info, depth);
   
  next = gst_discoverer_stream_info_get_next (info);
  if (next) {
    print_topology (next, depth + 1);
    gst_discoverer_stream_info_unref (next);
  } else if (GST_IS_DISCOVERER_CONTAINER_INFO (info)) {
    GList *tmp, *streams;
     
    streams = gst_discoverer_container_info_get_streams (GST_DISCOVERER_CONTAINER_INFO (info));
    for (tmp = streams; tmp; tmp = tmp->next) {
      GstDiscovererStreamInfo *tmpinf = (GstDiscovererStreamInfo *) tmp->data;
      print_topology (tmpinf, depth + 1);
    }
    gst_discoverer_stream_info_list_free (streams);
  }
}