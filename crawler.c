// headers
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <gumbo.h>
#include <ctype.h>
#include <curl/easy.h>

// function prototypes
void handle_parsing_error(GumboOutput *output, const char *url);
void handle_network_error(CURLcode res, const char *url);
void handle_memory_allocation_error(const char *msg);
void handle_failure(const char *msg);
int validate_url(const char *url);

// Logging Tags: info is just information, warning will flag the current process, error will end current process
const char *INFO = "INFO", *WARNING = "WARNING", *ERROR = "ERROR";

// variables for url validation
int url_valid = 1;
int url_invalid = 0;
int url_connectionfail = -1;

// Logger function to track process info and errors
void logger(const char *tag, const char *message)
{
    time_t now; // Time info for logging
    time(&now); // Get current time

    FILE *fpointer = fopen("errorLogs.txt", "a");

    if (fpointer == NULL) // Error check for opening error file
    {
        printf("LOGGING ERROR: Can not open logging file.");
        return;
    }

    fprintf(fpointer, "%s [%s]: %s\n", ctime(&now), tag, message);
    fclose(fpointer);
}

// structure for queue elements
// represents a node in a linked list. The nodes are the URLs to be fetched and consist of the URL, the depth that the URL was found, and a pointer to the next node.
typedef struct URLQueueNode
{
    char *url;
    // added depth control
    int depth;
    struct URLQueueNode *next;
} URLQueueNode;

// structure for a thread-safe queue of URLs to be fetched. has head and tail pointers to the linked list and lock for synchronization
typedef struct
{
    URLQueueNode *head, *tail;
    pthread_mutex_t lock;
} URLQueue;

// structure for args to be passed to the fetch_url function. has a pointer to the URLQueue and max depth
typedef struct
{
    URLQueue *queue;
    int max_depth;
} FetchArgs;

// initializing the URL queue.
void initQueue(URLQueue *queue)
{
    queue->head = queue->tail = NULL;
    pthread_mutex_init(&queue->lock, NULL); // initializing the mutex lock
}

// Adds a URL to the tail of the thread-safe queue. Handles depth as well. mutex locks are used here for synchronization
// the lock ensures that only one thread can access the queue at a time
void enqueue(URLQueue *queue, const char *url, int depth)
{
    URLQueueNode *newNode = malloc(sizeof(URLQueueNode)); // creating a new node
    newNode->url = strdup(url);                           // copying the URL to the new node
    newNode->depth = depth;                               // setting the depth
    newNode->next = NULL;                                 // next pointer = null

    pthread_mutex_lock(&queue->lock); // locking the mutex
    if (queue->tail)
    {
        queue->tail->next = newNode;
    } // if the queue isn't empty, we add the new node to the tail
    else
    {
        queue->head = newNode;
    } // if the queue is empty, the new node = head
    queue->tail = newNode;
    pthread_mutex_unlock(&queue->lock);
    // printf("Current Depth: %d\n", depth);
}

// removes a URL from the head of the thread-safe queue. Handles depth as well. mutex locks are used here for synchronixation
// returns the URL and depth
// the lock ensures that only one thread can access the queue at a time
char *dequeue(URLQueue *queue, int *depth)
{
    pthread_mutex_lock(&queue->lock);
    if (queue->head == NULL)
    {
        pthread_mutex_unlock(&queue->lock);
        logger(WARNING, "Empty queue");
        return NULL;
    }

    URLQueueNode *temp = queue->head; // accessing the head of the queue
    char *url = temp->url;            // getting the URL
    *depth = temp->depth;
    queue->head = queue->head->next; // moving the head pointer
    if (queue->head == NULL)
    {
        queue->tail = NULL;
    }
    pthread_mutex_unlock(&queue->lock);
    return url;
}

// callback function to be called by libcurl. It is called when libcurl receives data from the server
size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
    size_t total_size = size * nmemb;                                            // total size of the data
    char **response_ptr = (char **)stream;                                       // pointer to the response buffer
    char *temp = realloc(*response_ptr, strlen(*response_ptr) + total_size + 1); // reallocating memory for the response buffer
    if (temp == NULL)
    {
        fprintf(stderr, "realloc() failed\n");
        return 0;
    }
    *response_ptr = temp;
    strncat(*response_ptr, (char *)ptr, total_size); // concatenating the data to the response buffer
    return total_size;
}

