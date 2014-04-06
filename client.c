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

#define serverIP "localhost"
#define clientIP "localhost"
#define port_recv_rtp_sink0 5000
#define port_recv_rtp_sink1 5002
#define port_recv_rtcp_sink0 5001
#define port_recv_rtcp_sink1 5003
#define port_send_rtcp_src0 5005
#define port_send_rtcp_src1 5007
#define VIDEO_CAPS "application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)JPEG"
#define AUDIO_CAPS "application/x-rtp,media=(string)audio,clock-rate=(int)8000,encoding-name=(string)VORBIS"


typedef struct _CustomData {
  GstElement *clientPipeline;
  GstElement *demuxer;
  GstElement *audio_filter;
  GstElement *audio_queue_1, *video_queue_1;
  GstElement *video_decoder, *audio_decoder;
  GstElement *video_sink, *audio_sink;

  GstElement *videoDepayloader, *audioDepayloader;
  GstElement *audio_queue_2, *video_queue_2;

  GstElement *clientRTPBIN;
  GstElement *udpsrc_rtp0, *udpsrc_rtp1, *udpsrc_rtcp0, *udpsrc_rtcp1 ;
  GstElement *udpsink_rtcp0, *udpsink_rtcp1 ;

  GstCaps *video_caps;
  GstCaps *audio_caps;

} CustomData;


static void rtpbin_pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data);

