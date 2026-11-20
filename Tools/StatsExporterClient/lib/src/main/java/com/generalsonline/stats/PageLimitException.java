package com.generalsonline.stats;

/** A paged query still had more pages when the page limit of the helper was reached. */
public final class PageLimitException extends Exception {
    private static final long serialVersionUID = 1L;

    /** How many pages were fetched. */
    private final int pages;

    /**
     * Creates the exception.
     *
     * @param pages how many pages were fetched
     */
    public PageLimitException(int pages) {
        super("the result still has more after " + pages + " pages; raise the page limit or filter the query");
        this.pages = pages;
    }

    /**
     * Returns the number of pages that were fetched.
     *
     * @return the limit that was reached
     */
    public int pages() {
        return pages;
    }
}