// recursively search each child node of the current node for links in the HTML content AND add them to the queue. uses the Gumbo library
void search_for_links(GumboNode *node, URLQueue *queue, int depth, int max_depth)
{
    if (node->type != GUMBO_NODE_ELEMENT)
    {
        return;
    } // if node is not an element, return

    GumboAttribute *href; // attribute for the href of the link
    if (node->v.element.tag == GUMBO_TAG_A &&
        (href = gumbo_get_attribute(&node->v.element.attributes, "href")))
    {
        if (depth < max_depth)
        {
            enqueue(queue, href->value, depth + 1);
        }
    } // if the node is an anchor tag and has an href attribute, add the URL to the queue
    // checks the depth of the URL and enqueue. It checks for anchor tag and href attrbute because the links are usually in the anchor tag

    GumboVector *children = &node->v.element.children; // children of the current node
    for (unsigned int i = 0; i < children->length; ++i)
    {
        search_for_links((GumboNode *)children->data[i], queue, depth, max_depth);
    } // recursively searching each child node for links
}

// array to store the visited URLs
char **visited;
// keeping track of the visited URL count
int visited_count = 0;
// fetches and processes the URLs. Uses libcurl to send HTTP requests and Gumbo to parse the HTML content.
// fetches the URLs from the queue, marks them as visited, and sends the HTTP request to fetch the content.
// then it uses Gumbo to parse the content and adds the other links to the queue
void *fetch_url(void *arg)
{
    FetchArgs *fetchArgs = (FetchArgs *)arg; // pointer to the FetchArgs structure, contains the URLQueue and max depth
    URLQueue *queue = fetchArgs->queue;      // pointer to the URLQueue
    int max_depth = fetchArgs->max_depth;
    
    // // Print all elements in the queue
    // URLQueueNode *current = queue->head;
    // while (current != NULL) {
    //     printf("URL: %s, Depth: %d\n", current->url, current->depth);
    //     current = current->next;
    // }

    CURL *curl = curl_easy_init();                      // initializing libcurl
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // option to follow redirects

    while (1)
    { // loop to fetch URLs from the queue
        int depth;
        char *url = dequeue(queue, &depth); // getting the URL from the queue
        
        if (url == NULL)
        {
            break;
        }

        int url_status = validate_url(url); // validating the URL
        if (url_status == url_invalid)
        {
            fprintf(stderr, "Invalid or dead URL: %s\n", url);
            free(url);
            continue;
        }
        // handling connection failures
        else if (url_status == url_connectionfail)
        {
            fprintf(stderr, "Failed to connect. Please check your internet connection.\n");
            free(url);
            continue;
        }

        char *response = malloc(1); // buffer to store the response
        if (response == NULL)
        {
            handle_memory_allocation_error("Failed to allocate memory for response buffer");
            free(url);
            continue;
        }

        // checking if the URL has already been visited
        int visited_already = 0;
        for (int i = 0; i < visited_count; i++)
        {
            if (strcmp(visited[i], url) == 0)
            {
                logger(WARNING, "[Fetch_URL] Visited URL encountered.");
                visited_already = 1;
                break;
            }
        }

        // if visited, free the URL and continue to the next URL
        if (visited_already)
        {
            free(url);
            continue;
        }

        // reallocate memory for the visited URLs
        visited = realloc(visited, (visited_count + 1) * sizeof(char *));
        if (visited == NULL)
        {
            handle_memory_allocation_error("Failed to reallocate memory for visited URLs");
            free(url);
            free(response);
            continue;
        }
        // add the URL to the visited URLs
        visited[visited_count++] = strdup(url);
        printf("Fetching URL: %s\n", url);
        // printf("Depth link was found: %d\n", depth);

        *response = '\0';
        // handles the URL to fetch
        curl_easy_setopt(curl, CURLOPT_URL, url);
        // handles the callback function to be called by libcurl
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        // handles the data to be sent to the callback function
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        // sending the HTTP request
        CURLcode res = curl_easy_perform(curl);
        // handling network errors
        if (res != CURLE_OK)
        {
            handle_network_error(res, url);
            free(url);
            free(response);
            continue;
        }

        // parsing the HTML content using Gumbo
        GumboOutput *output = gumbo_parse(response);
        if (output == NULL)
        {
            handle_parsing_error(output, url);
            free(url);
            free(response);
            continue;
        }

        // if the depth is less than the max depth, search for links in the HTML content
        if (depth < max_depth)
        {
            search_for_links(output->root, queue, depth, max_depth);
        }
        gumbo_destroy_output(&kGumboDefaultOptions, output);

        free(url);
        free(response);
    }

    curl_easy_cleanup(curl); // cleaning up libcurl
    logger(INFO, "Curl cleaning");

    return NULL;
}

