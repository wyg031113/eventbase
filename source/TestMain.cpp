/*
 * TestMain.cpp
 *
 *  Created on: 2017-4-25
 *      Author: root
 */
#include <iostream>
#include <event.h>
#include <unistd.h>
#include "XEvent.h"
#include "easylogging++.h"
using namespace std;
struct event ev;
struct event ev2;
struct event ev3;
struct timeval tv;

XEventBase *base = new Epoll(1024);
void time_cb(int fd, short event, void *arg)
{
	printf("timer wakeup\n");
	event_add(&ev, &tv);
}
void read_cb(int fd, short event, void *arg)
{
	cout << "read cb" << endl;
	char buf[256];
	int n = read(fd, buf, 256);
	if (n > 0) {
		write(1, buf, n);
		event_add(&ev2, NULL);
	}
}
void read_cb2(int fd, short event, void *arg)
{
	cout << "read cb2" << endl;
	char buf[256];
	int n = read(fd, buf, 256);
	if (n > 0) {
		write(1, buf, n);
		event_add(&ev3, NULL);
	}
}

void mycb(XEvent *ev, int a)
{
	cout << "a=" << a << endl;
	static char buf[256];
	static int n;
	if (ev->revents_ & XEvent::READ) {
		n = read(ev->fd_, buf, 256);
		XEvent *xev = new XEvent(XEvent::WRITE, 1);
		xev->cb_ = std::bind(mycb, xev, 4);
		base->add(xev);
	}
	if (ev->revents_ & XEvent::WRITE) {
		if (n > 0) {
			write(1, buf, n);
		}
		base->del(ev->fd_);
		delete ev;
	}
}
void test()
{
	cout << "Hello World." << endl;
	struct event_base *base = event_init();
	tv.tv_sec = 6;
	tv.tv_usec = 0;
	evtimer_set(&ev, time_cb, NULL);

	event_set(&ev2, 0, EV_READ, read_cb, NULL);
	event_add(&ev, &tv);
	event_add(&ev2, NULL);
	event_set(&ev3, 0, EV_READ, read_cb2, NULL);
	cout << event_add(&ev, NULL) << endl;
	event_base_dispatch(base);
}

void hello_cb(int a)
{
	cout << "Hello world:" << a << endl;
}
void hello_cb2(TimerEvent *te, int a)
{
	cout << "Hello world cb2:" << a << endl;
	base->add_timer(te);
}
static int x = 0;
class A
{
public:
	void func(XEvent *xev, int a)
	{
		cout << "In a:"<< endl;
		base->del(xev->fd_);
		delete xev;
	}
	void func2(TimerEvent *xev, int a)
	{
		cout << "In a:" <<xev->us_.count()<< endl;
		base->add_timer(xev);

		x++;
		if(x>=100)
			base->stop();
		std::cout<<"X="<<x<<std::endl;
	}
};
A a;
int main(int argc, char *argv[])
{

	//配置日志
	el::Configurations conf("conf/log.conf");
	el::Loggers::reconfigureAllLoggers(conf);

    //base

	//使用方法1 添加普通函数
	base->add(new XEvent(XEvent::READ, 0), mycb, 6);

	//使用方法2 bind类成员函数和参数
	//base->add(new XEvent(XEvent::READ, 0), &A::func, a, 234);



	//定时函数
	base->add_timer(new TimerEvent(milliseconds(8000)), &A::func2, a, 333);
	base->add_timer(new TimerEvent(milliseconds(5000)), hello_cb2, 444);
	base->add_timer(new TimerEvent(milliseconds(3000)), &A::func2, a, 333);
	base->add_timer(new TimerEvent(milliseconds(2000)), &A::func2, a, 333);


	//开始事件循环
	base->loop();

	return 0;
}

