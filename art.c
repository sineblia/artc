/**
 * ARTinC - Adaptive Radix Tree in C
 * 
 * Copyright (c) 2023, Simone Bellavia <simone.bellavia@live.it>
 * All rights reserved.
 * Released under MIT License. Please refer to LICENSE for details
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __x86_64__
    #include <emmintrin.h>
#endif

#define MAX_PREFIX_LENGTH 32
#define EMPTY_KEY ((unsigned char)255) // A value that isn't a valid ASCII character

/*** DATA STRUCTURES ***/

/**
 * NodeType
 * 
 * To define the various types of nodes that an ART can have. 
 * These types can vary in the number of children they can contain.
 */
typedef enum {
    NODE4,
    NODE16,
    NODE48,
    NODE256,
    LEAF
} NodeType;

/**
 * Node
 * 
 * The Node structure acts as the basis for all node types in the ART. Its 
 * main function is to identify the specific node type within the tree.
 * The field 'type' field, of type NodeType (which is the previous declared enum), 
 * indicates the specific type of the node, such as NODE4, NODE16, NODE48, NODE256, 
 * or LEAF. This information is crucial in determining how to handle the node in 
 * various operations such as insert, search, and delete. We will use the type field 
 * to determine the node type at runtime and explicitly cast to the appropriate 
 * derived structure to access its specific fields.
 */
typedef struct Node {
    NodeType type;
} Node;

/**
 * Node4, Node16, Node48 and Node256
 *
 * Each type of internal node have a specific structure that 
 * extends Node. The main difference between these nodes is the 
 * number of children they can contain.
 * 
 * The 'prefix' array in Node4 is used to store the common prefix of 
 * all keys passing through this node. In an ART, the prefix is a part 
 * of the key that is common to all children of that node. Example: If 
 * you have keys like "apple," "appetite," and "application" in a node, 
 * the common prefix might be "app."
 * 
 * 'prefixLen' indicates the actual length of the prefix stored in the prefix 
 * array. This value is important because not all prefixes will use the full 
 * maximum length (MAX_PREFIX_LENGTH). Example: If the prefix is "app", prefixLen 
 * will be 3, although MAX_PREFIX_LENGTH could be much greater.
 * 
 * 'keys' is an array that contains the parts of the keys that differentiate the children 
 * in this node. Each element in keys corresponds to a specific child in children.
 * 
 * 'children' is an array of pointers to Node, representing the children of this node. 
 * Each element in children is a child that corresponds to a key in keys.
 * 
 * The association between 'keys' and 'children' is at the heart of ART's functionality.
*/
typedef struct {
    Node node;
    char prefix[MAX_PREFIX_LENGTH];
    int prefixLen;
    char keys[4];
    Node *children[4];
    int count;
} Node4;

typedef struct {
    Node node;
    char prefix[MAX_PREFIX_LENGTH];
    int prefixLen;
    char keys[16];
    Node *children[16];
    int count;
} Node16;

typedef struct {
    Node node;
    char prefix[MAX_PREFIX_LENGTH];
    int prefixLen;
    unsigned char keys[256];
    Node *children[48];
} Node48;

typedef struct {
    Node node;
    char prefix[MAX_PREFIX_LENGTH];
    int prefixLen;
    Node *children[256];
} Node256;

/**
 * LeafNode
 *
 * The LeafNode is the type of node that actually contains 
 * the value (or data) associated with the key.
 */
typedef struct {
    Node node;
    char *key;
    void *value;
} LeafNode;

/**
 * ART
 * 
*/
typedef struct {
    Node *root;
    size_t size;
} ART;

/*** FUNCTIONS ***/

/**
 * createRootNode
 *
 * Creates and initializes a new Node4 type root node for an Adaptive Radix Tree (ART).
 * Assigns an initial value of EMPTY_KEY to all elements in the keys array
 * and sets all child pointers to NULL, indicating that the node is initially empty.
 * Returns a pointer to the created root node, or NULL if memory allocation fails.
 */
Node *createRootNode() {
    Node4 *root = calloc(1, sizeof(Node4));
    if (!root) {
        return NULL;
    }

    root->node.type = NODE4;
    root->prefixLen = 0;

    for (int i = 0; i < 4; i++) {
        root->keys[i] = EMPTY_KEY;
    }

    return (Node *)root;
}