// initializes the URLQueue, adds the first link to the queue, and creates multiple threads(multithreading) to fetch the URLs concurrently
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s <starting-url> <max-depth>\n", argv[0]);
        return 1;
    } // error handling for invalid arguments

    URLQueue queue;
    initQueue(&queue); // initializing the queue
    logger(INFO, "URL Queue Initialized");

    enqueue(&queue, argv[1], 0); // adding the first URL to the queue
    logger(INFO, "Launch URL Enqueued");

    int max_depth = atoi(argv[2]); // getting the max depth from the command line

    // error handling for max depth
    if (max_depth < 1 || isalpha(max_depth))
    {
        puts("Invalid max depth. Please enter a number greater than 0.");
        return 1;
    }

    FetchArgs fetchArgs;             // creating the FetchArgs structure as an argument to the fetch_url function
    fetchArgs.queue = &queue;        // setting the queue
    fetchArgs.max_depth = max_depth; // setting the max depth

    //  creating threads and distributing the work of URL fetching among them.
    const int NUM_THREADS = 4;
    // creating threads
    pthread_t threads[NUM_THREADS];
    //
    for (int i = 0; i < NUM_THREADS; i++)
    {
        pthread_create(&threads[i], NULL, fetch_url, (void *)&fetchArgs);
    }

    // Joins threads after completion. Once the thread is done with its task, it returns and can be joined back to the main thread
    for (int i = 0; i < NUM_THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // Cleanup
    URLQueueNode *current = queue.head; // freeing the memory for the URLs in the queue
    while (current)
    {
        URLQueueNode *next = current->next;
        free(current->url);
        free(current);
        current = next;
    } // freeing the memory for the visited URLs

    return 0;
}

// ERROR HANDLING to validate URLs
int validate_url(const char *url)
{
    CURL *curl = curl_easy_init(); // initializing libcurl
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);   // setting the URL
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // Only check headers, not the body
        CURLcode res = curl_easy_perform(curl);     // sending the HTTP request
        curl_easy_cleanup(curl);

        if (res == CURLE_COULDNT_CONNECT)
        {
            return url_connectionfail;
        } // handling connection issues
        else if (res == CURLE_OPERATION_TIMEDOUT)
        {
            fprintf(stderr, "Network interruption during curl operation: %s\n", url);
            exit(1);
        }
        return (res == CURLE_OK) ? url_valid : url_invalid;
    }
    return url_invalid;
}

// error handling for Network errors
void handle_network_error(CURLcode res, const char *url)
{
    fprintf(stderr, "Failed to fetch URL '%s': %s\n", url, curl_easy_strerror(res));
}

// error handling For Parsing Errors
void handle_parsing_error(GumboOutput *output, const char *url)
{
    fprintf(stderr, "Failed to parse HTML content from URL '%s'\n", url);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
}

// Memory Allocation
void handle_memory_allocation_error(const char *msg)
{
    fprintf(stderr, "Memory allocation error: %s\n", msg);
}

// Other failures
void handle_failure(const char *msg)
{
    fprintf(stderr, "Operation failed: %s\n", msg);
}
/**
 * Things to Note:
 * Make sure to install the required libraries- For linux it is: sudo apt-get install libgumbo-dev and sudo apt-get install libcurl4-openssl-dev
 * you can run the code by using the makefile to handle the compilation.
 * Depth means it just goes down the depth of the current page its on so if depth is 2 it will get the links on the current webpage and the links on the pages it links to
 */

// reads web pages and reads their content
// uses multiple threads to fetch URL concurrently
// uses libcurl library to send the HTTP requests and Gumbo library to parse the HTML content