int main(int argc, char *argv[]) {

  CustomData data;

  /*Pads for requesting*/
  GstPadTemplate *recv_rtp_sink_temp, *recv_rtcp_sink_temp, *send_rtcp_src_temp;
  GstPad *recv_rtp_sink0, *recv_rtp_sink1;
  GstPad *recv_rtcp_sink0, *recv_rtcp_sink1; 
  GstPad *send_rtcp_src0, *send_rtcp_src1;

  /*static pads*/
  GstPad *udpsrc_rtp0_src, *udpsrc_rtp1_src, *udpsrc_rtcp0_src, *udpsrc_rtcp1_src;
  GstPad *udpsink_rtcp0_sink, *udpsink_rtcp1_sink ;
  GstCaps *videocaps; 
  GstCaps *audiocaps; 

  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;

  gboolean terminate = FALSE;
 

  memset (&data, 0, sizeof (data));
  gst_init(&argc, &argv);
  
  /* Create pipeline and attach a callback to it's
   * message bus */
  data.clientPipeline = gst_pipeline_new("client");
  
  /* Create elements */
  

  data.video_decoder = gst_element_factory_make("jpegdec", "video_decoder");
  data.audio_decoder = gst_element_factory_make("vorbisdec", "audio_decoder");    

  data.audio_queue_1 = gst_element_factory_make("queue", "audio_queue_1");
  data.video_queue_1 = gst_element_factory_make("queue", "video_queue_1");

  data.audio_queue_2 = gst_element_factory_make("queue", "audio_queue_2");
  data.video_queue_2 = gst_element_factory_make("queue", "video_queue_2");

  data.videoDepayloader = gst_element_factory_make("rtpjpegdepay","videoDepayloader");
  data.audioDepayloader = gst_element_factory_make("rtpvorbisdepay","audioDepayloader");

  data.video_sink = gst_element_factory_make("xvimagesink","video_sink");
  data.audio_sink = gst_element_factory_make("autoaudiosink","audio_sink");

  data.clientRTPBIN = gst_element_factory_make("gstrtpbin","clientRTPBIN");

  data.udpsrc_rtp0 = gst_element_factory_make("udpsrc", "udpsrc_rtp0");
  data.udpsrc_rtp1 = gst_element_factory_make("udpsrc", "udpsrc_rtp1");

  data.udpsrc_rtcp0 = gst_element_factory_make("udpsrc", "udpsrc_rtcp0");
  data.udpsrc_rtcp1 = gst_element_factory_make("udpsrc", "udpsrc_rtcp1");
  
  data.udpsink_rtcp0 = gst_element_factory_make("udpsink", "udpsink_rtcp0");
  data.udpsink_rtcp1 = gst_element_factory_make("udpsink", "udpsink_rtcp1");


  /* Check that elements are correctly initialized */
  if(!(data.clientPipeline && data.audio_decoder &&  data.audio_queue_1 && data.video_decoder && data.video_queue_1 && 
       data.audio_queue_2 && data.video_queue_2 && data.videoDepayloader && data.audioDepayloader &&
       data.clientRTPBIN && data.udpsrc_rtp0 && data.udpsrc_rtp1 && data.udpsink_rtcp0 && data.udpsink_rtcp1 &&
       data.udpsrc_rtcp0 && data.udpsrc_rtcp1 && data.video_sink && data.audio_sink ))
  {
    g_critical("Couldn't create pipeline elements");
    return FALSE;
  }

  videocaps = gst_caps_from_string(VIDEO_CAPS);
  audiocaps = gst_caps_from_string(AUDIO_CAPS);


  g_object_set(data.udpsrc_rtp0, "caps", videocaps, "port", port_recv_rtp_sink0,   NULL);
  g_object_set(data.udpsrc_rtp1,  "caps", audiocaps, "port", port_recv_rtp_sink1,  NULL);

  g_object_set(data.udpsrc_rtcp0, "port", port_recv_rtcp_sink0 ,NULL);
  g_object_set(data.udpsrc_rtcp1, "port", port_recv_rtcp_sink1 ,NULL);

  g_object_set(data.udpsink_rtcp0, /*"host", serverIP, */"port", port_send_rtcp_src0, "async", FALSE, "sync", FALSE, NULL);
  g_object_set(data.udpsink_rtcp1, /*"host", serverIP,*/ "port", port_send_rtcp_src1, "async", FALSE, "sync", FALSE,  NULL);


    data.video_caps = gst_caps_new_simple("image/jpeg",
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 480,
      NULL);

     data.audio_caps = gst_caps_new_simple("audio/x-raw-float",
      "rate", G_TYPE_INT, 8000,
      "channels", G_TYPE_INT, 2,
      "depths", G_TYPE_INT, 16,
      NULL);
 

  /* Add elements to the pipeline. This has to be done prior to
   * linking them */
  gst_bin_add_many(GST_BIN(data.clientPipeline), data.audio_decoder, data.audio_queue_1, data.video_decoder ,data.video_queue_1, 
       data.videoDepayloader, data.audioDepayloader, data.clientRTPBIN,
       data.udpsrc_rtp0, data.udpsrc_rtp1, data.udpsink_rtcp0, data.udpsink_rtcp1,
       data.udpsrc_rtcp0, data.udpsrc_rtcp1, data.video_sink, data.audio_sink, NULL);

  if(!gst_element_link_many(data.video_queue_1, data.videoDepayloader, data.video_decoder, data.video_sink, NULL))
  {
    g_printerr("videoqueue, depayloader, decoder, and sink cannot be linked!\n");
    gst_object_unref (data.clientPipeline);
    exit(1);
  }
  if(!gst_element_link_many(data.audio_queue_1, data.audioDepayloader, data.audio_decoder, data.audio_sink, NULL))
  {
    g_printerr("audio queue and audio decoder cannot be linked!.\n");
    gst_object_unref (data.clientPipeline);
    exit(1);
  }


  // /*Pads for requesting*/
  // GstPadTemplate *recv_rtp_sink_temp, *recv_rtcp_sink_temp, *send_rtcp_src_temp;
  // GstPad *recv_rtp_sink0, *recv_rtp_sink1;
  // GstPad *recv_rtcp_sink0, *recv_rtcp_sink1; 
  // GstPad *send_rtcp_src0, *send_rtcp_src1;

  // /*static pads*/
  // GstPad *video_queue_sinkPad, *audio_queue_sinkPad;
  // GstPad *udpsrc_rtp0_src, *udpsrc_rtp1_src, *udpsrc_rtcp0_src, *udpsrc_rtcp1_src;
  // GstPad *udpsink_rtcp0_sink, *udpsink_rtcp1_sink ;


  recv_rtp_sink_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data.clientRTPBIN), "recv_rtp_sink_%d");
  recv_rtp_sink0 = gst_element_request_pad (data.clientRTPBIN, recv_rtp_sink_temp, NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtp_sink !.\n", gst_pad_get_name (recv_rtp_sink0));
  udpsrc_rtp0_src = gst_element_get_static_pad(data.udpsrc_rtp0, "src");
  recv_rtp_sink1 = gst_element_request_pad (data.clientRTPBIN, recv_rtp_sink_temp, NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtp_sink !.\n", gst_pad_get_name (recv_rtp_sink1));
  udpsrc_rtp1_src = gst_element_get_static_pad(data.udpsrc_rtp1, "src");


  recv_rtcp_sink_temp = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(data.clientRTPBIN), "recv_rtcp_sink_%d");
  recv_rtcp_sink0 = gst_element_request_pad(data.clientRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink0));
  udpsrc_rtcp0_src = gst_element_get_static_pad(data.udpsrc_rtcp0, "src");
  recv_rtcp_sink1 = gst_element_request_pad(data.clientRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink1));
  udpsrc_rtcp1_src = gst_element_get_static_pad(data.udpsrc_rtcp1, "src");


  send_rtcp_src_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data.clientRTPBIN), "send_rtcp_src_%d");
  send_rtcp_src0 = gst_element_request_pad(data.clientRTPBIN, send_rtcp_src_temp, NULL, NULL);
  g_print ("Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src0));
  udpsink_rtcp0_sink = gst_element_get_static_pad(data.udpsink_rtcp0, "sink");
  send_rtcp_src1 = gst_element_request_pad(data.clientRTPBIN, send_rtcp_src_temp, NULL, NULL);
  g_print ("Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src1));
  udpsink_rtcp1_sink = gst_element_get_static_pad(data.udpsink_rtcp1, "sink");


  if (gst_pad_link (udpsrc_rtp0_src, recv_rtp_sink0 )!= GST_PAD_LINK_OK ||
      gst_pad_link (udpsrc_rtp1_src, recv_rtp_sink1 )!= GST_PAD_LINK_OK ||
      gst_pad_link (udpsrc_rtcp0_src, recv_rtcp_sink0 )!= GST_PAD_LINK_OK ||
      gst_pad_link (udpsrc_rtcp1_src, recv_rtcp_sink1 )!= GST_PAD_LINK_OK ||
      gst_pad_link (send_rtcp_src0, udpsink_rtcp0_sink )!= GST_PAD_LINK_OK ||
      gst_pad_link (send_rtcp_src1, udpsink_rtcp1_sink )!= GST_PAD_LINK_OK  )
  {
    g_printerr ("Some requested pads cannot be linked with static pads!\n");
    gst_object_unref (data.clientPipeline);
    exit(1);
  }

  gst_object_unref (udpsrc_rtp0_src);
  gst_object_unref (udpsrc_rtp1_src);
  gst_object_unref (udpsrc_rtcp0_src);
  gst_object_unref (udpsrc_rtcp1_src);
  gst_object_unref (udpsink_rtcp0_sink);
  gst_object_unref (udpsink_rtcp1_sink);
 
  g_signal_connect (data.clientRTPBIN, "pad-added", G_CALLBACK (rtpbin_pad_added_handler), &data);

  ret=gst_element_set_state(data.clientPipeline, GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.clientPipeline);
    return -1;
  }

  /* Listen to the bus */
  bus = gst_element_get_bus (data.clientPipeline);
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
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.clientPipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
            g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
          }
          break;
        case GST_MESSAGE_CLOCK_LOST:
          g_print ("Clock is lost\n") ;
          gst_element_set_state (data.clientPipeline, GST_STATE_PAUSED);
          gst_element_set_state (data.clientPipeline, GST_STATE_PLAYING);
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
  gst_element_release_request_pad (data.clientRTPBIN, recv_rtp_sink0);
  gst_element_release_request_pad (data.clientRTPBIN, recv_rtp_sink1);
  gst_element_release_request_pad (data.clientRTPBIN, recv_rtcp_sink0);
  gst_element_release_request_pad (data.clientRTPBIN, recv_rtcp_sink1);
  gst_element_release_request_pad (data.clientRTPBIN, send_rtcp_src0);
  gst_element_release_request_pad (data.clientRTPBIN, send_rtcp_src1);
  gst_object_unref (recv_rtp_sink0);
  gst_object_unref (recv_rtp_sink1);
  gst_object_unref (send_rtcp_src0);
  gst_object_unref (send_rtcp_src1);
  gst_object_unref (recv_rtcp_sink0);
  gst_object_unref (recv_rtcp_sink1);

  gst_object_unref (bus);
  gst_element_set_state (data.clientPipeline, GST_STATE_NULL);
  gst_object_unref (data.clientPipeline);
  return 0;
}
  