/**
 * initializeAdaptiveRadixTree 
 *
 * Initializes a new Adaptive Radix Tree (ART).
 * Creates a new ART structure and sets its root node
 * by calling the createRootNode function. Also initializes the size
 * of the tree to 0, indicating that the tree initially has no elements.
 * Returns a pointer to the initialized ART, or NULL if memory allocation fails.
 */
ART *initializeAdaptiveRadixTree() {
    ART *tree = malloc(sizeof(ART));
    if (!tree) {
        return NULL;
    }

    tree->root = createRootNode();
    tree->size = 0;

    return tree;
}

/**
 * findChildSSE - Finds a child node in a Node16 using SSE instructions.
 * 
 * Utilizes SSE (Streaming SIMD Extensions) to perform an efficient,
 * parallel comparison of a given byte against all keys in a Node16.
 * This function is optimized for architectures that support SSE and provides
 * a significant speed-up by processing multiple bytes in parallel.
 *
 * @param node A pointer to the Node16 to search in.
 * @param byte The byte (key) to find the corresponding child for.
 * @return A pointer to the found child node, or NULL if no match is found.
 */
#ifdef __SSE2__
    Node *findChildSSE(Node16 *node, char byte){
        __m128i key = _mm_set1_epi8(byte);
        __m128i keys = _mm_loadu_si128((__m128i *)(node->keys));
        __m128i cmp = _mm_cmpeq_epi8(key, keys);
        int mask = (1 << (node->count - 1));
        int bitfield = _mm_movemask_epi8(cmp) & mask;
        if (bitfield){
            int index = __builtin_ctz(bitfield);
            return node->children[index];
        } else {
            return NULL;
        }
    }
#endif

/**
 * findChildBinary - Finds a child node in a Node16 using binary search.
 * 
 * Implements a binary search algorithm to find a specific byte in the
 * keys array of a Node16. This method is used as a portable alternative
 * to SSE-based search, suitable for platforms that do not support SSE.
 * Binary search offers better performance than a linear search, especially
 * when the number of keys is relatively large.
 *
 * @param node A pointer to the Node16 to search in.
 * @param byte The byte (key) to find the corresponding child for.
 * @return A pointer to the found child node, or NULL if no match is found.
 */
Node *findChildBinary(Node16 *node, char byte){
    int low = 0;
    int high = node->count - 1;

    while (low <= high){
        int mid = low + (high - low) / 2;
        char midByte = node->keys[mid];

        if (midByte < byte){
            low = mid + 1;
        }
        else if (midByte > byte){
            high = mid - 1;
        }
        else{
            return node->children[mid];
        }
    }

    return NULL;
}

/**
 * findChild - Finds a child node in an ART node based on the given byte (key).
 * 
 * This function handles the retrieval of a child node from different types
 * of ART nodes (Node4, Node16, Node48, Node256) based on the provided byte.
 * The specific search algorithm or method used depends on the node type:
 *   - For Node4, a linear search is used.
 *   - For Node16, it utilizes either SSE-based search or binary search,
 *     depending on the platform's support for SSE.
 *   - For Node48, the function performs a lookup using an index array.
 *   - For Node256, it directly accesses the child based on the byte value.
 *
 * This approach ensures that the search is as efficient as possible given
 * the characteristics of each node type.
 *
 * @param node A pointer to the ART node to search in.
 * @param byte The byte (key) to find the corresponding child for.
 * @return A pointer to the found child node, or NULL if no match is found.
 */
Node *findChild(Node *node, char byte){
    if (node->type == NODE4){
        Node4 *node4 = (Node4 *)node;

        for (int i = 0; i < node4->count; i++){
            if(node4->keys[i] == byte){
                return node4->children[i];
            }
        }

        return NULL;
    }

    if(node->type == NODE16){
        Node16 *node16 = (Node16 *)node;
        #ifdef __SSE2__
            return findChildSSE(node16, byte);
        #else
            return findChildBinary(node16, byte);
        #endif
    }

    if(node->type == NODE48){
        Node48 *node48 = (Node48 *)node;

        unsigned char childIndex = node48->keys[byte];

        if(childIndex != EMPTY_KEY){
            return node48->children[childIndex];
        } else {
            return NULL;
        }
    }

    if(node->type == NODE256){
        Node256 *node256 = (Node256 *)node;
        return node256->children[byte];
    }

    return NULL;
}

int main(){
    return 0;
}