#include "layout.h"
#include <stdlib.h>

BspNode *bsp_node_create(HWND window)
{
    BspNode *node = (BspNode *)malloc(sizeof(BspNode));
    if (node == NULL) {
        return NULL;
    }

    node->window = window;
    node->rect.left = 0;
    node->rect.top = 0;
    node->rect.right = 0;
    node->rect.bottom = 0;
    node->split = SPLIT_NONE;
    node->split_ratio = 0.5;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;

    return node;
}

void bsp_node_destroy(BspNode *node)
{
    if (node == NULL) {
        return;
    }

    if (node->left != NULL) {
        bsp_node_destroy(node->left);
        node->left = NULL;
    }

    if (node->right != NULL) {
        bsp_node_destroy(node->right);
        node->right = NULL;
    }

    free(node);
}

void layout_arrange(BspNode *root, RECT bounds)
{
    if (root == NULL) {
        return;
    }

    root->rect = bounds;

    if (root->left != NULL && root->right != NULL) {
        int width = bounds.right - bounds.left;
        int height = bounds.bottom - bounds.top;

        RECT left_bounds = bounds;
        RECT right_bounds = bounds;

        if (root->split == SPLIT_VERTICAL) {
            int split_pos = bounds.left + (int)(width * root->split_ratio);
            left_bounds.right = split_pos;
            right_bounds.left = split_pos;
        } else if (root->split == SPLIT_HORIZONTAL) {
            int split_pos = bounds.top + (int)(height * root->split_ratio);
            left_bounds.bottom = split_pos;
            right_bounds.top = split_pos;
        }

        layout_arrange(root->left, left_bounds);
        layout_arrange(root->right, right_bounds);
    } else if (root->window != NULL && IsWindow(root->window)) {
        int width = bounds.right - bounds.left;
        int height = bounds.bottom - bounds.top;

        SetWindowPos(
            root->window,
            NULL,
            bounds.left,
            bounds.top,
            width,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE
        );
    }
}
