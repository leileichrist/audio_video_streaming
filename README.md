<h2>CS414: Video/ Audio Streaming</h2>

<h3>Introduction</h3>
<p> 
An implemetation of video-on-demand-service that streams video/audio content from one machine to another machine through connected network. The media server is able to stream video.audio to multiple clients concurrently. 
</p> 

<h3>Design and algorithm</h3>
<p>Each client in our video/audio system composed of three threads, the GUI thread, the control communication thread, and video/audio streaming thread. As there is no GUI requirement for the server, the server only has control communication thread and video audio streaming thread. The difference is that the client only need one streaming thread, however, the server may have several streaming threads (each thread takes care of one client). All the communication between client and server utilizes udp protocol</p>

<p> The GUI part is in charge of the user interface and controls the communication thread to let client and server communicate with each other. These communication include reservation, QoS negotiation, and video operation command, such as, Play, fast forward…</p>

<p>The server has a media source file, which recorded in MP1, then it connect to demux element and separate the video and audio to two different thread. And after the videorate and audiorate elements. They all connect to a same RTPBIN element (Real-Time Transport Protocol bin), which allows for multiple RTP sessions that will be synchronized together using RTCP SR packets. The RTP and RTCP pad connect to the server IP and PORT with the rules that, RTP is originated and received on even port numbers and the associated RTCP communication uses the next higher odd port number. On the Client side we use the corresponding RTPBIN element and UDPSINK elements. Then video and audio stream go into two different depay elements for the depayload part. Then go into video sink and audio sink part to play the video remotely.</p>