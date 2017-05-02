/*
 * XEvent.h
 *
 *  Created on: 2017-5-1
 *      Author: root
 */

#ifndef SOURCE_XEVENT_H_
#define SOURCE_XEVENT_H_
#include <functional>
#include <map>
#include <list>
#include <queue>
#include <sys/epoll.h>
#include <errno.h>
#include <exception>
#include "easylogging++.h"
#include <sys/types.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <fcntl.h>
#include <signal.h>
INITIALIZE_EASYLOGGINGPP
class NonCopyable
{
public:
	NonCopyable()
	{
	}
	virtual ~NonCopyable()
	{
	}
private:
	NonCopyable(const NonCopyable &other) = delete;
	NonCopyable &operator=(const NonCopyable &other) = delete;
};
class EventBase;
using namespace std::chrono;
class XEvent
{
public:
	enum XEVS
	{
		NONEV = 0, READ = 1, WRITE = 2, SIG = 4, TIMER = 8, ERROR = 16
	};

	XEvent(XEVS events = NONEV, int fd = -1, int priority = 0, void *priv = 0) :
			events_(events), revents_(NONEV), fd_(fd),

			priority_(priority), priv_(priv)
	{
	}
	void set_cb(std::function<void()> &cb)
	{
		cb_ = cb;
	}
	virtual ~XEvent()
	{
	}
	XEVS events_;
	XEVS revents_;
	int fd_;
	std::function<void()> cb_;
	int priority_;
	void *priv_;

};
class TimerEvent: public XEvent
{
public:
	TimerEvent(milliseconds us) :
			XEvent(TIMER), us_(us)
	{
	}
	milliseconds us_;
	time_point<high_resolution_clock> tm_;
};

class SigEvent: public XEvent
{
public:
	SigEvent(int sig):XEvent(SIG, sig)
	{
	}
};

class timer_cmp
{
public:
	bool operator()(const TimerEvent *a, const TimerEvent *b)
	{
		return a->tm_ > b->tm_;
	}
};
class xevent_cmp_pri
{
public:
	bool operator()(const XEvent *a, const XEvent *b) const
	{
		return a->priority_ < b->priority_;
	}
};
class XEventBase: public NonCopyable
{
public:
	XEventBase() :
			stop_(false)
	{
	}
	virtual ~XEventBase()
	{
	}

	void stop()
	{
		stop_ = true;
	}
	void add(XEvent *event)
	{
		ev_add(event);
	}
	template<typename T, typename ...Args>
	void add(XEvent *event, T func, Args&& ...args)
	{
		event->cb_ = std::bind(func, event, std::forward<Args>(args)...);
		add(event);
	}
	template<typename T, typename U, typename ...Args>
	void add(XEvent *event, T U::*func, U u, Args ...args)
	{
		event->cb_ = std::bind(func, u, event, std::forward<Args>(args)...);
		ev_add(event);
	}

	void add_active(XEvent *event)
	{
		actives.push(event);
	}
	template<typename T, typename ...Args>
	void add_active(XEvent *event, T func, Args&& ...args)
	{
		event->cb_ = std::bind(func, event, std::forward<Args>(args)...);
		add(event);
	}
	template<typename T, typename U, typename ...Args>
	void add_active(XEvent *event, T U::*func, U u, Args ...args)
	{
		event->cb_ = std::bind(func, u, event, std::forward<Args>(args)...);
		ev_add(event);
	}
	void run_actives()
	{
		while (!actives.empty()) {
			XEvent *xev = actives.top();
			actives.pop();
			(xev->cb_)();
		}
	}

	void loop()
	{
		milliseconds us(2000);
		milliseconds us2(0);
		LOG(INFO)<<"Event loop started!";
		while (!stop_) {
			run_actives();
			if (!timers.empty()) {
				TimerEvent *te = timers.top();
				us2 = milliseconds(duration_cast<milliseconds>(te->tm_ - high_resolution_clock::now()).count());
			}
			else
			us2 = us;
			if(us2.count() < 0)
			us2 = milliseconds(0);
			ev_wait(us2);
			do_timer();
		}
		LOG(INFO)<<"Event loop stopped!";
	}
	void mod(int fd, XEvent::XEVS evts)
	{
		ev_mod(fd, evts);
	}
	void del(int fd)
	{
		ev_del(fd);
	}
	void add_timer(TimerEvent *te)
	{
		te->tm_ = high_resolution_clock::now() + te->us_;
		timers.push(te);
	}
	template<typename T, typename ...Args>
	void add_timer(TimerEvent *event, T func, Args&& ...args)
	{
		event->events_ = XEvent::TIMER;
		event->cb_ = std::bind(func, event, std::forward<Args>(args)...);
		add_timer(event);
	}
	template<typename T, typename U, typename ...Args>
	void add_timer(TimerEvent *event, T U::*func, U u, Args ...args)
	{
		event->events_ = XEvent::TIMER;
		event->cb_ = std::bind(func, u, event, std::forward<Args>(args)...);
		add_timer(event);
	}
protected:
	void do_event(XEvent *event)
	{
		(event->cb_)();
	}
	void do_timer()
	{
		while (!timers.empty()) {
			TimerEvent *te = timers.top();
			milliseconds ms = milliseconds(duration_cast<milliseconds>(te->tm_ - high_resolution_clock::now()).count());
			if (ms.count()<=0) {
				timers.pop();
				te->revents_ = XEvent::TIMER;
				(te->cb_)();
			} else {
				std::cout<<"Break:"<<ms.count()<<std::endl;
				break;
			}
		}
	}

private:
	virtual void ev_add(XEvent *event) = 0;
	virtual void ev_del(int fd) = 0;
	virtual void ev_mod(int fd, XEvent::XEVS evts) = 0;

