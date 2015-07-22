#include <time.h>

#define FRAMESIZE 1472

//Flags == 'A', ACK
//Flags == 'D', data frame
//Flags == 'L', last data frame
//Flags == 'I', initial frame, tell server totally how many bytes;
//Flags == 'S', receiver's ACK to I
//Flags== 'F', sender tell receiver to finish
//Flags == 'R', receiver tell sender 'F' received
typedef struct {
  long SeqNum;
  long AckNum;
  char Flags;
} header;

typedef struct _slot {
  //If status == 0, empty slot
  //If status == 1, read but not sent
  //If status == 2, sent not ACKed
  //If status == 3, sent and ACKed
  int status;
  long SeqNum;
  struct _slot* next;
  char* sendBuf;
  struct timeval sendTime;
} slot;
