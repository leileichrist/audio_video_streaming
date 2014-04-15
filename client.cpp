#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fstream>
#include <pthread.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <thread>         // std::this_thread::sleep_for
#include <chrono> 
#include <time.h>       /* time */
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

using namespace std;

#define BACKLOG 10   // how many pending connections queue will hold
#define MAXDATASIZE 1024
#define MAXBUFLEN 100
#define BIND_WIDTH 100
#define ACTIVE_MODE 0
#define PASSIVE_MODE 1

#define HIGH 0
#define LOW 1
#define LEVEL1BR 1000
#define LEVEL2BR 344
#define LEVEL3BW 100

#define CONNECT 0
#define PASSIVE 1
#define ACTIVE 2
#define PLAY 3
#define PAUSE 4
#define STOP 5
#define FORWARD 6
#define REWIND 7
#define defaultServerIP "localhost"
#define defaultServerPort "3490"
#define VIDEO_CAPS "application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)JPEG"
#define AUDIO_CAPS "application/x-rtp, media=(string)audio, clock-rate=(int)8000, encoding-name=(string)AMR, encoding-params=(string)1, octet-align=(string)1, payload=(int)96"

char status_buf[100];
static pthread_mutex_t lock;
static string serverIP =  "localhost";   
static string serverPort = "3490"  ;    // default port users will be connecting to

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

  GstCaps *videocaps; 
  GstCaps *audiocaps; 

  GMainLoop *main_loop;  /* GLib's Main Loop */

  gboolean received_audio;
  gboolean received_video;
  gboolean audio_removed;

  gint mode;
  gint res_mode;
  gboolean connected;
  gboolean mode_selected;

  GtkEntry* ip_text, * port_text, *ba_text;
  GtkWidget *video_window; /* The drawing area where the video will be shown */
  GtkWidget* check_button, *check_button2;
  GtkWidget * text1, *text2, *text3, *status_text;
  GtkTextBuffer *buffer;

  GstState state;
  gint sourceid;
  gint clientport;
  gint port_recv_rtp_sink0 ;
  gint port_recv_rtp_sink1 ;
  gint port_recv_rtcp_sink0 ;
  gint port_recv_rtcp_sink1 ;
  gint port_send_rtcp_src0 ;
  gint port_send_rtcp_src1 ;


} CustomData;

CustomData* curr_data;


void *listening_feedbacks(void *arg);
void *RecvVideo(void *arg);
static void create_ui (CustomData *data) ;
static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) ;
static void print_caps (const GstCaps * caps, const gchar * pfx);
static void print_pad_capabilities (GstElement *element, gchar *pad_name) ;
static void rtpbin_pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data);
static void rtpbin_pad_removed_handler(GstElement *src, GstPad *old_pad, CustomData *data);
static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) ;
static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) ;
static gboolean refresh_ui (CustomData *data) ;
static void stop_cb (GtkButton *button, CustomData *data) ;
static void realize_cb (GtkWidget *widget, CustomData *data) ;
static void print_status(const char* inp);



//set the ABA to the new ABA number
bool changeABA(string FileName, int ABA){
  string inbuf;
  fstream input_file(FileName, ios::in);
  ofstream output_file(FileName);


  string NewABa = to_string(ABA);
  inbuf="ABA:";
  inbuf.append(NewABa);
  output_file << inbuf << endl;
  string endbuf="end";
  output_file << endbuf << endl;
  return true;
}


//calcuate ABA-BA, record new ABA, and return ABA-BA
int admissionProcess(string FileName, int Ba){
  string inbuf;
  fstream input_file(FileName, ios::in);
  ofstream output_file("tempresult.txt");
  int NumABa;
  while (!input_file.eof())
  {
    getline(input_file, inbuf);

    if (inbuf.compare("end")==0){
      output_file << inbuf << endl;
      break;
    }

    if (inbuf.compare(0,4,"ABA:")==0){
      NumABa = stoi(inbuf.substr(4,inbuf.length()-4));
      NumABa-=Ba;
      string NewABa = to_string(NumABa);
      inbuf="ABA:";
      inbuf.append(NewABa);
    }
    output_file << inbuf << endl;
  }
  if( remove( FileName.c_str() ) != 0 )
      perror( "Error deleting file" );

  if ( rename( "tempresult.txt" , FileName.c_str() ) != 0 )
    perror( "Error renaming file" );
  return NumABa;
}


