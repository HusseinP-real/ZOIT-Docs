#define _GNU_SOURCE
#include "../libs/markdown.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <ctype.h>

#define INVALID_CURSOR_POS -1
#define DELETE_POSITION -2
#define OUTDATED_VERSION -3
#define SUCCESS 0

static bool line_is_ordered_list(const line_node *ln);
static int find_line_and_offset(document *doc, size_t global_pos, line_node **target_line_out, size_t *offset_in_line_out);
static void apply_insert_op(document *doc, edit_op *op);
static void apply_delete_op(document *doc, edit_op *op);
static void apply_split_line_op(document *doc, edit_op *op);
static void apply_merge_line_op(document *doc, edit_op *op);
static void apply_all_pending_edits(document *doc);
static int check_if_start_of_line(document *doc, size_t pos, bool *is_start_of_line);

// helper functions
static bool line_is_ordered_list(const line_node *ln) {
    if (!ln || !ln->content || ln->length < 3) return false;
    if (!isdigit((unsigned char)ln->content[0])) return false;
    if (ln->content[1] != '.' || ln->content[2] != ' ') return false;
    return true;
}

// free line_nodes
static void free_line_nodes(line_node *head) {
    line_node *current = head;
    while (current) {
        line_node *next = current->next;
        free(current->content);
        free(current);
        current = next;
    }
}

// free edit_ops
static void free_edit_ops(edit_op *head) {
    edit_op *current = head;
    while (current) {
        edit_op *next = current->next;
        if (current->text) {
            free(current->text);
        }
        free(current);
        current = next;
    }
}

static int find_line_and_offset(document *doc, size_t global_pos, line_node **target_line_out, size_t *offset_in_line_out) {
    if (!doc || !target_line_out || !offset_in_line_out) {
        return INVALID_CURSOR_POS;
    }

    line_node *current_ln = doc->head;
    size_t chars_before_current_line = 0;

    if (doc->head == NULL) { 
        if (global_pos == 0) {
            *target_line_out = NULL;
            *offset_in_line_out = 0;
            return SUCCESS;
        }
        return INVALID_CURSOR_POS;
    }

    while (current_ln) {
        size_t is_last = (current_ln->next == NULL);
        
        if (global_pos <= chars_before_current_line + current_ln->length) {
            *target_line_out = current_ln;
            *offset_in_line_out = global_pos - chars_before_current_line;
            return SUCCESS;
        }
        
        if (!is_last) {
            chars_before_current_line += current_ln->length + 1;
        } else {
            chars_before_current_line += current_ln->length;
        }
        
        current_ln = (struct line_node *)current_ln->next;
    }
    
    if (global_pos == chars_before_current_line) {
        *target_line_out = NULL;
        *offset_in_line_out = 0;
        return SUCCESS;
    }
    return INVALID_CURSOR_POS;
}

// apply insert
static void apply_insert_op(document *doc, edit_op *op) {
    line_node *target_line = op->target;
    size_t ins_pos_in_line = op->pos;
    char *text_to_insert = op->text;
    size_t text_len = op->len;

    bool need_retarget = false;
    if (!doc->head) {
        need_retarget = true;
    } else {
        line_node *current = doc->head;
        bool found = false;
        while(current){
            if(current == target_line){
                found = true;
                break;
            }
            current = (line_node*)current->next;
        }
        if(!found) need_retarget = true;
    }
    if (need_retarget) {
        target_line = doc->head;
        ins_pos_in_line  = 0;
        op->target = doc->head;
        op->pos = 0;
    }

    if (target_line == NULL) { 
        if (doc->head == NULL && ins_pos_in_line == 0) {
            char *new_content = malloc(text_len + 1);
            if (!new_content) return;
            memcpy(new_content, text_to_insert, text_len);
            new_content[text_len] = '\0';

            line_node *new_ln = malloc(sizeof(line_node));
            if (!new_ln) {
                free(new_content);
                return;
            }
            
            new_ln->content = new_content;
            new_ln->length = text_len;
            new_ln->type = LINE_NORMAL;
            new_ln->metadata = 0;
            new_ln->prev = NULL;
            new_ln->next = NULL;

            doc->head = new_ln;
            doc->tail = new_ln;
            doc->line_count = 1;
            doc->total_length = text_len;
        } else {
            target_line = doc->head;
            ins_pos_in_line = 0;
        }
    }
    if (target_line != NULL) {
        if (ins_pos_in_line > target_line->length) return;
        
        char *old_content = target_line->content;
        size_t old_len = target_line->length;
        size_t new_total_len = old_len + text_len;
        char *new_content_buf = malloc(new_total_len + 1);
        if (!new_content_buf) return;

        memcpy(new_content_buf, old_content, ins_pos_in_line);
        memcpy(new_content_buf + ins_pos_in_line, text_to_insert, text_len);
        memcpy(new_content_buf + ins_pos_in_line + text_len,
            old_content + ins_pos_in_line,
            old_len - ins_pos_in_line);
        new_content_buf[new_total_len] = '\0';
        
        free(old_content);
        target_line->content = new_content_buf;
        target_line->length = new_total_len;
        doc->total_length += text_len;
    }
}

