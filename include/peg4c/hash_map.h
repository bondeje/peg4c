
/**
 * This is an implementation of a type-safe unordered hash map using open 
 * addressing with linear probing and a simple sentinel for indicating 
 * removed items
 *      FEATURES:
 *          perfectly fine to store const values (uses memcpy)
 *          minimal requirements for simple templating:
 *              declaration: HASH_MAP(key_type, value_type) map;
 *              initialization: HASH_MAP_INIT(key_type, value_type)(&map, 0)
 *              no other accesses require specifying the key_type and 
 *                  value_type. All other functionality are in member function 
 *                  calls to "map"
 *      WARNINGS:
 *          insertion order is NOT maintained
 *          key and value types must be single identifiers (structs, enums, 
 *              unions, pointers and base C types must be typedef'd)
 *          to any handlers or setting values, data is copied by value so 
 *              anything larger than a pointer should probably just be a 
 *              pointer
 *          if HASH_FUNC or KEY_COMP are not specified, default will compare 
 *              directly by value meaning pointers will be compared by address 
 *              and not by value. This is similar to C++ unordered_map 
 *              especially where passing a raw c-string (char *), the contents 
 *              are not used, i.e. different pointers to the same string will 
 *              not match
 *          if either HASH_FUNC or KEY_COMP are implemented, the other should 
 *              be as well so that:
 *              KEY_COMP(key1, key2) == 0 implies 
 *              HASH_FUNC(key1, any_value) == HASH_FUNC(key2, any_value)
 * 
 * To include in a header and get access to macros and declarations and NO 
 *      implementations, simply do not define either KEY_TYPE or VALUE_TYPE
 * 
 * To use an implementation: 
 *  set #defines
 *  include this header
 * 
 * the following #defines configure the hash map:
 *      required:
 *          KEY_TYPE
 *          VALUE_TYPE
 *      optional:
 *          KEY_COMP - The function used to compare two different KEY_TYPE 
 *              values. If not specified a default is built
 *              required function prototype:
 *                  int KEY_COMP(KEY_TYPE key1, KEY_TYPE key2)
 *                  returns 0 if key1 == key2 and non-zero if key1 != key2
 *          HASH_FUNC - The function used to compute hashes of the KEY_TYPE 
 *              values. If not specified a default is built
 *              required function prototype:
 *                  size_t HASH_FUNC(KEY_TYPE key, size_t hash_size)
 *                  returns a value in range [0, hash_size)
 *          HASH_FILL_RESIZE_RATIO - ratio above which the hash_map will 
 *              re-size and re-insert all entries into hash map. Defaults to 2 / 5
 * 
 */

#ifndef HASH_MAP_H
#define HASH_MAP_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h> // for debug print statements
#include "peg4c/utils.h"
#include "peg4c/mempool.h"

typedef enum hash_map_err {
    HM_SUCCESS = 0,
    HM_KEY_NOT_FOUND,
    HM_MALLOC_MAP_FAILURE,
    HM_MALLOC_PAIR_FAILURE,
    HM_INSERTION_FAILURE
} hash_map_err;

#define HASH_DEFAULT_SIZE 127

/* sentinel */
#define HASH_PAIR_REMOVED ((void *)1)

#define HASH_MAP_INIT(k, v) CAT(CAT3(k, _, v), _init)

#define HASH_FUNCTION(type) CAT(type, _hash)
#define KEY_COMPARE(type) CAT(type, _comp)
#define HASH_MAP(k, v) CAT(CAT3(k, _, v), _map)
#define HASH_MAP_CLASS(k, v) CAT(CAT3(k, _, v), _class)

#include <peg4c/hash_utils.h>

#endif // HASH_MAP_H

/* generate the implementation */
#if defined(KEY_TYPE) && defined(VALUE_TYPE)

#ifndef HASH_FILL_RESIZE_RATIO
#define HASH_FILL_RESIZE_RATIO .5
#endif

#ifndef HASH_PAIR_POOL_COUNT
#define HASH_PAIR_POOL_COUNT 256
#endif

#define HASH_COMBO CAT3(KEY_TYPE, _, VALUE_TYPE)
#define HASH_PAIR CAT(HASH_COMBO, _pair)
#define HASH_MAP_TYPE CAT(HASH_COMBO, _map)

/**
 * @brief default hash function. copies bytes into an automatic buffer and 
 * applies a basic string hash
 */