static void modify_cb(GtkButton *button,CustomData * data)
{
  GtkTextBuffer *buffer;
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text1));
  char buf_tmp[100];
  const char* inp_str;
  inp_str= gtk_entry_get_text(data->ba_text);
  int aba=atoi(inp_str);
  changeABA("resource.txt", aba);
  sprintf(buf_tmp,"Current ABA= %d KB/Sec \n",aba);
  gtk_text_buffer_set_text (buffer, buf_tmp, -1);

}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa){
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int sendMsg2Server(const char* desIp, const char* desPort, const char* mesg)
{
    int sockfd;
    char* msg = NULL;
    asprintf(&msg, "%s", mesg);
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(desIp, desPort, &hints, &servinfo)) != 0) {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      return 1;
    }

    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
      if ((sockfd = socket(p->ai_family, p->ai_socktype,
          p->ai_protocol)) == -1) {
        perror("sender: socket");
        continue;
      }
      break;
    }
    if (p == NULL) {
      fprintf(stderr, "sender: failed to bind socket\n");
      return 2;
    }

    char toip[INET_ADDRSTRLEN];
    inet_ntop(p->ai_family, &((struct sockaddr_in *)p->ai_addr)->sin_addr, toip, INET_ADDRSTRLEN);
    sprintf(status_buf,"server ip is %s\n",toip);
    print_status(status_buf);

    if ((numbytes = sendto(sockfd, msg, strlen(msg), 0,
         p->ai_addr, p->ai_addrlen)) == -1) {
      perror("sender: sendto");
      exit(1);
    }
    freeaddrinfo(servinfo);
    close(sockfd);
    return 0;
    
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
   
  g_main_loop_quit (data->main_loop);
}

   
/* This function is called periodically to refresh the GUI */
static gboolean refresh_ui (CustomData *data) {
  GstFormat fmt = GST_FORMAT_TIME;
  gint64 current = -1;
   
  /* We do not want to update anything unless we are in the PAUSED or PLAYING states */
  if (data->state < GST_STATE_PAUSED)
    return TRUE;
  
  return TRUE;
}


void *listening_feedbacks(void *arg)
{
    CustomData* data = (CustomData*)arg;
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[MAXBUFLEN];
    socklen_t addr_len;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, to_string(data->clientport).c_str(), &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return NULL;
    }
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("listener: socket");
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("listener: bind");
            continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n");
        return NULL;
    }
    while(1)
    {
        printf("Client: waiting for feedback...\n");
        addr_len = sizeof their_addr;
        if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
            (struct sockaddr *)&their_addr, &addr_len)) == -1) {
            perror("recvfrom");
            exit(1);
        }
        buf[numbytes] = '\0';
        char* c = buf;
        int type = atoi(buf);
        int status = atoi(++c);

        switch (type)
        {
          case CONNECT:
          {
            if(status==0)
            {
              print_status("After negotiation: Request accepted; connection established!\n"); 
              data->connected=TRUE;
              if(1)
              {
                realize_cb(data->video_window, data);
              }
            }
            else if(status == 1)
            {
              data->connected=TRUE;
              print_status("After negotiation: Request cannot be accepted, negotiate to low level; connection established!\n"); 
            }
            else
            {
              print_status("After negotiation: Request refused , server does not have enough bandwidth!\n");
            }
            break;
          }
          case PLAY:
          {
            if(status==0){print_status("Server side starts playing!\n");}
            else {print_status("Server side streaming failed!\n");}
            break;
          }
          case PAUSE:
          {
            if(status==0)
            {print_status("Server side paused!\n");}
            else {print_status("Server side failed to PAUSE\n");}
            break;
          }
          case STOP:
          {
            if(status==0)
            {
                print_status("Server side stops, session tore down\n");
                gst_element_set_state(data->clientPipeline, GST_STATE_NULL);
                data->connected = FALSE;
                data->mode_selected = FALSE;
            }
            else if(status == 1)
            {
              print_status("End of stream reached!\n");
              data->connected = FALSE;
            }
            else {print_status("Server side failed to stop!\n");}
            break;
          }
          case FORWARD:
          {
            if(status==0){print_status("forwarding!\n");}
            else {printf("forwarding failed\n");}
            break;
          }
          case REWIND:
          {
            if(status==0){print_status("rewinding!\n");}
            else {print_status("rewinding failed\n");}
            break;
          }
          default:
          {
            print_status(buf);
            break;
          }
        }
    }
    close(sockfd);
    return NULL;
}