	virtual void ev_wait(const milliseconds &us) = 0;
private:
	std::priority_queue<XEvent *, std::vector<XEvent*>, xevent_cmp_pri> actives;
	std::priority_queue<TimerEvent, std::vector<TimerEvent*>, timer_cmp> timers;
	std::map<int, SigEvent *> sig_mp_;
	volatile bool stop_;
};

class Epoll: public XEventBase
{
public:
	Epoll(size_t size) :
			XEventBase(), size_(size), poll_fd_(0), evts_(new epoll_event[size])
	{
		if ((poll_fd_ = epoll_create(size)) == -1) {
			LOG(ERROR)<<strerror(errno);
			throw std::runtime_error("epoll_create failed.");
		}
	}
	~Epoll()
	{
		if(poll_fd_ != -1)
		close(poll_fd_);
	}

	virtual void ev_add(XEvent *event) override
	{
		if(ev_mp_.find(event->fd_) != ev_mp_.end()) {
			LOG(ERROR)<<"Readd event.";
			throw std::logic_error("Readd event");
		}

		epoll_event ev = {0};
		ev_parse(&ev, event->events_);
		if(event->events_ == 0) {
			LOG(ERROR)<<"add event shouldn't be zero(nonevent)";
			throw std::logic_error("Add nonevent.");
		}
		//加入epoll
		ev.data.fd = event->fd_;
		int ret = 0;
		ret = ::epoll_ctl(poll_fd_, EPOLL_CTL_ADD, event->fd_, &ev);
		if(ret == -1) {
			LOG(WARNING)<<"epoll ctl failed:"<<strerror(errno);
			throw std::runtime_error("epoll ctl failed.");
		} else {
			ev_mp_[event->fd_] = event;
		}
	}
	virtual void ev_mod(int fd, XEvent::XEVS evts) override
	{

		if(evts == 0) {
			LOG(ERROR)<<"mod event shouldn't be zero(nonevent)";
			throw std::logic_error("Add nonevent.");
		}
		if(ev_mp_.find(fd) == ev_mp_.end()) {
			LOG(ERROR)<<"mod unexists event,should call ev_add";
			throw std::logic_error("mod unexists event");
		}

		//加入epoll
		ev_mp_[fd]->events_ = evts;
		epoll_event ev = {0};
		ev_parse(&ev, evts);
		int ret = 0;
		ret = ::epoll_ctl(poll_fd_, EPOLL_CTL_MOD, fd, &ev);

		if(ret == -1) {
			LOG(WARNING)<<"epoll ctl failed:"<<strerror(errno);
			throw std::runtime_error("epoll ctl failed.");
		}
	}
	virtual void ev_del(int fd) override
	{
		if(ev_mp_.find(fd) == ev_mp_.end()) {
			LOG(ERROR)<<"remove event failed, not exits.";
			throw std::logic_error("remove unexits events");
		}
		ev_mp_.erase(fd);
		int ret = ::epoll_ctl(poll_fd_, EPOLL_CTL_DEL, fd, NULL);
		if(ret == -1) {
			LOG(ERROR)<<"epoll ctl del failed:"<<strerror(errno);
			throw std::runtime_error("epoll ctl del failed.");
		}
	}
	virtual void ev_wait(const milliseconds &us)
	{

		std::cout<<"MS:"<<us.count()<<std::endl;
		int ret = ::epoll_wait(poll_fd_, evts_, size_, us.count());
		if(ret == -1) {
			LOG(WARNING)<<"epoll ctl wait failed:"<<strerror(errno);
			throw std::runtime_error("epoll ctl wait failed.");
		} else if(ret == 0) {
			std::cout<<"timed out:"<<us.count()<<std::endl;

		} else {
			for(int i = 0; i < ret; i++) {
				auto ev = ev_mp_.find(evts_[i].data.fd);
				if(ev == ev_mp_.end()) {
					LOG(ERROR)<<"find event in ev_map_ failed, not exits.";
					throw std::logic_error("find event in ev_map_ failed, not exits.");
				}
				ev_parse(ev->second->revents_, evts_[i]);
				do_event(ev->second);
			}
		}
	}

private:
	void ev_parse(epoll_event *ev, XEvent::XEVS xev)
	{
		bool flag = false;
		//读
		if(xev & XEvent::READ) {
			ev->events |= EPOLLIN;
			flag = true;
		}

		//写
		if(xev & XEvent::WRITE) {
			ev->events |= EPOLLOUT;
			flag= true;
		}

		//不支持事件
		if(!flag) {
			LOG(ERROR)<<"add unsupported event:"<<xev;
			throw std::runtime_error("add unsupported event.");
		}

	}
	void ev_parse(XEvent::XEVS &xev, epoll_event &ev)
	{
		bool flag = false;
		//读
		if(ev.events & EPOLLIN) {
			xev = static_cast<XEvent::XEVS>(xev|XEvent::READ);
			flag = true;
		}

		//写
		if(ev.events & EPOLLOUT) {
			xev = static_cast<XEvent::XEVS>(xev|XEvent::WRITE);
			flag= true;
		}

		//其他事件都是错误
		if(!flag) {
			xev = static_cast<XEvent::XEVS>(xev|XEvent::ERROR);
		}

	}
private:
	size_t size_;
	int poll_fd_;
	epoll_event *evts_;
	std::map<int, XEvent *> ev_mp_;
};

#endif /* SOURCE_XEVENT_H_ */