#ifndef HASH_FUNC
static size_t CAT(KEY_TYPE, _hash)(KEY_TYPE key, size_t bin_size) {
    char bytes[sizeof(KEY_TYPE)+1];
    bytes[sizeof(KEY_TYPE)] = '\0';
    memcpy(bytes, &key, sizeof(KEY_TYPE));
    return cstr_hash(bytes, bin_size);
}
#define HASH_FUNC HASH_FUNCTION(KEY_TYPE)
#endif

/**
 * @brief default comparison function. copies bytes into automatic buffers
 * and compares the bytes 
 */
#ifndef KEY_COMP
static int CAT(KEY_TYPE, _comp)(KEY_TYPE key1, KEY_TYPE key2) {
    char bytes1[sizeof(KEY_TYPE)+1];
    char bytes2[sizeof(KEY_TYPE)+1];
    bytes1[sizeof(KEY_TYPE)] = '\0', bytes2[sizeof(KEY_TYPE)] = '\0';
    memcpy(bytes1, &key1, sizeof(KEY_TYPE));
    memcpy(bytes2, &key2, sizeof(KEY_TYPE));
    return strcmp(bytes1, bytes2);                              
}
#define KEY_COMP KEY_COMPARE(KEY_TYPE)
#endif

/**
 * @brief key-value pair node storge in the hash map
 */
typedef struct HASH_PAIR {
    KEY_TYPE key;
    VALUE_TYPE value;
} HASH_PAIR;

// to allow calculation of alignment pre-C11
#if __STDC_VERSION__ < 201112L
struct CAT(HASH_PAIR,_ALIGNMENT) {
	char a;
	HASH_PAIR HASH_PAIR;
};
#endif

typedef struct HASH_MAP_TYPE HASH_MAP_TYPE;

/**
 * @brief vtable struct for hash map
 */
typedef struct CAT(HASH_COMBO, _Type) {
    bool (*in)(HASH_MAP_TYPE * map, KEY_TYPE key);
    hash_map_err (*get)(HASH_MAP_TYPE * map, KEY_TYPE key, VALUE_TYPE * value);
    hash_map_err (*set)(HASH_MAP_TYPE * map, KEY_TYPE key, VALUE_TYPE value);
    void (*dest)(HASH_MAP_TYPE * map);
    hash_map_err (*pop)(HASH_MAP_TYPE * map, KEY_TYPE key, VALUE_TYPE * value);
    hash_map_err (*remove)(HASH_MAP_TYPE * map, KEY_TYPE key);
    void (*for_each)(HASH_MAP_TYPE * map, int (*handle_item)(void * data, KEY_TYPE key, VALUE_TYPE value), void * data);
} CAT(HASH_COMBO, _Type);

/**
 * @brief base struct for the hash map
 */
struct HASH_MAP_TYPE {
    CAT(HASH_COMBO, _Type) const * _class;
    HASH_PAIR ** bins;  //!< store array of pointers to HASH_PAIR elements. Use
                        //!<   pointers to ensure re-binning does not cost too 
                        //!<   much memory for large structs
    size_t capacity;
    size_t fill;
    MemPoolManager * pair_mgr;  // memory manager for the HASH_PAIRs
};

// Forward declarations
static hash_map_err CAT(HASH_COMBO, _insert)(HASH_MAP_TYPE * map, HASH_PAIR * pair);
static HASH_PAIR * CAT(HASH_COMBO, _get_pair)(HASH_MAP_TYPE * map, KEY_TYPE key, size_t * bin_num);
static hash_map_err CAT(HASH_COMBO, _pop)(HASH_MAP_TYPE * map, KEY_TYPE key, VALUE_TYPE * value);
static hash_map_err CAT(HASH_COMBO, _remove)(HASH_MAP_TYPE * map, KEY_TYPE key);
static hash_map_err CAT(HASH_COMBO, _get)(HASH_MAP_TYPE * map, KEY_TYPE key, VALUE_TYPE * value);
static bool CAT(HASH_COMBO, _in)(HASH_MAP_TYPE * map, KEY_TYPE key);
static hash_map_err CAT(HASH_COMBO, _set)(HASH_MAP_TYPE * map, KEY_TYPE key, VALUE_TYPE value);
static void CAT(HASH_COMBO, _dest)(HASH_MAP_TYPE * map);
static void CAT(HASH_COMBO, _for_each)(HASH_MAP_TYPE * map, int (*handle_item)(void * data, KEY_TYPE key, VALUE_TYPE value), void * data);
static hash_map_err CAT(HASH_COMBO, _resize)(HASH_MAP_TYPE * map, size_t new_capacity);
static hash_map_err CAT(HASH_COMBO, _init)(HASH_MAP_TYPE * map, size_t init_capacity);

