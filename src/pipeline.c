#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

#include "pipeline.h"


struct pipeline_queue *
pipeline_queue_create(size_t initial_capacity)
{
    struct pipeline_queue * queue;

    if (initial_capacity == 0) {
        initial_capacity = 16;  /* Default initial size */
    }

    queue = malloc(sizeof(*queue));
    if (!queue) {
        return NULL;
    }

    queue->items = malloc(sizeof(void *) * initial_capacity);
    if (!queue->items) {
        free(queue);
        return NULL;
    }

    queue->capacity = initial_capacity;
    queue->size = 0;
    queue->head = 0;

    return queue;
}

void
pipeline_queue_destroy(struct pipeline_queue * queue)
{
    if (queue) {
        free(queue->items);
        free(queue);
    }
}

int
pipeline_queue_push(struct pipeline_queue * queue, void * item)
{
    size_t tail_idx;

    if (!queue) {
        return -1;
    }

    /* Check if we need to grow */
    if (queue->size >= queue->capacity) {
        size_t new_capacity = queue->capacity * 2;
        void ** new_items = malloc(sizeof(void *) * new_capacity);
        if (!new_items) {
            return -1;  /* Allocation failed */
        }

        /* Copy existing items to new array */
        for (size_t i = 0 ; i < queue->size ; i++) {
            new_items[i] = queue->items[(queue->head + i) % queue->capacity];
        }

        free(queue->items);
        queue->items = new_items;
        queue->capacity = new_capacity;
        queue->head = 0;
    }

    /* Add item at tail */
    tail_idx = (queue->head + queue->size) % queue->capacity;
    queue->items[tail_idx] = item;
    queue->size++;

    return 0;
}

