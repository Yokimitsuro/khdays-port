/* The character select's pages (charselect.c, host/screens/select_screen.c): the grid's positions -- the
 * characters in the select's order, the random choice last -- in pages of `columns` x `rows`, each page's
 * rows of `columns` with its last row centred, the page shown the one the cursor is on. Pure: the caller
 * says which positions may be moved onto. */
#ifndef KHDAYS_SELECT_PAGES_H
#define KHDAYS_SELECT_PAGES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysSelectGrid {
    int count;   /* the positions */
    int columns; /* a row's */
    int rows;    /* a page's (0: every position on one page) */
} KhdaysSelectGrid;

enum KhdaysSelectMove {
    KHDAYS_SELECT_LEFT,
    KHDAYS_SELECT_RIGHT,
    KHDAYS_SELECT_UP,
    KHDAYS_SELECT_DOWN,
    KHDAYS_SELECT_PREVIOUS_PAGE,
    KHDAYS_SELECT_NEXT_PAGE,
};

/* A page's positions (at least one). */
int khdays_select_per_page(KhdaysSelectGrid g);
/* The pages (at least one). */
int khdays_select_pages(KhdaysSelectGrid g);
/* The page of `position` (from 0). */
int khdays_select_page_of(KhdaysSelectGrid g, int position);
/* A page's first position and how many it holds. */
int khdays_select_page_first(KhdaysSelectGrid g, int page);
int khdays_select_page_count(KhdaysSelectGrid g, int page);
/* Where `position` is across its row: its middle in columns from the page's middle column (the last row
 * centred). */
float khdays_select_x(KhdaysSelectGrid g, int position);

/* The position a move leads to from `at`, stepping over the positions `may` refuses (it is asked
 * `may(position, user)`); `at` when none.
 * - left and right: the next position along the select's order, round from the last to the first;
 * - up and down: the nearest position of the row above or below, across pages (the first row of the next
 *   page under a page's last), round;
 * - the previous and next page: the same place on that page, or its last position; none past the first
 *   and the last page. */
int khdays_select_move(KhdaysSelectGrid g, int at, int move, int (*may)(int position, void *user), void *user);

#ifdef __cplusplus
}
#endif

#endif
