#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * DOC: Pipeline Framework - A Generic Node-Based Processing Pipeline
 *
 * This library provides a flexible framework for building processing pipelines
 * where data flows through a series of connected nodes. Each node performs a
 * specific operation and passes data to the next node via queues.
 *
 * Key Features:
 *
 * * Linear chain of processing nodes (pipeline)
 * * Type-safe node registration and instantiation
 * * YAML-based configuration
 * * Simple vector-based queue communication between nodes
 * * Support for multiple node types (INPUT, PROCESS, FORMAT, OUTPUT)
 *
 * Usage Flow:
 *
 * 1. Register nodes into the global registry using node_registry_add()
 * 2. Configure pipeline from YAML file using pipeline_configure()
 * 3. Initialize pipeline with pipeline_init()
 * 4. Start processing with pipeline_start()
 * 5. Stop/flush as needed
 * 6. Clean up with pipeline_fini()
 */

/**
 * struct pipeline_queue - Array-based queue for passing data between nodes
 * @items: Array of void* items
 * @capacity: Allocated capacity
 * @size: Current number of items
 * @head: Index of first item (for pop)
 *
 * This is a basic FIFO (First In, First Out) implementation.
 */
struct pipeline_queue {
    void ** items;
    size_t capacity;
    size_t size;
    size_t head;
};

/**
 * pipeline_queue_create - Create a new pipeline queue
 * @initial_capacity: Initial capacity (will grow as needed)
 *
 * Return: Pointer to queue, or NULL on allocation failure
 */
struct pipeline_queue *
pipeline_queue_create(size_t initial_capacity);

/**
 * pipeline_queue_destroy - Destroy a pipeline queue and free all memory
 * @queue: Queue to destroy
 *
 * Does NOT free the items themselves - caller must handle that.
 */
void
pipeline_queue_destroy(struct pipeline_queue * queue);

/**
 * pipeline_queue_push - Push an item onto the back of the queue
 * @queue: Queue to push to
 * @item: Item to push (takes ownership)
 *
 * Queue will automatically grow if needed.
 *
 * Return: 0 on success, -1 on allocation failure
 */
int
pipeline_queue_push(struct pipeline_queue * queue, void * item);

/**
 * pipeline_queue_pop - Pop an item from the front of the queue
 * @queue: Queue to pop from
 *
 * Return: Item pointer, or NULL if queue is empty
 */
void *
pipeline_queue_pop(struct pipeline_queue * queue);

/**
 * pipeline_queue_empty - Check if queue is empty
 * @queue: Queue to check
 *
 * Return: 1 if empty, 0 if not empty
 */
int
pipeline_queue_empty(struct pipeline_queue * queue);

/**
 * pipeline_queue_size - Get the current number of items in the queue
 * @queue: Queue to check
 *
 * Return: Number of items
 */
size_t
pipeline_queue_size(struct pipeline_queue * queue);

/**
 * enum node_type - Defines the category of a node in the pipeline
 * @NODE_UNDEFINED: Undefined node type
 * @NODE_INPUT: Source nodes - generate or receive data
 * @NODE_PROCESS: Processing nodes - transform data
 * @NODE_FORMAT: Formatting nodes - convert data format
 * @NODE_OUTPUT: Sink nodes - output or store data
 */
enum node_type {
    NODE_UNDEFINED = 0,
    NODE_INPUT,
    NODE_PROCESS,
    NODE_FORMAT,
    NODE_OUTPUT,
};

/**
 * enum pipeline_state - Tracks the lifecycle of a pipeline
 * @PIPELINE_UNDEFINED: Undefined state
 * @PIPELINE_INITIALIZED: Configuration loaded, instances created
 * @PIPELINE_READY: All nodes initialized and ready
 * @PIPELINE_RUNNING: Pipeline is actively processing
 * @PIPELINE_ERROR: Error state
 */
enum pipeline_state {
    PIPELINE_UNDEFINED = 0,
    PIPELINE_INITIALIZED,
    PIPELINE_READY,
    PIPELINE_RUNNING,
    PIPELINE_ERROR,
};

/** Forward declaration for callback signatures */
struct instance_node;

/**
 * struct node - Node Definition - describes a type of processing node
 * @name: Unique node type name
 * @version: Version string
 * @description: Human-readable description
 * @type: Category of this node
 * @next: Internal list management - used by pipeline and registry
 * @prev: Internal list management - used by pipeline and registry
 * @ctx_create: Allocate per-instance context
 * @ctx_destroy: Free per-instance context
 * @init: Initialize instance
 * @configure: Configure from YAML
 * @process: Process data
 * @flush: Flush pending data
 * @fini: Cleanup instance
 *
 * This structure defines the template for a node type. Multiple instances
 * of the same node type can be created in a pipeline.
 */
struct node {
    char const * name;
    char const * version;
    char const * description;
    enum node_type type;

    struct node * next;
    struct node * prev;

    void *(*ctx_create)(void);
    void (*ctx_destroy)(void * ctx);

    int (*init)(struct instance_node * inst);
    int (*configure)(struct instance_node * inst, char const * config);
    int (*process)(struct instance_node * inst, uint32_t batch_size);
    int (*flush)(struct instance_node * inst);
    void (*fini)(struct instance_node * inst);
};