void *RecvVideo(void *arg)
{
  CustomData* data = (CustomData*)arg;

  /*Pads for requesting*/
  GstPadTemplate *recv_rtp_sink_temp, *recv_rtcp_sink_temp, *send_rtcp_src_temp;
  GstPad *recv_rtp_sink0, *recv_rtp_sink1;
  GstPad *recv_rtcp_sink0, *recv_rtcp_sink1; 
  GstPad *send_rtcp_src0, *send_rtcp_src1;

  /*static pads*/
  GstPad *udpsrc_rtp0_src, *udpsrc_rtp1_src, *udpsrc_rtcp0_src, *udpsrc_rtcp1_src;
  GstPad *udpsink_rtcp0_sink, *udpsink_rtcp1_sink ;
 
  GstBus *bus = gst_bus_new();
 
  data->ip_text = (GtkEntry*)gtk_entry_new ();
  data->port_text = (GtkEntry*)gtk_entry_new ();
  GstMessage *msg;
  GstStateChangeReturn ret;

  data->received_video = FALSE;
  data->received_audio = FALSE;
  data->sourceid = -1;
  /* Create pipeline and attach a callback to it's
   * message bus */
  data->clientPipeline = gst_pipeline_new("client");
  
  /* Create elements */
  

  data->video_decoder = gst_element_factory_make("jpegdec", "video_decoder");
  data->audio_decoder = gst_element_factory_make("amrnbdec", "audio_decoder");    

  data->audio_queue_1 = gst_element_factory_make("queue", "audio_queue_1");
  data->video_queue_1 = gst_element_factory_make("queue", "video_queue_1");

  data->audio_queue_2 = gst_element_factory_make("queue", "audio_queue_2");
  data->video_queue_2 = gst_element_factory_make("queue", "video_queue_2");

  data->videoDepayloader = gst_element_factory_make("rtpjpegdepay","videoDepayloader");
  data->audioDepayloader = gst_element_factory_make("rtpamrdepay","audioDepayloader");

  data->video_sink = gst_element_factory_make("xvimagesink","video_sink");
  data->audio_sink = gst_element_factory_make("autoaudiosink","audio_sink");

  data->clientRTPBIN = gst_element_factory_make("gstrtpbin","clientRTPBIN");

  data->udpsrc_rtp0 = gst_element_factory_make("udpsrc", "udpsrc_rtp0");
  data->udpsrc_rtp1 = gst_element_factory_make("udpsrc", "udpsrc_rtp1");

  data->udpsrc_rtcp0 = gst_element_factory_make("udpsrc", "udpsrc_rtcp0");
  data->udpsrc_rtcp1 = gst_element_factory_make("udpsrc", "udpsrc_rtcp1");
  
  data->udpsink_rtcp0 = gst_element_factory_make("udpsink", "udpsink_rtcp0");
  data->udpsink_rtcp1 = gst_element_factory_make("udpsink", "udpsink_rtcp1");


  /* Check that elements are correctly initialized */
  if(!(data->clientPipeline && data->audio_decoder &&  data->audio_queue_1 && data->video_decoder && data->video_queue_1 && 
       data->audio_queue_2 && data->video_queue_2 && data->videoDepayloader && data->audioDepayloader &&
       data->clientRTPBIN && data->udpsrc_rtp0 && data->udpsrc_rtp1 && data->udpsink_rtcp0 && data->udpsink_rtcp1 &&
       data->udpsrc_rtcp0 && data->udpsrc_rtcp1 && data->video_sink && data->audio_sink ))
  {
    g_critical("Couldn't create pipeline elements");
    return FALSE;
  }

  data->videocaps = gst_caps_from_string(VIDEO_CAPS);
  data->audiocaps = gst_caps_from_string(AUDIO_CAPS);

  // printf("<client>: %s, %d, %d, %d, %d, %d, %d\n", serverIP.c_str(), data->port_recv_rtp_sink0,data->port_recv_rtp_sink1,
  //                                     data->port_recv_rtcp_sink0, data->port_recv_rtcp_sink1, data->port_send_rtcp_src0 , data->port_send_rtcp_src1 );

  // g_object_set(data->udpsrc_rtp0, "caps", videocaps, "port", data->port_recv_rtp_sink0,   NULL);
  // g_object_set(data->udpsrc_rtp1,  "caps", audiocaps, "port", data->port_recv_rtp_sink1,  NULL);

  // g_object_set(data->udpsrc_rtcp0, "port", data->port_recv_rtcp_sink0 ,NULL);
  // g_object_set(data->udpsrc_rtcp1, "port", data->port_recv_rtcp_sink1 ,NULL);

  // g_object_set(data->udpsink_rtcp0, "host", serverIP.c_str(), "port", data->port_send_rtcp_src0, "async", FALSE, "sync", FALSE, NULL);
  // g_object_set(data->udpsink_rtcp1, "host", serverIP.c_str(), "port", data->port_send_rtcp_src1, "async", FALSE, "sync", FALSE,  NULL);



  /* Add elements to the pipeline. This has to be done prior to
   * linking them */
  gst_bin_add_many(GST_BIN(data->clientPipeline), data->audio_decoder, data->audio_queue_1, data->video_decoder ,data->video_queue_1,
       data->videoDepayloader, data->audioDepayloader, data->clientRTPBIN,
       data->udpsrc_rtp0, data->udpsrc_rtp1, data->udpsink_rtcp0, data->udpsink_rtcp1,
       data->udpsrc_rtcp0, data->udpsrc_rtcp1, data->video_sink, data->audio_sink, NULL);

  if(!gst_element_link_many(data->video_queue_1, data->videoDepayloader, data->video_decoder, data->video_sink, NULL))
  {
    g_printerr("videoqueue, depayloader, decoder, and sink cannot be linked!\n");
    gst_object_unref (data->clientPipeline);
    exit(1);
  }
  
  if(!gst_element_link_many(data->audio_queue_1, data->audioDepayloader, data->audio_decoder, data->audio_sink, NULL))
  {
    g_printerr("audio queue and audio decoder cannot be linked!.\n");
    gst_object_unref (data->clientPipeline);
    exit(1);
  }

  recv_rtp_sink_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->clientRTPBIN), "recv_rtp_sink_%d");
  recv_rtp_sink0 = gst_element_request_pad (data->clientRTPBIN, recv_rtp_sink_temp, NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtp_sink !.\n", gst_pad_get_name (recv_rtp_sink0));
  udpsrc_rtp0_src = gst_element_get_static_pad(data->udpsrc_rtp0, "src");
  recv_rtp_sink1 = gst_element_request_pad (data->clientRTPBIN, recv_rtp_sink_temp, NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtp_sink !.\n", gst_pad_get_name (recv_rtp_sink1));
  udpsrc_rtp1_src = gst_element_get_static_pad(data->udpsrc_rtp1, "src");


  recv_rtcp_sink_temp = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(data->clientRTPBIN), "recv_rtcp_sink_%d");
  recv_rtcp_sink0 = gst_element_request_pad(data->clientRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
  g_print ("Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink0));
  udpsrc_rtcp0_src = gst_element_get_static_pad(data->udpsrc_rtcp0, "src");
   recv_rtcp_sink1 = gst_element_request_pad(data->clientRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
   g_print ("Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink1));
   udpsrc_rtcp1_src = gst_element_get_static_pad(data->udpsrc_rtcp1, "src");


  send_rtcp_src_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->clientRTPBIN), "send_rtcp_src_%d");
  send_rtcp_src0 = gst_element_request_pad(data->clientRTPBIN, send_rtcp_src_temp, NULL, NULL);
  g_print ("Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src0));
  udpsink_rtcp0_sink = gst_element_get_static_pad(data->udpsink_rtcp0, "sink");
  send_rtcp_src1 = gst_element_request_pad(data->clientRTPBIN, send_rtcp_src_temp, NULL, NULL);
  g_print ("Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src1));
  udpsink_rtcp1_sink = gst_element_get_static_pad(data->udpsink_rtcp1, "sink");


   if ( gst_pad_link (udpsrc_rtp0_src, recv_rtp_sink0 )!= GST_PAD_LINK_OK ||
     gst_pad_link (udpsrc_rtp1_src, recv_rtp_sink1 )!= GST_PAD_LINK_OK ||
      gst_pad_link (udpsrc_rtcp0_src, recv_rtcp_sink0 )!= GST_PAD_LINK_OK ||
     gst_pad_link (udpsrc_rtcp1_src, recv_rtcp_sink1 )!= GST_PAD_LINK_OK ||
      gst_pad_link (send_rtcp_src0, udpsink_rtcp0_sink )!= GST_PAD_LINK_OK ||
     gst_pad_link (send_rtcp_src1, udpsink_rtcp1_sink )!= GST_PAD_LINK_OK  )
  {
    g_printerr ("Some requested pads cannot be linked with static pads!\n");
    gst_object_unref (data->clientPipeline);
    exit(1);
  }

  gst_object_unref (udpsrc_rtp0_src);
 gst_object_unref (udpsrc_rtp1_src);
  gst_object_unref (udpsrc_rtcp0_src);
  gst_object_unref (udpsrc_rtcp1_src);
  gst_object_unref (udpsink_rtcp0_sink);
  gst_object_unref (udpsink_rtcp1_sink);
 
  g_signal_connect (data->clientRTPBIN, "pad-added", G_CALLBACK (rtpbin_pad_added_handler), data);

/* Create the GUI */
  create_ui (data);

  bus = gst_element_get_bus (data->clientPipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
  gst_object_unref (bus);

  //  ret=gst_element_set_state(data->clientPipeline, GST_STATE_PLAYING );
  //  if (ret == GST_STATE_CHANGE_FAILURE) {
  //   g_printerr ("Unable to set the pipeline to the playing state.\n");
  //   gst_object_unref (data->clientPipeline);
  //   exit(1);
  // }

/* Start the GTK main loop. We will not regain control until gtk_main_quit is called. */
  gtk_main ();

  /* Free resources */
  gst_element_release_request_pad (data->clientRTPBIN, recv_rtp_sink0);
  gst_element_release_request_pad (data->clientRTPBIN, recv_rtp_sink1);
  gst_element_release_request_pad (data->clientRTPBIN, recv_rtcp_sink0);
  gst_element_release_request_pad (data->clientRTPBIN, recv_rtcp_sink1);
  gst_element_release_request_pad (data->clientRTPBIN, send_rtcp_src0);
  gst_element_release_request_pad (data->clientRTPBIN, send_rtcp_src1);
  gst_object_unref (recv_rtp_sink0);
  gst_object_unref (recv_rtp_sink1);
  gst_object_unref (send_rtcp_src0);
  gst_object_unref (send_rtcp_src1);
  gst_object_unref (recv_rtcp_sink0);
  gst_object_unref (recv_rtcp_sink1);

  gst_object_unref (bus);
  gst_element_set_state (data->clientPipeline, GST_STATE_NULL);
  gst_object_unref (data->clientPipeline);

  pthread_mutex_destroy(&lock);
  return 0;
}
 
static void rtpbin_pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data)
{
      GstPad *video_queue_sinkPad = gst_element_get_static_pad (data->video_queue_1, "sink");
      GstPad *audio_queue_sinkPad ;
      if(data->mode == ACTIVE_MODE)
        audio_queue_sinkPad = gst_element_get_static_pad (data->audio_queue_1, "sink");

      GstPadLinkReturn ret;
      int result;
      GstCaps *new_pad_caps = NULL;
      GstStructure *new_pad_struct = NULL;
      const gchar *new_pad_type = NULL;
       
      g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
      if (data->mode == ACTIVE_MODE && gst_pad_is_linked(video_queue_sinkPad) && gst_pad_is_linked(audio_queue_sinkPad)
          || data->mode == PASSIVE_MODE && gst_pad_is_linked(video_queue_sinkPad) 
         ) {
        g_print ("  We are already linked. Ignoring.\n");
        goto exit;
      }

      /* Check the new pad's type */
      new_pad_caps = gst_pad_get_caps (new_pad);
      new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
      new_pad_type = gst_structure_get_name (new_pad_struct);

       if(strstr(GST_PAD_NAME (new_pad), "recv_rtp_src"))
       {
          if(strstr(GST_PAD_NAME(new_pad), "recv_rtp_src_0"))
           {
                // print_pad_capabilities(data->udpsrc_rtp0, "src");
                // print_pad_capabilities(data->videoDepayloader, "sink");
                ret = gst_pad_link (new_pad, video_queue_sinkPad);
                if (GST_PAD_LINK_FAILED (ret))
                {
                    g_print ("<rtpbin>: New pad is '%s' but link failed with error: %d \n", 
                            GST_PAD_NAME (new_pad), ret);
                }
                else
                {
                    g_print ("<video> Link succeeded (pad '%s') \n ", GST_PAD_NAME (new_pad) );
                
                    data->received_video = TRUE;
                }
           }
           else if(strstr(GST_PAD_NAME(new_pad), "recv_rtp_src_1"))
           {

                // print_pad_capabilities(data->udpsrc_rtp1, "src");
                // print_pad_capabilities(data->audioDepayloader, "sink");
                ret = gst_pad_link (new_pad, audio_queue_sinkPad);
                if (GST_PAD_LINK_FAILED (ret))
                {
                    g_print ("<rtpbin>: New pad is '%s' but link failed with error: %d \n", 
                              GST_PAD_NAME (new_pad), ret);
                }
                else
                {
                    g_print ("<audio> Link succeeded (pad '%s'). \n", GST_PAD_NAME (new_pad) );
                    data->received_audio = TRUE;
                }
           }
      }

    exit:
      /* Unreference the new pad's caps, if we got them */
      if (new_pad_caps != NULL)
        gst_caps_unref (new_pad_caps);
       
      /* Unreference the sink pad */
      gst_object_unref (video_queue_sinkPad);
      if(data->mode == ACTIVE_MODE)
        gst_object_unref (audio_queue_sinkPad);
}

void init_ports(CustomData* data)
{
  data->port_recv_rtp_sink0 = data->clientport+1;
  data->port_recv_rtp_sink1 =data->clientport+2;
  data->port_recv_rtcp_sink0 =data->clientport+3;
  data->port_recv_rtcp_sink1 =data->clientport+4;
  data->port_send_rtcp_src0 =data->clientport+5;
  data->port_send_rtcp_src1 =data->clientport+6;
}

static void delete_event_cb (GtkWidget *widget, GdkEvent *event, CustomData *data) {
  cout <<"FUCK"<<endl;
  stop_cb (NULL, data);
  gtk_main_quit ();
}

static void realize_cb (GtkWidget *widget, CustomData *data) 
{
  GdkWindow *window = gtk_widget_get_window (widget);
  guintptr window_handle;
  printf("in realize_cb!\n");
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


static gboolean expose_cb (GtkWidget *widget, GdkEventExpose *event, CustomData *data) {
  if (data->state < GST_STATE_PAUSED) {
    GtkAllocation allocation;
    GdkWindow *window = gtk_widget_get_window (widget);
    cairo_t *cr;
    
    //Cairo is a 2D graphics library which we use here to clean the video window.
     //It is used by GStreamer for other reasons, so it will always be available to us. 
    gtk_widget_get_allocation (widget, &allocation);
    cr = gdk_cairo_create (window);
    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
    cairo_fill (cr);
    cairo_destroy (cr);
  }
  
  return FALSE;
}


// #define CONNECT 0
// #define PASSIVE 1
// #define ACTIVE 2
// #define PLAY 3
// #define PAUSE 4
// #define STOP 5
// #define FORWARD 6
// #define REWIND 7

static void remove_audio_links(CustomData* data)
{
  if(data->audio_removed) return ;
  gst_element_set_state(data->audio_decoder, GST_STATE_NULL);
  gst_element_set_state(data->audio_queue_1, GST_STATE_NULL);
  gst_element_set_state(data->audioDepayloader, GST_STATE_NULL);
  gst_element_set_state(data->udpsrc_rtp1, GST_STATE_NULL);
  gst_element_set_state(data->udpsink_rtcp1, GST_STATE_NULL);
  gst_element_set_state(data->udpsrc_rtcp1, GST_STATE_NULL);
  gst_element_set_state(data->audio_sink, GST_STATE_NULL);

  gst_bin_remove_many(GST_BIN(data->clientPipeline), 
                      data->audio_decoder, data->audio_queue_1, data->audioDepayloader,data->udpsrc_rtp1, data->udpsink_rtcp1,
                      data->udpsrc_rtcp1, data->audio_sink, NULL);
  data->audio_removed = TRUE;
}




static void passive_cb(GtkButton *button, CustomData * data)
{
  if(data->connected)
  {
    stop_cb(NULL, data);
  }
  if(data->mode == ACTIVE_MODE)
    remove_audio_links(data);
  data->mode_selected = TRUE;
  data->mode = PASSIVE_MODE;
}

static void active_cb(GtkButton *button,CustomData * data)
{
  if(data->connected)
  {
    stop_cb(NULL, data);
  }
  data->mode_selected = TRUE;
  data->mode = ACTIVE_MODE;      
}

static void play_cb(GtkButton *button,CustomData * data)
{
  if(!data->connected) {
    fprintf(stderr, "Not connected to any server!\n" );
    return;
  }
   // if(GST_STATE(data->clientPipeline) == GST_STATE_PLAYING){
   //    fprintf(stderr, "Already playing!\n" );
   //    return;
   // }
  char* msg = NULL;
  asprintf(&msg, "%d %d %d", PLAY, data->clientport, BIND_WIDTH);
  printf("%s\n", msg);
  sendMsg2Server(serverIP.c_str(), serverPort.c_str(), msg);

  gst_element_set_state (data->clientPipeline, GST_STATE_PLAYING);

}

static void connect_cb(GtkButton *button,CustomData * data)
{
  if(!data->mode_selected)
  {
      print_status("please select a mode first!\n");
      return;
  }
  if(data->connected)
  {
    print_status("Already connected!\n");
    return ;
  }

  if(!data->check_button){
    print_status("please select a video QoS first!\n");     
    return ;
  }

  gchar* mesg = NULL;
  gchar * ip = NULL;
  gchar * port = NULL;
  ip=(gchar*)gtk_entry_get_text(data->ip_text);
  port=(gchar*)gtk_entry_get_text(data->port_text);
  //need to do resource admission here local machine! 
  int aba;
  char buf_tmp[100];
  GtkTextBuffer *buffer;
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text1));
  if(( GTK_TOGGLE_BUTTON(data->check_button)->active  ) )
  {
    aba=admissionProcess("resource.txt", LEVEL1BR);
    if (aba<0){
      print_status("Admission decline at local client machine!\n");
      return ;
    }
    else
      print_status("Admission permit at local client machine!\n");
  }
  else
  {
    aba=admissionProcess("resource.txt", LEVEL2BR);
    if (aba<0){
      print_status("Admission decline at local client machine!\n");
      return ;
    }
    else
      print_status("Admission permit at local client machine!\n");
  }
  sprintf(buf_tmp,"Current ABA= %d KB/Sec \n",aba);
  gtk_text_buffer_set_text (buffer, buf_tmp, -1);
 
  if( ip && port && strlen(ip)!=0 && strlen(port)!=0 )
  {
    serverIP = string(ip);
    serverPort = string(port);
  }
  else
  {
    serverIP = defaultServerIP;
    serverPort = defaultServerPort;
  }
  // printf("%s %s\n", serverIP.c_str(), serverPort.c_str() );


  asprintf(&mesg, "%d %d %d %d %d", CONNECT, data->clientport, BIND_WIDTH, data->mode, data->res_mode);
  printf("%s\n", mesg);

  gchar * host = (gchar*)malloc(256);
  int d1,d2,d3,d4,d5,d6;

  sprintf(status_buf, "<client> set connecting address to: %s, at ports %d, %d, %d, %d, %d, %d\n", serverIP.c_str(), data->port_recv_rtp_sink0,data->port_recv_rtp_sink1,
                                    data->port_recv_rtcp_sink0, data->port_recv_rtcp_sink1, data->port_send_rtcp_src0 , data->port_send_rtcp_src1 );
  print_status(status_buf);

  g_object_set(data->udpsrc_rtp0, "caps", data->videocaps, "port", data->port_recv_rtp_sink0,   NULL);
  g_object_set(data->udpsrc_rtcp0, "port", data->port_recv_rtcp_sink0 ,NULL);
  g_object_set(data->udpsink_rtcp0, "host", (gchar*)serverIP.c_str(), "port", data->port_send_rtcp_src0, "async", FALSE, "sync", FALSE, NULL);
  if(data->mode == ACTIVE_MODE)
  {
    g_object_set(data->udpsrc_rtp1,  "caps", data->audiocaps, "port", data->port_recv_rtp_sink1,  NULL);
    g_object_set(data->udpsrc_rtcp1, "port", data->port_recv_rtcp_sink1 ,NULL);
    g_object_set(data->udpsink_rtcp1, "host", (gchar*)serverIP.c_str(), "port", data->port_send_rtcp_src1, "async", FALSE, "sync", FALSE,  NULL);
  }

   GstStateChangeReturn  ret=gst_element_set_state(data->clientPipeline, GST_STATE_PLAYING );
   if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data->clientPipeline);
    exit(1);
  }
  printf("%s %s\n", serverIP.c_str(), serverPort.c_str() );

  sendMsg2Server(serverIP.c_str(), serverPort.c_str(), mesg);
}


static void check_cb1(GtkButton *button,CustomData * data)
{
    if(( GTK_TOGGLE_BUTTON(button)->active  ) )
    {
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON (data->check_button2), false );
      data->res_mode = HIGH;
    }
    else
    {

      //printf("off\n");
      //gtk_toggle_button_set_active( GtkToggleButton (data->check_button2, 1 );
    }
}

