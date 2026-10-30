/* The character select's pages (select_pages.h). */
#include "select_pages.h"

static int columns_of(KhdaysSelectGrid g)
{
    return g.columns > 0 ? g.columns : 1;
}

int khdays_select_per_page(KhdaysSelectGrid g)
{
    const int cols = columns_of(g);
    if (g.rows <= 0) {
        return g.count > 0 ? g.count : 1;
    }
    return cols * g.rows;
}

int khdays_select_pages(KhdaysSelectGrid g)
{
    const int per = khdays_select_per_page(g);
    return g.count > 0 ? (g.count + per - 1) / per : 1;
}

int khdays_select_page_of(KhdaysSelectGrid g, int position)
{
    return position > 0 ? position / khdays_select_per_page(g) : 0;
}

int khdays_select_page_first(KhdaysSelectGrid g, int page)
{
    return page * khdays_select_per_page(g);
}

int khdays_select_page_count(KhdaysSelectGrid g, int page)
{
    const int rest = g.count - khdays_select_page_first(g, page), per = khdays_select_per_page(g);
    return rest < 0 ? 0 : rest < per ? rest : per;
}

int khdays_select_row_on_page(KhdaysSelectGrid g, int position)
{
    const int page = khdays_select_page_of(g, position);
    return (position - khdays_select_page_first(g, page)) / columns_of(g);
}

float khdays_select_x(KhdaysSelectGrid g, int position)
{
    const int cols = columns_of(g), page = khdays_select_page_of(g, position);
    const int on_page = position - khdays_select_page_first(g, page), row = on_page / cols;
    const int left = khdays_select_page_count(g, page) - row * cols, in_row = left < cols ? left : cols;
    return (float)(on_page % cols) - (float)(in_row - 1) / 2.0f;
}

/* The rows of the whole grid in order, a page's after the one before: the row `position` is in. */
static int row_overall(KhdaysSelectGrid g, int position)
{
    const int cols = columns_of(g), page = khdays_select_page_of(g, position);
    int rows = 0;
    for (int p = 0; p < page; ++p) {
        rows += (khdays_select_page_count(g, p) + cols - 1) / cols;
    }
    return rows + khdays_select_row_on_page(g, position);
}

/* The first position of overall row `row`, and how many it holds. */
static int row_start(KhdaysSelectGrid g, int row, int *count)
{
    const int cols = columns_of(g), pages = khdays_select_pages(g);
    for (int p = 0; p < pages; ++p) {
        const int n = khdays_select_page_count(g, p), rows = (n + cols - 1) / cols;
        if (row < rows) {
            const int first = khdays_select_page_first(g, p) + row * cols, left = n - row * cols;
            *count = left < cols ? left : cols;
            return first;
        }
        row -= rows;
    }
    *count = 0;
    return -1;
}

int khdays_select_move(KhdaysSelectGrid g, int at, int move, int (*may)(int position, void *user), void *user)
{
    const int count = g.count;
    if (count <= 0 || at < 0 || at >= count) {
        return at;
    }
    if (move == KHDAYS_SELECT_LEFT || move == KHDAYS_SELECT_RIGHT) {
        const int dir = move == KHDAYS_SELECT_RIGHT ? 1 : -1;
        for (int k = 1; k < count; ++k) {
            const int p = ((at + dir * k) % count + count) % count;
            if (may(p, user)) {
                return p;
            }
        }
        return at;
    }
    if (move == KHDAYS_SELECT_UP || move == KHDAYS_SELECT_DOWN) {
        const int dir = move == KHDAYS_SELECT_DOWN ? 1 : -1, cols = columns_of(g);
        int rows = 0;
        for (int p = 0; p < khdays_select_pages(g); ++p) {
            rows += (khdays_select_page_count(g, p) + cols - 1) / cols;
        }
        const int from = row_overall(g, at);
        const float x = khdays_select_x(g, at);
        for (int k = 1; k < rows; ++k) {
            int n = 0;
            const int first = row_start(g, ((from + dir * k) % rows + rows) % rows, &n);
            int best = -1;
            float best_d = 0.0f;
            for (int p = first; p >= 0 && p < first + n; ++p) {
                const float d = khdays_select_x(g, p) - x, dist = d < 0 ? -d : d;
                if (may(p, user) && (best < 0 || dist < best_d)) {
                    best = p;
                    best_d = dist;
                }
            }
            if (best >= 0) {
                return best;
            }
        }
        return at;
    }
    if (move == KHDAYS_SELECT_PREVIOUS_PAGE || move == KHDAYS_SELECT_NEXT_PAGE) {
        const int page = khdays_select_page_of(g, at) + (move == KHDAYS_SELECT_NEXT_PAGE ? 1 : -1);
        if (page < 0 || page >= khdays_select_pages(g)) {
            return at;
        }
        const int first = khdays_select_page_first(g, page), n = khdays_select_page_count(g, page);
        const int slot = at - khdays_select_page_first(g, khdays_select_page_of(g, at));
        const int aim = first + (slot < n ? slot : n - 1);
        /* the nearest the page lets be moved onto: after the aim first, then before it */
        for (int k = 0; k < n; ++k) {
            if (aim + k < first + n && may(aim + k, user)) {
                return aim + k;
            }
            if (k > 0 && aim - k >= first && may(aim - k, user)) {
                return aim - k;
            }
        }
        return at;
    }
    return at;
}
