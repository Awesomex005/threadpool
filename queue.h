#ifndef __Q_H__
#define __Q_H__

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
typedef unsigned char INT8U;

#define OS_CREATE_Q(Queuepath)								\
if (-1 == mkfifo (Queuepath, 0777) && (errno != EEXIST)){	\
	printf("Error creating named pipe %s\n", Queuepath);	\
}

#define OS_OPEN_Q(Queuepath, pfd)							\
{	int fd;													\
	fd = open(Queuepath, O_RDWR);							\
	if(-1 == fd)											\
		printf("Error opening named pipe %s\n",Queuepath);	\
	*pfd = fd;												\
}

#define OS_ADD_TO_Q(pBuf, Size, Queuefd, pErr)				\
	*pErr = write(Queuefd, pBuf, Size)

#define OS_GET_FROM_Q(pBuf, Size, Queuefd, pErr)			\
{															\
	int ReadLen = 0, Left, Len;								\
	Left = Size;											\
	while( Left > 0 )										\
	{														\
		Len = read(Queuefd, (INT8U*)pBuf + ReadLen, Left ); \
		if( Len < 0 )										\
		{													\
			if( errno == EINTR || errno == EAGAIN )			\
			{												\
				continue;									\
			}												\
			else											\
			{												\
				*pErr = -1;									\
				break;										\
			}												\
		}													\
		ReadLen += Len;										\
		Left -= Len;										\
	}														\
	*pErr = ReadLen;										\
}

#endif