// apply delete
static void apply_delete_op(document *doc, edit_op *op) {
    line_node *target_line = op->target;
    size_t del_pos_in_line = op->pos;
    size_t len_to_del = op->len;

    if (!target_line || del_pos_in_line > target_line->length) return;

    size_t actual_del_len = len_to_del;
    if (del_pos_in_line + actual_del_len > target_line->length) {
        actual_del_len = target_line->length - del_pos_in_line;
    }
    if (actual_del_len == 0) return;

    doc->total_length -= actual_del_len;

    if (actual_del_len == target_line->length && del_pos_in_line == 0) {
        free(target_line->content);
        target_line->content = malloc(1);
        if (!target_line->content) {
            target_line->length = 0;
        } else {
            target_line->content[0] = '\0';
            target_line->length = 0;
        }
        return;
    } else if (del_pos_in_line + actual_del_len == target_line->length && target_line->next) {
        line_node *nxt = (line_node*)target_line->next;
        
        size_t new_len = del_pos_in_line + nxt->length;
        char *new_content = realloc(target_line->content, new_len + 1);
        
        if (!new_content) return;

        memcpy(new_content + del_pos_in_line, nxt->content, nxt->length);
        new_content[new_len] = '\0';
        
        target_line->content = new_content;
        target_line->length = new_len;
        
        target_line->next = nxt->next;
        if (nxt->next) {
            ((line_node*)nxt->next)->prev = (struct line_node*)target_line;
        } else {
            doc->tail = target_line;
        }
        
        free(nxt->content);
        free(nxt);
        
        doc->line_count--;
    } else {
        char *content = target_line->content;
        size_t old_len = target_line->length;
        memmove(content + del_pos_in_line, 
                content + del_pos_in_line + actual_del_len,
                old_len - (del_pos_in_line + actual_del_len));
        
        size_t new_len = old_len - actual_del_len;
        char *shrunk_content = realloc(content, new_len + 1);
        if (!shrunk_content && new_len > 0) {
            return;
        }
        if (!shrunk_content && new_len == 0) {
            free(content); 
            shrunk_content = malloc(1); 
            if(shrunk_content) shrunk_content[0] = '\0'; else return;
        }
        target_line->content = shrunk_content;
        target_line->length = new_len;
        target_line->content[new_len] = '\0';
    }
}

