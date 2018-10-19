#include "ne.h"
#include "router.h"
#include <sys/timerfd.h>
#include <stdbool.h>

// Struct that stores neighbor node data
typedef struct
{
    unsigned int no_nbr;
    unsigned int nbr_id[MAX_ROUTERS];
    unsigned int nbr_cost[MAX_ROUTERS];
    bool nbr_dead[MAX_ROUTERS];
    struct itimerspec failureTimer[MAX_ROUTERS];
    int failurefd[MAX_ROUTERS];

} nbr_data;

//-------------------------------------- FUNCTION DECLARATIONS---------------------*/

/* Binds router socket to listen for incoming UDP Connections and send UDP Packets */
int listenfd(int serverPort);
/* Send the initial request and the parses the initial response */
int initiliazeRouter(int recvfd, int routerID, struct sockaddr_in *neClient, nbr_data *nbrData);
/* The heart of the program that does all function such as update, converge, timeout handling */
void enableRouter(int recvfd, int routerID, FILE *configfd, nbr_data nbrData, struct sockaddr_in neClient);
/* ---------------------- ENABLEROUTER HELPER FUNCTIONS --------------------------*/
/* Initializes a specific type of timer */
/*
    type = 1 --> update timer
    type = 2 --> converge timer
    type = 3 --> failure detection timer
*/
int initializeTimer(struct itimerspec *genericTimer, int type);
/* Reset a specific type of timer */
/*
    type = 1 --> update timer (reset to UPDATE_INTERVAL)
    type = 2 --> converge timer (reset to CONVERGE_TIMEOUT)
    type = 3 --> failure detection timer (reset to FAILURE_DETECTION)
*/
void resetTimer(struct itimerspec *genericTimer, int type, int genericfd);
/* Parses all incoming routing table packets and upates the routing table */
bool parseUpdates(struct pkt_RT_UPDATE updatePktRcvd, int recvfd, nbr_data *nbrData,
                  int routerID, FILE *configfd, int convergefd, bool converged, struct itimerspec *convergeTimer);
/* Converts the routing table to a packet and sends it to other routers */
void sendUpdates(struct pkt_RT_UPDATE updatePktToSend, int recvfd, int updatefd, struct sockaddr_in neClient,
                 struct itimerspec *updateTimer, int routerID, nbr_data *nbrData);
/* Converges the tables */
bool convergeTable(bool converged, FILE *configfd, struct itimerspec *convergeTimer, int convergefd, int runtime);

/*------------------------------ START OF THE PROGRAM ---------------------------*/

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
    nbr_data nbrData;
    int initialize = initiliazeRouter(recvfd, routerID, &networkEmulatorClient, &nbrData);
    if (initialize < 0)
    {
        printf("Failed initial correspondance with the network\n");
        fclose(configfd);
        close(recvfd);
        return EXIT_FAILURE;
    }

    // Writing the initialized values into the logfile
    PrintRoutes(configfd, routerID);

    // Use timerfd style coding to update routing table information
    // https://www.programering.com/a/MDMzAjMwATc.html (Used for understanding how timerfd programming works)
    enableRouter(recvfd, routerID, configfd, nbrData, networkEmulatorClient);

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
int initiliazeRouter(int recvfd, int routerID, struct sockaddr_in *neClient, nbr_data *nbrData)
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

    // Storing all the neighbor information from the intial reponse
    nbrData->no_nbr = initialResponse.no_nbr;
    int i;
    for (i = 0; i < nbrData->no_nbr; i++)
    {
        nbrData->nbr_id[i] = initialResponse.nbrcost[i].nbr;
        nbrData->nbr_cost[i] = initialResponse.nbrcost[i].cost;
        if (nbrData->nbr_cost[i] == INFINITY)
        {
            nbrData->nbr_dead[i] = true;
        }
        else
        {
            nbrData->nbr_dead[i] = false;
        }

        // initialize failure detection timers and file descriptor for each neighbor
        nbrData->failurefd[i] = initializeTimer(&nbrData->failureTimer[i], 3);
    }
    return EXIT_SUCCESS;
}

// Implements the specific functionality of the router
void enableRouter(int recvfd, int routerID, FILE *configfd, nbr_data nbrData, struct sockaddr_in neClient)
{
    struct pkt_RT_UPDATE updatePktToSend;
    struct pkt_RT_UPDATE updatePktRcvd;

    fd_set rdfs;

    // update timer file descriptor and intializations
    struct itimerspec updateTimer;
    int updatefd = initializeTimer(&updateTimer, 1);

    // converge time file descriptor and initializations
    bool converged = false;
    struct itimerspec convergeTimer;
    int convergefd = initializeTimer(&convergeTimer, 2);

    // Keeps track of the program runtime (in seconds)
    int runtime = 0;

    // Use select to manipulate router funtionality
    while (true)
    {
        FD_ZERO(&rdfs);
        FD_SET(recvfd, &rdfs);
        FD_SET(updatefd, &rdfs);
        FD_SET(convergefd, &rdfs);
        int maxfailurefd = nbrData.failurefd[0];
        for (int i = 0; i < nbrData.no_nbr; i++)
        {
            FD_SET(nbrData.failurefd[i], &rdfs);
            maxfailurefd = (nbrData.failurefd[i] > maxfailurefd) ? nbrData.failurefd[i] : maxfailurefd;
        }

        int selectfd = (convergefd > updatefd) ? convergefd : updatefd;
        selectfd = (maxfailurefd > selectfd) ? maxfailurefd : selectfd;

        // Running select to determine which module to run
        if (select(selectfd + 1, &rdfs, NULL, NULL, NULL) == -1)
        {
            printf("Select failed with errno: %d\n", errno);
            exit(EXIT_FAILURE);
        }

        // Receive and parse updates from other routers
        if (FD_ISSET(recvfd, &rdfs))
        {
            converged = parseUpdates(updatePktRcvd, recvfd, &nbrData, routerID,
                                     configfd, convergefd, converged, &convergeTimer);
        }

        // Send updates to other routers
        if (FD_ISSET(updatefd, &rdfs))
        {
            sendUpdates(updatePktToSend, recvfd, updatefd, neClient,
                        &updateTimer, routerID, &nbrData);
            runtime += 1;
        }

        // Routing table converged
        if (FD_ISSET(convergefd, &rdfs))
        {
            converged = convergeTable(converged, configfd,
                                      &convergeTimer, convergefd, runtime);
        }

        // Check if any of my neighbors failed
        for (int i = 0; i < nbrData.no_nbr; i++)
        {
            if (FD_ISSET(nbrData.failurefd[i], &rdfs))
            {
                if (!nbrData.nbr_dead[i])
                {
                    UninstallRoutesOnNbrDeath(nbrData.nbr_id[i]);
                    PrintRoutes(configfd, routerID);
                    resetTimer(&convergeTimer, 2, convergefd);
                }
                nbrData.nbr_dead[i] = true;
            }
        }
    }
}

