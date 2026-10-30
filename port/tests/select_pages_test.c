/* khdays-select-pages-test: the character select's pages (runtime/menus/select_pages.c): the game's 19 characters,
 * thirty of a mod's and the random choice in pages of five by five; the moves across them, positions taken stepped
 * over; one page as the select always was. */
#include "../runtime/menus/select_pages.h"

#include <stdio.h>

static int failures;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                                      \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static int taken[64];

static int may(int position, void *user)
{
    (void)user;
    return !taken[position];
}

static int move(KhdaysSelectGrid g, int at, int m)
{
    return khdays_select_move(g, at, m, may, NULL);
}

static void test_pages(void)
{
    const KhdaysSelectGrid g = {19 + 30 + 1, 5, 5};
    CHECK(khdays_select_per_page(g) == 25 && khdays_select_pages(g) == 2);
    CHECK(khdays_select_page_of(g, 0) == 0 && khdays_select_page_of(g, 24) == 0 && khdays_select_page_of(g, 25) == 1);
    CHECK(khdays_select_page_first(g, 1) == 25 && khdays_select_page_count(g, 1) == 25);
    const KhdaysSelectGrid three = {19 + 31 + 1, 5, 5};
    CHECK(khdays_select_pages(three) == 3 && khdays_select_page_count(three, 2) == 1);
    /* the last page's last row centred: one position in the middle column */
    CHECK(khdays_select_x(three, 50) == 0.0f);
    CHECK(khdays_select_x(g, 25) == -2.0f && khdays_select_x(g, 29) == 2.0f);
}

static void test_moves(void)
{
    const KhdaysSelectGrid g = {19 + 30 + 1, 5, 5};
    /* left and right along the order, across pages, round */
    CHECK(move(g, 24, KHDAYS_SELECT_RIGHT) == 25 && move(g, 25, KHDAYS_SELECT_LEFT) == 24);
    CHECK(move(g, 49, KHDAYS_SELECT_RIGHT) == 0 && move(g, 0, KHDAYS_SELECT_LEFT) == 49);
    /* down from a page's last row: the next page's first row, same column; up back */
    CHECK(move(g, 22, KHDAYS_SELECT_DOWN) == 27 && move(g, 27, KHDAYS_SELECT_UP) == 22);
    /* down from the last page's last row: round to the first */
    CHECK(move(g, 47, KHDAYS_SELECT_DOWN) == 2);
    /* the pages: the same place, none past the ends */
    CHECK(move(g, 3, KHDAYS_SELECT_NEXT_PAGE) == 28 && move(g, 28, KHDAYS_SELECT_PREVIOUS_PAGE) == 3);
    CHECK(move(g, 3, KHDAYS_SELECT_PREVIOUS_PAGE) == 3 && move(g, 28, KHDAYS_SELECT_NEXT_PAGE) == 28);
    /* onto a shorter page: its last position */
    const KhdaysSelectGrid three = {19 + 31 + 1, 5, 5};
    CHECK(move(three, 40, KHDAYS_SELECT_NEXT_PAGE) == 50);
    /* a taken position stepped over: the next one on, then before it */
    taken[28] = 1;
    CHECK(move(g, 3, KHDAYS_SELECT_NEXT_PAGE) == 29);
    taken[29] = taken[30] = taken[31] = taken[32] = taken[33] = 1;
    for (int p = 34; p < 50; ++p) {
        taken[p] = 1;
    }
    CHECK(move(g, 3, KHDAYS_SELECT_NEXT_PAGE) == 27);
    CHECK(move(g, 24, KHDAYS_SELECT_RIGHT) == 25);
    for (int p = 0; p < 64; ++p) {
        taken[p] = 0;
    }
}

static void test_one_page(void)
{
    /* the game's 19 and the random choice: one page, the moves the select always made */
    const KhdaysSelectGrid g = {20, 5, 5};
    CHECK(khdays_select_pages(g) == 1);
    CHECK(move(g, 2, KHDAYS_SELECT_NEXT_PAGE) == 2 && move(g, 2, KHDAYS_SELECT_PREVIOUS_PAGE) == 2);
    CHECK(move(g, 17, KHDAYS_SELECT_DOWN) == 2 && move(g, 2, KHDAYS_SELECT_UP) == 17);
    const KhdaysSelectGrid all = {20, 5, 0};
    CHECK(khdays_select_pages(all) == 1 && khdays_select_per_page(all) == 20);
}

int main(void)
{
    test_pages();
    test_moves();
    test_one_page();
    if (failures == 0) {
        printf("select pages: ok\n");
    }
    return failures != 0;
}
