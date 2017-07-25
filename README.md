# libhyve-remote #

The libhyve-remote aims to abstract functionalities from other third part libraries such like libvncserver, freerdp and spice. With a basic data structure it is easy to implement any remote desktop protocol without dig into the protocol specification or third part libraries, you can check some of our examples.

We don't static link any third part library, instead we use dynamic linker and we load only functionalities that we believe is necessary to launch the service.

Our target is to abstract functionalities from libvncserver, freerdp and spice, right now libhyve-remote supports libvncserver, it is possible to launch a vnc server with different screen resolution as well as with authentication.

Also due some software license restrictions, this library is implemented using dual license GPL version 2 and BSD 2-Clauses license.

### How do I get set up? ###

* make; make install 

It will create and install libhyverem at /usr/lib/

### How do I use libhyve-remote? ###
* cd examples

### What libhyverem provides now? ###
Basically 3 main functions:
* vnc_enable_http(): That enables libvncserver to run via HTTP.
* vnc_init_server(): That prepares the vnc server to be started.
* vnc_event_loop(): Runs the vnc server.
