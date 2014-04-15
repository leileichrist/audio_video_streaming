#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fstream>
#include <string.h>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>         // std::this_thread::sleep_for
#include <chrono> 
#include <time.h>       /* time */

#include <gtk/gtk.h>
#include <string>
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

#define MAXIMUM_BW 10000
#define SERVER_PORT "3490"
#define MAXBUFLEN 100
#define BACKLOG 10  
#define MAXDATASIZE 256 // max number of bytes we can get at once 
#define ACTIVE_MODE 0
#define PASSIVE_MODE 1
#define DEFAULT_FILE "high.mkv"

#define CONNECT 0
#define PASSIVE 1
#define ACTIVE 2
#define PLAY 3
#define PAUSE 4
#define STOP 5
#define FORWARD 6
#define REWIND 7

#define HIGHFRAMERATE 500

#define SERVER_MAX_BW 1500000

#define LEVEL1BR 1000
#define LEVEL2BR 344
#define LEVEL3BW 100


using namespace std;

gchar* video_codec=NULL;
gchar* audio_codec=NULL;
gchar* container_format=NULL;


typedef struct _CustomData {
  GstElement *serverPipeline;
  GstElement *filesrc;
  GstElement *demuxer;
  GstElement *audio_filter;
  GstElement *audio_queue_1, *video_queue_1;
  GstElement *video_decoder, *audio_decoder;
  GstElement *video_encoder, *audio_encoder;

  GstElement *videorate_controller, *audiorate_controller;
  GstElement *videoPayloader, *audioPayloader;
  GstElement *audio_queue_2, *video_queue_2;

  GstElement *serverRTPBIN;
  GstElement *udpsink_rtp0, *udpsink_rtp1, *udpsink_rtcp0, *udpsink_rtcp1 ;
  GstElement *udpsrc_rtcp0, *udpsrc_rtcp1 ;
  GstDiscoverer *discoverer;

  GstCaps *video_caps;
  GstCaps *audio_caps;

  gint resolution;
  gint mode;
  gint res_mode;
  gint FPS;

  gchar* clientip;
  gint clientport;
  gint port_send_rtp_src0 ;
  gint port_send_rtp_src1 ;
  gint port_send_rtcp_src0 ;
  gint port_send_rtcp_src1 ;
  gint port_recv_rtcp_sink0 ;
  gint port_recv_rtcp_sink1 ;

  GMainLoop *main_loop;  /* GLib's Main Loop */

} CustomData;

static int ServerTempBW;
static unordered_map<int, CustomData*> clients;
static int available_BW ;
static void demuxer_pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data);
static void rtpbin_pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data);
static void print_tag_foreach (const GstTagList *tags, const gchar *tag, gpointer user_data);
static void find_tag_foreach (const GstTagList *tags, const gchar *tag, gpointer user_data);
static void* establish_connection(void* arg);
int sendMsg2Client(const char* desIp, const char* desPort, const char* mesg);


static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) ;
static void print_caps (const GstCaps * caps, const gchar * pfx);
static void print_pad_capabilities (GstElement *element, gchar *pad_name) ;
static void rtpbin_pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data);


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




/* Process messages from GStreamer */
static gboolean handle_message (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;
   
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (msg, &err, &debug_info);
      g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
      g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
      g_clear_error (&err);
      g_free (debug_info);
      g_main_loop_quit (data->main_loop);
      break;
    case GST_MESSAGE_EOS:
      g_print ("End-Of-Stream reached.\n");
      g_main_loop_quit (data->main_loop);
      break;
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->serverPipeline)) {
        g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
        // if (new_state == GST_STATE_PLAYING) {
        //    Once we are in the playing state, analyze the streams 
        //   // analyze_streams (data);
        // }
      }
    } break;
  }
   
  /* We want to keep receiving messages */
  return TRUE;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


/*init other ports, make sure ports are not repeated*/
void init_ports(CustomData* data)
{
  data->port_send_rtp_src0 = data->clientport + 1;
  data->port_send_rtp_src1 = data->clientport + 2;
  data->port_send_rtcp_src0 = data->clientport + 3;
  data->port_send_rtcp_src1 = data->clientport + 4;
  data->port_recv_rtcp_sink0 = data->clientport + 5;
  data->port_recv_rtcp_sink1 = data->clientport + 6;
}

int sendMsg2Client(const char* desIp, const char* desPort, const char* mesg)
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
    printf("client  ip is %s\n",toip);

    if ((numbytes = sendto(sockfd, msg, strlen(msg), 0,
         p->ai_addr, p->ai_addrlen)) == -1) {
      perror("sender: sendto");
      exit(1);
    }
    freeaddrinfo(servinfo);
    close(sockfd);
    return 0;
}