void *
pipeline_queue_pop(struct pipeline_queue * queue)
{
    void * item;

    if (!queue || queue->size == 0) {
        return NULL;
    }

    item = queue->items[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;

    return item;
}

int
pipeline_queue_empty(struct pipeline_queue * queue)
{
    return !queue || queue->size == 0;
}

size_t
pipeline_queue_size(struct pipeline_queue * queue)
{
    return queue ? queue->size : 0;
}

/* Global registry implemented as a doubly-linked list */
static struct node * registry_head = NULL;
static struct node * registry_tail = NULL;

void
node_registry_add(struct node * node)
{
    if (node == NULL) {
        return;
    }

    /* Check for duplicate registration */
    if (node_registry_find(node->name) != NULL) {
        return;
    }

    if (registry_head == NULL) {
        /* First node in registry */
        registry_head = node;
        registry_tail = node;
        node->next = NULL;
        node->prev = NULL;
    } else {
        /* Append to tail */
        registry_tail->next = node;
        node->prev = registry_tail;
        node->next = NULL;
        registry_tail = node;
    }
}

struct node *
node_registry_find(char const * name)
{
    if (name == NULL) {
        return NULL;
    }

    for (struct node * n = registry_head ; n != NULL ; n = n->next) {
        if (strcmp(n->name, name) == 0) {
            return n;
        }
    }

    return NULL;
}

struct node *
node_registry_get_head(void)
{
    return registry_head;
}


/*
 * Find a registered node by name within a pipeline
 */
static inline struct node *
pipeline_node_find(struct pipeline * main, char const * name)
{
    struct node * node;

    if (name == NULL) {
        return NULL;
    }

    for (node = main->nodes ; node != NULL ; node = node->next) {
        if (strcmp(node->name, name) == 0) {
            return node;
        }
    }

    return NULL;
}

int
node_register(struct pipeline * main, struct node * node,
        char const * next_node_name)
{
    struct node * next_node, * prev_node;

    /* Find the next node if specified */
    next_node = pipeline_node_find(main, next_node_name);
    if (next_node_name != NULL && next_node == NULL) {
        fprintf(stderr, "node_register: next node '%s' not found\n",
                next_node_name);
        return -1;
    }

    /* Initialize node's list pointers */
    node->next = next_node;
    node->prev = NULL;

    if (next_node != NULL) {
        /* Insert before next_node */
        prev_node = next_node->prev;
        node->prev = prev_node;
        next_node->prev = node;

        if (prev_node != NULL) {
            prev_node->next = node;
        } else {
            main->nodes = node; /* node is now the head */
        }
    } else {
        /* Append to the end of the list */
        if (main->nodes == NULL) {
            /* First node in the pipeline */
            main->nodes = node;
        } else {
            /* Find the last node */
            prev_node = main->nodes;
            while (prev_node->next != NULL) {
                prev_node = prev_node->next;
            }

            prev_node->next = node;
            node->prev = prev_node;
        }
    }

    return 0;
}

/*
 * Helper to serialize a YAML mapping to a string for node configuration
 */
static char *
serialize_yaml_mapping(yaml_parser_t * parser, char ** next_node_out)
{
    char * config_str = NULL;
    size_t config_len = 0;
    size_t config_capacity = 1024;
    yaml_event_t event;
    int depth = 1; /* We've already seen MAPPING_START */
    char * key = NULL;

    config_str = malloc(config_capacity);
    if (!config_str) {
        return NULL;
    }

    config_str[0] = '\0';

    while (depth > 0) {
        if (!yaml_parser_parse(parser, &event)) {
            free(config_str);
            free(key);
            return NULL;
        }

        switch (event.type) {
        case YAML_MAPPING_START_EVENT:
            depth++;
            break;

        case YAML_MAPPING_END_EVENT:
            depth--;
            if (depth == 0) {
                yaml_event_delete(&event);
                goto done;
            }

            break;

        case YAML_SCALAR_EVENT:
            if (key == NULL) {
                /* This is a key */
                key = strdup((char *)event.data.scalar.value);
                if (!key) {
                    free(config_str);
                    yaml_event_delete(&event);
                    return NULL;
                }
            } else {
                /* This is a value - check if key is "next" */
                char const * value = (char *)event.data.scalar.value;
                if (strcmp(key, "next") == 0 && next_node_out) {
                    /* Store next node name instead of adding to config */
                    *next_node_out = strdup(value);
                    if (!*next_node_out) {
                        free(config_str);
                        free(key);
                        yaml_event_delete(&event);
                        return NULL;
                    }
                } else {
                    /* This is a value - append "key: value\n" */
                    size_t needed = config_len + strlen(key) + strlen(value) + 4
                    ;

                    if (needed > config_capacity) {
                        config_capacity = needed * 2;
                        char * new_str = realloc(config_str, config_capacity);
                        if (!new_str) {
                            free(config_str);
                            free(key);
                            yaml_event_delete(&event);
                            return NULL;
                        }

                        config_str = new_str;
                    }

                    config_len += snprintf(config_str + config_len,
                            config_capacity - config_len,
                            "%s: %s\n", key, value);
                }

                free(key);
                key = NULL;
            }

            break;

        case YAML_SEQUENCE_START_EVENT: {
            /* Skip sequences for now - would need more complex serialization */
            int seq_depth = 1;
            yaml_event_delete(&event);
            while (seq_depth > 0) {
                if (!yaml_parser_parse(parser, &event)) {
                    free(config_str);
                    free(key);
                    return NULL;
                }

                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    seq_depth++;
                } else if (event.type == YAML_SEQUENCE_END_EVENT) {
                    seq_depth--;
                }

                if (seq_depth > 0) {
                    yaml_event_delete(&event);
                }
            }

            free(key);
            key = NULL;
            break;
        }

        default:
            break;
        }

        yaml_event_delete(&event);
    }

done:
    free(key);
    return config_str;
}

/* Helper structure to store next node names during parsing */
struct pending_connection {
    struct instance_node * inst;
    char * next_node_name;
    struct pending_connection * next;
};

