/*
 * TestMain.cpp
 *
 *  Created on: 2017-4-25
 *      Author: root
 */
#include <iostream>
#include <unistd.h>
#include "XEvent.h"
#include "easylogging++.h"
using namespace std;

XEventBase *base = new Epoll(1024);
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