gboolean fast_forward(gint port)
{
    if(clients.find(port)==clients.end())
    {
      printf("Client %d does not exist!\n", port);
      return FALSE;
    }
    CustomData* data = clients[port];
    if(!data->serverPipeline || GST_STATE(data->serverPipeline) == GST_STATE_READY || GST_STATE(data->serverPipeline) == GST_STATE_NULL)
    {
      printf("Server cannot perform fast forward on client %d \n", port);
      return FALSE;
    }
    gint64 current;
    GstFormat fmt = GST_FORMAT_TIME;

    if (gst_element_query_position (data->serverPipeline, &fmt, &current)) 
    {
      current+=6*GST_SECOND;
      gst_element_seek_simple (data->serverPipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH , current);

      g_print("fast forwarding to %d second \n", (gint)(current/GST_SECOND ) );
    }

    return TRUE;
}   

gboolean rewind(gint port)
{
    if(clients.find(port)==clients.end())
    {
      printf("Client %d does not exist!\n", port);
      return FALSE;
    }
    CustomData* data = clients[port];
    if(!data->serverPipeline || GST_STATE(data->serverPipeline) == GST_STATE_READY || GST_STATE(data->serverPipeline) == GST_STATE_NULL)
    {
      printf("Server cannot perform rewinding on client %d \n", port);
      return FALSE;
    }
    gint64 current;
    GstFormat fmt = GST_FORMAT_TIME;

    if (gst_element_query_position (data->serverPipeline, &fmt, &current)) 
    {
      current-=6*GST_SECOND;
      if(current<0) current = 0;
      gst_element_seek_simple (data->serverPipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH , current);
      g_print("rewinding to %d second \n", (gint)(current/GST_SECOND ) );
    }
    return TRUE;
} 

