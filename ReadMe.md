# Web Crawler

## Group Members:
- Momoreoluwa Ayinde (RUID: 221000193)
- Mo Obanor (RUID: 213000030)
- Shaniah Black (RUID:212005498)

## Overview
This web crawler is a multithreaded program made to fetch and process URLs concurrently. It uses libcurl for sending HTTP requests and Gumbo for parsing HTML content. The crawler starts from a given starting URL and explores links up to a specified depth.

## Architecture
The crawler consists of:
- **URLQueueNode**: A node in a linked list holding URLs to be fetched. Each node includes the URL, its depth, and a pointer to the next node.
- **URLQueue**: Function represents a thread-safe queue for managing URLs. It includes a head and tail pointer to the linked list and a mutex lock for synchronization.
- **FetchArgs**: Contains arguments for the fetch_url function, including a pointer to the URLQueue and the maximum depth to crawl.
- **fetch_url Function**: Fetches and processes URLs. It handles various errors such as network failures, parsing errors, and memory allocation failures.
- **Main Function**: Initializes the URLQueue, adds the starting URL to the queue, and creates multiple threads to fetch URLs concurrently.

## Multithreading Approach
The crawler uses multithreading to distribute the work of fetching URLs among multiple threads. Each thread calls the fetch_url function to process URLs from the queue concurrently. Thread synchronization is achieved using mutex locks to ensure safe access to the shared URLQueue.

## Design Decisions
- **Thread-Safe Queue**: We decided to use a thread-safe queue to manage URLs to be fetched. This allows multiple threads to enqueue and dequeue URLs without the risk of data corruption.
- **CURL Library**: We chose libcurl for handling HTTP requests due to its ease of use and extensive features for network operations.
- **Gumbo Library**: Gumbo was chosen for HTML parsing because of its simplicity and robustness in handling malformed HTML.

## Challenges Faced and Solutions
- **Concurrency**: Implementing a multithreaded crawler posed challenges related to thread synchronization and race conditions. We addressed this by using mutex locks to ensure that only one thread accesses the URLQueue at a time.
- **Memory Management**: Managing memory for dynamically allocated data, such as URLs and response buffers, required careful handling to avoid memory leaks. We freed allocated memory after use and implemented error handling for memory allocation failures.
- **Error Handling**: Implementing robust error handling for network failures, parsing errors, and other potential issues required thorough testing and debugging. We extensively tested the crawler under various scenarios and implemented error handling functions to handle different types of failures.


## Libraries Used
- **pthread**: For multithreading support.
- **libcurl**: For sending HTTP requests and handling network operations.
- **Gumbo**: For parsing HTML content and extracting links from web pages.

## Specific Roles and Contributions
- Momoreoluwa Ayinde: Implemented the main function, implemented URL fetching, depth and created Makefile.
- Mo Obanor: Added on to URL enqueueing, dequeueing, and implemented logging.
- Shaniah Black: Implemented URL parsing error handling functionns and created ReadMe file.

## How to Use
To run the web crawler, execute the following command: "./crawler <starting-url> <max-depth>"

Replace `<starting-url>` with the URL from which you want to start crawling and `<max-depth>` with the maximum depth to crawl.

or run "make run" in your terminal. This has a default starting-url and max-depth which you can replace in the Makefile.

## Error Handling
The web crawler includes error handling for various scenarios:
- Invalid or dead URLs: URLs are validated before fetching, and dead URLs are skipped.
- Network errors: Errors occurring during HTTP requests are handled, and appropriate messages are displayed.
- Parsing errors: Errors during HTML parsing are handled, and the affected URLs are skipped.
- Memory allocation errors: Out-of-memory conditions are handled gracefully, and appropriate messages are displayed.
- Other failures: Generic failure conditions are handled, and error messages are printed.

