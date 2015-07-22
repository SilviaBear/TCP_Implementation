#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include "data_structure.h"

int MAXBUFLEN = 1400;

long AckNum = 0;

//LastByteRead
slot* LBR;
//NextByteExpected
slot* NBE;
//Last slot available
slot* LSA = NULL;

long totalNumBytes;
long totalFrame;
long lastFrameSize;
long totalRecvdBytes;
pthread_cond_t recv_cv;
pthread_mutex_t recv_m;

int isFinished;

header* ACKheader;

struct sockaddr_in self, other;

socklen_t len;
int s;

void* write_func(void* destinationFile) {
  FILE* fp = fopen((char*)destinationFile, "w+");
  int numwrite;
  while(1) {
    pthread_mutex_lock(&recv_m);
    //printf("sleep here, %d, %lu, %lu %p %p\n", isFinished, LBR->SeqNum, NBE->SeqNum, NBE->next, LBR->next);
    while(LBR->SeqNum >= NBE->SeqNum - 1 && !isFinished) {
      pthread_cond_wait(&recv_cv, &recv_m);
    }
    pthread_mutex_unlock(&recv_m);
    //printf("pass sleep status, %lu %p %d %d\n", LBR->SeqNum, LBR->next, LBR->next->status, LBR->next->sendBuf == NULL);
    //printf("pass again %lu %p\n", NBE->SeqNum, NBE->next);
    slot* temp = LBR;
    //printf("LBR %lu NBE %lu\n", LBR->SeqNum, NBE->SeqNum);
    LBR = LBR->next;
    if(temp->sendBuf) {
      free(temp->sendBuf);
    }
    free(temp);
    numwrite = fwrite(LBR->sendBuf, 1, LBR->SeqNum == totalFrame? lastFrameSize : MAXBUFLEN,  fp);
    //printf("Numwrite numwrite %lu %d\n", totalRecvdBytes, numwrite);
    totalRecvdBytes += numwrite;
    if(totalRecvdBytes == totalNumBytes) {
      return NULL;
    }
  }
}

void send_func(long ACK, int isRetransmission) {
  int numbytes;
  ACKheader->Flags = isRetransmission? 'B' : 'A';
  ACKheader->AckNum = ACK;
  if((numbytes = sendto(s, ACKheader, sizeof(header), 0, (struct sockaddr *)&other, len)) <= 0) {
    perror("reply ACK");
  }
  //printf("Send ACK for %lu\n", ACK);
}

