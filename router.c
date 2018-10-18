#include "ne.h"
#include "router.h"
#include <sys/time.h>

int listenfd(int serverPort);
int initiliazeRouter(int recvfd, int routerID, struct sockaddr_in *neClient);

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        printf("Need 5 arguements to invoke this file\n");
        printf("usage: router <routerid> <ne hostname> <ne UDP port> <router UDP port>\n");
        return EXIT_FAILURE;
    }
    char configFileName[FILENAME_MAX];
    char *udpHostname;
    int routerID;
    int udpPort;
    int routerPort;

    // initializing all router ports, udp ports and log files
    routerID = atoi(argv[1]);
    if (routerID < 0 || routerID > MAX_ROUTERS - 1)
    {
        printf("Router ID must be between 0 and MAX_ROUTERS - 1\n");
        return EXIT_FAILURE;
    }
    udpHostname = argv[2];
    udpPort = atoi(argv[3]);
    routerPort = atoi(argv[4]);
    strcpy(configFileName, "router");
    strcat(configFileName, argv[1]);
    strcat(configFileName, ".log");
    FILE *configfd = fopen(configFileName, "w");

    // open socket and bind it and create the network emulator socket addr
    int recvfd = listenfd(routerPort);
    if (recvfd < 0)
    {
        fclose(configfd);
        printf("Failed to open ports with error code : %d\n", recvfd);
        return EXIT_FAILURE;
    }

    // Creating the network emulator client struct
    struct sockaddr_in networkEmulatorClient;
    bzero((char *)&networkEmulatorClient, sizeof(networkEmulatorClient));
    networkEmulatorClient.sin_family = AF_INET;
    // https://www.binarytides.com/hostname-to-ip-address-c-sockets-linux/
    struct hostent *host_entry = gethostbyname(udpHostname);
    if (host_entry == NULL)
    {
        printf("Failed to retreive host ip addr\n");
        return EXIT_FAILURE;
    }
    bcopy((char *)host_entry->h_addr_list[0],
          (char *)&networkEmulatorClient.sin_addr.s_addr,
          host_entry->h_length);
    networkEmulatorClient.sin_port = htons((unsigned short)udpPort);

    // Send INIT_REQUEST and get parse INIT_RESPONSE and initialize the routingTable
    int initialize = initiliazeRouter(recvfd, routerID, &networkEmulatorClient);
    if (initialize < 0)
    {
        printf("Failed initial correspondance with the network\n");
        fclose(configfd);
        close(recvfd);
        return EXIT_FAILURE;
    }

    // writing the initialized values into the logfile
    PrintRoutes(configfd, routerID);

    // Use timerfd style coding to update routing table information
    // https://www.programering.com/a/MDMzAjMwATc.html (Used for understanding how timerfd programming works)

    // Closing file read operations on router closing
    fclose(configfd);
    close(recvfd);
    return EXIT_SUCCESS;
}

int listenfd(int serverPort)
{
    int listenfd = -1;
    struct sockaddr_in serveraddr;

    // Creating a socket descriptor
    if ((listenfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        return -1;
    }

    // Creating an endpoint for all incoming connections
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)serverPort);

    // Binding the arguements to listenfd
    if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        return -2;
    }
    return listenfd;
}

// Initializes the router and routing table with the appropriate values
int initiliazeRouter(int recvfd, int routerID, struct sockaddr_in *neClient)
{
    struct pkt_INIT_REQUEST initialRequest;
    initialRequest.router_id = htonl(routerID);
    if (sendto(recvfd, (struct pkt_INIT_REQUEST *)&initialRequest, sizeof(initialRequest),
               0, (struct sockaddr *)neClient, sizeof(*neClient)) < 0)
    {
        printf("%d", errno);
        printf("Failed to send initial request\n");
        return -1;
    }
    struct pkt_INIT_RESPONSE initialResponse;
    if (recvfrom(recvfd, (struct pkt_INIT_RESPONSE *)&initialResponse,
                 sizeof(initialResponse), 0, NULL, NULL) < 0)
    {
        printf("Failed to receive initial response\n");
        return -2;
    }
    ntoh_pkt_INIT_RESPONSE(&initialResponse);
    InitRoutingTbl(&initialResponse, routerID);
    return EXIT_SUCCESS;
}