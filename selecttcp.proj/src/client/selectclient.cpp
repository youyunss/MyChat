#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <resolv.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>

#define MAXBUF 1024

int main(int argc, char **argv)
{
	int sockfd, len;
	struct sockaddr_in dest;
	char buf[MAXBUF + 1];
	fd_set rfds;
	struct timeval tv;
	int retval, maxfd=-1;

	if (argc != 3)
	{
		printf(" error format, it must be:\n\t\t%s IP port\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("Socket");
		exit(errno);
	}
	printf("socket craeted\n");
	bzero(&dest, sizeof(dest));
	dest.sin_family = AF_INET;					
	dest.sin_port = htons(atoi(argv[2]));			
	if (inet_aton(argv[1], (struct in_addr*) &dest.sin_addr.s_addr) == 0)			
	{
		perror(argv[1]);
		exit(errno);
	}
	if (connect(sockfd, (struct sockaddr*)&dest, sizeof(dest)) == -1)
	{
		perror("Connect ");
		exit(errno);
	}
	printf("server connected\nget ready pls chat\n");
	while (1)
	{
		FD_ZERO(&rfds);
		FD_SET(0, &rfds);       //将stdin加入文件描述符集合
		FD_SET(sockfd, &rfds);  //将new_fd加入文件描述符集合
		maxfd=sockfd;
		tv.tv_sec=1;
		tv.tv_usec=0;           //阻塞时间1s
		retval = select(maxfd+1, &rfds, NULL, NULL, &tv);     //多路复用，监测stdin和new_fd
		if (retval == -1)
		{
			perror("select");
			exit(EXIT_FAILURE);
		}
		else if (retval == 0)      //超时，继续执行
		{
			continue;
		}
		else
		{
			if (FD_ISSET(0, &rfds))    //监听到stdin有异常
			{
				bzero(buf, MAXBUF + 1);
				printf("input the message to send:");
				fgets(buf, MAXBUF, stdin);
				if (!strncasecmp(buf, "quit", 4))
				{
					printf("i will close the connect!\n");
					break;
				}
				len = send(sockfd, buf, strlen(buf) - 1, 0);
				if (len <= 0)
				{
					printf("message'%s' send failure! errno code is %d, erron message is '%s'\n", buf, errno, strerror(errno));
					break;
				}
				else
				{
					printf("send message successful, %d bytes send!\n", len);
				}
			}
			if (FD_ISSET(sockfd, &rfds))     //监听到sockfd有异常
			{
				bzero(buf, MAXBUF + 1);
				len = recv(sockfd, buf, MAXBUF, 0);
				if (len > 0)
				{
					printf("message recv successful :'%s', %dByte recv\n", buf, len);
				}
				else if (len < 0)
				{
					printf("recv failure! errno code is %d, errno message is '%s'\n", errno, strerror(errno));
					break;
				}
				else
				{
					printf("the other one close quit\n");
					break;
				}
			}
		}
	}
	close(sockfd);
	return 0;
}