int
pipeline_configure(struct pipeline * main, char const * config_path)
{
    FILE * fh;
    yaml_parser_t parser;
    yaml_event_t event;
    struct node * node;
    struct instance_node * inst, * prev_inst, * first_inst;
    struct pipeline_queue * queue;
    struct queue_list * queue_list_item;
    char * next_node_name = NULL;
    char * current_module_name = NULL;
    char * config_str = NULL;
    int ret = -1;
    int done = 0;
    int depth = 0;
    int in_pipeline = 0;
    int in_pipeline_item = 0;
    int instance_count = 0;
    struct pending_connection * connections = NULL;
    struct pending_connection * conn;

    if (main->state != PIPELINE_UNDEFINED) {
        fprintf(stderr, "pipeline_configure: pipeline already configured\n");
        return -1;
    }

    /* Open and parse YAML config file */
    fh = fopen(config_path, "r");
    if (!fh) {
        fprintf(stderr, "pipeline_configure: failed to open config file: %s\n",
                config_path);
        return -1;
    }

    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr,
                "pipeline_configure: failed to initialize YAML parser\n");
        fclose(fh);
        return -1;
    }

    yaml_parser_set_input_file(&parser, fh);

    prev_inst = NULL;
    first_inst = NULL;

    /* Parse the configuration and create instance nodes */
    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            fprintf(stderr, "pipeline_configure: YAML parse error\n");
            goto cleanup;
        }

        switch (event.type) {
        case YAML_MAPPING_START_EVENT:
            depth++;
            /* If we're in a pipeline item and see a mapping, this is the node
             * config */
            if (in_pipeline_item && current_module_name) {
                /* Serialize this mapping as config for the node, extracting
                 * 'next' */
                config_str = serialize_yaml_mapping(&parser, &next_node_name);
                yaml_event_delete(&event);

                /* Find the node from global registry by name */
                node = node_registry_find(current_module_name);
                if (!node) {
                    fprintf(stderr,
                            "pipeline_configure: node '%s' not found in registry\n",
                            current_module_name);
                    free(config_str);
                    config_str = NULL;
                    free(current_module_name);
                    current_module_name = NULL;
                    free(next_node_name);
                    next_node_name = NULL;
                    goto cleanup;
                }

                /* Create instance node and context */
                inst = calloc(1, sizeof(*inst));
                if (!inst) {
                    fprintf(stderr, "pipeline_configure: out of memory\n");
                    free(config_str);
                    config_str = NULL;
                    free(current_module_name);
                    current_module_name = NULL;
                    free(next_node_name);
                    next_node_name = NULL;
                    goto cleanup;
                }

                inst->node = node;

                /* Create per-instance context */
                if (node->ctx_create) {
                    inst->ctx = node->ctx_create();
                    if (!inst->ctx) {
                        fprintf(stderr,
                                "pipeline_configure: failed to create context for '%s'\n",
                                node->name);
                        free(inst);
                        free(config_str);
                        config_str = NULL;
                        free(current_module_name);
                        current_module_name = NULL;
                        free(next_node_name);
                        next_node_name = NULL;
                        goto cleanup;
                    }
                }

                inst->instance_name = malloc(strlen(node->name) + 20);
                if (!inst->instance_name) {
                    free(inst);
                    free(config_str);
                    config_str = NULL;
                    free(current_module_name);
                    current_module_name = NULL;
                    free(next_node_name);
                    next_node_name = NULL;
                    goto cleanup;
                }

                snprintf(inst->instance_name, strlen(node->name) + 20,
                        "%s.%d", node->name, instance_count++);

                /* Call node's configure callback */
                if (node->configure) {
                    ret = node->configure(inst, config_str);
                    if (ret != 0) {
                        fprintf(stderr,
                                "pipeline_configure: node '%s' configure failed\n",
                                node->name);
                        free(inst->instance_name);
                        free(inst);
                        free(config_str);
                        config_str = NULL;
                        free(current_module_name);
                        current_module_name = NULL;
                        free(next_node_name);
                        next_node_name = NULL;
                        goto cleanup;
                    }
                }

                free(config_str);
                config_str = NULL;

                /* Store the next node name for later wiring */
                if (next_node_name) {
                    conn = malloc(sizeof(*conn));
                    if (conn) {
                        conn->inst = inst;
                        conn->next_node_name = next_node_name;
                        conn->next = connections;
                        connections = conn;
                        next_node_name = NULL;     /* Ownership transferred */
                    }
                }

                /* Add to instance list */
                if (!first_inst) {
                    first_inst = inst;
                    main->instance_nodes = inst;
                } else {
                    prev_inst->next = inst;
                }

                prev_inst = inst;

                free(current_module_name);
                current_module_name = NULL;
                free(next_node_name);
                next_node_name = NULL;
                free(config_str);
                config_str = NULL;
                in_pipeline_item = 0;      /* Reset for next pipeline item */
                continue;     /* Event already deleted */
            }

            break;

        case YAML_MAPPING_END_EVENT:
            depth--;
            /* Don't reset in_pipeline_item here - it should only be reset
             * when we're done with the entire sequence item, not on nested
             * mappings */
            break;

        case YAML_SEQUENCE_START_EVENT:
            /* Sequence handling is implicit through scalar detection */
            break;

        case YAML_SEQUENCE_END_EVENT:
            if (in_pipeline) {
                in_pipeline = 0;
            }

            break;

        case YAML_SCALAR_EVENT: {
            char const * value = (char *)event.data.scalar.value;

            if (strcmp(value, "pipeline") == 0) {
                in_pipeline = 1;
            } else if (in_pipeline && current_module_name == NULL && depth > 1)
            {
                /* This is a node name (key in the pipeline mapping item) */
                current_module_name = strdup(value);
                in_pipeline_item = 1;
            }

            break;
        }

        case YAML_STREAM_END_EVENT:
            done = 1;
            break;

        default:
            break;
        }

        yaml_event_delete(&event);
    }

    /* Second pass: Wire nodes based on explicit 'next' field */
    for (conn = connections ; conn != NULL ; conn = conn->next) {
        struct instance_node * next_inst = NULL;

        /* Find the instance with matching node name */
        for (struct instance_node * search = first_inst ; search != NULL ;
             search = search->next) {
            if (search->node && strcmp(search->node->name, conn->next_node_name)
                == 0) {
                next_inst = search;
                break;
            }
        }

        if (!next_inst) {
            fprintf(stderr,
                    "pipeline_configure: node '%s' specifies next='%s' but no such node found\n",
                    conn->inst->node->name, conn->next_node_name);
            ret = -1;
            goto cleanup;
        }

        /* Create queue between current and next instance */
        queue = pipeline_queue_create(256); /* Default initial capacity */
        if (!queue) {
            fprintf(stderr, "pipeline_configure: failed to allocate queue\n");
            ret = -1;
            goto cleanup;
        }

        /* Wire up the queue */
        conn->inst->output_queue = queue;
        next_inst->input_queue = queue;

        /* Add queue to list for cleanup */
        queue_list_item = malloc(sizeof(*queue_list_item));
        if (!queue_list_item) {
            fprintf(stderr,
                    "pipeline_configure: failed to allocate queue_list_item\n");
            pipeline_queue_destroy(queue);
            ret = -1;
            goto cleanup;
        }

        queue_list_item->queue = queue;
        queue_list_item->next = main->queue_list;
        main->queue_list = queue_list_item;
    }

    main->state = PIPELINE_INITIALIZED;
    ret = 0;