static void rtpbin_pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data)
{
      GstPad *video_queue_sinkPad = gst_element_get_static_pad (data->video_queue_1, "sink");
      GstPad *audio_queue_sinkPad = gst_element_get_static_pad (data->audio_queue_1, "sink");

      GstPadLinkReturn ret;
      GstCaps *new_pad_caps = NULL;
      GstStructure *new_pad_struct = NULL;
      const gchar *new_pad_type = NULL;
       
      g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
      if (gst_pad_is_linked(video_queue_sinkPad) && gst_pad_is_linked(audio_queue_sinkPad)) {
        g_print ("  We are already linked. Ignoring.\n");
        goto exit;
      }

      /* Check the new pad's type */
      new_pad_caps = gst_pad_get_caps (new_pad);
      new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
      new_pad_type = gst_structure_get_name (new_pad_struct);


       if(strstr(GST_PAD_NAME (new_pad), "recv_rtp_src"))
       {
          g_print ("  It has type '%s' \n", new_pad_type);
          if(strstr(GST_PAD_NAME(new_pad), "recv_rtp_src_0"))
          {
               ret = gst_pad_link (new_pad, video_queue_sinkPad);
               if (GST_PAD_LINK_FAILED (ret)) 
               {
                   g_print ("  New pad is '%s' but link failed.\n", GST_PAD_NAME (new_pad));
               } 
               else 
               {
                   g_print ("  Link succeeded (pad '%s'). ", GST_PAD_NAME (new_pad) );
                   GstState curr = GST_STATE(data->clientPipeline) ;
                   g_print("And currrent state is : %s \n",gst_element_state_get_name (curr)) ;
                   if(curr != GST_STATE_PLAYING)
                   {
                      gst_element_set_state (data->clientPipeline, GST_STATE_PAUSED);
                     int x=gst_element_set_state(data->clientPipeline, GST_STATE_PLAYING);
                     if (x == GST_STATE_CHANGE_FAILURE) 
                     {
                      g_printerr ("Inside handler, unable to set the pipeline to the playing state.\n");
                      gst_object_unref (data->clientPipeline);
                      goto exit;
                     }
                     g_print("After setting state to play, state is : %s \n",gst_element_state_get_name (GST_STATE(data->clientPipeline))) ;    
  
                   }            
               }
          }
          else if(strstr(GST_PAD_NAME(new_pad), "recv_rtp_src_1"))
          {
               ret = gst_pad_link (new_pad, audio_queue_sinkPad);
               if (GST_PAD_LINK_FAILED (ret)) 
               {
                   g_print ("  New pad is '%s' but link failed.\n", GST_PAD_NAME (new_pad));
               } 
               else 
               {
                   g_print ("  Link succeeded (pad '%s'). ", GST_PAD_NAME (new_pad));
                   GstState curr = GST_STATE(data->clientPipeline) ;
                   g_print("And currrent state is : %s \n",gst_element_state_get_name (curr)) ;
                   if(curr != GST_STATE_PLAYING)
                   {
                     gst_element_set_state (data->clientPipeline, GST_STATE_PAUSED);
                     int x=gst_element_set_state(data->clientPipeline, GST_STATE_PLAYING);
                     if (x == GST_STATE_CHANGE_FAILURE) 
                     {
                      g_printerr ("Inside handler, unable to set the pipeline to the playing state.\n");
                      gst_object_unref (data->clientPipeline);
                      goto exit;
                     }
                     g_print("After setting state to play, state is : %s \n",gst_element_state_get_name (GST_STATE(data->clientPipeline))) ;    
  
                  }
              }
          }
      }

    exit:
      /* Unreference the new pad's caps, if we got them */
      if (new_pad_caps != NULL)
        gst_caps_unref (new_pad_caps);
       
      /* Unreference the sink pad */
      gst_object_unref (video_queue_sinkPad);
      gst_object_unref (audio_queue_sinkPad);
}
