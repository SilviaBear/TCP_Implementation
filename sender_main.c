#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include "data_structure.h"

int MAXBUFLEN = 1400;

//RTT in ms
long RTT = 20;

struct timeval sendTime;
struct timeval ACKTime;

int isFinished;
int waitForResend;

long SeqNum = 0;
long lastACK = 0;

//Duplicated ACKs for three times than resend
int ACKCount;

long totalFrame;
long lastFrameSize;

long bytesToTransfer;
long bytesRead;

//Congestion window size
int CWS = 32;
int AMID;
//LastbyteAcked
slot* LBA;
//LastByteSent
slot* LBS;
//LastByteRead
slot* LBR;

struct timeval timeout;

pthread_cond_t send_cv;
pthread_mutex_t send_m;

struct sockaddr_in server;
socklen_t len;
socklen_t sendLen;
int s;

void setTimeout() {
  if(2 * RTT >= 1000) {
    timeout.tv_sec = 2 * RTT / 1000;
    timeout.tv_usec = ((2 * RTT) % 1000) * 1000;
  }
  else {
    timeout.tv_sec = 0;
    timeout.tv_usec = 2 * 1000 * RTT;
  }
  if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    perror("set timeout to 2 RTT");
  }
}

void* read_func(void* filename) {
  FILE* fp = fopen((char*)filename, "r");
  //FILE* wfp = fopen("testMAXBUFLEN", "w+");
  long bytesread;
  header* DATAheader = (header*)malloc(sizeof(header));
  DATAheader->Flags = 'D';
  while(1) {
    pthread_mutex_lock(&send_m);
    LBR->next = (slot*)malloc(sizeof(slot));
    LBR = LBR->next;
    LBR->SeqNum = SeqNum;
    DATAheader->SeqNum = SeqNum;
    SeqNum++;
    LBR->next = NULL;
    if(bytesToTransfer - bytesRead < MAXBUFLEN) {
      LBR->sendBuf = (char*)malloc(sizeof(header) + lastFrameSize);
      if((bytesread = fread(LBR->sendBuf + sizeof(header), 1, lastFrameSize, fp)) < 0) {
        perror("read from file");
      }
    }
    else {
      LBR->sendBuf = (char*)malloc(sizeof(header) + MAXBUFLEN);
      if((bytesread = fread(LBR->sendBuf + sizeof(header), 1, MAXBUFLEN, fp)) < 0) {
        perror("read from file");
      }
    }
    bytesRead += bytesread;
    ////printf("bytesRead %lu\n", bytesRead);
    memcpy(LBR->sendBuf, DATAheader, sizeof(header));
    pthread_mutex_unlock(&send_m);
    pthread_cond_broadcast(&send_cv);
    //fwrite(LBR->sendBuf + sizeof(header), 1, bytesread, wfp); 
    ////printf("Read from file: %d %s\n", bytesread, LBR->sendBuf + sizeof(header));
    if(bytesRead == bytesToTransfer) {
      //printf("Read func exit\n");
      break;
    }
  }
  fclose(fp);
  //fclose(wfp);
}

void* send_func(void* unusedParam) {
  int numbytes;
  while(1) {
    pthread_mutex_lock(&send_m);
    /*
    //If next segment to read has not been read into memory or exceed congestion window size, wait
    while(LBS == NULL || LBA == NULL) {
      timeout.tv_sec = 1000 * 1000;
      if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("set timeout to infinity");
      }
      pthread_cond_wait(&send_cv, &send_m);
      /*if(isFinished) {
        pthread_mutex_unlock(&send_m);
        return NULL;
        }
    }
    while(LBS->sendBuf == NULL) {
      timeout.tv_sec = 1000 * 1000;
      if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("set timeout to infinity");
        }
      pthread_cond_wait(&send_cv, &send_m);
      /*if(isFinished) {
        pthread_mutex_unlock(&send_m);
        return NULL;
        }
        }*/
    ////printf("pass cv2\n");
    ////printf("LBS %lu LBA %lu\n", LBS->SeqNum, LBA->SeqNum);
    /*if(2 * RTT > 1000) {
      timeout.tv_sec = 2 * RTT / 1000;
      timeout.tv_usec = ((2 * RTT) % 1000) * 1000;
    }
    else {
      timeout.tv_sec = 0;
      timeout.tv_usec = 2 * 1000 * RTT;
    }
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
      perror("set timeout to 2RTT");
    }
    */
    
    while(waitForResend) {
      pthread_cond_wait(&send_cv, &send_m);
    }
    while(LBS->SeqNum >= LBR->SeqNum) {
      ////printf("wait for reading\n");
      pthread_cond_wait(&send_cv, &send_m);
    }
    while(LBS->SeqNum - LBA->SeqNum > CWS) {
      //printf("sending window full, wait for ACK\n");
      pthread_cond_wait(&send_cv, &send_m);
    }
    LBS = LBS->next;
    ////printf("pass cv4\n");
    pthread_mutex_unlock(&send_m);
    //printf("LBS %p LBR %p \n", LBS, LBR);
    ////printf("LBS->sendBuf %s\n", LBS->sendBuf + sizeof(header));
    usleep(500);
    setTimeout();
    if((numbytes = sendto(s, LBS->sendBuf, LBS->SeqNum == totalFrame? sizeof(header) + lastFrameSize : sizeof(header) + MAXBUFLEN, 0, (struct sockaddr *)&server, sendLen)) <= 0) {
      perror("send data");
    }
    ////printf("numbytes: %d  %lu\n", numbytes, lastFrameSize);
    // LBS->status = 2;
    //printf("Send Seq %lu\n", LBS->SeqNum);
    gettimeofday(&LBS->sendTime, 0);
  }
}

