module main

#flag -I @VMODROOT/lthread/src
#flag @VMODROOT/lthread/src/lthread.o
#flag @VMODROOT/lthread/src/lthread_socket.o
#flag @VMODROOT/lthread/src/lthread_sched.o
#flag @VMODROOT/lthread/src/lthread_io.o
#flag @VMODROOT/lthread/src/lthread_poller.o
#flag @VMODROOT/lthread/src/lthread_compute.o
#flag @VMODROOT/lthread/src/libcontext.o
// #flag @VMODROOT/lthread/src/lthread_epoll.o
// #flag @VMODROOT/lthread/src/lthread_kqueue.o

fn main() {
	println('Hello World!')
}
