// headers
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <gumbo.h>

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
// fetches and processes the URLS. handles some errors like network and memory allocation errors
void *fetch_url(void *arg)
{
    FetchArgs *fetchArgs = (FetchArgs *)arg;
    URLQueue *queue = fetchArgs->queue;
    int max_depth = fetchArgs->max_depth;

    // currently uses libcurl to send HTTP requests and Gumbo to parse HTML content
    CURL *curl = curl_easy_init();

    // libcurl's redirect handling. For redirecting the URLs
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    while (1)
    {
        int depth;
        char *url = dequeue(queue, &depth);
        if (url == NULL)
        {
            break;
        }
        char *response = malloc(1);
        if (response == NULL)
        {
            fprintf(stderr, "malloc() failed\n");
            free(url);
            continue;
        }

        int visited_already = 0;
        // Check if the URL has been visited.
        for (int i = 0; i < visited_count; i++)
        {
            if (strcmp(visited[i], url) == 0)
            {
                visited_already = 1;
                break;
            }
        }

        if (visited_already)
        {
            free(url);
            continue;
        }

        // Mark the URL as visited.
        visited = realloc(visited, (visited_count + 1) * sizeof(char *));
        visited[visited_count++] = strdup(url);
        printf("Fetching URL: %s\n", url);

        *response = '\0';
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        CURLcode res = curl_easy_perform(curl);
        // error handling
        if (res != CURLE_OK)
        {
            fprintf(stderr, "Operation unsuccessful: %s\n", curl_easy_strerror(res));
            free(url);
            free(response);
            continue;
        }

        GumboOutput *output = gumbo_parse(response);
        // error handling
        if (output == NULL)
        {
            fprintf(stderr, "gumbo_parse() failed\n");
            free(url);
            continue;
        }

        if (depth < max_depth)
        {
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

    // WRITTEN BY DEZANGHI
    // Cleanup and program termination.
    // You may need to add additional cleanup logic here.

    return 0;
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
