#ifndef ISAE_MONITOR_DEPARTMENTS_H
#define ISAE_MONITOR_DEPARTMENTS_H

#include "common.h"

/* Department information structure */
typedef struct {
    const char* key;
    const char* name;
    const char* feed_url;
    department_type_t type;
} department_t;

/* Initialize department registry */
void departments_init(void);

/* Get department by key (case-insensitive) */
const department_t* departments_get_by_key(const char* key);

/* Get department by type */
const department_t* departments_get_by_type(department_type_t type);

/* Get all departments count */
size_t departments_count(void);

/* Get department at index */
const department_t* departments_get_at(size_t index);

/* Check if a key is a valid department alias */
bool departments_is_valid_alias(const char* alias, const char** out_key);

#endif /* ISAE_MONITOR_DEPARTMENTS_H */
