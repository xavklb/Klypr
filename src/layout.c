#include "layout.h"
#include <stdlib.h>
#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

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

BspNode *bsp_node_find(BspNode *root, HWND window)
{
    if (root == NULL || window == NULL) {
        return NULL;
    }

    if (root->window == window) {
        return root;
    }

    BspNode *found = bsp_node_find(root->left, window);
    if (found != NULL) {
        return found;
    }

    return bsp_node_find(root->right, window);
}

void bsp_node_insert(BspNode **root, HWND window, HWND focused)
{
    if (root == NULL || window == NULL) {
        return;
    }

    if (*root == NULL) {
        *root = bsp_node_create(window);
        return;
    }

    BspNode *target = NULL;
    if (focused != NULL) {
        target = bsp_node_find(*root, focused);
    }

    if (target == NULL || target->window == NULL) {
        target = *root;
        while (target->left != NULL && target->right != NULL) {
            target = target->right;
        }
    }

    if (target == NULL) {
        return;
    }

    BspNode *child1 = bsp_node_create(target->window);
    if (child1 == NULL) {
        return;
    }

    BspNode *child2 = bsp_node_create(window);
    if (child2 == NULL) {
        free(child1);
        return;
    }

    int w = target->rect.right - target->rect.left;
    int h = target->rect.bottom - target->rect.top;

    if (w >= h && w > 0) {
        target->split = SPLIT_VERTICAL;
    } else if (h > w && h > 0) {
        target->split = SPLIT_HORIZONTAL;
    } else {
        target->split = SPLIT_VERTICAL;
    }

    target->split_ratio = 0.5;
    target->window = NULL;
    target->left = child1;
    target->right = child2;
    child1->parent = target;
    child2->parent = target;
}

void bsp_node_remove(BspNode **root, HWND window)
{
    if (root == NULL || *root == NULL || window == NULL) {
        return;
    }

    BspNode *leaf = bsp_node_find(*root, window);
    if (leaf == NULL) {
        return;
    }

    if (leaf->parent == NULL) {
        free(leaf);
        *root = NULL;
        return;
    }

    BspNode *parent = leaf->parent;
    BspNode *sibling = (parent->left == leaf) ? parent->right : parent->left;
    BspNode *grandparent = parent->parent;

    if (grandparent == NULL) {
        sibling->parent = NULL;
        *root = sibling;
    } else {
        if (grandparent->left == parent) {
            grandparent->left = sibling;
        } else {
            grandparent->right = sibling;
        }
        sibling->parent = grandparent;
    }

    free(leaf);
    free(parent);
}

void layout_arrange(BspNode *root, RECT bounds, int gap_size, int border_radius)
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

        int half_gap = gap_size / 2;

        if (root->split == SPLIT_VERTICAL) {
            int split_pos = bounds.left + (int)(width * root->split_ratio);
            left_bounds.right = split_pos - half_gap;
            right_bounds.left = split_pos + (gap_size - half_gap);
        } else if (root->split == SPLIT_HORIZONTAL) {
            int split_pos = bounds.top + (int)(height * root->split_ratio);
            left_bounds.bottom = split_pos - half_gap;
            right_bounds.top = split_pos + (gap_size - half_gap);
        }

        layout_arrange(root->left, left_bounds, gap_size, border_radius);
        layout_arrange(root->right, right_bounds, gap_size, border_radius);
    } else if (root->window != NULL && IsWindow(root->window)) {
        int width = bounds.right - bounds.left;
        int height = bounds.bottom - bounds.top;

        if (width > 0 && height > 0) {
            SetWindowPos(
                root->window,
                NULL,
                bounds.left,
                bounds.top,
                width,
                height,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
            );

            if (border_radius > 0) {
                DWORD pref = (border_radius <= 6) ? 3 : 2;
                HRESULT hr = DwmSetWindowAttribute(root->window, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
                if (FAILED(hr)) {
                    HRGN rgn = CreateRoundRectRgn(0, 0, width, height, border_radius * 2, border_radius * 2);
                    if (rgn != NULL) {
                        SetWindowRgn(root->window, rgn, TRUE);
                    }
                }
            } else {
                DWORD pref = 1;
                DwmSetWindowAttribute(root->window, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
                SetWindowRgn(root->window, NULL, TRUE);
            }
        }
    }
}
