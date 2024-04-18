// headers
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <gumbo.h>
#include <curl/easy.h>

//function prototypes
void handle_parsing_error(GumboOutput *output, const char *url);
void handle_network_error(CURLcode res, const char *url);
void handle_memory_allocation_error(const char *msg);
void handle_failure(const char *msg);
int validate_url(const char *url);

// structure for queue elements.
typedef struct URLQueueNode
{
    char *url;
    // added depth control
    int depth;
    struct URLQueueNode *next;
} URLQueueNode;

// structure for a thread-safe queue.
typedef struct
{
    URLQueueNode *head, *tail;
    pthread_mutex_t lock;
} URLQueue;

typedef struct
{
    URLQueue *queue;
    int max_depth;
} FetchArgs;

// initializing the URL queue.
void initQueue(URLQueue *queue)
{
    queue->head = queue->tail = NULL;
    pthread_mutex_init(&queue->lock, NULL);
}

// Adds a URL to the thread-safe queue. Handles depth as well. mutex locks are used here for synchronization
void enqueue(URLQueue *queue, const char *url, int depth)
{
    URLQueueNode *newNode = malloc(sizeof(URLQueueNode));
    newNode->url = strdup(url);
    newNode->depth = depth;
    newNode->next = NULL;

    pthread_mutex_lock(&queue->lock);
    if (queue->tail)
    {
        queue->tail->next = newNode;
    }
    else
    {
        queue->head = newNode;
    }
    queue->tail = newNode;
    pthread_mutex_unlock(&queue->lock);
}

// removes a URL from the thread-safe queue. Handles depth as well. mutex locks are used here for synchronixation
char *dequeue(URLQueue *queue, int *depth)
{
    pthread_mutex_lock(&queue->lock);
    if (queue->head == NULL)
    {
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }

    URLQueueNode *temp = queue->head;
    char *url = temp->url;
    *depth = temp->depth;
    queue->head = queue->head->next;
    if (queue->head == NULL)
    {
        queue->tail = NULL;
    }
    pthread_mutex_unlock(&queue->lock);
    return url;
}

size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
    size_t total_size = size * nmemb;
    char **response_ptr = (char **)stream;
    char *temp = realloc(*response_ptr, strlen(*response_ptr) + total_size + 1);
    if (temp == NULL)
    {
        fprintf(stderr, "realloc() failed\n");
        return 0;
    }
    *response_ptr = temp;
    strncat(*response_ptr, (char *)ptr, total_size);
    return total_size;
}

// recursively search for links in the HTML content AND add them to the queue. uses the Gumbo library
void search_for_links(GumboNode *node, URLQueue *queue, int depth, int max_depth)
{
    if (node->type != GUMBO_NODE_ELEMENT)
    {
        return;
    }

    GumboAttribute *href;
    if (node->v.element.tag == GUMBO_TAG_A &&
        (href = gumbo_get_attribute(&node->v.element.attributes, "href")))
    {
        if (depth < max_depth)
        {
            enqueue(queue, href->value, depth + 1);
        }
    }

    GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; ++i)
    {
        search_for_links((GumboNode *)children->data[i], queue, depth, max_depth);
    }
}

char **visited;
int visited_count = 0;
// fetches and processes the URLS. error handling included
void *fetch_url(void *arg) {
    FetchArgs *fetchArgs = (FetchArgs *)arg;
    URLQueue *queue = fetchArgs->queue;
    int max_depth = fetchArgs->max_depth;

    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    while (1) {
        int depth;
        char *url = dequeue(queue, &depth);
        if (url == NULL) {
            break;
        }

        if (!validate_url(url)) {
            fprintf(stderr, "Invalid or dead URL: %s\n", url);
            free(url);
            continue;
        }

        char *response = malloc(1);
        if (response == NULL) {
            handle_memory_allocation_error("Failed to allocate memory for response buffer");
            free(url);
            continue;
        }

        int visited_already = 0;
        for (int i = 0; i < visited_count; i++) {
            if (strcmp(visited[i], url) == 0) {
                visited_already = 1;
                break;
            }
        }

        if (visited_already) {
            free(url);
            continue;
        }

        visited = realloc(visited, (visited_count + 1) * sizeof(char *));
        if (visited == NULL) {
            handle_memory_allocation_error("Failed to reallocate memory for visited URLs");
            free(url);
            free(response);
            continue;
        }
        visited[visited_count++] = strdup(url);
        printf("Fetching URL: %s\n", url);

        *response = '\0';
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            handle_network_error(res, url);
            free(url);
            free(response);
            continue;
        }

        GumboOutput *output = gumbo_parse(response);
        if (output == NULL) {
            handle_parsing_error(output, url);
            free(url);
            free(response);
            continue;
        }

        if (depth < max_depth) {
            search_for_links(output->root, queue, depth, max_depth);
        }
        gumbo_destroy_output(&kGumboDefaultOptions, output);

        free(url);
        free(response);
    }

    curl_easy_cleanup(curl);

    return NULL;
}

// Main function to drive the web crawler. multithreading is done here as we are creating worker_threads and assigning the threads to the fetch_url function
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s <starting-url> <max-depth>\n", argv[0]);
        return 1;
    }

    URLQueue queue;
    initQueue(&queue);
    enqueue(&queue, argv[1], 0);

    int max_depth = atoi(argv[2]);

    FetchArgs fetchArgs;
    fetchArgs.queue = &queue;
    fetchArgs.max_depth = max_depth;

    // WRITTEN BY DEZANGHI
    //  Placeholder for creating and joining threads.
    //  You will need to create multiple threads and distribute the work of URL fetching among them.
    const int NUM_THREADS = 4; // Example thread count, adjust as needed.
    pthread_t threads[NUM_THREADS];

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
    URLQueueNode *current = queue.head;
    while (current)
    {
        URLQueueNode *next = current->next;
        free(current->url);
        free(current);
        current = next;
    }

    return 0;
}

