// Lists the game's object templates (the catalog) a page at a time, then looks one up by its internal name.
// The game answers from the templates it has loaded, so this also works outside a match.
//
// Run (Java 17+), with the library jar and protobuf-java on the class path:
//   java -cp stats-pipe-client-2.2.0.jar:protobuf-java-4.33.4.jar QueryCatalog.java
// (use ; instead of : between the jars on Windows)
// No game at hand? Serve a recorded dump as one with the test client:  stats-exporter-client serve match.dump
import com.generalsonline.stats.Format;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.CatalogEntry;
import com.generalsonline.stats.proto.CatalogQuery;
import com.generalsonline.stats.proto.CatalogResult;
import com.generalsonline.stats.proto.PageRequest;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.TimeUnit;

public class QueryCatalog {
    // The side to list and the template to look up.
    private static final String SIDE = "America";
    private static final String NAME = "AmericaTankCrusader";

    public static void main(String[] args) throws Exception {
        try (StatsPipe pipe = StatsPipe.builder().reconnect(false).build().start()) {
            if (pipe.awaitHello(Duration.ofSeconds(10)).isEmpty()) {
                System.err.println("no game found within 10 seconds");
                return;
            }

            // One page at a time. page_size 0 is the game's default; a size over Hello.max_page_size is lowered to it.
            // PageInfo says whether there is more, where the next page starts and (when known) how many rows match.
            int cursor = 0;
            boolean more = true;
            while (more) {
                CatalogQuery query = CatalogQuery.newBuilder().setSide(SIDE).setBuildableOnly(true)
                    .setPage(PageRequest.newBuilder().setCursor(cursor).setPageSize(25)).build();
                CatalogResult page = pipe.catalog(query).get(10, TimeUnit.SECONDS);
                for (CatalogEntry entry : page.getEntriesList()) {
                    System.out.println(Format.u(entry.getTemplateId()) + "  " + entry.getName() + "  cost " + Format.u(entry.getCost())
                        + "  " + String.join(",", entry.getKindOfList()));
                }
                System.out.println("-- " + page.getEntriesCount() + " templates of " + SIDE + ", " + Format.u(page.getPage().getTotal()) + " match");
                more = page.getPage().getHasMore();
                cursor = page.getPage().getNextCursor();
            }

            // catalogAll follows next_cursor for you (at most 100 pages), so a lookup by name is one call.
            List<CatalogEntry> byName = pipe.catalogAll(CatalogQuery.newBuilder().setName(NAME).build()).get(10, TimeUnit.SECONDS);
            if (byName.isEmpty()) {
                System.out.println(NAME + ": no such template");
            } else {
                CatalogEntry entry = byName.get(0);
                System.out.println(entry.getName() + ": " + entry.getDisplayName() + ", " + entry.getSide() + ", cost " + Format.u(entry.getCost())
                    + ", " + entry.getBuildTimeSeconds() + " s to build, " + (entry.getBuildable() ? "buildable" : "not buildable"));
            }
        }
    }
}