static void check_cb2(GtkButton *button,CustomData * data)
{
     if((GTK_TOGGLE_BUTTON(button)->active  ) )
    {

     // printf("on\n");
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON (data->check_button), false );
     // gtk_toggle_button_set_active( GtkToggleButton (data->check_button), 0 );
      data->res_mode = LOW;
    }
    else
    {

    //  printf("off\n");
      //gtk_toggle_button_set_active( GtkToggleButton (data->check_button2, 1 );
    }

}

static void pause_cb (GtkButton *button, CustomData *data)
{
  if(!data->connected)
  {
    print_status( "Not connected to any server!\n");
    return ;
  }
   if(GST_STATE(data->clientPipeline) == GST_STATE_PAUSED){
      fprintf(stderr, "Already paused!\n" );
      return;
   }
  gchar* mesg = NULL;
  asprintf(&mesg, "%d %d %d", PAUSE, data->clientport, BIND_WIDTH);
  sendMsg2Server(serverIP.c_str(), serverPort.c_str(), mesg);
}
/* This function is called when the STOP button is clicked */
static void stop_cb (GtkButton *button, CustomData *data) {
  if(!data->connected)
  {
    fprintf(stderr, "Not connected to server!\n");
    return ;
  }
  if(GST_STATE(data->clientPipeline) == GST_STATE_NULL || GST_STATE(data->clientPipeline) == GST_STATE_READY){
      fprintf(stderr, "Already stoped!\n" );
      return;
   }
  // gst_element_set_state (data->clientPipeline, GST_STATE_NULL);

  gchar* mesg = NULL;
  asprintf(&mesg, "%d %d %d", STOP, data->clientport, BIND_WIDTH);
  printf("%s\n", mesg );
  sendMsg2Server(serverIP.c_str(), serverPort.c_str(), mesg);

}