// split line
static void apply_split_line_op(document *doc, edit_op *op) {
    line_node *line_to_split = op->target;
    size_t split_pos_in_line = op->pos;

    if (line_to_split == NULL) {
        if (doc->head == NULL && split_pos_in_line == 0) {
            line_node *first_new = malloc(sizeof(line_node));
            if (!first_new) return;
            first_new->content = malloc(1);
            if(first_new->content) first_new->content[0] = '\0';
            else {
                free(first_new);
                return;
            }
            first_new->length = 0; 
            first_new->type = LINE_NORMAL;
            first_new->metadata = 0;
            first_new->prev = NULL;

            line_node *second_new = malloc(sizeof(line_node));
            if (!second_new) {
                free(first_new->content);
                free(first_new);
                return;
            }
            second_new->content = malloc(1);
            if(second_new->content) second_new->content[0] = '\0';
            else {
                free(second_new);
                free(first_new->content);
                free(first_new);
                
            }
            second_new->length = 0;
            second_new->type = op->new_type;
            second_new->metadata = op->new_metadata;
            second_new->prev = (struct line_node*)first_new; 
            second_new->next = NULL;

            first_new->next = (struct line_node*)second_new;
            doc->head = first_new;
            doc->tail = second_new;
            doc->line_count = 2;
        } else {
            return;
        }
        return;
    }

    if (split_pos_in_line > line_to_split->length) return;

    size_t second_part_len = line_to_split->length - split_pos_in_line;
    char *second_part_str = malloc(second_part_len + 1);
    if (!second_part_str) return;
    memcpy(second_part_str, line_to_split->content + split_pos_in_line, second_part_len);
    second_part_str[second_part_len] = '\0';

    line_node *new_line_after_split = malloc(sizeof(line_node));
    if (!new_line_after_split) {
        free(second_part_str);
        return;
    }
    new_line_after_split->content = second_part_str;
    new_line_after_split->length = second_part_len;
    new_line_after_split->type = op->new_type;
    new_line_after_split->metadata = op->new_metadata;

    // Truncate original line
    size_t first_part_new_len = split_pos_in_line;
    char *temp_first_content = realloc(line_to_split->content, first_part_new_len + 1);
    if (!temp_first_content && first_part_new_len > 0) { 
        // Preserve old buffer on failure
        free(new_line_after_split->content);
        free(new_line_after_split);
        return; 
    }
    if (!temp_first_content && first_part_new_len == 0) {
        free(line_to_split->content);
        temp_first_content = malloc(1);
        if(temp_first_content) temp_first_content[0] = '\0'; else {
            free(new_line_after_split->content);
            free(new_line_after_split);
            return;
        }
    }
    line_to_split->content = temp_first_content;
    line_to_split->length = first_part_new_len;
    line_to_split->content[first_part_new_len] = '\0';
    
    // Link new_line_after_split
    new_line_after_split->next = line_to_split->next;
    new_line_after_split->prev = (struct line_node*)line_to_split;
    if (line_to_split->next) {
        ((line_node*)line_to_split->next)->prev = (struct line_node*)new_line_after_split;
    } else {
        doc->tail = new_line_after_split;
    }
    line_to_split->next = (struct line_node*)new_line_after_split;
    doc->line_count++;
}

// merge line
static void apply_merge_line_op(document *doc, edit_op *op) {
    line_node *target_line = op->target;
    
    if (!target_line || !target_line->next) {
        return;
    }
    
    line_node *next_line = (line_node*)target_line->next;
    
    // Calculate new length
    size_t new_len = target_line->length + next_line->length;
    
    char *new_content = realloc(target_line->content, new_len + 1);
    if (!new_content) {
        return;
    }
    
    memcpy(new_content + target_line->length, next_line->content, next_line->length);
    new_content[new_len] = '\0';
    
    target_line->content = new_content;
    target_line->length = new_len;
    
    // Fix links
    target_line->next = next_line->next;
    if (next_line->next) {
        ((line_node*)next_line->next)->prev = (struct line_node*)target_line;
    } else {
        doc->tail = target_line;
    }

    free(next_line->content);
    free(next_line);

    doc->line_count--;
}