gboolean process_message(queue<pthread_t*>& wq, struct sockaddr_storage * their_addr, const char* msg)
{
      char s[INET6_ADDRSTRLEN];
      char* buf = NULL;
      char* c;
      int cmd, port, bw, mode, res_mode;
      asprintf(&buf, "%s", msg);
      struct sockaddr* fromaddr = (struct sockaddr *)their_addr ; 
      GstStateChangeReturn ret;
      cmd = atoi(buf);
      c = strstr(buf, " ");
      if(!c)
      {
        fprintf(stderr, "Unrecognized message!\n");
        return FALSE;
      }
      char* clientip = (char*)inet_ntop(their_addr->ss_family, get_in_addr(fromaddr), s, sizeof s) ;

      port = atoi(++c);
      c = strstr(c, " ");
      bw = atoi(++c);
      if(c = strstr(c, " "))
      {
          mode = atoi(++c);
          if(c = strstr(c, " "))
            res_mode = atoi(++c);
      }

      printf("!!receive command: cmd:%d port:%d bw:%d mode:%d res_mode:%d\n", cmd, port, bw, mode, res_mode );
      switch(cmd)
      {
          //this case is for creating a new connection:
          case CONNECT:
          {
              int result ;
              if(clients.find(port)!=clients.end())
              {
                  printf("Client exists!\n");
                  return FALSE;
              }
              if(res_mode == 0 )
              {
                  if(ServerTempBW-LEVEL1BR>0)
                  {
                      ServerTempBW=ServerTempBW-LEVEL1BR;
                      cout <<"accept the highest level BW to the client"<<endl;
                      cout <<"Server temp Bindwidth:" << ServerTempBW <<endl;
                      admissionProcess("Sresource.txt", LEVEL1BR);
                      cout <<"findish change"<<endl;
                      result = 0 ;
                  }
                  else
                  {
                     if (ServerTempBW-LEVEL2BR>0)
                     {
                         res_mode=1;
                         ServerTempBW=ServerTempBW-LEVEL2BR;
                         cout <<"server cannot satisfy the highest requirments, then negotation to second level"<<endl;
                         admissionProcess("Sresource.txt", LEVEL2BR);
                         result = 1;
                     }
                    else
                    {
                        cout <<"cannot accept any level, server decline request"<<endl;
                        sendMsg2Client(clientip, to_string(port).c_str(), "0 2"); 
                        return FALSE;
                    }
                 }
              }
              else 
              {  // data->res_mode == 1
                  if (ServerTempBW-LEVEL2BR>0)
                  {
                      ServerTempBW=ServerTempBW-LEVEL2BR;
                      cout <<"server satisfy the highest requirments, 2nd level"<<endl;
                      admissionProcess("Sresource.txt", LEVEL2BR);
                      result = 0;
                  }
                  else
                  {
                    cout <<"cannot accept any level, server decline request"<<endl;
                    sendMsg2Client(clientip, to_string(port).c_str(), "0 2"); 

                    return FALSE;
                  }
              }
              CustomData* data = (CustomData*)malloc(sizeof(CustomData));
              data->clientport = port;
              data->clientip = NULL;
              asprintf(&data->clientip, "%s", clientip);
              data->mode = mode;
              data->res_mode = res_mode;
              init_ports(data);
              printf("<server>: got connection message : %s from %s at real port %s \n", buf,
                data->clientip, to_string(((struct sockaddr_in*)fromaddr)->sin_port).c_str());
              data->serverPipeline = NULL;            
              clients[data->clientport] = data;  
              char* response = NULL;
              asprintf(&response, "%d %d", CONNECT, result);
              sendMsg2Client(data->clientip, to_string(data->clientport).c_str(), response);
              break;
          }
          case PASSIVE:
          {
             break;
          } 
          case ACTIVE:
          {
            break;
          }
          case PLAY:
          {
            if(clients.find(port)==clients.end())
            {
                printf("Client does not exist!\n");
                return FALSE;
            }
            CustomData* data = clients[port];
            if(!data->serverPipeline)
            {
                pthread_t* td = new pthread_t;
                wq.push(td);
                pthread_create(td, NULL, establish_connection, (void*)data);
                return TRUE;
            }
            else if(GST_STATE(data->serverPipeline) == GST_STATE_PLAYING)
            {
                fprintf(stderr, "state already playing!\n" );
                return FALSE;
            }
            else
            {
              ret = gst_element_set_state(data->serverPipeline, GST_STATE_PLAYING);
              if(ret == GST_STATE_CHANGE_FAILURE)
              {
                sendMsg2Client(data->clientip, to_string(data->clientport).c_str(), "3 1"); 
              }
              else
              {
                printf("<server>: playing state: %d\n", data->mode);
                sendMsg2Client(data->clientip, to_string(data->clientport).c_str(), "3 0"); 
              }
              return TRUE;
            }
            break;
          }
          case PAUSE:
          {
              if(clients.find(port)==clients.end())
              {
                  printf("Client does not exist!\n");
                  return FALSE;
              }
              CustomData* data = clients[port];
              if(GST_STATE(data->serverPipeline) == GST_STATE_PAUSED)
              {
                printf("Server side already paused\n");
              }
              ret = gst_element_set_state(data->serverPipeline, GST_STATE_PAUSED);
              if(ret == GST_STATE_CHANGE_FAILURE )
              {
                sendMsg2Client(data->clientip, to_string(data->clientport).c_str(), "4 1"); 
              }
              else
              {
                sendMsg2Client(data->clientip, to_string(data->clientport).c_str(), "4 0"); 
              }
              break;
          }
          case STOP:
          {
              if(clients.find(port)==clients.end())
              {
                  printf("Client does not exist!\n");
                  return FALSE;
              }
              CustomData* data = clients[port];
              if(data->serverPipeline == NULL)
              {
                  if (data->res_mode ==0){
                    ServerTempBW+=LEVEL1BR;
                    changeABA("Sresource.txt",ServerTempBW);
                  }
                  if (data->res_mode ==1){
                    ServerTempBW+=LEVEL2BR;
                    changeABA("Sresource.txt",ServerTempBW);
                  }

                  clients.erase(port);
                  sendMsg2Client(data->clientip, to_string(data->clientport).c_str(), "5 0"); 
                  return TRUE;
              }
              if(GST_STATE(data->serverPipeline) == GST_STATE_READY)
              {
                printf("Server side already stoped\n");
                return FALSE;
              }
              ret = gst_element_set_state(data->serverPipeline, GST_STATE_NULL);
              gst_object_unref(data->serverPipeline);
              data->serverPipeline = NULL;
              if(ret == GST_STATE_CHANGE_FAILURE )
              {
                sendMsg2Client(data->clientip, to_string(data->clientport).c_str(), "5 2"); 
              }
              else
              {
                if (data->res_mode ==0){
                  ServerTempBW+=LEVEL1BR;
                  changeABA("Sresource.txt",ServerTempBW);
                }
                if (data->res_mode ==1){
                  ServerTempBW+=LEVEL2BR;
                  changeABA("Sresource.txt",ServerTempBW);
                }
                                
                sendMsg2Client(data->clientip, to_string(data->clientport).c_str(), "5 0"); 
                clients.erase(port);
              }
            break;
          }
          case FORWARD:
          {
            if(fast_forward(port))
              sendMsg2Client(clientip, to_string(port).c_str(), "6 0"); 
            else
            {
              sendMsg2Client(clientip, to_string(port).c_str(), "6 1"); 
            }
            break;
          }
          case REWIND:
          {
            if(rewind(port))
              sendMsg2Client(clientip, to_string(port).c_str(), "7 0"); 
            else
            {
              sendMsg2Client(clientip, to_string(port).c_str(), "7 1"); 
            }
            break;
          }
          default:
          {
            fprintf(stderr, "Unrecognized message!\n");
            return FALSE;
          }
      }
      return TRUE;
}