//------------------------- ENABLE ROUTER BREAKDOWN ------------------------
int initializeTimer(struct itimerspec *genericTimer, int type)
{
    if (type == 1)
        genericTimer->it_value.tv_sec = UPDATE_INTERVAL;
    else if (type == 2)
        genericTimer->it_value.tv_sec = CONVERGE_TIMEOUT;
    else if (type == 3)
        genericTimer->it_value.tv_sec = FAILURE_DETECTION;
    else
        exit(EXIT_FAILURE);
    genericTimer->it_value.tv_nsec = 0;
    genericTimer->it_interval.tv_sec = 0;
    genericTimer->it_interval.tv_nsec = 0;
    int genericfd = timerfd_create(CLOCK_MONOTONIC, 0);
    timerfd_settime(genericfd, 0, genericTimer, NULL);
    return genericfd;
}

void resetTimer(struct itimerspec *genericTimer, int type, int genericfd)
{
    if (type == 1)
        genericTimer->it_value.tv_sec = UPDATE_INTERVAL;
    else if (type == 2)
        genericTimer->it_value.tv_sec = CONVERGE_TIMEOUT;
    else if (type == 3)
        genericTimer->it_value.tv_sec = FAILURE_DETECTION;
    else
        exit(EXIT_FAILURE);
    genericTimer->it_value.tv_nsec = 0;
    timerfd_settime(genericfd, 0, genericTimer, NULL);
}

bool parseUpdates(struct pkt_RT_UPDATE updatePktRcvd, int recvfd, nbr_data *nbrData,
                  int routerID, FILE *configfd, int convergefd, bool converged, struct itimerspec *convergeTimer)
{
    bzero((char *)&updatePktRcvd, sizeof(updatePktRcvd));
    // Receive the update packt from other routers
    if (recvfrom(recvfd, (struct pkt_RT_UPDATE *)&updatePktRcvd,
                 sizeof(updatePktRcvd), 0, NULL, NULL) < 0)
    {
        printf("recvfrom failed with errno: %d", errno);
        exit(EXIT_FAILURE);
    }

    ntoh_pkt_RT_UPDATE(&updatePktRcvd);
    int costToNbr = -1;

    // Get the cost to the neighbor the packet came from
    int i = 0;
    for (i = 0; i < nbrData->no_nbr; i++)
    {
        if (updatePktRcvd.sender_id == nbrData->nbr_id[i])
        {
            costToNbr = nbrData->nbr_cost[i];
            break;
        }
    }
    // Since an update has been received if the neighbor is down reset it back to alive
    resetTimer(&nbrData->failureTimer[i], 3, nbrData->failurefd[i]);
    nbrData->nbr_dead[i] = false;

    // Update the routing table
    int updatedTable = UpdateRoutes(&updatePktRcvd, costToNbr, routerID);

    // If the routing table updated, rest the converge count down timer and
    // the converged flag
    if (updatedTable)
    {
        PrintRoutes(configfd, routerID);
        resetTimer(convergeTimer, 2, convergefd);
        converged = false;
    }
    return converged;
}

void sendUpdates(struct pkt_RT_UPDATE updatePktToSend, int recvfd, int updatefd, struct sockaddr_in neClient,
                 struct itimerspec *updateTimer, int routerID, nbr_data *nbrData)
{
    bzero((char *)&updatePktToSend, sizeof(updatePktToSend));
    ConvertTabletoPkt(&updatePktToSend, routerID);
    int i;
    for (i = 0; i < nbrData->no_nbr; i++)
    {
        updatePktToSend.dest_id = nbrData->nbr_id[i];
        hton_pkt_RT_UPDATE(&updatePktToSend);
        if (sendto(recvfd, (struct pkt_RT_UPDATE *)&updatePktToSend,
                   sizeof(updatePktToSend), 0, (struct sockaddr *)&neClient,
                   sizeof(neClient)) < 0)
        {
            printf("Failed to send data to other routers\n");
            exit(EXIT_FAILURE);
        }
        ntoh_pkt_RT_UPDATE(&updatePktToSend);
    }

    // Reset the update interval
    resetTimer(updateTimer, 1, updatefd);
}

bool convergeTable(bool converged, FILE *configfd, struct itimerspec *convergeTimer, int convergefd, int runtime)
{
    // Print converged at the end of the file
    if (!converged)
    {
        fprintf(configfd, "%d:Converged\n", runtime);
        fflush(configfd);
        converged = true;
    }

    // Reset converge timeout
    resetTimer(convergeTimer, 2, convergefd);
    return converged;
}