// edit pending edit
static void apply_all_pending_edits(document *doc) {
    typedef struct { line_node *ln; size_t pos, len; } del_region;
    del_region dels[16];
    size_t nd = 0;

    edit_op *cur = doc->pending_edits;
    while (cur) {
        edit_op *next = cur->next;

        if (cur->type == EDIT_DELETE) {
            if (nd < 16)
                dels[nd++] = (del_region){ cur->target, cur->pos, cur->len };
            apply_delete_op(doc, cur);
        }
        else if (cur->type == EDIT_INSERT) {
            for (size_t i = 0; i < nd; i++) {
                if (cur->target == dels[i].ln
                    && cur->pos  >= dels[i].pos
                    && cur->pos  <  dels[i].pos + dels[i].len)
                {
                    cur->pos = dels[i].pos;
                    break;
                }
            }
            apply_insert_op(doc, cur);
        }
        else {           
            switch (cur->type) {
              case EDIT_SPLIT_LINE:  apply_split_line_op(doc, cur); break;
              case EDIT_MERGE_LINE:  apply_merge_line_op(doc, cur); break;
              case EDIT_CHANGE_TYPE:
                cur->target->type     = cur->new_type;
                cur->target->metadata = cur->new_metadata;
                break;
              default: break;
            }
        }

        if (cur->text) free(cur->text);
        free(cur);
        cur = next;
    }

    line_node *ln = doc->head;
    while (ln) {
        line_node *next = (line_node*)ln->next;
        if (ln->length == 0 && ln->content && ln->content[0] == '\0' && ln->metadata == 0) {
            if (ln->prev) ((line_node*)ln->prev)->next = ln->next;
            else doc->head = (line_node*)ln->next;
            if (ln->next) ((line_node*)ln->next)->prev = ln->prev;
            else doc->tail = (line_node*)ln->prev;
            free(ln->content);
            free(ln);
            doc->line_count--;
        }
        ln = next;
    }

    doc->pending_edits = NULL;
    doc->pending_edits_tail = NULL;
}

// === Init and Free ===
document *markdown_init(void) {
    document *doc = malloc(sizeof(document));
    if (!doc) {
        return NULL;
    }
    doc->head = NULL;
    doc->tail = NULL;
    doc->line_count = 0;
    doc->total_length = 0;
    doc->version = 0;
    pthread_mutex_init(&doc->lock, NULL);
    doc->pending_edits = NULL;
    doc->pending_edits_tail = NULL;
    
    return doc;
}

// free doc
void markdown_free(document *doc) {
    if (!doc) {
        return;
    }
    pthread_mutex_destroy(&doc->lock);
    free_line_nodes(doc->head);
    doc->head = NULL;
    doc->tail = NULL; 
    free_edit_ops(doc->pending_edits);
    doc->pending_edits = NULL;
    free(doc);
}

// === Edit Commands ===
int markdown_insert(document *doc, uint64_t version, size_t pos, const char *content) {
    if (!doc || !content) {
        return INVALID_CURSOR_POS;
    }

    const char *first_newline = strchr(content, '\n');
    if (first_newline && strchr(first_newline + 1, '\n')) {
        return INVALID_CURSOR_POS;
    }

    pthread_mutex_lock(&doc->lock);

    if (version != doc->version) {
        pthread_mutex_unlock(&doc->lock);
        return OUTDATED_VERSION;
    }

    line_node *target_node = NULL;
    size_t offset_in_node = 0;
    int find_result = find_line_and_offset(doc, pos, &target_node, &offset_in_node);

    if (find_result != SUCCESS) {
        pthread_mutex_unlock(&doc->lock);
        return INVALID_CURSOR_POS;
    }

    edit_op *new_op = malloc(sizeof(edit_op));
    if (!new_op) {
        pthread_mutex_unlock(&doc->lock);
        return INVALID_CURSOR_POS;
    }

    new_op->type = EDIT_INSERT;
    new_op->target = target_node;
    new_op->pos = offset_in_node;
    new_op->text = strdup(content);
    if (!new_op->text) {
        free(new_op);
        pthread_mutex_unlock(&doc->lock);
        return INVALID_CURSOR_POS;
    }
    new_op->len = strlen(content);
    new_op->new_type = LINE_NORMAL;
    new_op->new_metadata = 0;
    new_op->next = NULL;

    if (!doc->pending_edits) {
        doc->pending_edits = new_op;
    } else {
        doc->pending_edits_tail->next = new_op;
    }
    doc->pending_edits_tail = new_op;
    
    pthread_mutex_unlock(&doc->lock);
    return SUCCESS;
}

