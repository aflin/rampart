/* 
 * Copyright Richard Tobin 1995-9.
 */

#ifndef HASH_H
#define HASH_H

typedef struct hash_entry {
    void *key;
    int key_len;
    void *value;
    struct hash_entry *next;
} HashEntryStruct;

typedef HashEntryStruct *HashEntry;
typedef struct hash_table *HashTable;

HashTable create_hash_table ARGS((int init_size));
void free_hash_table ARGS((HashTable table));
HashEntry hash_find ARGS((HashTable table, CONST void *key, int key_len));
HashEntry hash_find_or_add ARGS((HashTable table, CONST void *key, int key_len, int *foundp));
void hash_remove ARGS((HashTable table, HashEntry entry));
void hash_map ARGS((HashTable table, 
	      void (*function)ARGS((CONST HashEntryStruct *, void *)), void *arg));
int hash_count ARGS((HashTable table));

#endif /* HASH_H */