/**
 * @brief idefinition of the vtable
 */
static const CAT(HASH_COMBO, _Type) CAT(HASH_COMBO, _class) = {
    .in = &(CAT(HASH_COMBO, _in)),      //!< test for presence of key in hash map
    .dest = &(CAT(HASH_COMBO, _dest)),  //!< destroy the hash map and reclaim 
                                        //!<   internal memory
    .get = &(CAT(HASH_COMBO, _get)),    //!< retrieve a value for a specified key
    .set = &(CAT(HASH_COMBO, _set)),    //!< set a value for a specified key
    .pop = &(CAT(HASH_COMBO, _pop)),    //!< retrieve and remove for a specified 
                                        //!<   key
    .remove = &(CAT(HASH_COMBO, _remove)),      //!< remove a specifid key
    .for_each = &(CAT(HASH_COMBO, _for_each)),  //!< apply a function to each 
                                                //!<   element in the hash map
};

/**
 * @brief perform linear probe through hash map
 * @param[in] bins the internal array of the hash map
 * @param[in] start_index where to start the linear probe
 * @param[in] capacity the current allocation size of the bins
 */
static HASH_PAIR ** CAT(HASH_COMBO, _linear_probe)(HASH_PAIR ** bins, size_t start_index, size_t capacity) {
    if (*(bins + start_index)) {
        HASH_PAIR ** bin_begin = bins + start_index;
        HASH_PAIR ** bin = bin_begin + 1;
        HASH_PAIR ** bin_cap = bins + capacity;
        if (bin == bin_cap) {
            bin = bins;
        }
        
        while (*bin != NULL && bin != bin_begin) {
            if (++bin == bin_cap) {
                bin = bins;
            }
        }
        if (bin == bin_begin) {
            return NULL;
        }
        bins = bin;
    } else {
        bins = bins + start_index;
    }
    return bins;
}

/**
 * @brief (internal function) inserts a key-value pair into the hash map
 * @param[in] map the hash map to insert the pair into
 * @param[in] pair pointer to the HASH_PAIR to be inserted
 */
static hash_map_err CAT(HASH_COMBO, _insert)(HASH_MAP_TYPE * map, HASH_PAIR * pair) {
    //printf("inserting pair into map of size %zu (%zu)\n", map->fill, map->capacity);
    hash_map_err status = HM_SUCCESS;
    if (map->fill >= map->capacity * HASH_FILL_RESIZE_RATIO) { /* this will even work if the HASH_MAP has been cleared since map->fill == map->capacity == 0 */
        size_t new_cap = next_prime(2*map->capacity);
        //printf("resizing hash %zu to %zu\n",map->capacity, new_cap);
        if ((status = CAT(HASH_COMBO, _resize)(map, new_cap))) {
            return status;
        }
    }
    size_t bin_start = HASH_FUNC(pair->key, map->capacity);
    
    HASH_PAIR ** bins = map->bins;

    bins = CAT(HASH_COMBO, _linear_probe)(bins, bin_start, map->capacity);
    if (bins) {
        *bins = pair;
    } else {
        return HM_INSERTION_FAILURE;
    }
    map->fill++;
    return status;
}

/* never returns a new pointer. returns NULL if not found. Should never return the sentinel */
/**
 * @brief (internal function) inserts a key-value pair into the hash map
 * @param[in] map the hash map to insert the pair into
 * @param[in] key the key value to find
 * @param[out] bin_num pointer to the location in the internal bins where the key is found
 * @returns a pointer to the key-value pair if found, else NULL
 */