// delete line
int markdown_delete(document *doc, uint64_t version, size_t pos, size_t len) {
    if (!doc) {
        return INVALID_CURSOR_POS;
    }
    if (len == 0) {
        return SUCCESS;
    }

    pthread_mutex_lock(&doc->lock);

    if (version != doc->version) {
        pthread_mutex_unlock(&doc->lock);
        return OUTDATED_VERSION;
    }

    line_node *current_line_node;
    size_t offset_in_first_node;

    int find_res = find_line_and_offset(doc, pos, &current_line_node, &offset_in_first_node);

    if (find_res != SUCCESS) {
        pthread_mutex_unlock(&doc->lock);
        return INVALID_CURSOR_POS;
    }

    if (current_line_node == NULL) { 
        pthread_mutex_unlock(&doc->lock);
        return SUCCESS;
    }

    if (offset_in_first_node == current_line_node->length && current_line_node->next) {
        line_node *next_line = (line_node*)current_line_node->next;
        size_t chars_to_delete = len - 1;

        if (chars_to_delete > 0) {
            edit_op *del_op = malloc(sizeof(edit_op));
            if (!del_op) { pthread_mutex_unlock(&doc->lock); return INVALID_CURSOR_POS; }
            del_op->type = EDIT_DELETE;
            del_op->target = next_line;
            del_op->pos = 0;
            del_op->len = chars_to_delete > next_line->length ? next_line->length : chars_to_delete;
            del_op->text = NULL;
            del_op->new_type = LINE_NORMAL;
            del_op->new_metadata = 0;
            del_op->next = NULL;

            if (!doc->pending_edits)
                doc->pending_edits = del_op;
            else
                doc->pending_edits_tail->next = del_op;
            doc->pending_edits_tail = del_op;

            if (chars_to_delete > next_line->length) {
                pthread_mutex_unlock(&doc->lock);
                return markdown_delete(doc, version, pos + 1 + next_line->length,
                                       chars_to_delete - next_line->length);
            }
        }

        edit_op *merge_op = malloc(sizeof(edit_op));
        if (!merge_op) { pthread_mutex_unlock(&doc->lock); return INVALID_CURSOR_POS; }
        merge_op->type = EDIT_MERGE_LINE;
        merge_op->target = current_line_node;
        merge_op->pos = 0;
        merge_op->len = 0;
        merge_op->text = NULL;
        merge_op->new_type = LINE_NORMAL;
        merge_op->new_metadata = 0;
        merge_op->next = NULL;

        if (!doc->pending_edits)
            doc->pending_edits = merge_op;
        else
            doc->pending_edits_tail->next = merge_op;
        doc->pending_edits_tail = merge_op;

        pthread_mutex_unlock(&doc->lock);
        return SUCCESS;
    }

    size_t remaining_len_to_delete = len;
    size_t current_offset_in_node = offset_in_first_node;

    while (current_line_node != NULL && remaining_len_to_delete > 0) {
        size_t chars_available_in_node_from_offset = current_line_node->length - current_offset_in_node;
        size_t chars_to_delete_this_iteration = (remaining_len_to_delete < chars_available_in_node_from_offset) ?
                                                remaining_len_to_delete : chars_available_in_node_from_offset;

        if (chars_to_delete_this_iteration > 0) {
            edit_op *op = malloc(sizeof(edit_op));
            if (!op) {
                pthread_mutex_unlock(&doc->lock);
                return INVALID_CURSOR_POS; 
            }
            op->type = EDIT_DELETE;
            op->target = current_line_node;
            op->pos = current_offset_in_node; 
            op->len = chars_to_delete_this_iteration;
            op->text = NULL; 
            op->new_type = LINE_NORMAL; 
            op->new_metadata = 0; 
            op->next = NULL;

            // Append to pending_edits queue using tail pointer
            if (!doc->pending_edits) {
                doc->pending_edits = op;
            } else {
                doc->pending_edits_tail->next = op;
            }
            doc->pending_edits_tail = op;
            
            if (current_offset_in_node + chars_to_delete_this_iteration == current_line_node->length && 
                current_line_node->next && remaining_len_to_delete > chars_to_delete_this_iteration) {
                // Add a merge operation
                edit_op *merge_op = malloc(sizeof(edit_op));
                if (!merge_op) {
                    pthread_mutex_unlock(&doc->lock);
                    return INVALID_CURSOR_POS;
                }
                merge_op->type = EDIT_MERGE_LINE;
                merge_op->target = current_line_node;
                merge_op->pos = 0;
                merge_op->len = 0;
                merge_op->text = NULL;
                merge_op->new_type = LINE_NORMAL;
                merge_op->new_metadata = 0;
                merge_op->next = NULL;
                
                // Append to pending_edits using tail pointer
                if (!doc->pending_edits) {
                    doc->pending_edits = merge_op;
                } else {
                    doc->pending_edits_tail->next = merge_op;
                }
                doc->pending_edits_tail = merge_op;
                remaining_len_to_delete -= 1;
            }
        }

        remaining_len_to_delete -= chars_to_delete_this_iteration;
        if (remaining_len_to_delete > 0 && current_line_node->next) {
            current_line_node = (struct line_node *)current_line_node->next; 
            current_offset_in_node = 0;
        } else {
            break;
        }
    }

    pthread_mutex_unlock(&doc->lock);
    return SUCCESS;
}