cleanup:
    /* Free any remaining allocated strings */
    free(current_module_name);
    free(next_node_name);
    free(config_str);

    /* Free pending connections list */
    while (connections) {
        conn = connections;
        connections = connections->next;
        free(conn->next_node_name);
        free(conn);
    }

    yaml_parser_delete(&parser);
    fclose(fh);
    return ret;
}

int
pipeline_init(struct pipeline * main)
{
    struct instance_node * inst, * prev_inst;
    int ret;

    if (main->state != PIPELINE_INITIALIZED) {
        fprintf(stderr,
                "pipeline_init: pipeline not in INITIALIZED state\n");
        return -1;
    }

    /* Verify instance nodes were created during configuration */
    if (main->instance_nodes == NULL) {
        fprintf(stderr,
                "pipeline_init: no instance nodes (call pipeline_configure first)\n");
        return -1;
    }

    /* Initialize all instance nodes and verify ring connectivity */
    prev_inst = NULL;
    for (inst = main->instance_nodes ; inst != NULL ; inst = inst->next) {
        /* Verify that input_queue is set (can be NULL only for NODE_INPUT) */
        if (inst->input_queue == NULL && inst->node->type != NODE_INPUT) {
            fprintf(stderr,
                    "pipeline_init: instance '%s' has no input queue (and is not NODE_INPUT)\n",
                    inst->instance_name);
            return -1;
        }

        /* Link output_queue of previous instance to input_queue of current */
        if (prev_inst != NULL) {
            if (prev_inst->output_queue == NULL) {
                fprintf(stderr,
                        "pipeline_init: instance '%s' has no output queue "
                        "but is not a sink node\n",
                        prev_inst->instance_name);
                return -1;
            }

            /* Verify queue connectivity */
            if (prev_inst->output_queue != inst->input_queue) {
                fprintf(stderr,
                        "pipeline_init: queue mismatch between '%s' and '%s'\n",
                        prev_inst->instance_name, inst->instance_name);
                return -1;
            }
        }

        /* Call node's init callback if present */
        if (inst->node->init != NULL) {
            ret = inst->node->init(inst);
            if (ret != 0) {
                fprintf(stderr,
                        "pipeline_init: instance '%s' init failed: %d\n",
                        inst->instance_name, ret);
                return ret;
            }
        }

        prev_inst = inst;
    }

    main->state = PIPELINE_READY;
    return 0;
}

