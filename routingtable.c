#include "ne.h"
#include "router.h"
#include <stdbool.h>

// Global Variables as required by "router.h"
struct route_entry routingTable[MAX_ROUTERS];
int NumRoutes;

// Takes the neighboring router values from the InitResponse and copies to the routing
// table  global variable, struct route_entry routingTable[MAX_ROUTERS]
void InitRoutingTbl(struct pkt_INIT_RESPONSE *InitResponse, int myID)
{
    // Inserting the current router details
    routingTable[0].dest_id = myID;
    routingTable[0].next_hop = myID;
    routingTable[0].cost = 0;
    NumRoutes = 1;

    // Initializing the table with neighboring values
    int neighborIterator;

    for (neighborIterator = 0; neighborIterator < InitResponse->no_nbr; neighborIterator++)
    {
        routingTable[neighborIterator].dest_id = InitResponse->nbrcost[neighborIterator].nbr;
        routingTable[neighborIterator].next_hop = InitResponse->nbrcost[neighborIterator].nbr;
        routingTable[neighborIterator].cost = InitResponse->nbrcost[neighborIterator].cost;
        NumRoutes += 1;
    }
}

// Update the route information based on split horizon and forced updates
int UpdateRoutes(struct pkt_RT_UPDATE *RecvdUpdatePacket, int costToNbr, int myID)
{
    int i, j, distance, updateOccured = 0;
    bool notInTable = true;
    struct route_entry *updateIterator;
    struct route_entry *routingTableIterator;
    // Iterate through all updates in the update packet
    for (i = 0; i < RecvdUpdatePacket->no_routes; i++)
    {
        updateIterator = &RecvdUpdatePacket->route[i];
        distance = updateIterator->cost + costToNbr;
        if (distance > INFINITY)
        {
            distance = INFINITY;
        }
        // Make changed to the required routing table entires
        for (j = 0; j < NumRoutes; j++)
        {
            routingTableIterator = &routingTable[j];
            if (routingTableIterator->dest_id == updateIterator->dest_id)
            {
                notInTable = false;
                // Forced Update
                if (routingTableIterator->next_hop == RecvdUpdatePacket->sender_id && routingTableIterator->cost != distance)
                {
                    routingTableIterator->cost = distance;
                    updateOccured = 1;
                }
                // Split Horizon
                else if (distance < routingTableIterator->cost && updateIterator->next_hop != myID)
                {
                    routingTableIterator->next_hop = RecvdUpdatePacket->sender_id;
                    routingTableIterator->cost = distance;
                    updateOccured = 1;
                }
                else
                {
                    break;
                }
            }
        }

        // If update dest_id is not in the table add the data to the table
        if (notInTable)
        {
            routingTable[NumRoutes].dest_id = updateIterator->dest_id;
            routingTable[NumRoutes].next_hop = RecvdUpdatePacket->sender_id;
            routingTable[NumRoutes].cost = distance;
            NumRoutes += 1;
            updateOccured = 1;
        }
    }
    return updateOccured;
}

// Converts table to an update packet
void ConvertTabletoPkt(struct pkt_RT_UPDATE *UpdatePacketToSend, int myID)
{
    UpdatePacketToSend->sender_id = myID;
    UpdatePacketToSend->no_routes = NumRoutes;
    int routingTableIterator = 0;
    for (routingTableIterator = 0; routingTableIterator < NumRoutes; routingTableIterator++)
    {
        UpdatePacketToSend->route[routingTableIterator] = routingTable[routingTableIterator];
    }
}

// Prints the routing table to a log file
void PrintRoutes(FILE *Logfile, int myID)
{
    int maxRouterCounter = 0;
    int routingTableIterator = 0;
    // Print to file
    fprintf(Logfile, "Routing Table:\n");
    for (maxRouterCounter = 0; maxRouterCounter < MAX_ROUTERS; maxRouterCounter++)
    {
        for (routingTableIterator = 0; routingTableIterator < NumRoutes; routingTableIterator++)
        {
            if (routingTable[routingTableIterator].dest_id == maxRouterCounter)
            {
                fprintf(Logfile, "R%d -> R%d: R%d, %d", myID, maxRouterCounter,
                        routingTable[routingTableIterator].next_hop, routingTable[routingTableIterator].cost);
                break;
            }
        }
    }
}

void UninstallRoutesOnNbrDeath(int DeadNbr)
{
    int routingTableIterator = 0;
    for (routingTableIterator = 0; routingTableIterator < NumRoutes; routingTableIterator++)
    {
        if (routingTable[routingTableIterator].next_hop == DeadNbr)
        {
            routingTable[routingTableIterator].cost = INFINITY;
        }
    }
}