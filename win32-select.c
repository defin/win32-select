#include <assert.h>
#include <fcntl.h>
#include <io.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define W32_FD_SETSIZE 1024
#define W32_NFDBITS (sizeof(unsigned long) * 8)

#define w32_fdset_mask(n) ((unsigned long)1 << ((n) % W32_NFDBITS))
#define w32_howmany(x, y) (((x) + ((y) - 1)) / (y))

typedef struct w32_fd_set {
    unsigned long __fds_bits[w32_howmany(W32_FD_SETSIZE, W32_NFDBITS)];
} w32_fd_set;

#define W32_FD_CLR(n, p) ((p)->__fds_bits[(n) / W32_NFDBITS] &= ~w32_fdset_mask(n))
#define W32_FD_COPY(f, t) (void)(*(t) = *(f))
#define W32_FD_ISSET(n, p) ((p)->__fds_bits[(n) / W32_NFDBITS] & w32_fdset_mask(n))
#define W32_FD_SET(n, p) ((p)->__fds_bits[(n) / W32_NFDBITS] |= w32_fdset_mask(n))
#define W32_FD_ZERO(p) do {                             \
        w32_fd_set *_p;                                 \
        size_t _n;                                      \
        _p = (p);                                       \
        _n = w32_howmany(W32_FD_SETSIZE, W32_NFDBITS);  \
        while (_n > 0)                                  \
                _p->__fds_bits[--_n] = 0;               \
} while (0)

int w32_select(int, w32_fd_set *, w32_fd_set *, w32_fd_set *, const struct timeval *);

int
w32_select(int nfds,
	   w32_fd_set * readfds,
	   w32_fd_set * writefds,
	   w32_fd_set * exceptfds,
           const struct timeval * tv)
{
  DWORD dwError = 0xDEADBEEF;
  DWORD dwMilliseconds = 0xDEADBEEF;
  DWORD dwType = 0xDEADBEEF;

  HANDLE hMultiple[MAXIMUM_WAIT_OBJECTS];
  DWORD countMultiple = 0;

  HANDLE hRead[MAXIMUM_WAIT_OBJECTS];
  HANDLE hWrite[MAXIMUM_WAIT_OBJECTS];
  HANDLE hExcept[MAXIMUM_WAIT_OBJECTS];
  HANDLE hTemp;

  int i = 0;
  int retval = 0;

  assert(nfds < MAXIMUM_WAIT_OBJECTS);

  if (tv == NULL)
    dwMilliseconds = INFINITE;
  else {
    dwMilliseconds = 0;
    dwMilliseconds += tv->tv_sec * 1000;
    dwMilliseconds += tv->tv_usec / 100;
  }

  /* map readfds */
  if (readfds != NULL) {
    for (i = 0; i != MAXIMUM_WAIT_OBJECTS; i++) {
      if (W32_FD_ISSET(i, readfds)) {
	hTemp = (HANDLE) _get_osfhandle(i);
	hMultiple[countMultiple++] = hTemp;
	hRead[i] = hTemp;
      }
      else {
	hRead[i] = NULL;
      }
    }
  }

  /* map writefds */
  if (writefds != NULL) {
    for (i = 0; i != MAXIMUM_WAIT_OBJECTS; i++) {
      if (W32_FD_ISSET(i, writefds)) {
	hTemp = (HANDLE) _get_osfhandle(i);
	hMultiple[countMultiple++] = hTemp;
	hWrite[i] = hTemp;
      }
      else {
	hWrite[i] = NULL;
      }
    }
  }

  /* map exceptfds */
  if (exceptfds != NULL) {
    for (i = 0; i != MAXIMUM_WAIT_OBJECTS; i++) {
      if (W32_FD_ISSET(i, exceptfds)) {
	hTemp = (HANDLE) _get_osfhandle(i);
	hMultiple[countMultiple++] = hTemp;
	hExcept[i] = hTemp;
      }
      else {
	hExcept[i] = NULL;
      }
    }
  }

  /* check for invalid handles */
  for (i = 0; i != countMultiple; i++) {
    if (hMultiple[i] == INVALID_HANDLE_VALUE) {
      DebugBreak();
    }
  }

  /* check for invalid file types */
  for (i = 0; i != countMultiple; i++) {
    dwType = GetFileType(hMultiple[i]);
    if (dwType != FILE_TYPE_DISK && dwType != FILE_TYPE_CHAR && dwType != FILE_TYPE_PIPE) {
      DebugBreak();
    }
  }

  /* sleep until one or more handles are ready */
  if (nfds == 0) {
    Sleep(dwMilliseconds);
  }
  else {
    dwError = WaitForMultipleObjects((DWORD) countMultiple, hMultiple, FALSE, dwMilliseconds);
    switch (dwError) {
    case WAIT_FAILED:
      PrintLastError("WaitForMultipleObjects");
      retval = -1;
      goto exit;
      break;
    case WAIT_TIMEOUT:
      if (readfds != NULL) {
	W32_FD_ZERO(readfds);
      }
      if (writefds != NULL) {
	W32_FD_ZERO(writefds);
      }
      if (exceptfds != NULL) {
	W32_FD_ZERO(exceptfds);
      }
      retval = 0;
      goto exit;
    }
  }

  /* scan read handles, clearing appropriate bits in readfds */
  if (readfds != NULL) {
    for (i = 0; i != MAXIMUM_WAIT_OBJECTS; i++) {
      if (hRead[i] != NULL) {
	dwError = WaitForSingleObject(hRead[i], 0);
	switch (dwError) {
	case WAIT_TIMEOUT:
	  W32_FD_CLR(i, readfds);
	  break;
	case WAIT_OBJECT_0:
	  retval++;
	  break;
	default:
	  PrintLastError("WaitForSingleObject");
	  DebugBreak();
	}
      }
    }
  }

  /* scan write handles, clearing appropriate bits in writefds */
  if (writefds != NULL) {
    for (i = 0; i != MAXIMUM_WAIT_OBJECTS; i++) {
      if (hWrite[i] != NULL) {
	dwError = WaitForSingleObject(hWrite[i], 0);
	switch (dwError) {
	case WAIT_TIMEOUT:
	  W32_FD_CLR(i, writefds);
	  break;
	case WAIT_OBJECT_0:
	  retval++;
	  break;
	default:
	  PrintLastError("WaitForSingleObject");
	  DebugBreak();
	}
      }
    }
  }

  /* scan except handles, clearing appropriate bits in exceptfds */
  if (exceptfds != NULL) {
    for (i = 0; i != MAXIMUM_WAIT_OBJECTS; i++) {
      if (hExcept[i] != NULL) {
	dwError = WaitForSingleObject(hExcept[i], 0);
	switch (dwError) {
	case WAIT_TIMEOUT:
	  W32_FD_CLR(i, exceptfds);
	  break;
	case WAIT_OBJECT_0:
	  retval++;
	  break;
	default:
	  PrintLastError("WaitForSingleObject");
	  DebugBreak();
	}
      }
    }
  }

 exit:
  return retval;
}