int
pipeline_start(struct pipeline * main)
{
    if (main->state != PIPELINE_READY && main->state != PIPELINE_RUNNING) {
        fprintf(stderr,
                "pipeline_start: pipeline not in READY state\n");
        return -1;
    }

    if (main->state == PIPELINE_RUNNING) {
        /* Already running, no-op */
        return 0;
    }

    main->state = PIPELINE_RUNNING;
    return 0;
}

int
pipeline_stop(struct pipeline * main)
{
    if (main->state != PIPELINE_RUNNING) {
        fprintf(stderr, "pipeline_stop: pipeline not running\n");
        return -1;
    }

    main->state = PIPELINE_READY;
    return 0;
}

int
pipeline_flush(struct pipeline * main)
{
    struct instance_node * inst;
    int ret;

    /* Flush all instance nodes in order.
     * Each flush callback is responsible for processing all data in its input
     * queue
     * and ensuring it flows to the next stage. */
    for (inst = main->instance_nodes ; inst != NULL ; inst = inst->next) {
        if (inst->node->flush != NULL) {
            ret = inst->node->flush(inst);
            if (ret != 0) {
                fprintf(stderr,
                        "pipeline_flush: instance '%s' flush failed: %d\n",
                        inst->instance_name, ret);
                return ret;
            }
        }
    }

    return 0;
}

void
pipeline_fini(struct pipeline * main)
{
    struct instance_node * inst, * next_inst;
    struct queue_list * queue, * next_queue;

    if (main == NULL) {
        return;
    }

    if (main->state == PIPELINE_RUNNING) {
        fprintf(stderr,
                "pipeline_fini: warning - finalizing a running pipeline\n");
        pipeline_stop(main);
    }

    /* Flush all buffers before cleanup */
    if (main->state == PIPELINE_READY) {
        pipeline_flush(main);
    }

    /* Call fini callback for all instance nodes in order */
    for (inst = main->instance_nodes ; inst != NULL ; inst = inst->next) {
        if (inst->node->fini != NULL) {
            inst->node->fini(inst);
        }
    }

    /* Free all instance nodes and their contexts */
    for (inst = main->instance_nodes ; inst != NULL ; inst = next_inst) {
        next_inst = inst->next;

        /* Destroy the context if node provides destructor */
        if (inst->ctx && inst->node->ctx_destroy) {
            inst->node->ctx_destroy(inst->ctx);
        }

        free(inst->instance_name);
        free(inst);
    }

    main->instance_nodes = NULL;

    /* Free all queues */
    for (queue = main->queue_list ; queue != NULL ; queue = next_queue) {
        next_queue = queue->next;
        pipeline_queue_destroy(queue->queue);
        free(queue);
    }

    main->queue_list = NULL;

    main->state = PIPELINE_UNDEFINED;
}

void
pipeline_dump(struct pipeline * main)
{
    struct instance_node * inst;

    printf("\nInstance nodes in pipeline order:\n");
    for (inst = main->instance_nodes ; inst != NULL ; inst = inst->next) {
        printf("  - %s (type: %s, version: %s)\n",
                inst->instance_name,
                inst->node->name,
                inst->node->version);
        printf("    input_queue:  %p\n", (void *)inst->input_queue);
        printf("    output_queue: %p\n", (void *)inst->output_queue);
    }
}