// === Formatting Commands ===
int markdown_newline(document *doc, uint64_t version, size_t pos) {
    if (!doc) {
        return INVALID_CURSOR_POS;
    }

    pthread_mutex_lock(&doc->lock);

    if (version != doc->version) {
        pthread_mutex_unlock(&doc->lock);
        return OUTDATED_VERSION;
    }

    line_node *target_node = NULL;
    size_t offset_in_node = 0;

    int find_result = find_line_and_offset(doc, pos, &target_node, &offset_in_node);

    if (find_result != SUCCESS) {
        pthread_mutex_unlock(&doc->lock);
        
        return INVALID_CURSOR_POS; 
    }
    
    edit_op *new_op = malloc(sizeof(edit_op));
    if (!new_op) {
        pthread_mutex_unlock(&doc->lock);
        return INVALID_CURSOR_POS; 
    }

    new_op->type = EDIT_SPLIT_LINE;
    new_op->target = target_node; 
    new_op->pos = offset_in_node;
    new_op->text = NULL;
    new_op->len = 0;
    new_op->new_type = LINE_NORMAL;
    new_op->new_metadata = 1;
    new_op->next = NULL;

    if (!doc->pending_edits) {
        doc->pending_edits = new_op;
    } else {
        doc->pending_edits_tail->next = new_op;
    }
    doc->pending_edits_tail = new_op;

    pthread_mutex_unlock(&doc->lock);
    return SUCCESS;
}

// check 1st line
static int check_if_start_of_line(document *doc, size_t pos, bool *is_start_of_line) {
    if (pos == 0) {
        *is_start_of_line = true;
        return SUCCESS;
    }

    line_node *prev_char_line = NULL;
    size_t offset_in_prev_char_line = 0;

    
    int find_res = find_line_and_offset(doc, pos - 1, &prev_char_line, &offset_in_prev_char_line);

    if (find_res != SUCCESS) {
        return INVALID_CURSOR_POS; 
    }

    if (prev_char_line == NULL) {
        *is_start_of_line = true;
        return SUCCESS;
    }

    if (prev_char_line->content &&
        offset_in_prev_char_line < prev_char_line->length) {
        if (prev_char_line->content[offset_in_prev_char_line] == '\n') {
            *is_start_of_line = true;
        } else {
            *is_start_of_line = false;
        }
        return SUCCESS;
    } else {
        *is_start_of_line = true;
        return SUCCESS;
    }
}

// insert heading
int markdown_heading(document *doc, uint64_t version, size_t level, size_t pos) {
    if (!doc || level == 0 || level > 6) {
        return INVALID_CURSOR_POS;
    }

    line_node *target_node_check = NULL;
    size_t offset_in_node_check = 0;
    if (find_line_and_offset(doc, pos, &target_node_check, &offset_in_node_check) != SUCCESS) {
        return INVALID_CURSOR_POS;
    }

    bool is_start_of_line = false;
    int prepend_check = check_if_start_of_line(doc, pos, &is_start_of_line);

    if (prepend_check != SUCCESS && pos != 0) {
        return INVALID_CURSOR_POS;
    }

    int result = SUCCESS;
    if (pos > 0 && prepend_check == SUCCESS && !is_start_of_line) {
        result = markdown_insert(doc, version, pos, "\n");
        if (result != SUCCESS) {
            return result; 
        }
    }

    char prefix[8];
    memset(prefix, '#', level);
    prefix[level] = ' ';
    prefix[level + 1] = '\0';

    result = markdown_insert(doc, version, pos, prefix);
    if (result != SUCCESS) return result;

    return SUCCESS;
}