//ERROR HANDLING
//To check URLs
int validate_url(const char *url) { 
    CURL *curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // Only check headers, not the body
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        return (res == CURLE_OK) ? 1 : 0;
    }
    return 0;
}

//For Network errors
void handle_network_error(CURLcode res, const char *url) {
    fprintf(stderr, "Failed to fetch URL '%s': %s\n", url, curl_easy_strerror(res));
}

//For Parsing Errors
void handle_parsing_error(GumboOutput *output, const char *url) {
    fprintf(stderr, "Failed to parse HTML content from URL '%s'\n", url);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
}

//Memory Allocation
void handle_memory_allocation_error(const char *msg) {
    fprintf(stderr, "Memory allocation error: %s\n", msg);
}

//Other failures
void handle_failure(const char *msg) {
    fprintf(stderr, "Operation failed: %s\n", msg);
}
/**
 * Things to Note:
 * Make sure to install the required libraries- For linux it is: sudo apt-get install libgumbo-dev and sudo apt-get install libcurl4-openssl-dev
 * Run the code on VScode and make sure it says build successful
 * Use the following terminal command to confirm the successful build: gcc -std=c11 -pedantic -pthread crawler.c -o crawler -lgumbo -lcurl
 * Run the code in the terminal: ./crawler http://www.example.com 2 (you can replace the website and depth)
 * Depth means it just goes down the depth of the current page its on so if depth is 2 it will get the links on the current webpage and the links on the pages it links to
 * Mo, your mac might change the .vscode file documents to the mac implementation. Idk if you would want to use a Linux VM instead of your mac but
 *          if you decide to use your mac, please change the .vscode file implementation to the Linux one when pushing
 *
 * Things I'm not sure of/Things to confirm from TA:
 * Should running on VScode cause it to prompt us for things or is it fine to use the terminal to run it? Right now it runs with the terminal
 * This command currently builds it with gcc but I'm not sure if it's the right command: gcc -std=c11 -pedantic -pthread crawler.c -o crawler -lgumbo -lcurl,
 * using the one giving by Dezanghi doesn't work for us
 */

// reads web pages and reads their content
// suses multiple threads to fetch URL concurrently
// uses libcurl library to send the HTTP requests and Gumbo library to parse the HTML content
// Dezanghi already gave us some code so i started based off of it
// URLQueueNode is a node in a linked list. Each node represents a URL to be fetched.
// It has the URL, the depth at which this URL was found, and a pointer to the next node.
// URLQueue: thread-safe queue of the URLS to be fetched. It has a head and tail pointer to the linked list and a lock for synchronization
// FetchArgs: arguments to be passed to the fetch_url function. It has a pointer to the URLQueue and the max depth to be fetched
// initQueue: initializes the URLQueue, head and tail to null
// enqueue: adds a URL to the queue. It creates a new node and adds it to the tail of the queue. the lock ensures that only one thread can
//          access the queue at a time
// dequeue: removes a URL from the queue. It removes the head of the queue and returns the URL. the lock ensures that only one thread can
//          access the queue at a time
// write_data: callback function to be called by libcurl. called when libcurl receives data from the server, appends data to a string
// search_for_links: recursively searches for links in the HTML content. It uses the Gumbo library to parse the HTML content and adds
// the links to the queue
// fetch_url: fetches and processes the URLs. It uses libcurl to send HTTP requests and Gumbo to parse the HTML content. It fetches the URLs
//           from the queue, marks them as visited, fetches the content, parses the content, and adds the other links to the queue
// main: initializes the URLQueue, adds the starting URL to the queue, and creates multiple threads

// reads web pages and reads their content
// suses multiple threads to fetch URL concurrently
// uses libcurl library to send the HTTP requests and Gumbo library to parse the HTML content
// Dezanghi already gave us some code so i started based off of it
// URLQueueNode is a node in a linked list. Each node represents a URL to be fetched.
// It has the URL, the depth at which this URL was found, and a pointer to the next node.
// URLQueue: thread-safe queue of the URLS to be fetched. It has a head and tail pointer to the linked list and a lock for synchronization
// FetchArgs: arguments to be passed to the fetch_url function. It has a pointer to the URLQueue and the max depth to be fetched
// initQueue: initializes the URLQueue, head and tail to null
// enqueue: adds a URL to the queue. It creates a new node and adds it to the tail of the queue. the lock ensures that only one thread can
//          access the queue at a time
// dequeue: removes a URL from the queue. It removes the head of the queue and returns the URL. the lock ensures that only one thread can
//          access the queue at a time
// write_data: callback function to be called by libcurl. called when libcurl receives data from the server, appends data to a string
// search_for_links: recursively searches for links in the HTML content. It uses the Gumbo library to parse the HTML content and adds
// the links to the queue
// fetch_url: fetches and processes the URLs. It uses libcurl to send HTTP requests and Gumbo to parse the HTML content. It fetches the URLs
//           from the queue, marks them as visited, fetches the content, parses the content, and adds the other links to the queue
// main: initializes the URLQueue, adds the starting URL to the queue, and creates multiple threads