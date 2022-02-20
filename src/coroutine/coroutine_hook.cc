#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include "coroutine_hook.h"
#include "coroutine.h"
#include "../net/fd_event.h"
#include "../net/reactor.h"
#include "../net/timer.h"
#include "../log/log.h"

#define HOOK_SYS_FUNC(name) static name##_fun_ptr_t g_sys_##name##_fun = (name##_fun_ptr_t)dlsym(RTLD_NEXT, #name);


HOOK_SYS_FUNC(accept);
HOOK_SYS_FUNC(read);
HOOK_SYS_FUNC(write);
HOOK_SYS_FUNC(connect);

static int g_hook_enable = false;

extern "C" {

void toEpoll(tinyrpc::FdEvent::ptr fd_event, int events) {
	
	tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine() ;
	if (events & tinyrpc::IOEvent::READ) {
		DebugLog << "fd:[" << fd_event->getFd() << "], register read event to epoll";
		fd_event->setCallBack(tinyrpc::IOEvent::READ, 
			[cur_cor, fd_event]() {
				tinyrpc::Coroutine::Resume(cur_cor);
			}
		);
		fd_event->addListenEvents(tinyrpc::IOEvent::READ);
	}
	if (events & tinyrpc::IOEvent::WRITE) {
		DebugLog << "fd:[" << fd_event->getFd() << "], register write event to epoll";
		fd_event->setCallBack(tinyrpc::IOEvent::WRITE, 
			[cur_cor]() {
				tinyrpc::Coroutine::Resume(cur_cor);
			}
		);
		fd_event->addListenEvents(tinyrpc::IOEvent::WRITE);
	}
	fd_event->updateToReactor();
}

ssize_t read(int fd, void *buf, size_t count) {
	DebugLog << "this is hook read";
  if (!g_hook_enable) {
    DebugLog << "hook disable, call sys func";
    return g_sys_read_fun(fd, buf, count);
  }

	tinyrpc::Reactor* reactor = tinyrpc::Reactor::GetReactor();
	assert(reactor != nullptr);

	tinyrpc::FdEvent::ptr fd_event = std::make_shared<tinyrpc::FdEvent>(reactor, fd);

	// if (fd_event->isNonBlock()) {
		// DebugLog << "user set nonblock, call sys func";
		// return g_sys_read_fun(fd, buf, count);
	// }

	fd_event->setNonBlock();

  ssize_t n = g_sys_read_fun(fd, buf, count);
  if (n > 0) {
    return n;
  } 
	
	toEpoll(fd_event, tinyrpc::IOEvent::READ);

	DebugLog << "read func to yield";
	tinyrpc::Coroutine::Yield();

	fd_event->delListenEvents(tinyrpc::IOEvent::READ);
	fd_event->updateToReactor();

	DebugLog << "read func yield back, now to call sys read";
	return g_sys_read_fun(fd, buf, count);

}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	DebugLog << "this is hook accept";
  if (!g_hook_enable) {
    DebugLog << "hook disable, call sys func";
    return g_sys_accept_fun(sockfd, addr, addrlen);
  }
	tinyrpc::Reactor* reactor = tinyrpc::Reactor::GetReactor();
	assert(reactor != nullptr);

	tinyrpc::FdEvent::ptr fd_event = std::make_shared<tinyrpc::FdEvent>(reactor, sockfd);

	// if (fd_event->isNonBlock()) {
		// DebugLog << "user set nonblock, call sys func";
		// return g_sys_accept_fun(sockfd, addr, addrlen);
	// }

	fd_event->setNonBlock();

  int n = g_sys_accept_fun(sockfd, addr, addrlen);
  if (n > 0) {
    return n;
  } 

	toEpoll(fd_event, tinyrpc::IOEvent::READ);
	
	DebugLog << "accept func to yield";
	tinyrpc::Coroutine::Yield();

	fd_event->delListenEvents(tinyrpc::IOEvent::READ);
	fd_event->updateToReactor();

	DebugLog << "accept func yield back, now to call sys accept";
	return g_sys_accept_fun(sockfd, addr, addrlen);

}

ssize_t write(int fd, const void *buf, size_t count) {
	DebugLog << "this is hook write";
  if (!g_hook_enable) {
    DebugLog << "hook disable, call sys func";
    return g_sys_write_fun(fd, buf, count);
  }
	tinyrpc::Reactor* reactor = tinyrpc::Reactor::GetReactor();
	assert(reactor != nullptr);

	tinyrpc::FdEvent::ptr fd_event = std::make_shared<tinyrpc::FdEvent>(reactor, fd);

	// if (fd_event->isNonBlock()) {
		// DebugLog << "user set nonblock, call sys func";
		// return g_sys_write_fun(fd, buf, count);
	// }

	fd_event->setNonBlock();
	
  ssize_t n = g_sys_write_fun(fd, buf, count);
  if (n > 0) {
    return n;
  } 

	toEpoll(fd_event, tinyrpc::IOEvent::WRITE);

	DebugLog << "write func to yield";
	tinyrpc::Coroutine::Yield();

	fd_event->delListenEvents(tinyrpc::IOEvent::WRITE);
	fd_event->updateToReactor();

	DebugLog << "write func yield back, now to call sys write";
	return g_sys_write_fun(fd, buf, count);

}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	DebugLog << "this is hook connect";
  if (!g_hook_enable) {
    DebugLog << "hook disable, call sys func";
    return g_sys_connect_fun(sockfd, addr, addrlen);
  }
	tinyrpc::Reactor* reactor = tinyrpc::Reactor::GetReactor();
	assert(reactor != nullptr);

	tinyrpc::FdEvent::ptr fd_event = std::make_shared<tinyrpc::FdEvent>(reactor, sockfd);

	// if (fd_event->isNonBlock()) {
		// DebugLog << "user set nonblock, call sys func";
    // return g_sys_connect_fun(sockfd, addr, addrlen);
	// }
	
	fd_event->setNonBlock();
  int n = g_sys_connect_fun(sockfd, addr, addrlen);
  if (n == 0) {

    DebugLog << "direct connect succ, return";
    return n;
  } else if (n == -1 && errno != EINPROGRESS) {
    return n;
  }

  toEpoll(fd_event, tinyrpc::IOEvent::WRITE);

  int max_timeout = 75000;

  auto delWriteListenCb = [fd_event]() {
    fd_event->delListenEvents(tinyrpc::IOEvent::WRITE); 
    fd_event->updateToReactor();
  };

  auto timeout_cb = [delWriteListenCb, max_timeout](){
    ErrorLog << "connect error,  timeout[ " << max_timeout << "ms]";
    delWriteListenCb();
  };

  tinyrpc::TimerEvent::ptr event = std::make_shared<tinyrpc::TimerEvent>(max_timeout, false, timeout_cb);
  
  tinyrpc::Timer* timer = reactor->getTimer();  
  timer->addTimerEvent(event);

  tinyrpc::Coroutine::Yield();
  delWriteListenCb();

	DebugLog << "connect func yield back, now to call sys connect";
	return g_sys_connect_fun(sockfd, addr, addrlen);
}

typedef int (*socket_fun_ptr_t)(int domain, int type, int protocol);

}


namespace tinyrpc {

void enableHook() {
  g_hook_enable = true;
}

void disableHook() {
  g_hook_enable = false;
}

}






