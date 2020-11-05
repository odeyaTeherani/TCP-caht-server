
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h> //select()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // getdtablesize()

#define USAGE "Usage: ./chatserver <port> <max_clients>\n"
#define BUFFER_SIZE 1024

int create_socket(int port);

void error(char *err)
{
    perror(err);
    exit(EXIT_FAILURE);
}

/*A client that wait to connect when there are already max-clients connected and
 welcome socket is unset in the readfds, will not get an error msg, it will wait 
 to connect. 
 Once another client is disconnected, all the messages of the client 
 that wait to connect will be sent to all clients. - HOW?
*/
int main(int argc, char **argv)
{

    if (argc != 3)
    {
        printf("%s", USAGE);
        exit(EXIT_FAILURE);
    }

    if (atoi(argv[1]) <= 0 || atoi(argv[2]) <= 0)
    {
        printf("%s", USAGE);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    int max_clients = atoi(argv[2]);
    int arr_size = max_clients + 1; //+1 for welcome/main socket
    int num_of_active_clients = 0;
    int message_bytes_size; //used to save the size of message in the buffer

    /*Array for active clients */
    // 1. Maintain fd array for active fds
    int *active_clients = (int *)malloc(arr_size * sizeof(int));
    if (active_clients == NULL)
        error("error: malloc\n");

    //arbitrary value for invalid/inactive socket filedescriptor in the array
    memset(active_clients, -1, (arr_size) * sizeof(int));

    char buffer[BUFFER_SIZE];
    //clean the buffer (just in case it's not initialized to '\0' )
    memset(buffer, 0, BUFFER_SIZE);

    //2. Initiate read and write fd_sets (kind of an hashmap)
    fd_set next_r_fdset, w_fdset, current_r_fdset;

    //initialize/setup
    FD_ZERO(&next_r_fdset);
    FD_ZERO(&w_fdset);

    //3. Create the welcome socket and set its fd in the read-set
    int welcome_socket = create_socket(port);

    //delete
    if (welcome_socket < 0)
        exit(EXIT_FAILURE);

    active_clients[0] = welcome_socket;
    num_of_active_clients++; //an element has been added to the active sockets fd array

    FD_SET(welcome_socket, &next_r_fdset); // main socket is initially ready to read (accept new client)

    //4. endless loop:
    while (1)
    {
        current_r_fdset = next_r_fdset;

        //4.a. Call select with both sets
        // getdtablesize() - returns filedescriptors table size
        /* Check the first NFDS(arg0) descriptors each in READFDS for read
            readiness, in WRITEFDS for write readiness,  
            Returns the number of ready descriptors, or -1 for errors.*/
        if (select(getdtablesize(), &current_r_fdset, &w_fdset, NULL, NULL) < 0)
            error("error: select\n");

        //4.b. Go over the active fds, for each of them check:

        /*write the message from buffer to all active clients*/
        for (int i = 1; i < arr_size; i++)
        {
            //iii. If it is ready to write and this isn’t the welcome socket
            if (FD_ISSET(active_clients[i], &w_fdset))
            {
                printf("fd %d is ready to write\n", active_clients[i]);

                // 1. Write the buffer to the client
                int bytesWritten = write(active_clients[i], buffer, message_bytes_size);
                if (bytesWritten < 0)
                {
                    free(active_clients);
                    error("error: write\n");
                }

                //reset (clear) the "ready to write" for this client
                FD_CLR(active_clients[i], &w_fdset);
            }
        }

        //4.b.i. If it is ready to read and this is the welcome socket
        if (FD_ISSET(welcome_socket, &current_r_fdset) && num_of_active_clients <= arr_size)
        {

            //4.b.i.1 accept new client
            int new_client = accept(welcome_socket, NULL, NULL);
            if (new_client < 0)
            {
                free(active_clients);
                error("accept\n");
            }

            //4.b.i.2 add the fd to the read-set
            FD_SET(new_client, &next_r_fdset);
            num_of_active_clients++;

            // if active sockets fd array became full, mark main (welcome) socket
            // as " I don't want to read at the moment"
            // (If there are already <max_clients> clients connected,
            //     unset the welcome socket from the read-set.)
            if (num_of_active_clients == arr_size)
                FD_CLR(welcome_socket, &next_r_fdset);

            // search for the first empty place in the array
            for (int i = 1; i < arr_size; i++)
                if (active_clients[i] == -1)
                {
                    active_clients[i] = new_client;
                    break;
                }
        }

        // 4.b.ii. If it is ready to read and this isn’t the welcome socket
        for (int i = 1; i < arr_size; i++)
        {
            if (FD_ISSET(active_clients[i], &current_r_fdset))
            {
                //4.b.ii.1. read from client into a buffer
                // we save the number of bytes actually read from the client
                message_bytes_size = read(active_clients[i], buffer, BUFFER_SIZE);
                if (message_bytes_size < 0)
                {
                    free(active_clients);
                    error("read\n");
                }
                else if (message_bytes_size == 0)
                {
                    //TODO: should we remove this socket in such a case?

                    // When you read 0 bytes from an fd, unset it wherever you need
                    close(active_clients[i]);                 //close the socket for this client
                    FD_CLR(active_clients[i], &next_r_fdset); //clear the read fd_set for its fd
                    FD_SET(welcome_socket, &next_r_fdset);    // we now have a room for another client for sure
                    active_clients[i] = -1;                   // mark as invalid- empty fd cell
                    num_of_active_clients--;
                }
                else //set all the active clients in the write set
                {
                    //You should add the fds to the write set only when there is something to write.
                    //  If you read a buffer from a client at iteration n of the select,
                    //     you should write the buffer only in iteration n+1
                    printf("fd %d is ready to read\n", active_clients[i]);
                    for (int j = 1; j < arr_size; j++)
                    {
                        if (active_clients[j] != -1)
                            FD_SET(active_clients[j], &w_fdset);
                    }
                }

                // Only one client is writing text in one iteration of select.
                //each round only one socket is allowed to share its message
                // with the all the sockets (including himself)=> go back to outer loop
                break;
            }
        } // end of for inner loop
    }

    return 0;
}

//this function create and return the main socket, else return -1 if there is any error.
int create_socket(int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
        error("error: socket\n");

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        close(sockfd);
        error("error: bind\n");
    }

    //  N = 5 connection requests will be queued before further requests are refused.
    if (listen(sockfd, 5) < 0)
    {
        close(sockfd);
        error("error: listen\n");
    }

    return sockfd;
}