int main(int argc, char *argv[]) 
{
    changeABA("Sresource.txt", SERVER_MAX_BW);
    ServerTempBW=SERVER_MAX_BW;
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[MAXBUFLEN];
    socklen_t addr_len;
    gst_init(&argc, &argv);
    available_BW = MAXIMUM_BW;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, SERVER_PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
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
        return 2;
    }

    queue<pthread_t*> wq;
    while(1)
    {
        printf("Server: waiting to recvfrom...\n");
        addr_len = sizeof their_addr;
        if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
            (struct sockaddr *)&their_addr, &addr_len)) == -1) {
            perror("recvfrom");
            exit(1);
        }
        buf[numbytes] = '\0';
        printf("received: %s\n", buf);
        process_message(wq, &their_addr, buf);
    }
    while(!wq.empty())
    {
        pthread_t* temp = wq.front();
        pthread_join(*temp, NULL);
        delete temp;
        wq.pop();
    }
        
    close(sockfd);
    return 0;
}


static void* establish_connection(void* arg)
{
      CustomData* data = (CustomData*)arg;
      
      /*Pads for requesting*/
      GstPadTemplate *send_rtp_sink_temp, *send_rtcp_src_temp, *recv_rtcp_sink_temp;
      GstPad *send_rtp_sink0, *send_rtp_sink1;
      GstPad *send_rtcp_src0, *send_rtcp_src1;
      GstPad *recv_rtcp_sink0, *recv_rtcp_sink1; 

      /*static pads*/
      GstPad *video_queue2_srcPad, *audio_queue2_srcPad;
      GstPad *udpsink_rtcp0_sink, *udpsink_rtcp1_sink ;
      GstPad *udpsrc_rtcp0_src, *udpsrc_rtcp1_src ;

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
      char* filename  = NULL;

      if(data->mode == ACTIVE)
      {
        data->FPS = 25;
      }
      else
      {
        data->FPS = 10;
      }

      if (data->res_mode==0)
        asprintf(&filename, "high.mkv");
      else if (data->res_mode==1)
        asprintf(&filename, "low.mkv");
      else
      {
                        // sendMsg2Client(data->clientip, to_string(data->clientport).c_str(), "0 2"); 

        // sendMsg2Client(data->clientip,"No video found");
      }

      if (getcwd(cwd, sizeof(cwd)) != NULL)
        fprintf(stdout, "Current working dir: %s\n", cwd);
      else
      {
        perror("getcwd_error");
        return NULL;
      }
      if(asprintf(&FILE_LOCATION, "%s/%s", cwd, filename)<0)
      {
        perror("cannot find file directory\n");
        return NULL;
      }
      g_print("%s\n", FILE_LOCATION);
      if(asprintf(&uri, "%s%s", "file:", FILE_LOCATION)<0)
      {
        perror("cannot find file directory\n");
        return NULL;
      }

      memset (data, 0, sizeof (data));
      data->discoverer = gst_discoverer_new (5 * GST_SECOND, &err);

      if (!data->discoverer) {
        g_print ("Error creating discoverer instance: %s\n", err->message);
        g_clear_error (&err);
        return NULL;
      }

      fileInfo = gst_discoverer_discover_uri(data->discoverer, uri , &err);
      if(!gst_discoverer_info_get_seekable(fileInfo)) 
      {
        g_print("uri cannot be seeked! \n");
        // sendMsg2Client("No video found");
        return NULL;
      }

      tagList = gst_discoverer_info_get_tags (fileInfo);
      if (tagList) {
        gst_tag_list_foreach (tagList, find_tag_foreach, GINT_TO_POINTER (1));
        gst_tag_list_foreach (tagList, print_tag_foreach, GINT_TO_POINTER (1));
      }
      
      /* Create pipeline and attach a callback to it's
       * message bus */
      data->serverPipeline = gst_pipeline_new("server");
       /* Create elements */

      data->filesrc = gst_element_factory_make("filesrc", "filesrc");

      data->demuxer = gst_element_factory_make("matroskademux", "demuxer");
     
      data->video_decoder = gst_element_factory_make("jpegdec", "video_decoder");
      data->video_encoder = gst_element_factory_make("jpegenc", "video_encoder");

      data->audio_encoder = gst_element_factory_make("amrnbenc", "audio_encoder");    

      data->audio_queue_1 = gst_element_factory_make("queue", "audio_queue_1");
      data->video_queue_1 = gst_element_factory_make("queue", "video_queue_1");

      data->audio_queue_2 = gst_element_factory_make("queue", "audio_queue_2");
      data->video_queue_2 = gst_element_factory_make("queue", "video_queue_2");

      data->videorate_controller = gst_element_factory_make("videorate","videorate_controller");
      data->audiorate_controller = gst_element_factory_make("audiorate","audiorate_controller");

      data->videoPayloader = gst_element_factory_make("rtpjpegpay","videoPayloader");
      data->audioPayloader = gst_element_factory_make("rtpamrpay","audioPayloader");

      data->serverRTPBIN = gst_element_factory_make("gstrtpbin","serverRTPBIN");

      data->udpsink_rtp0 = gst_element_factory_make("udpsink", "udpsink_rtp0");
      data->udpsink_rtp1 = gst_element_factory_make("udpsink", "udpsink_rtp1");

      data->udpsink_rtcp0 = gst_element_factory_make("udpsink", "udpsink_rtcp0");
      data->udpsink_rtcp1 = gst_element_factory_make("udpsink", "udpsink_rtcp1");
      
      data->udpsrc_rtcp0 = gst_element_factory_make("udpsrc", "udpsrc_rtcp0");
      data->udpsrc_rtcp1 = gst_element_factory_make("udpsrc", "udpsrc_rtcp1");


      /* Check that elements are correctly initialized */
      if(!(data->serverPipeline && data->filesrc && data->demuxer &&  data->audio_encoder && data->video_decoder && data->video_encoder &&
           data->audio_queue_1 && data->video_queue_1 && data->audio_queue_2 && data->video_queue_2 && 
           data->videorate_controller && data->audiorate_controller && data->videoPayloader && data->audioPayloader &&
           data->serverRTPBIN && data->udpsink_rtp0 && data->udpsink_rtp1 && data->udpsink_rtcp0 && data->udpsink_rtcp1 &&
           data->udpsrc_rtcp0 && data->udpsrc_rtcp1 ))
      {
        g_critical("Couldn't create pipeline elements");
        return NULL;
      }
       g_object_set(data->filesrc, "location", FILE_LOCATION, NULL);

       printf("<server>: %s, %d, %d, %d, %d, %d, %d\n", data->clientip, data->port_send_rtp_src0,data->port_send_rtp_src1,
                                      data->port_send_rtcp_src0, data->port_send_rtcp_src1,data->port_recv_rtcp_sink0 , data->port_recv_rtcp_sink1 );

       g_object_set(data->udpsink_rtp0, "host", data->clientip, "port", data->port_send_rtp_src0 ,/*"async", FALSE, "sync",TRUE ,*/NULL);
       g_object_set(data->udpsink_rtp1, "host", data->clientip, "port", data->port_send_rtp_src1 , NULL);
       g_object_set(data->udpsink_rtcp0, "host", data->clientip, "port", data->port_send_rtcp_src0, "async", FALSE, "sync", FALSE, NULL);
       g_object_set(data->udpsink_rtcp1, "host", data->clientip, "port", data->port_send_rtcp_src1, "async", FALSE, "sync", FALSE, NULL);
       g_object_set(data->udpsrc_rtcp0 , "port", data->port_recv_rtcp_sink0 , NULL);
       g_object_set(data->udpsrc_rtcp1 , "port", data->port_recv_rtcp_sink1 , NULL);
       g_object_set(data->videorate_controller , "drop-only", TRUE, "max-rate", data->FPS , NULL);


        data->video_caps = gst_caps_new_simple("image/jpeg",
            "framerate", GST_TYPE_FRACTION, 30, 1,
            NULL);

      data->audio_caps = gst_caps_new_simple("audio/x-raw-int",
          "rate", G_TYPE_INT, 8000,
          
          NULL);

      /* Add elements to the pipeline. This has to be done prior to
       * linking them */
      if(data->mode == ACTIVE_MODE)
      {
        gst_bin_add_many(GST_BIN(data->serverPipeline),data->filesrc, data->demuxer, data->audio_queue_1, /*data->video_decoder, data->video_encoder,*/
             data->video_queue_1, data->audio_encoder, data->audiorate_controller, data->videorate_controller,
             data->videoPayloader, data->audioPayloader, data->audio_queue_2 , data->video_queue_2,
             data->serverRTPBIN , data->udpsink_rtp0 , data->udpsink_rtp1 , data->udpsink_rtcp0 , data->udpsink_rtcp1 ,
             data->udpsrc_rtcp0 , data->udpsrc_rtcp1, NULL);
      }
      else
      {
        gst_bin_add_many(GST_BIN(data->serverPipeline),data->filesrc, data->demuxer, data->video_queue_1,  data->videorate_controller,
             data->videoPayloader,  data->video_queue_2, data->serverRTPBIN , data->udpsink_rtp0  , data->udpsink_rtcp0 , 
             data->udpsrc_rtcp0 , NULL);
      }

      if(!gst_element_link_many(data->filesrc, data->demuxer, NULL))
      {
        gst_object_unref(data->serverPipeline);
        return NULL;
      }
 /*     if(!gst_element_link_many(data->video_queue_1, data->video_decoder, NULL))
      {
        gst_object_unref(data->serverPipeline);
        return NULL;
      }*/
      if(!gst_element_link_filtered(data->video_queue_1, data->videorate_controller, data->video_caps))
      {
        g_printerr("video_queue, and videorate cannot be linked!.\n");
        gst_object_unref (data->serverPipeline);
        exit(1);
      }
      if(!gst_element_link_many(data->videorate_controller, data->videoPayloader, data->video_queue_2, NULL))
      {
        g_printerr("videorate, video_encoder, and payloader cannot be linked!.\n");
        gst_object_unref (data->serverPipeline);
        exit(1);
      }
      
      if(data->mode == ACTIVE_MODE)
      {
        if(!gst_element_link_filtered(data->audio_queue_1, data->audiorate_controller, data->audio_caps))
        {
          g_printerr("audio decoder and audio rate cannot be linked!.\n");
          gst_object_unref (data->serverPipeline);
          exit(1);
        }
       
        if(!gst_element_link_many(data->audiorate_controller, data->audio_encoder, data->audioPayloader, data->audio_queue_2, NULL))
        {
          g_printerr("audiorate, audio_encoder, and audioPayloader cannot be linked!.\n");
          gst_object_unref (data->serverPipeline);
          exit(1);
        }
      }
      g_signal_connect (data->serverRTPBIN, "pad-added", G_CALLBACK (rtpbin_pad_added_handler), data);


      send_rtp_sink_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->serverRTPBIN), "send_rtp_sink_%d");
      send_rtp_sink0 = gst_element_request_pad (data->serverRTPBIN, send_rtp_sink_temp, NULL, NULL);
      g_print ("Obtained request pad %s for send_rtp_sink !.\n", gst_pad_get_name (send_rtp_sink0));
      video_queue2_srcPad = gst_element_get_static_pad(data->video_queue_2, "src");
      if(data->mode == ACTIVE_MODE)
      {
        send_rtp_sink1 = gst_element_request_pad (data->serverRTPBIN, send_rtp_sink_temp, NULL, NULL);
        g_print ("Obtained request pad %s for send_rtp_sink !.\n", gst_pad_get_name (send_rtp_sink1));
        audio_queue2_srcPad = gst_element_get_static_pad(data->audio_queue_2, "src");
      }


      send_rtcp_src_temp = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->serverRTPBIN), "send_rtcp_src_%d");
      send_rtcp_src0 = gst_element_request_pad(data->serverRTPBIN, send_rtcp_src_temp, NULL, NULL);
      g_print ("Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src0));
      udpsink_rtcp0_sink = gst_element_get_static_pad(data->udpsink_rtcp0, "sink");
      if(data->mode == ACTIVE_MODE)
      {
        send_rtcp_src1 = gst_element_request_pad(data->serverRTPBIN, send_rtcp_src_temp, NULL, NULL);
        g_print ("Obtained request pad %s for send_rtcp_src !.\n", gst_pad_get_name (send_rtcp_src1));
        udpsink_rtcp1_sink = gst_element_get_static_pad(data->udpsink_rtcp1, "sink");
      }


      recv_rtcp_sink_temp = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(data->serverRTPBIN), "recv_rtcp_sink_%d");
      recv_rtcp_sink0 = gst_element_request_pad(data->serverRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
      g_print ("Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink0));
      udpsrc_rtcp0_src = gst_element_get_static_pad(data->udpsrc_rtcp0, "src");
      if(data->mode == ACTIVE_MODE)
      {
         recv_rtcp_sink1 = gst_element_request_pad(data->serverRTPBIN, recv_rtcp_sink_temp ,NULL, NULL);
         g_print ("Obtained request pad %s for recv_rtcp_sink !.\n", gst_pad_get_name (recv_rtcp_sink1));
         udpsrc_rtcp1_src = gst_element_get_static_pad(data->udpsrc_rtcp1, "src");
      }

      if( gst_pad_link (video_queue2_srcPad, send_rtp_sink0) != GST_PAD_LINK_OK ||
          gst_pad_link (send_rtcp_src0, udpsink_rtcp0_sink )!= GST_PAD_LINK_OK ||
          gst_pad_link (udpsrc_rtcp0_src, recv_rtcp_sink0 )!= GST_PAD_LINK_OK )
      {
        g_printerr ("Some requested pads cannot be linked with static pads!\n");
        gst_object_unref (data->serverPipeline);
        return NULL;
      }

      if ( data->mode == ACTIVE_MODE && ( gst_pad_link (audio_queue2_srcPad, send_rtp_sink1) != GST_PAD_LINK_OK ||
                                          gst_pad_link (send_rtcp_src1, udpsink_rtcp1_sink )!= GST_PAD_LINK_OK ||
                                          gst_pad_link (udpsrc_rtcp1_src, recv_rtcp_sink1 )!= GST_PAD_LINK_OK ) )
      {
        g_printerr ("In active mode, Some audio requested pads cannot be linked with static pads!\n");
        gst_object_unref (data->serverPipeline);
        return NULL;
      }

      gst_object_unref (video_queue2_srcPad);
      gst_object_unref (udpsink_rtcp0_sink);
      gst_object_unref (udpsrc_rtcp0_src);
      if(data->mode == ACTIVE_MODE)
      {
        gst_object_unref (audio_queue2_srcPad);
        gst_object_unref (udpsink_rtcp1_sink);
        gst_object_unref (udpsrc_rtcp1_src);
      }
      
      g_signal_connect (data->demuxer, "pad-added", G_CALLBACK (demuxer_pad_added_handler), data);

      /* Listen to the bus */
      bus = gst_element_get_bus (data->serverPipeline);
      gst_bus_add_watch (bus, (GstBusFunc)handle_message, data);
      g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
      gst_object_unref(bus);

      ret=gst_element_set_state(data->serverPipeline, GST_STATE_PLAYING);

       /* Create a GLib Main Loop and set it to run */
      data->main_loop = g_main_loop_new (NULL, FALSE);
      g_main_loop_run (data->main_loop);

      string src  = "src";
      print_pad_capabilities(data->videoPayloader, (gchar*)src.c_str());
      print_pad_capabilities(data->audioPayloader, (gchar*)src.c_str());

      /* Free resources */
      gst_element_release_request_pad (data->serverRTPBIN, send_rtp_sink0);
      gst_element_release_request_pad (data->serverRTPBIN, send_rtcp_src0);
      gst_element_release_request_pad (data->serverRTPBIN, recv_rtcp_sink0);
      gst_object_unref (send_rtp_sink0);
      gst_object_unref (send_rtcp_src0);
      gst_object_unref (recv_rtcp_sink0);

      if(data->mode == ACTIVE_MODE)
      {
        gst_element_release_request_pad (data->serverRTPBIN, send_rtp_sink1);
        gst_element_release_request_pad (data->serverRTPBIN, send_rtcp_src1);
        gst_element_release_request_pad (data->serverRTPBIN, recv_rtcp_sink1);
        gst_object_unref (send_rtp_sink1);
        gst_object_unref (send_rtcp_src1);
        gst_object_unref (recv_rtcp_sink1);
      } 

      gst_object_unref (bus);
      gst_element_set_state (data->serverPipeline, GST_STATE_NULL);

      if (data->res_mode ==0){
        ServerTempBW+=LEVEL1BR;
        changeABA("Sresource.txt",ServerTempBW);
      }
      if (data->res_mode ==1){
        ServerTempBW+=LEVEL2BR;
        changeABA("Sresource.txt",ServerTempBW);
      }

      sendMsg2Client(data->clientip, to_string(data->clientport).c_str(), "5 1");

      gst_object_unref (data->serverPipeline);
      clients.erase(data->clientport);
      if(filename) free(filename) ;
      if(FILE_LOCATION) free(FILE_LOCATION) ;
      if(uri)  free(uri);

      return NULL;
}
   
/*
/* This function will be called by the pad-added signal received by the demuxer */
static void demuxer_pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
  GstPad *video_queue_sink_pad = gst_element_get_static_pad (data->video_queue_1 , "sink");
  GstPad *audio_queue_sink_pad = gst_element_get_static_pad (data->audio_queue_1, "sink");

  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;
   
  g_print ("<Demuxer>: Received pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
  if (gst_pad_is_linked(video_queue_sink_pad) ) {
    g_print ("  We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_get_caps (new_pad);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);

  g_print ("  It has type '%s' \n", new_pad_type);

  if (g_str_has_prefix (new_pad_type, "image")) 
  {
    ret = gst_pad_link (new_pad, video_queue_sink_pad);
    if (GST_PAD_LINK_FAILED (ret)) 
    {
      g_print ("<Demuxer>: Type is '%s' but link failed.\n", new_pad_type);
    } 
    else  
    {
      g_print ("<Demuxer>: Link succeeded (type '%s').\n", new_pad_type);
    }
  }
  else if (g_str_has_prefix (new_pad_type, "audio")) 
  {
    ret = gst_pad_link (new_pad, audio_queue_sink_pad);
    if (GST_PAD_LINK_FAILED (ret)) 
    {
      g_print ("<Demuxer>: Type is '%s' but link failed.\n", new_pad_type);
    } 
    else  
    {
      g_print ("<Demuxer>:  Link succeeded (type '%s').\n", new_pad_type);
  
    }
  }
  else
  {
      fprintf(stderr, "Unknown pad type\n");
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);
   
  /* Unreference the sink pad */
  gst_object_unref (video_queue_sink_pad);
  gst_object_unref (audio_queue_sink_pad);

}


static void rtpbin_pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data)
{
      GstPad *udpsink_rtp0_sink = gst_element_get_static_pad (data->udpsink_rtp0, "sink");
      GstPad *udpsink_rtp1_sink = gst_element_get_static_pad (data->udpsink_rtp1, "sink");

      GstPadLinkReturn ret;
      GstCaps *new_pad_caps = NULL;
      GstStructure *new_pad_struct = NULL;
      const gchar *new_pad_type = NULL;
       
      // g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
      if (gst_pad_is_linked(udpsink_rtp0_sink) && gst_pad_is_linked(udpsink_rtp1_sink)) {
        g_print ("  We are already linked. Ignoring.\n");
        goto exit;
      }

      /* Check the new pad's type */
      new_pad_caps = gst_pad_get_caps (new_pad);
      new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
      new_pad_type = gst_structure_get_name (new_pad_struct);

      if(strstr(GST_PAD_NAME (new_pad), "send_rtp_src"))
      {
          g_print ("<rtpbin>: Received new pad '%s' from '%s', ", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
          g_print ("and it has type '%s' \n", new_pad_type);
           if(strstr(GST_PAD_NAME(new_pad), "0"))
           {
                ret = gst_pad_link (new_pad, udpsink_rtp0_sink);
                if (GST_PAD_LINK_FAILED (ret))
                {
                    g_print ("<rtpbin>: New pad is '%s' but link failed.\n", GST_PAD_NAME (new_pad));
                }
                else
                {
                    g_print ("<rtpbin>: Link succeeded (pad '%s'), and it has capacities:\n", GST_PAD_NAME (new_pad) );
                    print_caps (new_pad_caps, "      ");

                }
           }    
            if(strstr(GST_PAD_NAME(new_pad), "1"))
            {
                 ret = gst_pad_link (new_pad, udpsink_rtp1_sink);
                 if (GST_PAD_LINK_FAILED (ret)) 
                 {
                     g_print ("<rtpbin>: New pad is '%s' but link failed.\n", GST_PAD_NAME (new_pad));
                 } 
                 else 
                 {
                     g_print ("<rtpbin>: Link succeeded (pad '%s'), and it has capacities:\n", GST_PAD_NAME (new_pad));
                     print_caps (new_pad_caps, "      ");
                 }
            }      
      }

    exit:
      /* Unreference the new pad's caps, if we got them */
      if (new_pad_caps != NULL)
        gst_caps_unref (new_pad_caps);
       
      /* Unreference the sink pad */
      gst_object_unref (udpsink_rtp0_sink);
      gst_object_unref (udpsink_rtp1_sink);
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