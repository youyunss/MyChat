#include <iostream>
#include <cstring>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <resolv.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>

#define MAXBUF 6*1024*1024
#define MAXFDS 256

using namespace std;

class server
{
	public:
		server(string ip = "9.134.44.225", unsigned int port = 7575, unsigned int lisnum = 5)
		{
			ip_=ip;
			port_=port;
			lisnum_=lisnum;
		};
		~server(){};
		void run_server(void);

	private:
		string ip_;
		unsigned int port_, lisnum_;
		int sockfd_, epollfd_, new_fd_, now_fd_, all_fd_[MAXFDS]; 
		char buf_[MAXBUF + 1];
		struct sockaddr_in my_addr_, their_addr_[MAXFDS];
		socklen_t my_size_, their_size_, datalen_;
		int client_num_ = 0, now_num_ = 0, quit_num_ = 0;
		int ndfs_ = -1;
		int quit_flag_[MAXBUF] = {0};
		struct epoll_event ev_[MAXFDS+1], evs_, events_[MAXFDS];
		void server_connect(void);
		void server_accept(void);
		void server_msgsend(void);
		void server_msgrecv(void);
};

int main(int argc, char** argv)
{
	if (argc == 1)
	{
		server server1;
		server1.run_server();
	}
	else if (argc == 2)
	{
		server server1(argv[1]);
		server1.run_server();
	}
	else if (argc == 3)
	{
		server server1(argv[1], atoi(argv[2]));
		server1.run_server();
	}	
	else
	{
		server server1(argv[1], atoi(argv[2]), atoi(argv[3]));
		server1.run_server();
	}
	return 0;
}

void server::run_server(void)
{
	server_connect();
	epollfd_ = epoll_create(MAXFDS); //epoll实例句柄
	evs_.data.fd = sockfd_;
	evs_.events = EPOLLIN | EPOLLET; //边沿触发
	epoll_ctl(epollfd_, EPOLL_CTL_ADD, sockfd_, &evs_);   //将sockfd加入epoll句柄
	ev_[0].data.fd = 0;
	ev_[0].events = EPOLLIN | EPOLLET; //边沿触发
	epoll_ctl(epollfd_, EPOLL_CTL_ADD, 0, &ev_[0]);   //将键盘输入加入epoll句柄
	while (true)
	{
		ndfs_ = epoll_wait(epollfd_, events_, MAXFDS, -1);   //监听事件,判断是否是新的client连接/接收数据/键盘输入数据，永久阻塞
		if (ndfs_ == -1)
		{
			if (errno != EINTR && errno != EAGAIN)
			{
				perror("epoll");
				exit(EXIT_FAILURE);
			}
		}
		else if (ndfs_ == 0)
		{
			continue;
		}
		else
		{
			for (int i = 0; i < ndfs_; i++)
			{
				if (events_[i].data.fd == sockfd_)
				{
					server_accept();    //创建新的连接和文件描述符，并加入文件描述符数组中
					ev_[client_num_].data.fd = new_fd_;
					ev_[client_num_].events = EPOLLIN | EPOLLET; //边沿触发
					epoll_ctl(epollfd_, EPOLL_CTL_ADD, new_fd_, &ev_[client_num_]);   //将new_fd加入epoll句柄
				}
				else
				{
					if (events_[i].data.fd == 0)     //监听到键盘输入并给现在的连接发送消息
					{
						if (!quit_flag_[now_num_] && now_num_ != 0)     //防止向已经关断的fd发数据
						{
							server_msgsend();
						}
					}
					for (int j = 1; j <= client_num_; j++)
					{
						if (events_[i].data.fd == all_fd_[j])//监听到第j个fd有数据发来
						{
							now_num_=j;     //定位目前通信的连接
							now_fd_ = all_fd_[j];    //定位目前通信的连接
							server_msgrecv();
						}
					}
				}
				if (quit_flag_[now_num_])
				{
					their_size_ = sizeof(their_addr_);
					close(now_fd_);
					quit_num_++;   //退出总数计算
					if (quit_num_ == client_num_)
					{
						break;
					}
					now_num_ = 0;    //防止now_num_还指向已经关断的fd
				}
			}
		}
		if (quit_num_ == client_num_)
		{
			cout<<"need new connect (no->quit/other -> wait for new connect):"<<endl;
			fflush(stdin);       //刷新输入
			bzero(buf_, MAXBUF + 1);
			quit_num_ = 0;
			fgets(buf_, MAXBUF, stdin);
			if (!strncasecmp(buf_, "no", 2))
			{
				printf("quit server!\n");
				break;
			}
		}
	}	
	close(epollfd_);
	close(sockfd_);
}

