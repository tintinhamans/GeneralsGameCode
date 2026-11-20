package com.generalsonline.stats.app;

import com.generalsonline.stats.Format;
import com.generalsonline.stats.proto.CatalogQuery;
import com.generalsonline.stats.proto.CatalogResult;
import com.generalsonline.stats.proto.PageInfo;
import com.generalsonline.stats.proto.PageRequest;

import java.util.ArrayList;
import java.util.List;

/**
 * Walks a catalog query page by page, forwards and back. The game's cursor is opaque and only goes forward, so the
 * cursors of the pages already seen are kept for the way back. No Swing; the catalog panel drives it.
 */
final class CatalogPager {
    private CatalogQuery filter = CatalogQuery.getDefaultInstance();
    private int pageSize = 25;
    private final List<Integer> cursors = new ArrayList<>();
    private int index = -1;
    private PageInfo last = PageInfo.getDefaultInstance();
    private int shown;

    /**
     * Begins a new search at the first page.
     *
     * @param filter the name, side, kind and buildable filters; its page is ignored
     * @param pageSize rows per page, 1 or more
     * @return the query for the first page
     */
    CatalogQuery start(CatalogQuery filter, int pageSize) {
        this.filter = filter.toBuilder().clearPage().build();
        this.pageSize = Math.max(1, pageSize);
        cursors.clear();
        cursors.add(0);
        index = 0;
        last = PageInfo.getDefaultInstance();
        shown = 0;
        return query();
    }

    private CatalogQuery query() {
        return filter.toBuilder().setPage(PageRequest.newBuilder().setCursor(cursors.get(index)).setPageSize(pageSize)).build();
    }

    /** Takes the answer of the page asked for last. */
    void accept(CatalogResult result) {
        last = result.getPage();
        shown = result.getEntriesCount();
    }

    boolean hasNext() {
        return index >= 0 && last.getHasMore();
    }

    boolean hasPrevious() {
        return index > 0;
    }

    /** The query for the page after the current one; call only when {@link #hasNext()}. */
    CatalogQuery next() {
        if (!hasNext()) {
            throw new IllegalStateException("no next page");
        }
        if (cursors.size() == index + 1) {
            cursors.add(last.getNextCursor());
        }
        index++;
        return query();
    }

    /** The query for the page before the current one; call only when {@link #hasPrevious()}. */
    CatalogQuery previous() {
        if (!hasPrevious()) {
            throw new IllegalStateException("no previous page");
        }
        index--;
        return query();
    }

    /** "page 2: rows 26 to 50 of 120", or "no templates match" / "no search yet". */
    String text() {
        if (index < 0) {
            return "no search yet";
        }
        long total = Format.u(last.getTotal());
        if (shown == 0) {
            return total == 0 ? "no templates match" : "no rows on this page";
        }
        long first = (long) index * pageSize + 1;
        return "page " + (index + 1) + ": rows " + first + " to " + (first + shown - 1) + (total > 0 ? " of " + total : (last.getHasMore() ? " (more follow)" : ""));
    }
}
