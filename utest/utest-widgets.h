/*******************************************************************************
 * utest-widget.h
 *
 * Widgets for GUI layer
 *
 * Copyright (c) 2015 Cogent Embedded Inc. ALL RIGHTS RESERVED.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************/

#ifndef __UTEST_WIDGETS_H
#define __UTEST_WIDGETS_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest-display.h"

/*******************************************************************************
 * Forward declarations
 ******************************************************************************/

typedef struct gui_list         gui_list_t;
typedef struct gui_menu_item    gui_menu_item_t;
typedef struct gui_menu         gui_menu_t;

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...generic double-linked list items */
struct gui_list
{
    gui_list_t     *next, *prev;   
};

/* ...list head initialization */
static inline void gui_list_init(gui_list_t *list)
{
    list->prev = list->next = list;
}

/* ...add new item to the list */
static inline void gui_list_add(gui_list_t *list, gui_list_t *item)
{
    item->next = list;
    item->prev = list->prev;
    list->prev->next = item;
    list->prev = item;
}

/* ...remove item from the double-linked list */
static inline void gui_list_remove(gui_list_t *list, gui_list_t *item)
{
    item->prev->next = item->next;
    item->next->prev = item->prev;
}

/*******************************************************************************
 * Generic menu item
 ******************************************************************************/

/* ...menu item */
struct gui_menu_item
{
    /* ...double-linked list item */
    gui_list_t          list;

    /* ...region covered by item */
    int                 x, y, w, h;

    /* ...text string */
    char               *text;

    /* ...menu item flags */
    int                 flags;

    /* ...selection processing function */
    void              (*select)(gui_menu_item_t *, widget_data_t *);
};

/* ...menu item flags */
#define GUI_MENU_ITEM_CHECKBOX          (1 << 0)
#define GUI_MENU_ITEM_CHECKBOX_STATE    (1 << 1)
#define GUI_MENU_ITEM_CHECKBOX_ENABLED  (GUI_MENU_ITEM_CHECKBOX | GUI_MENU_ITEM_CHECKBOX_STATE)

/* ...generic menu widget */
struct gui_menu
{
    /* ...widget handle */
    widget_data_t      *widget;

    /* ...list sentinel node */
    gui_list_t          list;

    /* ...currently active item */
    gui_menu_item_t    *focus;
};

/* ...menu initialization function */
static inline void gui_menu_init(gui_menu_t *menu, widget_data_t *widget)
{
    gui_list_init(&menu->list);
    menu->widget = widget;
    menu->focus = NULL;
}

/* ...add item to the menu */
static inline void gui_menu_item_add(gui_menu_t *menu, gui_menu_item_t *item)
{
    /* ...add item to the tail of the list */
    gui_list_add(&menu->list, &item->list);
}

/* ...remove item from the list */
static inline void gui_menu_item_remove(gui_menu_t *menu, gui_menu_item_t *item)
{
    /* ...add item to the tail of the list */
    gui_list_remove(&menu->list, &item->list);
}

/* ...head of the list */
static inline gui_menu_item_t * gui_menu_first(gui_menu_t *menu)
{
    gui_list_t     *list = menu->list.next;
    
    return (list == &menu->list ? NULL : container_of(list, gui_menu_item_t, list));
}

/* ...getting to a next item */
static inline gui_menu_item_t * gui_menu_next(gui_menu_t *menu, gui_menu_item_t *item)
{
    gui_list_t     *list = item->list.next;
    
    return (list == &menu->list ? NULL : container_of(list, gui_menu_item_t, list));
}

/* ...tail of the list */
static inline gui_menu_item_t * gui_menu_last(gui_menu_t *menu)
{
    gui_list_t     *list = menu->list.prev;
    
    return (list == &menu->list ? NULL : container_of(list, gui_menu_item_t, list));
}

/* ...getting to a previous item */
static inline gui_menu_item_t * gui_menu_prev(gui_menu_t *menu, gui_menu_item_t *item)
{
    gui_list_t     *list = item->list.prev;
    
    return (list == &menu->list ? NULL : container_of(list, gui_menu_item_t, list));
}

/*******************************************************************************
 * Common menu widgets - need that at all?
 ******************************************************************************/

/* ...submenu item */
typedef struct gui_submenu_item
{
    /* ...generic menu item */
    gui_menu_item_t         item;

    /* ...submenu list */
    gui_menu_t              menu;

}   gui_submenu_item_t;

/* ...anything else? */

/*******************************************************************************
 * Public menu API - tbd 
 ******************************************************************************/

#endif  /* __UTEST_WIDGETS_H */
