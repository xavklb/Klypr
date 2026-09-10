#ifndef LAYOUT_H
#define LAYOUT_H

#include <windows.h>
#include <stdbool.h>

typedef enum {
    SPLIT_NONE,
    SPLIT_HORIZONTAL,
    SPLIT_VERTICAL
} SplitType;

typedef struct BspNode {
    HWND window;
    RECT rect;
    SplitType split;
    double split_ratio;
    struct BspNode *left;
    struct BspNode *right;
    struct BspNode *parent;
} BspNode;

BspNode *bsp_node_create(HWND window);
void bsp_node_destroy(BspNode *node);
void layout_arrange(BspNode *root, RECT bounds);

#endif