void* recv_func(void* unusedParam) {
  int numbytes;
  char* buf = (char*)malloc(sizeof(header) + MAXBUFLEN);
  while((numbytes = recvfrom(s, buf, sizeof(header) + MAXBUFLEN, 0, (struct sockaddr *)&other, &len)) > 0) {
    header* sendHeader = (header*)buf;
    if(sendHeader->Flags == 'I' || totalNumBytes == 0) {
      header* SHeader = (header*)malloc(sizeof(header));
      SHeader->Flags = 'S';
      if(sendto(s, SHeader, sizeof(header), 0, (struct sockaddr *)&other, len) <= 0) {
        perror("reply initial");
      }
      totalNumBytes = *((long*)(buf + sizeof(header)));
      totalFrame = totalNumBytes % MAXBUFLEN == 0? totalNumBytes / MAXBUFLEN : totalNumBytes / MAXBUFLEN + 1;
      lastFrameSize = totalNumBytes % MAXBUFLEN;
      if(lastFrameSize == 0) {
        lastFrameSize = MAXBUFLEN;
      }
      ////printf("lastFrameSize %lu\n", lastFrameSize);
      ////printf("totalNumBytes: %lu totalframe %lu lastframe %lu\n", totalNumBytes, totalFrame, lastFrameSize);
      continue;
    }
    else if(sendHeader->Flags == 'D') {
      ////printf("numbytes %d Recvbuf %s\n", numbytes, buf + sizeof(header));
      //printf("Recv seqnum %lu, NBE %lu\n", sendHeader->SeqNum, NBE->SeqNum);
      if(sendHeader->SeqNum == NBE->SeqNum) {
	pthread_mutex_lock(&recv_m);
	NBE->status = 1;
	NBE->sendBuf = (char*)malloc(sendHeader->SeqNum == totalFrame? lastFrameSize : MAXBUFLEN);
        memcpy(NBE->sendBuf, buf + sizeof(header), sendHeader->SeqNum == totalFrame? lastFrameSize : MAXBUFLEN);
	while(NBE->status == 1) {
	  AckNum++;
	  //printf("ACK num %lu\n", AckNum);
	  if(NBE->next == NULL) {
	    NBE->next = (slot*)malloc(sizeof(slot));
	    NBE->next->sendBuf = NULL;
	    NBE->next->SeqNum = NBE->SeqNum + 1;
	    NBE->next->next = NULL;
	  }
	  NBE = NBE->next;
	}
	if(NBE->SeqNum == totalFrame + 1) {
	  isFinished = 1;
	}
	pthread_mutex_unlock(&recv_m);
	pthread_cond_broadcast(&recv_cv);
	send_func(AckNum, 0);
      }
      else if(sendHeader->SeqNum > NBE->SeqNum) {
	int i;
	int shouldgo = 0;
	if(LSA == NULL) {
	  LSA = NBE;
	  shouldgo = 1;
	}
	slot* current = NBE;
	if(sendHeader->SeqNum <= LSA->SeqNum || shouldgo) {
	  //printf("sender seq %lu, NBE seq %lu LSA seq %lu\n", sendHeader->SeqNum, NBE->SeqNum, LSA->SeqNum);
	  printf("Packet lost, leap \n");
	  for(i = 0; i < sendHeader->SeqNum - NBE->SeqNum; i++) {
	    if(current->next == NULL) {
	      current->next = (slot*)malloc(sizeof(slot));
	      current->next->next = NULL;
	      current->next->SeqNum = current->SeqNum + 1;
	      printf("malloc for %lu %p\n", current->SeqNum + 1, current->next);
	      current->next->sendBuf = NULL;
	      ////printf("hole recieve, seqnum %lu\n", SeqNum);
	    }
	    current = current->next;
	  }
	  shouldgo = 0;
	}
	else {
	  current = LSA;
	  for(i = 0; i < sendHeader->SeqNum - LSA->SeqNum; i++) {
	    if(current->next == NULL) {
	      current->next = (slot*)malloc(sizeof(slot));
	      current->next->next = NULL;
	      current->next->SeqNum = current->SeqNum + 1;
	      //printf("malloc for %lu %p LSA %lu\n", current->SeqNum + 1, current->next, LSA->SeqNum);
	      current->next->sendBuf = NULL;
	      ////printf("hole recieve, seqnum %lu\n", SeqNum);
	    }
	    current = current->next;
	  }
	  LSA = current;
	}
	current->status = 1;
	//printf("current seq %lu sendseq %lu\n", current->SeqNum, sendHeader->SeqNum);
	//printf("current %lu, recv Seq %lu\n", current->SeqNum, sendHeader->SeqNum); 
        current->sendBuf = (char*)malloc(sendHeader->SeqNum == totalFrame? lastFrameSize : MAXBUFLEN);
	////printf("lastFrameSize %lu\n", lastFrameSize);
        memcpy(current->sendBuf, buf + sizeof(header), sendHeader->SeqNum == totalFrame? lastFrameSize : MAXBUFLEN);
	send_func(AckNum, 1);
      }
      else {
        send_func(sendHeader->SeqNum, 1);
      }
    }
    else if(sendHeader->Flags == 'F') {
      //printf("receive FIN\n");
      header* replyHeader = (header*)malloc(sizeof(header));
      replyHeader->Flags = 'R';
      int i = 0;
      for(i = 0; i < 5; i++) {
	if((numbytes = sendto(s, replyHeader, sizeof(header), 0, (struct sockaddr *)&other, len)) <= 0) {
	  perror("reply finish");
	}
      }
      free(replyHeader);
      free(buf);
      pthread_cond_broadcast(&recv_cv);
      //printf("recv thread exit\n");
      return NULL;
    }
  }
}

void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
  LBR = (slot*)malloc(sizeof(slot));
  NBE = (slot*)malloc(sizeof(slot));
  LBR->next = NBE;
  NBE->next = NULL;
  LBR->SeqNum = 0;
  NBE->SeqNum = LBR->SeqNum + 1;
  len = sizeof(struct sockaddr_in);
  if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    perror("socket");
    exit(1);
  }
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_port = htons(myUDPport);
  self.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s, (struct sockaddr *) &self, sizeof(self)) == -1) {
    perror("bind");
    exit(1);
  }
  ACKheader = (header*)malloc(sizeof(header));
  pthread_t recvThread;
  pthread_t writeThread;
  pthread_create(&writeThread, 0, write_func, (void*)destinationFile);
  pthread_create(&recvThread, 0, recv_func, (void*)0);
  pthread_join(recvThread, NULL);
  printf("recv exit\n");
  pthread_cond_broadcast(&recv_cv);
  pthread_join(writeThread, NULL);
  close(s);
}

int main(int argc, char** argv)
{
  unsigned short int udpPort;
	
  if(argc != 3)
    {
      //printf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
      exit(1);
    }
	
  udpPort = (unsigned short int)atoi(argv[1]);
	
  reliablyReceive(udpPort, argv[2]);
}