void resend() {
  if(sendto(s, (LBA->next)->sendBuf, (LBA->next)->SeqNum == totalFrame? sizeof(header) + lastFrameSize : sizeof(header) + MAXBUFLEN, 0, (struct sockaddr *)&server, sendLen) <= 0) {
    perror("resend");
  }
  //printf("Resend seq %lu\n", (LBA->next)->SeqNum);
}

void* recv_func(void* unusedParam) {
  int numbytes;
  char* buf = (char*)malloc(sizeof(header));
  header* recvHeader;
  struct timeval recvTime;
  while(1){
    ////printf("Enter loop\n");
    if((numbytes = recvfrom(s, buf, sizeof(header), 0, (struct sockaddr *)&server, &len)) < 0) {
      perror("timeout");
      pthread_mutex_lock(&send_m);
      LBS = LBA;
      ////printf("resend from %lu\n", LBS->SeqNum);
      CWS = 32;
      AMID = 0;
      pthread_mutex_unlock(&send_m);
      pthread_cond_broadcast(&send_cv);
      continue;
    }
    ////printf("recv numbytes %d\n", numbytes);
    recvHeader = (header*)buf;
    if(recvHeader->AckNum == lastACK) {
      ACKCount++;
    }
    //Duplicated ACK for 3 times and resend
    if(ACKCount == 2) {
      //printf("Duplicated ACK, resend\n");
      waitForResend = 1;
      pthread_mutex_lock(&send_m);
      resend();
      //printf("RESEND\n");
      while(1) {
        //fputs("enter recv\n", stderr);
        if(recvfrom(s, buf, sizeof(header), 0, (struct sockaddr *)&server, &len) < 0) {
          perror("resend timeout");
          resend();
        }
        //printf("recv Ack %lu\n", ((header*)buf)->AckNum);
        if(((header*)buf)->AckNum > lastACK) {
          ACKCount = 0;
          waitForResend = 0;
          lastACK = ((header*)buf)->AckNum;
          slot* sendslot = LBA;
          while(sendslot->SeqNum < lastACK) {
            sendslot = sendslot->next;
          }
          LBA = sendslot;
          break;
        }
      }
      CWS = CWS / 2 < 32? 32 : CWS / 2;
      AMID = 1;
      pthread_mutex_unlock(&send_m);
      pthread_cond_broadcast(&send_cv);
      continue;
    }
    //printf(stderr, "recv ACK %lu, expect ACK %lu\n", recvHeader->AckNum, lastACK + 1);
    //pthread_mutex_lock(&send_m);
    if(recvHeader->AckNum >= lastACK + 1) {
      ////printf("LBA Seq %lu\n", LBA->SeqNum);
      gettimeofday(&recvTime, 0);
      if(recvHeader->AckNum == lastACK + 1 && recvHeader->Flags == 'A') {
        long sample_RTT =  (recvTime.tv_sec * 1000 + recvTime.tv_usec / 1000 - (LBA->next)->sendTime.tv_sec * 1000 - (LBA->next)->sendTime.tv_usec / 1000);
        RTT = 0.2 * sample_RTT + 0.8 * RTT;
        //printf("sample RTT %lu new RTT %lu\n", sample_RTT, RTT);
        CWS = AMID? CWS + 1 : CWS * 2;
        if(CWS > 1024) {
          CWS = 1024;
        }
      }
      lastACK = recvHeader->AckNum;
      slot* sendslot = LBA;
      while(sendslot->SeqNum < lastACK) {
        sendslot = sendslot->next;
      }
      LBA = sendslot;
      ////printf("CWS %d\n", CWS);
      //Slide window
      /*while(LBA->SeqNum <= recvHeader->AckNum) {
        //printf("LBA seq %lu\n", LBA->SeqNum);
        slot* temp = LBA;
        LBA = LBA->next;
        free(temp->sendBuf);
        free(temp);
        if(LBA == NULL) {
          isFinished = 1;
          break;
        }
        }*/
     }
    ////printf("lastACK %lu, totalFrame%lu\n", lastACK, totalFrame);
    if(lastACK == totalFrame) {
      //printf("Decide FIN\n");
      header* finishHeader = (header*)malloc(sizeof(header));
      finishHeader->Flags = 'F';
      if(sendto(s, finishHeader, sizeof(header), 0, (struct sockaddr *)&server, sendLen) <= 0) {
        perror("send finish");
      }
      char* finishBuf = (char*)malloc(sizeof(header));
      while(1) {
        while(recvfrom(s, finishBuf, sizeof(header), 0, (struct sockaddr *)&server, &len) < 0) {
          if(sendto(s, finishHeader, sizeof(header), 0, (struct sockaddr *)&server, sendLen) <= 0) {
            perror("finish retransfer");
          }
          continue;
        }
        if(((header*)finishBuf)->Flags == 'R') {
          break;
        }
      }
      //pthread_mutex_unlock(&send_m);
      pthread_cond_broadcast(&send_cv);
      free(finishHeader);
      free(finishBuf);
      //printf("Recv thread exit.\n");
      return NULL;
    }
    //pthread_mutex_unlock(&send_m);
    pthread_cond_broadcast(&send_cv);
  }
}

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, long bytes) {
  bytesToTransfer = bytes;
  sendLen = sizeof(struct sockaddr_in);
  struct hostent *host;
  if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    perror("socket");
    exit(1);
  }
  host = gethostbyname(hostname);
  memset((char *) &server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = htons(hostUDPport);
  server.sin_addr = *((struct in_addr*) host->h_addr);
  LBA = (slot*)malloc(sizeof(slot));
  LBA->SeqNum = 0;
  SeqNum++;
  LBA->sendBuf = NULL;
  LBA->next = NULL;
  LBS = LBA;
  LBR = LBA;
  timeout.tv_sec = 0;
  totalFrame = bytesToTransfer % MAXBUFLEN == 0? bytesToTransfer / MAXBUFLEN : bytesToTransfer / MAXBUFLEN + 1;
  lastFrameSize = bytesToTransfer % MAXBUFLEN;
  if(lastFrameSize == 0) {
    lastFrameSize = MAXBUFLEN;
  }
  pthread_t sendingThread;
  pthread_t receivingThread;
  pthread_t readingThread;
  header* initialHeader = (header*)malloc(sizeof(header));
  initialHeader->Flags = 'I';
  char* sendBuf = (char*)malloc(sizeof(header) + sizeof(long));
  memcpy(sendBuf, initialHeader, sizeof(header));
  *((long*)(sendBuf + sizeof(header))) = bytesToTransfer;
  setTimeout();
  if(sendto(s, sendBuf, sizeof(header) + sizeof(long), 0, (struct sockaddr *)&server, sendLen) <= 0) {
    perror("initial transfer");
  }
  char* recvBuf = (char*)malloc(sizeof(header) + sizeof(long));
  while(recvfrom(s, recvBuf, sizeof(header) + sizeof(long), 0, (struct sockaddr *)&server, &len) < 0) {
    if(sendto(s, sendBuf, sizeof(header) + sizeof(long), 0, (struct sockaddr *)&server, sendLen) <= 0) {
      perror("initial retransfer");
    } 
  }
  //printf("Connection established\n");
  free(initialHeader);
  free(sendBuf);
  free(recvBuf);
  pthread_create(&readingThread, 0, read_func, (void*)filename);
  pthread_create(&sendingThread, 0, send_func, (void*)0);
  pthread_create(&receivingThread, 0, recv_func, (void*)0);
  pthread_join(readingThread, NULL);
  pthread_join(receivingThread, NULL);
  //pthread_join(sendingThread, NULL);
  //printf("Sending func finish\n");
    
}

int main(int argc, char** argv)
{
  unsigned short int udpPort;
  long numBytes;
	
  if(argc != 5)
	{
      //printf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
      exit(1);
	}
  udpPort = (unsigned short int)atoi(argv[2]);
  numBytes = atoll(argv[4]);
  reliablyTransfer(argv[1], udpPort, argv[3], numBytes);
} 