// *...*
int markdown_bold(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc || start > end) {
        return INVALID_CURSOR_POS;
    }

    int result = markdown_insert(doc, version, end, "**");
    if (result != SUCCESS) {
        return result;
    }
    result = markdown_insert(doc, version, start, "**");
    return result;
}

// *...*
int markdown_italic(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc || start > end) {
        return INVALID_CURSOR_POS;
    }
    int result = markdown_insert(doc, version, end, "*");
    if (result != SUCCESS) {
        return result;
    }
    result = markdown_insert(doc, version, start, "*");
    return result;
}

// insert quote
int markdown_blockquote(document *doc, uint64_t version, size_t pos) {
    if (!doc) return INVALID_CURSOR_POS;

    pthread_mutex_lock(&doc->lock);
    apply_all_pending_edits(doc);
    pthread_mutex_unlock(&doc->lock);

    bool is_start = false;
    if (check_if_start_of_line(doc, pos, &is_start) != SUCCESS) {
        return INVALID_CURSOR_POS;
    }
    int result = SUCCESS;
    if (pos > 0 && !is_start) {
        result = markdown_insert(doc, version, pos, "\n");
        if (result != SUCCESS) return result;
    }

    line_node *ln = NULL;
    size_t offset = 0;
    if (find_line_and_offset(doc, pos, &ln, &offset) != SUCCESS) {
        return INVALID_CURSOR_POS;
    }
    size_t insert_pos = pos - offset;
    result = markdown_insert(doc, version, insert_pos, "> ");
    if (result != SUCCESS) return result;

    pthread_mutex_lock(&doc->lock);
    apply_all_pending_edits(doc);
    pthread_mutex_unlock(&doc->lock);
    return SUCCESS;
}

// insert -
int markdown_unordered_list(document *doc, uint64_t version, size_t pos) {
    if (!doc) return INVALID_CURSOR_POS;
    pthread_mutex_lock(&doc->lock);
    if (version != doc->version) {
        pthread_mutex_unlock(&doc->lock);
        return OUTDATED_VERSION;
    }
    line_node *ln = NULL;
    size_t offset = 0;
    if (find_line_and_offset(doc, pos, &ln, &offset) != SUCCESS) {
        pthread_mutex_unlock(&doc->lock);
        return INVALID_CURSOR_POS;
    }
    size_t insert_pos = pos - offset;
    pthread_mutex_unlock(&doc->lock);

    // Prefix "- " at the beginning of the line
    int result = markdown_insert(doc, version, insert_pos, "- ");
    if (result != SUCCESS) return result;

    return SUCCESS;
}

// 1. 2. 3. ...
int markdown_ordered_list(document *doc, uint64_t version, size_t pos) {
    if (!doc) return INVALID_CURSOR_POS;
    pthread_mutex_lock(&doc->lock);
    if (version != doc->version) {
        pthread_mutex_unlock(&doc->lock);
        return OUTDATED_VERSION;
    }
    apply_all_pending_edits(doc);
    line_node *ln = NULL;
    size_t offset = 0;
    if (find_line_and_offset(doc, pos, &ln, &offset) != SUCCESS) {
        pthread_mutex_unlock(&doc->lock);
        return INVALID_CURSOR_POS;
    }
    int number = 1;
    line_node *prev_ln = ln->prev ? (line_node*)ln->prev : NULL;
    while (prev_ln && line_is_ordered_list(prev_ln)) {
        number++;
        prev_ln = (line_node*)prev_ln->prev;
    }
    size_t insert_pos = pos - offset;
    pthread_mutex_unlock(&doc->lock);
    char prefix[4];
    prefix[0] = '0' + number;
    prefix[1] = '.';
    prefix[2] = ' ';
    prefix[3] = '\0';
    int result = markdown_insert(doc, version, insert_pos, prefix);
    if (result != SUCCESS) return result;

    return SUCCESS;
}