static HASH_PAIR * CAT(HASH_COMBO, _get_pair)(HASH_MAP_TYPE * map, KEY_TYPE key, size_t * bin_num) {
    if (!map->capacity) {
        return NULL;
    }
    size_t bin_start = HASH_FUNC(key, map->capacity);
    HASH_PAIR ** bins = map->bins;
    HASH_PAIR ** bin_begin = bins + bin_start;

    if (*bin_begin && *bin_begin != HASH_PAIR_REMOVED && !KEY_COMP((*bin_begin)->key, key)) {
        if (bin_num) {
            *bin_num = bin_start;
        }
        return *bin_begin;
    }
    if (!*bin_begin) {
        if (bin_num) {
            *bin_num = bin_start;
        }
        return NULL;
    }
    
    HASH_PAIR ** bin = bin_begin + 1;
    HASH_PAIR ** bin_cap = bins + map->capacity;
    if (bin >= bin_cap) {
        bin = bins;
    }
    
    while (*bin && bin != bin_begin) {
        if (*bin != HASH_PAIR_REMOVED && !KEY_COMP((*bin)->key, key)) {
            if (bin_num) {
                *bin_num = (size_t) (bin - bins);
            }
            return *bin;
        }
        if (++bin >= bin_cap) {
            bin = bins;
        }
    }
    return NULL;
}

/**
 * @brief retrieve value and remove key from hash map
 * @param[in] map the hash map from which to remove key-value pair
 * @param[in] key the key to remove
 * @param[out] value a pointer to the value of the key, if found, else no change
 * @returns a non-zero value if key is not found, else 0
 */
static hash_map_err CAT(HASH_COMBO, _pop)(HASH_MAP_TYPE * map, KEY_TYPE key, VALUE_TYPE * value) {
    size_t bin_num;
    HASH_PAIR * pair = CAT(HASH_COMBO, _get_pair)(map, key, &bin_num); /* cannot return the HASH_PAIR_REMOVED sentinel */
    if (!pair) {
        return HM_KEY_NOT_FOUND;
    }
    if (value) {
        memcpy(value, &(pair->value), sizeof(VALUE_TYPE));
    }
    free(map->bins[bin_num]);
    map->bins[bin_num] = HASH_PAIR_REMOVED;
    map->fill--;
    return HM_SUCCESS;
}

/**
 * @brief remove key from hash map
 * @param[in] map the hash map from which to remove key
 * @param[in] key the key to remove
 * @returns a non-zero value if key is not found, else 0
 */
static hash_map_err CAT(HASH_COMBO, _remove)(HASH_MAP_TYPE * map, KEY_TYPE key) {
    return CAT(HASH_COMBO, _pop)(map, key, NULL);
}

/**
 * @brief retrieve the value of the given key
 * @param[in] map the hash map from which to retrieve value
 * @param[in] key the key corresponding to the value
 * @param[out] value a pointer to store the resulting value if found, else no change
 * @returns a non-zero value if key is not found, else 0
 */
static hash_map_err CAT(HASH_COMBO, _get)(HASH_MAP_TYPE * map, KEY_TYPE key, VALUE_TYPE * value) {
    HASH_PAIR * pair = CAT(HASH_COMBO, _get_pair)(map, key, NULL);
    if (!pair) {
        return HM_KEY_NOT_FOUND;
    }
    if (value) {
        memcpy(value, &(pair->value), sizeof(VALUE_TYPE));
    }
    return HM_SUCCESS;
}

/**
 * @brief check if key is in the corresponding hash map
 * @param[in] map the hash map to be search
 * @param[in] key the key to search for
 * @returns true if key is in map else false
 */
static bool CAT(HASH_COMBO, _in)(HASH_MAP_TYPE * map, KEY_TYPE key) {
    return CAT(HASH_COMBO, _get_pair)(map, key, NULL) != NULL;
}

/**
 * @brief set a key-value pair in the hash map
 * @param[in] map the hash map to insert the key-value pair into
 * @param[in] key the key to be store
 * @param[in] value the value to be stored
 * @returns a non-zero value if key is not found, else 0
 */
static hash_map_err CAT(HASH_COMBO, _set)(HASH_MAP_TYPE * map, KEY_TYPE key, VALUE_TYPE value) {
    HASH_PAIR * pair = CAT(HASH_COMBO, _get_pair)(map, key, NULL);
    if (pair) { /* key-value pair already exists, overwriting value */
        memmove(&(pair->value), &value, sizeof(VALUE_TYPE));
        return HM_SUCCESS;
    }
    pair = MemPoolManager_next(map->pair_mgr);
    if (!pair) {
        return HM_MALLOC_PAIR_FAILURE;
    }
    memcpy(&(pair->key), &key, sizeof(KEY_TYPE));
    memcpy(&(pair->value), &value, sizeof(VALUE_TYPE));
    return CAT(HASH_COMBO, _insert)(map, pair);
}