static void ff_cb (GtkButton *button, CustomData *data) 
{
  if(!data->connected)
  {
    fprintf(stderr, "Not connected to any server!\n");
    return ;
  }
  gchar* mesg = NULL;
  asprintf(&mesg, "%d %d %d", FORWARD, data->clientport, BIND_WIDTH);
  printf("%s\n", mesg );
  sendMsg2Server(serverIP.c_str(), serverPort.c_str(), mesg);
}

static void rw_cb (GtkButton *button, CustomData *data) 
{
  if(!data->connected)
  {
    fprintf(stderr, "Not connected to any server!\n");
    return ;
  }

  gchar* mesg = NULL;
  asprintf(&mesg, "%d %d %d", REWIND, data->clientport, BIND_WIDTH);
  printf("%s\n", mesg );
  sendMsg2Server(serverIP.c_str(), serverPort.c_str(), mesg);
}

/* This function is called when an End-Of-Stream message is posted on the bus.
 * We just set the pipeline to READY (which stops playback) */
static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  g_print ("End-Of-Stream reached.\n");
  gst_element_set_state (data->clientPipeline, GST_STATE_READY);
}
   
/* This function is called when the pipeline changes states. We use it to
 * keep track of the current state. */
static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->clientPipeline)) {
    data->state = new_state;
    g_print ("State set to %s\n", gst_element_state_get_name (new_state));
    if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
      /* For extra responsiveness, we refresh the GUI as soon as we reach the PAUSED state */
      refresh_ui (data);
    }
  }
}
 