/**
 * struct instance_node - a specific instantiation of a node in a pipeline
 * @node: Reference to node definition
 * @instance_name: Unique instance name (e.g. "parser.0")
 * @next: Next instance node in list
 * @ctx: Per-instance context - each instance has its own state
 * @input_queue: Where this node reads data from (can be NULL for NODE_INPUT)
 * @output_queue: Where this node writes data to (can be NULL for NODE_OUTPUT)
 *
 * Each instance has its own context, configuration, and ring buffers.
 * Multiple instances of the same node type can exist in a pipeline.
 *
 * The node takes ownership of items from input_queue and transfers
 * ownership when pushing to output_queue.
 */
struct instance_node {
    struct node const * node;
    char * instance_name;
    struct instance_node * next;

    void * ctx;

    struct pipeline_queue * input_queue;
    struct pipeline_queue * output_queue;
};

/**
 * struct queue_list - Queue List - tracks all allocated queues for cleanup
 * @queue: Pointer to a pipeline queue
 * @next: Next queue in list
 */
struct queue_list {
    struct pipeline_queue * queue;
    struct queue_list * next;
};

/**
 * struct pipeline - Pipeline - main pipeline structure
 * @nodes: List of registered node types
 * @instance_nodes: List of instantiated nodes
 * @queue_list: List of queues for cleanup
 * @state: Current pipeline state
 *
 * Manages the collection of registered node types, instantiated nodes,
 * queues, and overall pipeline state.
 */
struct pipeline {
    struct node * nodes;
    struct instance_node * instance_nodes;
    struct queue_list * queue_list;
    enum pipeline_state state;
};


/**
 * node_registry_add - Add a node to the global registry
 * @node: Pointer to node definition (must remain valid for program lifetime)
 *
 * Nodes should register themselves using __attribute__((constructor))
 * to ensure they're available before main() runs.
 */
void
node_registry_add(struct node * node);

/**
 * node_registry_find - Find a node by name in the registry
 * @name: Node type name to search for
 *
 * Return: Pointer to node definition, or NULL if not found
 */
struct node *
node_registry_find(char const * name);

/**
 * node_registry_get_head - Get the head of the registry list for iteration
 *
 * Return: Pointer to first node in registry, or NULL if empty
 */
struct node *
node_registry_get_head(void);

/**
 * node_register - Register a node type into a pipeline
 * @main: Pipeline to register into
 * @node: Node definition to register
 * @next_node_name: name of node to insert before (NULL = append to end)
 *
 * Adds a node to the pipeline's list of available node types.
 * The node can then be instantiated via YAML configuration.
 *
 * Return: 0 on success, -1 on error
 */
__attribute__((nonnull(1, 2)))
int
node_register(struct pipeline * main, struct node * node,
        char const * next_node_name);

/**
 * pipeline_configure - Configure pipeline from YAML file
 * @main: Pipeline to configure
 * @config_path: Path to YAML configuration file
 *
 * Parses the YAML configuration to:
 *
 * * Create instance nodes based on registered node types
 * * Configure each instance with its settings
 * * Wire instances together with queues
 *
 * YAML format::
 *
 *   pipeline:
 *     - node_type_name:
 *         param1: value1
 *         param2: value2
 *         next: next_node_type_name
 *
 * Return: 0 on success, -1 on error
 */
__attribute__((nonnull(1, 2)))
int
pipeline_configure(struct pipeline * main, char const * config_path);

/**
 * pipeline_init - Initialize the pipeline
 * @main: Pipeline to initialize
 *
 * * Verifies queue connectivity between nodes
 * * Calls each instance node's init() callback
 *
 * Must be called after pipeline_configure() and before pipeline_start().
 *
 * Return: 0 on success, -1 on error
 */
__attribute__((nonnull(1)))
int
pipeline_init(struct pipeline * main);

/**
 * pipeline_start - Start the pipeline processing
 * @main: Pipeline to start
 *
 * Transitions pipeline to RUNNING state, allowing nodes to process data.
 *
 * Return: 0 on success, -1 on error
 */
__attribute__((nonnull(1)))
int
pipeline_start(struct pipeline * main);

/**
 * pipeline_stop - Stop the pipeline processing
 * @main: Pipeline to stop
 *
 * Suspends processing - pipeline can be resumed with pipeline_start().
 *
 * Return: 0 on success, -1 on error
 */
__attribute__((nonnull(1)))
int
pipeline_stop(struct pipeline * main);

/**
 * pipeline_flush - Manually flush the pipeline buffers
 * @main: Pipeline to flush
 *
 * Forces all nodes to process remaining data in their input buffers.
 * Calls each node's flush() callback in order.
 *
 * Pipeline can be resumed with pipeline_start() after flushing.
 *
 * Return: 0 on success, -1 on error
 */
__attribute__((nonnull(1)))
int
pipeline_flush(struct pipeline * main);

/**
 * pipeline_fini - Clean up and destroy the pipeline
 * @main: Pipeline to finalize
 *
 * * Flushes all buffers
 * * Calls all node fini() callbacks
 * * Destroys all instance nodes and contexts
 * * Frees all queues
 */
void
pipeline_fini(struct pipeline * main);

/**
 * pipeline_dump - Dump pipeline structure for debugging
 * @main: Pipeline to dump
 *
 * Prints the name and connection info of all instance nodes.
 */
__attribute__((nonnull(1)))
void
pipeline_dump(struct pipeline * main);