/**
 * @brief destroy the hash map and reclaim memory
 * @param[in] map the hash map
 */
static void CAT(HASH_COMBO, _dest)(HASH_MAP_TYPE * map) {
    if (map->bins) {
        free(map->bins);
        map->bins = NULL;
    }
    if (map->pair_mgr) {
        MemPoolManager_del(map->pair_mgr);
    }
    map->pair_mgr = NULL;
    map->fill = 0;
    map->capacity = 0;
}

/**
 * @brief apply a function for each element in the hash map iteratively
 * @param[in] map the hash map over which to iterate
 * @param[in] handle_item 
 *      @brief the function to apply to each element
 *      @param[inout] data a context pointer for each element
 *      @param[in] key the key in the key-value pair
 *      @param[in] value the value in the key-value pair
 *      @returns 0 to continue iteration, non-zero to abort
 * @param[inout] data the context pointer for the handle_item function
 */
static void CAT(HASH_COMBO, _for_each)(HASH_MAP_TYPE * map, int (*handle_item)(void * data, KEY_TYPE key, VALUE_TYPE value), void * data) {
    int status = 0;
    size_t i = 0;
    for (i = 0; i < map->capacity; i++) {
        HASH_PAIR * p = map->bins[i];
        if (p && (p != HASH_PAIR_REMOVED)) {
            if ((status = handle_item(data, p->key, p->value))) {
                break;
            }
        }
    }
}

/**
 * @brief initialize the hash map
 * @param[in] map the hash map
 * @param[in] init_capacity the initial capacity to assign to the hash map. 
 *      HASH_DEFAULT_SIZE is provided if set to 0
 * @returns an error if initialization fails else 0
 */
static hash_map_err CAT(HASH_COMBO, _init)(HASH_MAP_TYPE * map, size_t init_capacity) {
    if (!init_capacity) {
        init_capacity = HASH_DEFAULT_SIZE;
    }
    map->bins = NULL;
    map->capacity = 0;
    map->fill = 0;
#if __STDC_VERSION__ < 201112L
	map->pair_mgr = MemPoolManager_new(HASH_PAIR_POOL_COUNT, sizeof(HASH_PAIR), offsetof(struct  CAT(HASH_PAIR,_ALIGNMENT), HASH_PAIR));
#else
    map->pair_mgr = MemPoolManager_new(HASH_PAIR_POOL_COUNT, sizeof(HASH_PAIR), _Alignof(HASH_PAIR));
#endif
    map->_class = &(CAT(HASH_COMBO, _class));
    return CAT(HASH_COMBO, _resize)(map, init_capacity);
}

/**
 * @brief (internal function) resize the underlying storage array
 * @param[in] map the hash map
 * @param[in] new_capacity the new capacity for storage. Note that 0 destroys the map
 * @returns an error if reallocation or reinsertion fails else 0
 */
static hash_map_err CAT(HASH_COMBO, _resize)(HASH_MAP_TYPE * map, size_t new_capacity) {
    if (!new_capacity) {
        CAT(DELAY(HASH_COMBO), _dest)(map);
        return HM_SUCCESS;
    }
    HASH_PAIR ** old_bins = map->bins;
    size_t old_capacity = map->capacity;
    map->bins = calloc(new_capacity, sizeof(*(map->bins)));
    map->fill = 0;
    map->capacity = new_capacity;
    if (!map->bins) {
        return HM_MALLOC_MAP_FAILURE;
    }
    hash_map_err status = HM_SUCCESS;
    for (size_t i = 0; i < old_capacity; i++) {
        HASH_PAIR * bin = old_bins[i];
        if (bin && bin != HASH_PAIR_REMOVED) {
            if ((status = CAT(HASH_COMBO, _insert)(map, bin))) {
                return status;
            }
        }
    }
    free(old_bins);
    return HM_SUCCESS;
}

// undef all the input macros so that the header can be re-included
#undef HASH_COMBO
#undef HASH_PAIR
#undef HASH_MAP_TYPE
#undef HASH_FUNC
#undef HASH_FILL_RESIZE_RATIO
#undef KEY_COMP
#undef KEY_TYPE
#undef VALUE_TYPE

#endif // if defined(KEY_TYPE) && defined(VALUE_TYPE)

