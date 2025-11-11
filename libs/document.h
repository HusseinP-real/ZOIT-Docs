#ifndef DOCUMENT_H

#define DOCUMENT_H
#define _GNU_SOURCE
#include <pthread.h>
/**
 * This file is the header file for all the document functions. You will be tested on the functions inside markdown.h
 * You are allowed to and encouraged multiple helper functions and data structures, and make your code as modular as possible. 
 * Ensure you DO NOT change the name of document struct.
 */


typedef enum {
    LINE_NORMAL,
    LINE_ORDERED_LIST,
    LINE_UNORDERED_LIST,
    LINE_CODE,
    LINE_HEADING,
    LINE_BLOCKQUOTE,
    LINE_HORIZONTAL_RULE,
} line_type;

typedef struct line_node {
    char *content;
    size_t length;
    line_type type;
    int metadata;
    struct line_node *next;
    struct line_node *prev;
} line_node;

typedef enum {
    EDIT_INSERT,
    EDIT_DELETE,
    EDIT_SPLIT_LINE,
    EDIT_MERGE_LINE,
    EDIT_CHANGE_TYPE
} edit_type;

typedef struct edit_op {
    edit_type type;
    line_node *target;
    size_t pos;
    char *text;
    size_t len;
    line_type new_type;
    int new_metadata;
    struct edit_op *next;
} edit_op;

typedef struct {
    // TODO
    line_node *head;
    line_node *tail;
    size_t line_count;
    size_t total_length;
    uint64_t version;
    pthread_mutex_t lock;
    edit_op *pending_edits;
    edit_op *pending_edits_tail;
} document;

// Functions from here onwards.
#endif