// `code`
int markdown_code(document *doc, uint64_t version, size_t start, size_t end) {
    pthread_mutex_lock(&doc->lock);
    apply_all_pending_edits(doc);
    pthread_mutex_unlock(&doc->lock);
    if (!doc || start > end) {
        return INVALID_CURSOR_POS;
    }
    int result = markdown_insert(doc, version, end, "`");
    if (result != SUCCESS) return result;
    return markdown_insert(doc, version, start, "`");
}

// \n---\n
int markdown_horizontal_rule(document *doc, uint64_t version, size_t pos) {
    pthread_mutex_lock(&doc->lock);
    apply_all_pending_edits(doc);
    pthread_mutex_unlock(&doc->lock);

    int r;
    r = markdown_insert(doc, version, pos, "\n");
    if (r != SUCCESS) return r;
    r = markdown_insert(doc, version, pos, "---");
    if (r != SUCCESS) return r;

    return markdown_insert(doc, version, pos, "\n");
}

// [link](url)
int markdown_link(document *doc, uint64_t version, size_t start, size_t end, const char *url) {
    if (!doc || !url || start >= end) {
        return INVALID_CURSOR_POS;
    }

    // "](" + url + ")"
    size_t url_len = strlen(url);
    char *suffix = malloc(url_len + 5); 
    if (!suffix) {
        return INVALID_CURSOR_POS;
    }
    sprintf(suffix, "](%s)", url);

    int result = markdown_insert(doc, version, end, suffix);
    free(suffix);
    if (result != SUCCESS) {
        return result;
    }
    result = markdown_insert(doc, version, start, "[");
    return result;
}

// === Utilities ===
void markdown_print(const document *doc, FILE *stream) {
    if (!doc || !stream) return;
    
    pthread_mutex_lock((pthread_mutex_t*)&doc->lock);

    line_node *current_ln = doc->head;
    while (current_ln != NULL) {
        if (current_ln->content) {
            fwrite(current_ln->content, 1, current_ln->length, stream);
        }
        fputc('\n', stream);
        current_ln = (line_node*)current_ln->next;
    }

    pthread_mutex_unlock((pthread_mutex_t*)&doc->lock);
}

char *markdown_flatten(const document *doc) {
    if (!doc) return NULL;

    pthread_mutex_lock((pthread_mutex_t*)&doc->lock);

    if (doc->head == NULL) {
        pthread_mutex_unlock((pthread_mutex_t*)&doc->lock);
        char *empty_str = malloc(1);
        if (empty_str) empty_str[0] = '\0';
        return empty_str;
    }

    size_t buffer_size = doc->total_length;
    if (doc->line_count > 0) {
        buffer_size += (doc->line_count - 1); 
    }
    buffer_size += 1; 

    char *result_buf = malloc(buffer_size);
    if (!result_buf) {
        pthread_mutex_unlock((pthread_mutex_t*)&doc->lock);
        return NULL; 
    }

    char *current_pos_in_buf = result_buf;
    line_node *current_ln = doc->head;
    bool first_line = true;

    while (current_ln != NULL) {
        if (!first_line) {
            *current_pos_in_buf++ = '\n';
        }
        if (current_ln->content && current_ln->length > 0) {
             memcpy(current_pos_in_buf, current_ln->content, current_ln->length);
             current_pos_in_buf += current_ln->length;
        }
        first_line = false;
        current_ln = (line_node*)current_ln->next;
    }
    *current_pos_in_buf = '\0';

    pthread_mutex_unlock((pthread_mutex_t*)&doc->lock);
    return result_buf;
}

// version++
void markdown_increment_version(document *doc) {
    if (!doc) return;
    pthread_mutex_lock(&doc->lock);
    apply_all_pending_edits(doc);
    doc->version++;
    pthread_mutex_unlock(&doc->lock);
}

void markdown_commit(document *doc) {
    if (!doc) return;
    pthread_mutex_lock(&doc->lock);
    apply_all_pending_edits(doc); 
    doc->version++;
    pthread_mutex_unlock(&doc->lock);
}