void server::server_connect(void)
{
	cout<<"start create sockfd and wait for connect"<<endl;
	if ((sockfd_ = socket(AF_INET, SOCK_STREAM, 0)) == -1)  //创建sockfd，采用ipv4协议
	{
		perror("socket");
		exit(EXIT_FAILURE);
	}
	bzero(&my_addr_, sizeof(my_addr_));
	my_addr_.sin_family = AF_INET;      //采用ipv4协议
	my_addr_.sin_port = htons(port_);  //转换为网络顺序（大端）
	cout<<"socket created"<<endl;
	bzero(buf_, MAXBUF + 1);
	//组合my_addr
	if (inet_aton(ip_.c_str(), (struct in_addr*) &my_addr_.sin_addr.s_addr) == 0)			
	{
		perror(ip_.c_str());
		exit(errno);
	}
	//bind
	if (bind(sockfd_, (struct sockaddr *) &my_addr_, sizeof(struct sockaddr)) == -1)
	{
		perror("bind");
		exit(EXIT_FAILURE);
	}
	//listen
	if (listen(sockfd_, lisnum_) == -1)
	{
		perror("listen");
		exit(EXIT_FAILURE);
	}
	cout<<"wait for connect"<<endl;
}

void server::server_accept(void)
{
	cout<<"\n-----wait for new connect\n"<<endl;
	socklen_t len = sizeof(struct sockaddr);
	new_fd_ = accept(sockfd_, (struct sockaddr*)&their_addr_, &len);
	if (new_fd_ == -1)
	{
		perror("accept");
		exit(EXIT_FAILURE);
	}
	else
	{
		now_fd_ = new_fd_;
		client_num_++;   
		all_fd_[client_num_] = new_fd_;        //将新的连接对应的文件描述符加入描述符数组中（注：这里用链表代替数组会更好，可动态扩张和删除）
		now_num_ = client_num_;
		their_size_ = sizeof(struct sockaddr_in);
		bzero(&their_addr_, MAXFDS*their_size_);
		getpeername(now_fd_, (struct sockaddr*)&their_addr_[now_num_], &their_size_);
		printf("server: got connection from %s, port %d, socket %d\n", inet_ntoa(their_addr_[now_num_].sin_addr), ntohs(their_addr_[now_num_].sin_port), now_fd_);
	}
}

void server::server_msgsend(void)
{
	bzero(buf_, MAXBUF + 1);
	fgets(buf_, MAXBUF, stdin);
	if (!strncasecmp(buf_, "quit", 4))
	{
		quit_flag_[now_num_] = 1;
		cout<<"I will close the connect!"<<endl;
		epoll_ctl(epollfd_, EPOLL_CTL_DEL, all_fd_[now_num_], &ev_[now_num_]);
	}
	else
	{
		datalen_ = send(now_fd_, buf_, strlen(buf_) - 1, 0);
		if (datalen_ > 0)
		{
			cout<<"my_addr ip "<<inet_ntoa(my_addr_.sin_addr)<<" port "<<ntohs(my_addr_.sin_port)<<" send message successful: /*"<<buf_<<" */ to their_addr ip "<<inet_ntoa(their_addr_[now_num_].sin_addr)<<" port "<<ntohs(their_addr_[now_num_].sin_port)<<endl;
		}
		else
		{
			if (errno != EINTR && errno != EAGAIN)
			{
				quit_flag_[now_num_] = 1;
				printf("send message failure! errno code is %d, errno message is '%s'\n", errno, strerror(errno));
				epoll_ctl(epollfd_, EPOLL_CTL_DEL, all_fd_[now_num_], &ev_[now_num_]);
			}
		}
	}
}

void server::server_msgrecv(void)
{
	bzero(buf_, MAXBUF + 1);
	datalen_ = recv(now_fd_, buf_, MAXBUF, 0);
	if (datalen_ > 0)
	{
		cout<<"my_addr ip "<<inet_ntoa(my_addr_.sin_addr)<<" port "<<ntohs(my_addr_.sin_port)<<" recv successful: /* "<<buf_<<" */ from their_addr ip "<<inet_ntoa(their_addr_[now_num_].sin_addr)<<" port "<<ntohs(their_addr_[now_num_].sin_port)<<endl;
	}
	else if (datalen_ < 0)
	{
		if (errno != EINTR && errno != EAGAIN)
		{
			quit_flag_[now_num_] = 1;
			printf("message'%s' send failure! errno code is %d, erron message is '%s'\n", buf_, errno, strerror(errno));
			epoll_ctl(epollfd_, EPOLL_CTL_DEL, all_fd_[now_num_], &ev_[now_num_]);
		}
	}
	else
	{	
		quit_flag_[now_num_] = 1;
		cout<<"the other one port close, i will quit"<<endl;
		epoll_ctl(epollfd_, EPOLL_CTL_DEL, all_fd_[now_num_], &ev_[now_num_]);
	}
}
