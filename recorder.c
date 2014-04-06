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

} CustomData;

static void usage(int argc, char **argv)
{
         printf( "Usage: %s [video_format] [audio_format] [muxer_format] [filename]\n\n"
                 "[video_format] [audio_format] [muxer_format]\n"
                 "m | mpeg4       v | vorbis     m | mkv  \n"
                 "j | jpeg        a | alaw       a | avi  \n"
                 "                m | mulaw               \n", argv[0]);
}

/* Handler for the pad-added signal */
// static void pad_added_handler (GstElement *src, GstPad *pad, CustomData *data);

int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;
  char* FILE_LOCATION=NULL;
  char* filename=NULL;

  if(argc!=5){
      usage(argc, argv);
      exit(1);
  }
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) != NULL)
    fprintf(stdout, "Current working dir: %s\n", cwd);
  else
  {
    perror("getcwd_error");
    return -1;
  }
  if(strcmp(argv[3],"m")==0) asprintf(&filename, "%s%s", argv[4], ".mkv" );
  else asprintf(&filename, "%s%s", argv[4], ".avi" );

  if(asprintf(&FILE_LOCATION, "%s/%s", cwd, filename )<0)
  {
    perror("cannot find file directory\n");
    return -1;
  }
   
  gst_init(&argc, &argv);
  
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

  /* Creates separate thread for the stream from which the image
   * is captured */

  if(strcmp(argv[1],"m")==0)
  { 
    printf("encoding video with mpeg4 format ...\n");
   data.video_encoder = gst_element_factory_make("ffenc_mpeg4", "video_encoder");
  }
  else{
    printf("encoding video with jpeg format...\n");
    data.video_encoder = gst_element_factory_make("jpegenc", "video_encoder");

  }

  if(strcmp(argv[2], "v")==0)
  {
    data.audio_encoder=gst_element_factory_make("vorbisenc", "audio_encoder");
    printf("encoding audio with vorbis format ... \n");    
  }
  else if(strcmp(argv[2],"a")==0){
    data.audio_encoder=gst_element_factory_make("alawenc", "audio_encoder");
    printf("encoding audio with alaw format\n");    
  }
  else{
    printf("encoding audio with mulaw format ...\n");
    data.audio_encoder=gst_element_factory_make("mulawenc", "audio_encoder");    
  }

  if(strcmp(argv[3], "m")==0  )
  {
    printf("compressing to .mkv format ..\n");
    data.muxer = gst_element_factory_make("matroskamux", "muxer");
  }
  else{
    printf("compressing to .avi format ...\n");
    data.muxer = gst_element_factory_make("avimux", "muxer");
  }

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
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 480,
      NULL);

  data.audio_caps = gst_caps_new_simple("audio/x-raw-int",
      "rate", G_TYPE_INT, 8000,
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

  // printf("===!!!!!!!!!!!!=======tee_video_2_pad============\n");
  // print_pad_capabilities(data.tee, gst_pad_get_name(data.tee_video_encode_pad));
  // printf("===!!!!!!!!!!!!=======video_queue_2_pad============\n");
  // print_pad_capabilities(data.video_queue_store, gst_pad_get_name(data.video_queue_encode_pad));
  //   printf("===!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
  //       printf("===!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
  //       return -1;
  //printf("=====RERER=======[%d]==============\n",gst_pad_link(data.tee_video_1_pad, data.video_queue_1_pad));

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
  printf("creating file %s \n", filename);
  gst_element_set_state(data.pipeline, GST_STATE_PLAYING);


  /* Listen to the bus */
  bus = gst_element_get_bus (data.pipeline);
  do {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  // printf("does it call here?\n");
    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;
      
      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
          if(!err)
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
