/*
 Simple TCP Load Balancer 
 Nick Booth 2018

*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <jsoncpp/json/json.h>
#include <fstream>
#include <pthread.h>

void error(const char *msg)
{
  perror(msg);
  exit(1);
}

unsigned int hash(char *str)
{
    unsigned int hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

int main(int argc, char **argv) {
  int sockfd, clientsockfd, serversockfd, portno = 8888;
  Json::Value root;
  socklen_t clilen;
  struct sockadd_in *servers[10];
  char cli_buffer[128];
  char srv_buffer[128];
  struct sockaddr_in tcplb_addr, serv_addr, cli_addr;
  int n, m;

  //load config
  std::ifstream settings("settings.json", std::ifstream::binary);
  settings >> root;
  int n_servers = root["servers"].size();
  if(n_servers < 1){
    printf("No servers defined in settings.json. Exiting.");
    exit(1);
  } else {
    printf("Found %d servers in settings.json.\n",n_servers);
    for (int i=0;i<root["servers"].size();i++)
    {
      printf("%s:%d\n",root["servers"][i]["host"].asCString(), root["servers"][i]["port"].asInt());
    }
  }

  if (argc < 2){
    printf("No Listen Port specified, defaulting to: %d\n", portno);
  } else {
    portno = atoi(argv[1]);
  }

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    error("Error opening socket");
  }

  bzero((char *) &serv_addr, sizeof(serv_addr));

  tcplb_addr.sin_family = AF_INET;
  tcplb_addr.sin_addr.s_addr = INADDR_ANY;
  tcplb_addr.sin_port = htons(portno);

  if (bind(sockfd, (struct sockaddr *) &tcplb_addr, sizeof(tcplb_addr)) < 0)
    error("Error binding");

  listen(sockfd, 10);

  clilen = sizeof(cli_addr);
  while(clientsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen)) {

    if(clientsockfd < 0)
      printf("Erron on accept");

    //spin off connection as new process
    if(fork()==0){

      char str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(cli_addr.sin_addr), str, INET_ADDRSTRLEN);
      printf("Got connection from client: %s on %d\n", str, ntohs(cli_addr.sin_port));
      char hash_str[INET_ADDRSTRLEN + 6];
      bzero((char *) &hash_str, INET_ADDRSTRLEN + 6);
      sprintf(hash_str, "%s:%d", str, ntohs(cli_addr.sin_port));
      //decide on server to use
      int h = (hash(hash_str) % n_servers);
      printf("hash of %s is: %d\n",hash_str, h);
      //open connection to new host
      if ((serversockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
      {
          error("Server Socket creation error");
          break;
      }

      memset(&serv_addr, '0', sizeof(serv_addr));

      serv_addr.sin_family = AF_INET;
      serv_addr.sin_port = htons(root["servers"][h]["port"].asInt());

      if(inet_pton(AF_INET, root["servers"][h]["host"].asCString(), &serv_addr.sin_addr)<=0)
      {
          error("Address type not supported\n");
          break;
      }

      inet_ntop(AF_INET, &(serv_addr.sin_addr), str, INET_ADDRSTRLEN);
      printf("Attempting connection to server on: %s:%d...", str, ntohs(serv_addr.sin_port));
      if(connect(serversockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
      {
          error("Connection to server failed\n");
          break;
      }

      bool server_closed = false;
      bool client_closed = false;

      while(true){
        bzero(cli_buffer, 128);
        bzero(srv_buffer, 128);

        n = recv(clientsockfd, cli_buffer, 127, MSG_DONTWAIT | MSG_WAITALL);
        if (n > 0)
        {
          send(serversockfd, cli_buffer, n, 0);
        } else if(n < 0){

          switch(errno){
            default:
              client_closed = true;
            break;
            case 11:
            break;
          }
        } else {
          client_closed = true;
        }
        if(server_closed && client_closed){
    		  break;
    	  }
        m = recv(serversockfd, srv_buffer, 127, MSG_DONTWAIT | MSG_WAITALL);
        if(m > 0)
        {
          // we don't want to send empty msg
          send(clientsockfd, srv_buffer, m, 0);
        } else if(m < 0){
          switch (errno){
            default:
              server_closed = true;
            break;
            case 11:
            break;
          }

        } else {
          server_closed = true;
        }

    	  if(server_closed || client_closed){
    		  break;
    	  }
      }

      close(clientsockfd);
      close(serversockfd);

    } else{
      //close client sock in parent process
      close(clientsockfd);
    }
  }

  close(sockfd);
}