static void disconnect_from_network(CustomData* data)
{
   //to do: unlink video queue and audio queue from the RTP bin
    return;
}


static void print_status(const char* inp)
{
  printf("%s\n",inp);
  curr_data->buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (curr_data->status_text));
  gtk_text_buffer_insert_at_cursor(curr_data->buffer,inp,strlen(inp));
}


static void create_status (CustomData *data) {
  GtkWidget *status_window;
  GtkWidget *status_hbox;
  
  char* title = NULL;
  asprintf(&title, "client-%d", data->clientport);
  status_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title((GtkWindow * )status_window, title);

  data->status_text=gtk_text_view_new();
  data->buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->status_text));
  
  sprintf(status_buf,"Network Status Log:\n");
  gtk_text_buffer_set_text (data->buffer, status_buf, -1);
  //gtk_box_pack_start (GTK_BOX (status_hbox), data->status_text, FALSE, FALSE, 2);
  gtk_container_add (GTK_CONTAINER (status_window), data->status_text);
  gtk_window_set_default_size (GTK_WINDOW (status_window), 320, 320);
  gtk_widget_show_all(status_window);
}


static void create_ui (CustomData *data) {
  GtkWidget *main_window;  /* The uppermost window, containing all other windows */
  GtkWidget *main_box;     /* VBox to hold main_hbox and the controls */
  GtkWidget *main_hbox;    /* HBox to hold the video_window and the stream info text widget */
  GtkWidget *controls,*control2, *control3,*control4; /* HBox to hold the buttons and the slider */
  GtkWidget *passive_button, *active_button, *play_button, *pause_button, *stop_button, *fast_forward, *rewind_button, *connect_button; /* Buttons *//* Buttons */
  GtkWidget *label, *label2;
  GtkWidget * view, *modify_button;

  gchar* window_title = NULL;
  asprintf(&window_title, "client-%d", data->clientport);
  main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title((GtkWindow * )main_window, window_title);
  
  g_signal_connect (G_OBJECT (main_window), "delete-event", G_CALLBACK (delete_event_cb), (CustomData*)data);
   
  data->video_window = gtk_drawing_area_new ();
  gtk_widget_set_double_buffered (data->video_window, FALSE);
  g_signal_connect (data->video_window, "realize", G_CALLBACK (realize_cb), data);
 g_signal_connect (data->video_window, "expose_event", G_CALLBACK (expose_cb), data);

  passive_button = gtk_button_new_with_label ("passive");

  g_signal_connect (G_OBJECT (passive_button), "clicked", G_CALLBACK (passive_cb), data);

  active_button = gtk_button_new_with_label ("active");
  g_signal_connect (G_OBJECT (active_button), "clicked", G_CALLBACK (active_cb), data);

  connect_button = gtk_button_new_with_label ("connect");
  g_signal_connect (G_OBJECT (connect_button), "clicked", G_CALLBACK (connect_cb), data);
  //printf("check 1\n");
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

  modify_button = gtk_button_new_with_label ("set");
  g_signal_connect (G_OBJECT (modify_button), "clicked", G_CALLBACK (modify_cb), data);
  data->check_button=gtk_check_button_new_with_label ("High Quaility Mode: 640X480 (~1000 Kpbs) ");
  data->check_button2=gtk_check_button_new_with_label ("Low Quaility Mode: 320X240 (~344 Kpbs) ");
  g_signal_connect(G_OBJECT(data->check_button), "clicked", G_CALLBACK(check_cb1), data);
  g_signal_connect(G_OBJECT(data->check_button2), "clicked", G_CALLBACK(check_cb2), data);
  (data->ba_text) = (GtkEntry*)gtk_entry_new ();
  label = gtk_label_new ("Enter IP: ");
  label2= gtk_label_new ("Enter PORT: ");


  data->text1=gtk_text_view_new();
  data->buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->text1));
  char buf_tmp[100];
  sprintf(buf_tmp,"Please Set ABA\n");
  gtk_text_buffer_set_text (data->buffer, buf_tmp, -1);

  
  controls = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (controls), passive_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), active_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), fast_forward, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), rewind_button, FALSE, FALSE, 2);

  control2=gtk_hbox_new (FALSE, 0);
  //printf("check 2\n");
  gtk_box_pack_start (GTK_BOX (control2), label, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (control2), (GtkWidget*) (data->ip_text), FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (control2), label2, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (control2), (GtkWidget*) (data->port_text), FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (control2), connect_button, FALSE, FALSE, 2);



  control3=gtk_hbox_new (FALSE, 0);
  control4=gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (control4), data->text1, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (control4), (GtkWidget*) (data->ba_text), FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (control4), modify_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (control3), data->check_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (control3), data->check_button2, FALSE, FALSE, 2);
  //printf("check 3\n");

  main_hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), data->video_window, TRUE, TRUE, 0);
  
  main_box = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), main_hbox, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), controls, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), control2, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), control3, FALSE, FALSE, 0);
gtk_box_pack_start (GTK_BOX (main_box), control4, FALSE, FALSE, 0);

  gtk_container_add (GTK_CONTAINER (main_window), main_box);
  gtk_window_set_default_size (GTK_WINDOW (main_window), 640, 480);
  printf("before show\n");
  gtk_widget_show_all (main_window);
 
 }

int main(int argc, char *argv[]) 
{
  gst_init(&argc, &argv);
  gtk_init (&argc, &argv);
  pthread_t t1,t2;
 
  CustomData* data = (CustomData*)malloc(sizeof(CustomData));
  curr_data=data;
  memset (data, 0, sizeof (data));
  srand (time(NULL));
  data->clientport = 2000+rand()%10000 ;
  init_ports(data);
  data->mode_selected = FALSE;
  data->connected= FALSE;
  data->audio_removed = FALSE;
  data->mode = ACTIVE_MODE;
  data->res_mode = HIGH;
  create_status(data);

 
  pthread_create(&t1,NULL, RecvVideo, (void*)data);
  pthread_create(&t2,NULL, listening_feedbacks, (void*)data);
   
  pthread_join(t1,NULL);
  pthread_join(t2,NULL);

  free(data);
  return 0;
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






